#include "gateway_core.h"
#include <string.h>

static void unobserve(GatewayClient *c)
{
  if (c->token)
    c->last_token = c->token;
  c->token = 0;
  c->pressure_ready = 0;
  c->tx[V2_PRESSURE_CHANNEL].active = 0;
}
static unsigned observers(const Gateway *g)
{
  unsigned n = 0;
  for (unsigned i = 0; i < GW_CLIENTS; i++)
    if (g->client[i].active && g->client[i].token)
      n++;
  return n;
}
static bool queue(Gateway *g, GatewayClient *c, uint8_t channel, const DebugPacket *p)
{
  if (!c->active || !(c->subscribed & (1u << channel)))
    return false;
  if (channel == V2_PRESSURE_CHANNEL)
  {
    c->pressure = *p;
    c->pressure_ready = 1;
    return true;
  }
  if (p->type == V2_STATE && !p->seq)
  {
    c->state = *p;
    c->state_ready = 1;
    return true;
  }
  GatewayQueue *q = channel == V2_CONTROL_CHANNEL ? &c->control : &c->event;
  unsigned limit = channel == V2_CONTROL_CHANNEL ? GW_CONTROL_DEPTH : GW_EVENT_DEPTH;
  if (q->count == limit)
  {
    if (channel == V2_CONTROL_CHANNEL)
    {
      c->close = 1;
      g->queue_disconnects++;
      unobserve(c);
    }
    else
      g->event_drops++;
    return false;
  }
  q->items[(q->read + q->count) % GW_CONTROL_DEPTH] = *p;
  q->count++;
  return true;
}
static void broadcast(Gateway *g, uint8_t channel, const DebugPacket *p)
{
  for (unsigned i = 0; i < GW_CLIENTS; i++)
    if (g->client[i].hello && (channel != V2_PRESSURE_CHANNEL || g->client[i].token))
      (void)queue(g, &g->client[i], channel, p);
}
static void make_result(Gateway *g, DebugPacket *p, uint16_t seq, uint8_t op, uint8_t phase,
                        uint8_t code)
{
  V2MakeResult(p, seq, op, phase, code, g->boot, g->revision);
  WriteU32(p->data + 28, g->selection_epoch);
  p->data[32] = g->mode;
  p->data[33] = g->selected;
}
static void remember(GatewayHistory *h, const DebugPacket *p)
{
  /* Replace repeated accepted/terminal results and repeated leaf entries after UART retries. */
  for (unsigned i = 0; i < h->count; i++)
  {
    DebugPacket *old = &h->replies[i];
    if (old->type == p->type &&
        (p->type != V2_LEAF || (p->len == V2_LEAF_LEN && old->data[12] == p->data[12])))
    {
      *old = *p;
      return;
    }
  }
  if (h->count < 6)
    h->replies[h->count++] = *p;
}
static void finish_local(Gateway *g, GatewayClient *c, GatewayHistory *h, uint8_t phase,
                         uint8_t code)
{
  DebugPacket p;
  make_result(g, &p, h->request.seq, h->request.data[0], phase, code);
  h->pending = 0;
  remember(h, &p);
  (void)queue(g, c, V2_CONTROL_CHANNEL, &p);
}
static bool map_matches(Gateway *g, GatewayMap *m)
{
  if (m->client >= GW_CLIENTS)
    return false;
  GatewayClient *c = &g->client[m->client];
  GatewayHistory *h = &c->history[m->history];
  return c->active && c->session == m->session && h->valid && h->request.seq == m->client_seq;
}
static void uncertain(Gateway *g, GatewayMap *m, uint8_t code)
{
  if (map_matches(g, m))
  {
    GatewayClient *c = &g->client[m->client];
    GatewayHistory *h = &c->history[m->history];
    finish_local(g, c, h, V2_UNKNOWN, code);
    h->pending = 1; /* Pin the entry until the late-response window closes. */
  }
}
static void session_packet(Gateway *g, GatewayClient *c, DebugPacket *p, uint16_t seq)
{
  memset(p, 0, sizeof(*p));
  p->version = 2;
  p->type = V2_SESSION;
  p->seq = seq;
  p->len = V2_SESSION_LEN;
  memcpy(p->data, g->device, 12);
  V2Write64(p->data + 12, g->epoch);
  V2Write64(p->data + 20, c->session);
  V2Write64(p->data + 28, g->boot);
  WriteU32(p->data + 36, g->linked ? V2_CAPABILITIES : 0);
  WriteU32(p->data + 40, g->revision);
  p->data[44] = g->linked;
  p->data[45] = GW_BLE_CLIENTS;
  WriteU16(p->data + 46, V2_BLE_MAX);
  p->data[48] = 2;
  p->data[49] = g->state_valid ? 2 : 0;
  p->data[50] = 1;
  WriteU32(p->data + 52, V2_OBSERVER_MS);
}
static void announce_link(Gateway *g)
{
  for (unsigned i = 0; i < GW_CLIENTS; i++)
    if (g->client[i].active && g->client[i].hello)
    {
      DebugPacket p;
      session_packet(g, &g->client[i], &p, 0);
      (void)queue(g, &g->client[i], 0, &p);
    }
}
static void link_lost(Gateway *g)
{
  g->linked = 0;
  for (unsigned i = 0; i < GW_CLIENTS; i++)
    unobserve(&g->client[i]);
  for (unsigned i = 1; i < GW_MAPS; i++)
    if (g->maps[i].active)
    {
      uncertain(g, &g->maps[i], V2_LINK_DOWN);
      if (map_matches(g, &g->maps[i]))
        g->client[g->maps[i].client].history[g->maps[i].history].pending = 0;
      g->maps[i].active = 0;
    }
  g->maps[0].active = 0;
  announce_link(g);
}
void Gateway_Init(Gateway *g, uint64_t epoch, const uint8_t device[12], GatewayBackend io)
{
  memset(g, 0, sizeof(*g));
  g->epoch = epoch ? epoch : 1;
  g->io = io;
  memcpy(g->device, device, 12);
}
int Gateway_Connect(Gateway *g, uint16_t handle)
{
  for (unsigned i = 0; i < GW_BLE_CLIENTS; i++)
    if (!g->client[i].active)
    {
      GatewayClient *c = &g->client[i];
      memset(c, 0, sizeof(*c));
      c->active = 1;
      c->handle = handle;
      c->session = ++g->session_counter;
      return (int)i;
    }
  return -1;
}
void Gateway_Disconnect(Gateway *g, unsigned i)
{
  if (i >= GW_CLIENTS)
    return;
  unobserve(&g->client[i]);
  g->client[i].active = 0;
  /* Already forwarded operations may finish; their session identity prevents handle reuse delivery.
   */
}
void Gateway_Subscribe(Gateway *g, unsigned i, uint8_t channel, bool enabled)
{
  if (i >= GW_CLIENTS || channel > 2)
    return;
  GatewayClient *c = &g->client[i];
  if (enabled)
    c->subscribed |= (uint8_t)(1u << channel);
  else
  {
    c->subscribed &= (uint8_t)~(1u << channel);
    memset(&c->tx[channel], 0, sizeof(c->tx[channel]));
    if (channel == V2_PRESSURE_CHANNEL)
      unobserve(c);
    if (channel == V2_CONTROL_CHANNEL)
    {
      /* Windows may retain the physical link after a GATT client closes. Ending
       * CONTROL subscription explicitly retires this business session. The old
       * UART maps keep their original session and cannot reply to its successor. */
      uint8_t active = c->active, subscribed = c->subscribed, closing = c->close;
      uint16_t handle = c->handle, transfer[3];
      memcpy(transfer, c->transfer, sizeof(transfer));
      memset(c, 0, sizeof(*c));
      c->active = active;
      c->subscribed = subscribed;
      c->close = closing;
      c->handle = handle;
      c->session = ++g->session_counter;
      memcpy(c->transfer, transfer, sizeof(transfer));
    }
    if (channel == V2_EVENT_CHANNEL)
    {
      memset(&c->event, 0, sizeof(c->event));
      c->hello = 0;
    }
  }
}
void Gateway_Info(const Gateway *g, uint8_t out[16])
{
  memset(out, 0, 16);
  memcpy(out, "EMB2", 4);
  out[4] = 2;
  WriteU16(out + 6, 194);
  out[8] = GW_BLE_CLIENTS;
  out[9] = (uint8_t)(6u | (g->linked ? 1u : 0u));
  WriteU32(out + 12, 3); /* Revision 3: USB session, compatible BLE wire format. */
}
static bool start_map(Gateway *g, GatewayMap *m, const DebugPacket *q, unsigned client,
                      unsigned history, uint32_t now)
{
  if (g->uart_seq == 65535u)
    return false;
  memset(m, 0, sizeof(*m));
  m->active = 1;
  m->client = (uint8_t)client;
  m->history = (uint8_t)history;
  m->op = q->data[0];
  m->client_seq = q->seq;
  m->started = now;
  m->sent = now - V2_RETRY_MS;
  if (client < GW_CLIENTS)
    m->session = g->client[client].session;
  m->wire = *q;
  m->wire.seq = ++g->uart_seq;
  m->wire.len += 8;
  memmove(m->wire.data + 8, m->wire.data, q->len);
  V2Write64(m->wire.data, g->epoch);
  return true;
}
static GatewayHistory *reserve(GatewayClient *c, const DebugPacket *p)
{
  for (unsigned n = 0; n < GW_HISTORY; n++)
  {
    unsigned i = (c->history_next + n) % GW_HISTORY;
    if (!c->history[i].valid || !c->history[i].pending)
    {
      GatewayHistory *h = &c->history[i];
      memset(h, 0, sizeof(*h));
      h->valid = 1;
      h->request = *p;
      c->history_next = (uint8_t)((i + 1) % GW_HISTORY);
      return h;
    }
  }
  return NULL;
}
void Gateway_Command(Gateway *g, unsigned i, const DebugPacket *q, uint32_t now)
{
  if (i >= GW_CLIENTS || !g->client[i].active || q->version != 2 || q->type != V2_REQUEST ||
      !q->seq || q->len < V2_REQUEST_HEADER || q->len > V2_BLE_MAX)
    return;
  GatewayClient *c = &g->client[i];
  uint8_t op = q->data[0];
  if (c->close)
    return;
  for (unsigned k = 0; k < GW_HISTORY; k++)
  {
    GatewayHistory *h = &c->history[k];
    if (h->valid && h->request.seq == q->seq)
    {
      if (h->request.len != q->len || memcmp(h->request.data, q->data, q->len))
      {
        DebugPacket p;
        make_result(g, &p, q->seq, op, V2_REJECTED, V2_REQUEST_CONFLICT);
        (void)queue(g, c, V2_CONTROL_CHANNEL, &p);
      }
      else
        for (unsigned j = 0; j < h->count; j++)
          (void)queue(g, c, V2_CONTROL_CHANNEL, &h->replies[j]);
      return;
    }
  }
  if (q->seq <= c->highwater)
  {
    DebugPacket p;
    make_result(g, &p, q->seq, op, V2_REJECTED, V2_REPLAY_EXPIRED);
    (void)queue(g, c, V2_CONTROL_CHANNEL, &p);
    return;
  }
  GatewayHistory *h = reserve(c, q);
  if (!h)
  {
    c->highwater = q->seq;
    DebugPacket p;
    make_result(g, &p, q->seq, op, V2_REJECTED, V2_BUSY);
    (void)queue(g, c, 0, &p);
    return;
  }
  c->highwater = q->seq;
  if ((uint32_t)(now - c->rate_ms) >= 1000u)
  {
    c->rate_ms = now;
    c->rate_count = 0;
  }
  if (++c->rate_count > 20u)
  {
    finish_local(g, c, h, V2_REJECTED, V2_RATE_LIMIT);
    return;
  }
  uint16_t n = (uint16_t)(q->len - V2_REQUEST_HEADER);
  const uint8_t *a = q->data + V2_REQUEST_HEADER;
  if (op == V2_HELLO)
  {
    if (n != 3 || a[1] > 2 || a[2] < 2 || a[1] > a[2])
    {
      finish_local(g, c, h, V2_REJECTED, V2_BAD_ARGUMENT);
      return;
    }
    if ((c->subscribed & 3u) != 3u)
    {
      finish_local(g, c, h, V2_REJECTED, V2_NOT_READY);
      return;
    }
    DebugPacket p;
    session_packet(g, c, &p, q->seq);
    c->hello = 1;
    remember(h, &p);
    (void)queue(g, c, 0, &p);
    return;
  }
  if (!c->hello || (c->subscribed & 3u) != 3u)
  {
    finish_local(g, c, h, V2_REJECTED, V2_NOT_READY);
    return;
  }
  if (op == V2_QUERY)
  {
    if (n != 2)
    {
      finish_local(g, c, h, V2_REJECTED, V2_BAD_ARGUMENT);
      return;
    }
    GatewayHistory *original = NULL;
    for (unsigned k = 0; k < GW_HISTORY; k++)
      if (&c->history[k] != h && c->history[k].valid && c->history[k].request.seq == ReadU16(a))
        original = &c->history[k];
    if (!original || !original->count)
    {
      finish_local(g, c, h, V2_UNKNOWN, V2_CACHE_GAP);
      return;
    }
    DebugPacket p = original->replies[0];
    for (unsigned j = 0; j < original->count; j++)
      if (original->replies[j].type == V2_RESULT)
        p = original->replies[j];
    if (original->pending && original->request.data[0] == V2_GET_LEAVES)
      make_result(g, &p, q->seq, V2_GET_LEAVES, V2_ACCEPTED, V2_NOT_READY);
    p.seq = q->seq;
    if (p.type == V2_RESULT)
    {
      p.data[3] = p.data[0];
      p.data[0] = V2_QUERY;
    }
    remember(h, &p);
    (void)queue(g, c, 0, &p);
    return;
  }
  if (!g->linked)
  {
    finish_local(g, c, h, V2_REJECTED, V2_LINK_DOWN);
    return;
  }
  if (op == V2_OBSERVE_RENEW || op == V2_OBSERVE_LEAVE)
  {
    if (n != 4)
    {
      finish_local(g, c, h, V2_REJECTED, V2_BAD_ARGUMENT);
      return;
    }
    uint32_t token = ReadU32(a);
    if (V2Read64(q->data + 1) != g->boot || !token ||
        (token != c->token && !(op == V2_OBSERVE_LEAVE && token == c->last_token)))
    {
      finish_local(g, c, h, V2_REJECTED, V2_STALE_STATE);
      return;
    }
    if (op == V2_OBSERVE_LEAVE)
      unobserve(c);
    else if (!c->token || (uint32_t)(now - c->observer_ms) >= V2_OBSERVER_MS)
    {
      unobserve(c);
      finish_local(g, c, h, V2_REJECTED, V2_STALE_STATE);
      return;
    }
    else
      c->observer_ms = now;
    finish_local(g, c, h, V2_COMPLETED, V2_OK);
    return;
  }
  if (op == V2_GET_LEAVES && (n != 1 || a[0] > 5))
  {
    finish_local(g, c, h, V2_REJECTED, V2_BAD_ARGUMENT);
    return;
  }
  if (op == V2_OBSERVE_ENTER && !(c->subscribed & 4u))
  {
    finish_local(g, c, h, V2_REJECTED, V2_NOT_READY);
    return;
  }
  unsigned in_flight = 0;
  for (unsigned k = 1; k < GW_MAPS; k++)
    if (g->maps[k].active && g->maps[k].client == i && g->maps[k].session == c->session)
      in_flight++;
  if (in_flight >= 2)
  {
    finish_local(g, c, h, V2_REJECTED, V2_BUSY);
    return;
  }
  for (unsigned k = 1; k < GW_MAPS; k++)
    if (!g->maps[k].active)
    {
      if (start_map(g, &g->maps[k], q, i, (unsigned)(h - c->history), now))
      {
        h->pending = 1;
        return;
      }
      break;
    }
  finish_local(g, c, h, V2_REJECTED, V2_BUSY);
}
void Gateway_Fragment(Gateway *g, unsigned i, const uint8_t *p, size_t n, uint32_t now)
{
  if (i >= GW_CLIENTS || !g->client[i].active)
    return;
  DebugPacket q;
  int rc = V2FragmentFeed(&g->client[i].rx, p, n, now, &q);
  if (rc < 0)
    g->frame_errors++;
  else if (rc == 1)
    Gateway_Command(g, i, &q, now);
}
static void selection(Gateway *g, uint8_t slot, uint32_t epoch)
{
  if (epoch != g->selection_epoch || slot != g->selected)
  {
    for (unsigned i = 0; i < GW_CLIENTS; i++)
    {
      g->client[i].pressure_ready = 0;
      g->client[i].tx[2].active = 0;
      if (!slot)
        unobserve(&g->client[i]);
    }
  }
  g->selected = slot;
  g->selection_epoch = epoch;
}
static void center_rebooted(Gateway *g, uint64_t boot)
{
  /* Remove snapshots from the old boot; unresolved operations become UNKNOWN. */
  g->boot = boot;
  g->revision = 0;
  g->state_valid = 0;
  g->mode = V2_IDLE;
  g->selected = 0;
  g->selection_epoch = 0;
  for (unsigned i = 0; i < GW_CLIENTS; i++)
  {
    GatewayClient *c = &g->client[i];
    memset(&c->control, 0, sizeof(c->control));
    memset(&c->event, 0, sizeof(c->event));
    memset(c->tx, 0, sizeof(c->tx));
    c->state_ready = 0;
    for (unsigned j = 0; j < GW_HISTORY; j++)
      if (!c->history[j].pending)
        c->history[j].valid = 0;
  }
  link_lost(g);
}
static void take_state(Gateway *g, const DebugPacket *p, uint32_t now)
{
  uint64_t boot = V2Read64(p->data);
  if (g->boot && g->boot != boot)
    center_rebooted(g, boot);
  g->boot = boot;
  g->last_rx = now;
  if ((int32_t)(ReadU32(p->data + 8) - g->revision) < 0)
    return;
  if (g->state_valid && V2Read64(g->state.data) == boot &&
      (int32_t)(ReadU32(p->data + 12) - ReadU32(g->state.data + 12)) < 0)
    return;
  g->revision = ReadU32(p->data + 8);
  g->mode = p->data[20];
  selection(g, p->data[21], ReadU32(p->data + 28));
  g->state = *p;
  g->state_valid = 1;
  g->last_rx = now;
}
void Gateway_Uart(Gateway *g, const DebugPacket *wire, uint32_t now)
{
  if (wire->version != 2)
    return;
  DebugPacket p = *wire;
  if (!p.seq)
  {
    if (p.type == V2_STATE && p.len == V2_STATE_LEN)
    {
      take_state(g, &p, now);
      broadcast(g, 0, &p);
    }
    else if (g->linked && p.len >= 8 && V2Read64(p.data) == g->boot)
    {
      if (p.type == V2_PRESSURE && p.len == 125 && p.data[12] == g->selected &&
          ReadU32(p.data + 8) == g->selection_epoch)
        broadcast(g, 2, &p);
      else if ((p.type == V2_HIT && p.len == 28) || (p.type == V2_MOTOR && p.len == 22))
        broadcast(g, 1, &p);
    }
    return;
  }
  if (p.len < 8 || V2Read64(p.data) != g->epoch)
    return;
  p.len -= 8;
  memmove(p.data, p.data + 8, p.len);
  for (unsigned k = 0; k < GW_MAPS; k++)
  {
    GatewayMap *m = &g->maps[k];
    if (!m->active || m->wire.seq != p.seq)
      continue;
    bool valid = (p.type == V2_RESULT && p.len == V2_RESULT_LEN && p.data[0] == m->op &&
                  p.data[1] <= V2_UNKNOWN) ||
                 (p.type == V2_STATE && p.len == V2_STATE_LEN &&
                  (m->op == V2_HELLO || m->op == V2_GET_STATE)) ||
                 (p.type == V2_LEAF && p.len == V2_LEAF_LEN && m->op == V2_GET_LEAVES &&
                  p.data[12] >= 1 && p.data[12] <= 5);
    if (!valid)
      return;
    uint64_t boot = V2Read64(p.data + (p.type == V2_RESULT ? 4 : 0));
    if (g->boot && g->boot != boot)
    {
      center_rebooted(g, boot);
      return;
    }
    g->boot = boot;
    g->last_rx = now;
    if (p.type == V2_STATE)
      take_state(g, &p, now);
    if (p.type == V2_RESULT)
    {
      uint32_t revision = ReadU32(p.data + 12);
      if ((int32_t)(revision - g->revision) >= 0)
      {
        g->revision = revision;
        g->mode = p.data[32];
        selection(g, p.data[33], ReadU32(p.data + 28));
      }
    }
    if (k == 0)
    {
      if (m->op == V2_HELLO && p.type == V2_STATE)
      {
        g->linked = 1;
        announce_link(g);
      }
    }
    else if (map_matches(g, m))
    {
      GatewayClient *c = &g->client[m->client];
      GatewayHistory *h = &c->history[m->history];
      p.seq = m->client_seq;
      if (m->op == V2_OBSERVE_ENTER && p.type == V2_RESULT && p.data[1] == V2_COMPLETED &&
          p.data[2] == V2_OK && (c->subscribed & 4u) && g->mode == V2_HIT_TEST &&
          p.data[33] == g->selected && ReadU32(p.data + 28) == g->selection_epoch)
      {
        if (!c->token)
          c->token = ++g->token_counter;
        c->observer_ms = now;
        WriteU32(p.data + 24, c->token);
      }
      remember(h, &p);
      if (m->op == V2_GET_LEAVES)
      {
        unsigned mask = 0, expected = h->request.data[V2_REQUEST_HEADER];
        DebugPacket *end = NULL;
        for (unsigned j = 0; j < h->count; j++)
        {
          if (h->replies[j].type == V2_LEAF)
            mask |= 1u << (h->replies[j].data[12] - 1);
          if (h->replies[j].type == V2_RESULT)
            end = &h->replies[j];
        }
        unsigned wanted = expected ? 1u << (expected - 1) : 31u;
        if (p.type != V2_RESULT)
          (void)queue(g, c, 0, &p);
        if (end && (end->data[1] != V2_COMPLETED || mask == wanted))
        {
          (void)queue(g, c, 0, end);
          h->pending = 0;
          m->active = 0;
        }
        return; /* Missing leaf packets trigger the same UART request retry. */
      }
      (void)queue(g, c, 0, &p);
      if (V2Terminal(&p, m->op))
        h->pending = 0;
    }
    if (V2Terminal(&p, m->op))
      m->active = 0;
    return;
  }
}
void Gateway_Poll(Gateway *g, uint32_t now)
{
  for (unsigned i = 0; i < GW_CLIENTS; i++)
  {
    GatewayClient *c = &g->client[i];
    if (c->active && c->token && (uint32_t)(now - c->observer_ms) >= V2_OBSERVER_MS)
      unobserve(c);
    if (c->rx.active && (uint32_t)(now - c->rx.started) > V2_FRAGMENT_MS)
    {
      c->rx.active = 0;
      g->frame_errors++;
    }
  }
  if (g->linked && (uint32_t)(now - g->last_rx) >= V2_LINK_MS)
    link_lost(g);
  bool any = false;
  for (unsigned i = 0; i < GW_MAPS; i++)
    if (g->maps[i].active)
      any = true;
  if (g->uart_seq == 65535u && !any)
  {
    link_lost(g);
    g->epoch++;
    if (!g->epoch)
      g->epoch = 1;
    g->uart_seq = 0;
    for (unsigned i = 0; i < GW_CLIENTS; i++)
      if (g->client[i].active)
        g->client[i].close = 1;
  }
  if (!g->maps[0].active && (uint32_t)(now - g->last_internal) >= 500u)
  {
    DebugPacket q = {.version = 2, .type = V2_REQUEST, .len = V2_REQUEST_HEADER};
    q.data[0] = !g->linked ? V2_HELLO : V2_GET_STATE;
    if (g->linked && g->selected)
    {
      q.data[0] = observers(g) ? V2_OBSERVE_RENEW : V2_OBSERVE_LEAVE;
      q.len += 4;
      WriteU32(q.data + V2_REQUEST_HEADER, g->selection_epoch);
    }
    V2Write64(q.data + 1, g->boot);
    WriteU32(q.data + 9, g->revision);
    if (start_map(g, &g->maps[0], &q, 255, 0, now))
      g->last_internal = now;
  }
  /* Send new requests in UART sequence order. Retries retain their original identity. */
  for (unsigned pass = 0; pass < GW_MAPS; pass++)
  {
    GatewayMap *m = NULL;
    for (unsigned i = 0; i < GW_MAPS; i++)
    {
      GatewayMap *candidate = &g->maps[i];
      if (candidate->active && !candidate->timed_out && candidate->tries < 3 &&
          (uint32_t)(now - candidate->sent) >= V2_RETRY_MS &&
          (!m || candidate->wire.seq < m->wire.seq))
        m = candidate;
    }
    if (!m)
      break;
    if (!g->io.uart(g->io.ctx, &m->wire))
      break;
    m->sent = now;
    m->tries++;
  }
  for (unsigned i = 0; i < GW_MAPS; i++)
  {
    GatewayMap *m = &g->maps[i];
    if (!m->active)
      continue;
    if (!m->timed_out && (uint32_t)(now - m->started) >= V2_REQUEST_MS)
    {
      g->timeouts++;
      m->timed_out = 1;
      if (i == 0)
      {
        m->active = 0;
        g->linked = 0;
      }
      else
        uncertain(g, m, V2_TIMEOUT);
    }
    if (m->timed_out && (uint32_t)(now - m->started) >= 15000u)
    {
      if (map_matches(g, m))
        g->client[m->client].history[m->history].pending = 0;
      m->active = 0;
    }
  }
}
size_t Gateway_NextFragment(Gateway *g, unsigned i, uint8_t channel, uint8_t out[20])
{
  if (i >= GW_CLIENTS || channel > 2)
    return 0;
  GatewayClient *c = &g->client[i];
  GatewayTx *tx = &c->tx[channel];
  if (!c->active || c->close || !(c->subscribed & (1u << channel)))
    return 0;
  if (!tx->active)
  {
    DebugPacket packet;
    if (!Gateway_NextPacket(g, i, channel, &packet))
      return 0;
    tx->size = (uint16_t)WireEncode(tx->bytes, 2, packet.type, packet.seq, packet.data, packet.len);
    tx->transfer = ++c->transfer[channel];
    tx->index = 0;
    tx->count = (uint8_t)((tx->size + 15) / 16);
    tx->active = 1;
  }
  return V2Fragment(tx->bytes, tx->size, tx->transfer, tx->index, out);
}
void Gateway_FragmentSent(Gateway *g, unsigned i, uint8_t channel)
{
  if (i >= GW_CLIENTS || channel > 2)
    return;
  GatewayTx *tx = &g->client[i].tx[channel];
  if (tx->active && ++tx->index == tx->count)
    tx->active = 0;
}
void Gateway_ConnectUsb(Gateway *g)
{
  GatewayClient *c = &g->client[GW_USB_CLIENT];
  memset(c, 0, sizeof(*c));
  c->active = 1;
  c->subscribed = 7;
  c->handle = 0xffff;
  c->session = ++g->session_counter;
}
bool Gateway_NextPacket(Gateway *g, unsigned i, uint8_t channel, DebugPacket *out)
{
  if (i >= GW_CLIENTS || channel > 2)
    return false;
  GatewayClient *c = &g->client[i];
  if (!c->active || c->close || !(c->subscribed & (1u << channel)))
    return false;
  if (channel == V2_PRESSURE_CHANNEL)
  {
    if (!c->pressure_ready || !c->token) return false;
    *out = c->pressure;
    c->pressure_ready = 0;
    return true;
  }
  GatewayQueue *q = channel == V2_CONTROL_CHANNEL ? &c->control : &c->event;
  if (q->count)
  {
    *out = q->items[q->read];
    q->read = (uint8_t)((q->read + 1) % GW_CONTROL_DEPTH);
    q->count--;
    return true;
  }
  if (channel == V2_CONTROL_CHANNEL && c->state_ready)
  {
    *out = c->state;
    c->state_ready = 0;
    return true;
  }
  return false;
}
