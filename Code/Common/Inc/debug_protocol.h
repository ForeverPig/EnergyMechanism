#ifndef DEBUG_PROTOCOL_H
#define DEBUG_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DBG_VERSION 1u
#define DBG_PAYLOAD_MAX 192u
#define DBG_FRAME_MAX (DBG_PAYLOAD_MAX + 10u)
#define DBG_CMD 1u
#define DBG_ACK 2u
#define DBG_STATUS 0x10u
#define DBG_CAN_EVENT 0x11u
#define DBG_SNAPSHOT 0x12u
#define DBG_MOTOR 0x14u
#define CMD_STATUS 1u
#define CMD_STREAM 2u
#define CMD_CALIBRATE 3u
#define RES_OK 0u
#define RES_INVALID 1u
#define RES_OFFLINE 2u
#define RES_BUSY 3u
#define RES_TIMEOUT 4u
#define RES_UNSUPPORTED 5u
#define CAN_COMMAND 0x500u
#define CAN_ACK 0x501u
#define CAN_HEARTBEAT 0x510u
#define CAN_IDENTITY 0x512u
#define CAN_HIT 0x513u
#define CAN_SAMPLE_A 0x520u
#define CAN_SAMPLE_B 0x530u
#define CAN_OP_STREAM 2u
#define CAN_OP_CALIBRATE 3u
#define SENSOR_COUNT 10u
typedef struct
{
  uint16_t id;
  uint8_t len;
  uint8_t data[8];
} CanFrame;
typedef struct
{
  uint16_t raw, baseline;
  int16_t delta;
  uint16_t peak;
  uint8_t state, ring;
} SensorValue;
typedef struct
{
  uint8_t seq;
  uint32_t timestamp;
  SensorValue values[SENSOR_COUNT];
} PressureSnapshot;
typedef struct
{
  uint8_t active;
  uint16_t mask_a, mask_b;
  uint32_t started;
  PressureSnapshot snapshot;
} SnapshotJoin;
typedef struct
{
  uint8_t type, version;
  uint16_t seq, len;
  uint8_t data[DBG_PAYLOAD_MAX];
} DebugPacket;
typedef struct
{
  uint8_t bytes[DBG_FRAME_MAX];
  uint16_t used;
  uint32_t last_ms, errors;
} FrameParser;
uint16_t ReadU16(const uint8_t *p);
uint32_t ReadU32(const uint8_t *p);
void WriteU16(uint8_t *p, uint16_t v);
void WriteU32(uint8_t *p, uint32_t v);
uint16_t DebugCrc(const uint8_t *p, size_t n);
size_t DebugEncode(uint8_t *out, uint8_t type, uint16_t seq, const uint8_t *data, uint16_t n);
bool DebugFeed(FrameParser *p, uint8_t byte, uint32_t now, DebugPacket *out);
size_t WireEncode(uint8_t *out, uint8_t version, uint8_t type, uint16_t seq, const uint8_t *data,
                  uint16_t n);
bool WireFeed(FrameParser *p, uint8_t byte, uint32_t now, DebugPacket *out, bool v2);
bool SnapshotAccept(SnapshotJoin *join, const CanFrame *frame, uint32_t now);
void SnapshotFrame(const PressureSnapshot *s, uint8_t part, CanFrame *out);
void SnapshotPack(const PressureSnapshot *s, uint8_t port, uint8_t out[106]);
bool MotorDecode(const CanFrame *f, int16_t *rpm, int16_t *current, uint16_t *angle, uint8_t *temp);
bool CanIdAllowed(bool center, uint16_t id);
#endif
