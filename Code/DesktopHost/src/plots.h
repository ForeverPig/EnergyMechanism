#pragma once
#include "client.h"
#include <QWidget>
#include <QIcon>
#include <deque>
namespace em
{
QIcon appIcon();
QIcon navIcon(int index, const QColor &color);
class Topology : public QWidget
{
  Q_OBJECT
  quint8 mask = 0;
  bool linked = false;

public:
  using QWidget::QWidget;
  void setState(quint8 m, bool l)
  {
    mask = m;
    linked = l;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override;
};
class RingView : public QWidget
{
  Q_OBJECT
  int ring = 0;

public:
  using QWidget::QWidget;
  void highlight(int r)
  {
    ring = r;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override;
};
class CurveView : public QWidget
{
  Q_OBJECT
  std::deque<Pressure> samples;
  std::array<bool, 10> visible{true, true, true, true, true, true, true, true, true, true};
  int metric = 0;

public:
  explicit CurveView(QWidget *parent = nullptr);
  void append(const Pressure &p);
  void clear();
  void setMetric(int m)
  {
    metric = m;
    update();
  }
  void setVisibleRing(int r, bool value)
  {
    visible.at(static_cast<size_t>(r)) = value;
    update();
  }
  static QColor channelColor(int i);

protected:
  void paintEvent(QPaintEvent *) override;
};
} // namespace em
