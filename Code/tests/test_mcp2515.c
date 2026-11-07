#include "mcp2515.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[256];
static uint32_t now;
static unsigned transactions;
static int failure;
static GPIO_TypeDef port;
static SPI_HandleTypeDef spi;
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, GPIO_PinState state)
{
  (void)p;
  if (pin == 2 && state == GPIO_PIN_RESET)
  {
    memset(regs, 0, sizeof(regs));
    regs[0x0e] = 0x80;
  }
}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin)
{
  (void)p;
  (void)pin;
  return (regs[0x2c] & 3) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
void HAL_Delay(uint32_t ms)
{
  now += ms;
}
uint32_t HAL_GetTick(void)
{
  return now;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *s, uint8_t *tx, uint8_t *rx,
                                          uint16_t n, uint32_t timeout)
{
  (void)s;
  (void)timeout;
  transactions++;
  if (failure)
    return HAL_ERROR;
  memset(rx, 0, n);
  if (tx[0] == 3)
  {
    for (unsigned i = 2; i < n; i++)
      rx[i] = regs[(uint8_t)(tx[1] + i - 2)];
  }
  else if (tx[0] == 2)
  {
    for (unsigned i = 2; i < n; i++)
      regs[(uint8_t)(tx[1] + i - 2)] = tx[i];
    if (tx[1] == 0x0f)
      regs[0x0e] = tx[2] & 0xe0;
  }
  else if (tx[0] == 5)
    regs[tx[1]] = (uint8_t)((regs[tx[1]] & ~tx[2]) | (tx[3] & tx[2]));
  else if (tx[0] == 0x81)
    regs[0x30] |= 8;
  else
    assert(0);
  return HAL_OK;
}
int main(void)
{
  Mcp2515 m = {.spi = &spi,
               .cs_port = &port,
               .reset_port = &port,
               .int_port = &port,
               .cs_pin = 1,
               .reset_pin = 2,
               .int_pin = 3};
  assert(Mcp2515_Init(&m) && m.ready);
  assert(regs[0x28] == 1 && regs[0x29] == 0x98 && regs[0x2a] == 0);
  assert(regs[0x20] == 0xf8 && regs[0x21] == 8 && regs[0] == 0xa0 && regs[0x60] == 4 &&
         regs[0x2b] == 3);
  CanFrame f = {.id = 0x200, .len = 8};
  unsigned before = transactions;
  assert(!Mcp2515_Send(&m, &f) && transactions == before);
  f.id = CAN_COMMAND;
  assert(Mcp2515_Send(&m, &f));
  assert(regs[0x31] == 0xa0 && regs[0x35] == 8 && (regs[0x30] & 8));
  assert(!Mcp2515_Send(&m, &f));
  now = 200;
  Mcp2515_Poll(&m, now);
  assert(!(regs[0x30] & 8) && m.tx_aborts == 1);
  /* Receive a hit even without an EXTI edge: the active-low interrupt level is checked. */
  regs[0x2c] = 1;
  regs[0x60] = 4;
  regs[0x61] = 0x513 >> 3;
  regs[0x62] = (0x513 & 7) << 5;
  regs[0x65] = 8;
  regs[0x66] = 99;
  assert(Mcp2515_Receive(&m, &f));
  assert(f.id == 0x513 && f.len == 8 && f.data[0] == 99 && !(regs[0x2c] & 1));
  regs[0x2c] = 1;
  regs[0x60] |= 8;
  assert(!Mcp2515_Receive(&m, &f)); /* standard RTR */
  failure = 1;
  assert(!Mcp2515_Send(&m, &(CanFrame){.id = CAN_COMMAND, .len = 8}));
  assert(!m.ready && m.io_errors);
  puts("PASS: actual MCP2515 register configuration, TX guard, timeout, active-low RX, RTR and SPI "
       "errors");
  return 0;
}
