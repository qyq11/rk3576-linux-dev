#ifndef SN100_PRIVATE_QC_HISTORY_CORE_H
#define SN100_PRIVATE_QC_HISTORY_CORE_H

#include "../quality/statistics.h"
#include <QDate>
#include <QList>
#include <QMap>
#include <QString>
#include <QTime>
#include <limits>

namespace Sn100QcHistory
{
// 私有离线预算，不是仪器规格；调用者以后应在SQL worker中有界查询。
constexpr int kMaximumRows = 10000;
constexpr int kMaximumResultCharacters = 65536;
constexpr qint64 kMaximumTotalResultCharacters = 2 * 1024 * 1024;

enum class ResultType { Original = 1, Calibration = 2, Rate = 3, INR = 4 };
QString typeName(ResultType type);

struct HistoryRow
{
    qint64 id = 0;
    QString controlName, projectName, batchName;
    QString date, time, resultValue;
};

struct ParsedValue
{
    bool valid = false;
    QString errorCode = QStringLiteral("missing_result_type");
    QString storedNumber, sampleRate, taskId;
    // 无效值保持NaN，不能从默认构造获得伪0点。
    double number = std::numeric_limits<double>::quiet_NaN();
};

struct ParsedRow
{
    bool envelopeValid = false;
    QString errorCode;
    QString projectName, calibrationId, abnormalType;
    QMap<int, ParsedValue> values;
    ParsedValue value(ResultType type) const;
};

// 只认QC标准四结果及单项目writer合同；未知/重复类型或多项目包不猜归属。
ParsedRow parseResult(const QString &text, const QString &expectedProject);

enum class UnitSource
{
    UnknownLegacy,
    AssociationSnapshotOnly,
    DeclaredAnonymousFixture,
    StoredPerPointSnapshot
};

struct UnitEvidence
{
    qint64 rowId = 0;
    ResultType type = ResultType::Original;
    UnitSource source = UnitSource::UnknownLegacy;
    QString unit, provenance;
    double ratio = std::numeric_limits<double>::quiet_NaN();
    int decimals = 1;
};

struct HistoryPoint
{
    HistoryRow row;
    QDate date;
    QTime time;
    ParsedValue result;
    UnitEvidence unitEvidence;
    bool displayAvailable = false;
    QString unitError = QStringLiteral("historical_unit_unknown");
    double displayValue = std::numeric_limits<double>::quiet_NaN();
    QString displayText;
};

struct ExcludedRow
{
    qint64 id = 0;
    QString reason;
};

struct SelectionRequest
{
    // worker以后从稳定control/association/batch ID解析本库已核名称；
    // 本纯值核心没有catalog或最新单位字典接口，不负责凭同名猜身份。
    QString controlName, projectName, currentBatch, previousBatch;
    ResultType type = ResultType::Original;
    QList<UnitEvidence> historicalUnits;
};

struct Selection
{
    bool ok = false;
    QString errorCode;
    QList<HistoryPoint> previous, current, plotOrder;
    QList<ExcludedRow> excluded;
    int invalidResultCount = 0;
    bool legacyCardVisible = false;
    int initialVisibleBegin = 0;
    int initialVisibleEnd = 0; // exclusive；这是原序号窗口，不声称是最新日期30条。
};

// 保留“前批+当前，各自date DESC”；time DESC/id DESC为明确的同日确定性补全。
Selection selectHistory(const QList<HistoryRow> &rows, const SelectionRequest &request);
// 指针仅在selection存活且未被修改期间有效；异步接线应传值，不缓存这些指针。
const HistoryPoint *pointAtPlotIndex(const Selection &selection, int index);
// 保留原traceData的反向索引供独立对照：初始counter=N-1会指向plotOrder[0]。
const HistoryPoint *legacyTraceAtCounter(const Selection &selection, int counter);

struct Summary
{
    bool ok = false;
    QString errorCode, unit;
    int selectedRows = 0, invalidResultsExcluded = 0, unknownUnits = 0;
    // CV有符号ratio，不是百分数；没有临床正常/警告/失控状态字段。
    Sn100Quality::StatisticsResult statistics;
};

// 数字无效的行有计数地排除；任何有效点单位未知/不同则拒绝混合聚合。
Summary summarize(const Selection &selection);
}
#endif
