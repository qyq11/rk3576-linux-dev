#ifndef SN100_LIS_MLLP_CODEC_H
#define SN100_LIS_MLLP_CODEC_H

#include <QByteArray>
#include <QList>
#include <QString>

namespace Sn100Lis
{

// 只处理原LIS消息的编码和MLLP头尾，不解释HL7字段或确认检测任务。
class MllpCodec
{
public:
    static constexpr int MaximumFrameBytes = 1048576;

    explicit MllpCodec(bool utf16 = false);
    void reset(bool utf16);
    bool feed(const QByteArray &bytes, QList<QByteArray> *bodies, QString *error);
    static QByteArray encode(const QString &message, bool utf16);
    static bool decode(const QByteArray &body, bool utf16, QString *message, QString *error);
    int bufferedBytes() const;

private:
    bool utf16_ = false;
    QByteArray buffer_;
};

}

#endif
