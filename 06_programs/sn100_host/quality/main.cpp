#include "statistics.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <cmath>

using namespace Sn100Quality;

namespace
{
constexpr int kMaximumSeries = 64;
constexpr qint64 kMaximumInputBytes = 2 * 1024 * 1024;

int inputError(const QString &code, const QString &message)
{
    const QJsonObject error{{QStringLiteral("ok"), false}, {QStringLiteral("error_code"), code},
        {QStringLiteral("error_message"), message}};
    QTextStream(stderr) << QJsonDocument(error).toJson(QJsonDocument::Compact) << Qt::endl;
    return 2;
}

QJsonObject serialize(const StatisticsResult &result)
{
    QJsonObject output{{QStringLiteral("ok"), result.ok}, {QStringLiteral("count"), result.count}};
    if (!result.ok)
    {
        output.insert(QStringLiteral("error_code"), result.errorCode);
        output.insert(QStringLiteral("error_message"), result.errorMessage);
        return output;
    }
    output.insert(QStringLiteral("mean"), result.mean);
    output.insert(QStringLiteral("range"), result.range);
    output.insert(QStringLiteral("sample_estimate_available"), result.sampleEstimateAvailable);
    output.insert(QStringLiteral("sample_variance"), result.sampleEstimateAvailable ? QJsonValue(result.sampleVariance) : QJsonValue());
    output.insert(QStringLiteral("sample_stddev"), result.sampleEstimateAvailable ? QJsonValue(result.sampleStddev) : QJsonValue());
    output.insert(QStringLiteral("cv_available"), result.cvAvailable);
    output.insert(QStringLiteral("legacy_cv_ratio"), result.cvAvailable ? QJsonValue(result.legacyCvRatio) : QJsonValue());
    output.insert(QStringLiteral("cv_unit"), QStringLiteral("ratio-not-percent"));
    if (!result.sampleEstimateAvailable)
    {
        output.insert(QStringLiteral("unavailable_reason"), QStringLiteral("sample_size_below_two"));
    }
    else if (!result.cvAvailable)
    {
        output.insert(QStringLiteral("unavailable_reason"), QStringLiteral("mean_magnitude_at_or_below_1e-12"));
    }
    return output;
}

bool writeJson(const QString &path, const QJsonObject &data)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(data).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

double percentile(QVector<double> values, int percent)
{
    if (values.isEmpty()) return 0.0;
    std::sort(values.begin(), values.end());
    return values.at(qMin(values.size() - 1, (values.size() * percent + 99) / 100 - 1));
}

class StatisticsRunner : public QObject
{
public:
    StatisticsRunner(QJsonArray series, QString directory, int stressCount)
        : m_series(series), m_directory(directory), m_stressCount(stressCount)
    {
        m_heartbeat.setInterval(10);
        m_heartbeat.setTimerType(Qt::PreciseTimer);
        connect(&m_heartbeat, &QTimer::timeout, this, [this]()
        {
            const qint64 now = m_clock.nsecsElapsed();
            m_heartbeatGaps.append(double(now - m_lastHeartbeat) / 1000000.0);
            m_lastHeartbeat = now;
        });
    }

    void start()
    {
        m_clock.start();
        m_heartbeat.start();
        QTimer::singleShot(0, this, [this]() { step(); });
    }

private:
    void failOutput()
    {
        m_heartbeat.stop();
        inputError(QStringLiteral("output_error"), QStringLiteral("无法提交新输出文件，本轮不算通过"));
        QCoreApplication::exit(3);
    }

    void step()
    {
        if (m_seriesIndex < m_series.size())
        {
            const QJsonObject input = m_series.at(m_seriesIndex++).toObject();
            QVector<double> values;
            for (const QJsonValue &value : input.value(QStringLiteral("values")).toArray()) values.append(value.toDouble());
            const StatisticsResult result = calculateStatistics(values);
            const QJsonObject record{{QStringLiteral("id"), input.value(QStringLiteral("id"))},
                {QStringLiteral("input_values"), input.value(QStringLiteral("values"))},
                {QStringLiteral("statistics"), serialize(result)}};
            m_results.append(record);
            QTextStream(stdout) << QJsonDocument(record).toJson(QJsonDocument::Compact) << Qt::endl;
            if (!result.ok) ++m_failures;
            // 压力阶段只重复已成功的真实统计输入，不产生模拟临床判定。
            if (result.ok && m_stressInput.isEmpty()) m_stressInput = values;
            QTimer::singleShot(1, this, [this]() { step(); });
            return;
        }
        if (m_stressIndex < m_stressCount)
        {
            if (m_stressInput.isEmpty())
            {
                m_heartbeat.stop();
                inputError(QStringLiteral("stress_input_unavailable"), QStringLiteral("没有可重复计算的合法序列"));
                QCoreApplication::exit(4);
                return;
            }
            // 每批最多20次并回到事件循环，避免在一个槽中跑完整压力任务。
            for (int batch = 0; batch < 20 && m_stressIndex < m_stressCount; ++batch)
            {
                QElapsedTimer operation;
                operation.start();
                const StatisticsResult result = calculateStatistics(m_stressInput);
                m_operationTimes.append(double(operation.nsecsElapsed()) / 1000000.0);
                if (!result.ok) ++m_failures;
                m_lastStressMean = result.mean;
                ++m_stressIndex;
            }
            QTimer::singleShot(1, this, [this]() { step(); });
            return;
        }
        finish();
    }

    void finish()
    {
        m_heartbeat.stop();
        const QJsonObject results{{QStringLiteral("schema"), QStringLiteral("sn100-quality-statistics-result-v1")},
            {QStringLiteral("evidence"), QStringLiteral("anonymous-offline-source-derived")},
            {QStringLiteral("series"), m_results}};
        const QJsonObject summary{{QStringLiteral("schema"), QStringLiteral("sn100-quality-statistics-measurement-v1")},
            {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
            {QStringLiteral("architecture"), QSysInfo::buildCpuArchitecture()},
            {QStringLiteral("os"), QSysInfo::prettyProductName()},
            {QStringLiteral("elapsed_ms"), double(m_clock.elapsed())},
            {QStringLiteral("series_count"), m_results.size()}, {QStringLiteral("failures"), m_failures},
            {QStringLiteral("stress_operations"), m_stressIndex},
            {QStringLiteral("stress_input_values"), QJsonArray::fromVariantList(toVariantList(m_stressInput))},
            {QStringLiteral("stress_last_mean"), m_stressIndex == 0 ? QJsonValue() : QJsonValue(m_lastStressMean)},
            {QStringLiteral("operation_p95_ms"), m_operationTimes.isEmpty() ? QJsonValue() : QJsonValue(percentile(m_operationTimes, 95))},
            {QStringLiteral("operation_p99_ms"), m_operationTimes.isEmpty() ? QJsonValue() : QJsonValue(percentile(m_operationTimes, 99))},
            {QStringLiteral("operation_max_ms"), m_operationTimes.isEmpty() ? QJsonValue() : QJsonValue(*std::max_element(m_operationTimes.cbegin(), m_operationTimes.cend()))},
            {QStringLiteral("heartbeat_samples"), m_heartbeatGaps.size()},
            {QStringLiteral("heartbeat_p95_ms"), m_heartbeatGaps.isEmpty() ? QJsonValue() : QJsonValue(percentile(m_heartbeatGaps, 95))},
            {QStringLiteral("heartbeat_max_ms"), m_heartbeatGaps.isEmpty() ? QJsonValue() : QJsonValue(*std::max_element(m_heartbeatGaps.cbegin(), m_heartbeatGaps.cend()))},
            {QStringLiteral("clinical_rule_evaluation"), false}, {QStringLiteral("network_or_instrument"), false}};
        if (!writeJson(m_directory + QStringLiteral("/results.json"), results) ||
            !writeJson(m_directory + QStringLiteral("/measurement.json"), summary))
        {
            failOutput();
            return;
        }
        QTextStream(stdout) << "SUMMARY " << QJsonDocument(summary).toJson(QJsonDocument::Compact) << Qt::endl;
        QCoreApplication::exit(m_failures == 0 ? 0 : 4);
    }

    static QVariantList toVariantList(const QVector<double> &values)
    {
        QVariantList output;
        for (double value : values) output.append(value);
        return output;
    }

    QJsonArray m_series;
    QString m_directory;
    int m_stressCount = 0;
    int m_seriesIndex = 0;
    int m_stressIndex = 0;
    int m_failures = 0;
    QJsonArray m_results;
    QVector<double> m_stressInput;
    QVector<double> m_operationTimes;
    QVector<double> m_heartbeatGaps;
    double m_lastStressMean = 0.0;
    QTimer m_heartbeat;
    QElapsedTimer m_clock;
    qint64 m_lastHeartbeat = 0;
};
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sn100_quality_statistics"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("O06-1 匿名离线质控统计；不计算失控规则、不连接设备。"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("input"), QStringLiteral("匿名固定JSON输入文件"), QStringLiteral("absolute-file")});
    parser.addOption({QStringLiteral("output"), QStringLiteral("尚不存在的绝对输出目录"), QStringLiteral("absolute-directory")});
    parser.addOption({QStringLiteral("stress"), QStringLiteral("重复第一个合法序列0至1000000次，分批返回事件循环"), QStringLiteral("count"), QStringLiteral("0")});
    parser.process(application);
    if (!parser.positionalArguments().isEmpty()) return inputError(QStringLiteral("arguments"), QStringLiteral("不接受位置参数"));
    const QString inputPath = parser.value(QStringLiteral("input"));
    const QString outputPath = parser.value(QStringLiteral("output"));
    if (!QFileInfo(inputPath).isAbsolute() || !QFileInfo(outputPath).isAbsolute())
    {
        return inputError(QStringLiteral("absolute_paths_required"), QStringLiteral("输入文件和新输出目录必须显式使用绝对路径"));
    }
    bool stressOk = false;
    const int stressCount = parser.value(QStringLiteral("stress")).toInt(&stressOk);
    if (!stressOk || stressCount < 0 || stressCount > 1000000)
    {
        return inputError(QStringLiteral("stress_limit"), QStringLiteral("压力次数应为0至1000000"));
    }
    QFile inputFile(inputPath);
    if (!inputFile.open(QIODevice::ReadOnly) || inputFile.size() > kMaximumInputBytes)
    {
        return inputError(QStringLiteral("input_file"), QStringLiteral("输入不可读或超过2MiB预算"));
    }
    const QByteArray inputBytes = inputFile.read(kMaximumInputBytes + 1);
    if (inputBytes.size() > kMaximumInputBytes || inputFile.error() != QFileDevice::NoError)
    {
        return inputError(QStringLiteral("input_file"), QStringLiteral("输入读取失败或超过2MiB预算"));
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(inputBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        return inputError(QStringLiteral("invalid_json"), QStringLiteral("输入必须是合法JSON对象，数值须有限"));
    }
    const QJsonObject input = document.object();
    if (input.value(QStringLiteral("schema")) != QStringLiteral("sn100-quality-statistics-input-v1") ||
        input.value(QStringLiteral("evidence")) != QStringLiteral("anonymous-handcrafted") ||
        !input.value(QStringLiteral("series")).isArray())
    {
        return inputError(QStringLiteral("input_schema"), QStringLiteral("需要已声明的匿名统计输入契约"));
    }
    const QJsonArray series = input.value(QStringLiteral("series")).toArray();
    if (series.isEmpty() || series.size() > kMaximumSeries)
    {
        return inputError(QStringLiteral("series_limit"), QStringLiteral("序列数量须为1至64"));
    }
    QSet<QString> identifiers;
    for (const QJsonValue &entry : series)
    {
        const QJsonObject item = entry.toObject();
        const QString id = item.value(QStringLiteral("id")).toString();
        if (!entry.isObject() || id.isEmpty() || id.size() > 64 || identifiers.contains(id) ||
            !item.value(QStringLiteral("values")).isArray())
        {
            return inputError(QStringLiteral("series_schema"), QStringLiteral("序列需唯一1至64字符ID及数值数组"));
        }
        identifiers.insert(id);
        const QJsonArray values = item.value(QStringLiteral("values")).toArray();
        if (values.size() > kMaximumSamples)
        {
            return inputError(QStringLiteral("sample_limit"), QStringLiteral("单序列超过10000点预算"));
        }
        for (const QJsonValue &value : values)
        {
            if (!value.isDouble() || !std::isfinite(value.toDouble()))
            {
                return inputError(QStringLiteral("non_numeric_sample"), QStringLiteral("样本必须是有限JSON数值，不能是字符串/null/布尔值"));
            }
        }
    }
    // 检查后用mkdir原子争用新目录，绝不复用已有目录或覆盖旧结果。
    const QFileInfo outputInfo(QDir::cleanPath(outputPath));
    QDir parent = outputInfo.dir();
    if (outputInfo.exists() || outputInfo.isSymLink() || !parent.exists() || !parent.mkdir(outputInfo.fileName()))
    {
        return inputError(QStringLiteral("new_output_required"), QStringLiteral("输出目录必须尚不存在且父目录存在"));
    }
    StatisticsRunner runner(series, outputInfo.absoluteFilePath(), stressCount);
    QTimer::singleShot(0, &runner, [&runner]() { runner.start(); });
    return application.exec();
}
