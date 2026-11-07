#ifndef GATEWAY_CORE_H
#define GATEWAY_CORE_H
#include "comm_v2.h"
#define GW_BLE_CLIENTS V2_MAX_CLIENTS
#define GW_USB_CLIENT GW_BLE_CLIENTS
#define GW_CLIENTS (GW_BLE_CLIENTS + 1u)
#define GW_HISTORY 4u
#define GW_MAPS 8u
#define GW_CONTROL_DEPTH 12u
#define GW_EVENT_DEPTH 4u
typedef struct
{
  DebugPacket request, replies[6];
  uint8_t valid, pending, count;
} GatewayHistory;
typedef struct
{
  DebugPacket items[GW_CONTROL_DEPTH];
  uint8_t read, count;
} GatewayQueue;
typedef struct
{
  uint8_t bytes[194], active, index, count;
  uint16_t size, transfer;
} GatewayTx;
typedef struct
{
  uint8_t active, hello, subscribed, close, history_next;
  uint16_t handle, highwater, transfer[3];
  uint64_t session;
  uint32_t token, last_token, observer_ms, rate_ms, rate_count;
  V2Reassembly rx;
  GatewayHistory history[GW_HISTORY];
  GatewayQueue control, event;
  DebugPacket pressure, state;
  uint8_t pressure_ready, state_ready;
  GatewayTx tx[3];
} GatewayClient;
typedef struct
{
  DebugPacket wire;
  uint64_t session;
  uint32_t started, sent;
  uint16_t client_seq;
  uint8_t active, client, history, tries, timed_out, op;
} GatewayMap;
typedef struct
{
  bool (*uart)(void *ctx, const DebugPacket *packet);
  void *ctx;
} GatewayBackend;
typedef struct
{
  GatewayBackend io;
  GatewayClient client[GW_CLIENTS];
  GatewayMap maps[GW_MAPS];
  DebugPacket state;
  uint64_t epoch, boot, session_counter;
  uint8_t device[12], linked, selected, mode, state_valid;
  uint16_t uart_seq;
  uint32_t revision, selection_epoch, token_counter, last_rx, last_internal;
  uint32_t event_drops, frame_errors, timeouts, queue_disconnects;
} Gateway;
void Gateway_Init(Gateway *g, uint64_t epoch, const uint8_t device[12], GatewayBackend backend);
int Gateway_Connect(Gateway *g, uint16_t handle);
void Gateway_ConnectUsb(Gateway *g);
bool Gateway_NextPacket(Gateway *g, unsigned client, uint8_t channel, DebugPacket *out);
void Gateway_Disconnect(Gateway *g, unsigned client);
void Gateway_Subscribe(Gateway *g, unsigned client, uint8_t channel, bool enabled);
void Gateway_Info(const Gateway *g, uint8_t out[16]);
void Gateway_Command(Gateway *g, unsigned client, const DebugPacket *packet, uint32_t now);
void Gateway_Fragment(Gateway *g, unsigned client, const uint8_t *p, size_t n, uint32_t now);
void Gateway_Uart(Gateway *g, const DebugPacket *packet, uint32_t now);
void Gateway_Poll(Gateway *g, uint32_t now);
size_t Gateway_NextFragment(Gateway *g, unsigned client, uint8_t channel, uint8_t out[20]);
void Gateway_FragmentSent(Gateway *g, unsigned client, uint8_t channel);
#endif
