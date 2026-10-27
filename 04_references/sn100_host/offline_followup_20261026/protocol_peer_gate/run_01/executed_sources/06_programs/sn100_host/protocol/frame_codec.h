#ifndef SN100_PROTOCOL_FRAME_CODEC_H
#define SN100_PROTOCOL_FRAME_CODEC_H

#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QtGlobal>

namespace Sn100Protocol
{

enum class Direction
{
    HostToDevice,
    DeviceToHost
};

// 2000 字节分片来自原 commanddefine.h。其余是本轮离线实现的本地资源预算，
// 不是对 SN100 固件最大报文、分包数或超时要求的声明。
constexpr int kMaxFragmentBytes = 2000;
constexpr int kMaxMessagePackets = 64;
constexpr int kMaxMessageBytes = kMaxFragmentBytes * kMaxMessagePackets;
constexpr int kMaxFeedBytes = 65536;
constexpr qint64 kIncompleteTimeoutMs = 10000;

struct DecodeBatch
{
    QList<QByteArray> messages;
    QStringList errors;
    int duplicatePackets = 0;
};

// 业务消息必须非空。每片线上数据包含 index/total 两个大端 uint16，
// XOR 覆盖这四个字节和业务分片，不包含外层帧头、长度、校验及帧尾。
QList<QByteArray> encodeMessage(const QByteArray &message, Direction direction,
                              QString *error = nullptr);

class FrameDecoder
{
public:
    explicit FrameDecoder(Direction direction = Direction::DeviceToHost);

    // nowMs 由调用者提供单调时钟毫秒值；feed/expire 都会核查 10 秒无进展。
    // 一个对象只接收一个方向、一个连接的有序字节流，断线/重连须 reset。
    DecodeBatch feed(const QByteArray &bytes, qint64 nowMs);
    DecodeBatch expire(qint64 nowMs);
    void reset();
    int bufferedBytes() const;
    int assemblyBytes() const;

private:
    bool advanceTime(qint64 nowMs, DecodeBatch *batch);
    void clearAssembly();
    void acceptPacket(const QByteArray &packet, qint64 nowMs, DecodeBatch *batch);

    Direction m_direction;
    QByteArray m_buffer;
    QByteArray m_assembly;
    QByteArray m_previousPacket;
    int m_totalPackets = 0;
    int m_nextPacketIndex = 0;
    qint64 m_bufferProgressMs = -1;
    qint64 m_assemblyProgressMs = -1;
    qint64 m_lastCallMs = -1;
};

} // namespace Sn100Protocol

#endif
