/* External wiring test: CAN1/2/5 share one bus, CAN3/4 share the other.
 * Explicit CMake opt-in replaces board_app.c. Never sends motor control IDs.
 * All controllers use NORMAL mode, never internal/external controller loopback.
 * Arm through SWD by writing g_can_test.start=1; completion stops all traffic.
 */
#include "board_app.h"
#include "can_bus.h"
#include "fdcan.h"
#include "mcp2515.h"
#include "reserved_features.h"
#include "spi.h"
#include <string.h>

typedef struct
{
  uint32_t magic, version, state, start, frames_per_port, gap_ms;
  uint32_t ready_mask, core_hz, fdcan_hz, started_ms, elapsed_ms;
  uint32_t tx[5], tx_busy[5], rx[5][5]; /* rx[receiver][sender], zero-based */
  uint32_t invalid[5], unexpected[5], sequence_errors[5], rx_drops[5], tx_aborts[5];
  uint32_t fdcan_psr[3], fdcan_ecr[3], fdcan_error_log[3];
  uint32_t max_tec[5], max_rec[5], bus_off[5], mcp_io_errors[2];
  uint32_t mcp_canstat[2], mcp_cnf[2], mcp_eflg[2], mcp_txctrl[2];
  uint32_t deadline, config_error;
} CanTestReport;

volatile CanTestReport g_can_test;
static CanBus bus[3];
static Mcp2515 mcp[2];
static uint32_t next_seq[5][5], last_send, last_health, drain_since;
static unsigned next_port;

/* Stable symbol for a hardware breakpoint; no semihosting/UART dependency. */
__attribute__((noinline)) void CanSelfTest_Done(void) { __NOP(); }

static bool linked(unsigned receiver, unsigned sender)
{
  static const uint8_t group[5] = {0, 0, 1, 1, 0};
  return receiver != sender && group[receiver] == group[sender];
}

static CanFrame make_frame(unsigned sender, uint32_t seq)
{
  /* Invalid protocol version, so a leaf cannot interpret this as a command. */
  CanFrame f = {.id = CAN_COMMAND, .len = 8,
                .data = {0xa7, (uint8_t)(sender + 1), (uint8_t)seq,
                         (uint8_t)(seq >> 8), (uint8_t)(seq ^ 0x5a),
                         (uint8_t)((seq >> 3) ^ 0xa5), (uint8_t)(0xf0u | sender), 0}};
  for (unsigned i = 0; i < 7; i++)
    f.data[7] ^= f.data[i];
  return f;
}

static bool read_mcp(unsigned i, uint8_t reg, uint8_t *out, unsigned n)
{
  uint8_t tx[16] = {3, reg}, rx[16] = {0};
  Mcp2515 *m = &mcp[i];
  HAL_GPIO_WritePin(m->cs_port, m->cs_pin, GPIO_PIN_RESET);
  HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(m->spi, tx, rx, (uint16_t)(n + 2), 2);
  HAL_GPIO_WritePin(m->cs_port, m->cs_pin, GPIO_PIN_SET);
  if (status != HAL_OK)
  {
    m->io_errors++;
    return false;
  }
  memcpy(out, rx + 2, n);
  return true;
}

static void health(void)
{
  for (unsigned i = 0; i < 3; i++)
  {
    FDCAN_ProtocolStatusTypeDef p = {0};
    FDCAN_ErrorCountersTypeDef e = {0};
    g_can_test.fdcan_psr[i] = bus[i].can->Instance->PSR;
    g_can_test.fdcan_ecr[i] = bus[i].can->Instance->ECR;
    (void)HAL_FDCAN_GetProtocolStatus(bus[i].can, &p);
    /* Reading ECR clears CEL; use the captured register, not another ECR read. */
    e.TxErrorCnt = g_can_test.fdcan_ecr[i] & 0xffu;
    e.RxErrorCnt = (g_can_test.fdcan_ecr[i] >> 8) & 0x7fu;
    g_can_test.fdcan_error_log[i] += (g_can_test.fdcan_ecr[i] >> 16) & 0xffu;
    if (e.TxErrorCnt > g_can_test.max_tec[i]) g_can_test.max_tec[i] = e.TxErrorCnt;
    if (e.RxErrorCnt > g_can_test.max_rec[i]) g_can_test.max_rec[i] = e.RxErrorCnt;
    if (p.BusOff) g_can_test.bus_off[i]++;
    g_can_test.rx_drops[i] = bus[i].rx_drops;
    g_can_test.tx_aborts[i] = bus[i].tx_aborts;
  }
  for (unsigned i = 0; i < 2; i++)
  {
    uint8_t stat = 0xff, timing[3] = {0}, errors[2] = {0}, flags = 0xff, ctrl = 0xff;
    (void)read_mcp(i, 0x0e, &stat, 1);
    (void)read_mcp(i, 0x28, timing, 3);
    (void)read_mcp(i, 0x1c, errors, 2);
    (void)read_mcp(i, 0x2d, &flags, 1);
    (void)read_mcp(i, 0x30, &ctrl, 1);
    g_can_test.mcp_canstat[i] = stat;
    g_can_test.mcp_cnf[i] = ((uint32_t)timing[2] << 16) | ((uint32_t)timing[1] << 8) | timing[0];
    g_can_test.mcp_eflg[i] = flags;
    g_can_test.mcp_txctrl[i] = ctrl;
    if (errors[0] > g_can_test.max_tec[i + 3]) g_can_test.max_tec[i + 3] = errors[0];
    if (errors[1] > g_can_test.max_rec[i + 3]) g_can_test.max_rec[i + 3] = errors[1];
    if (flags & 0x20u) g_can_test.bus_off[i + 3]++;
    g_can_test.mcp_io_errors[i] = mcp[i].io_errors;
    g_can_test.rx_drops[i + 3] = mcp[i].rx_overruns;
    g_can_test.tx_aborts[i + 3] = mcp[i].tx_aborts;
  }
}

static void receive(unsigned port, const CanFrame *f)
{
  if (f->id != CAN_COMMAND || f->len != 8 || f->data[0] != 0xa7 ||
      f->data[1] < 1 || f->data[1] > 5)
  {
    g_can_test.invalid[port]++;
    return;
  }
  unsigned sender = f->data[1] - 1u;
  uint32_t seq = (uint32_t)f->data[2] | ((uint32_t)f->data[3] << 8);
  CanFrame expected = make_frame(sender, seq);
  if (seq >= g_can_test.frames_per_port || memcmp(f->data, expected.data, 8))
  {
    g_can_test.invalid[port]++;
    return;
  }
  if (!linked(port, sender)) g_can_test.unexpected[port]++;
  if (seq != next_seq[port][sender]) g_can_test.sequence_errors[port]++;
  next_seq[port][sender] = seq + 1;
  g_can_test.rx[port][sender]++;
}

static void finish(void)
{
  health();
  bool ok = g_can_test.ready_mask == 31u && !g_can_test.deadline && !g_can_test.config_error;
  for (unsigned i = 0; i < 5; i++)
  {
    if (g_can_test.tx[i] != g_can_test.frames_per_port || g_can_test.invalid[i] ||
        g_can_test.unexpected[i] || g_can_test.sequence_errors[i] || g_can_test.rx_drops[i] ||
        g_can_test.tx_aborts[i] || g_can_test.bus_off[i] || g_can_test.max_tec[i] ||
        g_can_test.max_rec[i]) ok = false;
    for (unsigned j = 0; j < 5; j++)
      if (g_can_test.rx[i][j] != (linked(i, j) ? g_can_test.frames_per_port : 0u)) ok = false;
  }
  for (unsigned i = 0; i < 3; i++)
    if (g_can_test.fdcan_error_log[i]) ok = false;
  for (unsigned i = 0; i < 2; i++)
    if (g_can_test.mcp_io_errors[i] || g_can_test.mcp_eflg[i] ||
        (g_can_test.mcp_canstat[i] & 0xe0u) || (g_can_test.mcp_txctrl[i] & 0x78u)) ok = false;
  /* Stop queued traffic on timeout/fault, before exposing completion to SWD. */
  for (unsigned i = 0; i < 3; i++)
    (void)HAL_FDCAN_AbortTxRequest(bus[i].can, 7);
  for (unsigned i = 0; i < 2; i++)
  {
    uint8_t tx[4] = {5, 0x30, 8, 0}, rx[4];
    HAL_GPIO_WritePin(mcp[i].cs_port, mcp[i].cs_pin, GPIO_PIN_RESET);
    (void)HAL_SPI_TransmitReceive(mcp[i].spi, tx, rx, 4, 2);
    HAL_GPIO_WritePin(mcp[i].cs_port, mcp[i].cs_pin, GPIO_PIN_SET);
  }
  g_can_test.state = ok ? 3u : 4u;
  CanSelfTest_Done();
}

void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == INT1_Pin) mcp[0].irq = 1;
  if (pin == INT2_Pin) mcp[1].irq = 1;
}

void BoardApp_Init(void)
{
  LedOutput_Disable();
  memset((void *)&g_can_test, 0, sizeof(g_can_test));
  g_can_test.magic = 0x43414e35; /* CAN5 */
  g_can_test.version = 1;
  g_can_test.frames_per_port = 1000;
  g_can_test.gap_ms = 5;
  g_can_test.core_hz = HAL_RCC_GetHCLKFreq();
  g_can_test.fdcan_hz = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_FDCAN);
  FDCAN_HandleTypeDef *handles[3] = {&hfdcan1, &hfdcan2, &hfdcan3};
  for (unsigned i = 0; i < 3; i++)
  {
    /* Leaf filter receives 0x500. Center TX policy still permits ONLY 0x500. */
    if (CanBus_Init(&bus[i], handles[i], false, false)) g_can_test.ready_mask |= 1u << i;
    bus[i].center = 1;
  }
  mcp[0] = (Mcp2515){.spi = &hspi2, .cs_port = SPI2_CS_GPIO_Port, .cs_pin = SPI2_CS_Pin,
                     .reset_port = RESET2_GPIO_Port, .reset_pin = RESET2_Pin,
                     .int_port = INT1_GPIO_Port, .int_pin = INT1_Pin};
  mcp[1] = (Mcp2515){.spi = &hspi1, .cs_port = SPI1_CS_GPIO_Port, .cs_pin = SPI1_CS_Pin,
                     .reset_port = RESET1_GPIO_Port, .reset_pin = RESET1_Pin,
                     .int_port = INT2_GPIO_Port, .int_pin = INT2_Pin};
  for (unsigned i = 0; i < 2; i++)
    if (Mcp2515_Init(&mcp[i])) g_can_test.ready_mask |= 1u << (i + 3);
  health();
  g_can_test.state = 1;
}

void BoardApp_Process(void)
{
  uint32_t now = HAL_GetTick();
  if (g_can_test.state == 1)
  {
    if (!g_can_test.start) return;
    g_can_test.start = 0;
    if (!g_can_test.frames_per_port || g_can_test.frames_per_port > 10000 ||
        !g_can_test.gap_ms || g_can_test.gap_ms > 20) g_can_test.config_error = 1;
    g_can_test.started_ms = last_send = now;
    g_can_test.state = 2;
    if (g_can_test.ready_mask != 31u || g_can_test.config_error) finish();
    return;
  }
  if (g_can_test.state != 2) return;
  g_can_test.elapsed_ms = now - g_can_test.started_ms;
  for (unsigned i = 0; i < 3; i++) CanBus_Poll(&bus[i], now);
  for (unsigned i = 0; i < 2; i++) Mcp2515_Poll(&mcp[i], now);
  for (unsigned i = 0; i < 5; i++)
  {
    CanFrame f;
    unsigned budget = 16;
    while (budget-- && (i < 3 ? CanBus_Receive(&bus[i], &f) : Mcp2515_Receive(&mcp[i - 3], &f)))
      receive(i, &f);
  }
  if (now - last_health >= 10u) { last_health = now; health(); }
  if (g_can_test.elapsed_ms > g_can_test.frames_per_port * 5u * g_can_test.gap_ms + 5000u)
  {
    g_can_test.deadline = 1;
    finish();
    return;
  }
  bool all_sent = true;
  for (unsigned i = 0; i < 5; i++)
    if (g_can_test.tx[i] < g_can_test.frames_per_port) all_sent = false;
  if (all_sent)
  {
    if (!drain_since) drain_since = now;
    if (now - drain_since >= 200u) finish();
    return;
  }
  if (now - last_send < g_can_test.gap_ms) return;
  last_send = now;
  unsigned port = next_port;
  next_port = (next_port + 1u) % 5u;
  if (g_can_test.tx[port] >= g_can_test.frames_per_port) return;
  CanFrame f = make_frame(port, g_can_test.tx[port]);
  bool sent = port < 3 ? CanBus_Send(&bus[port], &f) : Mcp2515_Send(&mcp[port - 3], &f);
  if (sent) g_can_test.tx[port]++;
  else g_can_test.tx_busy[port]++;
}
