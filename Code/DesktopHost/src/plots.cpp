#include "plots.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <numbers>
namespace em {
QIcon navIcon(int index,const QColor &color) {
  QPixmap pix(48,48);pix.fill(Qt::transparent);QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(color,3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
  if(index==0) {for(int x:{9,27})for(int y:{9,27})p.drawRoundedRect(x,y,12,12,3,3);}
  else if(index==1) {p.drawEllipse(QPointF(24,24),12,12);p.drawLine(24,5,24,12);p.drawLine(24,36,24,43);p.drawLine(5,24,12,24);p.drawLine(36,24,43,24);}
  else if(index==2) {QPainterPath path;path.moveTo(5,30);path.lineTo(13,30);path.lineTo(19,12);path.lineTo(27,38);path.lineTo(33,22);path.lineTo(43,22);p.drawPath(path);}
  else {p.drawRoundedRect(10,7,28,34,4,4);for(int y:{17,24,31})p.drawLine(17,y,31,y);}
  return QIcon(pix);
}
QIcon appIcon() {
  QPixmap pix(128,128);pix.fill(Qt::transparent);QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
  p.setPen(Qt::NoPen);p.setBrush(QColor("#0eaf9b"));p.drawRoundedRect(4,4,120,120,28,28);
  p.setPen(QPen(Qt::white,9,Qt::SolidLine,Qt::RoundCap));
  for(int i=0;i<5;i++){double a=(i*72-90)*std::numbers::pi/180;p.drawLine(QPointF(64+19*std::cos(a),64+19*std::sin(a)),QPointF(64+40*std::cos(a),64+40*std::sin(a)));}
  p.setBrush(Qt::white);p.setPen(Qt::NoPen);p.drawEllipse(QPointF(64,64),10,10);return QIcon(pix);
}
void Topology::paintEvent(QPaintEvent *) {
  QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QPointF c(width()/2.,height()/2.);
  double r=std::min(width(),height())*.32;
  p.setPen(QPen(QColor("#e6edf3"),1,Qt::DashLine));p.drawEllipse(c,r,r);
  for(int i=0;i<5;i++) {
    double a=(i*72-90)*std::numbers::pi/180;QPointF v(std::cos(a),std::sin(a)),pos=c+v*r;
    bool online=linked && (mask&(1<<i));
    p.setPen(QPen(online?QColor("#a0ded1"):QColor("#dbe3eb"),2));p.drawLine(c,pos);
    p.setBrush(online?QColor("#e4f8f2"):QColor("#f3f6fa"));p.setPen(QPen(online?QColor("#0c9d87"):QColor("#c4cfda"),1.4));
    p.drawRoundedRect(QRectF(pos.x()-35,pos.y()-23,70,46),12,12);
    p.setPen(online?QColor("#087566"):QColor("#8090a3"));p.drawText(QRectF(pos.x()-35,pos.y()-21,70,42),Qt::AlignCenter,QString("叶片 %1\nCAN %1").arg(i+1));
  }
  p.setBrush(QColor("#172b43"));p.setPen(Qt::NoPen);p.drawEllipse(c,43,43);p.setPen(Qt::white);
  p.drawText(QRectF(c.x()-43,c.y()-35,86,70),Qt::AlignCenter,linked?"中心板\n已在线":"中心板\n未连接");
}
void RingView::paintEvent(QPaintEvent *) {
  QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
  QPointF c(width()/2.,height()/2.-8);double r=std::min(width(),height()-38)*.43;
  for(int i=1;i<=10;i++) {
    double radius=r*(11-i)/10.;p.setPen(QPen(QColor("#bacbda"),1));
    p.setBrush(i==ring?QColor("#41c8af"):(i%2?QColor("#f1f6fa"):Qt::white));p.drawEllipse(c,radius,radius);
  }
  p.setPen(QColor("#8092a5"));
  for(int i=1;i<=9;i++) p.drawText(QRectF(c.x()+r*(10.5-i)/10.-10,c.y()-8,20,16),Qt::AlignCenter,QString::number(i));
  p.setPen(QColor("#19344d"));p.drawText(QRectF(c.x()-15,c.y()-10,30,20),Qt::AlignCenter,"10");
  p.drawText(QRectF(0,height()-30,width(),25),Qt::AlignCenter,ring?QString("最近命中：%1 环").arg(ring):"环数示意 · 无角度定位");
}
QColor CurveView::channelColor(int i) {static const char *colors[]={"#009f8a","#4269d6","#dc8e2e","#b454bc","#d75974","#33a7c2","#6f83a3","#85a338","#ce6a39","#705ad2"};return QColor(colors[i%10]);}
CurveView::CurveView(QWidget *p):QWidget(p) {setMinimumHeight(220);}
void CurveView::append(const Pressure &p) {
  if(!samples.empty() && (samples.back().slot!=p.slot || samples.back().selection!=p.selection)) samples.clear();
  samples.push_back(p);while(samples.size()>601 || (!samples.empty() && p.received-samples.front().received>30000)) samples.pop_front();update();
}
void CurveView::clear(){samples.clear();update();}
void CurveView::paintEvent(QPaintEvent *) {
  QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QRectF plot(54,14,width()-74,height()-48);
  auto value=[this](const Sensor &s){switch(metric){case 1:return s.raw;case 2:return s.baseline;case 3:return s.peak;case 4:return s.state;default:return s.delta;}};
  double low=0,high=1;
  for(const auto &s:samples)for(int i=0;i<10;i++)if(visible[i]){low=std::min(low,double(value(s.values[i])));high=std::max(high,double(value(s.values[i])));}
  if(metric!=4){auto margin=std::max(10.,(high-low)*.12);low-=margin;high+=margin;}
  for(int i=0;i<=4;i++) {
    double y=plot.top()+plot.height()*i/4.;p.setPen(QPen(QColor("#e6edf3"),1));p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
    p.setPen(QColor("#8594a7"));p.drawText(QRectF(0,y-9,45,18),Qt::AlignRight|Qt::AlignVCenter,QString::number(high-(high-low)*i/4.,'f',0));
  }
  for(int i=0;i<=6;i++) {double x=plot.left()+plot.width()*i/6.;p.drawText(QRectF(x-23,plot.bottom()+10,46,20),Qt::AlignCenter,QString("%1 s").arg(-30+i*5));}
  if(samples.empty()) {p.setPen(QColor("#8a9bad"));p.drawText(plot,Qt::AlignCenter,"开始观察后显示实时曲线");return;}
  const auto end=samples.back().received;
  p.save();p.setClipRect(plot);
  for(int ch=0;ch<10;ch++) if(visible[ch]) {
    QPainterPath line;bool first=true;qint64 previous=0;
    for(const auto &s:samples) {
      QPointF point(plot.right()-(end-s.received)*plot.width()/30000.,plot.bottom()-(value(s.values[ch])-low)*plot.height()/(high-low));
      if(first || s.received-previous>250){line.moveTo(point);first=false;}else line.lineTo(point);
      previous=s.received;
    }
    p.setPen(QPen(channelColor(ch),1.7));p.drawPath(line);
  }p.restore();
}
}
