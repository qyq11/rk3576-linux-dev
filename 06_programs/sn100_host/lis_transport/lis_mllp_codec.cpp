#include "lis_mllp_codec.h"

#include <QVector>

namespace Sn100Lis
{
namespace
{
constexpr int MaximumReadBytes = 65536;
constexpr int MaximumFramesPerRead = 256;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

constexpr int MllpCodec::MaximumFrameBytes;

MllpCodec::MllpCodec(bool utf16) : utf16_(utf16)
{
}

void MllpCodec::reset(bool utf16)
{
    utf16_ = utf16;
    buffer_.clear();
}

bool MllpCodec::feed(const QByteArray &bytes, QList<QByteArray> *bodies, QString *error)
{
    if (error) error->clear();
    if (!bodies)
        return fail(error, QStringLiteral("LIS分帧输出对象为空，本批未处理。"));
    if (bytes.size() > MaximumReadBytes)
        return fail(error, QStringLiteral("LIS单次读取超过64KiB，本批未发布；请核对读取边界并关闭连接。"));

    // 先在局部暂存。本批任一帧失败时，不发布前面已拆出的消息，也不改上次半帧。
    QByteArray pending = buffer_;
    pending.append(bytes);
    QList<QByteArray> completed;
    const QByteArray head = utf16_ ? QByteArray("\x0b\x00",2) : QByteArray("\x0b",1);
    const QByteArray tail = utf16_ ? QByteArray("\x1c\x00\x0d\x00",4) : QByteArray("\x1c\x0d",2);
    int cursor = 0;
    while (cursor < pending.size())
    {
        const int headPosition = pending.indexOf(head,cursor);
        if (headPosition < 0)
        {
            // 未找到完整头时连末尾的半个UTF16头也保留，等下一次实际收包。
            if (pending.size()-cursor > MaximumFrameBytes)
                return fail(error, QStringLiteral("LIS等待消息头的缓存超过1MiB，本批未发布；请关闭连接。"));
            buffer_ = pending.mid(cursor);
            *bodies = completed;
            return true;
        }

        // 沿原版规则，只有找到完整消息头后才跳过它前面的垃圾字节。
        const int tailPosition = pending.indexOf(tail,headPosition+head.size());
        if (tailPosition < 0)
        {
            if (pending.size()-headPosition > MaximumFrameBytes)
                return fail(error, QStringLiteral("LIS等待消息尾的缓存超过1MiB，本批未发布；请关闭连接。"));
            buffer_ = pending.mid(headPosition);
            *bodies = completed;
            return true;
        }

        const int frameBytes = tailPosition-headPosition+tail.size();
        if (frameBytes > MaximumFrameBytes)
            return fail(error, QStringLiteral("LIS完整消息超过1MiB本地预算，本批未发布；请关闭连接。"));
        if (completed.size() >= MaximumFramesPerRead)
            return fail(error, QStringLiteral("LIS单次读取产生超过256条完整消息，本批未发布；请关闭连接。"));

        const int bodyPosition = headPosition+head.size();
        completed.append(pending.mid(bodyPosition,tailPosition-bodyPosition));
        cursor = tailPosition+tail.size();
    }

    buffer_.clear();
    *bodies = completed;
    return true;
}

QByteArray MllpCodec::encode(const QString &message, bool utf16)
{
    QString framed;
    framed.append(QChar(0x0b));
    framed.append(message);
    framed.append(QChar(0x1c));
    framed.append(QChar(0x0d));
    if (!utf16) return framed.toUtf8();

    // 原Windows直接发送QString原生UTF16内存：这里明确写小端字节，不加BOM。
    QByteArray encoded;
    for (QChar character : framed)
    {
        const ushort value = character.unicode();
        encoded.append(static_cast<char>(value & 0xff));
        encoded.append(static_cast<char>((value >> 8) & 0xff));
    }
    return encoded;
}

bool MllpCodec::decode(const QByteArray &body, bool utf16, QString *message, QString *error)
{
    if (error) error->clear();
    if (!message)
        return fail(error, QStringLiteral("LIS正文输出对象为空，未解码。"));
    if (!utf16)
    {
        // 原版使用QByteArray重载；Qt5在首个NUL处停止转换，不能换成显式长度重载。
        *message = QString::fromUtf8(body);
        return true;
    }
    if (body.size()%2 != 0)
        return fail(error, QStringLiteral("LIS的UTF16正文含不完整双字节字符，未截断解码。"));

    QVector<ushort> aligned;
    aligned.reserve(body.size()/2);
    for (int index = 0; index < body.size(); index += 2)
    {
        const auto low = static_cast<unsigned char>(body.at(index));
        const auto high = static_cast<unsigned char>(body.at(index+1));
        aligned.append(static_cast<ushort>(low | (static_cast<ushort>(high) << 8)));
    }
    // 逐对转成对齐的UTF16字值，避免把任意QByteArray地址当ushort指针读取。
    *message = QString::fromUtf16(aligned.constData(),aligned.size());
    return true;
}

int MllpCodec::bufferedBytes() const
{
    return buffer_.size();
}

}
