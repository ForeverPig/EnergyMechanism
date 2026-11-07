#include "mcp2515.h"
#include <string.h>
/* MCP2515 DS20001801K. 16 MHz: BRP=0, Sync=1, Prop=1, PS1=4, PS2=2 => 1 Mbps, SP=75%. */
static bool transfer(Mcp2515 *m, uint8_t *tx, uint8_t *rx, uint16_t n)
{
  HAL_GPIO_WritePin(m->cs_port, m->cs_pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef s = HAL_SPI_TransmitReceive(m->spi, tx, rx, n, 2);
  HAL_GPIO_WritePin(m->cs_port, m->cs_pin, GPIO_PIN_SET);
  if (s != HAL_OK)
  {
    m->io_errors++;
    m->ready = 0;
    return false;
  }
  return true;
}
static bool read_reg(Mcp2515 *m, uint8_t address, uint8_t *out, uint8_t n)
{
  uint8_t tx[16] = {3, address}, rx[16];
  if (n > 14 || !transfer(m, tx, rx, (uint16_t)(n + 2u)))
    return false;
  memcpy(out, rx + 2, n);
  return true;
}
static bool write_reg(Mcp2515 *m, uint8_t address, const uint8_t *data, uint8_t n)
{
  uint8_t tx[16] = {2, address}, rx[16];
  if (n > 14)
    return false;
  memcpy(tx + 2, data, n);
  return transfer(m, tx, rx, (uint16_t)(n + 2u));
}
static bool write_byte(Mcp2515 *m, uint8_t a, uint8_t v)
{
  return write_reg(m, a, &v, 1);
}
static bool modify(Mcp2515 *m, uint8_t a, uint8_t mask, uint8_t v)
{
  uint8_t tx[4] = {5, a, mask, v}, rx[4];
  return transfer(m, tx, rx, 4);
}
bool Mcp2515_Init(Mcp2515 *m)
{
  m->ready = 0;
  HAL_GPIO_WritePin(m->cs_port, m->cs_pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(m->reset_port, m->reset_pin, GPIO_PIN_RESET);
  HAL_Delay(1);
  HAL_GPIO_WritePin(m->reset_port, m->reset_pin, GPIO_PIN_SET);
  HAL_Delay(2);
  uint8_t stat;
  if (!read_reg(m, 0x0e, &stat, 1) || (stat & 0xe0u) != 0x80u)
    return false;
  const uint8_t timing[3] = {0x01, 0x98, 0x00};
  if (!write_reg(m, 0x28, timing, 3))
    return false;
  /* Match standard 0x500..0x53f; EID/data masks zero, MIDE=1 rejects extended frames. */
  const uint8_t mask[4] = {0xf8, 0x08, 0, 0}, filter[4] = {0xa0, 0, 0, 0};
  if (!write_reg(m, 0x20, mask, 4) || !write_reg(m, 0x24, mask, 4))
    return false;
  const uint8_t filters[6] = {0x00, 0x04, 0x08, 0x10, 0x14, 0x18};
  for (unsigned i = 0; i < 6; i++)
    if (!write_reg(m, filters[i], filter, 4))
      return false;
  if (!write_byte(m, 0x60, 0x04) || !write_byte(m, 0x70, 0) || !write_byte(m, 0x2c, 0) ||
      !write_byte(m, 0x2b, 3) || !write_byte(m, 0x0f, 0))
    return false;
  for (unsigned tries = 0; tries < 5; tries++)
  {
    if (!read_reg(m, 0x0e, &stat, 1))
      return false;
    if ((stat & 0xe0u) == 0)
    {
      m->ready = 1;
      return true;
    }
    HAL_Delay(1);
  }
  return false;
}
bool Mcp2515_Send(Mcp2515 *m, const CanFrame *f)
{
  if (!m->ready || !CanIdAllowed(true, f->id) || f->len > 8)
    return false;
  uint8_t ctrl;
  if (!read_reg(m, 0x30, &ctrl, 1) || (ctrl & 8u))
    return false;
  uint8_t tx[13] = {0};
  tx[0] = (uint8_t)(f->id >> 3);
  tx[1] = (uint8_t)(f->id << 5);
  tx[4] = f->len;
  memcpy(tx + 5, f->data, f->len);
  if (!write_reg(m, 0x31, tx, (uint8_t)(5u + f->len)))
    return false;
  uint8_t cmd = 0x81, reply;
  m->tx_since = HAL_GetTick();
  return transfer(m, &cmd, &reply, 1);
}
bool Mcp2515_Receive(Mcp2515 *m, CanFrame *f)
{
  if (!m->ready)
    return false;
  /* Poll the level as well as the EXTI hint: /INT may already be low at startup. */
  if (!m->irq && HAL_GPIO_ReadPin(m->int_port, m->int_pin) != GPIO_PIN_RESET)
    return false;
  m->irq = 0;
  uint8_t flags;
  if (!read_reg(m, 0x2c, &flags, 1) || !(flags & 3u))
    return false;
  uint8_t bit = (flags & 1u) ? 1u : 2u, reg = (flags & 1u) ? 0x60u : 0x70u, registers[14];
  if (!read_reg(m, reg, registers, 14))
    return false;
  const uint8_t *raw = registers + 1;
  if (!modify(m, 0x2c, bit, 0))
    return false;
  if ((registers[0] & 8u) || (raw[1] & 8u) || (raw[4] & 0x40u) || (raw[4] & 15u) > 8u)
    return false;
  f->id = (uint16_t)(((uint16_t)raw[0] << 3) | (raw[1] >> 5));
  f->len = raw[4] & 15u;
  memcpy(f->data, raw + 5, f->len);
  return true;
}
void Mcp2515_Poll(Mcp2515 *m, uint32_t now)
{
  if (!m->ready || (uint32_t)(now - m->last_health) < 100u)
    return;
  m->last_health = now;
  if (!read_reg(m, 0x2d, &m->eflg, 1))
    return;
  if (m->eflg & 0xc0u)
  {
    m->rx_overruns++;
    (void)modify(m, 0x2d, 0xc0, 0);
  }
  uint8_t ctrl;
  if (read_reg(m, 0x30, &ctrl, 1) && (ctrl & 8u) && (uint32_t)(now - m->tx_since) >= 100u)
  {
    (void)modify(m, 0x30, 8, 0);
    m->tx_aborts++;
  }
}
