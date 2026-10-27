#include "../quality_control/qc_history_core.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <cmath>
#include <functional>
#include <limits>

using namespace Sn100QcHistory;

namespace
{
int checks = 0, failures = 0;
QJsonArray cases;
QString currentCase;

bool check(bool condition, const char *expression, int line)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        QTextStream(stderr) << currentCase << ':' << line << ' ' << expression << Qt::endl;
    }
    return condition;
}
#define CHECK(expression) do { if (!check(bool(expression), #expression, __LINE__)) return; } while (false)

void run(const QString &name, const std::function<void()> &body)
{
    currentCase = name;
    const int before = failures, beforeChecks = checks;
    body();
    cases.append(QJsonObject{{"name", name}, {"checks", checks - beforeChecks}, {"passed", before == failures}});
}

bool near(double value, double expected)
{
    return std::isfinite(value) && std::fabs(value - expected) <= 1e-12;
}

QString encoded(const QString &number, const QString &project = QStringLiteral("PROJ_QC_DEMO"))
{
    return '[' + project + QStringLiteral("|0|=Original-") + number + QStringLiteral(":1/1-101]");
}

HistoryRow row(qint64 id, const QString &batch, const QString &date, const QString &number)
{
    HistoryRow result;
    result.id = id;
    result.controlName = QStringLiteral("QC_DEMO");
    result.projectName = QStringLiteral("PROJ_QC_DEMO");
    result.batchName = batch;
    result.date = date;
    result.time = QStringLiteral("10:00:00");
    result.resultValue = encoded(number);
    return result;
}

QList<HistoryRow> goldenRows()
{
    return {row(101, "LOT_PREV", "2026/10/23", "10"), row(102, "LOT_CUR", "2026/10/24", "12"),
            row(103, "LOT_CUR", "2026/10/25", "14")};
}

SelectionRequest requestFor(const QList<HistoryRow> &rows, double ratio = 1.0)
{
    SelectionRequest request;
    request.controlName = QStringLiteral("QC_DEMO");
    request.projectName = QStringLiteral("PROJ_QC_DEMO");
    request.currentBatch = QStringLiteral("LOT_CUR");
    request.previousBatch = QStringLiteral("LOT_PREV");
    for (const HistoryRow &input : rows)
    {
        UnitEvidence unit;
        unit.rowId = input.id;
        unit.type = ResultType::Original;
        unit.source = UnitSource::DeclaredAnonymousFixture;
        unit.provenance = QStringLiteral("anonymous-handcrafted-per-row-test-declaration");
        unit.unit = QStringLiteral("ANON_UNIT");
        unit.ratio = ratio;
        unit.decimals = 1;
        request.historicalUnits.append(unit);
    }
    return request;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    run("four-types-and-real-envelope", []()
    {
        const ParsedRow parsed = parseResult("[PROJ_QC_DEMO|9|0=Original-10:1/1-101|Calibration-120:1/1-101|Rate-1.2:1/1-101|INR-1.4:1/1-101]", "PROJ_QC_DEMO");
        CHECK(parsed.envelopeValid);
        CHECK(parsed.values.size() == 4);
        CHECK(parsed.calibrationId == "9" && parsed.abnormalType == "0");
        CHECK(near(parsed.value(ResultType::Original).number, 10));
        CHECK(near(parsed.value(ResultType::Calibration).number, 120));
        CHECK(near(parsed.value(ResultType::Rate).number, 1.2));
        CHECK(near(parsed.value(ResultType::INR).number, 1.4));
        CHECK(parsed.value(ResultType::Original).sampleRate == "1/1");
        CHECK(parsed.value(ResultType::Original).taskId == "101");
    });
    run("missing-type-is-invalid-not-zero", []()
    {
        const ParsedRow parsed = parseResult("[PROJ_QC_DEMO||=Calibration-120:1/1-101]", "PROJ_QC_DEMO");
        CHECK(parsed.envelopeValid);
        CHECK(parsed.value(ResultType::Calibration).valid);
        CHECK(!parsed.value(ResultType::Original).valid);
        CHECK(parsed.value(ResultType::Original).errorCode == "missing_result_type");
        CHECK(std::isnan(parsed.value(ResultType::Original).number));
    });
    run("invalid-nonfinite-and-overflow-never-zero", []()
    {
        for (const QString &number : {QString("****"), QString("NaN"), QString("Inf"), QString("1e309"), QString("1,5"), QString(" 12")})
        {
            const ParsedValue value = parseResult(encoded(number), "PROJ_QC_DEMO").value(ResultType::Original);
            CHECK(!value.valid);
            CHECK(!value.errorCode.isEmpty());
            CHECK(std::isnan(value.number));
        }
    });
    run("signed-and-exponent-values-retain-sign", []()
    {
        CHECK(near(parseResult(encoded("-1"), "PROJ_QC_DEMO").value(ResultType::Original).number, -1));
        CHECK(near(parseResult(encoded("1e-2"), "PROJ_QC_DEMO").value(ResultType::Original).number, 0.01));
        CHECK(near(parseResult(encoded("+2.5"), "PROJ_QC_DEMO").value(ResultType::Original).number, 2.5));
    });
    run("unsupported-and-duplicate-types-fail-closed", []()
    {
        for (const QString &body : {QString("MDA-10:1/1-1"), QString("Formula-10:1/1-1"), QString("Time-10:1/1-1"),
             QString("Original-10:1/1-1|Original-12:1/1-2")})
        {
            const ParsedRow parsed = parseResult("[PROJ_QC_DEMO||=" + body + "]", "PROJ_QC_DEMO");
            CHECK(!parsed.envelopeValid);
            CHECK(!parsed.value(ResultType::Original).valid);
            CHECK(std::isnan(parsed.value(ResultType::Original).number));
        }
    });
    run("bounded-malformed-envelope", []()
    {
        for (const QString &text : {QString(""), QString("broken"), QString("[]"), QString("[PROJ_QC_DEMO||]"),
             QString("[PROJ_QC_DEMO||=Original-10]"), encoded("10") + encoded("11"),
             QString("[PROJ_QC_DEMO||=Original-10:1/1-1|]")})
            CHECK(!parseResult(text, "PROJ_QC_DEMO").envelopeValid);
        CHECK(parseResult(QString(kMaximumResultCharacters + 1, 'x'), "PROJ_QC_DEMO").errorCode == "result_character_limit");
        CHECK(parseResult(encoded("10", "OTHER"), "PROJ_QC_DEMO").errorCode == "embedded_project_mismatch");
        CHECK(!parseResult("[PROJ_QC_DEMO||=Original-10:bad-detail-extra]", "PROJ_QC_DEMO").value(ResultType::Original).valid);
    });
    run("control-project-current-previous-isolation", []()
    {
        QList<HistoryRow> rows = goldenRows();
        HistoryRow other = row(201, "LOT_CUR", "2026/10/26", "999");
        other.controlName = "OTHER_CONTROL"; rows.append(other);
        other.id = 202; other.controlName = "QC_DEMO"; other.projectName = "OTHER_PROJECT"; rows.append(other);
        other.id = 203; other.projectName = "PROJ_QC_DEMO"; other.batchName = "OTHER_BATCH"; rows.append(other);
        const Selection selected = selectHistory(rows, requestFor(goldenRows()));
        CHECK(selected.ok && selected.plotOrder.size() == 3);
        CHECK(selected.excluded.size() == 3);
        CHECK(selected.excluded.at(0).reason == "other_control");
        CHECK(selected.excluded.at(1).reason == "other_project");
        CHECK(selected.excluded.at(2).reason == "other_batch");
        CHECK(near(summarize(selected).statistics.mean, 12));
    });
    run("date-time-rowid-descending-with-batch-concatenation", []()
    {
        QList<HistoryRow> rows = goldenRows();
        HistoryRow later = row(104, "LOT_CUR", "2026/10/25", "16");
        later.time = "11:00:00"; rows.append(later);
        HistoryRow sameTime = later; sameTime.id = 105; sameTime.resultValue = encoded("18"); rows.prepend(sameTime);
        const Selection selected = selectHistory(rows, requestFor(rows));
        CHECK(selected.plotOrder.size() == 5);
        CHECK(selected.plotOrder.at(0).row.id == 101);
        CHECK(selected.plotOrder.at(1).row.id == 105);
        CHECK(selected.plotOrder.at(2).row.id == 104);
        CHECK(selected.plotOrder.at(3).row.id == 103);
        CHECK(selected.plotOrder.at(4).row.id == 102);
    });
    run("legacy-trace-suspicion-remains-explicit", []()
    {
        const Selection selected = selectHistory(goldenRows(), requestFor(goldenRows()));
        const HistoryPoint *first = legacyTraceAtCounter(selected, 2);
        CHECK(first && first->row.id == 101);
        CHECK(pointAtPlotIndex(selected, 1) && pointAtPlotIndex(selected, 1)->row.id == 103);
        CHECK(!pointAtPlotIndex(selected, -1) && !pointAtPlotIndex(selected, 3));
        CHECK(!legacyTraceAtCounter(selected, -1) && !legacyTraceAtCounter(selected, 3));
    });
    run("golden-10-12-14-and-cv-ratio", []()
    {
        const Selection selected = selectHistory(goldenRows(), requestFor(goldenRows()));
        const Summary summary = summarize(selected);
        CHECK(summary.ok && selected.legacyCardVisible);
        CHECK(summary.selectedRows == 3 && summary.invalidResultsExcluded == 0 && summary.unknownUnits == 0);
        CHECK(summary.statistics.count == 3);
        CHECK(near(summary.statistics.mean, 12));
        CHECK(near(summary.statistics.sampleVariance, 4));
        CHECK(near(summary.statistics.sampleStddev, 2));
        CHECK(near(summary.statistics.legacyCvRatio, 2.0 / (12.0 + 1e-12)));
        CHECK(summary.statistics.cvAvailable);
        CHECK(QString::number(summary.statistics.legacyCvRatio, 'f', 1) == "0.2");
        CHECK(selected.plotOrder.at(0).displayText == "10.0 ANON_UNIT");
    });
    run("integer-and-fractional-unit-ratios", []()
    {
        const Summary twice = summarize(selectHistory(goldenRows(), requestFor(goldenRows(), 2)));
        CHECK(twice.ok && near(twice.statistics.mean, 24) && near(twice.statistics.sampleStddev, 4));
        CHECK(near(twice.statistics.legacyCvRatio, 1.0 / 6));
        const Selection half = selectHistory(goldenRows(), requestFor(goldenRows(), 0.5));
        const Summary summary = summarize(half);
        CHECK(half.ok && half.plotOrder.size() == 3);
        CHECK(summary.ok && near(summary.statistics.mean, 6) && near(summary.statistics.sampleStddev, 1));
        CHECK(half.plotOrder.at(0).displayText == "5.0 ANON_UNIT");
        CHECK(near(pointAtPlotIndex(half, 1)->displayValue, 7));
        CHECK(near(legacyTraceAtCounter(half, 2)->displayValue, 5));
    });
    run("unknown-and-association-units-never-pretend-point-snapshots", []()
    {
        SelectionRequest request = requestFor(goldenRows());
        request.historicalUnits.clear();
        Selection selected = selectHistory(goldenRows(), request);
        CHECK(selected.ok && selected.plotOrder.size() == 3);
        CHECK(selected.plotOrder.at(0).result.valid && near(selected.plotOrder.at(0).result.number, 10));
        CHECK(!selected.plotOrder.at(0).displayAvailable && std::isnan(selected.plotOrder.at(0).displayValue));
        CHECK(selected.plotOrder.at(0).unitError == "historical_unit_unknown");
        CHECK(summarize(selected).errorCode == "historical_unit_unavailable");
        request = requestFor(goldenRows());
        for (UnitEvidence &unit : request.historicalUnits) unit.source = UnitSource::AssociationSnapshotOnly;
        selected = selectHistory(goldenRows(), request);
        CHECK(selected.ok && selected.plotOrder.size() == 3);
        CHECK(selected.plotOrder.at(0).unitError == "association_snapshot_is_not_point_snapshot");
        CHECK(!summarize(selected).ok && summarize(selected).unknownUnits == 3);
    });
    run("invalid-duplicate-and-mixed-unit-evidence", []()
    {
        SelectionRequest request = requestFor(goldenRows());
        request.historicalUnits[0].ratio = 0;
        CHECK(!summarize(selectHistory(goldenRows(), request)).ok);
        request = requestFor(goldenRows()); request.historicalUnits[0].ratio = std::numeric_limits<double>::infinity();
        Selection selected = selectHistory(goldenRows(), request);
        CHECK(selected.previous.size() == 1);
        CHECK(selected.previous.first().unitError == "invalid_unit_evidence");
        request = requestFor(goldenRows()); request.historicalUnits.append(request.historicalUnits.first());
        selected = selectHistory(goldenRows(), request);
        CHECK(selected.previous.size() == 1);
        CHECK(selected.previous.first().unitError == "ambiguous_unit_evidence");
        request = requestFor(goldenRows()); request.historicalUnits[0].unit = "ANOTHER_UNIT";
        CHECK(summarize(selectHistory(goldenRows(), request)).errorCode == "mixed_historical_units");
        request = requestFor(goldenRows()); request.historicalUnits[0].decimals = 1000;
        selected = selectHistory(goldenRows(), request);
        CHECK(selected.previous.size() == 1);
        CHECK(selected.previous.first().unitError == "invalid_unit_evidence");
    });
    run("invalid-results-explicitly-counted-not-zero", []()
    {
        QList<HistoryRow> rows = goldenRows(); rows[0].resultValue = encoded("****");
        const Selection selected = selectHistory(rows, requestFor(rows));
        const Summary summary = summarize(selected);
        CHECK(selected.previous.size() == 1);
        CHECK(selected.invalidResultCount == 1 && std::isnan(selected.previous.first().result.number));
        CHECK(summary.ok && summary.selectedRows == 3 && summary.invalidResultsExcluded == 1);
        CHECK(summary.statistics.count == 2 && near(summary.statistics.mean, 13));
        for (HistoryRow &item : rows) item.resultValue = encoded("****");
        const Summary none = summarize(selectHistory(rows, requestFor(rows)));
        CHECK(!none.ok && none.invalidResultsExcluded == 3 && none.errorCode == "empty_series");
    });
    run("thirty-five-points-statistics-not-visible-window", []()
    {
        QList<HistoryRow> rows;
        for (int day = 0; day < 35; ++day)
            rows.append(row(day + 1, "LOT_CUR", QDate(2026, 1, 1).addDays(day).toString("yyyy/MM/dd"), QString::number(day + 1)));
        const Selection selected = selectHistory(rows, requestFor(rows));
        const Summary summary = summarize(selected);
        CHECK(selected.plotOrder.size() == 35 && selected.initialVisibleBegin == 5 && selected.initialVisibleEnd == 35);
        CHECK(selected.plotOrder.first().row.id == 35 && selected.plotOrder.last().row.id == 1);
        CHECK(summary.ok && summary.statistics.count == 35 && near(summary.statistics.mean, 18));
        CHECK(near(summary.statistics.sampleVariance, 105));
        CHECK(near(summary.statistics.sampleStddev, std::sqrt(105.0)));
    });
    run("single-point-and-zero-mean-availability", []()
    {
        QList<HistoryRow> rows{row(1, "LOT_CUR", "2026/10/25", "9")};
        Summary summary = summarize(selectHistory(rows, requestFor(rows)));
        CHECK(summary.ok && near(summary.statistics.mean, 9));
        CHECK(!summary.statistics.sampleEstimateAvailable && !summary.statistics.cvAvailable);
        rows = {row(1,"LOT_CUR","2026/10/23","-1"),row(2,"LOT_CUR","2026/10/24","0"),row(3,"LOT_CUR","2026/10/25","1")};
        summary = summarize(selectHistory(rows, requestFor(rows)));
        CHECK(summary.ok && near(summary.statistics.mean, 0) && near(summary.statistics.sampleStddev, 1));
        CHECK(summary.statistics.sampleEstimateAvailable && !summary.statistics.cvAvailable);
    });
    run("constant-empty-and-previous-only", []()
    {
        QList<HistoryRow> rows = goldenRows();
        for (HistoryRow &input : rows) input.resultValue = encoded("7");
        Summary summary = summarize(selectHistory(rows, requestFor(rows)));
        CHECK(summary.ok && near(summary.statistics.sampleStddev, 0) && near(summary.statistics.legacyCvRatio, 0));
        const Selection empty = selectHistory({}, requestFor({}));
        CHECK(empty.ok && !empty.legacyCardVisible && !summarize(empty).ok);
        rows = {row(1, "LOT_PREV", "2026/10/25", "10")};
        const Selection previousOnly = selectHistory(rows, requestFor(rows));
        CHECK(previousOnly.ok && !previousOnly.legacyCardVisible && previousOnly.previous.size() == 1);
    });
    run("invalid-dates-time-and-row-identity", []()
    {
        QList<HistoryRow> rows = goldenRows();
        rows[0].date = "2026/02/30"; rows[1].time = "25:00:00"; rows[2].id = 0;
        Selection selected = selectHistory(rows, requestFor(rows));
        CHECK(selected.ok && selected.plotOrder.isEmpty() && selected.excluded.size() == 3);
        CHECK(selected.excluded.at(0).reason == "invalid_date");
        CHECK(selected.excluded.at(1).reason == "invalid_time");
        CHECK(selected.excluded.at(2).reason == "invalid_row_id");
        rows = goldenRows(); rows[1].id = rows[2].id;
        selected = selectHistory(rows, requestFor(rows));
        CHECK(selected.plotOrder.size() == 1 && selected.excluded.size() == 2);
        CHECK(selected.excluded.at(0).reason == "duplicate_row_id");
        rows = goldenRows(); rows[0].date = "2026/1/01";
        selected = selectHistory(rows, requestFor(rows));
        CHECK(selected.excluded.size() == 1);
        CHECK(selected.excluded.first().reason == "invalid_date");
    });
    run("invalid-requests-and-input-budgets", []()
    {
        SelectionRequest request = requestFor({}); request.previousBatch = request.currentBatch;
        CHECK(!selectHistory({}, request).ok);
        request = requestFor({}); request.type = static_cast<ResultType>(5);
        CHECK(!selectHistory({}, request).ok);
        QList<HistoryRow> rows;
        const HistoryRow example = row(1, "LOT_CUR", "2026/10/25", "10");
        for (int i = 0; i <= kMaximumRows; ++i) rows.append(example);
        CHECK(selectHistory(rows, requestFor({})).errorCode == "row_or_unit_limit");
        rows.clear();
        for (int i = 0; i < 33; ++i)
        {
            HistoryRow input = example; input.id = i + 1; input.resultValue = QString(kMaximumResultCharacters, 'x'); rows.append(input);
        }
        CHECK(selectHistory(rows, requestFor({})).errorCode == "total_result_character_limit");
    });
    run("pure-statistics-reused-guards", []()
    {
        CHECK(!Sn100Quality::calculateStatistics({}).ok);
        CHECK(!Sn100Quality::calculateStatistics({std::numeric_limits<double>::quiet_NaN()}).ok);
        CHECK(!Sn100Quality::calculateStatistics({std::numeric_limits<double>::infinity()}).ok);
        const auto negative = Sn100Quality::calculateStatistics({-12,-10,-8});
        CHECK(negative.ok && negative.cvAvailable && near(negative.legacyCvRatio, -0.2));
    });
    const QJsonObject report{{"schema","sn100-private-qc-history-tests-v1"},{"anonymous",true},
        {"qt_version",QString::fromLatin1(qVersion())},{"cases",cases},{"case_count",cases.size()},
        {"checks",checks},{"failed_checks",failures},{"clinical_rule_evaluation",false},
        {"database_or_network_or_gui",false},{"cv_unit","ratio-not-percent"}};
    QTextStream(stdout) << QJsonDocument(report).toJson(QJsonDocument::Indented);
    return failures == 0 ? 0 : 1;
}
