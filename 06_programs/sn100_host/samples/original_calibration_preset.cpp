#include "original_calibration_preset.h"
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace OriginalCalibrationPreset
{
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool fraction(const QString &text, double *value)
{
    const auto parts = text.split('/');
    bool a = false, b = false;
    if (parts.size() != 2) return false;
    const double numerator = parts[0].toDouble(&a), denominator = parts[1].toDouble(&b);
    if (!a || !b || !std::isfinite(numerator) || !std::isfinite(denominator) || numerator < 0 || denominator <= 0) return false;
    *value = numerator / denominator;
    return std::isfinite(*value);
}
QString defaultRate(const ProjectRecord &project)
{
    for (const auto &rate : project.sampleRates) if (rate.defaultOption) return rate.sampleRate;
    return {};
}
}
bool available(const ProjectRecord &project)
{
    for (const auto &result : project.results)
        if (result.type == QStringLiteral("Calibration")) return project.enabled && result.used;
    return false;
}
bool defaults(const ProjectRecord &project, int firstHole, int holeCount,
              SampleCalibrationPreset *preset, QString *message)
{
    if (!preset || !available(project) || firstHole < 1 || firstHole > holeCount)
        return fail(message, QStringLiteral("项目未启用校准结果，或当前孔位不存在。"));
    SampleCalibrationPreset value;
    value.projectId = project.id;
    value.groupToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    value.autoDilution = project.originalBaseValues.value(QStringLiteral("calibration_mode")) == QStringLiteral("自动稀释");
    bool repeatsOk = false, pointsOk = false;
    value.repeatCount = project.originalBaseValues.value(QStringLiteral("repeat_number")).toInt(&repeatsOk);
    if (!repeatsOk || value.repeatCount < 1 || value.repeatCount > 99)
        return fail(message, QStringLiteral("原项目的校准重复次数无效，未自动改为1。"));
    if (value.autoDilution)
    {
        for (const auto &row : project.advanced.samples)
            if (row.value(QStringLiteral("calibration_option")).toInt() > 0)
            {
                double ratio = 0;
                if (!fraction(row.value(QStringLiteral("sample_rate")), &ratio))
                    return fail(message, QStringLiteral("原校准稀释比例不是有效分数，请核对项目资料。"));
                value.rates.append(row.value(QStringLiteral("sample_rate")));
            }
        std::stable_sort(value.rates.begin(), value.rates.end(), [](const QString &a, const QString &b)
        { double x = 0, y = 0; fraction(a, &x); fraction(b, &y); return x < y; });
        value.pointCount = value.rates.size();
        for (int i = 0; i < value.pointCount; ++i) value.holes.append(firstHole);
    }
    else
    {
        value.pointCount = project.originalBaseValues.value(QStringLiteral("point_number")).toInt(&pointsOk);
        if (!pointsOk || value.pointCount < 1 || value.pointCount > holeCount || firstHole + value.pointCount - 1 > holeCount)
            return fail(message, QStringLiteral("原手动校准点数超出可用孔位，未截断校准点。"));
        const QString rate = defaultRate(project);
        if (rate.isEmpty()) return fail(message, QStringLiteral("原项目没有默认稀释比例，未猜造校准方案。"));
        for (int i = 0; i < value.pointCount; ++i)
        { value.holes.append(firstHole + i); value.rates.append(rate); }
    }
    if (value.pointCount < 1 || value.pointCount > 128)
        return fail(message, QStringLiteral("原校准没有有效点，或点数超过128，未生成空任务。"));
    *preset = value;
    return true;
}
bool setConcentrations(const ProjectRecord &project, const ProjectUnits &units,
                       const QStringList &displayValues, SampleCalibrationPreset *preset, QString *message)
{
    if (!preset || displayValues.size() != (preset->autoDilution ? 1 : preset->pointCount))
        return fail(message, QStringLiteral("校准品浓度输入数与原点数不符。"));
    QString unitName;
    for (const auto &result : project.results) if (result.type == QStringLiteral("Calibration")) unitName = result.unit;
    double unitRatio = 1;
    for (const auto &unit : units) if (unit.name == unitName) unitRatio = unit.ratio;
    if (!std::isfinite(unitRatio) || unitRatio <= 0) return fail(message, QStringLiteral("原校准浓度单位倍率无效。"));
    QStringList concentrations;
    for (int index = 0; index < preset->pointCount; ++index)
    {
        bool ok = false;
        const QString input = displayValues[preset->autoDilution ? 0 : index];
        const double display = input.toDouble(&ok);
        double ratio = 1;
        if (!ok || input.isEmpty() || !std::isfinite(display) || display < 0 ||
            (preset->autoDilution && !fraction(preset->rates.value(index), &ratio)))
            return fail(message, QStringLiteral("校准品浓度为空、不是有限非负数，或原稀释比例无效。"));
        const double concentration = display * ratio / unitRatio;
        if (!std::isfinite(concentration)) return fail(message, QStringLiteral("校准浓度换算超出数值范围。"));
        // 原on_btnSave_clicked使用QString::number(double)，保留原默认精度。
        concentrations.append(QString::number(concentration));
    }
    preset->concentrations = concentrations;
    preset->baselineConcentration = preset->autoDilution ? displayValues.first() : QString();
    return true;
}
bool validate(const ProjectRecord &project, const SampleCalibrationPreset &preset,
              int holeCount, QString *message)
{
    static const QRegularExpression batch(QStringLiteral("^[A-Za-z0-9]{1,16}$"));
    if (!available(project) || preset.projectId != project.id || QUuid(preset.groupToken).isNull() ||
        !batch.match(preset.calibratorBatch).hasMatch() || preset.reagentBatchGroup.isEmpty())
        return fail(message, QStringLiteral("校准项目、草稿身份、校准品批号或试剂组无效。"));
    SampleCalibrationPreset expected;
    if (!defaults(project, preset.holes.value(0), holeCount, &expected, message)) return false;
    if (preset.autoDilution != expected.autoDilution || preset.pointCount != expected.pointCount ||
        preset.repeatCount != expected.repeatCount || preset.rates != expected.rates || preset.holes != expected.holes ||
        preset.concentrations.size() != preset.pointCount)
        return fail(message, QStringLiteral("校准模式、点数、孔位、重复次数或稀释比例与当前原项目不符，请重设。"));
    for (const auto &text : preset.concentrations)
    {
        bool ok = false; const double number = text.toDouble(&ok);
        if (text.isEmpty() || !ok || !std::isfinite(number) || number < 0)
            return fail(message, QStringLiteral("原存储单位的校准浓度无效，未生成任务。"));
    }
    if (project.originalBaseValues.value(QStringLiteral("is_related")).toInt() == 0 && preset.reagentBatchGroup != QStringLiteral("Default"))
        return fail(message, QStringLiteral("原项目未关联试剂组，只能使用Default。"));
    return true;
}
bool apply(const ProjectRecord &project, const SampleCalibrationPreset &preset, SampleRack *rack, QString *message)
{
    if (!rack || !validate(project, preset, rack->holes.size(), message)) return false;
    for (const auto &hole : rack->holes)
        if (preset.holes.contains(hole.number) && (!hole.tasks.isEmpty() || !hole.revise))
            return fail(message, QStringLiteral("目标孔位已有任务或已下发，请先删除并核对状态；整组草稿未改动。"));
    for (const auto &hole : rack->holes)
        if (hole.sampleType == 1 && hole.calibration.projectId == project.id)
            return fail(message, QStringLiteral("本架该项目已有校准任务，请先删除整组，不能混用两个校准品批号。"));
    SampleRack candidate = *rack;
    for (int point = 0; point < preset.pointCount; ++point)
    {
        int index = -1;
        for (int i = 0; i < candidate.holes.size(); ++i) if (candidate.holes[i].number == preset.holes[point]) index = i;
        if (index < 0) return fail(message, QStringLiteral("校准目标孔位已不存在，整组草稿未改动。"));
        auto &hole = candidate.holes[index];
        hole.sampleType = 1;
        hole.sampleId = QStringLiteral("c-") + preset.calibratorBatch;
        hole.quality = SampleQcIdentity();
        hole.calibration = preset;
        SampleTask task;
        task.projectId = project.id; task.rate = preset.rates[point]; task.repeats = preset.repeatCount;
        task.calibrationPoint = point + 1; task.calibrationConcentration = preset.concentrations[point];
        hole.tasks.append(task);
    }
    *rack = candidate;
    return true;
}
}
