#include "gateway_core.h"
#include "center_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct
{
  DebugPacket p[256];
  unsigned read, count;
} Packets;
static Gateway g;
static CenterService s;
static CenterView view;
static Packets to_center, to_gateway, received[3][3];
static V2Reassembly rx[3][3];
static uint32_t now;
static unsigned cal_calls, drop_leaf;
static uint8_t cal_seq;
static bool queue_packet(Packets *q, const DebugPacket *p)
{
  assert(q->count < 256);
  q->p[(q->read + q->count) % 256] = *p;
  q->count++;
  return true;
}
static DebugPacket pop(Packets *q)
{
  assert(q->count);
  DebugPacket p = q->p[q->read];
  q->read = (q->read + 1) % 256;
  q->count--;
  return p;
}
static bool uart_gateway(void *ctx, const DebugPacket *p)
{
  (void)ctx;
  return queue_packet(&to_center, p);
}
static bool uart_center(void *ctx, const DebugPacket *p)
{
  (void)ctx;
  return queue_packet(&to_gateway, p);
}
static bool calibration(void *ctx, uint8_t slot, uint8_t seq)
{
  (void)ctx;
  assert(slot >= 1 && slot <= 5);
  cal_calls++;
  cal_seq = seq;
  return true;
}
static DebugPacket request(uint16_t seq, uint8_t op, const uint8_t *args, uint16_t size)
{
  DebugPacket p = {
      .version = 2, .type = V2_REQUEST, .seq = seq, .len = (uint16_t)(V2_REQUEST_HEADER + size)};
  p.data[0] = op;
  V2Write64(p.data + 1, g.boot);
  WriteU32(p.data + 9, g.revision);
  if (size)
    memcpy(p.data + V2_REQUEST_HEADER, args, size);
  return p;
}
static void command(unsigned client, const DebugPacket *p)
{
  uint8_t wire[194], fragment[20];
  size_t n = WireEncode(wire, 2, p->type, p->seq, p->data, p->len);
  for (uint8_t i = 0; i < (n + 15) / 16; i++)
  {
    size_t k = V2Fragment(wire, n, p->seq, i, fragment);
    Gateway_Fragment(&g, client, fragment, k, now);
  }
}
static void drain(void)
{
  for (unsigned i = 0; i < 3; i++)
    for (uint8_t ch = 0; ch < 3; ch++)
    {
      uint8_t bytes[20];
      size_t n;
      unsigned budget = 300;
      while (budget-- && (n = Gateway_NextFragment(&g, i, ch, bytes)) != 0)
      {
        DebugPacket p;
        int rc = V2FragmentFeed(&rx[i][ch], bytes, n, now, &p);
        assert(rc >= 0);
        if (rc == 1)
          queue_packet(&received[i][ch], &p);
        Gateway_FragmentSent(&g, i, ch);
      }
    }
}
static void tick(uint32_t elapsed)
{
  now += elapsed;
  for (unsigned i = 0; i < 5; i++)
    view.leaf[i].heartbeat = now;
  CenterService_View(&s, &view, now);
  Gateway_Poll(&g, now);
  while (to_center.count)
  {
    DebugPacket p = pop(&to_center);
    CenterService_Request(&s, &p, now);
  }
  CenterService_Poll(&s, now);
  while (to_gateway.count)
  {
    DebugPacket p = pop(&to_gateway);
    if (drop_leaf && p.type == V2_LEAF)
    {
      drop_leaf--;
      continue;
    }
    Gateway_Uart(&g, &p, now);
  }
  drain();
}
static void clear_received(void)
{
  memset(received, 0, sizeof(received));
}
static DebugPacket reply(unsigned client, uint16_t seq, uint8_t type)
{
  Packets *q = &received[client][0];
  for (unsigned i = 0; i < q->count; i++)
  {
    DebugPacket p = q->p[(q->read + i) % 256];
    if (p.seq == seq && p.type == type)
      return p;
  }
  fprintf(stderr, "missing reply client=%u seq=%u type=%u now=%u\n", client, seq, type, now);
  assert(0);
  return (DebugPacket){0};
}
static void reset(void)
{
  memset(&view, 0, sizeof(view));
  memset(&to_center, 0, sizeof(to_center));
  memset(&to_gateway, 0, sizeof(to_gateway));
  memset(rx, 0, sizeof(rx));
  clear_received();
  now = 0;
  cal_calls = 0;
  drop_leaf = 0;
  uint8_t id[12] = {1, 2, 3, 4, 5, 6};
  Gateway_Init(&g, 0x1122334455667788ULL, id, (GatewayBackend){uart_gateway, NULL});
  CenterService_Init(&s, 0x8877665544332211ULL, (CenterBackend){uart_center, calibration, NULL});
  view.motor_known = view.motor_valid = 1;
  for (unsigned i = 0; i < 5; i++)
  {
    view.leaf[i].online = 1;
    view.leaf[i].status = 1;
  }
  for (unsigned i = 0; i < 3; i++)
  {
    assert(Gateway_Connect(&g, (uint16_t)(10 + i)) == (int)i);
    for (uint8_t ch = 0; ch < 3; ch++)
      Gateway_Subscribe(&g, i, ch, true);
    uint8_t args[3] = {0, 2, 2};
    DebugPacket q = request(1, V2_HELLO, args, 3);
    command(i, &q);
  }
  assert(Gateway_Connect(&g, 99) == -1);
  tick(10);
  tick(500);
  tick(1);
  assert(g.linked && g.boot == s.boot && s.stopped);
  for (unsigned i = 0; i < 3; i++)
    assert(reply(i, 1, V2_SESSION).data[45] == 3);
  clear_received();
}
static void test_routing_and_guard(void)
{
  reset();
  DebugPacket a = request(2, V2_ENTER_DEBUG, NULL, 0), b = request(2, V2_ENTER_DEBUG, NULL, 0);
  command(0, &a);
  command(1, &b);
  tick(1);
  assert(reply(0, 2, V2_RESULT).data[1] == V2_COMPLETED);
  assert(reply(1, 2, V2_RESULT).data[2] == V2_STALE_STATE);
  assert(s.mode == V2_HIT_TEST && received[2][0].count == 0);
  a = request(3, V2_START, NULL, 0);
  command(0, &a);
  tick(1);
  assert(reply(0, 3, V2_RESULT).data[2] == V2_UNSUPPORTED);
  view.motor_valid = 0;
  tick(500);
  assert(!s.stopped && s.mode == V2_IDLE);
  a = request(4, V2_ENTER_DEBUG, NULL, 0);
  command(0, &a);
  tick(1);
  assert(reply(0, 4, V2_RESULT).data[2] == V2_NOT_STOPPED);
  /* A stale epoch and an old boot cannot operate the center. */
  DebugPacket stale = request(100, V2_ENTER_DEBUG, NULL, 0);
  stale.len += 8;
  memmove(stale.data + 8, stale.data, stale.len - 8);
  V2Write64(stale.data, 123);
  CenterService_Request(&s, &stale, now);
  assert(s.mode == V2_IDLE);
}
static void enter_debug(void)
{
  DebugPacket q = request(2, V2_ENTER_DEBUG, NULL, 0);
  command(0, &q);
  tick(1);
  assert(s.mode == V2_HIT_TEST);
  clear_received();
}
static void test_leases_and_broadcast(void)
{
  reset();
  enter_debug();
  uint8_t slot = 1;
  DebugPacket q = request(3, V2_OBSERVE_ENTER, &slot, 1);
  command(0, &q);
  tick(1);
  uint32_t token = ReadU32(reply(0, 3, V2_RESULT).data + 24);
  assert(token && s.selected == 1);
  slot = 2;
  q = request(2, V2_OBSERVE_ENTER, &slot, 1);
  command(1, &q);
  tick(1);
  assert(s.selected == 2 && g.client[0].token && g.client[1].token);
  clear_received();
  PressureSnapshot snap = {.timestamp = 100};
  for (unsigned i = 0; i < 10; i++)
    snap.values[i].ring = (uint8_t)(i + 1);
  CenterService_Pressure(&s, 2, &snap, now);
  tick(1);
  assert(received[0][2].count == 1 && received[1][2].count == 1 && !received[2][2].count);
  uint8_t hit[8] = {7, 10, 12, 0, 1, 0, 0, 0};
  CenterService_Hit(&s, 2, hit, now);
  tick(1);
  for (unsigned i = 0; i < 3; i++)
    assert(received[i][1].count == 1);
  Gateway_Disconnect(&g, 0);
  clear_received();
  for (unsigned i = 0; i < 20; i++)
  {
    tick(100);
    clear_received();
  }
  assert(s.selected == 2); /* B's lease keeps the global selection alive. */
  for (unsigned i = 0; i < 30; i++)
  {
    tick(100);
    clear_received();
  }
  assert(s.selected == 0 && !g.client[1].token);
}
static void test_calibration_idempotence(void)
{
  reset();
  enter_debug();
  tick(2200);
  clear_received();
  uint8_t slot = 1;
  DebugPacket q = request(3, V2_CALIBRATE, &slot, 1);
  command(0, &q);
  tick(1);
  assert(cal_calls == 1 && reply(0, 3, V2_RESULT).data[1] == V2_ACCEPTED);
  command(0, &q);
  tick(1);
  assert(cal_calls == 1);
  DebugPacket conflict = q;
  conflict.data[V2_REQUEST_HEADER] = 2;
  command(0, &conflict);
  drain();
  assert(received[0][0].p[(received[0][0].read + received[0][0].count - 1) % 256].data[2] ==
         V2_REQUEST_CONFLICT);
  CenterService_CalAck(&s, 1, cal_seq, RES_OK, 0, now);
  view.leaf[0].status = 0;
  tick(10);
  assert(s.cal_index != 255);
  view.leaf[0].status = 1;
  tick(200);
  assert(s.cal_index == 255);
  clear_received();
  command(0, &q);
  drain();
  assert(reply(0, 3, V2_RESULT).data[1] == V2_COMPLETED && cal_calls == 1);
  /* Disconnect and reuse handle while an operation is in flight: no reply reaches the new session.
   */
  q = request(4, V2_CALIBRATE, &slot, 1);
  command(0, &q);
  tick(1);
  clear_received();
  uint64_t old = g.client[0].session;
  Gateway_Disconnect(&g, 0);
  assert(Gateway_Connect(&g, 10) == 0);
  assert(g.client[0].session != old);
  Gateway_Subscribe(&g, 0, 0, true);
  Gateway_Subscribe(&g, 0, 1, true);
  CenterService_CalAck(&s, 1, cal_seq, RES_OK, 0, now);
  tick(1);
  assert(!received[0][0].count);
}
static void test_leaves_retry(void)
{
  reset();
  uint8_t all = 0;
  drop_leaf = 1;
  DebugPacket q = request(2, V2_GET_LEAVES, &all, 1);
  command(0, &q);
  tick(1);
  tick(1);
  bool pending = false;
  for (unsigned i = 1; i < GW_MAPS; i++)
    pending |= g.maps[i].active;
  assert(pending);
  tick(500);
  tick(1);
  assert(reply(0, 2, V2_RESULT).data[1] == V2_COMPLETED);
  unsigned mask = 0;
  for (unsigned i = 0; i < received[0][0].count; i++)
    if (received[0][0].p[i].type == V2_LEAF)
      mask |= 1u << (received[0][0].p[i].data[12] - 1);
  assert(mask == 31);
}
static void test_replay_and_reboot(void)
{
  reset();
  for (uint16_t seq = 2; seq < 8; seq++)
  {
    DebugPacket q = request(seq, V2_GET_STATE, NULL, 0);
    command(0, &q);
    tick(1);
    clear_received();
  }
  DebugPacket old = request(2, V2_GET_STATE, NULL, 0);
  command(0, &old);
  drain();
  assert(reply(0, 2, V2_RESULT).data[2] == V2_REPLAY_EXPIRED);
  clear_received();
  DebugPacket q = request(8, V2_ENTER_DEBUG, NULL, 0);
  command(0, &q);
  Gateway_Poll(&g, now);
  assert(to_center.count);
  CenterService_Init(&s, 99, (CenterBackend){uart_center, calibration, NULL});
  tick(1); /* Old expected boot is rejected or a new boot is detected, never executed. */
  assert(s.mode == V2_IDLE && !g.linked);
  for (unsigned i = 0; i < 15; i++)
  {
    tick(500);
    clear_received();
  }
  assert(g.linked && g.boot == 99 && s.mode == V2_IDLE);
}
static void test_slow_peer_and_timeout(void)
{
  reset();
  for (unsigned i = 0; i < 20; i++)
  {
    DebugPacket q = request((uint16_t)(2 + i), V2_HELLO, (uint8_t[]){0, 2, 2}, 3);
    Gateway_Command(&g, 0, &q, now);
  }
  assert(g.client[0].close && g.queue_disconnects);
  DebugPacket q = request(2, V2_GET_STATE, NULL, 0);
  command(1, &q);
  tick(1);
  assert(reply(1, 2, V2_STATE).seq == 2);
  clear_received();
  /* UART disappears. Gateway must release observers and report link loss, never drive anything. */
  now += V2_LINK_MS + 1;
  Gateway_Poll(&g, now);
  assert(!g.linked);
  q = request(3, V2_CALIBRATE, (uint8_t[]){1}, 1);
  command(1, &q);
  drain();
  assert(reply(1, 3, V2_RESULT).data[2] == V2_LINK_DOWN);
}
static void test_query_and_center_dedup(void)
{
  reset();
  enter_debug();
  uint8_t slot = 1;
  DebugPacket q = request(3, V2_CALIBRATE, &slot, 1);
  command(0, &q);
  tick(1);
  assert(reply(0, 3, V2_RESULT).data[2] == V2_NOT_READY && cal_calls == 0);
  tick(2200);
  clear_received();
  q = request(4, V2_CALIBRATE, &slot, 1);
  command(0, &q);
  tick(1);
  unsigned index = s.cal_index;
  assert(index < CENTER_CACHE && cal_calls == 1);
  DebugPacket wire = s.cache[index].request;
  memmove(wire.data + 8, wire.data, wire.len);
  wire.len += 8;
  V2Write64(wire.data, s.gateway);
  CenterService_Request(&s, &wire, now);
  CenterService_Poll(&s, now);
  assert(cal_calls == 1 && s.cal_index == index);
  uint8_t original[2];
  WriteU16(original, 4);
  q = request(5, V2_QUERY, original, 2);
  command(0, &q);
  tick(1);
  assert(reply(0, 5, V2_RESULT).data[0] == V2_QUERY);
  assert(reply(0, 5, V2_RESULT).data[1] == V2_ACCEPTED);
  /* A lost initial ACK can yield a READY ACK from the V1 duplicate cache. */
  tick(300);
  assert(cal_calls == 2);
  CenterService_CalAck(&s, 1, cal_seq, RES_OK, 1, now);
  tick(200);
  assert(s.cal_index == 255);
  q = request(6, V2_QUERY, original, 2);
  command(0, &q);
  tick(1);
  assert(reply(0, 6, V2_RESULT).data[1] == V2_COMPLETED);
}
static void test_sequence_exhaustion(void)
{
  reset();
  uint64_t epoch = g.epoch;
  g.uart_seq = 65535;
  for (unsigned i = 0; i < GW_MAPS; i++)
    g.maps[i].active = 0;
  Gateway_Poll(&g, now + 1000);
  assert(g.epoch != epoch && g.uart_seq == 1 && !g.linked);
  for (unsigned i = 0; i < 3; i++)
    assert(g.client[i].close);
}
static void test_subscription_session_restart(void)
{
  reset();
  enter_debug();
  tick(2200);
  uint64_t old_session = g.client[0].session, other_session = g.client[1].session;
  uint8_t slot = 1;
  DebugPacket q = request(3, V2_CALIBRATE, &slot, 1);
  command(0, &q);
  tick(1);
  assert(s.cal_index != 255);
  g.client[0].token = 123;
  Gateway_Subscribe(&g, 0, V2_CONTROL_CHANNEL, false);
  assert(g.client[0].active && g.client[0].handle == 10);
  assert(g.client[0].session != old_session && g.client[0].highwater == 0);
  assert(!g.client[0].token && !g.client[0].hello && !g.client[0].rx.active);
  assert(g.client[1].session == other_session);
  clear_received();
  Gateway_Subscribe(&g, 0, V2_CONTROL_CHANNEL, true);
  q = request(1, V2_HELLO, (uint8_t[]){0, 2, 2}, 3);
  command(0, &q);
  tick(1);
  assert(V2Read64(reply(0, 1, V2_SESSION).data + 20) == g.client[0].session);
  CenterService_CalAck(&s, 1, cal_seq, RES_OK, 0, now);
  tick(200);
  for (unsigned i = 0; i < received[0][0].count; i++)
    assert(received[0][0].p[i].seq != 3); /* Old operation cannot leak to the new session. */
  q = request(2, V2_GET_STATE, NULL, 0);
  command(0, &q);
  tick(1);
  assert(reply(0, 2, V2_STATE).seq == 2);
}
int main(void)
{
  test_routing_and_guard();
  test_leases_and_broadcast();
  test_calibration_idempotence();
  test_leaves_retry();
  test_replay_and_reboot();
  test_slow_peer_and_timeout();
  test_query_and_center_dedup();
  test_sequence_exhaustion();
  test_subscription_session_restart();
  puts("V2 center/gateway integration: PASS");
  return 0;
}
