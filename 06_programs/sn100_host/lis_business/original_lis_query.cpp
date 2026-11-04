#include "original_lis_query.h"

#include <QVector>

namespace Sn100Lis
{
namespace
{
constexpr int MshLastField = 20;
constexpr int QrdLastField = 19;
constexpr int QrfLastField = 19;

QString segmentText(const QVector<QString> &fields, int firstField)
{
    QString text = fields.at(0);
    for (int field = firstField; field < fields.size(); ++field)
    {
        text += QChar('|');
        text += fields.at(field);
    }
    text += QChar('\r');
    return text;
}
}

OriginalLisQuery buildOriginalLisQuery(QString rackId, int holeNumber,
    const QString &sampleBarcode, const OriginalLisQueryTimes &times)
{
    OriginalLisQuery query;
    // 原Windows/MinGW32的toULong及后续算术为32位无符号数。
    // ARM64的unsigned long为64位，不能直接照写而改变失败/边界输入的结果。
    const quint32 control = times.controlHhmmss.toUInt() * quint32(100) + quint32(holeNumber);
    query.lisCode = QString::number(control);

    // MSH是消息头：原构造补到20，序列化从2开始，字段1只表示分隔符。
    QVector<QString> msh(MshLastField + 1);
    msh[0] = QStringLiteral("MSH");
    msh[1] = QStringLiteral("|");
    msh[2] = QStringLiteral("^~\\&");
    msh[7] = times.messageTimestamp;
    msh[9] = QStringLiteral("QRY^Q02");
    msh[10] = query.lisCode;
    msh[11] = QStringLiteral("P");
    msh[12] = QStringLiteral("2.3.1");
    msh[16] = QStringLiteral("0");
    msh[18] = QStringLiteral("UNICODE");

    // QRD是查询条件：原查询不填写设备序列号，也不因急诊更改D/N/T。
    QVector<QString> qrd(QrdLastField + 1);
    qrd[0] = QStringLiteral("QRD");
    qrd[1] = times.queryTimestamp;
    qrd[2] = QStringLiteral("BC");
    qrd[3] = QStringLiteral("D");
    qrd[4] = query.lisCode;
    qrd[7] = QStringLiteral("RD");
    qrd[8] = sampleBarcode;
    qrd[10] = QString::number(rackId.remove(0, 1).toInt()) + QChar('^') + QString::number(holeNumber);
    qrd[11] = QStringLiteral("N");
    qrd[12] = QStringLiteral("T");

    // QRF是时间条件：沿原整数除乘取日首，不用日期库重新解释或修正文字。
    const qulonglong dayStart = times.filterStartNow.toULongLong() / 1000000 * 1000000;
    QVector<QString> qrf(QrfLastField + 1);
    qrf[0] = QStringLiteral("QRF");
    qrf[2] = QString::number(dayStart);
    qrf[3] = times.filterEndNow;
    qrf[6] = QStringLiteral("RCT");
    qrf[7] = QStringLiteral("COR");
    qrf[8] = QStringLiteral("ALL");

    const QString body = segmentText(msh, 2) + segmentText(qrd, 1) + segmentText(qrf, 1);
    // 原各字段toStdString后拼接，再fromStdString。保留同一UTF8往返，
    // 不以QByteArray重载提前截断NUL，不擅加字段转义或MLLP边界。
    query.body = QString::fromStdString(body.toStdString());
    return query;
}
}
