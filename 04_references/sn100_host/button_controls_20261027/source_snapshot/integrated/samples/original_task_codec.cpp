#include "original_task_codec.h"
#include "protocol/frame_codec.h"
#include <QRegularExpression>
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

bool integer(const QString &text, qint64 minimum, qint64 maximum, qint64 *number)
{
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    bool ok = false;
    *number = text.toLongLong(&ok);
    return ok && digits.match(text).hasMatch() && *number >= minimum && *number <= maximum;
}

bool wireField(const QString &field)
{
    if (field.size() > 4096 || QString::fromLatin1(field.toLatin1()) != field) return false;
    for (const auto character : field)
        if (character.unicode() < 32 || character == QLatin1Char('[') || character == QLatin1Char(']')) return false;
    return true;
}

void appendSlot(QStringList *fields, const QPair<QString, QString> &slot, bool linuxNew)
{
    fields->append(slot.first);
    if (!linuxNew) fields->append(slot.second);
}
}

bool parseRackRequest(const QByteArray &payload, RackRequest *request, QString *message)
{
    if (!request || !payload.startsWith("Tt") || payload.size() > Sn100Protocol::kMaxMessageBytes)
        return fail(message, QStringLiteral("设备任务请求不是有效Tt消息。"));
    QStringList values;
    int cursor = 2;
    while (cursor < payload.size())
    {
        if (payload[cursor++] != '[') return fail(message, QStringLiteral("任务请求字段缺少左括号。"));
        const int end = payload.indexOf(']', cursor);
        if (end < cursor || end - cursor > 4096 || values.size() >= 4096)
            return fail(message, QStringLiteral("任务请求字段不完整或超过读取上限。"));
        const QString field = QString::fromLatin1(payload.mid(cursor, end - cursor));
        if (!wireField(field)) return fail(message, QStringLiteral("任务请求含非法字段。"));
        values.append(field);
        cursor = end + 1;
    }
    if (values.size() < 3) return fail(message, QStringLiteral("任务请求缺少架号、孔位数或已接收任务数。"));
    RackRequest parsed;
    parsed.rack = values[0];
    // 原pcTaskSend允许空架号；是否采用首个就绪预设架由原“关闭架扫码”配置决定。
    qint64 count = 0;
    if (!integer(values[1], 0, 128, &count) || values.size() < 3 + count * 2)
        return fail(message, QStringLiteral("任务请求的孔位数或对应条码不完整。"));
    cursor = 2;
    for (int i = 0; i < count; ++i)
    {
        qint64 hole = 0;
        if (!integer(values[cursor++], 1, 128, &hole) || parsed.barcodes.contains(int(hole)))
            return fail(message, QStringLiteral("任务请求孔位号非法或重复。"));
        parsed.barcodes.insert(int(hole), values[cursor++]);
    }
    if (!integer(values[cursor++], 0, 4096, &count) || values.size() != cursor + count)
        return fail(message, QStringLiteral("设备已接收任务清单与声明数量不符。"));
    for (int i = 0; i < count; ++i)
    {
        qint64 id = 0;
        if (!integer(values[cursor++], 1, std::numeric_limits<int>::max(), &id))
            return fail(message, QStringLiteral("设备已接收任务编号无效。"));
        parsed.receivedTaskIds.insert(id);
    }
    *request = parsed;
    return true;
}

bool taskFields(const Task &task, bool linuxNew, QStringList *fields, QString *message)
{
    if (!fields || task.taskId <= 0 || task.taskId > std::numeric_limits<int>::max() ||
        task.reportRow <= 0 || task.hole < 1 || task.hole > 128 || task.rack.isEmpty() ||
        (task.container != QStringLiteral("tube") && task.container != QStringLiteral("cup")) ||
        (task.type != QStringLiteral("SAM") && task.type != QStringLiteral("QC")))
        return fail(message, QStringLiteral("任务编号、报告行、样本位置或任务类型不完整，未生成命令。"));
    const auto &project = task.project;
    const auto &base = project.originalBaseValues;
    if (project.originalFileName.isEmpty() || !project.enabled || !project.advanced.configured)
        return fail(message, QStringLiteral("任务必须引用当前原项目库中已启用并有完整加样参数的项目。"));
    ProjectActionFields sample;
    QString defaultRate;
    bool rateEnabled = false;
    for (const auto &option : project.sampleRates)
    {
        if (option.defaultOption) defaultRate = option.sampleRate;
        if (option.sampleRate == task.rate) rateEnabled = option.sampleOption;
    }
    for (const auto &row : project.advanced.samples)
    {
        if (row.value(QStringLiteral("sample_rate")) == task.rate) sample = row;
    }
    if (sample.isEmpty() || !rateEnabled)
        return fail(message, QStringLiteral("任务所选稀释方案已不存在或未启用，请重新保存样本设置。"));
    if (task.reagentSlots.size() != project.advanced.reagents.size() || task.batchGroup.isEmpty())
        return fail(message, QStringLiteral("任务未按试剂步骤分配完整孔位或批组，未生成命令。"));
    const QString name = base.value(QStringLiteral("project_en_name"));
    if (name.isEmpty() || task.sample.isEmpty()) return fail(message, QStringLiteral("项目英文名或样本编号为空。"));
    QStringList result{task.type, QString::number(task.taskId)};
    if (!linuxNew) result.append(task.rack);
    result << QString::number(task.hole) << task.container << task.sample;
    if (!linuxNew) result.append(base.value(QStringLiteral("project_id")));
    result << name << (base.value(QStringLiteral("test_analysis_method")) == QStringLiteral("凝固法") ? QStringLiteral("0") : QStringLiteral("1"));
    if (linuxNew) result << (task.prolonged ? QStringLiteral("1") : QStringLiteral("0")) << task.rate;
    else
    {
        result << base.value(QStringLiteral("warmup_banding_channel")) << base.value(QStringLiteral("test_banding_channel"))
               << base.value(QStringLiteral("min_test_time"));
        if (task.prolonged)
        {
            qint64 maximum = 0, extra = 0;
            if (!integer(base.value(QStringLiteral("max_test_time")), 0, 86400, &maximum) ||
                !integer(base.value(QStringLiteral("sample_change_clean")), 0, 86400, &extra))
                return fail(message, QStringLiteral("原项目延长测试时间参数无效。"));
            result << QString::number(maximum + extra);
        }
        else result << base.value(QStringLiteral("max_test_time"));
        result << base.value(QStringLiteral("first_light")) << base.value(QStringLiteral("second_light"));
        const auto &algorithm = project.originalAlgorithmValues;
        const QString key = algorithm.contains(QStringLiteral("testMode"))
            ? algorithm.value(QStringLiteral("testMode")) == QStringLiteral("0") ? QStringLiteral("Coa_dT") : QStringLiteral("Sort_dT")
            : QStringLiteral("0");
        result << algorithm.value(key, QStringLiteral("1")) << defaultRate << task.rate;
    }
    result << task.batchGroup;
    if (!linuxNew) result << base.value(QStringLiteral("IBS_name")) << sample.value(QStringLiteral("IBS_amount_ul"));
    appendSlot(&result, task.sampleAreaSlots.value(base.value(QStringLiteral("IBS_name"))), linuxNew);
    if (!linuxNew)
    {
        for (const QString &key : {QStringLiteral("sample_amount_ul"), QStringLiteral("blender_type"), QStringLiteral("sample_add_mode"),
             QStringLiteral("mix_amount_ul"), QStringLiteral("second_IBS_amount_ul"), QStringLiteral("action_amount_ul"),
             QStringLiteral("before_clean_name"), QStringLiteral("before_clean_frequency"), QStringLiteral("before_clean_amount_ul")})
            result << sample.value(key);
        appendSlot(&result, task.sampleAreaSlots.value(QStringLiteral("Cleaner")), false);
        for (const QString &key : {QStringLiteral("after_clean_name"), QStringLiteral("after_clean_frequency"), QStringLiteral("after_clean_amount_ul")})
            result << sample.value(key);
        appendSlot(&result, task.sampleAreaSlots.value(QStringLiteral("Cleaner")), false);
    }
    result << QString::number(project.advanced.reagents.size());
    for (int i = 0; i < project.advanced.reagents.size(); ++i)
    {
        const auto &step = project.advanced.reagents[i];
        if (linuxNew)
            result << base.value(QStringLiteral("is_related")) << (task.multiBottle ? task.reagentSlots[i].first : QString());
        else
        {
            result << step.value(QStringLiteral("reagent_name"))
                   << (step.value(QStringLiteral("needle_select")) == QStringLiteral("左试剂针") ? QStringLiteral("1") : QStringLiteral("2"))
                   << step.value(QStringLiteral("reagent_amount_ul"));
            appendSlot(&result, task.reagentSlots[i], false);
            for (const QString &key : {QStringLiteral("blender_type"), QStringLiteral("before_clean_name"),
                 QStringLiteral("before_clean_frequency"), QStringLiteral("before_clean_amount_ul")}) result << step.value(key);
            appendSlot(&result, task.reagentAreaSlots.value(step.value(QStringLiteral("before_clean_name"))), false);
            for (const QString &key : {QStringLiteral("after_clean_name"), QStringLiteral("after_clean_frequency"),
                 QStringLiteral("after_clean_amount_ul")}) result << step.value(key);
            // 原旧版这里保留空字段和混匀时间，没有再追加后清洗孔号。
            result << QString() << step.value(QStringLiteral("blender_time_sec")) << step.value(QStringLiteral("reagent_warmup_length"));
        }
    }
    for (const auto &field : result)
        if (!wireField(field)) return fail(message, QStringLiteral("任务字段含括号、控制字符或原Latin1协议不能表示的文字，未发送。"));
    *fields = result;
    return true;
}

bool rackPayload(const QString &rack, const QList<Task> &tasks, bool linuxNew, QByteArray *payload, QString *message)
{
    if (!payload || (rack.isEmpty() && !tasks.isEmpty()) || !wireField(rack) || tasks.size() > 4096)
        return fail(message, QStringLiteral("样本架号无效或本次任务数超过4096。"));
    QByteArray result = "Tt[" + rack.toLatin1() + "][" + QByteArray::number(tasks.size()) + ']';
    for (const auto &task : tasks)
    {
        if (task.rack != rack) return fail(message, QStringLiteral("同一次下发混入其他样本架，未发送。"));
        QStringList values;
        if (!taskFields(task, linuxNew, &values, message)) return false;
        for (const auto &field : values) result += '[' + field.toLatin1() + ']';
        if (result.size() > Sn100Protocol::kMaxMessageBytes)
            return fail(message, QStringLiteral("任务报文超过当前128000字节上限，未截断任务。"));
    }
    *payload = result;
    return true;
}
}
