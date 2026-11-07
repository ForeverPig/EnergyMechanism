#include "debug_protocol.h"
#include <string.h>
uint16_t ReadU16(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
uint32_t ReadU32(const uint8_t *p)
{
  return (uint32_t)ReadU16(p) | ((uint32_t)ReadU16(p + 2) << 16);
}
void WriteU16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
void WriteU32(uint8_t *p, uint32_t v)
{
  WriteU16(p, (uint16_t)v);
  WriteU16(p + 2, (uint16_t)(v >> 16));
}
uint16_t DebugCrc(const uint8_t *p, size_t n)
{
  uint16_t c = 0xffffu;
  while (n--)
  {
    c ^= (uint16_t)*p++ << 8;
    for (unsigned i = 0; i < 8; i++)
      c = (uint16_t)((c & 0x8000u) ? ((uint32_t)c << 1) ^ 0x1021u : (uint32_t)c << 1);
  }
  return c;
}
size_t WireEncode(uint8_t *out, uint8_t version, uint8_t type, uint16_t seq, const uint8_t *data,
                  uint16_t n)
{
  if (n > DBG_PAYLOAD_MAX)
    return 0;
  out[0] = 0xa5;
  out[1] = 0x5a;
  out[2] = version;
  out[3] = type;
  WriteU16(out + 4, n);
  WriteU16(out + 6, seq);
  if (n)
    memcpy(out + 8, data, n);
  WriteU16(out + 8 + n, DebugCrc(out + 2, 6u + n));
  return 10u + n;
}
size_t DebugEncode(uint8_t *out, uint8_t type, uint16_t seq, const uint8_t *data, uint16_t n)
{
  return WireEncode(out, DBG_VERSION, type, seq, data, n);
}
static void discard(FrameParser *p)
{
  memmove(p->bytes, p->bytes + 1, --p->used);
}
bool WireFeed(FrameParser *p, uint8_t byte, uint32_t now, DebugPacket *out, bool v2)
{
  if (p->used && (uint32_t)(now - p->last_ms) > 100u)
  {
    p->used = 0;
    p->errors++;
  }
  p->last_ms = now;
  if (p->used == DBG_FRAME_MAX)
  {
    discard(p);
    p->errors++;
  }
  p->bytes[p->used++] = byte;
  while (p->used)
  {
    if (p->bytes[0] != 0xa5 || (p->used > 1 && p->bytes[1] != 0x5a))
    {
      discard(p);
      continue;
    }
    if (p->used < 8)
      return false;
    uint16_t n = ReadU16(p->bytes + 4);
    if ((p->bytes[2] != DBG_VERSION && !(v2 && p->bytes[2] == 2)) || n > DBG_PAYLOAD_MAX)
    {
      discard(p);
      p->errors++;
      continue;
    }
    if (p->used < 10u + n)
      return false;
    if (ReadU16(p->bytes + 8 + n) != DebugCrc(p->bytes + 2, 6u + n))
    {
      discard(p);
      p->errors++;
      continue;
    }
    out->version = p->bytes[2];
    out->type = p->bytes[3];
    out->len = n;
    out->seq = ReadU16(p->bytes + 6);
    memcpy(out->data, p->bytes + 8, n);
    p->used = (uint16_t)(p->used - 10u - n);
    memmove(p->bytes, p->bytes + 10u + n, p->used);
    return true;
  }
  return false;
}
bool DebugFeed(FrameParser *p, uint8_t byte, uint32_t now, DebugPacket *out)
{
  return WireFeed(p, byte, now, out, false);
}
bool CanIdAllowed(bool center, uint16_t id)
{
  if (center)
    return id == CAN_COMMAND; /* No motor current command can pass this boundary. */
  return id == CAN_ACK || id == CAN_HEARTBEAT || id == CAN_IDENTITY || id == CAN_HIT ||
         (id >= CAN_SAMPLE_A && id < CAN_SAMPLE_A + 10u) ||
         (id >= CAN_SAMPLE_B && id < CAN_SAMPLE_B + 10u);
}
void SnapshotFrame(const PressureSnapshot *s, uint8_t part, CanFrame *out)
{
  uint8_t ch = (uint8_t)(part % 10u);
  const SensorValue *v = &s->values[ch];
  out->id = (uint16_t)((part < 10u ? CAN_SAMPLE_A : CAN_SAMPLE_B) + ch);
  out->len = 8;
  out->data[0] = s->seq;
  if (part < 10u)
  {
    out->data[1] = v->state;
    WriteU16(out->data + 2, v->raw);
    WriteU16(out->data + 4, v->baseline);
    WriteU16(out->data + 6, (uint16_t)v->delta);
  }
  else
  {
    out->data[1] = v->ring;
    WriteU16(out->data + 2, v->peak);
    WriteU32(out->data + 4, s->timestamp);
  }
}
bool SnapshotAccept(SnapshotJoin *j, const CanFrame *f, uint32_t now)
{
  bool a = f->id >= CAN_SAMPLE_A && f->id < CAN_SAMPLE_A + 10u;
  bool b = f->id >= CAN_SAMPLE_B && f->id < CAN_SAMPLE_B + 10u;
  if (f->len != 8 || (!a && !b))
    return false;
  if (!j->active || j->snapshot.seq != f->data[0] || (uint32_t)(now - j->started) > 100u)
  {
    memset(j, 0, sizeof(*j));
    j->active = 1;
    j->snapshot.seq = f->data[0];
    j->started = now;
  }
  uint8_t ch = (uint8_t)(f->id - (a ? CAN_SAMPLE_A : CAN_SAMPLE_B));
  SensorValue *v = &j->snapshot.values[ch];
  if (a)
  {
    v->state = f->data[1];
    v->raw = ReadU16(f->data + 2);
    v->baseline = ReadU16(f->data + 4);
    v->delta = (int16_t)ReadU16(f->data + 6);
    j->mask_a |= (uint16_t)(1u << ch);
  }
  else
  {
    uint32_t stamp = ReadU32(f->data + 4);
    if ((j->mask_b && j->snapshot.timestamp != stamp) || f->data[1] < 1u || f->data[1] > 10u)
    {
      j->active = 0;
      return false;
    }
    v->ring = f->data[1];
    v->peak = ReadU16(f->data + 2);
    j->snapshot.timestamp = stamp;
    j->mask_b |= (uint16_t)(1u << ch);
  }
  if (j->mask_a == 0x3ffu && j->mask_b == 0x3ffu)
  {
    j->active = 0;
    return true;
  }
  return false;
}
void SnapshotPack(const PressureSnapshot *s, uint8_t port, uint8_t out[106])
{
  out[0] = port;
  out[1] = s->seq;
  WriteU32(out + 2, s->timestamp);
  for (unsigned i = 0; i < 10; i++)
  {
    const SensorValue *v = &s->values[i];
    uint8_t *p = out + 6 + 10 * i;
    p[0] = v->ring;
    p[1] = v->state;
    WriteU16(p + 2, v->raw);
    WriteU16(p + 4, v->baseline);
    WriteU16(p + 6, (uint16_t)v->delta);
    WriteU16(p + 8, v->peak);
  }
}
bool MotorDecode(const CanFrame *f, int16_t *rpm, int16_t *current, uint16_t *angle, uint8_t *temp)
{
  if (f->id < 0x201u || f->id > 0x208u || f->len != 8)
    return false;
  *angle = (uint16_t)((uint16_t)f->data[0] << 8 | f->data[1]);
  *rpm = (int16_t)((uint16_t)f->data[2] << 8 | f->data[3]);
  *current = (int16_t)((uint16_t)f->data[4] << 8 | f->data[5]);
  *temp = f->data[6];
  return true;
}
