#include "clot_endpoint.h"

#include <algorithm>
#include <cmath>

namespace Sn100Coagulation
{
namespace
{
constexpr int kMaximumDerivativeWindowPoints = 101;

struct CurveContext
{
    EndpointResult reference;
    QVector<double> response;
    QVector<double> intervalRates;
    double sign = 1.0;
    double searchStart = 0.0;
    double searchEnd = 0.0;
    double maximumIntervalRate = 0.0;
};

EndpointResult failure(const QString &code, const QString &message)
{
    EndpointResult result;
    result.errorCode = code;
    result.errorMessage = message;
    return result;
}

bool fractionIsValid(double value)
{
    return std::isfinite(value) && value > 0.0 && value < 1.0;
}

// 时间/光学值先平移和缩放，降低大偏置、非均匀采样下OLS的消差与溢出风险。
// 这是普通局部直线拟合，不是厂商仪器的滤波或异常曲线保护算法。
bool regressionSlope(const QVector<CurvePoint> &curve, int first, int count,
                     double sign, double *slope)
{
    const CurvePoint &origin = curve.at(first + count / 2);
    double timeScale = 0.0;
    double valueScale = 0.0;
    for (int index = first; index < first + count; ++index)
    {
        const double x = curve.at(index).timeSeconds - origin.timeSeconds;
        const double y = curve.at(index).opticalValue - origin.opticalValue;
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        timeScale = std::max(timeScale, std::fabs(x));
        valueScale = std::max(valueScale, std::fabs(y));
    }
    if (!(timeScale > 0.0)) return false;
    if (valueScale == 0.0)
    {
        *slope = 0.0;
        return true;
    }
    double meanX = 0.0;
    double meanY = 0.0;
    for (int index = first; index < first + count; ++index)
    {
        meanX += ((curve.at(index).timeSeconds - origin.timeSeconds) / timeScale) / count;
        meanY += (sign * (curve.at(index).opticalValue - origin.opticalValue) / valueScale) / count;
    }
    double covariance = 0.0;
    double timeVariance = 0.0;
    for (int index = first; index < first + count; ++index)
    {
        const double x = (curve.at(index).timeSeconds - origin.timeSeconds) / timeScale - meanX;
        const double y = sign * (curve.at(index).opticalValue - origin.opticalValue) / valueScale - meanY;
        covariance += x * y;
        timeVariance += x * x;
    }
    if (!(timeVariance > 0.0)) return false;
    const long double fitted = (static_cast<long double>(covariance) / timeVariance) * valueScale / timeScale;
    *slope = static_cast<double>(fitted);
    return std::isfinite(*slope);
}

bool referenceWindow(const QVector<CurvePoint> &curve, int first, int count,
                     double *mean, double *range)
{
    double minimum = curve.at(first).opticalValue;
    double maximum = minimum;
    *mean = 0.0;
    for (int index = first; index < first + count; ++index)
    {
        // 每项先除以点数，均值不会仅因大常数偏置的原始求和溢出。
        *mean += curve.at(index).opticalValue / count;
        minimum = std::min(minimum, curve.at(index).opticalValue);
        maximum = std::max(maximum, curve.at(index).opticalValue);
    }
    *range = maximum - minimum;
    return std::isfinite(*mean) && std::isfinite(*range);
}

int highRateRegions(const QVector<double> &rates, double threshold)
{
    int regions = 0;
    bool previousHigh = false;
    for (double rate : rates)
    {
        const bool high = rate >= threshold;
        if (high && !previousHigh) ++regions;
        previousHigh = high;
    }
    return regions;
}

bool prepareCurve(const QVector<CurvePoint> &curve, CurveDirection direction,
                  const EndpointOptions &options, CurveContext *context, EndpointResult *error)
{
    if (!options.completeCurve)
    {
        *error = failure(QStringLiteral("incomplete_curve"), QStringLiteral("本实现只接受调用者明确确认已采集完成的离线曲线"));
        return false;
    }
    if (direction != CurveDirection::Rising && direction != CurveDirection::Falling)
    {
        *error = failure(QStringLiteral("invalid_direction"), QStringLiteral("光学响应方向必须显式指定为rising或falling"));
        return false;
    }
    if (curve.size() > kMaximumCurvePoints)
    {
        *error = failure(QStringLiteral("point_limit"), QStringLiteral("曲线超过100000点离线预算"));
        return false;
    }
    if (options.baselinePoints < 2 || options.plateauPoints < 2 ||
        options.baselinePoints >= curve.size() || options.plateauPoints >= curve.size() ||
        options.baselinePoints > curve.size() - options.plateauPoints - 1)
    {
        *error = failure(QStringLiteral("reference_windows"), QStringLiteral("首末参考窗口均须至少2点、互不重叠，并保留反应区采样点"));
        return false;
    }
    if (!std::isfinite(options.minimumAmplitude) || options.minimumAmplitude <= 0.0 ||
        !fractionIsValid(options.maximumBaselineRangeFraction) ||
        !fractionIsValid(options.maximumPlateauRangeFraction) ||
        !fractionIsValid(options.maximumReferenceSlopeFraction))
    {
        *error = failure(QStringLiteral("invalid_options"), QStringLiteral("最小幅度须为正有限值，参考窗口范围/斜率比例须在0至1之间"));
        return false;
    }
    for (int index = 0; index < curve.size(); ++index)
    {
        const CurvePoint &point = curve.at(index);
        if (!std::isfinite(point.timeSeconds) || !std::isfinite(point.opticalValue))
        {
            *error = failure(QStringLiteral("non_finite_point"), QStringLiteral("时间及光学值必须均为有限数值"));
            return false;
        }
        if (point.timeSeconds < 0.0)
        {
            *error = failure(QStringLiteral("negative_time"), QStringLiteral("本轮只接受加试剂起点之后的非负秒数；不自动重定位负时间曲线"));
            return false;
        }
        if (index > 0 && !(point.timeSeconds > curve.at(index - 1).timeSeconds))
        {
            *error = failure(QStringLiteral("time_not_increasing"), QStringLiteral("采样时间必须严格递增；不能重复、倒序或按点序替代时间"));
            return false;
        }
    }
    context->searchStart = curve.first().timeSeconds;
    context->searchEnd = curve.last().timeSeconds;
    if (options.useSearchWindow)
    {
        if (!std::isfinite(options.searchStartSeconds) || !std::isfinite(options.searchEndSeconds) ||
            options.searchStartSeconds < context->searchStart || options.searchEndSeconds > context->searchEnd ||
            !(options.searchStartSeconds < options.searchEndSeconds))
        {
            *error = failure(QStringLiteral("invalid_search_window"), QStringLiteral("搜索时间窗须有限、起点小于终点且完整位于采样时间范围内"));
            return false;
        }
        context->searchStart = options.searchStartSeconds;
        context->searchEnd = options.searchEndSeconds;
    }
    double baselineRange = 0.0;
    double plateauRange = 0.0;
    EndpointResult &reference = context->reference;
    if (!referenceWindow(curve, 0, options.baselinePoints, &reference.baseline, &baselineRange) ||
        !referenceWindow(curve, curve.size() - options.plateauPoints, options.plateauPoints,
                         &reference.plateau, &plateauRange))
    {
        *error = failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("参考窗口均值或极差超出数值范围"));
        return false;
    }
    context->sign = direction == CurveDirection::Rising ? 1.0 : -1.0;
    reference.amplitude = context->sign * (reference.plateau - reference.baseline);
    if (!std::isfinite(reference.amplitude))
    {
        *error = failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("响应幅度超出数值范围"));
        return false;
    }
    if (reference.amplitude < 0.0)
    {
        *error = failure(QStringLiteral("direction_mismatch"), QStringLiteral("首末响应与指定光学方向相反，不自动反转方向"));
        return false;
    }
    if (reference.amplitude < options.minimumAmplitude)
    {
        *error = failure(QStringLiteral("low_response_amplitude"), QStringLiteral("响应为平坦或低于显式最小幅度，不能给出有效判点"));
        return false;
    }
    if (baselineRange / reference.amplitude > options.maximumBaselineRangeFraction ||
        plateauRange / reference.amplitude > options.maximumPlateauRangeFraction)
    {
        *error = failure(QStringLiteral("unstable_reference_window"), QStringLiteral("基线或末端窗口波动过大，不能证明稳定参考平台"));
        return false;
    }
    context->response.reserve(curve.size());
    context->intervalRates.reserve(curve.size() - 1);
    bool reverses = false;
    for (int index = 0; index < curve.size(); ++index)
    {
        const double value = context->sign * (curve.at(index).opticalValue - reference.baseline);
        if (!std::isfinite(value))
        {
            *error = failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("基线平移后的响应超出数值范围"));
            return false;
        }
        context->response.append(value);
        if (index > 0)
        {
            const double elapsed = curve.at(index).timeSeconds - curve.at(index - 1).timeSeconds;
            const double rate = (value - context->response.at(index - 1)) / elapsed;
            if (!std::isfinite(elapsed) || !std::isfinite(rate))
            {
                *error = failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("时间跨度或相邻响应速率超出数值范围"));
                return false;
            }
            context->intervalRates.append(rate);
            context->maximumIntervalRate = std::max(context->maximumIntervalRate, rate);
            reverses = reverses || rate < 0.0;
        }
    }
    double baselineSlope = 0.0;
    double plateauSlope = 0.0;
    if (!regressionSlope(curve, 0, options.baselinePoints, context->sign, &baselineSlope) ||
        !regressionSlope(curve, curve.size() - options.plateauPoints, options.plateauPoints,
                         context->sign, &plateauSlope))
    {
        *error = failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("参考窗口拟合斜率不可表示"));
        return false;
    }
    // 仅看极差/总幅度会把高密度采样的整段线性斜坡误认为已达平台。
    // 同时约束参考窗口斜率；比例是可调工程启发式，不能证明临床采集充分。
    const double allowedSlope = options.maximumReferenceSlopeFraction * context->maximumIntervalRate;
    if (!(context->maximumIntervalRate > 0.0) || std::fabs(baselineSlope) > allowedSlope ||
        std::fabs(plateauSlope) > allowedSlope)
    {
        *error = failure(QStringLiteral("reference_still_changing"), QStringLiteral("基线或末端仍保持明显变化速率，不能当作完整稳定平台"));
        return false;
    }
    if (reverses) reference.warnings.append(QStringLiteral("non_monotonic_curve"));
    return true;
}
}

EndpointResult percentageEndpoint(const QVector<CurvePoint> &curve, CurveDirection direction,
                                  const EndpointOptions &options)
{
    if (!std::isfinite(options.thresholdPercent) || options.thresholdPercent <= 0.0 || options.thresholdPercent >= 100.0)
    {
        return failure(QStringLiteral("invalid_threshold"), QStringLiteral("百分比阈值须为大于0且小于100的有限值"));
    }
    CurveContext context;
    EndpointResult error;
    if (!prepareCurve(curve, direction, options, &context, &error)) return error;
    EndpointResult result = context.reference;
    const double level = result.amplitude * (options.thresholdPercent / 100.0);
    result.thresholdValue = result.baseline + context.sign * level;
    if (!std::isfinite(result.thresholdValue)) return failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("阈值光学值不可表示"));
    int crossings = 0;
    bool previousWasIncreasing = false;
    double previousTime = 0.0;
    for (int index = 1; index < curve.size(); ++index)
    {
        const double before = context.response.at(index - 1);
        const double after = context.response.at(index);
        const double startTime = curve.at(index - 1).timeSeconds;
        const double endTime = curve.at(index).timeSeconds;
        if (before == level && after == level && endTime >= context.searchStart && startTime <= context.searchEnd)
        {
            return failure(QStringLiteral("flat_threshold_segment"), QStringLiteral("曲线在阈值处形成平段，不能确定唯一交点"));
        }
        if (before == after || !((before <= level && after >= level) || (before >= level && after <= level))) continue;
        const double fraction = (level - before) / (after - before);
        const double time = startTime + fraction * (endTime - startTime);
        if (!std::isfinite(time)) return failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("阈值交点插值不可表示"));
        if (time < context.searchStart || time > context.searchEnd) continue;
        const bool increasing = after > before;
        if (crossings > 0 && time == previousTime)
        {
            if (increasing != previousWasIncreasing)
            {
                return failure(QStringLiteral("threshold_touch_without_crossing"), QStringLiteral("曲线仅触及阈值后反向，不能当作唯一凝血交点"));
            }
            continue;
        }
        ++crossings;
        previousTime = time;
        previousWasIncreasing = increasing;
        result.timeSeconds = time;
        result.lowerIndex = index - 1;
        result.upperIndex = index;
    }
    if (crossings == 0) return failure(QStringLiteral("no_threshold_crossing"), QStringLiteral("搜索范围内没有阈值交点"));
    if (crossings != 1 || !previousWasIncreasing)
    {
        return failure(QStringLiteral("multiple_threshold_crossings"), QStringLiteral("搜索范围内存在重复或反向阈值交点，不能唯一判点"));
    }
    if (result.timeSeconds <= context.searchStart || result.timeSeconds >= context.searchEnd ||
        result.timeSeconds <= curve.at(options.baselinePoints - 1).timeSeconds ||
        result.timeSeconds >= curve.at(curve.size() - options.plateauPoints).timeSeconds)
    {
        return failure(QStringLiteral("endpoint_at_boundary"), QStringLiteral("阈值交点位于搜索边缘或参考窗口，缺少两侧反应证据"));
    }
    if (result.timeSeconds < 0.0) return failure(QStringLiteral("endpoint_before_reagent_start"), QStringLiteral("交点早于调用者指定的加试剂时间起点"));
    if (fractionIsValid(options.secondaryPeakFraction) &&
        highRateRegions(context.intervalRates, context.maximumIntervalRate * options.secondaryPeakFraction) > 1)
    {
        result.warnings.append(QStringLiteral("multiple_high_rate_regions"));
    }
    result.ok = true;
    return result;
}

EndpointResult maximumDerivativeEndpoint(const QVector<CurvePoint> &curve, CurveDirection direction,
                                         const EndpointOptions &options)
{
    if (options.derivativeWindowPoints < 3 || options.derivativeWindowPoints > kMaximumDerivativeWindowPoints ||
        options.derivativeWindowPoints % 2 == 0 || !fractionIsValid(options.secondaryPeakFraction))
    {
        return failure(QStringLiteral("invalid_derivative_options"), QStringLiteral("导数回归窗口须为3至101的奇数；第二峰比例须在0至1之间"));
    }
    CurveContext context;
    EndpointResult error;
    if (!prepareCurve(curve, direction, options, &context, &error)) return error;
    struct Candidate { int index; double slope; };
    QVector<Candidate> candidates;
    const int half = options.derivativeWindowPoints / 2;
    for (int index = half; index < curve.size() - half; ++index)
    {
        // 整个拟合窗口都须处在搜索范围，避免借用范围外主峰替边缘候选增加斜率。
        if (curve.at(index - half).timeSeconds < context.searchStart ||
            curve.at(index + half).timeSeconds > context.searchEnd ||
            index <= options.baselinePoints - 1 || index >= curve.size() - options.plateauPoints) continue;
        double slope = 0.0;
        if (!regressionSlope(curve, index - half, options.derivativeWindowPoints, context.sign, &slope))
        {
            return failure(QStringLiteral("arithmetic_overflow"), QStringLiteral("局部线性回归斜率不可表示"));
        }
        candidates.append({index, slope});
    }
    if (candidates.size() < 3) return failure(QStringLiteral("insufficient_peak_support"), QStringLiteral("搜索范围内不足3个有完整回归窗口的峰候选"));
    int maximum = 0;
    for (int index = 1; index < candidates.size(); ++index)
    {
        if (candidates.at(index).slope > candidates.at(maximum).slope) maximum = index;
    }
    const double peak = candidates.at(maximum).slope;
    if (!(peak > 0.0)) return failure(QStringLiteral("no_positive_rate_peak"), QStringLiteral("搜索范围内没有指定方向的正速率峰"));
    if (maximum == 0 || maximum == candidates.size() - 1)
    {
        return failure(QStringLiteral("peak_at_boundary"), QStringLiteral("最大速率落在搜索或可计算范围边缘，不能确认峰顶"));
    }
    QVector<double> slopes;
    slopes.reserve(candidates.size());
    for (const Candidate &candidate : candidates) slopes.append(candidate.slope);
    if (highRateRegions(slopes, peak * options.secondaryPeakFraction) > 1)
    {
        return failure(QStringLiteral("multiple_rate_peaks"), QStringLiteral("存在分离的明显高速度峰，不能自动指定唯一凝血点"));
    }
    int tied = 1;
    const double tieTolerance = peak * 1e-9;
    for (int index = maximum + 1; index < candidates.size() && std::fabs(candidates.at(index).slope - peak) <= tieTolerance; ++index) ++tied;
    for (int index = maximum - 1; index >= 0 && std::fabs(candidates.at(index).slope - peak) <= tieTolerance; --index) ++tied;
    if (tied >= 3) return failure(QStringLiteral("flat_rate_peak"), QStringLiteral("最大速率形成至少3个采样点的平顶，无法确定唯一峰时刻"));
    EndpointResult result = context.reference;
    if (tied == 2) result.warnings.append(QStringLiteral("adjacent_peak_samples_within_tolerance"));
    const int peakIndex = candidates.at(maximum).index;
    result.timeSeconds = curve.at(peakIndex).timeSeconds;
    result.lowerIndex = peakIndex - half;
    result.upperIndex = peakIndex + half;
    result.peakSlope = peak;
    if (result.timeSeconds < 0.0) return failure(QStringLiteral("endpoint_before_reagent_start"), QStringLiteral("速度峰早于调用者指定的加试剂时间起点"));
    result.warnings.append(QStringLiteral("sampled_local_regression_peak_not_clinical_ct"));
    result.ok = true;
    return result;
}
}
