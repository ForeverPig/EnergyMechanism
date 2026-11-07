#include "protocol.h"
#include <QStringList>
#include <stdexcept>
namespace em
{
static void bounds(const QByteArray &b, int p, int n)
{
  if (p < 0 || p + n > b.size())
    throw std::out_of_range("V2 field outside payload");
}
quint16 u16(const QByteArray &b, int p)
{
  bounds(b, p, 2);
  return ReadU16(reinterpret_cast<const uint8_t *>(b.constData() + p));
}
quint32 u32(const QByteArray &b, int p)
{
  bounds(b, p, 4);
  return ReadU32(reinterpret_cast<const uint8_t *>(b.constData() + p));
}
quint64 u64(const QByteArray &b, int p)
{
  bounds(b, p, 8);
  return V2Read64(reinterpret_cast<const uint8_t *>(b.constData() + p));
}
void put16(QByteArray &b, int p, quint16 v)
{
  bounds(b, p, 2);
  WriteU16(reinterpret_cast<uint8_t *>(b.data() + p), v);
}
void put32(QByteArray &b, int p, quint32 v)
{
  bounds(b, p, 4);
  WriteU32(reinterpret_cast<uint8_t *>(b.data() + p), v);
}
void put64(QByteArray &b, int p, quint64 v)
{
  bounds(b, p, 8);
  V2Write64(reinterpret_cast<uint8_t *>(b.data() + p), v);
}
QByteArray encode(const Frame &f)
{
  if (f.data.size() > 192)
    return {};
  QByteArray b(202, 0);
  auto n = WireEncode(reinterpret_cast<uint8_t *>(b.data()), 2, f.type, f.seq,
                      reinterpret_cast<const uint8_t *>(f.data.constData()),
                      static_cast<uint16_t>(f.data.size()));
  b.resize(static_cast<qsizetype>(n));
  return b;
}
QByteArray request(quint8 op, quint64 boot, quint32 revision, const QByteArray &args)
{
  QByteArray b(13, 0);
  b[0] = static_cast<char>(op);
  put64(b, 1, boot);
  put32(b, 9, revision);
  return b + args;
}
QVector<QByteArray> fragments(const Frame &f, quint16 transfer)
{
  QVector<QByteArray> out;
  if (f.data.size() > 184)
    return out;
  const auto wire = encode(f);
  for (int i = 0; i < (wire.size() + 15) / 16; i++)
  {
    uint8_t b[20];
    auto n = V2Fragment(reinterpret_cast<const uint8_t *>(wire.constData()),
                        static_cast<size_t>(wire.size()), transfer, static_cast<uint8_t>(i), b);
    out.append(QByteArray(reinterpret_cast<const char *>(b), static_cast<qsizetype>(n)));
  }
  return out;
}
static Frame frame(const DebugPacket &p)
{
  return {p.type, p.seq, QByteArray(reinterpret_cast<const char *>(p.data), p.len)};
}
QVector<Frame> StreamParser::feed(const QByteArray &bytes, quint32 now)
{
  QVector<Frame> out;
  DebugPacket p{};
  for (auto b : bytes)
    if (WireFeed(&parser, static_cast<uint8_t>(b), now, &p, true) && p.version == 2)
      out.append(frame(p));
  return out;
}
bool Reassembler::feed(const QByteArray &bytes, quint32 now, Frame &out)
{
  DebugPacket p{};
  if (V2FragmentFeed(&parser, reinterpret_cast<const uint8_t *>(bytes.constData()),
                     static_cast<size_t>(bytes.size()), now, &p) == 1)
  {
    out = frame(p);
    return true;
  }
  return false;
}
QString codeName(int c)
{
  const QStringList names = {
      "完成",         "参数无效", "固件未支持",   "尚未就绪",     "模式不允许",
      "未确认停转",   "叶片离线", "设备忙碌",     "请求过于频繁", "状态已变化，请刷新后重试",
      "已在运行",     "等待超时", "中心链路断开", "请求冲突",     "重试缓存已过期",
      "结果缓存缺失", "存在故障"};
  return c >= 0 && c < names.size() ? names[c] : QString("未知错误 %1").arg(c);
}
QString calibrationName(int s)
{
  if (s & 128)
    return "采样故障";
  switch (s & 127)
  {
  case 0:
    return "校准中";
  case 1:
    return "就绪";
  case 2:
    return "不稳定";
  default:
    return "未知";
  }
}
} // namespace em
