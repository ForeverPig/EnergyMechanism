#ifndef CENTER_SERVICE_H
#define CENTER_SERVICE_H
#include "comm_v2.h"
#define CENTER_CACHE 8u
#define CENTER_REPLIES 6u
typedef struct
{
  uint8_t online, status, uid_valid, uid[12];
  uint16_t adc_errors, adc_drops;
  uint32_t age, drops, heartbeat;
} CenterLeaf;
typedef struct
{
  CenterLeaf leaf[5];
  uint8_t can_ready, motor_valid, motor_moving, motor_known;
  uint32_t uart_errors, uart_drops;
} CenterView;
typedef struct
{
  DebugPacket request, reply[CENTER_REPLIES];
  uint64_t epoch;
  uint8_t valid, count, next, pending;
} CenterRequest;
typedef struct
{
  bool (*send)(void *ctx, const DebugPacket *p);
  bool (*calibrate)(void *ctx, uint8_t slot, uint8_t can_seq);
  void *ctx;
} CenterBackend;
typedef struct
{
  CenterBackend io;
  CenterView view;
  CenterRequest cache[CENTER_CACHE];
  uint64_t boot, gateway, retired[8];
  uint16_t highwater;
  uint32_t revision, state_revision, selection_epoch, lease_ms, last_state;
  uint32_t startup_ms, stop_since, cal_start, cal_sent, cal_ack_ms, snapshot_seq, hit_seq;
  uint8_t view_started, mode, selected, stopped, stop_tracking, retired_next, cache_next;
  uint8_t cal_index, cal_slot, cal_seq, cal_tries, cal_ack, cal_running;
  DebugPacket pressure;
  uint8_t pressure_ready;
} CenterService;
void CenterService_Init(CenterService *s, uint64_t boot, CenterBackend backend);
void CenterService_View(CenterService *s, const CenterView *v, uint32_t now);
void CenterService_Request(CenterService *s, const DebugPacket *uart, uint32_t now);
void CenterService_Poll(CenterService *s, uint32_t now);
void CenterService_CalAck(CenterService *s, uint8_t slot, uint8_t seq, uint8_t result,
                          uint8_t status, uint32_t now);
void CenterService_Pressure(CenterService *s, uint8_t slot, const PressureSnapshot *p,
                            uint32_t now);
void CenterService_Hit(CenterService *s, uint8_t slot, const uint8_t can_data[8], uint32_t now);
void CenterService_Motor(CenterService *s, const uint8_t v1[14]);
#endif
