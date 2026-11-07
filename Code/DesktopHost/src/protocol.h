#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QVector>
#include <QString>
#include <QMetaType>
extern "C" {
#include "comm_v2.h"
}
namespace em {
constexpr quint8 UsbOpen=0x70, UsbAck=0x71, UsbPing=0x72, UsbPong=0x73, UsbClose=0x74;
struct Frame { quint8 type=0; quint16 seq=0; QByteArray data; };
quint16 u16(const QByteArray &b, int p);
quint32 u32(const QByteArray &b, int p);
quint64 u64(const QByteArray &b, int p);
void put16(QByteArray &b, int p, quint16 v);
void put32(QByteArray &b, int p, quint32 v);
void put64(QByteArray &b, int p, quint64 v);
QByteArray encode(const Frame &f);
QByteArray request(quint8 op, quint64 boot, quint32 revision, const QByteArray &args={});
QVector<QByteArray> fragments(const Frame &f, quint16 transfer);
QString codeName(int code);
QString calibrationName(int status);
class StreamParser {
  FrameParser parser{};
public:
  QVector<Frame> feed(const QByteArray &bytes, quint32 now);
  void reset() { parser={}; }
  quint32 errors() const { return parser.errors; }
};
class Reassembler {
  V2Reassembly parser{};
public:
  bool feed(const QByteArray &bytes, quint32 now, Frame &out);
  void reset() { parser={}; }
};
}
Q_DECLARE_METATYPE(em::Frame)
