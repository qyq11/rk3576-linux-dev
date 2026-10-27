#include "qc_history_core.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace Sn100QcHistory
{
namespace
{
bool knownType(ResultType type)
{
    const int value = static_cast<int>(type);
    return value >= 1 && value <= 4;
}

bool plainName(const QString &name)
{
    return !name.isEmpty() && name.size() <= 256 && name == name.trimmed() &&
        !name.contains('\r') && !name.contains('\n') && !name.contains(QChar(0));
}

bool serializedProjectName(const QString &name)
{
    return plainName(name) && !name.contains(QRegularExpression(QStringLiteral("[\\[\\]|=]")));
}

ParsedRow broken(const QString &code)
{
    ParsedRow result;
    result.errorCode = code;
    return result;
}

ParsedValue parseValue(const QString &number, const QString &detail)
{
    ParsedValue result;
    result.storedNumber = number;
    // 找的是固定metadata分隔符，不按'-'拆数值；负数/科学计数法不丢符号。
    const int dash = detail.indexOf('-');
    if (dash <= 0 || dash == detail.size() - 1 || detail.indexOf('-', dash + 1) >= 0 ||
        detail.contains(QRegularExpression(QStringLiteral("[\\[\\]|=:\\s]"))))
    {
        result.errorCode = QStringLiteral("invalid_result_detail");
        return result;
    }
    result.sampleRate = detail.left(dash);
    result.taskId = detail.mid(dash + 1);
    const QRegularExpression decimal(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?$"));
    if (number.size() > 128 || !decimal.match(number).hasMatch())
    {
        result.errorCode = QStringLiteral("invalid_numeric_value");
        return result;
    }
    bool converted = false;
    const double value = number.toDouble(&converted);
    if (!converted || !std::isfinite(value))
    {
        result.errorCode = QStringLiteral("non_finite_or_overflow_value");
        return result;
    }
    result.number = value;
    result.valid = true;
    result.errorCode.clear();
    return result;
}

using UnitKey = QPair<qint64, int>;

void resolveUnit(HistoryPoint *point, ResultType type, const QMap<UnitKey, UnitEvidence> &units,
                 const QSet<UnitKey> &duplicates)
{
    const UnitKey key(point->row.id, static_cast<int>(type));
    if (duplicates.contains(key))
    {
        point->unitError = QStringLiteral("ambiguous_unit_evidence");
        return;
    }
    if (!units.contains(key)) return;
    point->unitEvidence = units.value(key);
    const UnitEvidence &evidence = point->unitEvidence;
    if (evidence.source == UnitSource::AssociationSnapshotOnly)
    {
        point->unitError = QStringLiteral("association_snapshot_is_not_point_snapshot");
        return;
    }
    if (evidence.source != UnitSource::DeclaredAnonymousFixture &&
        evidence.source != UnitSource::StoredPerPointSnapshot) return;
    if (!plainName(evidence.unit) || evidence.provenance.trimmed().isEmpty() ||
        evidence.provenance.size() > 1024 || !std::isfinite(evidence.ratio) || evidence.ratio <= 0 ||
        evidence.decimals < 0 || evidence.decimals > 16)
    {
        point->unitError = QStringLiteral("invalid_unit_evidence");
        return;
    }
    if (!point->result.valid)
    {
        point->unitError = QStringLiteral("result_invalid");
        return;
    }
    const double converted = point->result.number * evidence.ratio;
    if (!std::isfinite(converted))
    {
        point->unitError = QStringLiteral("unit_conversion_overflow");
        return;
    }
    point->displayValue = converted;
    point->displayText = QString::number(converted, 'f', evidence.decimals) + QStringLiteral(" ") + evidence.unit;
    point->displayAvailable = true;
    point->unitError.clear();
}

bool newestFirst(const HistoryPoint &left, const HistoryPoint &right)
{
    if (left.date != right.date) return left.date > right.date;
    if (left.time != right.time) return left.time > right.time;
    return left.row.id > right.row.id;
}
}

QString typeName(ResultType type)
{
    switch (type)
    {
    case ResultType::Original: return QStringLiteral("Original");
    case ResultType::Calibration: return QStringLiteral("Calibration");
    case ResultType::Rate: return QStringLiteral("Rate");
    case ResultType::INR: return QStringLiteral("INR");
    }
    return {};
}

ParsedValue ParsedRow::value(ResultType type) const
{
    if (!knownType(type))
    {
        ParsedValue invalid;
        invalid.errorCode = QStringLiteral("unsupported_result_type");
        return invalid;
    }
    if (!envelopeValid)
    {
        ParsedValue invalid;
        invalid.errorCode = errorCode;
        return invalid;
    }
    return values.value(static_cast<int>(type));
}

ParsedRow parseResult(const QString &text, const QString &expectedProject)
{
    if (!serializedProjectName(expectedProject)) return broken(QStringLiteral("invalid_expected_project"));
    if (text.size() > kMaximumResultCharacters) return broken(QStringLiteral("result_character_limit"));
    if (text.size() < 3 || text != text.trimmed() || !text.startsWith('[') || !text.endsWith(']') ||
        text.contains(QChar(0)) || text.contains('\r') || text.contains('\n'))
        return broken(QStringLiteral("invalid_envelope"));
    const QString body = text.mid(1, text.size() - 2);
    if (body.contains('[') || body.contains(']')) return broken(QStringLiteral("multiple_or_nested_project_blocks"));
    const int equal = body.indexOf('=');
    if (equal < 0 || body.indexOf('=', equal + 1) >= 0) return broken(QStringLiteral("invalid_project_header"));
    const QStringList header = body.left(equal).split('|', Qt::KeepEmptyParts);
    if (header.size() != 3 || !serializedProjectName(header.at(0))) return broken(QStringLiteral("invalid_project_header"));
    if (header.at(0) != expectedProject) return broken(QStringLiteral("embedded_project_mismatch"));
    const QStringList entries = body.mid(equal + 1).split('|', Qt::KeepEmptyParts);
    if (entries.isEmpty() || entries.size() > 4) return broken(QStringLiteral("result_type_count"));
    ParsedRow result;
    result.projectName = header.at(0);
    result.calibrationId = header.at(1);
    result.abnormalType = header.at(2);
    for (const QString &entry : entries)
    {
        int type = 0;
        QString numericAndDetail;
        for (int candidate = 1; candidate <= 4; ++candidate)
        {
            const QString prefix = typeName(static_cast<ResultType>(candidate)) + '-';
            if (entry.startsWith(prefix))
            {
                type = candidate;
                numericAndDetail = entry.mid(prefix.size());
                break;
            }
        }
        if (!type) return broken(QStringLiteral("unsupported_result_type"));
        if (result.values.contains(type)) return broken(QStringLiteral("duplicate_result_type"));
        const int colon = numericAndDetail.indexOf(':');
        if (colon <= 0 || numericAndDetail.indexOf(':', colon + 1) >= 0)
            return broken(QStringLiteral("invalid_result_entry"));
        result.values.insert(type, parseValue(numericAndDetail.left(colon), numericAndDetail.mid(colon + 1)));
    }
    result.envelopeValid = true;
    return result;
}

Selection selectHistory(const QList<HistoryRow> &rows, const SelectionRequest &request)
{
    Selection selection;
    if (!plainName(request.controlName) || !serializedProjectName(request.projectName) ||
        !plainName(request.currentBatch) || (!request.previousBatch.isEmpty() && !plainName(request.previousBatch)) ||
        request.previousBatch == request.currentBatch || !knownType(request.type))
    {
        selection.errorCode = QStringLiteral("invalid_selection");
        return selection;
    }
    if (rows.size() > kMaximumRows || request.historicalUnits.size() > kMaximumRows * 4)
    {
        selection.errorCode = QStringLiteral("row_or_unit_limit");
        return selection;
    }
    qint64 characters = 0;
    for (const HistoryRow &row : rows) characters += row.resultValue.size();
    if (characters > kMaximumTotalResultCharacters)
    {
        selection.errorCode = QStringLiteral("total_result_character_limit");
        return selection;
    }
    QMap<UnitKey, UnitEvidence> units;
    QSet<UnitKey> duplicateUnits;
    for (const UnitEvidence &evidence : request.historicalUnits)
    {
        const UnitKey key(evidence.rowId, static_cast<int>(evidence.type));
        if (units.contains(key)) duplicateUnits.insert(key);
        units.insert(key, evidence);
    }
    QMap<qint64, int> selectedIdCounts;
    for (const HistoryRow &row : rows)
        if (row.controlName == request.controlName && row.projectName == request.projectName &&
            (row.batchName == request.currentBatch || (!request.previousBatch.isEmpty() && row.batchName == request.previousBatch)))
            ++selectedIdCounts[row.id];
    for (const HistoryRow &row : rows)
    {
        QString excluded;
        if (row.controlName != request.controlName) excluded = QStringLiteral("other_control");
        else if (row.projectName != request.projectName) excluded = QStringLiteral("other_project");
        else if (row.batchName != request.currentBatch &&
                 (request.previousBatch.isEmpty() || row.batchName != request.previousBatch)) excluded = QStringLiteral("other_batch");
        else if (row.id <= 0) excluded = QStringLiteral("invalid_row_id");
        else if (selectedIdCounts.value(row.id) != 1) excluded = QStringLiteral("duplicate_row_id");
        const QDate date = QDate::fromString(row.date, QStringLiteral("yyyy/MM/dd"));
        const QTime time = QTime::fromString(row.time, QStringLiteral("HH:mm:ss"));
        if (excluded.isEmpty() && (!date.isValid() || date.toString(QStringLiteral("yyyy/MM/dd")) != row.date))
            excluded = QStringLiteral("invalid_date");
        if (excluded.isEmpty() && (!time.isValid() || time.toString(QStringLiteral("HH:mm:ss")) != row.time))
            excluded = QStringLiteral("invalid_time");
        if (!excluded.isEmpty())
        {
            selection.excluded.append(ExcludedRow{row.id, excluded});
            continue;
        }
        HistoryPoint point;
        point.row = row;
        point.date = date;
        point.time = time;
        point.result = parseResult(row.resultValue, row.projectName).value(request.type);
        resolveUnit(&point, request.type, units, duplicateUnits);
        if (!point.result.valid) ++selection.invalidResultCount;
        if (row.batchName == request.currentBatch) selection.current.append(point);
        else selection.previous.append(point);
    }
    std::sort(selection.previous.begin(), selection.previous.end(), newestFirst);
    std::sort(selection.current.begin(), selection.current.end(), newestFirst);
    selection.plotOrder = selection.previous;
    selection.plotOrder.append(selection.current);
    selection.legacyCardVisible = !selection.current.isEmpty();
    selection.initialVisibleEnd = selection.plotOrder.size();
    selection.initialVisibleBegin = qMax(0, selection.initialVisibleEnd - 30);
    selection.ok = true;
    return selection;
}

const HistoryPoint *pointAtPlotIndex(const Selection &selection, int index)
{
    if (!selection.ok || index < 0 || index >= selection.plotOrder.size()) return nullptr;
    return &selection.plotOrder.at(index);
}

const HistoryPoint *legacyTraceAtCounter(const Selection &selection, int counter)
{
    if (counter < 0 || counter >= selection.plotOrder.size()) return nullptr;
    return pointAtPlotIndex(selection, selection.plotOrder.size() - counter - 1);
}

Summary summarize(const Selection &selection)
{
    Summary summary;
    if (!selection.ok)
    {
        summary.errorCode = selection.errorCode;
        return summary;
    }
    summary.selectedRows = selection.plotOrder.size();
    QVector<double> values;
    bool mixedUnits = false;
    for (const HistoryPoint &point : selection.plotOrder)
    {
        if (!point.result.valid)
        {
            ++summary.invalidResultsExcluded;
            continue;
        }
        if (!point.displayAvailable)
        {
            ++summary.unknownUnits;
            continue;
        }
        if (summary.unit.isEmpty()) summary.unit = point.unitEvidence.unit;
        else if (summary.unit != point.unitEvidence.unit) mixedUnits = true;
        values.append(point.displayValue);
    }
    if (summary.unknownUnits) summary.errorCode = QStringLiteral("historical_unit_unavailable");
    else if (mixedUnits) summary.errorCode = QStringLiteral("mixed_historical_units");
    if (!summary.errorCode.isEmpty()) return summary;
    summary.statistics = Sn100Quality::calculateStatistics(values);
    summary.ok = summary.statistics.ok;
    summary.errorCode = summary.statistics.errorCode;
    return summary;
}
}
