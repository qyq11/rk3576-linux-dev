#ifndef SN100_ORIGINAL_LIS_QUERY_H
#define SN100_ORIGINAL_LIS_QUERY_H

#include <QString>

namespace Sn100Lis
{
// 原查询先后读取五次时钟，跨秒/跨日时可以不同；调用者按原顺序提供文字。
// 本对象不读取系统时间，也不把五次取时合并成一次。
struct OriginalLisQueryTimes
{
    QString controlHhmmss;       // 第一次：SystemCommon::getNow("%H%M%S")。
    QString messageTimestamp;   // 第二次：MSH构造的yyyyMMddHHmmss.zzz本地时间。
    QString queryTimestamp;     // 第三次：QRD构造的同格式本地时间。
    QString filterStartNow;     // 第四次：getNow()的yyyyMMddHHmmss，原整数规则取日首。
    QString filterEndNow;       // 第五次：另一次getNow()的yyyyMMddHHmmss，原样作为终点。
};

struct OriginalLisQuery
{
    QString body;               // 无MLLP头尾的正文；每段末尾保留CR，交传输层只封包一次。
    QString lisCode;            // 原MSH-10/QRD-4控制号，不是传输层的递增请求编号。
};

// 对应原LisCommSetting::genLISFromSeverInfo的正文部分。
// sampleBarcode是主控扫码条码，不是内部样本序号；此处不选孔、不校验状态、不置T1。
// 保留原数字转换失败得到0的行为；调用者负责按原入口提供孔号和五个取时点。
OriginalLisQuery buildOriginalLisQuery(QString rackId, int holeNumber,
    const QString &sampleBarcode, const OriginalLisQueryTimes &times);
}

#endif
