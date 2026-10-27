#ifndef SN100_READ_ONLY_MESSAGES_H
#define SN100_READ_ONLY_MESSAGES_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Sn100Protocol
{

enum class ReadOnlyQuery
{
    Status,
    Alarm,
    Consumables
};

// 仅暴露原 commandsender.cpp 中有实际发送函数的三个只读查询。
// Qq/QK 只接收；本接口不据接收分支猜测它们的实际请求链。
QByteArray queryPayload(ReadOnlyQuery query);

struct ReadOnlyMessage
{
    QByteArray command;
    QJsonObject data;
};

// 离线文本配置：严格 UTF-8，无括号转义、嵌套或控制字符。
// 本地预算：消息<=128000字节、字段<=4096、单字段<=4096字节、单集合<=1024。
// 预算和UTF-8配置均不表示现场固件规格已确认；不自动猜测GBK。
// Qj 委托既有 parseStatus，继续沿用其更窄的字段/ASCII支持范围。
// 成功才整体写入 output；合法零数量消息发布空集合，格式错误不发布清空。
bool parseReadOnlyMessage(const QByteArray &message, ReadOnlyMessage *output,
                          QString *error);

} // namespace Sn100Protocol

#endif
