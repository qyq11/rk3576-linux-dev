#include "original_lis_download_ack.h"

#include <QVector>
#include <initializer_list>

namespace Sn100Lis
{
namespace
{
OriginalLisDownloadAck rejected(const QString &field, const QString &detail)
{
    OriginalLisDownloadAck result; result.error.field = field; result.error.detail = detail;
    return result;
}

bool originalTimestampShape(const QString &text)
{
    if (text.size() != 18 || text.at(14) != QChar('.')) return false;
    for (int index = 0; index < text.size(); ++index)
        if (index != 14 && (text.at(index) < QChar('0') || text.at(index) > QChar('9'))) return false;
    return true; // 只核显式输入格式，不从这里证明实际时刻或应用阶段。
}
}

OriginalLisDownloadAck buildOriginalLisDownloadAck(const ConfigurationIdentity &configuration,
    const ReceiveProfilePtr &profile, const QString &receivedControlId, const OriginalLisDownloadAckTimes &times)
{
    if (!profile || !profile->matches(configuration))
        return rejected(QStringLiteral("source"), QStringLiteral("缺少同配置身份的原快照，没有重读当前序列号。"));
    if (!originalTimestampShape(times.messageTimestampAfterApplication))
        return rejected(QStringLiteral("MSH.7"), QStringLiteral("调用点须显式提供应用/MAX之后的yyyyMMddHHmmss.zzz时间；没有补当前时间。"));
    // 原ACK()为空，之后显式构造MSH/MSA；不是ACK(request)交换双方字段。
    QVector<QString> msh(21), msa(3);
    msh[0] = QStringLiteral("MSH"); msh[1] = QStringLiteral("|"); msh[2] = QStringLiteral("^~\\&");
    msh[3] = profile->sequenceNumber(); msh[7] = times.messageTimestampAfterApplication;
    msh[9] = QStringLiteral("ACK^Q03"); msh[10] = receivedControlId;
    msh[11] = QStringLiteral("P"); msh[12] = QStringLiteral("2.3.1");
    msh[16] = QStringLiteral("0"); msh[18] = QStringLiteral("UNICODE");
    msa[0] = QStringLiteral("MSA"); msa[1] = QStringLiteral("AA"); msa[2] = receivedControlId;
    qint64 codeUnits = 0;
    for (const auto *segment : {&msh, &msa})
    {
        const int first = segment == &msh ? 2 : 1;
        const qint64 fixed = qint64(segment->at(0).size()) + 1 + segment->size() - first;
        if (fixed > OriginalLisMaximumAckTextBytes / 2 - codeUnits)
            return rejected(QStringLiteral("ACK"), QStringLiteral("ACK正文超过本地4MiB文字预算，未截断。"));
        codeUnits += fixed;
        for (int index = first; index < segment->size(); ++index)
        {
            if (qint64(segment->at(index).size()) > OriginalLisMaximumAckTextBytes / 2 - codeUnits)
                return rejected(QStringLiteral("ACK"), QStringLiteral("ACK字段超过本地4MiB文字预算，未截断。"));
            codeUnits += segment->at(index).size();
        }
    }
    QString body; body.reserve(int(codeUnits));
    for (const auto *segment : {&msh, &msa})
    {
        const int first = segment == &msh ? 2 : 1;
        body += segment->at(0);
        for (int index = first; index < segment->size(); ++index)
        { body += QChar('|'); body += segment->at(index); }
        body += QChar('\r'); // MSH20为空也保留最后分隔符；每段只加CR，不封MLLP。
    }
    OriginalLisDownloadAck result;
    result.body = QString::fromStdString(body.toStdString()); // 原UTF8往返，不加转义/编码猜测。
    result.state = OriginalLisAckState::Prepared;
    return result;
}
}
