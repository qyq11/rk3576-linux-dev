#include "reagents/original_reagent_repository.h"
#include "original_files/original_ini.h"
#include "projects/original_project_repository.h"
#include "protocol/frame_codec.h"
#include <QDate>
#include <QDir>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

using namespace OriginalBusiness;
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool number(const QVariant &value, double *result)
{
    bool ok = false;
    *result = value.toString().toDouble(&ok);
    return ok && std::isfinite(*result) && *result >= 0;
}
const Row *named(const Rows &rows, const QString &field, const QString &name)
{
    for (const auto &row : rows) if (row.value(field).toString() == name) return &row;
    return nullptr;
}
QString text(const Row &row, const char *name) { return row.value(QString::fromLatin1(name)).toString(); }
bool slot(const Row &row, QPair<QString, QString> *result, QString *message)
{
    const QString name = text(row, "hole_name");
    const QString send = name.section('_', 1, 1);
    bool ok = false;
    const int index = send.toInt(&ok);
    if (!ok || index < 0 || index > 255) return fail(message, QStringLiteral("原试剂孔名不能得到0–255的发送孔号：") + name);
    *result = qMakePair(name, QString::number(index));
    return true;
}
bool eligible(const OriginalReagentState &state, const Row &hole)
{
    const QString status = text(hole, "state");
    if (status == QStringLiteral("r_none") || status == QStringLiteral("r_unknown") || status == QStringLiteral("r_outdate") ||
        text(hole, "reagent_name").isEmpty() || text(hole, "reagent_name") == QStringLiteral("none")) return false;
    const auto *reagent = named(state.reagents, QStringLiteral("reagent_name"), text(hole, "reagent_name"));
    if (!reagent || !reagent->value(QStringLiteral("valid")).toBool()) return false;
    // 原check_limit要求检查有效期时，按原保存格式读取；不把无效日期当作尚未过期。
    if (reagent->value(QStringLiteral("check_limit")).toInt() > 0)
    {
        QDate expiry = QDate::fromString(text(hole, "out_date"), QStringLiteral("yyyy/MM/dd"));
        if (!expiry.isValid()) expiry = QDate::fromString(text(hole, "out_date"), Qt::ISODate);
        if (!expiry.isValid() || expiry < QDate::currentDate()) return false;
    }
    return true;
}
bool appendWire(QByteArray *payload, const QStringList &fields, QString *message)
{
    for (const auto &field : fields)
    {
        if (QString::fromLatin1(field.toLatin1()) != field || field.contains('[') || field.contains(']'))
            return fail(message, QStringLiteral("原试剂通信字段含协议不能表示的文字或括号，未发送。"));
        for (const auto character : field)
            if (character.unicode() < 32) return fail(message, QStringLiteral("原试剂通信字段含控制字符。"));
        *payload += '[' + field.toLatin1() + ']';
        if (payload->size() > Sn100Protocol::kMaxMessageBytes) return fail(message, QStringLiteral("原试剂报文超过128000字节，未截断。"));
    }
    return true;
}
}

bool OriginalReagentRepository::initialize(const QString &root, QString *message)
{
    if (!workingRoot(root, message)) return false;
    OriginalIni::Settings settings;
    if (!OriginalIni::read(QDir(root).filePath(QStringLiteral("theme_config.ini")), &settings, message)) return false;
    bool ok = false;
    const int count = settings.values.value(QStringLiteral("theme/ReagentPanelChannelCount"), 20).toInt(&ok);
    if (!ok || (count != 20 && count != 42)) return fail(message, QStringLiteral("原试剂盘孔数未对应20或42配置，未选择其他机型表。"));
    root_ = root;
    table_ = count == 20 ? QStringLiteral("reagent_setting") : QStringLiteral("reagent_setting_6600");
    OriginalReagentState state;
    return load(&state, message);
}

bool OriginalReagentRepository::load(OriginalReagentState *state, QString *message)
{
    if (!state) return fail(message, QStringLiteral("原试剂读取输出为空。"));
    OriginalReagentState read;
    read.root = root_;
    read.settingTable = table_;
    const QStringList files{QStringLiteral("db/userSet/reagent_Information.db"), QStringLiteral("db/systemInfo/reagent_Bottle_Type.db"),
                            QStringLiteral("db/userSet/reagent_Setting.db")};
    for (const auto &file : files)
    {
        const auto version = OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file), message);
        if (version.isEmpty()) return false;
        read.versions.insert(file, version);
    }
    Database information, bottles, setting;
    if (!information.open(root_, files[0], false, message) || !bottles.open(root_, files[1], false, message) ||
        !setting.open(root_, files[2], false, message)) return false;
    if (!information.rows(QStringLiteral("reagent_list"), {"id","reagent_name","reagent_code","reagent_type","banding_needle",
        "ref_use_ul","manufacturer","dect_clean","cleaner_name","valid","warning_ul","out_ul","useage_ul","fixed","check_limit"}, &read.reagents, message) ||
        !information.rows(QStringLiteral("reagent_batch_group"), {"id","project_name","batch_group"}, &read.batchGroups, message) ||
        !information.rows(QStringLiteral("reagent_barcode"), {"id","reagent_name","reagent_barcode","reagent_limit_date","bottle_type","isi_value"}, &read.barcodes, message) ||
        !bottles.rows(QStringLiteral("reagent_bottle_type"), {"id","bottle_type_name","area","bottle_real_height_mm","max_amount_ul","is_adpter","bottom_diameter","angle"}, &read.bottles, message) ||
        !setting.rows(table_, {"id","hole_name","area","reagent_type","reagent_name","reagent_batch","bottle_type","amount_uL",
            "priority","state","datetime","out_date","formula_level","used_ul","out_ul","warning_ul","max_ul"}, &read.holes, message)) return false;
    QSet<QString> uniqueHoles;
    for (const auto &hole : read.holes)
    {
        if (uniqueHoles.contains(text(hole, "hole_name"))) return fail(message, QStringLiteral("原试剂表存在重名孔位，未合并。"));
        uniqueHoles.insert(text(hole, "hole_name"));
        if (eligible(read, hole))
        {
            double remaining = 0;
            if (!number(hole.value(QStringLiteral("used_ul")), &remaining))
                return fail(message, QStringLiteral("原试剂剩余量不是有效的非负数：") + text(hole, "hole_name"));
        }
    }
    for (const auto &file : files)
        if (read.versions.value(file) != OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file), message))
            return fail(message, QStringLiteral("读取期间原试剂数据发生变化，请重新读取。"));
    *state = read;
    return true;
}

bool OriginalReagentRepository::cleanerReady(const OriginalReagentState &state, QString *message) const
{
    double sample = 0, reagent = 0;
    for (const auto &hole : state.holes)
        if (text(hole, "reagent_name") == QStringLiteral("Cleaner") && eligible(state, hole))
        {
            double value = 0;
            if (!number(hole.value(QStringLiteral("used_ul")), &value)) return fail(message, QStringLiteral("Cleaner剩余量无效。"));
            if (text(hole, "area") == QStringLiteral("s_area")) sample += value;
            if (text(hole, "area") == QStringLiteral("r_area")) reagent += value;
        }
    if (sample < 500 || reagent < 500) return fail(message, QStringLiteral("样本区和试剂区均须放置至少500µL Cleaner，与原Windows开始检查一致。"));
    return true;
}

bool OriginalReagentRepository::assignTask(const OriginalReagentState &state, OriginalTasks::Task *task,
                                         QMap<qint64, double> *remaining, QString *message) const
{
    if (!task || !remaining) return fail(message, QStringLiteral("原任务试剂分配输入为空。"));
    QMap<qint64, double> proposed = *remaining;
    for (const auto &hole : state.holes)
    {
        const qint64 id = hole.value(QStringLiteral("id")).toLongLong();
        if (!proposed.contains(id) && eligible(state, hole))
        {
            double amount = 0;
            if (!number(hole.value(QStringLiteral("used_ul")), &amount)) return fail(message, QStringLiteral("原试剂剩余量无效。"));
            proposed.insert(id, amount);
        }
    }
    auto holes = state.holes;
    std::stable_sort(holes.begin(), holes.end(), [](const Row &a, const Row &b)
    { return a.value(QStringLiteral("priority")).toInt() < b.value(QStringLiteral("priority")).toInt(); });
    const bool related = task->project.originalBaseValues.value(QStringLiteral("is_related")).toInt() > 0;
    QStringList groups;
    if (related)
        for (const auto &row : state.batchGroups)
            if (text(row, "project_name") == task->project.name) groups.append(text(row, "batch_group"));
    if (!related) groups = QStringList{QStringLiteral("Default")};
    else if (!groups.contains(QStringLiteral("Default"))) groups.append(QStringLiteral("Default"));
    QList<QPair<QString, QString>> chosen;
    QString selectedGroup;
    QMap<qint64, double> selectedRemaining;
    for (const auto &group : groups)
    {
        auto trial = proposed;
        QList<QPair<QString, QString>> allocation;
        bool ready = true;
        for (const auto &step : task->project.advanced.reagents)
        {
            double volume = 0;
            if (!number(step.value(QStringLiteral("reagent_amount_ul")), &volume))
                return fail(message, QStringLiteral("原项目试剂加样量无效。"));
            const Row *selected = nullptr;
            for (const auto &hole : holes)
            {
                const QString batch = text(hole, "reagent_batch");
                const bool inGroup = !related || (group == QStringLiteral("Default")
                    ? batch == QStringLiteral("Default") || batch == QStringLiteral("None")
                    : group.split('|', Qt::KeepEmptyParts).contains(batch));
                if (text(hole, "area") != QStringLiteral("r_area") || !eligible(state, hole) || !inGroup ||
                    text(hole, "reagent_name") != step.value(QStringLiteral("reagent_name"))) continue;
                const auto *definition = named(state.reagents, QStringLiteral("reagent_name"), text(hole, "reagent_name"));
                double minimum = 0;
                if (!definition || !number(definition->value(QStringLiteral("out_ul")), &minimum)) continue;
                const qint64 id = hole.value(QStringLiteral("id")).toLongLong();
                if (trial.value(id) - minimum >= volume) { selected = &hole; break; }
            }
            if (!selected) { ready = false; break; }
            QPair<QString, QString> chosenSlot;
            if (!slot(*selected, &chosenSlot, message)) return false;
            allocation.append(chosenSlot);
            trial[selected->value(QStringLiteral("id")).toLongLong()] -= volume;
        }
        if (ready)
        {
            chosen = allocation;
            selectedGroup = group;
            selectedRemaining = trial;
            break;
        }
    }
    if (selectedGroup.isEmpty()) return fail(message, QStringLiteral("项目%1没有足量且批组匹配的可用试剂，未下发该任务。").arg(task->project.name));
    QMap<QString, QPair<QString, QString>> sampleSlots, reagentSlots;
    // 原辅助清洗/稀释孔位选择：按表顺序取最后匹配项，遇priority=1则立即采用。
    for (const auto &area : {QStringLiteral("s_area"), QStringLiteral("r_area")})
    {
        QSet<QString> firstPriorityChosen;
        for (const auto &hole : state.holes)
        {
            if (text(hole, "area") != area || text(hole, "reagent_name") == QStringLiteral("none")) continue;
            auto &areaSlots = area == QStringLiteral("s_area") ? sampleSlots : reagentSlots;
            const QString name = text(hole, "reagent_name");
            if (firstPriorityChosen.contains(name)) continue;
            QPair<QString, QString> position;
            if (!slot(hole, &position, message)) return false;
            if (hole.value(QStringLiteral("priority")).toInt() == 1) firstPriorityChosen.insert(name);
            areaSlots[name] = position;
        }
    }
    const QString diluent = task->project.originalBaseValues.value(QStringLiteral("IBS_name"));
    if (!diluent.isEmpty() && diluent != QStringLiteral("none") && !sampleSlots.contains(diluent))
        return fail(message, QStringLiteral("原样本区未放置所选稀释液：") + diluent);
    task->batchGroup = selectedGroup;
    task->reagentSlots = chosen;
    task->sampleAreaSlots = sampleSlots;
    task->reagentAreaSlots = reagentSlots;
    *remaining = selectedRemaining;
    return true;
}

bool OriginalReagentRepository::auxiliaryVolumesReady(const OriginalReagentState &state,
                                                     const QList<OriginalTasks::Task> &tasks, QString *message) const
{
    QMap<QString, double> available, required;
    for (const auto &hole : state.holes)
        if (eligible(state, hole))
        {
            double amount = 0;
            if (!number(hole.value(QStringLiteral("used_ul")), &amount)) return fail(message, QStringLiteral("原试剂可用量无效。"));
            available[text(hole,"area") + '/' + text(hole,"reagent_name")] += amount;
        }
    const auto add = [&required, message](const QString &area, const QString &name, const QString &amount, const QString &frequency)
    {
        if (name.isEmpty() || name == QStringLiteral("none")) return true;
        double volume = 0, times = 0;
        // 原未填写的清洗量/次数按0；非空非法文本不能默认为0。
        if (!number(amount.isEmpty() ? QVariant(QStringLiteral("0")) : QVariant(amount), &volume) ||
            !number(frequency.isEmpty() ? QVariant(QStringLiteral("0")) : QVariant(frequency), &times))
            return fail(message, QStringLiteral("原稀释/清洗参数不是非负数：") + name);
        required[area + '/' + name] += volume * times;
        return true;
    };
    for (const auto &task : tasks)
    {
        for (const auto &sample : task.project.advanced.samples)
            if (sample.value(QStringLiteral("sample_rate")) == task.rate)
            {
                const QString name = task.project.originalBaseValues.value(QStringLiteral("IBS_name"));
                if (!add(QStringLiteral("s_area"), name, sample.value(QStringLiteral("IBS_amount_ul")), QStringLiteral("1")) ||
                    !add(QStringLiteral("s_area"), name, sample.value(QStringLiteral("second_IBS_amount_ul")), QStringLiteral("1"))) return false;
                for (const auto &prefix : {QStringLiteral("before_clean"),QStringLiteral("after_clean")})
                    if (!add(QStringLiteral("s_area"), sample.value(prefix + QStringLiteral("_name")),
                             sample.value(prefix + QStringLiteral("_amount_ul")), sample.value(prefix + QStringLiteral("_frequency")))) return false;
            }
        for (const auto &step : task.project.advanced.reagents)
            for (const auto &prefix : {QStringLiteral("before_clean"),QStringLiteral("after_clean")})
                if (!add(QStringLiteral("r_area"), step.value(prefix + QStringLiteral("_name")),
                         step.value(prefix + QStringLiteral("_amount_ul")), step.value(prefix + QStringLiteral("_frequency")))) return false;
    }
    for (auto i = required.cbegin(); i != required.cend(); ++i)
        if (i.value() > available.value(i.key()))
            return fail(message, QStringLiteral("%1需要%2µL，当前原库记录只有%3µL，未下发任务。")
                .arg(i.key()).arg(i.value()).arg(available.value(i.key())));
    return true;
}

bool OriginalReagentRepository::reagentUpdatePayload(const OriginalReagentState &state, QByteArray *payload, QString *message) const
{
    if (!payload) return fail(message, QStringLiteral("试剂下发输出为空。"));
    QList<QStringList> rows;
    static const QStringList states{"r_none","r_normal","r_warning","r_useout","r_unknown","r_outdate"};
    for (const auto &hole : state.holes)
    {
        if (!eligible(state, hole)) continue;
        const auto *bottle = named(state.bottles, QStringLiteral("bottle_type_name"), text(hole, "bottle_type"));
        const auto *reagent = named(state.reagents, QStringLiteral("reagent_name"), text(hole, "reagent_name"));
        if (!bottle || !reagent) return fail(message, QStringLiteral("原孔位引用的瓶型或试剂资料不存在。"));
        if (!states.contains(text(hole,"state"))) return fail(message, QStringLiteral("原试剂孔位状态未识别，未发送负数状态。"));
        QPair<QString, QString> position;
        if (!slot(hole, &position, message)) return false;
        rows.append({position.first, text(hole,"area"), text(hole,"reagent_name"), text(hole,"reagent_type"),
            text(hole,"reagent_batch"), text(hole,"bottle_type"), text(hole,"amount_uL"), text(hole,"priority"), position.second,
            text(*bottle,"max_amount_ul"), text(*reagent,"out_ul"), text(*bottle,"bottom_diameter"), text(*bottle,"angle"),
            QString::number(states.indexOf(text(hole,"state"))), text(hole,"datetime"), text(hole,"out_date")});
    }
    QByteArray result = "Tu[" + QByteArray::number(rows.size()) + ']';
    for (const auto &row : rows) if (!appendWire(&result, row, message)) return false;
    *payload = result;
    return true;
}

bool OriginalReagentRepository::saveRemaining(const OriginalReagentState &observed,
                                            const QMap<qint64, double> &remaining, QString *message)
{
    const QString file = QStringLiteral("db/userSet/reagent_Setting.db");
    const QString version = observed.versions.value(file);
    if (observed.root != root_ || observed.settingTable != table_ || version.isEmpty() ||
        version != OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file), message))
        return fail(message, QStringLiteral("分配任务后原试剂孔位已变化，请重新核对，未覆盖剩余量。"));
    Database database;
    if (!database.open(root_, file, true, message) || !database.begin(message)) return false;
    if (version != OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file), message))
        return fail(message, QStringLiteral("保存前原试剂孔位已变化，未覆盖。"));
    for (const auto &row : observed.holes)
    {
        const qint64 id = row.value(QStringLiteral("id")).toLongLong();
        if (!remaining.contains(id)) continue;
        double previous = 0;
        const double value = remaining.value(id);
        if (!number(row.value(QStringLiteral("used_ul")), &previous) || !std::isfinite(value) || value < 0 || value > previous)
            return fail(message, QStringLiteral("任务预留试剂量无效，未提交。"));
        if (value == previous) continue;
        if (!database.execute(QStringLiteral("UPDATE \"%1\" SET used_ul=? WHERE id=? AND used_ul=?").arg(table_),
            {QString::number(value, 'g', 15), id, row.value(QStringLiteral("used_ul"))}, message, 1)) return false;
    }
    return database.commit(message);
}

bool OriginalReagentRepository::applyDeviceReply(const QByteArray &payload, QString *message)
{
    // 原pcReceiveReagentInfo每孔15字段，末尾可带isSingle；先校验整包，再改原试剂表。
    if (!payload.startsWith("Ur") || payload.size() > Sn100Protocol::kMaxMessageBytes)
        return fail(message, QStringLiteral("不是有效的原试剂信息返回。"));
    QStringList values;
    for (int cursor = 2; cursor < payload.size(); )
    {
        if (payload[cursor++] != '[') return fail(message, QStringLiteral("试剂返回字段缺少左括号。"));
        const int end = payload.indexOf(']', cursor);
        if (end < cursor || end - cursor > 4096 || values.size() >= 4096)
            return fail(message, QStringLiteral("试剂返回字段不完整或超过读取上限。"));
        const QByteArray field = payload.mid(cursor, end - cursor);
        for (unsigned char character : field)
            if (character < 32 || character == '[') return fail(message, QStringLiteral("试剂返回含非法控制字符。"));
        values.append(QString::fromLatin1(field));
        cursor = end + 1;
    }
    if (values.size() < 2) return fail(message, QStringLiteral("原试剂返回缺少孔位清单或完整/单孔标记，未清空原试剂盘。"));
    bool countOk = false;
    const int count = values.first().toInt(&countOk);
    if (!countOk || count < 0 || count > 128)
        return fail(message, QStringLiteral("原试剂返回孔数无效或超过128孔。"));
    const int expected = 1 + count * 15;
    if (values.size() != expected && values.size() != expected + 1)
        return fail(message, QStringLiteral("原试剂返回孔数与每孔15字段不符。"));
    const bool single = values.size() == expected + 1 && values.last() == QStringLiteral("1");
    if (values.size() == expected + 1 && values.last() != QStringLiteral("0") && values.last() != QStringLiteral("1"))
        return fail(message, QStringLiteral("原试剂返回完整/单孔标记无效。"));
    const QStringList columns{"hole_name","reagent_type","reagent_name","reagent_batch","bottle_type","formula_level","out_ul",
        "amount_uL","priority","state","datetime","out_date","warning_ul","max_ul","used_ul"};
    const QStringList states{"r_none","r_normal","r_warning","r_useout","r_unknown","r_outdate"};
    Rows incoming;
    QSet<QString> received;
    for (int i = 0; i < count; ++i)
    {
        Row row;
        for (int column = 0; column < columns.size(); ++column) row.insert(columns[column], values[1 + i * 15 + column]);
        const QString name = text(row,"hole_name");
        if (name.isEmpty() || received.contains(name)) return fail(message, QStringLiteral("原试剂返回孔名为空或重复。"));
        received.insert(name);
        bool stateOk = false;
        const int state = row.value(QStringLiteral("state")).toString().toInt(&stateOk);
        if (!stateOk || state < 0 || state >= states.size()) return fail(message, QStringLiteral("原试剂返回孔位状态未识别。"));
        row[QStringLiteral("state")] = states[state];
        for (const auto &key : {QStringLiteral("amount_uL"),QStringLiteral("out_ul"),QStringLiteral("priority"),QStringLiteral("warning_ul"),QStringLiteral("max_ul"),QStringLiteral("used_ul")})
        {
            double value = 0;
            if (key == QStringLiteral("used_ul") && row.value(key).toString().isEmpty()) continue;
            if (!number(row.value(key), &value)) return fail(message, QStringLiteral("原试剂返回的容量、优先级或液量无效。"));
        }
        incoming.append(row);
    }
    OriginalReagentState observed;
    if (!load(&observed, message)) return false;
    for (const auto &row : incoming)
        if (!named(observed.holes,QStringLiteral("hole_name"),text(row,"hole_name")))
            return fail(message, QStringLiteral("原试剂返回包含当前机型表中不存在的孔位，未自动建孔：") + text(row,"hole_name"));
    Database database;
    const QString file = QStringLiteral("db/userSet/reagent_Setting.db");
    if (!database.open(root_,file,true,message) || !database.begin(message)) return false;
    if (observed.versions.value(file) != OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file),message))
        return fail(message,QStringLiteral("试剂信息返回处理前原试剂盘已改变，未覆盖。"));
    if (!single && !database.execute(QStringLiteral("UPDATE \"%1\" SET reagent_name='none',reagent_type=' ',reagent_batch='',bottle_type='',amount_uL=0,used_ul='0',state='r_none'").arg(table_),{},message)) return false;
    for (const auto &row : incoming)
    {
        QStringList assignments;
        QVariantList arguments;
        for (const auto &column : columns)
        {
            if (column == QStringLiteral("hole_name") || (column == QStringLiteral("used_ul") && row.value(column).toString().isEmpty())) continue;
            assignments.append('"' + column + QStringLiteral("\"=?")); arguments.append(row.value(column));
        }
        arguments.append(row.value(QStringLiteral("hole_name")));
        if (!database.execute(QStringLiteral("UPDATE \"%1\" SET %2 WHERE hole_name=?").arg(table_,assignments.join(',')),arguments,message,1)) return false;
    }
    return database.commit(message);
}
