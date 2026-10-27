#include "statistics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Sn100Quality
{
namespace
{
StatisticsResult failure(int count, const QString &code, const QString &message)
{
    StatisticsResult result;
    result.count = count;
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}
}

StatisticsResult calculateStatistics(const QVector<double> &values)
{
    if (values.isEmpty())
    {
        return failure(0, QStringLiteral("empty_series"), QStringLiteral("统计至少需要一个有限数值"));
    }
    if (values.size() > kMaximumSamples)
    {
        return failure(values.size(), QStringLiteral("sample_limit"), QStringLiteral("单序列超过10000点离线预算"));
    }

    StatisticsResult result;
    result.count = values.size();
    double maximum = -std::numeric_limits<double>::infinity();
    double minimum = std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (double value : values)
    {
        if (!std::isfinite(value))
        {
            return failure(values.size(), QStringLiteral("non_finite_sample"), QStringLiteral("输入含NaN或无穷值"));
        }
        sum += value;
        maximum = std::max(maximum, value);
        minimum = std::min(minimum, value);
        if (!std::isfinite(sum))
        {
            return failure(values.size(), QStringLiteral("arithmetic_overflow"), QStringLiteral("求和超出double范围"));
        }
    }
    result.mean = sum / values.size();
    result.range = maximum - minimum;
    if (!std::isfinite(result.mean) || !std::isfinite(result.range))
    {
        return failure(values.size(), QStringLiteral("arithmetic_overflow"), QStringLiteral("均值或极差超出double范围"));
    }

    if (values.size() > 1)
    {
        double squaredDeviations = 0.0;
        double deviations = 0.0;
        for (double value : values)
        {
            // 保留原两遍累加及(n-1)样本方差，不替换成总体方差。
            const double difference = value - result.mean;
            squaredDeviations += std::pow(difference, 2);
            deviations += difference;
            if (!std::isfinite(difference) || !std::isfinite(squaredDeviations) || !std::isfinite(deviations))
            {
                return failure(values.size(), QStringLiteral("arithmetic_overflow"), QStringLiteral("偏差平方或累加超出double范围"));
            }
        }
        const double correction = std::pow(deviations, 2) / values.size();
        if (!std::isfinite(correction))
        {
            return failure(values.size(), QStringLiteral("arithmetic_overflow"), QStringLiteral("方差修正项超出double范围"));
        }
        result.sampleVariance = (squaredDeviations - correction) / (values.size() - 1);
        if (result.sampleVariance < 0.0)
        {
            result.sampleVariance = 0.0;
        }
        result.sampleStddev = std::sqrt(result.sampleVariance);
        if (!std::isfinite(result.sampleVariance) || !std::isfinite(result.sampleStddev))
        {
            return failure(values.size(), QStringLiteral("arithmetic_overflow"), QStringLiteral("方差或标准差超出double范围"));
        }
        result.sampleEstimateAvailable = true;

        // 原程序把近零均值也除以mean+1e-12。离线移植明确标为不可用，避免把巨大比值当有效CV。
        if (std::fabs(result.mean) > kMinimumCvMeanMagnitude)
        {
            result.legacyCvRatio = result.sampleStddev / (result.mean + std::pow(10.0, -12));
            if (!std::isfinite(result.legacyCvRatio))
            {
                return failure(values.size(), QStringLiteral("arithmetic_overflow"), QStringLiteral("原兼容CV比值超出double范围"));
            }
            result.cvAvailable = true;
        }
    }
    // 单点只提供均值/极差；方差、SD与CV的可用标志仍为false。
    result.ok = true;
    return result;
}
}
