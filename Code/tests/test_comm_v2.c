#include "comm_v2.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
  uint8_t payload[V2_BLE_MAX], frame[DBG_FRAME_MAX], frag[20];
  for (unsigned i = 0; i < sizeof(payload); i++)
    payload[i] = (uint8_t)i;
  size_t n = WireEncode(frame, 2, V2_REQUEST, 0x1234, payload, sizeof(payload));
  assert(n == 194 && DebugCrc((const uint8_t *)"123456789", 9) == 0x29b1);
  DebugPacket p = {0};
  assert(V2Decode(frame, n, &p) && p.seq == 0x1234 && p.len == 184);
  V2Reassembly r = {0};
  for (uint8_t i = 0; i < 13; i++)
  {
    size_t size = V2Fragment(frame, n, 7, i, frag);
    assert(size == (i == 12 ? 6u : 20u));
    assert(V2FragmentFeed(&r, frag, size, 100 + i, &p) == (i == 12 ? 1 : 0));
  }
  assert(!memcmp(p.data, payload, 184));
  frame[n - 1] ^= 1;
  assert(!V2Decode(frame, n, &p));
  frame[n - 1] ^= 1;
  size_t k = V2Fragment(frame, n, 8, 0, frag);
  assert(V2FragmentFeed(&r, frag, k, 200, &p) == 0);
  k = V2Fragment(frame, n, 8, 2, frag);
  assert(V2FragmentFeed(&r, frag, k, 201, &p) == -1);
  k = V2Fragment(frame, n, 9, 0, frag);
  assert(V2FragmentFeed(&r, frag, k, 300, &p) == 0);
  k = V2Fragment(frame, n, 9, 1, frag);
  assert(V2FragmentFeed(&r, frag, k, 1301, &p) == -1);
  k = V2Fragment(frame, n, 10, 0, frag);
  assert(V2FragmentFeed(&r, frag, k - 1, 1400, &p) == -1);
  /* A new transfer replaces an incomplete old one, including timer wrap-around. */
  k = V2Fragment(frame, n, 11, 0, frag);
  assert(V2FragmentFeed(&r, frag, k, UINT32_MAX - 50, &p) == 0);
  k = V2Fragment(frame, n, 12, 0, frag);
  assert(V2FragmentFeed(&r, frag, k, 10, &p) == 0);
  for (uint8_t i = 1; i < 13; i++)
  {
    k = V2Fragment(frame, n, 12, i, frag);
    assert(V2FragmentFeed(&r, frag, k, 20 + i, &p) == (i == 12 ? 1 : 0));
  }
  FrameParser parser = {0};
  unsigned count = 0;
  for (unsigned i = 0; i < n; i++)
    count += WireFeed(&parser, frame[i], 100, &p, true);
  assert(count == 1 && p.version == 2);
  memset(&parser, 0, sizeof(parser));
  count = 0;
  for (unsigned i = 0; i < n; i++)
    count += DebugFeed(&parser, frame[i], 100, &p);
  assert(count == 0);
  n = DebugEncode(frame, DBG_STATUS, 1, payload, 4);
  memset(&parser, 0, sizeof(parser));
  count = 0;
  for (unsigned i = 0; i < n; i++)
    count += WireFeed(&parser, frame[i], 100, &p, true);
  assert(count == 1 && p.version == 1);
  puts("V2 framing and fragmentation: PASS");
  return 0;
}
