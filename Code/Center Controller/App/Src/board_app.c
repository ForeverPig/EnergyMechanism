#include "board_app.h"
#include "center_service.h"
#include "boot_identity.h"
#include "motor_control_plan.h"
#include "reserved_features.h"
#include "can_bus.h"
#include "uart_link.h"
#include "mcp2515.h"
#include "fdcan.h"
#include "usart.h"
#include "spi.h"
#include "tim.h"
#include <string.h>
typedef struct
{
  uint8_t seen, status, uid_mask, uid[12];
  uint16_t adc_errors, adc_drops;
  uint32_t heartbeat, uid_start;
  SnapshotJoin join;
} Leaf;
typedef struct
{
  CanFrame frame;
  uint32_t received;
  uint8_t seen;
} Motor;
static CanBus bus[3];
static Mcp2515 mcp[2];
static UartLink serial;
static Leaf leaf[5];
static Motor motors[8];
static volatile uint32_t scheduler_ticks;
static uint8_t selected, command_seq, lease_port, motor_cursor;
static uint32_t last_status, last_lease, last_motor;
static CenterService service;
static bool online(unsigned port, uint32_t now)
{
  return leaf[port].seen && (uint32_t)(now - leaf[port].heartbeat) < 1000u;
}
static bool send_can(unsigned port, const CanFrame *f)
{
  return port < 3 ? CanBus_Send(&bus[port], f) : Mcp2515_Send(&mcp[port - 3], f);
}
static bool receive_can(unsigned port, CanFrame *f)
{
  return port < 3 ? CanBus_Receive(&bus[port], f) : Mcp2515_Receive(&mcp[port - 3], f);
}
static void ack(uint16_t seq, uint8_t op, uint8_t port, uint8_t result)
{
  uint8_t p[3] = {op, port, result};
  (void)UartLink_Send(&serial, DBG_ACK, seq, p, 3);
}
static CanFrame command(uint8_t seq, uint8_t op, uint8_t value)
{
  CanFrame f = {.id = CAN_COMMAND, .len = 8, .data = {DBG_VERSION, seq, op, value, 0, 0, 0, 0}};
  return f;
}
static bool service_send(void *ctx, const DebugPacket *p)
{
  (void)ctx;
  return UartLink_SendVersion(&serial, 2, p->type, p->seq, p->data, p->len);
}
static bool service_calibrate(void *ctx, uint8_t slot, uint8_t seq)
{
  (void)ctx;
  CanFrame f = command(seq, CAN_OP_CALIBRATE, 0);
  return send_can(slot - 1u, &f);
}
static void update_view(uint32_t now)
{
  CenterView v = {0};
  v.uart_errors = serial.errors + serial.rx_overruns + serial.parser.errors;
  v.uart_drops = serial.tx_drops;
  for (unsigned i = 0; i < 5; i++)
  {
    if (i < 3 ? (bus[i].ready && !bus[i].bus_off)
              : (mcp[i - 3].ready && !(mcp[i - 3].eflg & 0x20u)))
      v.can_ready |= (uint8_t)(1u << i);
    CenterLeaf *l = &v.leaf[i];
    l->online = (uint8_t)online(i, now);
    l->status = leaf[i].status;
    l->uid_valid = leaf[i].uid_mask == 3;
    memcpy(l->uid, leaf[i].uid, 12);
    l->adc_errors = leaf[i].adc_errors;
    l->adc_drops = leaf[i].adc_drops;
    l->age = leaf[i].seen ? now - leaf[i].heartbeat : UINT32_MAX;
    l->heartbeat = leaf[i].heartbeat;
    l->drops = i < 3 ? bus[i].rx_drops : mcp[i - 3].rx_overruns;
  }
  for (unsigned i = 0; i < 8; i++)
    if (motors[i].seen)
    {
      v.motor_known |= (uint8_t)(1u << i);
      if ((uint32_t)(now - motors[i].received) < 1000u)
      {
        v.motor_valid |= (uint8_t)(1u << i);
        int16_t rpm, current;
        uint16_t angle;
        uint8_t temp;
        if (MotorDecode(&motors[i].frame, &rpm, &current, &angle, &temp) && (rpm > 10 || rpm < -10))
          v.motor_moving |= (uint8_t)(1u << i);
      }
    }
  CenterService_View(&service, &v, now);
}
static void send_status(uint16_t seq, uint32_t now)
{
  uint8_t p[140] = {2, selected, 0, 0};
  WriteU32(p + 4, now);
  WriteU32(p + 8, serial.errors + serial.rx_overruns);
  WriteU32(p + 12, serial.tx_drops);
  WriteU32(p + 16, scheduler_ticks);
  for (unsigned i = 0; i < 5; i++)
  {
    if (i < 3 ? (bus[i].ready && !bus[i].bus_off)
              : (mcp[i - 3].ready && !(mcp[i - 3].eflg & 0x20u)))
      p[3] |= (uint8_t)(1u << i);
    uint8_t *r = p + 20 + i * 24;
    r[0] = (uint8_t)(i + 1);
    r[1] = (uint8_t)online(i, now);
    r[2] = leaf[i].status;
    r[3] = (leaf[i].uid_mask == 3);
    uint32_t age = leaf[i].seen ? now - leaf[i].heartbeat : 65535u;
    WriteU16(r + 4, (uint16_t)(age > 65535u ? 65535u : age));
    WriteU16(r + 6, leaf[i].adc_errors);
    WriteU16(r + 8, leaf[i].adc_drops);
    WriteU16(r + 10, (uint16_t)(i < 3 ? bus[i].rx_drops : mcp[i - 3].rx_overruns));
    memcpy(r + 12, leaf[i].uid, 12);
  }
  (void)UartLink_Send(&serial, DBG_STATUS, seq, p, sizeof(p));
}
static void on_frame(unsigned port, const CanFrame *f, uint32_t now)
{
  if (port == 0 && f->id >= 0x201 && f->id <= 0x208 && f->len == 8)
  {
    Motor *m = &motors[f->id - 0x201];
    m->frame = *f;
    m->received = now;
    m->seen = 1;
    return;
  }
  if (f->len != 8)
    return;
  Leaf *l = &leaf[port];
  if (f->id == CAN_HEARTBEAT && f->data[0] == DBG_VERSION)
  {
    l->seen = 1;
    l->heartbeat = now;
    l->status = f->data[1];
    l->adc_errors = ReadU16(f->data + 2);
    l->adc_drops = ReadU16(f->data + 4);
  }
  else if (f->id == CAN_IDENTITY && f->data[0] == DBG_VERSION)
  {
    uint8_t part = f->data[1];
    if (part == 0)
    {
      l->uid_mask = 1;
      l->uid_start = now;
      memcpy(l->uid, f->data + 2, 6);
    }
    else if (part == 1 && l->uid_mask == 1 && (uint32_t)(now - l->uid_start) < 300u)
    {
      memcpy(l->uid + 6, f->data + 2, 6);
      l->uid_mask = 3;
    }
  }
  else if (f->id == CAN_HIT)
  {
    if (f->data[1] < 1 || f->data[1] > 10)
      return;
    uint8_t p[12] = {(uint8_t)(port + 1)};
    WriteU16(p + 1, f->id);
    p[3] = 8;
    memcpy(p + 4, f->data, 8);
    if (!service.gateway)
      (void)UartLink_Send(&serial, DBG_CAN_EVENT, 0, p, sizeof(p));
    CenterService_Hit(&service, (uint8_t)(port + 1), f->data, now);
  }
  else if (f->id == CAN_ACK && f->data[0] == DBG_VERSION && f->data[2] == CAN_OP_CALIBRATE)
  {
    CenterService_CalAck(&service, (uint8_t)(port + 1), f->data[1], f->data[3], f->data[4], now);
  }
  else if (selected == port + 1u && online(port, now) && SnapshotAccept(&l->join, f, now))
  {
    CenterService_Pressure(&service, (uint8_t)(port + 1), &l->join.snapshot, now);
  }
}
static void on_command(const DebugPacket *p, uint32_t now)
{
  if (p->type != DBG_CMD)
    return;
  uint8_t op = p->len ? p->data[0] : 0;
  if (p->len != 3)
  {
    ack(p->seq, op, 0, RES_INVALID);
    return;
  }
  uint8_t port = p->data[1], value = p->data[2];
  if (op == CMD_STATUS)
  {
    if (port || value)
      ack(p->seq, op, port, RES_INVALID);
    else
      send_status(p->seq, now);
    return;
  }
  /* V1 reads remain available for the UART bench test. Mutations require V2 gates. */
  ack(p->seq, op, port, RES_UNSUPPORTED);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *h)
{
  if (h->Instance == TIM7)
    scheduler_ticks++;
}
void HAL_GPIO_EXTI_Callback(uint16_t pin)
{
  if (pin == INT1_Pin)
    mcp[0].irq = 1;
  if (pin == INT2_Pin)
    mcp[1].irq = 1;
}
void BoardApp_Init(void)
{
  /* No motor control initialization or current-command transmitter exists in this build. */
  LedOutput_Disable();
  if (!UartLink_Init(&serial, &huart1))
    Error_Handler();
  serial.allow_v2 = true;
  CenterService_Init(&service, BootIdentity_Create(),
                     (CenterBackend){service_send, service_calibrate, NULL});
  (void)CanBus_Init(&bus[0], &hfdcan1, true, true);
  (void)CanBus_Init(&bus[1], &hfdcan2, true, false);
  (void)CanBus_Init(&bus[2], &hfdcan3, true, false);
  mcp[0] = (Mcp2515){.spi = &hspi2,
                     .cs_port = SPI2_CS_GPIO_Port,
                     .cs_pin = SPI2_CS_Pin,
                     .reset_port = RESET2_GPIO_Port,
                     .reset_pin = RESET2_Pin,
                     .int_port = INT1_GPIO_Port,
                     .int_pin = INT1_Pin};
  mcp[1] = (Mcp2515){.spi = &hspi1,
                     .cs_port = SPI1_CS_GPIO_Port,
                     .cs_pin = SPI1_CS_Pin,
                     .reset_port = RESET1_GPIO_Port,
                     .reset_pin = RESET1_Pin,
                     .int_port = INT2_GPIO_Port,
                     .int_pin = INT2_Pin};
  (void)Mcp2515_Init(&mcp[0]);
  (void)Mcp2515_Init(&mcp[1]);
  if (HAL_TIM_Base_Start_IT(&htim7) != HAL_OK)
    Error_Handler();
  send_status(0, HAL_GetTick());
}
void BoardApp_Process(void)
{
  uint32_t now = HAL_GetTick();
  UartLink_Poll(&serial);
  for (unsigned i = 0; i < 3; i++)
    CanBus_Poll(&bus[i], now);
  for (unsigned i = 0; i < 2; i++)
    Mcp2515_Poll(&mcp[i], now);
  for (unsigned port = 0; port < 5; port++)
  {
    CanFrame f;
    unsigned budget = 8;
    while (budget-- && receive_can(port, &f))
      on_frame(port, &f, now);
    if (!online(port, now))
    {
      leaf[port].join.active = 0;
      leaf[port].uid_mask = 0;
    }
  }
  update_view(now);
  DebugPacket p;
  unsigned budget = 4;
  while (budget-- && UartLink_Receive(&serial, &p))
  {
    if (p.version == 2)
      CenterService_Request(&service, &p, now);
    else
      on_command(&p, now);
  }
  CenterService_Poll(&service, now);
  if (selected != service.selected)
  {
    selected = service.selected;
    last_lease = now - 500u;
    lease_port = 0;
    for (unsigned i = 0; i < 5; i++)
      leaf[i].join.active = 0;
  }
  if ((uint32_t)(now - last_lease) >= 500u)
  {
    CanFrame f = command(++command_seq, CAN_OP_STREAM, (uint8_t)(selected == lease_port + 1u));
    (void)send_can(lease_port, &f);
    if (++lease_port == 5u)
    {
      lease_port = 0;
      last_lease = now;
    }
  }
  if ((uint32_t)(now - last_status) >= 500u)
  {
    if (!service.gateway)
      send_status(0, now);
    last_status = now;
  }
  if ((uint32_t)(now - last_motor) >= 100u)
  {
    last_motor = now;
    unsigned i = motor_cursor;
    motor_cursor = (uint8_t)((motor_cursor + 1u) % 8u);
    if (motors[i].seen)
    {
      int16_t rpm, current;
      uint16_t angle;
      uint8_t temp;
      Motor *m = &motors[i];
      if (MotorDecode(&m->frame, &rpm, &current, &angle, &temp))
      {
        uint8_t pld[14] = {(uint8_t)(i + 1), temp};
        uint32_t age = now - m->received;
        WriteU16(pld + 2, angle);
        WriteU16(pld + 4, (uint16_t)rpm);
        WriteU16(pld + 6, (uint16_t)current);
        WriteU32(pld + 8, age);
        pld[12] = (age < 1000u);
        pld[13] = 0;
        if (!service.gateway)
          (void)UartLink_Send(&serial, DBG_MOTOR, 0, pld, sizeof(pld));
        CenterService_Motor(&service, pld);
      }
    }
  }
}
