#include "clot_endpoint.h"

#include <QtTest>
#include <cmath>
#include <limits>

using namespace Sn100Coagulation;

namespace
{
EndpointOptions completeOptions()
{
    EndpointOptions options;
    options.completeCurve = true;
    return options;
}

double gompertz(double time, double center = 50.0, double width = 8.0)
{
    return std::exp(-std::exp(-(time - center) / width));
}

QVector<CurvePoint> syntheticCurve(bool falling = false, double timeOffset = 0.0, bool irregular = false)
{
    QVector<CurvePoint> curve;
    for (int index = 0; index <= 240; ++index)
    {
        const double time = index * 0.5 + (irregular ? (index % 3) * 0.03 : 0.0);
        const double value = gompertz(time);
        curve.append({time + timeOffset, falling ? 1.0 - value : value});
    }
    return curve;
}
}

class EndpointTest : public QObject
{
    Q_OBJECT
private slots:
    void requiresCompletedAcquisition()
    {
        const EndpointResult result = percentageEndpoint(syntheticCurve(), CurveDirection::Rising);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("incomplete_curve"));
    }

    void distinctMethodsOnAsymmetricCurve()
    {
        // Gompertz导数峰在50s，50%时刻在50-8*ln(-ln(0.5))≈52.9321s。
        // 首末窗口均值接近而不等于0/1，因此给出有限采样下的0.04s误差界。
        const EndpointOptions options = completeOptions();
        const EndpointResult percentage = percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        const EndpointResult rate = maximumDerivativeEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        QVERIFY2(percentage.ok, qPrintable(percentage.errorCode));
        QVERIFY2(rate.ok, qPrintable(rate.errorCode));
        QVERIFY(std::fabs(percentage.timeSeconds - (50.0 - 8.0 * std::log(-std::log(0.5)))) < 0.04);
        QVERIFY(std::fabs(rate.timeSeconds - 50.0) <= 0.5);
        QVERIFY(percentage.timeSeconds - rate.timeSeconds > 2.0);
        QVERIFY(rate.peakSlope > 0.0);
    }

    void fallingDirectionIsExplicit()
    {
        const EndpointOptions options = completeOptions();
        const EndpointResult rising = percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        const EndpointResult falling = percentageEndpoint(syntheticCurve(true), CurveDirection::Falling, options);
        QVERIFY(rising.ok && falling.ok);
        QVERIFY(std::fabs(rising.timeSeconds - falling.timeSeconds) < 1e-10);
        const EndpointResult wrong = percentageEndpoint(syntheticCurve(true), CurveDirection::Rising, options);
        QVERIFY(!wrong.ok);
        QCOMPARE(wrong.errorCode, QStringLiteral("direction_mismatch"));
    }

    void preservesReagentTimeOrigin()
    {
        const EndpointOptions options = completeOptions();
        const EndpointResult original = percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        const EndpointResult offset = percentageEndpoint(syntheticCurve(false, 13.0), CurveDirection::Rising, options);
        QVERIFY(original.ok && offset.ok);
        QVERIFY(std::fabs(offset.timeSeconds - original.timeSeconds - 13.0) < 1e-12);
    }

    void supportsNonuniformTimes()
    {
        const EndpointResult result = maximumDerivativeEndpoint(syntheticCurve(false, 0.0, true), CurveDirection::Rising, completeOptions());
        QVERIFY2(result.ok, qPrintable(result.errorCode));
        QVERIFY(std::fabs(result.timeSeconds - 50.0) < 0.6);
    }

    void configurablePercentage()
    {
        EndpointOptions options = completeOptions();
        options.thresholdPercent = 25.0;
        const EndpointResult result = percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        QVERIFY2(result.ok, qPrintable(result.errorCode));
        QVERIFY(std::fabs(result.timeSeconds - (50.0 - 8.0 * std::log(-std::log(0.25)))) < 0.04);
    }

    void doesNotInventPlatformOnLinearRamp()
    {
        QVector<CurvePoint> curve;
        for (int index = 0; index <= 200; ++index) curve.append({double(index), double(index)});
        const EndpointResult result = percentageEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("reference_still_changing"));
    }

    void flatAndLowResponsesFail()
    {
        QVector<CurvePoint> curve = syntheticCurve();
        for (CurvePoint &point : curve) point.opticalValue = 3.0;
        const EndpointResult flat = percentageEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(!flat.ok);
        QCOMPARE(flat.errorCode, QStringLiteral("low_response_amplitude"));
        curve = syntheticCurve();
        for (CurvePoint &point : curve) point.opticalValue *= 1e-8;
        const EndpointResult weak = maximumDerivativeEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(!weak.ok);
        QCOMPARE(weak.errorCode, QStringLiteral("low_response_amplitude"));
    }

    void unstablePlatformFails()
    {
        QVector<CurvePoint> curve = syntheticCurve();
        curve.last().opticalValue += 0.3;
        const EndpointResult result = percentageEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("unstable_reference_window"));
    }

    void clippedSearchIsNotAPeak()
    {
        EndpointOptions options = completeOptions();
        options.useSearchWindow = true;
        options.searchStartSeconds = 0.0;
        options.searchEndSeconds = 40.0;
        const EndpointResult result = maximumDerivativeEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("peak_at_boundary"));
    }

    void thresholdAtSearchBoundaryFails()
    {
        EndpointOptions options = completeOptions();
        const EndpointResult original = percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        QVERIFY(original.ok);
        options.useSearchWindow = true;
        options.searchStartSeconds = original.timeSeconds;
        options.searchEndSeconds = 100.0;
        const EndpointResult result = percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("endpoint_at_boundary"));
    }

    void rejectsTwoSeparateRatePeaks()
    {
        QVector<CurvePoint> curve;
        for (int index = 0; index <= 240; ++index)
        {
            const double time = index * 0.5;
            curve.append({time, 0.5 * gompertz(time, 35.0, 3.0) + 0.5 * gompertz(time, 75.0, 3.0)});
        }
        const EndpointOptions options = completeOptions();
        const EndpointResult rate = maximumDerivativeEndpoint(curve, CurveDirection::Rising, options);
        QVERIFY(!rate.ok);
        QCOMPARE(rate.errorCode, QStringLiteral("multiple_rate_peaks"));
        const EndpointResult percentage = percentageEndpoint(curve, CurveDirection::Rising, options);
        QVERIFY(percentage.ok);
        QVERIFY(percentage.warnings.contains(QStringLiteral("multiple_high_rate_regions")));
    }

    void repeatedThresholdCrossingsFail()
    {
        QVector<CurvePoint> curve;
        for (int time = 0; time <= 100; ++time)
        {
            double value = 0.0;
            if (time > 10 && time <= 30) value = (time - 10) * 0.04;
            else if (time > 30 && time <= 50) value = 0.8 - (time - 30) * 0.03;
            else if (time > 50 && time <= 70) value = 0.2 + (time - 50) * 0.04;
            else if (time > 70) value = 1.0;
            curve.append({double(time), value});
        }
        const EndpointResult result = percentageEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, QStringLiteral("multiple_threshold_crossings"));
    }

    void flatRatePeakIsNotUnique()
    {
        QVector<CurvePoint> curve;
        for (int time = 0; time <= 100; ++time)
        {
            const double value = time <= 20 ? 0.0 : time >= 80 ? 1.0 : (time - 20) / 60.0;
            curve.append({double(time), value});
        }
        const EndpointResult percentage = percentageEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(percentage.ok);
        QVERIFY(std::fabs(percentage.timeSeconds - 50.0) < 1e-10);
        const EndpointResult rate = maximumDerivativeEndpoint(curve, CurveDirection::Rising, completeOptions());
        QVERIFY(!rate.ok);
        QCOMPARE(rate.errorCode, QStringLiteral("flat_rate_peak"));
    }

    void invalidPointsFail()
    {
        QVector<CurvePoint> curve = syntheticCurve();
        curve[1].timeSeconds = curve.first().timeSeconds;
        QCOMPARE(percentageEndpoint(curve, CurveDirection::Rising, completeOptions()).errorCode, QStringLiteral("time_not_increasing"));
        curve = syntheticCurve();
        curve[1].opticalValue = std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(percentageEndpoint(curve, CurveDirection::Rising, completeOptions()).errorCode, QStringLiteral("non_finite_point"));
        curve = syntheticCurve();
        curve[0].timeSeconds = -0.5;
        QCOMPARE(percentageEndpoint(curve, CurveDirection::Rising, completeOptions()).errorCode, QStringLiteral("negative_time"));
    }

    void invalidParametersFail()
    {
        EndpointOptions options = completeOptions();
        options.thresholdPercent = 100.0;
        QCOMPARE(percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options).errorCode, QStringLiteral("invalid_threshold"));
        options = completeOptions(); options.baselinePoints = 1;
        QCOMPARE(percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options).errorCode, QStringLiteral("reference_windows"));
        options = completeOptions(); options.derivativeWindowPoints = 4;
        QCOMPARE(maximumDerivativeEndpoint(syntheticCurve(), CurveDirection::Rising, options).errorCode, QStringLiteral("invalid_derivative_options"));
        options = completeOptions(); options.minimumAmplitude = std::numeric_limits<double>::infinity();
        QCOMPARE(percentageEndpoint(syntheticCurve(), CurveDirection::Rising, options).errorCode, QStringLiteral("invalid_options"));
    }

    void pointBudgetIsBounded()
    {
        const QVector<CurvePoint> curve(kMaximumCurvePoints + 1);
        QCOMPARE(percentageEndpoint(curve, CurveDirection::Rising, completeOptions()).errorCode, QStringLiteral("point_limit"));
    }
};

QTEST_GUILESS_MAIN(EndpointTest)
#include "endpoint_test.moc"
