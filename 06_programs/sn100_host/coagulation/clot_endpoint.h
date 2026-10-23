#ifndef SN100_COAGULATION_CLOT_ENDPOINT_H
#define SN100_COAGULATION_CLOT_ENDPOINT_H

#include <QString>
#include <QStringList>
#include <QVector>

namespace Sn100Coagulation
{
// 本轮离线工程预算；不代表仪器的采样容量或临床参数。
constexpr int kMaximumCurvePoints = 100000;

struct CurvePoint
{
    // 时间已由调用者换算为相对加试剂起点的秒数；算法不会减掉首点时间。
    double timeSeconds = 0.0;
    double opticalValue = 0.0;
};

enum class CurveDirection
{
    Rising,
    Falling
};

struct EndpointOptions
{
    // 必须由调用者明确保证采集完整；仍需通过末端平台的数值检查。
    bool completeCurve = false;
    int baselinePoints = 5;
    int plateauPoints = 5;
    double thresholdPercent = 50.0;
    bool useSearchWindow = false;
    double searchStartSeconds = 0.0;
    double searchEndSeconds = 0.0;
    double minimumAmplitude = 1e-6;
    double maximumBaselineRangeFraction = 0.1;
    double maximumPlateauRangeFraction = 0.1;
    double maximumReferenceSlopeFraction = 0.1;
    int derivativeWindowPoints = 5;
    double secondaryPeakFraction = 0.8;
};

struct EndpointResult
{
    bool ok = false;
    QString errorCode;
    QString errorMessage;
    QStringList warnings;
    double timeSeconds = 0.0;
    double baseline = 0.0;
    double plateau = 0.0;
    // 幅度和斜率按显式方向正化；下降曲线的这两个字段仍为正值。
    double amplitude = 0.0;
    double thresholdValue = 0.0;
    double peakSlope = 0.0;
    int lowerIndex = -1;
    int upperIndex = -1;
};

// 基线/末端平台间指定百分比的首次唯一交点，线性插值；不是最大速率时刻。
EndpointResult percentageEndpoint(const QVector<CurvePoint> &curve, CurveDirection direction,
                                  const EndpointOptions &options = EndpointOptions());

// 奇数点局部线性回归求一阶导数，返回最大方向速率的采样时刻。
// 支持非均匀时间；不提供厂商保护算法、临床阈值或AL4A等价声明。
EndpointResult maximumDerivativeEndpoint(const QVector<CurvePoint> &curve, CurveDirection direction,
                                         const EndpointOptions &options = EndpointOptions());
}

#endif
