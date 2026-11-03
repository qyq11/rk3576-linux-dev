#include "reagents/original_reagent_repository.h"
#include "original_files/original_ini.h"
#include "projects/original_project_repository.h"
#include "protocol/frame_codec.h"
#include <QDate>
#include <QDir>
#include <QLockFile>
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

const QStringList &deviceReplyColumns()
{
    static const QStringList columns{"hole_name","reagent_type","reagent_name","reagent_batch","bottle_type","formula_level","out_ul",
        "amount_uL","priority","state","datetime","out_date","warning_ul","max_ul","used_ul"};
    return columns;
}

bool readReagentFields(const QByteArray &payload, const QByteArray &command, QStringList *values, QString *message)
{
    if (!payload.startsWith(command) || payload.size() > Sn100Protocol::kMaxMessageBytes)
        return fail(message, QStringLiteral("不是有效的原试剂信息返回。"));
    values->clear();
    for (int cursor = command.size(); cursor < payload.size(); )
    {
        if (payload[cursor++] != '[') return fail(message, QStringLiteral("试剂返回字段缺少左括号。"));
        const int end = payload.indexOf(']', cursor);
        if (end < cursor || end - cursor > 4096 || values->size() >= 4096)
            return fail(message, QStringLiteral("试剂返回字段不完整或超过读取上限。"));
        const QByteArray field = payload.mid(cursor, end - cursor);
        for (unsigned char character : field)
            if (character < 32 || character == '[') return fail(message, QStringLiteral("试剂返回含非法控制字符。"));
        values->append(QString::fromLatin1(field));
        cursor = end + 1;
    }
    return true;
}

// 保存回读和核对优先级共用整包校验，不能只取孔名就跳过其他孔的错误字段。
bool readDeviceReply(const QByteArray &payload, Rows *rows, bool *single, QString *message)
{
    QStringList values;
    if (!readReagentFields(payload, QByteArrayLiteral("Ur"), &values, message)) return false;
    if (values.size() < 2) return fail(message, QStringLiteral("原试剂返回缺少孔位清单或完整/单孔标记，未清空原试剂盘。"));
    bool countOk = false;
    const int count = values.first().toInt(&countOk);
    if (!countOk || count < 0 || count > 128)
        return fail(message, QStringLiteral("原试剂返回孔数无效或超过128孔。"));
    const int expected = 1 + count * 15;
    if (values.size() != expected && values.size() != expected + 1)
        return fail(message, QStringLiteral("原试剂返回孔数与每孔15字段不符。"));
    if (values.size() == expected + 1 && values.last() != QStringLiteral("0") && values.last() != QStringLiteral("1"))
        return fail(message, QStringLiteral("原试剂返回完整/单孔标记无效。"));
    const auto &columns = deviceReplyColumns();
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
    *rows = incoming;
    *single = values.size() == expected + 1 && values.last() == QStringLiteral("1");
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
            // 原库可能保留已扣成负数的剩余量；读取页面时保留原值，不改库或阻止整个程序启动。
            // 原Windows在used_ul为空时读取amount_uL。分配任务、清洗液检查和扣量仍分别核对可用量。
            const QString used = text(hole, "used_ul");
            bool valid = false;
            const double remaining = (used.isEmpty() ? text(hole, "amount_uL") : used).toDouble(&valid);
            if (!valid || !std::isfinite(remaining))
                return fail(message, QStringLiteral("原试剂剩余量不是有效数字：") + text(hole, "hole_name"));
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

bool OriginalReagentRepository::singleReagentUpdatePayload(const OriginalReagentState &state,
    const QString &holeName, bool updateAmount, QByteArray *payload, QString *message) const
{
    if (!payload || state.root!=root_ || state.settingTable!=table_)
        return fail(message,QStringLiteral("原试剂下发来源或输出无效。"));
    const Row *selected=nullptr;
    for (const auto &hole : state.holes)
        if (text(hole,"hole_name").compare(holeName,Qt::CaseInsensitive)==0)
        { if (selected) return fail(message,QStringLiteral("原试剂孔位重名，未猜测下发目标。")); selected=&hole; }
    if (!selected) return fail(message,QStringLiteral("原试剂孔位不存在，未生成下发。"));
    const auto &hole=*selected;
    static const QStringList states{"r_none","r_normal","r_warning","r_useout","r_unknown","r_outdate"};
    const int status=states.indexOf(text(hole,"state"));
    if (status<0) return fail(message,QStringLiteral("原试剂孔位状态未识别，未发送负数状态。"));
    auto numeric=[&](const char *key,QString *value)
    {
        double numberValue=0;
        if (!number(hole.value(QString::fromLatin1(key)),&numberValue)) return false;
        *value=QString::number(numberValue); // 原getSingleReagentUpdateCommand使用Qt默认数值精度。
        return true;
    };
    QString amount,priority,warning,out,max;
    if (!numeric("amount_uL",&amount) || !numeric("priority",&priority) || !numeric("warning_ul",&warning) ||
        !numeric("out_ul",&out) || !numeric("max_ul",&max))
        return fail(message,QStringLiteral("原试剂孔位容量、优先级或阈值无效，未下发。"));
    // 原Us的第14项确实发送max_ul，不能按字段名自行改成used_ul；第15项决定是否更新。
    QByteArray result="Us";
    if (!appendWire(&result,{text(hole,"hole_name"),text(hole,"reagent_type"),text(hole,"reagent_name"),
        text(hole,"reagent_batch"),text(hole,"bottle_type"),amount,priority,QString::number(status),
        text(hole,"datetime"),text(hole,"out_date"),warning,out,max,max,updateAmount ? "1" : "0"},message)) return false;
    *payload=result;
    return true;
}

bool OriginalReagentRepository::singleReagentSettingPayload(const OriginalReagentState &state,
    const QString &holeName, bool updateAmount, QByteArray *payload, QString *message) const
{
    // MaskDlgReagentSetting::on_btnSave_clicked在新协议分支对保存和清孔都setPriority(99)。
    // 仅改待发送快照；清孔原库仍是0，仪器返回Ur后再按真实回读更新。
    OriginalReagentState sending=state;
    for (auto &hole : sending.holes)
        if (text(hole,"hole_name").compare(holeName,Qt::CaseInsensitive)==0)
            hole.insert(QStringLiteral("priority"),QStringLiteral("99"));
    return singleReagentUpdatePayload(sending,holeName,updateAmount,payload,message);
}

bool OriginalReagentRepository::singleReagentPriorityPayload(const OriginalReagentState &state,
    const QString &holeName, QByteArray *payload, QString *message) const
{
    OriginalReagentState sending=state;
    for (auto &hole : sending.holes)
        if (text(hole,"hole_name").compare(holeName,Qt::CaseInsensitive)==0)
            hole.insert(QStringLiteral("priority"),QStringLiteral("1"));
    return singleReagentUpdatePayload(sending,holeName,false,payload,message);
}

bool OriginalReagentRepository::priorityReplyMatches(const QByteArray &payload,
    const QByteArray &sentPayload, QString *message) const
{
    Rows incoming;
    bool single=false;
    if (!readDeviceReply(payload,&incoming,&single,message)) return false;
    if (!single || incoming.size()!=1)
        return fail(message,QStringLiteral("优先使用仅匹配明确单孔标记1且恰含一个孔位的Ur，整盘或未知范围回包未确认本次动作。"));
    QStringList sent;
    if (!readReagentFields(sentPayload,QByteArrayLiteral("Us"),&sent,message)) return false;
    if (sent.size()!=15 || sent[0].isEmpty() || sent[14]!=QStringLiteral("0"))
        return fail(message,QStringLiteral("优先使用发送快照不是完整15字段Us或末项不是0，未确认回读。"));
    for (int index : {5,6,10,11,12,13})
    {
        double value=0;
        if (!number(sent[index],&value))
            return fail(message,QStringLiteral("优先使用发送快照的液量、优先级或阈值无效。"));
    }
    bool stateOk=false;
    const int sentState=sent[7].toInt(&stateOk);
    static const QStringList states{"r_none","r_normal","r_warning","r_useout","r_unknown","r_outdate"};
    if (!stateOk || sentState<0 || sentState>=states.size() || sent[6].toDouble()!=1 ||
        sent[12].toDouble()!=sent[13].toDouble())
        return fail(message,QStringLiteral("优先使用快照的状态、优先级1或原max字段不符，未确认回读。"));
    const Row *received=named(incoming,QStringLiteral("hole_name"),sent[0]);
    if (!received)
        return fail(message,QStringLiteral("合法试剂回读未包含本次优先使用孔位%1，未确认本次动作。").arg(sent[0]));
    const QList<QPair<int,QString>> identityFields{{0,QStringLiteral("hole_name")},{1,QStringLiteral("reagent_type")},
        {2,QStringLiteral("reagent_name")},{3,QStringLiteral("reagent_batch")},{4,QStringLiteral("bottle_type")},
        {8,QStringLiteral("datetime")},{9,QStringLiteral("out_date")}};
    for (const auto &field : identityFields)
        if (received->value(field.second).toString()!=sent[field.first])
            return fail(message,QStringLiteral("优先使用回读字段%1与本次发送快照不符，未确认完成。").arg(field.second));
    if (received->value(QStringLiteral("priority")).toString().toDouble()!=1 ||
        received->value(QStringLiteral("state")).toString()!=states[sentState])
        return fail(message,QStringLiteral("优先使用回读的优先级或孔位状态与本次发送不符，未确认完成。"));
    const QList<QPair<int,QString>> configurationFields{{10,QStringLiteral("warning_ul")},
        {11,QStringLiteral("out_ul")},{12,QStringLiteral("max_ul")}};
    for (const auto &field : configurationFields)
        if (received->value(field.second).toString().toDouble()!=sent[field.first].toDouble())
            return fail(message,QStringLiteral("优先使用回读字段%1与本次发送快照不符，未确认完成。").arg(field.second));
    // isUpdate=0不要求液量按Us第14项max重置；formula_level也没有在Us中发送。
    // 此处仅核报文内容，不据此认定旧连接/迟到回复属于本次动作，也不写入原库。
    return true;
}

bool OriginalReagentRepository::applyPriorityReply(const QByteArray &payload, const QByteArray &sentPayload,
    const QMap<QString,QString> &expectedVersions, const std::function<bool()> &stillCurrent,
    bool *committed, QString *savedFingerprint, QString *message)
{
    if (committed) *committed=false;
    if (savedFingerprint) savedFingerprint->clear();
    if (!committed || !savedFingerprint)
        return fail(message,QStringLiteral("优先级回读提交结果或指纹输出为空，未写入原库。"));
    if (root_.isEmpty() || (table_!=QStringLiteral("reagent_setting") && table_!=QStringLiteral("reagent_setting_6600")))
        return fail(message,QStringLiteral("原试剂资料未初始化，未保存优先级回读。"));
    if (!priorityReplyMatches(payload,sentPayload,message)) return false;
    Rows incoming;
    bool single=false;
    if (!readDeviceReply(payload,&incoming,&single,message)) return false;
    // 无尾标/尾标0在原协议中会清整盘；本动作只接明确的目标单孔，不改尾标或截取满盘。
    if (!single || incoming.size()!=1)
        return fail(message,QStringLiteral("优先使用仅接受明确单孔标记1且恰含目标孔的Ur，未改写整盘或其他孔。"));

    const QString settingFile=QStringLiteral("db/userSet/reagent_Setting.db");
    const QStringList files{QStringLiteral("db/userSet/reagent_Information.db"),
        QStringLiteral("db/systemInfo/reagent_Bottle_Type.db"),settingFile};
    if (expectedVersions.size()!=files.size())
        return fail(message,QStringLiteral("优先使用必须固定发送时的试剂、瓶型及孔位三份文件版本，未保存回读。"));
    for (const auto &file : files)
        if (expectedVersions.value(file).isEmpty())
            return fail(message,QStringLiteral("优先使用缺少发送时文件版本：")+file);
    const auto permitted=[&]()
    {
        if (!stillCurrent || !stillCurrent())
            return fail(message,QStringLiteral("优先使用的连接、请求或等待许可已失效，回读未提交。"));
        return true;
    };
    const auto versionsMatch=[&](bool includeSetting)
    {
        if (!permitted()) return false;
        for (const auto &file : files)
        {
            if (!includeSetting && file==settingFile) continue;
            const QString actual=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(file),message);
            if (actual.isEmpty()) return false;
            if (actual!=expectedVersions.value(file))
                return fail(message,QStringLiteral("优先使用请求后原资料已变化，未用迟到回读覆盖当前文件：")+file);
        }
        return permitted();
    };
    if (!permitted()) return false;
    // 与原编辑器采用相同顺序，避免一个入口先取试剂锁、另一个先取项目锁。
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0))
        return fail(message,QStringLiteral("原项目或试剂正在保存，优先级回读未覆盖当前编辑。"));
    if (!versionsMatch(true)) return false;

    Database database;
    if (!database.open(root_,settingFile,true,message) || !database.begin(message)) return false;
    const auto rollback=[&]() { database.rollback(); return false; };
    // BEGIN IMMEDIATE取得孔位库写保留锁后再核请求时三份版本，防止检查与入事务之间被改写。
    if (!versionsMatch(true)) return rollback();
    OriginalReagentState observed;
    if (!load(&observed,message)) return rollback();
    for (const auto &file : files)
        if (observed.versions.value(file)!=expectedVersions.value(file))
        {
            fail(message,QStringLiteral("读取优先级回读目标期间原资料已变化，未保存：")+file);
            return rollback();
        }
    const Row &row=incoming.first();
    const QString holeName=text(row,"hole_name");
    if (!named(observed.holes,QStringLiteral("hole_name"),holeName))
    {
        fail(message,QStringLiteral("优先级回读的孔位不在当前机型原表中，未创建或猜测孔位：")+holeName);
        return rollback();
    }
    if (!versionsMatch(true)) return rollback();
    QStringList assignments;
    QVariantList arguments;
    for (const auto &column : deviceReplyColumns())
    {
        if (column==QStringLiteral("hole_name") ||
            (column==QStringLiteral("used_ul") && row.value(column).toString().isEmpty())) continue;
        assignments.append('"'+column+QStringLiteral("\"=?"));
        arguments.append(row.value(column));
    }
    arguments.append(holeName);
    if (!database.execute(QStringLiteral("UPDATE \"%1\" SET %2 WHERE hole_name=?").arg(table_,assignments.join(',')),
                          arguments,message,1)) return rollback();
    // 孔位库已在本事务内修改，不能把自己的未提交页当外部冲突；写锁仍阻止另一写事务。
    // 另外两份资料未被本方法修改，提交前继续核其发送时版本及可撤销许可。
    if (!versionsMatch(false) || !permitted()) return rollback();
    if (!database.commit(message)) return rollback();
    *committed=true;
    database.db.close();
    QString fingerprintError;
    *savedFingerprint=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(settingFile),&fingerprintError);
    if (savedFingerprint->isEmpty())
        return fail(message,QStringLiteral("优先级回读已经提交，但无法核对保存后的文件指纹，请核对当前资料：")+fingerprintError);
    if (!stillCurrent || !stillCurrent())
        return fail(message,QStringLiteral("优先级回读已经提交，但提交期间连接、请求或等待许可发生变化；结果待核对，未报告操作完成。"));
    if (message) *message=QStringLiteral("优先级回读已提交并取得文件指纹，仍须页面重新读取后确认。");
    return true;
}

bool OriginalReagentRepository::detectionPayload(const OriginalReagentState &state,
    const QStringList &holeNames, const QMap<QString,int> &needleSelection, QByteArray *payload, QString *message) const
{
    if (!payload || root_.isEmpty() || table_.isEmpty() || state.root!=root_ || state.settingTable!=table_)
        return fail(message,QStringLiteral("试剂探测资料来源或输出无效，未准备命令。"));
    if (holeNames.isEmpty() || holeNames.size()>128)
        return fail(message,QStringLiteral("试剂探测孔位为空或超过128孔，未截断。"));
    QByteArray result="Mg["+QByteArray::number(holeNames.size())+']';
    for (const auto &holeName : holeNames)
    {
        const Row *selected=nullptr;
        for (const auto &hole : state.holes)
            if (text(hole,"hole_name").compare(holeName,Qt::CaseInsensitive)==0)
            {
                if (selected) return fail(message,QStringLiteral("试剂探测孔位重名，未猜测目标：")+holeName);
                selected=&hole;
            }
        if (!selected) return fail(message,QStringLiteral("试剂探测孔位不存在：")+holeName);
        const auto &hole=*selected;
        const QString area=text(hole,"area"), reagent=text(hole,"reagent_name");
        if ((area!=QStringLiteral("r_area") && area!=QStringLiteral("s_area")) || reagent.isEmpty() || reagent==QStringLiteral("none"))
            return fail(message,QStringLiteral("试剂探测孔位区域或试剂名称未配置：")+holeName);
        // 原编码器按瓶名采用第一项，不用另一区域的后续同名项替代它。
        const Row *bottle=named(state.bottles,QStringLiteral("bottle_type_name"),text(hole,"bottle_type"));
        if (!bottle) return fail(message,QStringLiteral("试剂探测孔位缺少原瓶型：")+holeName);
        double diameter=0;
        bool angleOk=false;
        const double angle=text(*bottle,"angle").toDouble(&angleOk);
        if (!number(bottle->value(QStringLiteral("bottom_diameter")),&diameter) || !angleOk || !std::isfinite(angle))
            return fail(message,QStringLiteral("试剂探测瓶型的底径或角度不是有效数值：")+holeName);
        int needle=needleSelection.value(reagent,0);
        if (needle==0) needle=area==QStringLiteral("r_area") ? 1 : 3;
        if (needle<1 || needle>3) return fail(message,QStringLiteral("试剂探测针号不是1、2或3：")+holeName);
        QPair<QString,QString> position;
        // 原initPanel取split('_')[1]，与既有slot一致，不能误用数据库记录id。
        if (!slot(hole,&position,message)) return false;
        if (!appendWire(&result,{position.first,reagent,area,QString::number(needle),position.second,
            text(hole,"bottle_type"),text(*bottle,"bottom_diameter"),text(*bottle,"angle")},message)) return false;
    }
    *payload=result;
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
    Rows incoming;
    bool single = false;
    if (!readDeviceReply(payload, &incoming, &single, message)) return false;
    const auto &columns = deviceReplyColumns();
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
