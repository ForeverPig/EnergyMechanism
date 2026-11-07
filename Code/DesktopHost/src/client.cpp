#include "client.h"
#include <QDateTime>
#include <algorithm>
namespace em
{
static quint8 byte(const QByteArray &b, int p)
{
  return static_cast<quint8>(b[p]);
}
static bool readOnly(quint8 op)
{
  return op == V2_HELLO || op == V2_GET_STATE || op == V2_GET_LEAVES || op == V2_QUERY;
}
DeviceClient::DeviceClient(QObject *p, Factory f) : QObject(p), factory(std::move(f))
{
  clock.start();
  timer.setInterval(100);
  connect(&timer, &QTimer::timeout, this, &DeviceClient::tick);
  timer.start();
  reconnectTimer.setSingleShot(true);
  reconnectTimer.setInterval(2000);
  connect(&reconnectTimer, &QTimer::timeout, this,
          [this]
          {
            if (!intentional)
              start();
          });
}
bool DeviceClient::fresh() const
{
  return connected && centerOnline && state.valid && clock.elapsed() - lastState < 2000;
}
bool DeviceClient::busy() const
{
  for (const auto &p : pending)
  {
    auto op = byte(p.frame.data, 0);
    if (!readOnly(op) && op != V2_OBSERVE_RENEW && op != V2_OBSERVE_LEAVE)
      return true;
  }
  return false;
}
bool DeviceClient::canDebug() const
{
  return fresh() && (caps & V2_CAP_DEBUG) && state.stopped && !busy();
}
bool DeviceClient::canObserve(int slot) const
{
  return canDebug() && state.mode == V2_HIT_TEST && (caps & V2_CAP_PRESSURE) && slot >= 1 &&
         slot <= 5 && (state.online & (1 << (slot - 1)));
}
bool DeviceClient::canQueryLast() const
{
  return connected && lastOperation && !pendingOp(V2_QUERY);
}
qint64 DeviceClient::onlineSeconds(int slot) const
{
  if (slot < 1 || slot > 5 || !fresh() || onlineSince[slot - 1] < 0 ||
      !(state.online & (1 << (slot - 1))))
    return -1;
  return (clock.elapsed() - onlineSince[slot - 1]) / 1000;
}
void DeviceClient::open(const Endpoint &e)
{
  if (active())
    return;
  endpoint = e;
  intentional = false;
  everConnected = false;
  start();
}
void DeviceClient::invalidate()
{
  connected = false;
  centerOnline = false;
  state = {};
  leaves = {};
  motors = {};
  onlineSince.fill(-1);
  token = 0;
  wantObservation = false;
  pressureSeen = false;
  hitSeen = false;
  caps = 0;
  emit resetPlot();
}
void DeviceClient::start()
{
  if (transport)
    return;
  pending.clear();
  sequence = 0;
  lastOperation = 0;
  boot = epoch = session = 0;
  invalidate();
  if (factory)
    transport = factory(endpoint, this);
  else if (endpoint.kind == Endpoint::Usb)
    transport = new UsbTransport(endpoint.id, this);
  else if (endpoint.kind == Endpoint::Ble)
    transport = new BleTransport(endpoint.device, this);
  else
    transport = new DemoTransport(this);
  connect(transport, &Transport::progress, this,
          [this](const QString &s)
          {
            status = s;
            emit changed();
          });
  connect(transport, &Transport::ready, this,
          [this] { command(V2_HELLO, QByteArray::fromHex("010202")); });
  connect(transport, &Transport::received, this, &DeviceClient::receive);
  connect(transport, &Transport::closed, this,
          [this](const QString &r)
          {
            bool uncertain = false;
            for (auto it = pending.cbegin(); it != pending.cend(); ++it)
            {
              const auto op = byte(it->frame.data, 0);
              if (!readOnly(op) && op != V2_OBSERVE_RENEW && op != V2_OBSERVE_LEAVE)
              {
                uncertain = true;
                emit operation(it.key(), op, V2_UNKNOWN, V2_LINK_DOWN);
              }
            }
            pending.clear();
            invalidate();
            lastOperation = 0;
            transport->deleteLater();
            transport = nullptr;
            status = r;
            if (!intentional && everConnected)
            {
              status += "；2 秒后重连原设备";
              reconnectTimer.start();
            }
            emit notice(uncertain ? r + "；未完成操作结果未知，重连后不会自动重新执行" : r);
            emit changed();
            emit disconnected();
          });
  status = "正在连接…";
  emit changed();
  transport->start();
}
void DeviceClient::close()
{
  intentional = true;
  reconnectTimer.stop();
  wantObservation = false;
  if (transport)
    transport->stop();
  else
  {
    status = "未连接";
    emit changed();
    emit disconnected();
  }
}
bool DeviceClient::pendingOp(quint8 op) const
{
  for (const auto &p : pending)
    if (byte(p.frame.data, 0) == op)
      return true;
  return false;
}
quint16 DeviceClient::command(quint8 op, const QByteArray &args)
{
  if (!transport || (op != V2_HELLO && !connected))
    return 0;
  if (pendingOp(op))
    return 0;
  int inflight = 0;
  for (const auto &p : pending)
    if (!p.unknown)
      ++inflight;
  if (inflight >= 2)
    return 0;
  if (sequence == 65535)
  {
    emit notice("请求序号即将换代，重新建立会话");
    transport->stop();
    return 0;
  }
  auto seq = ++sequence;
  Frame f{V2_REQUEST, seq,
          request(op, readOnly(op) ? 0 : boot, readOnly(op) ? 0 : state.revision, args)};
  pending.insert(seq, {f, clock.elapsed(), clock.elapsed(), 1, false, false, 0});
  if (!readOnly(op) && op != V2_OBSERVE_RENEW && op != V2_OBSERVE_LEAVE)
    lastOperation = seq;
  emit requestSent(op, false);
  transport->send(f);
  emit changed();
  return seq;
}
void DeviceClient::refresh()
{
  if (!connected)
    return;
  command(V2_GET_STATE);
  command(V2_GET_LEAVES, QByteArray(1, 0));
  lastLeaves = clock.elapsed();
}
void DeviceClient::enterDebug()
{
  if (canDebug())
    command(V2_ENTER_DEBUG);
}
void DeviceClient::exitDebug()
{
  if (canDebug())
  {
    wantObservation = false;
    command(V2_EXIT_DEBUG);
  }
}
void DeviceClient::observe(int slot)
{
  if (!canObserve(slot))
    return;
  if (command(V2_OBSERVE_ENTER, QByteArray(1, static_cast<char>(slot))))
    wantObservation = true;
}
void DeviceClient::leaveObservation()
{
  wantObservation = false;
  emit resetPlot();
  if (token)
  {
    QByteArray a(4, 0);
    put32(a, 0, token);
    command(V2_OBSERVE_LEAVE, a);
  }
}
void DeviceClient::calibrate(int slot)
{
  if (canObserve(slot) && (caps & V2_CAP_CALIBRATE))
    command(V2_CALIBRATE, QByteArray(1, static_cast<char>(slot)));
}
void DeviceClient::queryLast()
{
  if (canQueryLast())
  {
    QByteArray a(2, 0);
    put16(a, 0, lastOperation);
    command(V2_QUERY, a);
  }
}
void DeviceClient::tick()
{
  const auto now = clock.elapsed();
  if (!fresh())
    onlineSince.fill(-1);
  const auto keys = pending.keys();
  for (auto seq : keys)
  {
    if (!pending.contains(seq))
      continue;
    auto &p = pending[seq];
    const auto op = byte(p.frame.data, 0);
    if (now - p.started >= 7000 && !p.unknown)
    {
      p.unknown = true;
      emit operation(seq, op, V2_UNKNOWN, V2_TIMEOUT);
      if (!readOnly(op))
        emit notice(QString("请求 %1 结果未知，可查询最近操作；不会自动重新执行").arg(seq));
      if (op == V2_HELLO && transport)
      {
        transport->stop();
        return;
      }
    }
    if (now - p.started >= 16000)
    {
      pending.remove(seq);
      continue;
    }
    if (!p.accepted && !p.unknown && p.tries < 3 && now - p.sent >= 1000 && transport)
    {
      p.sent = now;
      ++p.tries;
      const auto frame = p.frame;
      emit requestSent(op, true);
      transport->send(frame);
    }
  }
  if (connected && centerOnline)
  {
    if (token && wantObservation && now - lastRenew >= 1000)
    {
      QByteArray a(4, 0);
      put32(a, 0, token);
      if (command(V2_OBSERVE_RENEW, a))
        lastRenew = now;
    }
    else if (token && !wantObservation && !pendingOp(V2_OBSERVE_LEAVE))
      leaveObservation();
    if (now - lastLeaves >= 3000 && !busy())
    {
      if (command(V2_GET_LEAVES, QByteArray(1, 0)))
        lastLeaves = now;
    }
    if (now - lastState >= 1500)
      command(V2_GET_STATE);
  }
  if (connected && !centerOnline && now - lastWire >= 1500)
    command(V2_GET_STATE);
  if (connected && now - lastWire >= 6000 && transport)
  {
    emit notice("未收到有效消息，重新连接原设备");
    transport->stop();
    return;
  }
  emit changed();
}
void DeviceClient::receive(const Frame &f)
{
  const auto &b = f.data;
  const int expected = f.type == V2_RESULT     ? 36
                       : f.type == V2_SESSION  ? 56
                       : f.type == V2_STATE    ? 48
                       : f.type == V2_LEAF     ? 44
                       : f.type == V2_PRESSURE ? 125
                       : f.type == V2_HIT      ? 28
                       : f.type == V2_MOTOR    ? 22
                                               : 0;
  if (!expected || b.size() != expected || (f.seq && !pending.contains(f.seq)))
    return;
  lastWire = clock.elapsed();
  quint8 op = f.seq ? byte(pending[f.seq].frame.data, 0) : 0;
  if (f.seq && f.type == V2_SESSION && op != V2_HELLO && op != V2_QUERY)
    return;
  if (f.seq && f.type == V2_STATE && op != V2_GET_STATE && op != V2_QUERY)
    return;
  if (f.type == V2_SESSION)
  {
    if (byte(b, 48) != 2 || u16(b, 46) != 184)
      return;
    const auto newBoot = u64(b, 28);
    if (connected && (epoch != u64(b, 12) || session != u64(b, 20)))
    {
      if (transport)
        transport->stop();
      return;
    }
    if (boot && boot != newBoot)
    {
      for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        if (!readOnly(byte(it->frame.data, 0)))
          emit operation(it.key(), byte(it->frame.data, 0), V2_UNKNOWN, V2_LINK_DOWN);
      pending.clear();
      lastOperation = 0;
      state = {};
      leaves = {};
      motors = {};
      onlineSince.fill(-1);
      token = 0;
      wantObservation = false;
      pressureSeen = false;
      hitSeen = false;
      emit resetPlot();
      emit notice("中心已重启，旧操作结果不能推定，新状态已重新同步");
    }
    boot = newBoot;
    epoch = u64(b, 12);
    session = u64(b, 20);
    caps = u32(b, 36);
    centerOnline = byte(b, 44) != 0;
    if (!centerOnline)
    {
      token = 0;
      wantObservation = false;
      state.valid = false;
      emit resetPlot();
    }
    connected = true;
    everConnected = true;
    deviceId = QString::fromLatin1(b.left(12).toHex());
    status = centerOnline ? "已连接" : "已连接 · 中心离线";
    if (f.seq)
      pending.remove(f.seq);
    emit changed();
    if (op == V2_HELLO)
      refresh();
    return;
  }
  if (!connected)
    return;
  if (f.type == V2_RESULT)
  {
    if (!f.seq || byte(b, 0) != op || byte(b, 1) > V2_UNKNOWN)
      return;
    const int phase = byte(b, 1), code = byte(b, 2);
    if (u64(b, 4) == boot && static_cast<qint32>(u32(b, 12) - state.revision) >= 0)
    {
      state.revision = u32(b, 12);
      state.mode = byte(b, 32);
      if (state.selection != u32(b, 28) || state.slot != byte(b, 33))
      {
        emit resetPlot();
        pressureSeen = false;
      }
      state.selection = u32(b, 28);
      state.slot = byte(b, 33);
    }
    if (op == V2_OBSERVE_ENTER && phase == V2_COMPLETED && code == V2_OK && u64(b, 4) == boot)
    {
      token = u32(b, 24);
      lastRenew = clock.elapsed();
    }
    if (op == V2_OBSERVE_LEAVE || (op == V2_OBSERVE_RENEW && code != V2_OK) ||
        ((op == V2_STOP || op == V2_EXIT_DEBUG) && phase == V2_COMPLETED && code == V2_OK))
    {
      token = 0;
      wantObservation = false;
      emit resetPlot();
    }
    if (op == V2_GET_LEAVES && phase == V2_COMPLETED && code == V2_OK &&
        pending[f.seq].leafMask != 31)
      return;
    if (phase == V2_ACCEPTED && op != V2_QUERY)
      pending[f.seq].accepted = true;
    else if (phase == V2_UNKNOWN)
      pending[f.seq].unknown = true;
    else
      pending.remove(f.seq);
    emit operation(f.seq, op, phase, code);
    if (!readOnly(op) && op != V2_OBSERVE_RENEW)
    {
      QString text = phase == V2_ACCEPTED  ? "已受理，等待设备完成"
                     : phase == V2_UNKNOWN ? "结果未知"
                                           : codeName(code);
      if (op == V2_CALIBRATE && phase == V2_COMPLETED && code == V2_OK)
        text = "重新校准完成 · " + calibrationName(byte(b, 3));
      if (op == V2_CALIBRATE && phase == V2_FAILED)
        text = "校准失败 · " + calibrationName(byte(b, 3)) + " · " + codeName(code);
      emit notice(QString("请求 %1：%2").arg(f.seq).arg(text));
    }
    else if (op == V2_QUERY)
      emit notice(QString("最近操作：%1（阶段 %2）").arg(codeName(code)).arg(phase));
    if (op == V2_OBSERVE_ENTER && token && !wantObservation)
      leaveObservation();
    emit changed();
    return;
  }
  if (!centerOnline || u64(b, 0) != boot)
    return;
  if (f.type == V2_STATE)
  {
    if (static_cast<qint32>(u32(b, 8) - state.revision) < 0 ||
        (state.valid && static_cast<qint32>(u32(b, 12) - state.publication) < 0))
      return;
    if (byte(b, 21) > 5)
      return;
    if (state.slot != byte(b, 21) || state.selection != u32(b, 28))
    {
      pressureSeen = false;
      emit resetPlot();
    }
    state = {true,        boot,        u32(b, 8),   u32(b, 12),  u32(b, 16),  u32(b, 28),
             u32(b, 32),  u32(b, 36),  u32(b, 40),  byte(b, 20), byte(b, 21), byte(b, 22),
             byte(b, 23), byte(b, 24), byte(b, 25), byte(b, 26), byte(b, 27)};
    for (int i = 0; i < 5; ++i)
    {
      if (!(state.online & (1 << i)))
        onlineSince[i] = -1;
      else if (onlineSince[i] < 0)
        onlineSince[i] = clock.elapsed();
    }
    lastState = clock.elapsed();
    if (!state.slot || state.mode != V2_HIT_TEST)
    {
      token = 0;
      wantObservation = false;
      emit resetPlot();
    }
    if (f.seq)
      pending.remove(f.seq);
  }
  else if (f.type == V2_LEAF)
  {
    int slot = byte(b, 12);
    if (!f.seq || op != V2_GET_LEAVES || slot < 1 || slot > 5)
      return;
    leaves[slot - 1] = {true,        byte(b, 13) != 0, byte(b, 15) != 0,
                        byte(b, 14), u32(b, 16),       u32(b, 24),
                        u16(b, 20),  u16(b, 22),       QString::fromLatin1(b.mid(28, 12).toHex())};
    pending[f.seq].leafMask |= static_cast<quint8>(1 << (slot - 1));
  }
  else if (f.type == V2_PRESSURE)
  {
    if (!token || !wantObservation || !fresh() || state.mode != V2_HIT_TEST ||
        byte(b, 12) != state.slot || u32(b, 8) != state.selection)
      return;
    if (pressureSeen && static_cast<qint32>(u32(b, 13) - pressureSeq) <= 0)
      return;
    Pressure p;
    p.slot = byte(b, 12);
    p.selection = u32(b, 8);
    p.sequence = u32(b, 13);
    p.received = clock.elapsed();
    unsigned rings = 0;
    for (int i = 0; i < 10; i++)
    {
      int at = 25 + i * 10, r = byte(b, at);
      if (r < 1 || r > 10 || (rings & (1u << (r - 1))))
        return;
      rings |= 1u << (r - 1);
      p.values[r - 1] = {r,
                         byte(b, at + 1),
                         u16(b, at + 2),
                         u16(b, at + 4),
                         static_cast<qint16>(u16(b, at + 6)),
                         u16(b, at + 8)};
    }
    pressureSeen = true;
    pressureSeq = p.sequence;
    emit pressure(p);
    return;
  }
  else if (f.type == V2_HIT)
  {
    if (byte(b, 12) < 1 || byte(b, 12) > 5 || byte(b, 15) < 1 || byte(b, 15) > 10)
      return;
    auto n = u32(b, 8);
    if (hitSeen && static_cast<qint32>(n - lastHit) <= 0)
      return;
    if (hitSeen && n - lastHit > 1)
      emit notice(QString("诊断击打流缺少 %1 条记录").arg(n - lastHit - 1));
    lastHit = n;
    hitSeen = true;
    emit hit({QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), n, byte(b, 12), byte(b, 15),
              u16(b, 16)});
    return;
  }
  else if (f.type == V2_MOTOR)
  {
    int id = byte(b, 8);
    if (id < 1 || id > 8)
      return;
    motors[id - 1] = {true,
                      byte(b, 20) != 0,
                      static_cast<qint16>(u16(b, 12)),
                      static_cast<qint16>(u16(b, 14)),
                      byte(b, 9),
                      u32(b, 16)};
  }
  emit changed();
}
} // namespace em
