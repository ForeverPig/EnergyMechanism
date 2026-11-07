#include "center_service.h"
#include <string.h>

static void changed(CenterService *s)
{
  s->revision++;
  s->state_revision++;
}
static void clear_selection(CenterService *s)
{
  if (s->selected)
  {
    s->selected = 0;
    s->selection_epoch++;
    s->pressure_ready = 0;
    changed(s);
  }
}
static void state(CenterService *s, DebugPacket *p, uint16_t seq, uint32_t now)
{
  memset(p, 0, sizeof(*p));
  p->version = 2;
  p->type = V2_STATE;
  p->seq = seq;
  p->len = V2_STATE_LEN;
  V2Write64(p->data, s->boot);
  WriteU32(p->data + 8, s->revision);
  WriteU32(p->data + 12, s->state_revision);
  WriteU32(p->data + 16, now);
  p->data[20] = s->mode;
  p->data[21] = s->selected;
  for (unsigned i = 0; i < 5; i++)
    if (s->view.leaf[i].online)
      p->data[22] |= (uint8_t)(1u << i);
  p->data[23] = s->view.can_ready;
  p->data[24] = s->stopped;
  p->data[25] = s->view.motor_valid;
  p->data[26] = s->view.motor_moving;
  p->data[27] = s->view.motor_known;
  WriteU32(p->data + 28, s->selection_epoch);
  WriteU32(p->data + 32, V2_CAPABILITIES);
  WriteU32(p->data + 36, s->view.uart_errors);
  WriteU32(p->data + 40, s->view.uart_drops);
  uint32_t age = now - s->lease_ms;
  WriteU32(p->data + 44, s->selected && age < V2_UPSTREAM_MS ? V2_UPSTREAM_MS - age : 0);
}
static void result(CenterService *s, CenterRequest *r, uint8_t phase, uint8_t code)
{
  r->count = 1;
  r->next = 0;
  V2MakeResult(&r->reply[0], r->request.seq, r->request.data[0], phase, code, s->boot, s->revision);
  uint8_t *p = r->reply[0].data;
  WriteU32(p + 24, s->selected ? s->selection_epoch : 0);
  WriteU32(p + 28, s->selection_epoch);
  p[32] = s->mode;
  p[33] = s->selected;
}
static bool emit(CenterService *s, uint64_t epoch, const DebugPacket *p)
{
  if (!p->seq)
    return s->io.send(s->io.ctx, p);
  DebugPacket wire = *p;
  wire.len += 8;
  memmove(wire.data + 8, wire.data, p->len);
  V2Write64(wire.data, epoch);
  return s->io.send(s->io.ctx, &wire);
}
static void reject(CenterService *s, uint64_t epoch, uint16_t seq, uint8_t op, uint8_t code)
{
  DebugPacket p;
  V2MakeResult(&p, seq, op, V2_REJECTED, code, s->boot, s->revision);
  WriteU32(p.data + 28, s->selection_epoch);
  p.data[32] = s->mode;
  p.data[33] = s->selected;
  (void)emit(s, epoch, &p);
}
void CenterService_Init(CenterService *s, uint64_t boot, CenterBackend backend)
{
  memset(s, 0, sizeof(*s));
  s->boot = boot;
  s->io = backend;
  s->revision = 1;
  s->state_revision = 1;
  s->cal_index = 255;
}
void CenterService_View(CenterService *s, const CenterView *v, uint32_t now)
{
  if (!s->view_started)
  {
    s->view_started = 1;
    s->startup_ms = now;
  }
  uint8_t old = s->stopped;
  bool stationary = v->motor_known && v->motor_valid == v->motor_known && !v->motor_moving;
  if (!stationary)
  {
    s->stop_tracking = 0;
    s->stopped = 0;
  }
  else if (!s->stop_tracking)
  {
    s->stop_tracking = 1;
    s->stop_since = now;
  }
  else if ((uint32_t)(now - s->stop_since) >= 500u)
    s->stopped = 1;
  s->view = *v;
  if (old != s->stopped)
    changed(s);
  if (s->mode == V2_HIT_TEST && !s->stopped)
  {
    s->mode = V2_IDLE;
    clear_selection(s);
    changed(s);
  }
  if (s->selected && !v->leaf[s->selected - 1].online)
    clear_selection(s);
}
static void leaf_packet(CenterService *s, DebugPacket *p, uint16_t seq, uint8_t slot, uint8_t count)
{
  const CenterLeaf *l = &s->view.leaf[slot - 1];
  memset(p, 0, sizeof(*p));
  p->version = 2;
  p->type = V2_LEAF;
  p->seq = seq;
  p->len = V2_LEAF_LEN;
  V2Write64(p->data, s->boot);
  WriteU32(p->data + 8, s->state_revision);
  p->data[12] = slot;
  p->data[13] = l->online;
  p->data[14] = l->status;
  p->data[15] = l->uid_valid;
  WriteU32(p->data + 16, l->age);
  WriteU16(p->data + 20, l->adc_errors);
  WriteU16(p->data + 22, l->adc_drops);
  WriteU32(p->data + 24, l->drops);
  memcpy(p->data + 28, l->uid, 12);
  WriteU16(p->data + 40, seq);
  p->data[42] = count;
  p->data[43] = 1;
}
void CenterService_Request(CenterService *s, const DebugPacket *wire, uint32_t now)
{
  if (wire->version != 2 || wire->type != V2_REQUEST || !wire->seq ||
      wire->len < 8 + V2_REQUEST_HEADER)
    return;
  uint64_t epoch = V2Read64(wire->data);
  DebugPacket q = *wire;
  q.len -= 8;
  memmove(q.data, q.data + 8, q.len);
  uint8_t op = q.data[0];
  if (!s->boot || !epoch)
  {
    reject(s, epoch, q.seq, op, V2_NOT_READY);
    return;
  }
  if (op == V2_HELLO && epoch != s->gateway && q.len == V2_REQUEST_HEADER)
  {
    for (unsigned i = 0; i < 8; i++)
      if (s->retired[i] == epoch)
      {
        reject(s, epoch, q.seq, op, V2_STALE_STATE);
        return;
      }
    if (s->cal_index != 255)
    {
      reject(s, epoch, q.seq, op, V2_BUSY);
      return;
    }
    s->retired[s->retired_next++ % 8] = s->gateway;
    s->gateway = epoch;
    s->highwater = 0;
    memset(s->cache, 0, sizeof(s->cache));
    clear_selection(s);
  }
  if (epoch != s->gateway)
  {
    reject(s, epoch, q.seq, op, V2_NOT_READY);
    return;
  }
  for (unsigned i = 0; i < CENTER_CACHE; i++)
  {
    CenterRequest *r = &s->cache[i];
    if (r->valid && r->request.seq == q.seq)
    {
      if (r->request.len != q.len || memcmp(r->request.data, q.data, q.len))
        reject(s, epoch, q.seq, op, V2_REQUEST_CONFLICT);
      else
        r->next = 0;
      return;
    }
  }
  if (q.seq <= s->highwater)
  {
    reject(s, epoch, q.seq, op, V2_REPLAY_EXPIRED);
    return;
  }
  /* Reserve storage before accepting an operation; never evict a pending calibration/reply. */
  CenterRequest *r = NULL;
  for (unsigned n = 0; n < CENTER_CACHE; n++)
  {
    unsigned i = (s->cache_next + n) % CENTER_CACHE;
    if (!s->cache[i].valid || (!s->cache[i].pending && s->cache[i].next == s->cache[i].count))
    {
      r = &s->cache[i];
      s->cache_next = (uint8_t)((i + 1) % CENTER_CACHE);
      break;
    }
  }
  if (!r)
  {
    s->highwater = q.seq;
    reject(s, epoch, q.seq, op, V2_BUSY);
    return;
  }
  memset(r, 0, sizeof(*r));
  r->valid = 1;
  r->request = q;
  r->epoch = epoch;
  s->highwater = q.seq;
  result(s, r, V2_REJECTED, V2_BAD_ARGUMENT);
  uint16_t n = (uint16_t)(q.len - V2_REQUEST_HEADER);
  const uint8_t *a = q.data + V2_REQUEST_HEADER;
  if (op == V2_HELLO || op == V2_GET_STATE)
  {
    if (n)
      return;
    state(s, &r->reply[0], q.seq, now);
    return;
  }
  if (op == V2_GET_LEAVES)
  {
    if (n != 1 || a[0] > 5)
      return;
    uint8_t count = a[0] ? 1 : 5;
    DebugPacket end;
    V2MakeResult(&end, q.seq, op, V2_COMPLETED, V2_OK, s->boot, s->revision);
    for (uint8_t i = 0; i < count; i++)
      leaf_packet(s, &r->reply[i], q.seq, a[0] ? a[0] : (uint8_t)(i + 1), count);
    WriteU32(end.data + 28, s->selection_epoch);
    end.data[32] = s->mode;
    end.data[33] = s->selected;
    r->reply[count] = end;
    r->count = (uint8_t)(count + 1);
    return;
  }
  if (op != V2_ENTER_DEBUG && op != V2_EXIT_DEBUG && op != V2_STOP && op != V2_OBSERVE_ENTER &&
      op != V2_OBSERVE_RENEW && op != V2_OBSERVE_LEAVE && op != V2_CALIBRATE)
  {
    result(s, r, V2_REJECTED, V2_UNSUPPORTED);
    return;
  }
  if (V2Read64(q.data + 1) != s->boot)
  {
    result(s, r, V2_REJECTED, V2_STALE_STATE);
    return;
  }
  if (op != V2_STOP && op != V2_OBSERVE_RENEW && op != V2_OBSERVE_LEAVE &&
      ReadU32(q.data + 9) != s->revision)
  {
    result(s, r, V2_REJECTED, V2_STALE_STATE);
    return;
  }
  if (op == V2_OBSERVE_LEAVE || op == V2_OBSERVE_RENEW)
  {
    if (n != 4)
      return;
    if (ReadU32(a) != s->selection_epoch || !s->selected)
    {
      result(s, r, V2_REJECTED, V2_STALE_STATE);
      return;
    }
    if (op == V2_OBSERVE_LEAVE)
      clear_selection(s);
    else
      s->lease_ms = now;
    result(s, r, V2_COMPLETED, V2_OK);
    return;
  }
  if (!s->stopped)
  {
    result(s, r, V2_REJECTED, V2_NOT_STOPPED);
    return;
  }
  if (op == V2_ENTER_DEBUG || op == V2_EXIT_DEBUG || op == V2_STOP)
  {
    if (n)
      return;
    if (s->cal_index != 255)
    {
      result(s, r, V2_REJECTED, V2_BUSY);
      return;
    }
    uint8_t mode = op == V2_ENTER_DEBUG ? V2_HIT_TEST : V2_IDLE;
    if (mode != s->mode)
    {
      s->mode = mode;
      changed(s);
    }
    if (mode == V2_IDLE)
      clear_selection(s);
    result(s, r, V2_COMPLETED, V2_OK);
    return;
  }
  if (s->mode != V2_HIT_TEST)
  {
    result(s, r, V2_REJECTED, V2_WRONG_MODE);
    return;
  }
  if (n != 1 || a[0] < 1 || a[0] > 5)
    return;
  if (!s->view.leaf[a[0] - 1].online)
  {
    result(s, r, V2_REJECTED, V2_LEAF_OFFLINE);
    return;
  }
  if (op == V2_OBSERVE_ENTER)
  {
    if (a[0] != s->selected)
    {
      s->selected = a[0];
      s->selection_epoch++;
      s->pressure_ready = 0;
      changed(s);
    }
    s->lease_ms = now;
    result(s, r, V2_COMPLETED, V2_OK);
    return;
  }
  if (s->cal_index != 255)
  {
    result(s, r, V2_REJECTED, V2_BUSY);
    return;
  }
  /* Let the unchanged V1 leaf's 2 s command cache expire after a center reboot. */
  if (!s->view_started || (uint32_t)(now - s->startup_ms) < 2100u)
  {
    result(s, r, V2_REJECTED, V2_NOT_READY);
    return;
  }
  s->cal_index = (uint8_t)(r - s->cache);
  s->cal_slot = a[0];
  s->cal_seq++;
  s->cal_start = now;
  s->cal_sent = now - 300u;
  s->cal_tries = 0;
  s->cal_ack = 0;
  s->cal_running = 0;
  r->pending = 1;
  result(s, r, V2_ACCEPTED, V2_OK);
}
static void cal_finish(CenterService *s, uint8_t phase, uint8_t code, uint8_t detail)
{
  CenterRequest *r = &s->cache[s->cal_index];
  r->pending = 0;
  result(s, r, phase, code);
  r->reply[0].data[3] = detail;
  s->cal_index = 255;
}
void CenterService_CalAck(CenterService *s, uint8_t slot, uint8_t seq, uint8_t code, uint8_t status,
                          uint32_t now)
{
  if (s->cal_index == 255 || slot != s->cal_slot || seq != s->cal_seq)
    return;
  if (code != RES_OK || (status & 0x80u))
  {
    cal_finish(s, V2_FAILED, V2_FAULT_ACTIVE, status);
    return;
  }
  if (!s->cal_ack)
    s->cal_ack_ms = now;
  s->cal_ack = 1;
  s->cal_running = 1; /* A retry ACK may already report READY. */
}
void CenterService_Poll(CenterService *s, uint32_t now)
{
  if (s->selected && (uint32_t)(now - s->lease_ms) >= V2_UPSTREAM_MS)
    clear_selection(s);
  if (s->cal_index != 255)
  {
    CenterLeaf *l = &s->view.leaf[s->cal_slot - 1];
    if (!s->stopped)
      cal_finish(s, V2_FAILED, V2_NOT_STOPPED, l->status);
    else if (!l->online)
      cal_finish(s, V2_FAILED, V2_LEAF_OFFLINE, l->status);
    else if ((uint32_t)(now - s->cal_start) >= 5000u)
      cal_finish(s, V2_UNKNOWN, V2_TIMEOUT, l->status);
    else if (s->cal_ack && s->cal_running && (int32_t)(l->heartbeat - s->cal_ack_ms) > 0)
    {
      if (l->status == 1)
        cal_finish(s, V2_COMPLETED, V2_OK, 1);
      else if (l->status != 0)
        cal_finish(s, V2_FAILED, V2_FAULT_ACTIVE, l->status);
    }
    else if (!s->cal_ack && s->cal_tries < 3 && (uint32_t)(now - s->cal_sent) >= 300u)
    {
      (void)s->io.calibrate(s->io.ctx, s->cal_slot, s->cal_seq);
      s->cal_sent = now;
      s->cal_tries++;
    }
  }
  unsigned budget = 4;
  for (unsigned i = 0; i < CENTER_CACHE && budget; i++)
  {
    CenterRequest *r = &s->cache[i];
    while (r->valid && r->next < r->count && budget)
    {
      if (!emit(s, r->epoch, &r->reply[r->next]))
        return;
      r->next++;
      budget--;
    }
  }
  if (s->boot && s->gateway && (uint32_t)(now - s->last_state) >= 500u)
  {
    DebugPacket p;
    s->state_revision++;
    state(s, &p, 0, now);
    if (emit(s, 0, &p))
      s->last_state = now;
  }
  if (s->pressure_ready && budget)
  {
    if (emit(s, 0, &s->pressure))
      s->pressure_ready = 0;
  }
}
void CenterService_Pressure(CenterService *s, uint8_t slot, const PressureSnapshot *snap,
                            uint32_t now)
{
  if (!s->gateway || !s->selected || slot != s->selected || s->mode != V2_HIT_TEST)
    return;
  DebugPacket *p = &s->pressure;
  memset(p, 0, sizeof(*p));
  p->version = 2;
  p->type = V2_PRESSURE;
  p->len = 125;
  V2Write64(p->data, s->boot);
  WriteU32(p->data + 8, s->selection_epoch);
  p->data[12] = slot;
  WriteU32(p->data + 13, ++s->snapshot_seq);
  WriteU32(p->data + 17, now);
  WriteU32(p->data + 21, snap->timestamp);
  uint8_t v1[106];
  SnapshotPack(snap, slot, v1);
  memcpy(p->data + 25, v1 + 6, 100);
  s->pressure_ready = 1;
}
void CenterService_Hit(CenterService *s, uint8_t slot, const uint8_t d[8], uint32_t now)
{
  if (!s->gateway)
    return;
  DebugPacket p = {.version = 2, .type = V2_HIT, .len = 28};
  V2Write64(p.data, s->boot);
  WriteU32(p.data + 8, ++s->hit_seq);
  p.data[12] = slot;
  p.data[13] = 1;
  memcpy(p.data + 14, d, 8);
  WriteU32(p.data + 22, now);
  /* Diagnostic V1 hit only: no reliable-event or training-score capability is advertised. */
  (void)emit(s, 0, &p);
}
void CenterService_Motor(CenterService *s, const uint8_t v1[14])
{
  if (!s->gateway)
    return;
  DebugPacket p = {.version = 2, .type = V2_MOTOR, .len = 22};
  V2Write64(p.data, s->boot);
  memcpy(p.data + 8, v1, 14);
  (void)emit(s, 0, &p);
}
