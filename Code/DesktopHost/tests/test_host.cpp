#include "client.h"
#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
using namespace em;
class FakeTransport : public Transport {
public:
  using Transport::Transport;QVector<Frame> sent;
  void start() override {emit ready();}
  void stop() override {emit closed("test close");}
  void send(const Frame &f) override {sent.append(f);}
  void inject(const Frame &f){emit received(f);}
};
class HostTests : public QObject {
  Q_OBJECT
private slots:
  void crcAndStream(){
    QCOMPARE(DebugCrc(reinterpret_cast<const uint8_t*>("123456789"),9),quint16(0x29b1));
    Frame f{1,17,request(1,0,0,QByteArray::fromHex("010202"))};auto b=encode(f);StreamParser p;
    QVERIFY(p.feed("boot log\n"+b.left(9),1).isEmpty());auto frames=p.feed(b.mid(9)+b,2);QCOMPARE(frames.size(),2);QCOMPARE(frames[0].data,f.data);QCOMPARE(frames[1].seq,f.seq);
    auto bad=b;bad[8]=static_cast<char>(bad[8]^1);QCOMPARE(p.feed(bad+b,3).size(),1);QVERIFY(p.errors()>0);
    QVERIFY(p.feed(b.left(8),10).isEmpty());QVERIFY(p.feed(b.mid(8),111).isEmpty());QCOMPARE(p.feed(b,112).size(),1);
  }
  void bleFragmentation(){
    Frame f{V2_PRESSURE,0,QByteArray(125,'x')};auto packets=fragments(f,6);Reassembler r;Frame out;
    for(qsizetype i=0;i<packets.size();i++) QCOMPARE(r.feed(packets[i],0,out),i==packets.size()-1);
    QCOMPARE(out.data,f.data);r.reset();QVERIFY(!r.feed(packets[0],0,out));QVERIFY(!r.feed(packets[2],1,out));QVERIFY(!r.feed(packets[1],2,out));
    r.reset();QVERIFY(!r.feed(packets[0],0,out));QVERIFY(!r.feed(packets[1],1001,out));
  }
  void pythonVectors(){
    QFile file(QStringLiteral(EM_SOURCE_DIR "/tests/vectors.json"));QVERIFY(file.open(QIODevice::ReadOnly));
    for(const auto &v:QJsonDocument::fromJson(file.readAll()).array()) {auto o=v.toObject();Frame f{quint8(o["type"].toInt()),quint16(o["seq"].toInt()),QByteArray::fromHex(o["payload"].toString().toLatin1())};QCOMPARE(encode(f).toHex(),o["wire"].toString().toLatin1());}
  }
  void gatesSessionsAndSharedSelection(){
    FakeTransport *fake=nullptr;DeviceClient c(nullptr,[&](const Endpoint &,QObject *p){fake=new FakeTransport(p);return fake;});
    QSignalSpy pressureSpy(&c,&DeviceClient::pressure);QSignalSpy operations(&c,&DeviceClient::operation);
    c.open({Endpoint::Usb,"test","test",{}});QCOMPARE(fake->sent.size(),1);QCOMPARE(quint8(fake->sent[0].data[0]),quint8(V2_HELLO));
    QByteArray session(56,0);put64(session,12,7);put64(session,20,1);put64(session,28,99);put32(session,36,127);session[44]=1;session[45]=3;put16(session,46,184);session[48]=2;put32(session,52,3000);
    fake->inject({V2_SESSION,1,session});QVERIFY(c.connected);QCOMPARE(fake->sent.size(),3);
    QByteArray state(48,0);put64(state,0,99);put32(state,8,1);put32(state,12,1);put32(state,32,127);state[22]=31;state[23]=31;
    fake->inject({V2_STATE,2,state});QVERIFY(!c.canDebug());auto count=fake->sent.size();c.enterDebug();QCOMPARE(fake->sent.size(),count);
    // Complete the initial five-leaf batch; a premature RESULT must not retire it.
    QByteArray result(36,0);result[0]=V2_GET_LEAVES;result[1]=1;put64(result,4,99);put32(result,12,1);fake->inject({V2_RESULT,3,result});
    for(int i=1;i<=5;i++){QByteArray leaf(44,0);put64(leaf,0,99);leaf[12]=char(i);leaf[13]=1;leaf[14]=1;fake->inject({V2_LEAF,3,leaf});}
    fake->inject({V2_RESULT,3,result});
    state[24]=1;state[25]=1;state[27]=1;put32(state,12,2);fake->inject({V2_STATE,0,state});QVERIFY(c.canDebug());
    c.enterDebug();auto q=fake->sent.last();QCOMPARE(quint8(q.data[0]),quint8(V2_ENTER_DEBUG));QCOMPARE(u64(q.data,1),quint64(99));
    result[0]=V2_ENTER_DEBUG;result[32]=5;put32(result,12,2);fake->inject({V2_RESULT,q.seq,result});
    state[20]=5;put32(state,8,2);put32(state,12,3);fake->inject({V2_STATE,0,state});c.observe(1);q=fake->sent.last();
    result[0]=V2_OBSERVE_ENTER;result[33]=1;put32(result,12,3);put32(result,24,42);put32(result,28,1);fake->inject({V2_RESULT,q.seq,result});QVERIFY(c.observing());
    state[21]=1;put32(state,8,3);put32(state,12,4);put32(state,28,1);fake->inject({V2_STATE,0,state});
    QByteArray pressure(125,0);put64(pressure,0,99);put32(pressure,8,1);pressure[12]=1;put32(pressure,13,1);for(int i=0;i<10;i++)pressure[25+i*10]=char(i+1);
    fake->inject({V2_PRESSURE,0,pressure});QCOMPARE(pressureSpy.size(),1);fake->inject({V2_PRESSURE,0,pressure});QCOMPARE(pressureSpy.size(),1);
    // Another controller switches the shared selection; the old snapshot must be rejected.
    state[21]=2;put32(state,8,4);put32(state,12,5);put32(state,28,2);fake->inject({V2_STATE,0,state});put32(pressure,13,2);fake->inject({V2_PRESSURE,0,pressure});QCOMPARE(pressureSpy.size(),1);
    pressure[12]=2;put32(pressure,8,2);fake->inject({V2_PRESSURE,0,pressure});QCOMPARE(pressureSpy.size(),2);
    c.calibrate(2);q=fake->sent.last();result[0]=V2_CALIBRATE;result[1]=0;result[33]=2;put32(result,12,4);put32(result,28,2);fake->inject({V2_RESULT,q.seq,result});QVERIFY(c.busy());
    result[1]=1;result[3]=1;fake->inject({V2_RESULT,q.seq,result});QVERIFY(!c.busy());QVERIFY(operations.size()>=4);
    c.calibrate(2);q=fake->sent.last();QVERIFY(c.busy());
    put64(session,28,200);fake->inject({V2_SESSION,0,session});QVERIFY(!c.busy());QVERIFY(!c.observing());QVERIFY(!c.fresh());
    auto operationCount=operations.size();fake->inject({V2_RESULT,q.seq,result});QCOMPARE(operations.size(),operationCount);
    c.close();QVERIFY(!c.connected);QVERIFY(!c.observing());
  }
  void retriesPreserveIdentity(){
    FakeTransport *fake=nullptr;DeviceClient c(nullptr,[&](const Endpoint &,QObject *p){fake=new FakeTransport(p);return fake;});c.open({});
    QTRY_VERIFY_WITH_TIMEOUT(fake->sent.size()>=2,1800);QCOMPARE(fake->sent[0].seq,fake->sent[1].seq);QCOMPARE(fake->sent[0].data,fake->sent[1].data);c.close();
  }
};
QTEST_MAIN(HostTests)
#include "test_host.moc"
