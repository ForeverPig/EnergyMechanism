#pragma once
#include "client.h"
#include "plots.h"
#include <QMainWindow>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QBluetoothDeviceDiscoveryAgent>
#include <optional>
namespace em {
class Window final : public QMainWindow {
  Q_OBJECT
  DeviceClient model;
  QBluetoothDeviceDiscoveryAgent scanner;
  QVector<Endpoint> endpoints;
  QComboBox *transportKind=nullptr,*devices=nullptr,*leafChoice=nullptr,*metric=nullptr;
  QPushButton *scan=nullptr,*connectButton=nullptr,*debugEnter=nullptr,*debugExit=nullptr,*observeButton=nullptr,*leaveButton=nullptr,*calibrateButton=nullptr,*queryButton=nullptr;
  QLabel *connectionState=nullptr,*title=nullptr,*subtitle=nullptr,*gate=nullptr,*observation=nullptr,*message=nullptr,*identity=nullptr;
  std::array<QLabel*,4> stats{};
  std::array<QPushButton*,4> nav{};
  QStackedWidget *pages=nullptr;
  QTableWidget *leafTable=nullptr,*motorTable=nullptr,*pressureTable=nullptr,*hitTable=nullptr;
  Topology *topology=nullptr;RingView *rings=nullptr;CurveView *curve=nullptr;
  QTimer renderTimer;QElapsedTimer sampleAge;
  std::optional<Pressure> latest;
  quint32 renderedSeq=0;
  bool samplePending=false,closing=false,closeReady=false;
  QWidget *homePage();QWidget *debugPage();QWidget *pressurePage();QWidget *hitsPage();
  void scanDevices();void updateView();void renderPressure();void resetPressure();void addHit(const Hit &h);
protected:void closeEvent(QCloseEvent *) override;
public:
  explicit Window(QWidget *parent=nullptr);
  DeviceClient *client(){return &model;}
  void showPage(int page);
  void startDemo();
};
}
