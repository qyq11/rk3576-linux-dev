#include "frame_codec.h"

namespace Sn100Protocol
{
namespace
{

constexpr int kPacketHeaderBytes = 4;
constexpr int kFrameOverheadBytes = 7;
constexpr int kMaxPacketBytes = kPacketHeaderBytes + kMaxFragmentBytes;
constexpr int kMaxFrameBytes = kMaxPacketBytes + kFrameOverheadBytes;
constexpr int kMaxBufferedBytes = kMaxFeedBytes + kMaxFrameBytes;

QByteArray frameHeader(Direction direction)
{
    return direction == Direction::HostToDevice
            ? QByteArray::fromHex("0203") : QByteArray::fromHex("0607");
}

QByteArray frameTail(Direction direction)
{
    return direction == Direction::HostToDevice
            ? QByteArray::fromHex("0405") : QByteArray::fromHex("0809");
}

void appendBigEndian16(QByteArray *bytes, int value)
{
    bytes->append(static_cast<char>((value >> 8) & 0xff));
    bytes->append(static_cast<char>(value & 0xff));
}

int bigEndian16(const QByteArray &bytes, int offset)
{
    return (static_cast<quint8>(bytes.at(offset)) << 8)
            | static_cast<quint8>(bytes.at(offset + 1));
}

quint8 xorValue(const QByteArray &bytes)
{
    quint8 checksum = 0;
    for (char byte : bytes)
    {
        checksum ^= static_cast<quint8>(byte);
    }
    return checksum;
}

bool timedOut(qint64 nowMs, qint64 progressMs)
{
    return progressMs >= 0 && nowMs >= progressMs
            && nowMs - progressMs >= kIncompleteTimeoutMs;
}

} // namespace

QList<QByteArray> encodeMessage(const QByteArray &message, Direction direction,
                              QString *error)
{
    if (error)
    {
        error->clear();
    }
    if (message.isEmpty() || message.size() > kMaxMessageBytes)
    {
        if (error)
        {
            *error = QStringLiteral("message.length: 业务消息必须为 1..128000 字节（本地预算）");
        }
        return {};
    }

    const int packetCount = (message.size() + kMaxFragmentBytes - 1) / kMaxFragmentBytes;
    QList<QByteArray> frames;
    for (int index = 0; index < packetCount; ++index)
    {
        QByteArray packet;
        appendBigEndian16(&packet, index);
        appendBigEndian16(&packet, packetCount);
        packet.append(message.mid(index * kMaxFragmentBytes, kMaxFragmentBytes));

        QByteArray frame = frameHeader(direction);
        appendBigEndian16(&frame, packet.size());
        frame.append(packet);
        frame.append(static_cast<char>(xorValue(packet)));
        frame.append(frameTail(direction));
        frames.append(frame);
    }
    return frames;
}

FrameDecoder::FrameDecoder(Direction direction)
    : m_direction(direction)
{
}

void FrameDecoder::clearAssembly()
{
    m_assembly.clear();
    m_previousPacket.clear();
    m_totalPackets = 0;
    m_nextPacketIndex = 0;
    m_assemblyProgressMs = -1;
}

void FrameDecoder::reset()
{
    m_buffer.clear();
    m_bufferProgressMs = -1;
    m_lastCallMs = -1;
    clearAssembly();
}

int FrameDecoder::bufferedBytes() const
{
    return m_buffer.size();
}

int FrameDecoder::assemblyBytes() const
{
    return m_assembly.size();
}

bool FrameDecoder::advanceTime(qint64 nowMs, DecodeBatch *batch)
{
    if (nowMs < 0 || (m_lastCallMs >= 0 && nowMs < m_lastCallMs))
    {
        batch->errors.append(QStringLiteral("input.time: 时间必须是非负、单调不减的毫秒值；已清空连接缓存"));
        reset();
        return false;
    }
    m_lastCallMs = nowMs;

    const bool frameExpired = !m_buffer.isEmpty() && timedOut(nowMs, m_bufferProgressMs);
    const bool assemblyExpired = m_totalPackets > 0 && timedOut(nowMs, m_assemblyProgressMs);
    if (frameExpired)
    {
        batch->errors.append(QStringLiteral("frame.timeout: 半帧 10000 ms 无新字节；已清空缓存和组包"));
    }
    if (assemblyExpired)
    {
        batch->errors.append(QStringLiteral("packet.timeout: 组包 10000 ms 无新有效分片；已清空缓存和组包"));
    }
    // 半帧与组包属于同一连接。任一超时后都不能拿旧数据拼接后来的消息。
    if (frameExpired || assemblyExpired)
    {
        m_buffer.clear();
        m_bufferProgressMs = -1;
        clearAssembly();
    }
    return true;
}

DecodeBatch FrameDecoder::expire(qint64 nowMs)
{
    DecodeBatch batch;
    advanceTime(nowMs, &batch);
    return batch;
}

void FrameDecoder::acceptPacket(const QByteArray &packet, qint64 nowMs, DecodeBatch *batch)
{
    const int index = bigEndian16(packet, 0);
    const int total = bigEndian16(packet, 2);
    if (total < 1 || total > kMaxMessagePackets || index >= total)
    {
        batch->errors.append(QStringLiteral("packet.metadata: 分包 index=%1、total=%2 无效或超过本地 64 片预算")
                             .arg(index).arg(total));
        clearAssembly();
        return;
    }

    // 线上没有事务 ID。只在尚未完成的组包内识别完全相同的前一分片；
    // 不按业务内容去重完整消息，否则会丢掉合法的重复状态通知。
    if (m_totalPackets > 0 && packet == m_previousPacket)
    {
        ++batch->duplicatePackets;
        return;
    }

    if (m_totalPackets > 0 && (index != m_nextPacketIndex || total != m_totalPackets))
    {
        batch->errors.append(QStringLiteral("packet.sequence: 期待 index=%1、total=%2，收到 index=%3、total=%4；已丢弃当前组包")
                             .arg(m_nextPacketIndex).arg(m_totalPackets).arg(index).arg(total));
        clearAssembly();
        // 新的 index 0 可作为恢复起点；非零片不能独立恢复消息。
        if (index != 0)
        {
            return;
        }
    }
    else if (m_totalPackets == 0 && index != 0)
    {
        batch->errors.append(QStringLiteral("packet.sequence: 没有组包起点却收到 index=%1；等待 index 0").arg(index));
        return;
    }

    const QByteArray fragment = packet.mid(kPacketHeaderBytes);
    if (m_assembly.size() + fragment.size() > kMaxMessageBytes)
    {
        batch->errors.append(QStringLiteral("packet.limit: 业务消息超过本地 128000 字节预算；已丢弃组包"));
        clearAssembly();
        return;
    }
    if (m_totalPackets == 0)
    {
        m_totalPackets = total;
    }
    m_assembly.append(fragment);
    m_previousPacket = packet;
    m_nextPacketIndex = index + 1;
    m_assemblyProgressMs = nowMs;

    if (m_nextPacketIndex == m_totalPackets)
    {
        batch->messages.append(m_assembly);
        clearAssembly();
    }
}

DecodeBatch FrameDecoder::feed(const QByteArray &bytes, qint64 nowMs)
{
    DecodeBatch batch;
    if (bytes.size() > kMaxFeedBytes)
    {
        batch.errors.append(QStringLiteral("input.limit: 单次输入超过本地 65536 字节预算；拒收并清空连接缓存"));
        reset();
        return batch;
    }
    if (!advanceTime(nowMs, &batch))
    {
        return batch;
    }
    if (m_buffer.size() + bytes.size() > kMaxBufferedBytes)
    {
        batch.errors.append(QStringLiteral("input.buffer: 输入缓存超过本地预算；拒收并清空连接缓存"));
        reset();
        return batch;
    }
    if (!bytes.isEmpty())
    {
        m_buffer.append(bytes);
        m_bufferProgressMs = nowMs;
    }

    const QByteArray header = frameHeader(m_direction);
    const QByteArray tail = frameTail(m_direction);
    while (!m_buffer.isEmpty())
    {
        const int headerOffset = m_buffer.indexOf(header);
        if (headerOffset < 0)
        {
            // TCP 可恰好在两个帧头字节之间拆包，只保留末尾第一个帧头字节。
            const int keep = m_buffer.endsWith(header.left(1)) ? 1 : 0;
            const int discarded = m_buffer.size() - keep;
            if (discarded > 0)
            {
                batch.errors.append(QStringLiteral("frame.noise: 丢弃 %1 个非帧头字节").arg(discarded));
                clearAssembly();
            }
            m_buffer = m_buffer.right(keep);
            break;
        }
        if (headerOffset > 0)
        {
            batch.errors.append(QStringLiteral("frame.noise: 帧头前丢弃 %1 个噪声字节").arg(headerOffset));
            m_buffer.remove(0, headerOffset);
            clearAssembly();
        }
        if (m_buffer.size() < 4)
        {
            break;
        }

        const int packetBytes = bigEndian16(m_buffer, 2);
        if (packetBytes <= kPacketHeaderBytes || packetBytes > kMaxPacketBytes)
        {
            batch.errors.append(QStringLiteral("frame.length: 数据字段长度 %1 无效，允许 5..2004（含四字节分包头）")
                                .arg(packetBytes));
            m_buffer.remove(0, 1);
            clearAssembly();
            continue;
        }
        const int frameBytes = packetBytes + kFrameOverheadBytes;
        if (m_buffer.size() < frameBytes)
        {
            break;
        }
        if (m_buffer.mid(packetBytes + 5, 2) != tail)
        {
            batch.errors.append(QStringLiteral("frame.tail: 帧尾与方向不符；重新查找帧头"));
            m_buffer.remove(0, 1);
            clearAssembly();
            continue;
        }
        const QByteArray packet = m_buffer.mid(4, packetBytes);
        if (static_cast<quint8>(m_buffer.at(packetBytes + 4)) != xorValue(packet))
        {
            batch.errors.append(QStringLiteral("frame.xor: 数据字段 XOR 校验失败；重新查找帧头"));
            m_buffer.remove(0, 1);
            clearAssembly();
            continue;
        }
        m_buffer.remove(0, frameBytes);
        acceptPacket(packet, nowMs, &batch);
    }
    if (m_buffer.isEmpty())
    {
        m_bufferProgressMs = -1;
    }
    return batch;
}

} // namespace Sn100Protocol
