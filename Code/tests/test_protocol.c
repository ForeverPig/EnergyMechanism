#include "debug_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
  assert(DebugCrc((const uint8_t *)"123456789", 9) == 0x29b1);
  uint8_t data[192], encoded[202];
  for (unsigned i = 0; i < 192; i++)
    data[i] = (uint8_t)i;
  for (unsigned n = 0; n <= 192; n++)
  {
    FrameParser p = {0};
    DebugPacket out;
    size_t size = DebugEncode(encoded, 0x12, 65535, data, (uint16_t)n);
    unsigned packets = 0;
    (void)DebugFeed(&p, 0x99, 0, &out);
    for (size_t i = 0; i < size; i++)
      if (DebugFeed(&p, encoded[i], 1, &out))
        packets++;
    assert(packets == 1 && out.seq == 65535 && out.len == n && !memcmp(out.data, data, n));
  }
  FrameParser p = {0};
  DebugPacket out;
  size_t n = DebugEncode(encoded, 1, 3, data, 3);
  encoded[8] ^= 1;
  for (size_t i = 0; i < n; i++)
    assert(!DebugFeed(&p, encoded[i], 0, &out));
  n = DebugEncode(encoded, 1, 4, data, 3);
  unsigned packets = 0;
  for (size_t i = 0; i < n; i++)
    if (DebugFeed(&p, encoded[i], 1, &out))
      packets++;
  assert(packets == 1 && out.seq == 4 && p.errors);
  p.used = 0;
  for (unsigned i = 0; i < 6; i++)
    assert(!DebugFeed(&p, encoded[i], 5, &out));
  assert(!DebugFeed(&p, 0, 200, &out));
  assert(p.used == 0);
  /* A complete pressure snapshot needs both halves of every channel, from one epoch. */
  PressureSnapshot s = {.seq = 255, .timestamp = 123456};
  for (unsigned i = 0; i < 10; i++)
    s.values[i] = (SensorValue){(uint16_t)(100 + i), 99, -12, 321, 2, (uint8_t)(i + 1)};
  SnapshotJoin join = {0};
  CanFrame f;
  for (int part = 19; part >= 0; part--)
  {
    SnapshotFrame(&s, (uint8_t)part, &f);
    bool done = SnapshotAccept(&join, &f, 10);
    assert(done == (part == 0));
  }
  assert(join.snapshot.values[9].raw == 109 && join.snapshot.values[0].delta == -12);
  join = (SnapshotJoin){0};
  SnapshotFrame(&s, 0, &f);
  assert(!SnapshotAccept(&join, &f, 1));
  for (unsigned part = 1; part < 20; part++)
  {
    SnapshotFrame(&s, (uint8_t)part, &f);
    assert(!SnapshotAccept(&join, &f, 200));
  }
  s.seq = 0;
  SnapshotFrame(&s, 0, &f);
  assert(!SnapshotAccept(&join, &f, 201));
  assert(join.mask_a == 1 && join.mask_b == 0);
  SnapshotFrame(&s, 10, &f);
  assert(!SnapshotAccept(&join, &f, 202));
  f.id++;
  WriteU32(f.data + 4, s.timestamp + 1);
  assert(!SnapshotAccept(&join, &f, 203) && !join.active);
  for (unsigned id = 0; id < 2048; id++)
    assert(CanIdAllowed(true, (uint16_t)id) == (id == CAN_COMMAND));
  assert(!CanIdAllowed(false, 0x200) && !CanIdAllowed(false, 0x1ff));
  CanFrame motor = {.id = 0x208, .len = 8, .data = {0x1f, 0xff, 0xff, 0x9c, 0x80, 0, 55, 0}};
  int16_t rpm, current;
  uint16_t angle;
  uint8_t temp;
  assert(MotorDecode(&motor, &rpm, &current, &angle, &temp));
  assert(rpm == -100 && current == -32768 && angle == 8191 && temp == 55);
  motor.len = 7;
  assert(!MotorDecode(&motor, &rpm, &current, &angle, &temp));
  puts("PASS: framing/CRC/resync, complete snapshots/epoch/timeout, motor endian and all 2048 TX "
       "IDs");
  return 0;
}
