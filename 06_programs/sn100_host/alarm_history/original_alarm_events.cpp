#include "original_alarm_events.h"
#include <QJsonArray>

namespace AlarmHistory
{
namespace
{
bool rejected(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
QString substituteDescription(QString description, const QStringList &parameters)
{
    // 对照原MaskDlgAbnormal::convertAlarm：只展开1至5个非空参数，超过5个原样保留。
    switch (parameters.size())
    {
    case 1: return description.arg(parameters[0]);
    case 2: return description.arg(parameters[0], parameters[1]);
    case 3: return description.arg(parameters[0], parameters[1], parameters[2]);
    case 4: return description.arg(parameters[0], parameters[1], parameters[2], parameters[3]);
    case 5: return description.arg(parameters[0], parameters[1], parameters[2], parameters[3], parameters[4]);
    default: return description;
    }
}
}

bool originalNewEvents(const Sn100Protocol::ReadOnlyMessage &message,
                       const QVector<OriginalRuntime::AlarmRule> &rules,
                       const QString &currentUser, const QDateTime &receivedAt,
                       Rows *result, QString *error)
{
    if (!result || (message.command != "Ql" && message.command != "QL") ||
        !message.data.value(QStringLiteral("notification")).isBool() || !message.data.value(QStringLiteral("notification")).toBool())
        return rejected(error, QStringLiteral("只有已解析的新告警Ql/QL可以追加；当前查询Qk/QK和常规状态不写历史。"));
    if (!receivedAt.isValid() || receivedAt.date().year() < 1 || receivedAt.date().year() > 9999 || currentUser.contains(QChar::Null))
        return rejected(error, QStringLiteral("新告警缺少明确接收时间或操作员上下文，未生成历史。"));
    const bool fromBoard = message.command == "QL";
    if (!message.data.value(QStringLiteral("from_board")).isBool() || message.data.value(QStringLiteral("from_board")).toBool() != fromBoard ||
        !message.data.value(QStringLiteral("alarms")).isArray())
        return rejected(error, QStringLiteral("新告警来源或字段结构与已解析命令不符。"));
    const QJsonArray input = message.data.value(QStringLiteral("alarms")).toArray();
    const double declared = message.data.value(QStringLiteral("declared_count")).toDouble(-1);
    if (input.size() > 1024 || declared != input.size())
        return rejected(error, QStringLiteral("新告警数量不符或超过既有协议解析单批1024条上限，未截断写入。"));
    Rows output;
    for (const QJsonValue &value : input)
    {
        if (!value.isObject()) return rejected(error, QStringLiteral("新告警项不是完整字段对象。"));
        const QJsonObject object = value.toObject();
        const QString code = object.value(QStringLiteral("error_code")).toString();
        if (!object.value(QStringLiteral("error_code")).isString() || code.isEmpty() || code != code.trimmed() || code.size() > 63)
            return rejected(error, QStringLiteral("新告警编号为空或无效，未猜造编号。"));
        Row row;
        row.errorCode = code + (fromBoard ? QStringLiteral("*") : QString());
        row.dateTime = receivedAt.toString(QStringLiteral("yyyy/MM/dd HH:mm:ss"));
        row.user = currentUser;
        if (fromBoard)
        {
            if (!object.value(QStringLiteral("description")).isString() || !object.value(QStringLiteral("measure")).isString())
                return rejected(error, QStringLiteral("主控新异常须有原描述和处理提示两个字段。"));
            row.description = object.value(QStringLiteral("description")).toString();
            row.measure = object.value(QStringLiteral("measure")).toString();
        }
        else
        {
            if (!object.value(QStringLiteral("raw_parameters")).isString())
                return rejected(error, QStringLiteral("新告警缺少原参数字段。"));
            const QString rawParameters = object.value(QStringLiteral("raw_parameters")).toString();
            QStringList parameters = rawParameters.split(QLatin1Char('|'));
            parameters.removeAll(QString());
            for (const auto &rule : rules)
                if (rule.code == code)
                {
                    row.description = substituteDescription(rule.description, parameters);
                    row.measure = rule.measure;
                    break;
                }
            // 原版未匹配配置或描述为空时拼接原始二字段，不借未知编号生成严重程度或动作。
            if (row.description.isEmpty()) { row.description = code + rawParameters; row.measure.clear(); }
        }
        for (const QString &field : {row.errorCode, row.description, row.measure, row.user})
            if (field.contains(QChar::Null) || field.toUtf8().size() > 65536)
                return rejected(error, QStringLiteral("新告警字段含空字符或超出上限，未生成部分历史。"));
        output.append(row);
    }
    *result = output;
    return true;
}
}
