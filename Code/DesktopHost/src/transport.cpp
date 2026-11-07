#include "transport.h"
#include <QBluetoothUuid>
#include <QRandomGenerator>
#include <cmath>
namespace em {
static QBluetoothUuid uuid(int n) {return QBluetoothUuid(QString("af51000%1-7b3d-4b28-9f02-6c6d454d0001").arg(n));}
UsbTransport::UsbTransport(const QString &name,QObject *p):Transport(p),portName(name) {
  timer.setInterval(1000);
  connect(&timer,&QTimer::timeout,this,[this]{
    if(!opened) {if(++attempts>3) fail("USB 握手超时：请确认 ESP32 已运行支持 USB V2 的 Gateway 固件");else control(UsbOpen);}
    else if(clock.elapsed()-lastRx>=3000) fail("USB 心跳超时");else control(UsbPing);
  });
  connect(&port,&QSerialPort::readyRead,this,[this]{
    for(auto f:parser.feed(port.readAll(),static_cast<quint32>(clock.elapsed()))) {
      if(f.data.size()<8 || u64(f.data,0)!=nonce) continue;
      if(f.type==UsbAck && !f.seq && f.data.size()==24 && f.data.mid(8,4)=="EMB2" && f.data[12]==2) {
        lastRx=clock.elapsed();if(!opened) {opened=true;emit ready();}
      } else if(opened && f.type==UsbPong && !f.seq && f.data.size()==8) lastRx=clock.elapsed();
      else if(opened && f.type<0x70) {lastRx=clock.elapsed();f.data.remove(0,8);emit received(f);}
    }
  });
  connect(&port,&QSerialPort::errorOccurred,this,[this](QSerialPort::SerialPortError e){
    if(e!=QSerialPort::NoError && e!=QSerialPort::NotOpenError) {
      QString reason="USB："+port.errorString();
#ifdef Q_OS_LINUX
      if(e==QSerialPort::PermissionError) reason+="。请检查串口占用或将当前用户加入 dialout 组后重新登录，详见使用说明。";
#endif
      fail(reason);
    }
  });
}
void UsbTransport::start() {
  clock.start();nonce=QRandomGenerator::system()->generate64();if(!nonce) nonce=1;
  port.setPortName(portName);port.setBaudRate(1000000);port.setDataBits(QSerialPort::Data8);
  port.setParity(QSerialPort::NoParity);port.setStopBits(QSerialPort::OneStop);port.setFlowControl(QSerialPort::NoFlowControl);
  emit progress("正在打开 USB 串口…");
  if(!port.open(QIODevice::ReadWrite)) {fail("无法打开串口："+port.errorString());return;}
  // Native USB CDC: never perform DTR/RTS download/reset sequences.
  control(UsbOpen);attempts=1;timer.start();
}
void UsbTransport::control(quint8 type) {QByteArray b(8,0);put64(b,0,nonce);port.write(encode({type,0,b}));}
void UsbTransport::send(const Frame &f) {
  if(!opened || finished) return;
  if(port.bytesToWrite()>8192) {fail("USB 发送队列拥塞");return;}
  QByteArray b(8,0);put64(b,0,nonce);b+=f.data;port.write(encode({f.type,f.seq,b}));
}
void UsbTransport::stop() {
  if(finished) return;
  if(opened) {control(UsbClose);port.flush();}
  timer.stop();opened=false;QTimer::singleShot(60,this,[this]{fail("已断开");});
}
void UsbTransport::fail(const QString &r) {
  if(finished) return;
  finished=true;opened=false;timer.stop();port.close();emit closed(r);
}
BleTransport::BleTransport(const QBluetoothDeviceInfo &d,QObject *p):Transport(p),device(d) {
  deadline.setSingleShot(true);
  connect(&deadline,&QTimer::timeout,this,[this]{fail(closing?"已断开":"蓝牙操作超时");});
}
void BleTransport::start() {
  clock.start();emit progress("正在连接蓝牙…");deadline.start(15000);
  controller=QLowEnergyController::createCentral(device,this);
  connect(controller,&QLowEnergyController::connected,this,[this]{emit progress("正在读取蓝牙服务…");controller->discoverServices();});
  connect(controller,&QLowEnergyController::discoveryFinished,this,[this]{
    service=controller->createServiceObject(uuid(0),this);
    if(!service) {fail("设备没有能量机关 V2 服务");return;}
    connect(service,&QLowEnergyService::stateChanged,this,[this](QLowEnergyService::ServiceState s){if(s==QLowEnergyService::RemoteServiceDiscovered) setup();});
    connect(service,&QLowEnergyService::errorOccurred,this,[this](QLowEnergyService::ServiceError){fail("蓝牙服务操作失败");});
    service->discoverDetails();
  });
  connect(controller,&QLowEnergyController::disconnected,this,[this]{fail(closing?"已断开":"蓝牙连接断开");});
  connect(controller,&QLowEnergyController::errorOccurred,this,[this](QLowEnergyController::Error){fail("蓝牙："+controller->errorString());});
  controller->connectToDevice();
}
void BleTransport::setup() {
  info=service->characteristic(uuid(1));command=service->characteristic(uuid(2));
  for(int i=0;i<3;i++) channels[i]=service->characteristic(uuid(3+i));
  if(!info.isValid() || !command.isValid()) {fail("蓝牙特征值不完整");return;}
  for(const auto &c:channels) if(!c.isValid() || !c.clientCharacteristicConfiguration().isValid()) {fail("设备缺少通知通道");return;}
  connect(service,&QLowEnergyService::characteristicRead,this,[this](const QLowEnergyCharacteristic &c,const QByteArray &b){
    if(c!=info) return;
    if(b.size()!=16 || b.left(4)!="EMB2" || b[4]!=2 || u16(b,6)!=194 || u32(b,12)<2) {fail("蓝牙协议版本不兼容");return;}
    step=0;subscribeNext();
  });
  connect(service,&QLowEnergyService::descriptorWritten,this,[this](const QLowEnergyDescriptor &,const QByteArray &){++step;subscribeNext();});
  connect(service,&QLowEnergyService::characteristicChanged,this,[this](const QLowEnergyCharacteristic &c,const QByteArray &b){
    if(!live || closing) return;
    for(int i=0;i<3;i++) if(c==channels[i]) {Frame f;if(parsers[i].feed(b,static_cast<quint32>(clock.elapsed()),f)) emit received(f);}
  });
  connect(service,&QLowEnergyService::characteristicWritten,this,[this](const QLowEnergyCharacteristic &c,const QByteArray &){
    if(c!=command || !awaitingWrite) return;
    awaitingWrite=false;deadline.stop();if(!writes.isEmpty()) writes.dequeue();pump();
  });
  service->readCharacteristic(info);
}
void BleTransport::subscribeNext() {
  if(finished || !service) return;
  if(closing) {
    if(step>=3) {controller->disconnectFromDevice();return;}
    service->writeDescriptor(channels[2-step].clientCharacteristicConfiguration(),QByteArray::fromHex("0000"));return;
  }
  // CONTROL on/off/on retires a business session even if Windows retained the physical link.
  static const int channel[]={0,0,0,1,2};
  if(step>=5) {live=true;deadline.stop();for(auto &p:parsers) p.reset();emit ready();return;}
  service->writeDescriptor(channels[channel[step]].clientCharacteristicConfiguration(),QByteArray::fromHex(step==1?"0000":"0100"));
}
void BleTransport::send(const Frame &f) {
  if(!live || closing || finished) return;
  const auto packets=fragments(f,++transfer);
  if(writes.size()+packets.size()>128) {fail("蓝牙发送队列拥塞");return;}
  for(const auto &p:packets) writes.enqueue(p);
  pump();
}
void BleTransport::pump() {
  if(awaitingWrite || writes.isEmpty() || closing || finished) return;
  awaitingWrite=true;deadline.start(3000);service->writeCharacteristic(command,writes.head(),QLowEnergyService::WriteWithResponse);
}
void BleTransport::stop() {
  if(finished || closing) return;
  closing=true;awaitingWrite=false;writes.clear();deadline.start(1000);
  if(live && service) {live=false;step=0;subscribeNext();}
  else if(controller) controller->disconnectFromDevice();else fail("已断开");
}
void BleTransport::fail(const QString &r) {
  if(finished) return;
  finished=true;live=false;deadline.stop();if(controller) controller->disconnectFromDevice();emit closed(r);
}
Frame DemoTransport::state(quint16 seq) const {
  QByteArray b(48,0);put64(b,0,1234);put32(b,8,revision);put32(b,12,ticks+1);put32(b,16,ticks*50);
  b[20]=static_cast<char>(mode);b[21]=static_cast<char>(slot);b[22]=31;b[23]=31;b[24]=1;b[25]=1;b[27]=1;
  put32(b,28,selection);put32(b,32,127);return {V2_STATE,seq,b};
}
void DemoTransport::start() {
  running=true;timer.setInterval(50);
  connect(&timer,&QTimer::timeout,this,[this]{
    if(!running) return;
    ++ticks;if(ticks%10==0) emit received(state());
    if(watching) {
      QByteArray b(125,0);put64(b,0,1234);put32(b,8,selection);b[12]=static_cast<char>(slot);put32(b,13,ticks);put32(b,17,ticks*50);put32(b,21,ticks*50);
      for(int i=0;i<10;i++) {int p=25+i*10;b[p]=static_cast<char>(i+1);
        auto d=static_cast<qint16>(20*std::sin(ticks*0.16+i)+((ticks+i*13)%90<3?300:0));
        b[p+1]=d>100?1:0;put16(b,p+2,static_cast<quint16>(2000+d));put16(b,p+4,2000);put16(b,p+6,static_cast<quint16>(d));put16(b,p+8,320);}
      emit received({V2_PRESSURE,0,b});
      if(ticks%40==0) {QByteArray h(28,0);put64(h,0,1234);put32(h,8,ticks/40);h[12]=static_cast<char>(slot);h[13]=1;h[15]=static_cast<char>(1+(ticks/40)%10);put16(h,16,320);emit received({V2_HIT,0,h});}
    }
  });timer.start();QTimer::singleShot(0,this,[this]{emit ready();});
}
void DemoTransport::stop(){running=false;timer.stop();emit closed("已退出模拟");}
void DemoTransport::send(const Frame &q) {
  QTimer::singleShot(5,this,[this,q]{
    if(!running || q.data.size()<13) return;
    quint8 op=static_cast<quint8>(q.data[0]);
    if(op==V2_HELLO) {QByteArray b(56,0);b.replace(0,4,"DEMO");put64(b,12,9);put64(b,20,1);put64(b,28,1234);put32(b,36,127);put32(b,40,revision);b[44]=1;b[45]=3;put16(b,46,184);b[48]=2;b[49]=2;b[50]=1;put32(b,52,3000);emit received({V2_SESSION,q.seq,b});return;}
    if(op==V2_GET_STATE) {emit received(state(q.seq));return;}
    if(op==V2_GET_LEAVES) for(int i=1;i<=5;i++){QByteArray b(44,0);put64(b,0,1234);put32(b,8,ticks+1);b[12]=static_cast<char>(i);b[13]=1;b[14]=1;b[15]=1;put32(b,16,100);b.replace(28,12,QByteArray(12,static_cast<char>(i)));b[42]=5;b[43]=1;emit received({V2_LEAF,q.seq,b});}
    if(op==V2_ENTER_DEBUG) {mode=5;revision++;}
    if(op==V2_EXIT_DEBUG || op==V2_STOP) {mode=0;slot=0;watching=false;revision++;}
    if(op==V2_OBSERVE_ENTER && q.data.size()==14) {slot=static_cast<quint8>(q.data[13]);watching=true;selection++;revision++;}
    if(op==V2_OBSERVE_LEAVE) watching=false;
    QByteArray b(36,0);b[0]=static_cast<char>(op);b[1]=1;put64(b,4,1234);put32(b,12,revision);put32(b,20,q.seq);put32(b,24,watching?42:0);put32(b,28,selection);b[32]=static_cast<char>(mode);b[33]=static_cast<char>(slot);
    if(op==V2_CALIBRATE) {b[1]=0;emit received({V2_RESULT,q.seq,b});QTimer::singleShot(700,this,[this,q,b]()mutable{if(!running)return;b[1]=1;b[3]=1;emit received({V2_RESULT,q.seq,b});});}
    else emit received({V2_RESULT,q.seq,b});
    emit received(state());
  });
}
}
