#include "original_result_codec.h"
#include "protocol/frame_codec.h"
#include <QRegularExpression>
#include <QStringList>
#include <limits>

namespace OriginalTasks
{
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool number(const QString &text, qint64 minimum, qint64 maximum, qint64 *value)
{
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    bool ok = false; *value = text.toLongLong(&ok);
    return ok && digits.match(text).hasMatch() && *value >= minimum && *value <= maximum;
}
bool fields(const QByteArray &payload, QStringList *result, QString *message)
{
    if (!result || payload.size() < 2 || payload.size() > Sn100Protocol::kMaxMessageBytes)
        return fail(message, QStringLiteral("设备结果消息为空或超过128000字节，未截断保存。"));
    QStringList read;
    for (int cursor = 2; cursor < payload.size(); )
    {
        if (payload[cursor++] != '[') return fail(message, QStringLiteral("设备结果字段缺少左括号。"));
        const int end = payload.indexOf(']', cursor);
        if (end < cursor || read.size() >= 4096) return fail(message, QStringLiteral("设备结果字段不完整或超过4096项。"));
        const QByteArray value = payload.mid(cursor, end - cursor);
        for (unsigned char character : value)
            if (character < 32 || character == '[') return fail(message, QStringLiteral("设备结果字段含未定义转义或控制字符。"));
        read.append(QString::fromLatin1(value));
        cursor = end + 1;
    }
    *result = read;
    return true;
}
}

bool parseResult(const QByteArray &payload, ReceivedResult *result, QString *message)
{
    QStringList data;
    if (!result || !payload.startsWith("Tr") || !fields(payload, &data, message)) return false;
    if (data.size() < 18) return fail(message, QStringLiteral("原结果消息缺少任务身份、加样信息或曲线。"));
    qint64 needles = 0;
    ReceivedResult read;
    if (!number(data[3], 1, std::numeric_limits<int>::max(), &read.taskId) ||
        !number(data[9], 0, 64, &needles) || data.size() != 18 + needles * 3)
        return fail(message, QStringLiteral("结果任务编号、试剂针数量或完整字段数不符。"));
    const QStringList fixed{"taskType","rack_id","sample_id","task_id","project_id","project_name","batch_group","warm_channel","test_channel"};
    for (int i = 0; i < fixed.size(); ++i) read.fields.insert(fixed[i], data[i]);
    QStringList needleNumbers, temperatures, reagentHoles;
    int cursor = 10;
    for (int i = 0; i < needles; ++i)
    {
        needleNumbers.append(data[cursor++]); temperatures.append(data[cursor++]); reagentHoles.append(data[cursor++]);
    }
    read.fields.insert(QStringLiteral("needle_num"), needleNumbers.join(QStringLiteral("--")));
    read.fields.insert(QStringLiteral("needle_temp"), temperatures.join(QStringLiteral("--")));
    read.fields.insert(QStringLiteral("reagent_holeName"), reagentHoles.join(QStringLiteral("--")));
    for (const auto &key : {QStringLiteral("sample_rate"),QStringLiteral("sample_frequency"),QStringLiteral("sample_hole"),QStringLiteral("IBS_holeName")})
        read.fields.insert(key, data[cursor++]);
    cursor += 3; // 原协议预留三项，完整payload保留原值。
    const QStringList spectrums = data[cursor].split('|', Qt::SkipEmptyParts);
    if (spectrums.isEmpty()) return fail(message, QStringLiteral("结果没有任何原始曲线，未生成计算结果。"));
    for (const auto &spectrum : spectrums)
    {
        const int dash = spectrum.indexOf('-');
        if (dash < 1 || dash > 32 || dash + 1 == spectrum.size())
            return fail(message, QStringLiteral("原始曲线缺少波长标记或采样值。"));
        const QString key = QStringLiteral("spectrum_") + spectrum.left(dash);
        if (read.fields.contains(key)) return fail(message, QStringLiteral("结果重复带回同一波长曲线，未选择其中一条覆盖。"));
        read.fields.insert(key, spectrum.mid(dash + 1));
    }
    read.payload = payload;
    *result = read;
    return true;
}

bool parseProgress(const QByteArray &payload, ReceivedProgress *progress, QString *message)
{
    QStringList data;
    if (!progress || !payload.startsWith("TD") || !fields(payload, &data, message)) return false;
    if (data.size() != 3 && data.size() != 7) return fail(message, QStringLiteral("原加样进度消息应为3项或7项，未按错位字段更新样本。"));
    ReceivedProgress read;
    if (!number(data[2], 1, std::numeric_limits<int>::max(), &read.taskId)) return fail(message, QStringLiteral("加样进度任务编号无效。"));
    read.rack = data[0]; read.hole = data[1];
    if (data.size() == 7) { read.state = data[3]; read.type = data[5]; read.project = data[6]; }
    *progress = read;
    return true;
}
}
