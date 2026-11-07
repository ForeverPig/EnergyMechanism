#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QScrollArea>
#include <QScrollBar>
using namespace em;
int main(int argc, char **argv)
{
  QApplication app(argc, argv);
  QApplication::setStyle("Fusion");
  app.setApplicationName("EnergyMechanismHost");
  app.setApplicationVersion("0.1.0");
  const auto fontPath = QCoreApplication::applicationDirPath() + "/resources/EnergyUI.otf";
  int fontId = QFontDatabase::addApplicationFont(fontPath);
  if (fontId >= 0)
    app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).value(0), 10));
  QCommandLineParser cli;
  cli.setApplicationDescription("能量机关便携上位机 · 电机只读");
  cli.addHelpOption();
  cli.addVersionOption();
  cli.addOption(QCommandLineOption("demo", "模拟界面，不连接硬件"));
  cli.addOption(QCommandLineOption("smoke-test", "生成模拟界面截图并退出", "directory"));
  cli.addOption(QCommandLineOption("probe-usb", "只读 USB 验证并退出", "port"));
  cli.addOption(QCommandLineOption("probe-ble", "只读 BLE 验证并退出", "address"));
  cli.addOption(QCommandLineOption("report", "只读验证 JSON 输出路径", "file"));
  cli.addOption(QCommandLineOption("seconds", "只读验证秒数", "number", "15"));
  cli.process(app);
  if (cli.isSet("probe-usb") || cli.isSet("probe-ble"))
  {
    if (cli.isSet("probe-usb") && cli.isSet("probe-ble"))
      return 2;
    DeviceClient client;
    int onlineTicks = 0, stateTicks = 0, leafTicks = 0, requests = 0, retries = 0, controls = 0,
        closed = 0;
    QString lastMessage;
    quint64 initialBoot = 0;
    quint32 initialErrors = 0, initialDrops = 0;
    QObject::connect(&client, &DeviceClient::requestSent, &app,
                     [&](int op, bool retry)
                     {
                       ++requests;
                       if (retry)
                         ++retries;
                       if (op != V2_HELLO && op != V2_GET_STATE && op != V2_GET_LEAVES)
                         ++controls;
                     });
    QObject::connect(&client, &DeviceClient::disconnected, &app, [&] { ++closed; });
    QTimer timer;
    timer.setInterval(500);
    QObject::connect(&timer, &QTimer::timeout, &app,
                     [&]
                     {
                       if (client.connected)
                         onlineTicks++;
                       if (client.fresh())
                       {
                         stateTicks++;
                         if (!initialBoot)
                         {
                           initialBoot = client.state.boot;
                           initialErrors = client.state.errors;
                           initialDrops = client.state.drops;
                         }
                       }
                       if (client.leaves[0].valid)
                         leafTicks++;
                     });
    timer.start();
    QObject::connect(&client, &DeviceClient::notice, &app,
                     [&](const QString &s) { lastMessage = s; });
    Endpoint endpoint;
    if (cli.isSet("probe-usb"))
    {
      endpoint.kind = Endpoint::Usb;
      endpoint.id = cli.value("probe-usb");
    }
    else
    {
      endpoint.kind = Endpoint::Ble;
      endpoint.id = cli.value("probe-ble");
      endpoint.device = QBluetoothDeviceInfo(QBluetoothAddress(endpoint.id), "EnergyMechanism", 0);
      endpoint.device.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);
    }
    const int seconds = qBound(5, cli.value("seconds").toInt(), 120);
    client.open(endpoint);
    QTimer::singleShot(
        seconds * 1000, &app,
        [&]
        {
          QJsonObject report{{"transport", endpoint.kind == Endpoint::Usb ? "USB" : "BLE"},
                             {"endpoint", endpoint.id},
                             {"device_id", client.deviceId},
                             {"status", client.status},
                             {"connected_ticks", onlineTicks},
                             {"fresh_state_ticks", stateTicks},
                             {"leaf_ticks", leafTicks},
                             {"center_boot", QString::number(client.state.boot)},
                             {"gateway_epoch", QString::number(client.gatewayEpoch())},
                             {"client_session", QString::number(client.clientSession())},
                             {"uart_errors", int(client.state.errors)},
                             {"uart_drops", int(client.state.drops)},
                             {"uart_error_delta", int(client.state.errors - initialErrors)},
                             {"uart_drop_delta", int(client.state.drops - initialDrops)},
                             {"requests", requests},
                             {"retries", retries},
                             {"control_requests", controls},
                             {"unexpected_disconnects", closed},
                             {"last_notice", lastMessage}};
          const bool success = client.fresh() && client.state.boot == initialBoot &&
                               stateTicks >= qMax(1, (seconds - 5) * 2) && leafTicks > 0 &&
                               controls == 0 && closed == 0 &&
                               client.state.errors == initialErrors &&
                               client.state.drops == initialDrops;
          report["passed"] = success;
          QFile file(cli.value("report"));
          if (!file.open(QIODevice::WriteOnly))
          {
            app.exit(3);
            return;
          }
          file.write(QJsonDocument(report).toJson());
          file.close();
          client.close();
          QTimer::singleShot(1200, &app, [&, success] { app.exit(success ? 0 : 2); });
        });
    return app.exec();
  }
  Window window;
  bool smokeOk = true;
  window.show();
  if (cli.isSet("demo") || cli.isSet("smoke-test"))
    window.startDemo();
  if (cli.isSet("smoke-test"))
  {
    const QString dir = cli.value("smoke-test");
    if (!QDir().mkpath(dir))
      return 3;
    const auto capture = [&window, &smokeOk, dir](const QString &name)
    { smokeOk = window.grab().save(dir + '/' + name + ".png") && smokeOk; };
    const auto scroll = [&window](bool bottom)
    {
      for (auto *area : window.findChildren<QScrollArea *>())
        if (area->isVisible())
          area->verticalScrollBar()->setValue(bottom ? area->verticalScrollBar()->maximum() : 0);
    };
    QTimer::singleShot(300, &window, [&window] { window.client()->enterDebug(); });
    QTimer::singleShot(600, &window,
                       [&window, capture]
                       {
                         window.resize(1366, 768);
                         window.showPage(0);
                         capture("01-home");
                         window.showPage(1);
                       });
    QTimer::singleShot(900, &window,
                       [&window, capture]
                       {
                         capture("02-debug");
                         window.showPage(2);
                         window.client()->observe(1);
                       });
    QTimer::singleShot(4200, &window,
                       [capture, scroll]
                       {
                         capture("03-pressure");
                         scroll(true);
                       });
    QTimer::singleShot(4500, &window,
                       [&window, capture, scroll]
                       {
                         capture("04-curve");
                         scroll(false);
                         window.client()->calibrate(1);
                       });
    QTimer::singleShot(4700, &window, [capture] { capture("05-calibration-accepted"); });
    QTimer::singleShot(5400, &window,
                       [&window, capture]
                       {
                         capture("06-calibration-completed");
                         window.showPage(3);
                       });
    QTimer::singleShot(5600, &window,
                       [&window, &smokeOk, capture]
                       {
                         smokeOk = smokeOk && window.client()->fresh();
                         capture("07-records");
                         window.showPage(0);
                         window.resize(900, 600);
                       });
    QTimer::singleShot(5800, &window,
                       [&window, capture]
                       {
                         capture("08-compact");
                         window.client()->findChild<DemoTransport *>()->setFault(true);
                         window.client()->refresh();
                       });
    QTimer::singleShot(6000, &window,
                       [&window, capture]
                       {
                         capture("09-fault");
                         window.showPage(1);
                       });
    QTimer::singleShot(6200, &window,
                       [&window, capture]
                       {
                         capture("10-fault-details");
                         window.client()->close();
                         window.showPage(0);
                       });
    QTimer::singleShot(6500, &window,
                       [&app, &smokeOk, capture]
                       {
                         capture("11-offline");
                         app.exit(smokeOk ? 0 : 2);
                       });
  }
  return app.exec();
}
