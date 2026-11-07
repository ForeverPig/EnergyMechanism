#include "comm_v2.h"
#include <string.h>
uint64_t V2Read64(const uint8_t *p)
{
  return ReadU32(p) | ((uint64_t)ReadU32(p + 4) << 32);
}
void V2Write64(uint8_t *p, uint64_t v)
{
  WriteU32(p, (uint32_t)v);
  WriteU32(p + 4, (uint32_t)(v >> 32));
}
bool V2Decode(const uint8_t *p, size_t n, DebugPacket *out)
{
  if (n < 10 || n > 194 || p[0] != 0xa5 || p[1] != 0x5a || p[2] != 2 ||
      ReadU16(p + 4) > V2_BLE_MAX || n != 10u + ReadU16(p + 4) ||
      ReadU16(p + n - 2) != DebugCrc(p + 2, n - 4))
    return false;
  out->version = 2;
  out->type = p[3];
  out->seq = ReadU16(p + 6);
  out->len = ReadU16(p + 4);
  memcpy(out->data, p + 8, out->len);
  return true;
}
int V2FragmentFeed(V2Reassembly *r, const uint8_t *p, size_t n, uint32_t now, DebugPacket *out)
{
  if (r->active && (uint32_t)(now - r->started) > V2_FRAGMENT_MS)
  {
    r->active = 0;
    r->errors++;
  }
  if (n < 5 || n > 20 || !p[3] || p[3] > 13 || p[2] >= p[3] || (p[2] + 1u < p[3] && n != 20))
    goto bad;
  uint16_t id = ReadU16(p);
  if (p[2] == 0)
  {
    if (r->active)
      r->errors++;
    r->active = 1;
    r->used = 0;
    r->next = 0;
    r->count = p[3];
    r->transfer = id;
    r->started = now;
  }
  if (!r->active || id != r->transfer || r->count != p[3] || p[2] != r->next ||
      r->used + n - 4 > sizeof(r->bytes))
    goto bad;
  memcpy(r->bytes + r->used, p + 4, n - 4);
  r->used += (uint16_t)(n - 4);
  r->next++;
  if (r->next != r->count)
    return 0;
  r->active = 0;
  if (V2Decode(r->bytes, r->used, out))
    return 1;
bad:
  r->active = 0;
  r->errors++;
  return -1;
}
size_t V2Fragment(const uint8_t *frame, size_t size, uint16_t transfer, uint8_t index,
                  uint8_t out[20])
{
  size_t off = (size_t)index * 16;
  if (size < 10 || size > 194 || off >= size)
    return 0;
  size_t n = size - off;
  if (n > 16)
    n = 16;
  WriteU16(out, transfer);
  out[2] = index;
  out[3] = (uint8_t)((size + 15) / 16);
  memcpy(out + 4, frame + off, n);
  return n + 4;
}
bool V2Terminal(const DebugPacket *p, uint8_t op)
{
  if (p->type == V2_RESULT && p->len == V2_RESULT_LEN && p->data[0] == op)
    return p->data[1] != V2_ACCEPTED;
  return (op == V2_HELLO && (p->type == V2_SESSION || p->type == V2_STATE)) ||
         (op == V2_GET_STATE && p->type == V2_STATE);
}
void V2MakeResult(DebugPacket *p, uint16_t seq, uint8_t op, uint8_t phase, uint8_t code,
                  uint64_t boot, uint32_t revision)
{
  memset(p, 0, sizeof(*p));
  p->version = 2;
  p->type = V2_RESULT;
  p->seq = seq;
  p->len = V2_RESULT_LEN;
  p->data[0] = op;
  p->data[1] = phase;
  p->data[2] = code;
  V2Write64(p->data + 4, boot);
  WriteU32(p->data + 12, revision);
  WriteU32(p->data + 20, seq);
}
