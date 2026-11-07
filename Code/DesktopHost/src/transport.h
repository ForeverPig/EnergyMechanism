#pragma once
#include "protocol.h"
#include <QBluetoothDeviceInfo>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QSerialPort>
#include <QTimer>
#include <QQueue>
#include <array>
namespace em
{
struct Endpoint
{
  enum Kind
  {
    Usb,
    Ble,
    Demo
  } kind = Usb;
  QString id, name;
  QBluetoothDeviceInfo device;
};
class Transport : public QObject
{
  Q_OBJECT
public:
  using QObject::QObject;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void send(const Frame &) = 0;
signals:
  void ready();
  void received(const em::Frame &frame);
  void closed(const QString &reason);
  void progress(const QString &text);
};
class UsbTransport final : public Transport
{
  Q_OBJECT
  QSerialPort port;
  QTimer timer;
  QElapsedTimer clock;
  StreamParser parser;
  QString portName;
  quint64 nonce = 0;
  qint64 lastRx = 0;
  int attempts = 0;
  bool opened = false, finished = false;
  void control(quint8 type);
  void fail(const QString &reason);

public:
  explicit UsbTransport(const QString &name, QObject *parent = nullptr);
  void start() override;
  void stop() override;
  void send(const Frame &) override;
};
class BleTransport final : public Transport
{
  Q_OBJECT
  QBluetoothDeviceInfo device;
  QLowEnergyController *controller = nullptr;
  QLowEnergyService *service = nullptr;
  QLowEnergyCharacteristic command, info;
  std::array<QLowEnergyCharacteristic, 3> channels;
  std::array<Reassembler, 3> parsers;
  QQueue<QByteArray> writes;
  QTimer deadline;
  QElapsedTimer clock;
  quint16 transfer = 0;
  int step = 0;
  bool live = false, closing = false, finished = false, awaitingWrite = false;
  void setup();
  void subscribeNext();
  void pump();
  void fail(const QString &reason);

public:
  explicit BleTransport(const QBluetoothDeviceInfo &, QObject *parent = nullptr);
  void start() override;
  void stop() override;
  void send(const Frame &) override;
};
/* Explicit --demo only: no hardware access. */
class DemoTransport final : public Transport
{
  Q_OBJECT
  QTimer timer;
  quint32 ticks = 0, revision = 1, selection = 0;
  quint8 mode = 0, slot = 0;
  bool watching = false, running = false;
  Frame state(quint16 seq = 0) const;

public:
  using Transport::Transport;
  void start() override;
  void stop() override;
  void send(const Frame &) override;
};
} // namespace em
