#include "clot_endpoint.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>
#include <cmath>
#include <limits>

using namespace Sn100Coagulation;

namespace
{
constexpr qint64 kMaximumInputBytes = 8 * 1024 * 1024;

int fail(const QString &code, const QString &message)
{
    QTextStream(stderr) << QJsonDocument(QJsonObject{
        {QStringLiteral("ok"), false}, {QStringLiteral("error_code"), code},
        {QStringLiteral("error_message"), message}}).toJson(QJsonDocument::Compact) << Qt::endl;
    return 2;
}

bool knownKeys(const QJsonObject &object, const QSet<QString> &allowed, QString &error)
{
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        if (!allowed.contains(it.key()))
        {
            error = QStringLiteral("未知字段: ") + it.key();
            return false;
        }
    }
    return true;
}

bool numberOption(const QJsonObject &object, const QString &key, double &destination, QString &error)
{
    if (!object.contains(key)) return true;
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()))
    {
        error = key + QStringLiteral(" 必须为有限数值");
        return false;
    }
    destination = value.toDouble();
    return true;
}

bool integerOption(const QJsonObject &object, const QString &key, int &destination, QString &error)
{
    double value = destination;
    if (!numberOption(object, key, value, error)) return false;
    if (std::floor(value) != value || value < 0 || value > 100000)
    {
        error = key + QStringLiteral(" 必须为0到100000的整数");
        return false;
    }
    destination = int(value);
    return true;
}

QJsonObject serialize(const EndpointResult &result, const QString &method, double elapsedMs)
{
    QJsonObject object{{QStringLiteral("method"), method},
        {QStringLiteral("candidate_found"), result.ok},
        {QStringLiteral("time_seconds"), result.ok ? QJsonValue(result.timeSeconds) : QJsonValue()},
        {QStringLiteral("elapsed_ms"), elapsedMs},
        {QStringLiteral("warnings"), QJsonArray::fromStringList(result.warnings)}};
    if (!result.ok)
    {
        object.insert(QStringLiteral("error_code"), result.errorCode);
        object.insert(QStringLiteral("error_message"), result.errorMessage);
        return object;
    }
    object.insert(QStringLiteral("baseline"), result.baseline);
    object.insert(QStringLiteral("plateau"), result.plateau);
    object.insert(QStringLiteral("directed_amplitude"), result.amplitude);
    object.insert(QStringLiteral("lower_index"), result.lowerIndex);
    object.insert(QStringLiteral("upper_index"), result.upperIndex);
    if (method == QStringLiteral("percentage"))
    {
        object.insert(QStringLiteral("threshold_value"), result.thresholdValue);
    }
    else
    {
        object.insert(QStringLiteral("peak_slope_optical_units_per_second"), result.peakSlope);
    }
    return object;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sn100_clot_endpoint"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("凝血点算法离线研究原型：百分比法和一阶导数最大速率法"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("input"), QStringLiteral("合成/匿名完整曲线JSON绝对路径"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("output"), QStringLiteral("新的结果JSON绝对路径；父目录须存在，不覆盖"), QStringLiteral("file")});
    parser.process(app);
    if (!parser.positionalArguments().isEmpty()) return fail(QStringLiteral("arguments"), QStringLiteral("不接受位置参数"));
    const QFileInfo inputInfo(parser.value(QStringLiteral("input")));
    const QFileInfo outputInfo(parser.value(QStringLiteral("output")));
    if (!inputInfo.isAbsolute() || !inputInfo.isFile() || inputInfo.isSymLink() ||
        inputInfo.size() <= 0 || inputInfo.size() > kMaximumInputBytes)
        return fail(QStringLiteral("input_path"), QStringLiteral("输入必须是非链接的绝对普通文件，大小1到8MiB"));
    if (QDir::fromNativeSeparators(inputInfo.canonicalFilePath()).contains(QStringLiteral("/sn100_host_reference/"), Qt::CaseInsensitive))
        return fail(QStringLiteral("reference_data_forbidden"), QStringLiteral("本入口不读取原参考工程中的数据"));
    if (!outputInfo.isAbsolute() || !outputInfo.dir().exists() || outputInfo.exists() || outputInfo.isSymLink())
        return fail(QStringLiteral("output_path"), QStringLiteral("输出必须为新绝对文件，父目录须存在；已有文件不覆盖"));
    const QString resolvedOutput = QDir::fromNativeSeparators(outputInfo.dir().canonicalPath())
            + QLatin1Char('/') + outputInfo.fileName();
    if (resolvedOutput.contains(QStringLiteral("/sn100_host_reference/"), Qt::CaseInsensitive))
        return fail(QStringLiteral("reference_output_forbidden"), QStringLiteral("参考工程只读，不能作为输出目录"));
    QFile input(inputInfo.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) return fail(QStringLiteral("input_read"), input.errorString());
    const QByteArray bytes = input.read(kMaximumInputBytes + 1);
    if (bytes.size() > kMaximumInputBytes || !input.atEnd())
        return fail(QStringLiteral("input_size"), QStringLiteral("输入超过8MiB"));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(QStringLiteral("input_json"), QStringLiteral("JSON对象解析失败: ") + parseError.errorString());
    const QJsonObject root = document.object();
    QString error;
    if (!knownKeys(root, {QStringLiteral("schema"), QStringLiteral("data_kind"), QStringLiteral("time_reference"),
                         QStringLiteral("complete_curve"), QStringLiteral("direction"), QStringLiteral("parameters"),
                         QStringLiteral("points"), QStringLiteral("description")}, error))
        return fail(QStringLiteral("input_fields"), error);
    if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("sn100-clot-endpoint-input-v1") ||
        root.value(QStringLiteral("time_reference")).toString() != QStringLiteral("seconds-since-reagent-addition") ||
        (root.value(QStringLiteral("data_kind")).toString() != QStringLiteral("synthetic") &&
         root.value(QStringLiteral("data_kind")).toString() != QStringLiteral("anonymized")))
        return fail(QStringLiteral("input_contract"), QStringLiteral("须指定输入schema、synthetic/anonymized及相对加试剂起点的秒单位"));
    if (!root.value(QStringLiteral("complete_curve")).isBool() || !root.value(QStringLiteral("complete_curve")).toBool())
        return fail(QStringLiteral("incomplete_curve"), QStringLiteral("本轮仅处理已完整采集且包含终末平台的离线曲线"));
    CurveDirection direction;
    const QString directionText = root.value(QStringLiteral("direction")).toString();
    if (directionText == QStringLiteral("rising")) direction = CurveDirection::Rising;
    else if (directionText == QStringLiteral("falling")) direction = CurveDirection::Falling;
    else return fail(QStringLiteral("direction"), QStringLiteral("须显式指定 rising 或 falling，不自动推定信号方向"));
    if (!root.value(QStringLiteral("parameters")).isObject() || !root.value(QStringLiteral("points")).isArray())
        return fail(QStringLiteral("input_fields"), QStringLiteral("parameters须为对象，points须为[秒,光学值]数组"));
    const QJsonObject parameters = root.value(QStringLiteral("parameters")).toObject();
    if (!knownKeys(parameters, {QStringLiteral("baseline_points"), QStringLiteral("plateau_points"),
        QStringLiteral("threshold_percent"), QStringLiteral("minimum_amplitude"),
        QStringLiteral("maximum_baseline_range_fraction"), QStringLiteral("maximum_plateau_range_fraction"),
        QStringLiteral("maximum_reference_slope_fraction"),
        QStringLiteral("derivative_window_points"), QStringLiteral("secondary_peak_fraction"),
        QStringLiteral("search_start_seconds"), QStringLiteral("search_end_seconds")}, error))
        return fail(QStringLiteral("parameter_fields"), error);
    EndpointOptions options;
    options.completeCurve = true;
    if (!integerOption(parameters, QStringLiteral("baseline_points"), options.baselinePoints, error) ||
        !integerOption(parameters, QStringLiteral("plateau_points"), options.plateauPoints, error) ||
        !integerOption(parameters, QStringLiteral("derivative_window_points"), options.derivativeWindowPoints, error) ||
        !numberOption(parameters, QStringLiteral("threshold_percent"), options.thresholdPercent, error) ||
        !numberOption(parameters, QStringLiteral("minimum_amplitude"), options.minimumAmplitude, error) ||
        !numberOption(parameters, QStringLiteral("maximum_baseline_range_fraction"), options.maximumBaselineRangeFraction, error) ||
        !numberOption(parameters, QStringLiteral("maximum_plateau_range_fraction"), options.maximumPlateauRangeFraction, error) ||
        !numberOption(parameters, QStringLiteral("maximum_reference_slope_fraction"), options.maximumReferenceSlopeFraction, error) ||
        !numberOption(parameters, QStringLiteral("secondary_peak_fraction"), options.secondaryPeakFraction, error))
        return fail(QStringLiteral("parameter_type"), error);
    const bool hasStart = parameters.contains(QStringLiteral("search_start_seconds"));
    const bool hasEnd = parameters.contains(QStringLiteral("search_end_seconds"));
    if (hasStart != hasEnd) return fail(QStringLiteral("search_window"), QStringLiteral("搜索起止秒数必须同时提供"));
    options.useSearchWindow = hasStart;
    if (!numberOption(parameters, QStringLiteral("search_start_seconds"), options.searchStartSeconds, error) ||
        !numberOption(parameters, QStringLiteral("search_end_seconds"), options.searchEndSeconds, error))
        return fail(QStringLiteral("search_window"), error);
    const QJsonArray points = root.value(QStringLiteral("points")).toArray();
    if (points.size() > 100000) return fail(QStringLiteral("point_limit"), QStringLiteral("本轮最多100000点，属于本地资源预算"));
    QVector<CurvePoint> curve;
    curve.reserve(points.size());
    for (const QJsonValue &entry : points)
    {
        const QJsonArray point = entry.toArray();
        if (!entry.isArray() || point.size() != 2 || !point.at(0).isDouble() || !point.at(1).isDouble() ||
            !std::isfinite(point.at(0).toDouble()) || !std::isfinite(point.at(1).toDouble()))
            return fail(QStringLiteral("point_type"), QStringLiteral("每点必须恰为[有限秒数,有限光学数值]"));
        curve.append({point.at(0).toDouble(), point.at(1).toDouble()});
    }
    QElapsedTimer clock;
    clock.start();
    const EndpointResult percentage = percentageEndpoint(curve, direction, options);
    const double percentageMs = double(clock.nsecsElapsed()) / 1000000.0;
    clock.restart();
    const EndpointResult derivative = maximumDerivativeEndpoint(curve, direction, options);
    const double derivativeMs = double(clock.nsecsElapsed()) / 1000000.0;
    const QJsonObject effectiveParameters{
        {QStringLiteral("baseline_points"), options.baselinePoints},
        {QStringLiteral("plateau_points"), options.plateauPoints},
        {QStringLiteral("threshold_percent"), options.thresholdPercent},
        {QStringLiteral("minimum_amplitude"), options.minimumAmplitude},
        {QStringLiteral("maximum_baseline_range_fraction"), options.maximumBaselineRangeFraction},
        {QStringLiteral("maximum_plateau_range_fraction"), options.maximumPlateauRangeFraction},
        {QStringLiteral("maximum_reference_slope_fraction"), options.maximumReferenceSlopeFraction},
        {QStringLiteral("derivative_window_points"), options.derivativeWindowPoints},
        {QStringLiteral("secondary_peak_fraction"), options.secondaryPeakFraction},
        {QStringLiteral("use_search_window"), options.useSearchWindow},
        {QStringLiteral("search_start_seconds"), options.useSearchWindow ? QJsonValue(options.searchStartSeconds) : QJsonValue()},
        {QStringLiteral("search_end_seconds"), options.useSearchWindow ? QJsonValue(options.searchEndSeconds) : QJsonValue()}};
    const QJsonObject result{{QStringLiteral("schema"), QStringLiteral("sn100-clot-endpoint-result-v1")},
        {QStringLiteral("stage"), QStringLiteral("offline-research-prototype")},
        {QStringLiteral("clinical_validated"), false}, {QStringLiteral("al4a_equivalence_verified"), false},
        {QStringLiteral("hardware_verified"), false}, {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("input_sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("input"), root},
        {QStringLiteral("effective_parameters"), effectiveParameters},
        {QStringLiteral("percentage"), serialize(percentage, QStringLiteral("percentage"), percentageMs)},
        {QStringLiteral("maximum_derivative"), serialize(derivative, QStringLiteral("maximum_derivative"), derivativeMs)}};
    QFile output(outputInfo.absoluteFilePath());
    const QByteArray resultBytes = QJsonDocument(result).toJson(QJsonDocument::Indented);
    // NewOnly原子拒绝同名写入；失败保留诊断，绝不覆盖历史结果。
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(QStringLiteral("output_create"), output.errorString());
    if (output.write(resultBytes) != resultBytes.size() || !output.flush())
    {
        QTextStream(stderr) << "output_write_failed: " << output.errorString() << Qt::endl;
        return 3;
    }
    output.close();
    QTextStream(stdout) << resultBytes << Qt::endl;
    return percentage.ok && derivative.ok ? 0 : 4;
}
