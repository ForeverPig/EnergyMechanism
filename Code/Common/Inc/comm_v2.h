#ifndef COMM_V2_H
#define COMM_V2_H
#include "debug_protocol.h"
#define V2_VERSION 2u
#define V2_BLE_MAX 184u
#define V2_MAX_CLIENTS 3u
#define V2_FRAGMENT_MS 1000u
#define V2_OBSERVER_MS 3000u
#define V2_UPSTREAM_MS 1500u
#define V2_LINK_MS 1500u
#define V2_RETRY_MS 500u
#define V2_REQUEST_MS 6000u
#define V2_RESULT_LEN 36u
#define V2_STATE_LEN 48u
#define V2_LEAF_LEN 44u
#define V2_SESSION_LEN 56u
#define V2_REQUEST_HEADER 13u
enum
{
  V2_REQUEST = 1,
  V2_RESULT = 2,
  V2_SESSION = 3,
  V2_STATE = 0x10,
  V2_LEAF = 0x11,
  V2_PRESSURE = 0x12,
  V2_HIT = 0x13,
  V2_MOTOR = 0x14
};
enum
{
  V2_HELLO = 1,
  V2_GET_STATE = 2,
  V2_GET_LEAVES = 3,
  V2_PATCH = 0x10,
  V2_START = 0x11,
  V2_STOP = 0x12,
  V2_ENTER_DEBUG = 0x13,
  V2_EXIT_DEBUG = 0x14,
  V2_OBSERVE_ENTER = 0x20,
  V2_OBSERVE_RENEW = 0x21,
  V2_OBSERVE_LEAVE = 0x22,
  V2_CALIBRATE = 0x23,
  V2_EVENT_ACK = 0x30,
  V2_REPLAY = 0x31,
  V2_QUERY = 0x32
};
enum
{
  V2_ACCEPTED = 0,
  V2_COMPLETED = 1,
  V2_REJECTED = 2,
  V2_FAILED = 3,
  V2_UNKNOWN = 4
};
enum
{
  V2_OK = 0,
  V2_BAD_ARGUMENT = 1,
  V2_UNSUPPORTED = 2,
  V2_NOT_READY = 3,
  V2_WRONG_MODE = 4,
  V2_NOT_STOPPED = 5,
  V2_LEAF_OFFLINE = 6,
  V2_BUSY = 7,
  V2_RATE_LIMIT = 8,
  V2_STALE_STATE = 9,
  V2_ALREADY_RUNNING = 10,
  V2_TIMEOUT = 11,
  V2_LINK_DOWN = 12,
  V2_REQUEST_CONFLICT = 13,
  V2_REPLAY_EXPIRED = 14,
  V2_CACHE_GAP = 15,
  V2_FAULT_ACTIVE = 16
};
enum
{
  V2_IDLE = 0,
  V2_HIT_TEST = 5
};
enum
{
  V2_CAP_STATE = 1u,
  V2_CAP_LEAVES = 2u,
  V2_CAP_DEBUG = 4u,
  V2_CAP_PRESSURE = 8u,
  V2_CAP_CALIBRATE = 16u,
  V2_CAP_MOTOR_READ = 32u,
  V2_CAP_DIAGNOSTIC_HIT = 64u
};
#define V2_CAPABILITIES 127u
enum
{
  V2_CONTROL_CHANNEL = 0,
  V2_EVENT_CHANNEL = 1,
  V2_PRESSURE_CHANNEL = 2
};
typedef struct
{
  uint8_t bytes[194], active, count, next;
  uint16_t transfer, used;
  uint32_t started, errors;
} V2Reassembly;
uint64_t V2Read64(const uint8_t *p);
void V2Write64(uint8_t *p, uint64_t v);
bool V2Decode(const uint8_t *p, size_t n, DebugPacket *out);
int V2FragmentFeed(V2Reassembly *r, const uint8_t *p, size_t n, uint32_t now, DebugPacket *out);
size_t V2Fragment(const uint8_t *frame, size_t size, uint16_t transfer, uint8_t index,
                  uint8_t out[20]);
bool V2Terminal(const DebugPacket *p, uint8_t op);
void V2MakeResult(DebugPacket *p, uint16_t seq, uint8_t op, uint8_t phase, uint8_t code,
                  uint64_t boot, uint32_t revision);
#endif
