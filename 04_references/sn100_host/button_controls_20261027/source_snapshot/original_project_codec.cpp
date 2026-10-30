#include "original_project_codec.h"
#include "protocol/frame_codec.h"
#include <QRegularExpression>
#include <QStringList>

namespace OriginalProjectWire
{
namespace
{
// 依据原commandsender::getProjectInformationCommand，注释掉的字段不占位。
const QStringList sendBase{
    "change_time","project_en_name","is_used","min_test_time","max_test_time","is_related","stopTime",
    "second_light","first_light","repeat_number_max","repeat_number","point_number","x_change","y_change",
    "IBS_name","sample_warmup_length","current_reagent_group","sample_change_clean","fixed","xishu_a",
    "xishu_b","last_cal_para","auto_apply","project_lis_code"};
const QStringList sendSample{
    "xuhao","sample_rate","IBS_amount_ul","sample_amount_ul","blender_type","blender_time_sec","needle_select",
    "mix_amount_ul","second_IBS_amount_ul","action_amount_ul","before_clean_amount_ul","sample_option",
    "calibration_option","sample_default_option","sample_add_mode","before_clean_frequency","after_clean_frequency","before_clean_name"};
const QStringList sendReagent{
    "xuhao","reagent_name","reagent_warmup_length","before_clean_frequency","after_clean_frequency",
    "reagent_amount_ul","blender_type","blender_time_sec","needle_select","before_clean_amount_ul","before_clean_name"};
// 依据原frmSystemMaterial::slotReceiveProjectInfo；不能用发送字段表反向解析。
const QStringList receiveBase{
    "change_time","project_en_name","project_id","is_used","min_test_time","max_test_time","is_related","stopTime",
    "second_light","xuhao","collect_rate_ms","first_light","repeat_number_max","repeat_number","point_number","x_change","y_change",
    "IBS_name","sample_warmup_length","current_reagent_group","sample_change_clean","fixed","xishu_a","xishu_b","last_cal_para","auto_apply","project_lis_code"};
const QStringList receiveSample{
    "xuhao","sample_rate","sample_add_length","IBS_amount_ul","sample_amount_ul","blender_type","blender_time_sec","suck_release_count",
    "mix_amount_ul","second_IBS_amount_ul","action_amount_ul","before_clean_amount_ul","after_clean_amount_ul","sample_option","calibration_option",
    "sample_default_option","sample_add_mode","before_clean_frequency","after_clean_frequency","before_clean_name","after_clean_name"};
const QStringList receiveReagent{
    "xuhao","reagent_add_length","reagent_name","reagent_warmup_length","before_clean_frequency","after_clean_frequency","reagent_amount_ul",
    "blender_type","blender_time_sec","needle_select","before_clean_amount_ul","after_clean_amount_ul","before_clean_name","after_clean_name"};
const QStringList resultKeys{
    "result_id","result_type","result_name","decimal_point","unit_name","test_low_range","test_up_range","normal_low_range",
    "normal_up_range","is_used","main_result","display","lis_code"};

bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool fieldValid(const QString &value)
{
    if (value.size() > 4096 || QString::fromLatin1(value.toLatin1()) != value) return false;
    for (const QChar c : value)
        if (c.unicode() < 32 || c == '[' || c == ']') return false;
    return true;
}
void appendFields(QStringList *values, const Fields &row, const QStringList &keys, bool reagent)
{
    for (const auto &key : keys)
    {
        const QString value = row.value(key);
        // 原发送端仅对试剂针名转换；样本步骤的needle_select原值直接发送。
        values->append(reagent && key == "needle_select" ? value == QStringLiteral("左试剂针") ? "1" : "2" : value);
    }
}
bool readFields(const QStringList &values, int *cursor, const QStringList &keys, Fields *row, QString *message)
{
    if (*cursor + keys.size() > values.size()) return fail(message, QStringLiteral("仪器项目字段不完整，未保存任何项目参数。"));
    for (const auto &key : keys) row->insert(key,values[(*cursor)++]);
    return true;
}
bool readRows(const QStringList &values, int *cursor, const QStringList &keys, QList<Fields> *rows, QString *message)
{
    if (*cursor >= values.size()) return fail(message, QStringLiteral("仪器项目缺少步骤数量。"));
    static const QRegularExpression number(QStringLiteral("^[0-9]+$"));
    const QString countText = values[(*cursor)++];
    bool ok = false;
    const int count = countText.toInt(&ok);
    if (!ok || !number.match(countText).hasMatch() || count < 0 || count > 256 || count > (values.size()-*cursor)/keys.size())
        return fail(message, QStringLiteral("仪器项目步骤数量非法或字段不足，未截断资料。"));
    for (int i=0;i<count;++i)
    {
        Fields row;
        if (!readFields(values,cursor,keys,&row,message)) return false;
        rows->append(row);
    }
    return true;
}
}

bool updatePayload(const Project &project, QByteArray *payload, QString *message)
{
    if (!payload || project.base.value("project_en_name").isEmpty() || project.samples.size()>256 || project.reagents.size()>256 || project.results.size()>256)
        return fail(message, QStringLiteral("待同步项目名称为空或步骤数量超过256，未生成项目命令。"));
    QStringList values;
    appendFields(&values,project.base,sendBase,false);
    values.append(QString::number(project.samples.size()));
    for (const auto &row : project.samples) appendFields(&values,row,sendSample,false);
    values.append(QString::number(project.reagents.size()));
    for (const auto &row : project.reagents) appendFields(&values,row,sendReagent,true);
    values.append(QString::number(project.results.size()));
    for (const auto &row : project.results) appendFields(&values,row,resultKeys,false);
    QByteArray result("Up");
    for (const auto &value : values)
    {
        if (!fieldValid(value)) return fail(message,QStringLiteral("项目字段含括号、控制字符或原Latin1协议不能表示的文字，未发送。"));
        result += '[' + value.toLatin1() + ']';
        if (result.size()>Sn100Protocol::kMaxMessageBytes) return fail(message,QStringLiteral("项目命令超过128000字节，未截断。"));
    }
    *payload = result;
    return true;
}

bool parseDeviceProject(const QByteArray &payload, Project *project, QString *message)
{
    if (!project || !payload.startsWith("UP") || payload.size()>Sn100Protocol::kMaxMessageBytes)
        return fail(message,QStringLiteral("仪器项目返回不是有效UP消息。"));
    QStringList values;
    int cursor=2;
    while (cursor<payload.size())
    {
        if (payload[cursor++]!='[') return fail(message,QStringLiteral("仪器项目字段缺少左括号。"));
        const int end=payload.indexOf(']',cursor);
        if (end<cursor || end-cursor>4096 || values.size()>=16384) return fail(message,QStringLiteral("仪器项目字段不完整或超过上限。"));
        const QString value=QString::fromLatin1(payload.mid(cursor,end-cursor));
        if (!fieldValid(value)) return fail(message,QStringLiteral("仪器项目含非法字段。"));
        values.append(value); cursor=end+1;
    }
    cursor=0;
    Project parsed;
    if (!readFields(values,&cursor,receiveBase,&parsed.base,message) || parsed.base.value("project_en_name").isEmpty() ||
        !readRows(values,&cursor,receiveSample,&parsed.samples,message) || !readRows(values,&cursor,receiveReagent,&parsed.reagents,message) ||
        !readRows(values,&cursor,resultKeys,&parsed.results,message))
        return fail(message,message && !message->isEmpty() ? *message : QStringLiteral("仪器项目名称或参数不完整。"));
    if (cursor+1!=values.size()) return fail(message,QStringLiteral("仪器项目缺少凝固终点模式或包含多余字段，未保存。"));
    parsed.coagulationEndMode=values[cursor];
    *project=parsed;
    return true;
}
}
