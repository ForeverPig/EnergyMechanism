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
using namespace em;
int main(int argc,char **argv) {
  QApplication app(argc,argv);QApplication::setStyle("Fusion");app.setApplicationName("EnergyMechanismHost");app.setApplicationVersion("0.1.0");
  const auto fontPath=QCoreApplication::applicationDirPath()+"/resources/EnergyUI.otf";
  int fontId=QFontDatabase::addApplicationFont(fontPath);
  if(fontId>=0) app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).value(0),10));
  QCommandLineParser cli;cli.setApplicationDescription("能量机关便携上位机 · 电机只读");cli.addHelpOption();cli.addVersionOption();
  cli.addOption(QCommandLineOption("demo","模拟界面，不连接硬件"));
  cli.addOption(QCommandLineOption("smoke-test","生成模拟界面截图并退出","directory"));
  cli.addOption(QCommandLineOption("probe-usb","只读 USB 验证并退出","port"));
  cli.addOption(QCommandLineOption("probe-ble","只读 BLE 验证并退出","address"));
  cli.addOption(QCommandLineOption("report","只读验证 JSON 输出路径","file"));
  cli.addOption(QCommandLineOption("seconds","只读验证秒数","number","15"));
  cli.process(app);
  if(cli.isSet("probe-usb") || cli.isSet("probe-ble")) {
    if(cli.isSet("probe-usb") && cli.isSet("probe-ble")) return 2;
    DeviceClient client;int onlineTicks=0,stateTicks=0,leafTicks=0;QString lastMessage;
    QTimer timer;timer.setInterval(500);QObject::connect(&timer,&QTimer::timeout,&app,[&]{if(client.connected)onlineTicks++;if(client.fresh())stateTicks++;if(client.leaves[0].valid)leafTicks++;});timer.start();
    QObject::connect(&client,&DeviceClient::notice,&app,[&](const QString &s){lastMessage=s;});
    Endpoint endpoint;
    if(cli.isSet("probe-usb")) {endpoint.kind=Endpoint::Usb;endpoint.id=cli.value("probe-usb");}
    else {endpoint.kind=Endpoint::Ble;endpoint.id=cli.value("probe-ble");endpoint.device=QBluetoothDeviceInfo(QBluetoothAddress(endpoint.id),"EnergyMechanism",0);endpoint.device.setCoreConfigurations(QBluetoothDeviceInfo::LowEnergyCoreConfiguration);}
    const int seconds=qBound(5,cli.value("seconds").toInt(),120);client.open(endpoint);
    QTimer::singleShot(seconds*1000,&app,[&]{
      QJsonObject report{{"transport",endpoint.kind==Endpoint::Usb?"USB":"BLE"},{"endpoint",endpoint.id},{"device_id",client.deviceId},{"status",client.status},{"connected_ticks",onlineTicks},{"fresh_state_ticks",stateTicks},{"leaf_ticks",leafTicks},{"center_boot",QString::number(client.state.boot)},{"uart_errors",int(client.state.errors)},{"uart_drops",int(client.state.drops)},{"motor_commands_sent",0},{"last_notice",lastMessage}};
      const bool success=stateTicks>0 && leafTicks>0;report["passed"]=success;
      QFile file(cli.value("report"));if(!file.open(QIODevice::WriteOnly)) {app.exit(3);return;}file.write(QJsonDocument(report).toJson());file.close();client.close();QTimer::singleShot(1200,&app,[&,success]{app.exit(success?0:2);});
    });return app.exec();
  }
  Window window;window.show();
  if(cli.isSet("demo") || cli.isSet("smoke-test")) window.startDemo();
  if(cli.isSet("smoke-test")) {
    const QString dir=cli.value("smoke-test");if(!QDir().mkpath(dir))return 3;
    QTimer::singleShot(300,&window,[&window]{window.client()->enterDebug();});
    QTimer::singleShot(600,&window,[&window,dir]{window.resize(1366,768);window.showPage(0);window.grab().save(dir+"/01-home.png");window.showPage(1);});
    QTimer::singleShot(900,&window,[&window,dir]{window.grab().save(dir+"/02-debug.png");window.showPage(2);window.client()->observe(1);});
    QTimer::singleShot(4200,&window,[&window,dir]{window.grab().save(dir+"/03-pressure.png");window.showPage(3);});
    QTimer::singleShot(4500,&window,[&window,&app,dir]{bool ok=window.grab().save(dir+"/04-records.png") && window.client()->fresh();window.client()->close();QTimer::singleShot(200,&app,[&app,ok]{app.exit(ok?0:2);});});
  }
  return app.exec();
}
