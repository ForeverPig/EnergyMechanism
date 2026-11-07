#pragma once
#include "transport.h"
#include <QMap>
#include <functional>
namespace em
{
struct DeviceState
{
  bool valid = false;
  quint64 boot = 0;
  quint32 revision = 0, publication = 0, time = 0, selection = 0, caps = 0, errors = 0, drops = 0;
  quint8 mode = 0, slot = 0, online = 0, can = 0, stopped = 0, motorValid = 0, motorMoving = 0,
         motorKnown = 0;
};
struct Leaf
{
  bool valid = false, online = false, uidValid = false;
  quint8 calibration = 0;
  quint32 age = 0xffffffff, canDrops = 0;
  quint16 adcErrors = 0, adcDrops = 0;
  QString uid;
};
struct Motor
{
  bool valid = false, online = false;
  int rpm = 0, current = 0, temp = 0;
  quint32 age = 0;
};
struct Sensor
{
  int ring = 0, state = 0, raw = 0, baseline = 0, delta = 0, peak = 0;
};
struct Pressure
{
  quint8 slot = 0;
  quint32 selection = 0, sequence = 0;
  std::array<Sensor, 10> values{};
  qint64 received = 0;
};
struct Hit
{
  QString time;
  quint32 sequence = 0;
  int slot = 0, ring = 0, peak = 0;
};
class DeviceClient : public QObject
{
  Q_OBJECT
public:
  using Factory = std::function<Transport *(const Endpoint &, QObject *)>;
  explicit DeviceClient(QObject *parent = nullptr, Factory factory = {});
  DeviceState state;
  std::array<Leaf, 5> leaves{};
  std::array<Motor, 8> motors{};
  Endpoint endpoint;
  QString status = "未连接", deviceId;
  bool connected = false, centerOnline = false;
  bool active() const
  {
    return transport != nullptr || reconnectTimer.isActive();
  }
  bool fresh() const;
  bool busy() const;
  bool observing() const
  {
    return token != 0;
  }
  bool canDebug() const;
  bool canObserve(int slot) const;
  bool canQueryLast() const;
  quint64 gatewayEpoch() const
  {
    return epoch;
  }
  quint64 clientSession() const
  {
    return session;
  }
  void open(const Endpoint &endpoint);
  void close();
  void refresh();
  void enterDebug();
  void exitDebug();
  void observe(int slot);
  void leaveObservation();
  void calibrate(int slot);
  void queryLast();
signals:
  void changed();
  void notice(const QString &text);
  void disconnected();
  void pressure(const em::Pressure &sample);
  void hit(const em::Hit &event);
  void resetPlot();
  void operation(quint16 seq, int opcode, int phase, int code);
  void requestSent(int opcode, bool retry);

private:
  struct Pending
  {
    Frame frame;
    qint64 started = 0, sent = 0;
    int tries = 1;
    bool accepted = false, unknown = false;
    quint8 leafMask = 0;
  };
  Factory factory;
  Transport *transport = nullptr;
  QTimer timer, reconnectTimer;
  QElapsedTimer clock;
  QMap<quint16, Pending> pending;
  quint16 sequence = 0, lastOperation = 0;
  quint64 boot = 0, epoch = 0, session = 0;
  quint32 caps = 0, token = 0, pressureSeq = 0, lastHit = 0;
  qint64 lastState = 0, lastLeaves = 0, lastRenew = 0, lastWire = 0;
  bool intentional = false, everConnected = false, wantObservation = false, pressureSeen = false,
       hitSeen = false;
  void start();
  void tick();
  void receive(const Frame &frame);
  quint16 command(quint8 op, const QByteArray &args = {});
  bool pendingOp(quint8 op) const;
  void invalidate();
};
} // namespace em
Q_DECLARE_METATYPE(em::Pressure)
Q_DECLARE_METATYPE(em::Hit)
