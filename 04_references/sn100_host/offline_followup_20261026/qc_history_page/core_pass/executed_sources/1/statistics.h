#ifndef SN100_QUALITY_STATISTICS_H
#define SN100_QUALITY_STATISTICS_H

#include <QString>
#include <QVector>

namespace Sn100Quality
{
// 本轮离线输入预算，并非仪器规格；限制单次计算与事件循环占用。
constexpr int kMaximumSamples = 10000;
constexpr double kMinimumCvMeanMagnitude = 1e-12;

struct StatisticsResult
{
    bool ok = false;
    QString errorCode;
    QString errorMessage;
    int count = 0;
    double mean = 0.0;
    double range = 0.0;
    double sampleVariance = 0.0;
    double sampleStddev = 0.0;
    bool sampleEstimateAvailable = false;
    bool cvAvailable = false;
    double legacyCvRatio = 0.0;
};

// 来源：原 getStaticInfo/staticInfo_Impl；仅统计，不执行任何失控判定或设备动作。
// CV沿用 stddev/(mean+1e-12)，是带符号比值，不是百分数。
StatisticsResult calculateStatistics(const QVector<double> &values);
}

#endif
