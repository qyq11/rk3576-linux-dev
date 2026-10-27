#include "read_only_messages.h"

#include "frame_codec.h"
#include "status_session.h"

#include <QJsonArray>
#include <QMap>
#include <QStringList>
#include <QTextCodec>

namespace Sn100Protocol
{
namespace
{

constexpr int kMaximumFields = 4096;
constexpr int kMaximumFieldBytes = 4096;
constexpr int kMaximumCollection = 1024;

bool fail(QString *error, const QString &description)
{
    if (error)
    {
        *error = description;
    }
    return false;
}

bool readFields(const QByteArray &bytes, QStringList *fields, QString *error)
{
    QTextCodec *codec = QTextCodec::codecForName("UTF-8");
    if (!codec)
    {
        return fail(error, QStringLiteral("message.encoding: UTF-8编解码器不可用"));
    }
    QTextCodec::ConverterState state;
    const QString text = codec->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars || state.remainingChars)
    {
        return fail(error, QStringLiteral("message.encoding: 无效或未完成的UTF-8序列；不猜测GBK"));
    }

    int cursor = 2;
    while (cursor < text.size())
    {
        if (text.at(cursor++) != QLatin1Char('['))
        {
            return fail(error, QStringLiteral("message.syntax: 字段必须连续且完整地放在方括号内"));
        }
        const int start = cursor;
        while (cursor < text.size() && text.at(cursor) != QLatin1Char(']'))
        {
            const QChar character = text.at(cursor++);
            if (character == QLatin1Char('[') || character.category() == QChar::Other_Control)
            {
                return fail(error, QStringLiteral("message.syntax: 字段含嵌套括号或非法控制字符"));
            }
        }
        if (cursor == text.size())
        {
            return fail(error, QStringLiteral("message.syntax: 字段缺少结束括号"));
        }
        const QString field = text.mid(start, cursor - start);
        if (field.toUtf8().size() > kMaximumFieldBytes || fields->size() >= kMaximumFields)
        {
            return fail(error, QStringLiteral("message.limit: 超过本地4096字段或单字段4096字节预算"));
        }
        fields->append(field);
        ++cursor;
    }
    return true;
}

bool collectionCount(const QStringList &fields, int position, int *count, QString *error)
{
    if (position >= fields.size())
    {
        return fail(error, QStringLiteral("message.count: 缺少集合数量"));
    }
    const QString text = fields.at(position);
    if (text.isEmpty() || text.size() > 10)
    {
        return fail(error, QStringLiteral("message.count: 数量为空或超过可解析整数长度"));
    }
    for (QChar character : text)
    {
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
        {
            return fail(error, QStringLiteral("message.count: 数量必须为非负十进制整数"));
        }
    }
    bool ok = false;
    const qulonglong number = text.toULongLong(&ok);
    if (!ok || number > kMaximumCollection)
    {
        return fail(error, QStringLiteral("message.count: 数量超过本地1024项预算或整数溢出"));
    }
    *count = int(number);
    return true;
}

QJsonObject mapToJson(const QMap<QString, QString> &map)
{
    QJsonObject object;
    for (auto entry = map.cbegin(); entry != map.cend(); ++entry)
    {
        object.insert(entry.key(), entry.value());
    }
    return object;
}

bool parseChannels(const QStringList &fields, QJsonObject *data, QString *error)
{
    int count = 0;
    if (!collectionCount(fields, 0, &count, error))
    {
        return false;
    }
    if (fields.size() != 1 + count * 2)
    {
        return fail(error, QStringLiteral("message.count: Qq通道字段数与声明数量不一致"));
    }
    QMap<QString, QString> channels;
    int duplicates = 0;
    for (int index = 0; index < count; ++index)
    {
        const QString &channel = fields.at(1 + index * 2);
        const QString &rawAd = fields.at(2 + index * 2);
        // 原版按QString键处理，Base、1、01各自独立；重复键首次值生效。
        if (channels.contains(channel))
        {
            ++duplicates;
        }
        else
        {
            channels.insert(channel, rawAd);
        }
    }
    data->insert(QStringLiteral("declared_count"), count);
    data->insert(QStringLiteral("channels"), mapToJson(channels));
    data->insert(QStringLiteral("duplicate_channels"), duplicates);
    return true;
}

bool parseAlarms(const QByteArray &command, const QStringList &fields,
                 QJsonObject *data, QString *error)
{
    const bool fromBoard = command == "QK" || command == "QL";
    const int columns = fromBoard ? 3 : 2;
    int count = 0;
    if (!collectionCount(fields, 0, &count, error))
    {
        return false;
    }
    if (fields.size() != 1 + count * columns)
    {
        return fail(error, QStringLiteral("message.count: 告警/异常字段数与声明数量不一致"));
    }
    QJsonArray alarms;
    for (int index = 0; index < count; ++index)
    {
        const int start = 1 + index * columns;
        QJsonObject alarm{{QStringLiteral("error_code"), fields.at(start)}};
        if (fromBoard)
        {
            alarm.insert(QStringLiteral("description"), fields.at(start + 1));
            alarm.insert(QStringLiteral("measure"), fields.at(start + 2));
        }
        else
        {
            alarm.insert(QStringLiteral("raw_parameters"), fields.at(start + 1));
        }
        // 保留重复记录与顺序；源码UI按描述去重属于后续显示规则，不属协议。
        alarms.append(alarm);
    }
    data->insert(QStringLiteral("declared_count"), count);
    data->insert(QStringLiteral("from_board"), fromBoard);
    data->insert(QStringLiteral("notification"), command == "Ql" || command == "QL");
    data->insert(QStringLiteral("alarms"), alarms);
    return true;
}

bool parseConsumables(const QStringList &fields, QJsonObject *data, QString *error)
{
    int hasCount = 0;
    if (!collectionCount(fields, 0, &hasCount, error))
    {
        return false;
    }
    const int usedCountPosition = 1 + hasCount * 2;
    int usedCount = 0;
    if (!collectionCount(fields, usedCountPosition, &usedCount, error))
    {
        return false;
    }
    if (fields.size() != usedCountPosition + 1 + usedCount * 2)
    {
        return fail(error, QStringLiteral("message.count: QH两个项目集合的字段数与声明不一致"));
    }
    QMap<QString, QString> has;
    QMap<QString, QString> used;
    // 与原QHash赋值相同，重复项目最后一个值生效；值仍为原始字符串。
    for (int index = 0; index < hasCount; ++index)
    {
        has.insert(fields.at(1 + index * 2), fields.at(2 + index * 2));
    }
    for (int index = 0; index < usedCount; ++index)
    {
        const int start = usedCountPosition + 1 + index * 2;
        used.insert(fields.at(start), fields.at(start + 1));
    }
    data->insert(QStringLiteral("declared_has_count"), hasCount);
    data->insert(QStringLiteral("declared_used_count"), usedCount);
    data->insert(QStringLiteral("has"), mapToJson(has));
    data->insert(QStringLiteral("used"), mapToJson(used));
    return true;
}

} // namespace

QByteArray queryPayload(ReadOnlyQuery query)
{
    switch (query)
    {
    case ReadOnlyQuery::Status:
        return QByteArray("Qj");
    case ReadOnlyQuery::Alarm:
        return QByteArray("Qk");
    case ReadOnlyQuery::Consumables:
        return QByteArray("QH");
    }
    return {};
}

bool parseReadOnlyMessage(const QByteArray &message, ReadOnlyMessage *output,
                          QString *error)
{
    if (error)
    {
        error->clear();
    }
    if (!output || message.size() < 2 || message.size() > kMaxMessageBytes)
    {
        return fail(error, QStringLiteral("message.input: 输出为空或消息长度不在本地2..128000字节范围"));
    }
    ReadOnlyMessage parsed;
    parsed.command = message.left(2);
    if (parsed.command != "Qj" && parsed.command != "Qq" && parsed.command != "Qk" &&
        parsed.command != "Ql" && parsed.command != "QK" && parsed.command != "QL" &&
        parsed.command != "QH")
    {
        return fail(error, QStringLiteral("message.command: 当前只支持Qj/Qq/Qk/Ql/QK/QL/QH"));
    }
    QStringList fields;
    if (!readFields(message, &fields, error))
    {
        return false;
    }
    if (parsed.command == "Qj")
    {
        StatusSnapshot status;
        if (!parseStatus(message, &status, error))
        {
            return false;
        }
        parsed.data = status.toJson();
    }
    else if (parsed.command == "Qq")
    {
        if (!parseChannels(fields, &parsed.data, error))
        {
            return false;
        }
    }
    else if (parsed.command == "QH")
    {
        if (!parseConsumables(fields, &parsed.data, error))
        {
            return false;
        }
    }
    else if (!parseAlarms(parsed.command, fields, &parsed.data, error))
    {
        return false;
    }
    parsed.data.insert(QStringLiteral("evidence"), QStringLiteral("simulated-source-derived"));
    parsed.data.insert(QStringLiteral("source_encoding"), QStringLiteral("utf-8-offline-profile"));
    *output = parsed;
    return true;
}

} // namespace Sn100Protocol
