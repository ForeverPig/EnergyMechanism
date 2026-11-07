#include "window.h"
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QScreen>
#include <QResizeEvent>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QStyle>
#include <QVBoxLayout>
#include <bit>
#include <algorithm>
namespace em
{
static QLabel *label(const QString &text, const char *name = "", QWidget *parent = nullptr)
{
  auto *l = new QLabel(text, parent);
  l->setObjectName(name);
  return l;
}
static QFrame *card(QVBoxLayout **layout = nullptr)
{
  auto *w = new QFrame;
  w->setObjectName("card");
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(22, 20, 22, 20);
  l->setSpacing(14);
  if (layout)
    *layout = l;
  return w;
}
static QTableWidget *table(const QStringList &headers, int rows)
{
  auto *t = new QTableWidget(rows, headers.size());
  t->setHorizontalHeaderLabels(headers);
  t->verticalHeader()->hide();
  t->setEditTriggers(QAbstractItemView::NoEditTriggers);
  t->setSelectionBehavior(QAbstractItemView::SelectRows);
  t->setShowGrid(false);
  t->setAlternatingRowColors(true);
  t->horizontalHeader()->setHighlightSections(false);
  t->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  t->verticalHeader()->setDefaultSectionSize(36);
  t->setMinimumHeight((rows + 1) * 36 + 8);
  return t;
}
static void cell(QTableWidget *t, int row, int col, const QString &text)
{
  auto *item = t->item(row, col);
  if (!item)
  {
    item = new QTableWidgetItem;
    t->setItem(row, col, item);
  }
  if (item->text() != text)
    item->setText(text);
  item->setToolTip(text);
}
static QComboBox *choices(const QStringList &items)
{
  auto *c = new QComboBox;
  c->addItems(items);
  return c;
}
static QWidget *scrollable(QWidget *page)
{
  auto *s = new QScrollArea;
  s->setWidgetResizable(true);
  s->setFrameShape(QFrame::NoFrame);
  s->setWidget(page);
  return s;
}
Window::Window(QWidget *parent) : QMainWindow(parent), model(this), scanner(this)
{
  setWindowTitle("Energy Mechanism · 能量机关");
  setWindowIcon(appIcon());
  const auto available = QGuiApplication::primaryScreen()->availableGeometry().size();
  resize(qMin(1440, available.width() - 30), qMin(960, available.height() - 60));
  setMinimumSize(860, 500);
  setStyleSheet(R"(
    QWidget{font-size:14px;color:#233a51;} QMainWindow,QScrollArea,QStackedWidget{background:#f3f6fa;}
    QFrame#sidebar{background:#14253b;border:none;} QLabel#brand{font-size:19px;font-weight:700;color:#fff;}
    QLabel#brandSub{font-size:11px;color:#88a4bb;} QLabel#sideNote{color:#91a8bf;font-size:12px;}
    QPushButton#nav{background:transparent;border:none;color:#aebed0;text-align:left;padding:14px 16px;border-radius:9px;}
    QPushButton#nav:checked{background:#24445a;color:#fff;font-weight:600;} QPushButton#nav:hover{background:#20374f;}
    QFrame#card{background:#fff;border:1px solid #e2e9f0;border-radius:14px;}
    QLabel#title{font-size:27px;font-weight:700;color:#182c43;} QLabel#subtitle,QLabel#muted{color:#7b8b9e;}
    QLabel#section{font-size:17px;font-weight:600;color:#273e55;} QLabel#stat{font-size:28px;font-weight:650;color:#19364c;}
    QLabel#pill{background:#e5f5f1;color:#147c6b;border-radius:7px;padding:7px 12px;font-size:12px;}
    QLabel#hint{background:#eef4fa;color:#577089;border-radius:8px;padding:12px;}
    QLabel#message{background:#eaf4f7;color:#396779;border-radius:8px;padding:10px 14px;}
    QPushButton{background:#fff;border:1px solid #d4dfe9;border-radius:7px;padding:8px 15px;min-height:20px;}
    QPushButton:hover{background:#edf7f5;border-color:#69baa9;} QPushButton:pressed{background:#d5eee7;}
    QPushButton#primary{background:#0d9e89;color:white;border-color:#0d9e89;font-weight:600;}
    QPushButton#primary:hover{background:#0a8b78;} QPushButton:disabled{background:#f2f5f8;color:#9aa8b7;border-color:#e2e8ee;}
    QComboBox,QSpinBox{background:#fff;border:1px solid #d6e0ea;border-radius:7px;padding:8px;min-height:20px;}
    QComboBox:disabled,QSpinBox:disabled{background:#f4f6f9;color:#9aa8b7;} QComboBox::drop-down{width:23px;border:0;}
    QTableWidget{background:white;alternate-background-color:#f7f9fc;border:0;selection-background-color:#dff2ed;selection-color:#204f47;}
    QHeaderView::section{background:#f0f5f9;border:0;padding:10px 8px;color:#788a9e;font-size:12px;}
    QTableWidget::item{padding:5px 8px;} QCheckBox{spacing:5px;} QScrollBar:vertical{width:9px;background:transparent;}
    QScrollBar::handle:vertical{background:#ccd7e2;border-radius:4px;min-height:30px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
  )");
  auto *root = new QWidget;
  auto *outer = new QHBoxLayout(root);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  setCentralWidget(root);
  auto *side = new QFrame;
  sidebar = side;
  side->setObjectName("sidebar");
  side->setFixedWidth(190);
  auto *sl = new QVBoxLayout(side);
  sl->setContentsMargins(16, 30, 16, 20);
  sl->setSpacing(8);
  auto *logo = new QLabel;
  logo->setPixmap(appIcon().pixmap(48, 48));
  sl->addWidget(logo);
  sl->addSpacing(6);
  sl->addWidget(label("能量机关", "brand"));
  sl->addWidget(label("ENERGY MECHANISM", "brandSub"));
  sl->addSpacing(34);
  const QStringList names = {"设备总览", "击打调试", "压力观察", "诊断记录"};
  auto *group = new QButtonGroup(this);
  group->setExclusive(true);
  for (int i = 0; i < 4; i++)
  {
    nav[i] = new QPushButton(names[i]);
    nav[i]->setObjectName("nav");
    nav[i]->setCheckable(true);
    nav[i]->setIcon(navIcon(i, QColor("#b6d2da")));
    nav[i]->setIconSize(QSize(22, 22));
    group->addButton(nav[i], i);
    sl->addWidget(nav[i]);
    connect(nav[i], &QPushButton::clicked, this, [this, i] { showPage(i); });
  }
  sl->addStretch();
  sl->addWidget(label("基础调试 · V2\n电机反馈只读", "sideNote"));
  sl->addSpacing(10);
  sl->addWidget(label("DESKTOP  0.1.0", "brandSub"));
  outer->addWidget(side);
  auto *body = new QWidget;
  auto *bl = new QVBoxLayout(body);
  bl->setContentsMargins(26, 22, 26, 18);
  bl->setSpacing(16);
  outer->addWidget(body, 1);
  auto *bar = new QFrame;
  bar->setObjectName("card");
  auto *barLayout = new QGridLayout(bar);
  connectionLayout = barLayout;
  barLayout->setContentsMargins(14, 12, 14, 12);
  barLayout->setSpacing(10);
  transportKind = choices({"USB 有线", "BLE 蓝牙"});
  devices = new QComboBox;
  devices->setMinimumWidth(140);
  devices->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  scan = new QPushButton("刷新端口");
  connectButton = new QPushButton("连接设备");
  connectButton->setObjectName("primary");
  connectionState = label("未连接", "pill");
  barLayout->addWidget(transportKind, 0, 0);
  barLayout->addWidget(devices, 0, 1);
  barLayout->setColumnStretch(1, 1);
  barLayout->addWidget(scan, 0, 2);
  barLayout->addWidget(connectButton, 0, 3);
  barLayout->addWidget(connectionState, 0, 4);
  bl->addWidget(bar);
  title = label("", "title");
  subtitle = label("", "subtitle");
  bl->addWidget(title);
  bl->addWidget(subtitle);
  pages = new QStackedWidget;
  pages->addWidget(scrollable(homePage()));
  pages->addWidget(scrollable(debugPage()));
  pages->addWidget(scrollable(pressurePage()));
  pages->addWidget(scrollable(hitsPage()));
  bl->addWidget(pages, 1);
  message = label("选择 USB 串口或扫描蓝牙设备以开始。连接后只同步状态。", "message");
  message->setWordWrap(true);
  bl->addWidget(message);
  connect(transportKind, &QComboBox::currentIndexChanged, this,
          [this]
          {
            scanner.stop();
            endpoints.clear();
            devices->clear();
            scan->setText(transportKind->currentIndex() ? "扫描蓝牙" : "刷新端口");
            if (!transportKind->currentIndex())
              scanDevices();
          });
  connect(scan, &QPushButton::clicked, this, &Window::scanDevices);
  connect(connectButton, &QPushButton::clicked, this,
          [this]
          {
            if (model.active())
              model.close();
            else if (devices->currentIndex() >= 0 && devices->currentIndex() < endpoints.size())
              model.open(endpoints[devices->currentIndex()]);
          });
  scanner.setLowEnergyDiscoveryTimeout(6000);
  connect(&scanner, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered, this,
          [this](const QBluetoothDeviceInfo &d)
          {
            if (!d.name().startsWith("EnergyMechanism") &&
                !d.serviceUuids().contains(
                    QBluetoothUuid(QString("af510000-7b3d-4b28-9f02-6c6d454d0001"))))
              return;
            QString id = d.address().toString();
            for (const auto &e : endpoints)
              if (e.id == id)
                return;
            endpoints.append({Endpoint::Ble, id, d.name(), d});
            devices->addItem(d.name() + " · " + id);
          });
  connect(&scanner, &QBluetoothDeviceDiscoveryAgent::finished, this,
          [this]
          {
            scan->setEnabled(true);
            message->setText(endpoints.isEmpty()
                                 ? "未发现设备。请确认 ESP32 已运行、系统蓝牙已开启且尚有连接名额。"
                                 : "扫描完成，选择设备后连接。");
          });
  connect(&scanner, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
          [this](QBluetoothDeviceDiscoveryAgent::Error)
          {
            scan->setEnabled(true);
            message->setText("蓝牙扫描失败：" + scanner.errorString());
          });
  connect(&model, &DeviceClient::changed, this, &Window::updateView);
  connect(&model, &DeviceClient::notice, message, &QLabel::setText);
  connect(&model, &DeviceClient::pressure, this,
          [this](const Pressure &p)
          {
            latest = p;
            samplePending = true;
            sampleAge.restart();
            curve->append(p);
          });
  connect(&model, &DeviceClient::resetPlot, this, &Window::resetPressure);
  connect(&model, &DeviceClient::hit, this, &Window::addHit);
  connect(&model, &DeviceClient::disconnected, this,
          [this]
          {
            if (closing)
            {
              closeReady = true;
              close();
            }
          });
  renderTimer.setInterval(50);
  connect(&renderTimer, &QTimer::timeout, this, &Window::renderPressure);
  renderTimer.start();
  showPage(0);
  scanDevices();
  updateView();
}
QWidget *Window::homePage()
{
  auto *w = new QWidget;
  auto *v = new QVBoxLayout(w);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(18);
  auto *tiles = new QHBoxLayout;
  const QStringList captions = {"运行状态", "在线叶片", "CAN 就绪", "电机状态"};
  for (int i = 0; i < 4; i++)
  {
    QVBoxLayout *l;
    auto *c = card(&l);
    l->addWidget(label(captions[i], "muted"));
    stats[i] = label("—", "stat");
    l->addWidget(stats[i]);
    tiles->addWidget(c);
  }
  v->addLayout(tiles);
  health = label("等待中心通信状态", "hint");
  health->setWordWrap(true);
  v->addWidget(health);
  auto *row = new QHBoxLayout;
  homeRow = row;
  row->setSpacing(18);
  QVBoxLayout *networkLayout;
  auto *network = card(&networkLayout);
  networkLayout->addWidget(label("设备连接", "section"));
  topology = new Topology;
  topology->setMinimumHeight(290);
  networkLayout->addWidget(topology, 1);
  identity = label("等待设备身份", "muted");
  identity->setWordWrap(true);
  networkLayout->addWidget(identity);
  row->addWidget(network, 1);
  QVBoxLayout *settings;
  auto *training = card(&settings);
  settings->addWidget(label("训练设置", "section"));
  auto *hint = label("训练功能预留，当前固件开放基础通信与击打调试。", "hint");
  hint->setWordWrap(true);
  settings->addWidget(hint);
  auto *form = new QFormLayout;
  form->setSpacing(14);
  for (const auto &item :
       QList<QPair<QString, QStringList>>{{"机关类型", {"小能量机关", "大能量机关"}},
                                          {"阵营颜色", {"红方", "蓝方"}},
                                          {"旋转方向", {"每轮随机", "固定顺时针", "固定逆时针"}}})
  {
    auto *c = choices(item.second);
    c->setEnabled(false);
    form->addRow(item.first, c);
  }
  auto *continuous = new QCheckBox("完成或超时后自动重新开启");
  continuous->setEnabled(false);
  form->addRow("连续训练", continuous);
  auto *interval = new QSpinBox;
  interval->setRange(1, 30);
  interval->setValue(3);
  interval->setSuffix(" 秒");
  interval->setEnabled(false);
  form->addRow("重开间隔", interval);
  settings->addLayout(form);
  auto *start = new QPushButton("开启能量机关 · 尚未开放");
  start->setEnabled(false);
  settings->addWidget(start);
  settings->addWidget(label("电压：无数据    ·    机关轴转速：无换算数据", "muted"));
  settings->addStretch();
  row->addWidget(training, 1);
  v->addLayout(row);
  v->addStretch();
  return w;
}
QWidget *Window::debugPage()
{
  auto *w = new QWidget;
  auto *v = new QVBoxLayout(w);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(18);
  QVBoxLayout *l;
  auto *controls = card(&l);
  auto *buttons = new QHBoxLayout;
  debugEnter = new QPushButton("进入击打测试");
  debugEnter->setObjectName("primary");
  debugExit = new QPushButton("退出测试");
  auto *refresh = new QPushButton("刷新状态");
  queryButton = new QPushButton("查询最近操作");
  buttons->addWidget(debugEnter);
  buttons->addWidget(debugExit);
  buttons->addWidget(refresh);
  buttons->addWidget(queryButton);
  buttons->addStretch();
  l->addLayout(buttons);
  gate = label("", "hint");
  gate->setWordWrap(true);
  l->addWidget(gate);
  v->addWidget(controls);
  connect(refresh, &QPushButton::clicked, &model, &DeviceClient::refresh);
  connect(queryButton, &QPushButton::clicked, &model, &DeviceClient::queryLast);
  connect(debugEnter, &QPushButton::clicked, this,
          [this]
          {
            if (QMessageBox::question(
                    this, "进入击打测试",
                    "中心已确认停转。进入击打测试后，可观察压力和重新校准。是否进入？") ==
                QMessageBox::Yes)
              model.enterDebug();
          });
  connect(debugExit, &QPushButton::clicked, this,
          [this]
          {
            if (QMessageBox::question(this, "退出击打测试",
                                      "退出后保持待机并结束压力观察。是否退出？") ==
                QMessageBox::Yes)
              model.exitDebug();
          });
  QVBoxLayout *leafLayout;
  auto *leafCard = card(&leafLayout);
  leafLayout->addWidget(label("叶片状态", "section"));
  leafTable = table({"叶片", "连接", "校准 / 采样", "心跳年龄", "ADC 错误", "ADC 丢帧", "CAN 丢帧",
                     "UID", "本次在线"},
                    5);
  leafTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  leafTable->horizontalHeader()->setStretchLastSection(true);
  leafTable->setToolTip(
      "本次在线时长由本应用观察计算；断连或状态过期后重新计时，不代表叶片固件运行时间。");
  leafLayout->addWidget(leafTable);
  v->addWidget(leafCard);
  QVBoxLayout *motorLayout;
  auto *motorCard = card(&motorLayout);
  motorLayout->addWidget(label("C620 反馈 · 电机只读", "section"));
  motorTable = table({"电调 ID", "连接", "转子 rpm", "电流原始值", "温度 °C", "反馈年龄 ms"}, 8);
  motorLayout->addWidget(motorTable);
  v->addWidget(motorCard);
  v->addStretch();
  return w;
}
QWidget *Window::pressurePage()
{
  auto *w = new QWidget;
  auto *v = new QVBoxLayout(w);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(18);
  QVBoxLayout *controls;
  auto *controlsCard = card(&controls);
  auto *row = new QHBoxLayout;
  leafChoice = choices(
      {"叶片 1 · CAN 1", "叶片 2 · CAN 2", "叶片 3 · CAN 3", "叶片 4 · CAN 4", "叶片 5 · CAN 5"});
  observeButton = new QPushButton("观察该叶片");
  observeButton->setObjectName("primary");
  leaveButton = new QPushButton("停止本端观察");
  calibrateButton = new QPushButton("重新校准");
  row->addWidget(leafChoice);
  row->addWidget(observeButton);
  row->addWidget(leaveButton);
  row->addWidget(calibrateButton);
  row->addStretch();
  controls->addLayout(row);
  observation = label("尚未观察", "hint");
  observation->setWordWrap(true);
  controls->addWidget(observation);
  v->addWidget(controlsCard);
  connect(observeButton, &QPushButton::clicked, this,
          [this] { model.observe(leafChoice->currentIndex() + 1); });
  connect(leaveButton, &QPushButton::clicked, &model, &DeviceClient::leaveObservation);
  connect(leafChoice, &QComboBox::currentIndexChanged, this, &Window::updateView);
  connect(
      calibrateButton, &QPushButton::clicked, this,
      [this]
      {
        int slot = leafChoice->currentIndex() + 1;
        if (QMessageBox::question(
                this, "重新校准",
                QString("重新校准叶片 %1？请保持传感器不受击打，等待最终校准结果。").arg(slot)) ==
            QMessageBox::Yes)
          model.calibrate(slot);
      });
  auto *dataRow = new QHBoxLayout;
  pressureRow = dataRow;
  dataRow->setSpacing(18);
  QVBoxLayout *ringLayout;
  auto *ringCard = card(&ringLayout);
  ringLayout->addWidget(label("命中环数", "section"));
  rings = new RingView;
  rings->setMinimumSize(220, 280);
  ringLayout->addWidget(rings, 1);
  dataRow->addWidget(ringCard, 1);
  QVBoxLayout *dataLayout;
  auto *dataCard = card(&dataLayout);
  dataLayout->addWidget(label("十路压力快照", "section"));
  pressureTable = table({"环", "Raw", "Baseline", "Delta", "Peak", "State"}, 10);
  dataLayout->addWidget(pressureTable);
  dataRow->addWidget(dataCard, 2);
  v->addLayout(dataRow);
  QVBoxLayout *plotLayout;
  auto *plotCard = card(&plotLayout);
  auto *plotHeader = new QHBoxLayout;
  plotHeader->addWidget(label("实时压力 · 最近 30 秒", "section"));
  plotHeader->addStretch();
  metric = choices({"Delta", "Raw", "Baseline", "Peak", "State"});
  plotHeader->addWidget(metric);
  plotLayout->addLayout(plotHeader);
  auto *channels = new QHBoxLayout;
  curve = new CurveView;
  connect(metric, &QComboBox::currentIndexChanged, curve, &CurveView::setMetric);
  for (int i = 0; i < 10; i++)
  {
    auto *c = new QCheckBox(QString("%1 环").arg(i + 1));
    c->setChecked(true);
    c->setStyleSheet("color:" + CurveView::channelColor(i).name() + ";");
    channels->addWidget(c);
    connect(c, &QCheckBox::toggled, this, [this, i](bool b) { curve->setVisibleRing(i, b); });
  }
  plotLayout->addLayout(channels);
  plotLayout->addWidget(curve);
  v->addWidget(plotCard);
  return w;
}
QWidget *Window::hitsPage()
{
  auto *w = new QWidget;
  auto *v = new QVBoxLayout(w);
  v->setContentsMargins(0, 0, 0, 0);
  QVBoxLayout *l;
  auto *c = card(&l);
  auto *row = new QHBoxLayout;
  row->addWidget(label("最近诊断击打", "section"));
  row->addStretch();
  auto *clear = new QPushButton("清空显示");
  row->addWidget(clear);
  l->addLayout(row);
  auto *hint =
      label("保留最近 500 条，退出清空。V1 诊断事件可能缺失或重复，不作为训练成绩。", "hint");
  hint->setWordWrap(true);
  l->addWidget(hint);
  hitTable = table({"接收时间", "诊断序号", "叶片", "环数", "峰值"}, 0);
  hitTable->setMinimumHeight(410);
  l->addWidget(hitTable, 1);
  connect(clear, &QPushButton::clicked, this, [this] { hitTable->setRowCount(0); });
  v->addWidget(c, 1);
  return w;
}
void Window::scanDevices()
{
  if (model.active())
    return;
  endpoints.clear();
  devices->clear();
  if (transportKind->currentIndex() == 0)
  {
    auto ports = QSerialPortInfo::availablePorts();
    auto priority = [](const QSerialPortInfo &p)
    {
      return p.vendorIdentifier() == 0x303a && p.productIdentifier() == 0x1001 ? 0
             : p.hasVendorIdentifier()                                         ? 1
                                                                               : 2;
    };
    std::stable_sort(ports.begin(), ports.end(),
                     [&](const auto &a, const auto &b) { return priority(a) < priority(b); });
    for (const auto &p : ports)
    {
      endpoints.append({Endpoint::Usb, p.portName(), p.description(), {}});
      devices->addItem(p.portName() + " · " + p.description());
    }
    message->setText(endpoints.isEmpty() ? "没有可用串口。请连接中心板 Type-C 数据线。"
                                         : "选择中心板对应的 USB 串口。");
  }
  else
  {
    message->setText("正在扫描 BLE 能量机关…");
    scan->setEnabled(false);
    scanner.start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
  }
}
void Window::showPage(int page)
{
  if (pages->currentIndex() == 2 && page != 2)
    model.leaveObservation();
  pages->setCurrentIndex(page);
  nav[page]->setChecked(true);
  const QStringList titles = {"设备总览", "击打调试", "压力观察", "诊断记录"};
  const QStringList descriptions = {
      "查看机关连接、运行状态与基础遥测", "确认停转后进入测试，检查叶片与电机反馈",
      "共享当前观察叶片，各控制端独立维护观察租约", "当前应用内的短期诊断记录"};
  title->setText(titles[page]);
  subtitle->setText(descriptions[page]);
}
void Window::startDemo()
{
  model.open({Endpoint::Demo, "demo", "演示数据", {}});
  devices->clear();
  devices->addItem("模拟预览 · 软件生成的数据");
  message->setText("模拟预览：所有数值由软件生成，未连接任何硬件。");
}
void Window::updateView()
{
  bool fresh = model.fresh();
  bool active = model.active();
  bool demo = model.endpoint.kind == Endpoint::Demo;
  connectionState->setText(demo && active    ? "模拟数据 · 未连接硬件"
                           : model.connected ? (model.centerOnline ? "设备已连接" : "中心离线")
                                             : (active ? "连接中…" : "未连接"));
  connectionState->setToolTip(model.status);
  connectionState->setStyleSheet(fresh ? "background:#e5f5f1;color:#147c6b;"
                                       : "background:#fff0df;color:#986120;");
  connectButton->setText(active ? "断开连接" : "连接设备");
  transportKind->setEnabled(!active);
  devices->setEnabled(!active);
  scan->setEnabled(!active && !scanner.isActive());
  stats[0]->setText(fresh ? (model.state.mode == 5 ? "击打测试" : "待机") : "无数据");
  stats[1]->setText(fresh ? QString("%1 / 5").arg(std::popcount(unsigned(model.state.online & 31)))
                          : "— / 5");
  stats[2]->setText(fresh ? QString("%1 / 5").arg(std::popcount(unsigned(model.state.can & 31)))
                          : "— / 5");
  stats[3]->setText(!fresh                                             ? "无数据"
                    : !model.state.motorKnown                          ? "失联"
                    : model.state.motorValid != model.state.motorKnown ? "部分失联"
                    : model.state.stopped                              ? "已停转"
                                                                       : "未停转");
  topology->setState(model.state.online, fresh);
  if (fresh)
  {
    QStringList problems;
    if (model.state.can != 31)
      problems << "部分 CAN 端口未就绪";
    if (model.state.online != 31)
      problems << "部分叶片离线";
    if (!model.state.motorKnown || model.state.motorKnown != model.state.motorValid)
      problems << "电机反馈失联";
    for (int i = 0; i < 5; ++i)
      if (model.leaves[i].valid && (model.leaves[i].calibration & 128))
        problems << QString("叶片 %1 采样故障").arg(i + 1);
    health->setText(QString("%1\n中心 UART 累计错误 %2 · 丢帧 %3（计数非本轮新增）")
                        .arg(problems.isEmpty() ? "设备通信状态正常" : problems.join(" · "))
                        .arg(model.state.errors)
                        .arg(model.state.drops));
  }
  else
    health->setText("无有效中心状态；故障与遥测等待连接后同步。");
  identity->setText(model.connected ? QString("设备 %1\n%2 · %3")
                                          .arg(model.deviceId,
                                               model.endpoint.kind == Endpoint::Usb ? "USB"
                                               : demo                               ? "模拟"
                                                                                    : "BLE",
                                               model.status)
                                    : model.status);
  debugEnter->setEnabled(model.canDebug() && model.state.mode != 5);
  debugExit->setEnabled(model.canDebug() && model.state.mode == 5);
  queryButton->setEnabled(model.canQueryLast());
  gate->setText(!fresh ? "等待中心最新状态。可刷新状态检查连接。"
                : !model.state.stopped
                    ? "中心尚未确认电机停转：电机失联或运动中均不能进入测试、观察压力或校准。"
                : model.busy() ? "正在等待设备操作结果，请稍候。"
                               : "中心已确认停转。进入击打测试后可观察压力并重新校准。");
  int slot = leafChoice->currentIndex() + 1;
  observeButton->setEnabled(model.canObserve(slot));
  calibrateButton->setEnabled(model.canObserve(slot) && (model.state.caps & V2_CAP_CALIBRATE));
  leaveButton->setEnabled(model.observing());
  observation->setText(
      model.observing() ? QString("共享观察：叶片 %1  ·  %2  ·  其他控制端切换时同步跟随")
                              .arg(model.state.slot)
                              .arg(sampleAge.isValid() && sampleAge.elapsed() < 1000 ? "快照接收中"
                                                                                     : "等待新快照")
                        : "本端尚未观察。请进入击打测试后选择叶片；退出本页面会释放本端租约。");
  for (int i = 0; i < 5; i++)
  {
    const auto &l = model.leaves[i];
    cell(leafTable, i, 0, QString::number(i + 1));
    cell(leafTable, i, 1, !fresh ? "无数据" : l.online ? "在线" : "离线");
    cell(leafTable, i, 2, l.valid ? calibrationName(l.calibration) : "—");
    cell(leafTable, i, 3, l.valid && l.age != 0xffffffff ? QString::number(l.age) + " ms" : "—");
    cell(leafTable, i, 4, l.valid ? QString::number(l.adcErrors) : "—");
    cell(leafTable, i, 5, l.valid ? QString::number(l.adcDrops) : "—");
    cell(leafTable, i, 6, l.valid ? QString::number(l.canDrops) : "—");
    cell(leafTable, i, 7, l.uidValid ? l.uid : "—");
    const auto seconds = model.onlineSeconds(i + 1);
    cell(leafTable, i, 8, seconds < 0 ? "—" : QString::number(seconds) + " s");
  }
  for (int i = 0; i < 8; i++)
  {
    const auto &m = model.motors[i];
    bool online = fresh && (model.state.motorValid & (1 << i));
    cell(motorTable, i, 0, QString::number(i + 1));
    cell(motorTable, i, 1, online ? "在线" : "离线");
    cell(motorTable, i, 2, m.valid && online ? QString::number(m.rpm) : "—");
    cell(motorTable, i, 3, m.valid && online ? QString::number(m.current) : "—");
    cell(motorTable, i, 4, m.valid && online ? QString::number(m.temp) : "—");
    cell(motorTable, i, 5, m.valid && online ? QString::number(m.age) : "—");
  }
}
void Window::resetPressure()
{
  latest.reset();
  samplePending = false;
  sampleAge.invalidate();
  if (curve)
    curve->clear();
  if (rings)
    rings->highlight(0);
  if (pressureTable)
    for (int i = 0; i < 10; i++)
      for (int j = 0; j < 6; j++)
        cell(pressureTable, i, j, j == 0 ? QString::number(i + 1) : "—");
}
void Window::renderPressure()
{
  if (!samplePending || !latest)
    return;
  renderedSeq = latest->sequence;
  samplePending = false;
  for (int i = 0; i < 10; i++)
  {
    const auto &s = latest->values[i];
    const int values[] = {s.ring, s.raw, s.baseline, s.delta, s.peak, s.state};
    for (int j = 0; j < 6; j++)
      cell(pressureTable, i, j, QString::number(values[j]));
  }
}
void Window::addHit(const Hit &h)
{
  hitTable->insertRow(0);
  cell(hitTable, 0, 0, h.time);
  cell(hitTable, 0, 1, QString::number(h.sequence));
  cell(hitTable, 0, 2, QString::number(h.slot));
  cell(hitTable, 0, 3, QString::number(h.ring));
  cell(hitTable, 0, 4, QString::number(h.peak));
  if (hitTable->rowCount() > 500)
    hitTable->removeRow(500);
  if (model.observing() && h.slot == model.state.slot)
    rings->highlight(h.ring);
}
void Window::closeEvent(QCloseEvent *e)
{
  if (closeReady || !model.active())
  {
    e->accept();
    return;
  }
  e->ignore();
  if (closing)
    return;
  closing = true;
  scanner.stop();
  model.close();
  QTimer::singleShot(1400, this,
                     [this]
                     {
                       closeReady = true;
                       close();
                     });
}
void Window::resizeEvent(QResizeEvent *e)
{
  QMainWindow::resizeEvent(e);
  if (!connectionLayout)
    return;
  const bool compact = width() < 1120;
  sidebar->setFixedWidth(compact ? 160 : 190);
  for (auto *widget :
       std::array<QWidget *, 5>{transportKind, devices, scan, connectButton, connectionState})
    connectionLayout->removeWidget(widget);
  for (int i = 0; i < 5; ++i)
    connectionLayout->setColumnStretch(i, 0);
  connectionLayout->setColumnStretch(1, 1);
  connectionLayout->addWidget(transportKind, 0, 0);
  connectionLayout->addWidget(devices, 0, 1, 1, compact ? 2 : 1);
  connectionLayout->addWidget(scan, compact ? 1 : 0, compact ? 0 : 2);
  connectionLayout->addWidget(connectButton, compact ? 1 : 0, compact ? 1 : 3);
  connectionLayout->addWidget(connectionState, compact ? 1 : 0, compact ? 2 : 4);
  homeRow->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
  pressureRow->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
}
} // namespace em
