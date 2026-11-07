#include "board_app.h"
#include "pressure_sampler.h"
#include "hit_detector.h"
#include "reserved_features.h"
#include "can_bus.h"
#include "uart_link.h"
#include "fdcan.h"
#include "usart.h"
#include <string.h>
static const uint8_t rings[10] = {6, 7, 8, 9, 10, 5, 4, 3, 1, 2};
static HitDetector detector;
static HitDetectorConfig config;
static PressureBlock block;
static UartLink serial;
static CanBus bus;
static PressureSnapshot snapshot;
static uint32_t sample_ms, stream_lease, last_snapshot, last_heartbeat, last_uid, last_recovery,
    hit_drops;
static uint8_t sample_div, local_stream, can_stream, snapshot_part = 20, hit_seq, snapshot_seq;
static uint8_t uid_part;
static uint32_t uid[3];
static CanFrame pending[16];
static uint8_t wr, rd;
static uint8_t cached_seq, cached_op, cached_result, cache_valid;
static uint32_t cached_ms;
static uint16_t local_seq;
static uint8_t local_op, local_result, local_cache;
static uint32_t local_ms;
static uint8_t status(void)
{
  return (uint8_t)(HitDetector_GetCalibrationStatus(&detector) |
                   (PressureSampler_Faulted() ? 0x80u : 0u));
}
static void forward(const CanFrame *f)
{
  uint8_t p[12] = {0};
  WriteU16(p + 1, f->id);
  p[3] = f->len;
  memcpy(p + 4, f->data, f->len);
  (void)UartLink_Send(&serial, DBG_CAN_EVENT, 0, p, sizeof(p));
}
static void enqueue(const CanFrame *f)
{
  uint8_t next = (uint8_t)((wr + 1u) % 16u);
  if (next == rd)
  {
    hit_drops++;
    return;
  }
  pending[wr] = *f;
  wr = next;
}
static bool recalibrate(void)
{
  snapshot_part = 20;
  rd = wr;
  HitDetector_Reset(&detector);
  sample_ms = HAL_GetTick();
  sample_div = 0;
  bool ok = PressureSampler_Start();
  last_recovery = HAL_GetTick();
  return ok;
}
static void can_command(const CanFrame *f, uint32_t now)
{
  if (f->id != CAN_COMMAND || f->len != 8 || f->data[0] != DBG_VERSION)
    return;
  uint8_t seq = f->data[1], op = f->data[2], value = f->data[3], result = RES_OK;
  if (op == CAN_OP_STREAM)
  {
    if (value > 1)
      result = RES_INVALID;
    else
    {
      can_stream = value;
      stream_lease = now;
      if (!value)
        snapshot_part = 20;
    }
  }
  else if (op == CAN_OP_CALIBRATE)
  {
    if (value != 0)
      result = RES_INVALID;
    else if (cache_valid && seq == cached_seq && op == cached_op &&
             (uint32_t)(now - cached_ms) < 2000u)
      result = cached_result;
    else
      result = recalibrate() ? RES_OK : RES_OFFLINE;
  }
  else
    result = RES_UNSUPPORTED;
  if (op == CAN_OP_CALIBRATE)
  {
    cached_seq = seq;
    cached_op = op;
    cached_result = result;
    cached_ms = now;
    cache_valid = 1;
  }
  CanFrame ack = {.id = CAN_ACK, .len = 8, .data = {DBG_VERSION, seq, op, result, 0, 0, 0, 0}};
  ack.data[4] = status();
  enqueue(&ack);
}
static void send_status(uint16_t seq)
{
  uint8_t p[28] = {1, status(), bus.ready, bus.bus_off};
  WriteU32(p + 4, HAL_GetTick());
  WriteU32(p + 8, PressureSampler_Errors());
  WriteU32(p + 12, PressureSampler_Drops());
  WriteU32(p + 16, serial.errors + serial.rx_overruns);
  WriteU32(p + 20, serial.tx_drops);
  WriteU32(p + 24, hit_drops);
  (void)UartLink_Send(&serial, DBG_STATUS, seq, p, sizeof(p));
}
static void local_command(const DebugPacket *p, uint32_t now)
{
  if (p->type != DBG_CMD)
    return;
  uint8_t op = p->len ? p->data[0] : 0, result = RES_OK;
  if (p->len != 3 || p->data[1] != 0)
    result = RES_INVALID;
  else if (op == CMD_STATUS)
  {
    send_status(p->seq);
    return;
  }
  else if (op == CMD_STREAM)
  {
    if (p->data[2] > 1)
      result = RES_INVALID;
    else
      local_stream = p->data[2];
  }
  else if (op == CMD_CALIBRATE)
  {
    if (p->data[2] != 0)
      result = RES_INVALID;
    else if (local_cache && p->seq == local_seq && op == local_op &&
             (uint32_t)(now - local_ms) < 2000u)
      result = local_result;
    else
      result = recalibrate() ? RES_OK : RES_OFFLINE;
  }
  else
    result = RES_UNSUPPORTED;
  local_cache = 1;
  local_seq = p->seq;
  local_op = op;
  local_result = result;
  local_ms = now;
  uint8_t ack[3] = {op, 0, result};
  (void)UartLink_Send(&serial, DBG_ACK, p->seq, ack, 3);
}
void BoardApp_Init(void)
{
  LedOutput_Disable();
  HitDetector_Init(&detector, rings);
  HitDetector_DefaultConfig(&config);
  uid[0] = HAL_GetUIDw0();
  uid[1] = HAL_GetUIDw1();
  uid[2] = HAL_GetUIDw2();
  if (!UartLink_Init(&serial, &huart2))
    Error_Handler();
  (void)CanBus_Init(&bus, &hfdcan2, false, false);
  (void)recalibrate();
  send_status(0);
}
void BoardApp_Process(void)
{
  uint32_t now = HAL_GetTick();
  LedOutput_Disable();
  UartLink_Poll(&serial);
  CanBus_Poll(&bus, now);
  if (PressureSampler_Faulted() && (uint32_t)(now - last_recovery) >= 1000u)
    (void)recalibrate();
  unsigned budget = 4;
  while (budget-- && PressureSampler_Take(&block))
  {
    for (unsigned f = 0; f < SAMPLE_HALF_FRAMES; f++)
    {
      HitEvent hit;
      uint8_t ready;
      HitDetector_Process(&detector, &config, block.samples[f], sample_ms, &hit, &ready);
      if (++sample_div == 10u)
      {
        sample_div = 0;
        sample_ms++;
      }
      if (ready)
      {
        CanFrame msg = {.id = CAN_HIT, .len = 8};
        msg.data[0] = ++hit_seq;
        msg.data[1] = hit.ring;
        WriteU16(msg.data + 2, hit.peak);
        WriteU32(msg.data + 4, hit.timestamp_ms);
        enqueue(&msg);
        forward(&msg);
      }
    }
  }
  CanFrame in;
  budget = 8;
  while (budget-- && CanBus_Receive(&bus, &in))
    can_command(&in, now);
  DebugPacket packet;
  budget = 4;
  while (budget-- && UartLink_Receive(&serial, &packet))
    local_command(&packet, now);
  if (can_stream && (uint32_t)(now - stream_lease) > 1500u)
  {
    can_stream = 0;
    snapshot_part = 20;
  }
  if ((uint32_t)(now - last_snapshot) >= 50u)
  {
    last_snapshot = now;
    if (can_stream || local_stream)
    {
      HitSensorSnapshot v[10];
      HitDetector_GetSnapshot(&detector, v);
      snapshot.seq = ++snapshot_seq;
      snapshot.timestamp = sample_ms;
      for (unsigned i = 0; i < 10; i++)
      {
        snapshot.values[i] = (SensorValue){
            v[i].raw, (uint16_t)v[i].baseline, v[i].delta, v[i].peak, v[i].state, rings[i]};
      }
      if (local_stream)
      {
        uint8_t payload[106];
        SnapshotPack(&snapshot, 0, payload);
        (void)UartLink_Send(&serial, DBG_SNAPSHOT, 0, payload, sizeof(payload));
      }
      snapshot_part = can_stream ? 0 : 20; /* A stalled old snapshot is discarded as a unit. */
    }
  }
  if (rd != wr)
  {
    if (CanBus_Send(&bus, &pending[rd]))
      rd = (uint8_t)((rd + 1u) % 16u);
  }
  else if ((uint32_t)(now - last_heartbeat) >= 200u)
  {
    CanFrame hb = {.id = CAN_HEARTBEAT, .len = 8, .data = {DBG_VERSION, status()}};
    WriteU16(hb.data + 2, (uint16_t)PressureSampler_Errors());
    WriteU16(hb.data + 4, (uint16_t)PressureSampler_Drops());
    WriteU16(hb.data + 6, (uint16_t)(now / 1000u));
    (void)CanBus_Send(&bus, &hb);
    forward(&hb);
    last_heartbeat = now;
  }
  else if ((uint32_t)(now - last_uid) >= 1000u)
  {
    CanFrame hello = {.id = CAN_IDENTITY, .len = 8, .data = {DBG_VERSION, uid_part}};
    memcpy(hello.data + 2, (uint8_t *)uid + 6u * uid_part, 6);
    if (CanBus_Send(&bus, &hello))
    {
      if (++uid_part == 2)
      {
        uid_part = 0;
        last_uid = now;
      }
    }
  }
  else if (snapshot_part < 20)
  {
    CanFrame f;
    SnapshotFrame(&snapshot, snapshot_part, &f);
    if (CanBus_Send(&bus, &f))
      snapshot_part++;
  }
}
