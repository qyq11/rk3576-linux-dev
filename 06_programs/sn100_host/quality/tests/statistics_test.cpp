#include "statistics.h"

#include <QtTest>
#include <cmath>
#include <limits>

using namespace Sn100Quality;

class StatisticsTest : public QObject
{
    Q_OBJECT
private slots:
    void handCalculatedGolden()
    {
        // 手算：均值10、极差4、平方偏差和8、样本方差4、SD2；CV为比值约0.2。
        const StatisticsResult result = calculateStatistics({8, 10, 12});
        QVERIFY(result.ok);
        QCOMPARE(result.mean, 10.0);
        QCOMPARE(result.range, 4.0);
        QCOMPARE(result.sampleVariance, 4.0);
        QCOMPARE(result.sampleStddev, 2.0);
        QVERIFY(result.sampleEstimateAvailable);
        QVERIFY(result.cvAvailable);
        QVERIFY(std::fabs(result.legacyCvRatio - 0.2) < 1e-12);
    }

    void sampleNotPopulation()
    {
        const StatisticsResult result = calculateStatistics({1, 2, 3, 4});
        QVERIFY(result.ok);
        QCOMPARE(result.mean, 2.5);
        QVERIFY(std::fabs(result.sampleVariance - 5.0 / 3.0) < 1e-14);
        QVERIFY(std::fabs(result.sampleStddev - std::sqrt(5.0 / 3.0)) < 1e-14);
    }

    void constantValues()
    {
        const StatisticsResult result = calculateStatistics({7, 7, 7});
        QVERIFY(result.ok);
        QCOMPARE(result.range, 0.0);
        QCOMPARE(result.sampleVariance, 0.0);
        QCOMPARE(result.sampleStddev, 0.0);
        QVERIFY(result.cvAvailable);
        QCOMPARE(result.legacyCvRatio, 0.0);
    }

    void singleSampleIsNotAnEstimate()
    {
        const StatisticsResult result = calculateStatistics({9});
        QVERIFY(result.ok);
        QCOMPARE(result.mean, 9.0);
        QCOMPARE(result.range, 0.0);
        QVERIFY(!result.sampleEstimateAvailable);
        QVERIFY(!result.cvAvailable);
    }

    void zeroMeanHasNoCv()
    {
        const StatisticsResult result = calculateStatistics({-1, 0, 1});
        QVERIFY(result.ok);
        QVERIFY(result.sampleEstimateAvailable);
        QCOMPARE(result.mean, 0.0);
        QCOMPARE(result.sampleVariance, 1.0);
        QVERIFY(!result.cvAvailable);
    }

    void nearZeroMeanHasNoCv()
    {
        const StatisticsResult result = calculateStatistics({0.5e-12, 0.5e-12});
        QVERIFY(result.ok);
        QVERIFY(!result.cvAvailable);
    }

    void epsilonCompatibility()
    {
        const StatisticsResult result = calculateStatistics({1e-11, 3e-11});
        QVERIFY(result.ok);
        QVERIFY(result.cvAvailable);
        // 手算SD=sqrt(2)*1e-11，原分母为2.1e-11；不能偷偷改成mean。
        QVERIFY(std::fabs(result.legacyCvRatio - std::sqrt(2.0) / 2.1) < 1e-14);
    }

    void negativeMeanKeepsSignedLegacyRatio()
    {
        const StatisticsResult result = calculateStatistics({-12, -10, -8});
        QVERIFY(result.ok);
        QCOMPARE(result.mean, -10.0);
        QCOMPARE(result.sampleStddev, 2.0);
        QVERIFY(result.legacyCvRatio < 0.0);
    }

    void invalid_data()
    {
        QTest::addColumn<QVector<double>>("values");
        QTest::addColumn<QString>("code");
        QTest::newRow("empty") << QVector<double>{} << QStringLiteral("empty_series");
        QTest::newRow("nan") << QVector<double>{std::numeric_limits<double>::quiet_NaN()} << QStringLiteral("non_finite_sample");
        QTest::newRow("positive-infinity") << QVector<double>{std::numeric_limits<double>::infinity()} << QStringLiteral("non_finite_sample");
        QTest::newRow("negative-infinity") << QVector<double>{-std::numeric_limits<double>::infinity()} << QStringLiteral("non_finite_sample");
        QTest::newRow("sample-budget") << QVector<double>(kMaximumSamples + 1, 1) << QStringLiteral("sample_limit");
        QTest::newRow("sum-overflow") << QVector<double>{1e308, 1e308} << QStringLiteral("arithmetic_overflow");
        QTest::newRow("range-overflow") << QVector<double>{-1e308, 1e308} << QStringLiteral("arithmetic_overflow");
        QTest::newRow("variance-overflow") << QVector<double>{-1e200, 1e200} << QStringLiteral("arithmetic_overflow");
    }

    void invalid()
    {
        QFETCH(QVector<double>, values);
        QFETCH(QString, code);
        const StatisticsResult result = calculateStatistics(values);
        QVERIFY(!result.ok);
        QCOMPARE(result.errorCode, code);
        QVERIFY(!result.errorMessage.isEmpty());
        QVERIFY(!result.cvAvailable);
        QVERIFY(!result.sampleEstimateAvailable);
    }

    void exactBudgetAccepted()
    {
        const StatisticsResult result = calculateStatistics(QVector<double>(kMaximumSamples, 2));
        QVERIFY(result.ok);
        QCOMPARE(result.count, kMaximumSamples);
        QCOMPARE(result.mean, 2.0);
        QCOMPARE(result.sampleStddev, 0.0);
    }
};

QTEST_GUILESS_MAIN(StatisticsTest)
#include "statistics_test.moc"
