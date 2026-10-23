#include "result_statistics.h"

#include <cmath>

namespace
{
QString missingValuesReason()
{
    return QStringLiteral("没有可参与统计的有限数值；原始任务及异常文本仍保留。");
}

void rejectGroup(HistoryStatisticGroup *group, const QString &reason)
{
    group->meanAvailable = false;
    group->sampleSdAvailable = false;
    group->cvAvailable = false;
    group->meanText.clear();
    group->cvText.clear();
    group->meanUnavailableReason = reason;
    group->cvUnavailableReason = reason;
}

bool sameConfiguration(const HistoryStatisticGroup &group, const HistoryResultValue &value)
{
    return group.label == value.label && group.unit == value.unit
        && group.decimals == value.decimals && group.factor == value.factor;
}

void calculateGroup(HistoryStatisticGroup *group)
{
    if (!group->configurationConsistent)
    {
        rejectGroup(group, QStringLiteral("同一项目/结果类型的名称、单位、精度或倍率不一致，不能合并统计。"));
        return;
    }
    if (group->project.isEmpty() || group->resultType < 1 || group->resultType > 6
        || group->decimals < -1 || group->decimals > 12
        || !std::isfinite(group->factor) || group->factor <= 0.0)
    {
        rejectGroup(group, QStringLiteral("项目统计配置无效；请核对结果类型、精度和正数倍率。"));
        return;
    }
    for (const auto &entry : group->entries)
    {
        bool converted = false;
        const double raw = entry.value.rawValue.toDouble(&converted);
        if (converted && std::isfinite(raw) && !std::isfinite(entry.scaledValue))
        {
            // A finite measurement that overflows during unit conversion is
            // an arithmetic failure, not an invalid repeat to silently omit.
            rejectGroup(group, QStringLiteral("单位换算超出数值范围，均值/CV不可用。"));
            return;
        }
    }
    if (group->validCount == 0)
    {
        rejectGroup(group, missingValuesReason());
        return;
    }

    double sum = 0.0;
    for (const auto &entry : group->entries)
    {
        if (!entry.numeric)
            continue;
        sum += entry.scaledValue;
        if (!std::isfinite(sum))
        {
            rejectGroup(group, QStringLiteral("求和超出数值范围，均值/CV不可用。"));
            return;
        }
    }
    group->mean = sum / group->validCount;
    if (!std::isfinite(group->mean))
    {
        rejectGroup(group, QStringLiteral("均值超出数值范围。"));
        return;
    }
    group->meanAvailable = true;
    if (group->decimals >= 0)
        group->meanText = QString::number(group->mean, 'f', group->decimals);
    else
        group->meanUnavailableReason = QStringLiteral("未提供项目显示精度；均值数值已计算，不猜测显示位数。");

    if (group->validCount < 2)
    {
        group->cvUnavailableReason = QStringLiteral("少于两个有效数值，样本标准差/CV不可用。");
        return;
    }
    double squaredDeviations = 0.0;
    for (const auto &entry : group->entries)
    {
        if (!entry.numeric)
            continue;
        const double difference = entry.scaledValue - group->mean;
        squaredDeviations += difference * difference;
        if (!std::isfinite(difference) || !std::isfinite(squaredDeviations))
        {
            group->cvUnavailableReason = QStringLiteral("偏差平方超出数值范围，标准差/CV不可用。");
            return;
        }
    }
    // 原报告页使用(n-1)样本标准差，不能替换成总体标准差或质控模块的CV比值。
    group->sampleSd = std::sqrt(squaredDeviations / (group->validCount - 1));
    if (!std::isfinite(group->sampleSd))
    {
        group->cvUnavailableReason = QStringLiteral("样本标准差超出数值范围，CV不可用。");
        return;
    }
    group->sampleSdAvailable = true;
    if (group->mean == 0.0)
    {
        group->cvUnavailableReason = QStringLiteral("均值为零，CV不可用；不将不可定义的比值显示为0。");
        return;
    }
    group->cvPercent = group->sampleSd / group->mean * 100.0;
    if (!std::isfinite(group->cvPercent))
    {
        group->cvUnavailableReason = QStringLiteral("CV超出数值范围。");
        return;
    }
    group->cvAvailable = true;
    group->cvText = std::fabs(group->cvPercent) > 0.0001
        ? QString::number(group->cvPercent, 'f', 2) + QStringLiteral("%")
        : QStringLiteral("0");
}
}

QList<HistoryStatisticGroup> HistoryResultStatistics::groups(const HistoryReport &report)
{
    QList<HistoryStatisticGroup> groups;
    for (const auto &result : report.results)
        for (const auto &value : result.values)
        {
            int index = 0;
            while (index < groups.size()
                   && (groups.at(index).project != result.project
                       || groups.at(index).resultType != value.resultType))
                ++index;
            if (index == groups.size())
            {
                HistoryStatisticGroup group;
                group.project = result.project;
                group.resultType = value.resultType;
                group.label = value.label;
                group.unit = value.unit;
                group.decimals = value.decimals;
                group.factor = value.factor;
                groups.append(group);
            }
            auto &group = groups[index];
            group.configurationConsistent = group.configurationConsistent && sameConfiguration(group, value);
        }
    for (auto &group : groups)
    {
        for (const auto &result : report.results)
        {
            if (result.project != group.project)
                continue;
            HistoryStatisticEntry entry;
            entry.taskId = result.taskId;
            entry.testedAt = result.testedAt;
            entry.abnormalType = result.abnormalType;
            entry.valuePresent = false;
            entry.value.resultType = group.resultType;
            entry.value.label = group.label;
            entry.value.unit = group.unit;
            entry.value.decimals = group.decimals;
            entry.value.factor = group.factor;
            for (const auto &value : result.values)
                if (value.resultType == group.resultType)
                {
                    entry.value = value;
                    entry.valuePresent = true;
                    break;
                }
            // 原版对配置列遍历同项目全部任务，缺失类型也计一次无效复测。
            // 此处只生成显示/统计快照，绝不向原任务或数据库补写结果。
            bool converted = false;
            const double raw = entry.value.rawValue.toDouble(&converted);
            entry.scaledValue = raw;
            if (entry.value.resultType == 2 || entry.value.resultType == 3)
                entry.scaledValue *= entry.value.factor;
            entry.numeric = entry.valuePresent && converted
                && std::isfinite(raw) && std::isfinite(entry.scaledValue);
            if (entry.numeric)
                ++group.validCount;
            else
                ++group.invalidCount;
            group.entries.append(entry);
        }
        calculateGroup(&group);
    }
    return groups;
}
