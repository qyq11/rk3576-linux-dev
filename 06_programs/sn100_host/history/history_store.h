#ifndef HISTORY_STORE_H
#define HISTORY_STORE_H

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <memory>
#include "../data/account_store.h"
#include "report_remaining_view.h"
#include "../projects/project_current_format.h"

struct HistoryFilter
{
    bool byDate = true; // Original dialog validation flag; DAL still uses dates.
    QDateTime start = QDateTime(QDate::currentDate(), QTime(0, 0));
    QDateTime end = QDateTime(QDate::currentDate(), QTime(23, 59, 59));
    // Preserve the original DAL: valid start < end adds inclusive BETWEEN,
    // regardless of byDate; equal times omit the date condition.
    QString sampleNumber; // Exact report_data.sn; empty means no sample-number filter.
    QString barcode;      // Exact report_data.sample_id; empty means no barcode filter.
    int offset = 0;
    int limit = 20;        // Original screen page size; accepted range is 1..100.
};

// Only the fields edited by MaskDlgReportDetail::on_btnSave_clicked are writable.
// reportId identifies report_data.id, never the legacy row_id or task_id.
struct HistoryPatient
{
    qint64 reportId = 0;
    QString name;
    int sex = -1; // Original SexType: -1 unknown, 0 male, 1 female.
    int age = 0;
    QString department;
    QString patientType;
    QString applicationSerial;
    QString bedNumber;
    QString examiner; // report_data.jianyan (original C++ member: jinayan).
    QString sender;   // report_data.checker, original UI label: 送检.
    QString auditor;
    QString remark;
};

enum class HistoryValueFormatOrigin
{
    AnonymousFixture,
    CurrentCommitted,
    UnknownCurrent
};

struct HistoryCurrentFormatSource
{
    QString historyDatasetUuid; // Existing result store UUID; no catalog UUID is invented.
    QString catalogDirectory;
    QString catalogDatabasePath;
    QString catalogVersion;
    QString formatVersion;
    QString fingerprint;
    quint64 sourceGeneration = 0;
    quint64 requestRevision = 0;
    QDateTime capturedAtUtc;
    QString configurationNotice;
};
Q_DECLARE_METATYPE(HistoryCurrentFormatSource)

struct HistoryResultValue
{
    int resultType = -1; // Original ResultType IDs 1..6; no algorithm runs here.
    QString label;
    QString unit;
    QString rawValue;
    int decimals = -1;   // -1: no known configuration; preserve raw text.
    double factor = 1.0; // Fixture's explicit unit scale, only for types 2 and 3.
    HistoryValueFormatOrigin formatOrigin = HistoryValueFormatOrigin::AnonymousFixture;
    QString currentDisplayValue;
    QString formatMessage;
    int sourceResultId = 0;
    bool selectionKnown = false;
    bool used = false;
    bool display = false;
    bool mainResult = false;
    HistoryCurrentFormatSource currentSource;
};
using HistoryResultValues = QList<HistoryResultValue>;

struct HistoryResult
{
    qint64 taskId = 0;
    QString project;
    QDateTime testedAt;
    QString abnormalType;
    HistoryResultValues values;
    // All four persisted strings remain traceable even when a known selection
    // excludes a type from the vertical display list above. Never written back.
    HistoryResultValues storedValues;
    qint64 projectId = 0;
    QString instrumentProjectId;
};
using HistoryResults = QList<HistoryResult>;

struct HistoryReport
{
    qint64 id = 0;
    qint64 rowId = 0;
    int sampleNumber = 0;
    QString barcode;
    QDateTime dateTime;
    QString state;
    HistoryPatient patient;
    HistoryResults results;
};
using HistoryReports = QList<HistoryReport>;
using HistoryIds = QList<qint64>;

Q_DECLARE_METATYPE(HistoryFilter)
Q_DECLARE_METATYPE(HistoryPatient)
Q_DECLARE_METATYPE(HistoryResultValue)
Q_DECLARE_METATYPE(HistoryResultValues)
Q_DECLARE_METATYPE(HistoryResult)
Q_DECLARE_METATYPE(HistoryResults)
Q_DECLARE_METATYPE(HistoryReport)
Q_DECLARE_METATYPE(HistoryReports)
Q_DECLARE_METATYPE(HistoryIds)

enum class HistoryReservationMode
{
    // The task owner still admits reset/retest against its execution state.
    // This store enforces identity/write semantics; it does not schedule a device.
    Initial,
    ErrorResetSameIds,
    RetestNewIds
};

struct HistoryTaskReservation
{
    QString taskType;       // Must explicitly equal SAM; no default clinical task kind.
    QString mdaName;        // This first slice requires the upstream non-MDA value to be empty.
    qint64 taskId = 0;
    qint64 reportRowId = 0; // Explicit task-side group, never report_data.id.
    qint64 projectId = 0;   // Stable catalog project_base.id.
    QString instrumentProjectId;
    QString projectName;
    QString sampleId;
    QString dilution;
    QDateTime reservedAt;
};
using HistoryTaskReservations = QList<HistoryTaskReservation>;

struct HistoryReportReservation
{
    HistoryReport report;
    bool automaticNumber = false; // Explicit caller metadata; not inferred from a sample id.
    QString sampleHole;
};
using HistoryReportReservations = QList<HistoryReportReservation>;

struct HistoryReservationBatch
{
    HistoryReservationMode mode = HistoryReservationMode::Initial;
    // id/patient.reportId must be zero: SQL report ids are returned after commit.
    // Grouping and positive task/row ids are explicit inputs, not a scheduler.
    HistoryReportReservations reports;
    HistoryTaskReservations tasks;
};

struct HistoryComputedResult
{
    QString taskType;
    QString mdaName;
    qint64 taskId = 0;
    qint64 reportRowId = 0;
    qint64 projectId = 0;
    QString instrumentProjectId;
    QString projectName;
    QString sampleId;
    QString dilution;
    QString calibrationId;
    QString abnormalType;
    QDateTime testedAt;
    QString original;
    QString calibration;
    QString rate;
    QString inr;
};
Q_DECLARE_METATYPE(HistoryTaskReservation)
Q_DECLARE_METATYPE(HistoryTaskReservations)
Q_DECLARE_METATYPE(HistoryReportReservation)
Q_DECLARE_METATYPE(HistoryReportReservations)
Q_DECLARE_METATYPE(HistoryReservationBatch)
Q_DECLARE_METATYPE(HistoryComputedResult)

class ResultProjectReferences;

// Construct without a parent, move to a QThread, and call through queued
// connections. initialize/use/close SQL all execute on that worker thread.
class HistoryStore : public QObject
{
    Q_OBJECT

public:
    explicit HistoryStore(QObject *parent = nullptr);
    ~HistoryStore() override;
    static void registerTypes();
    static QString displayValue(const HistoryResultValue &value);
    static QString sexText(int sex);

public slots:
    // Explicit offline-only directory. Refuses reference paths, symlinks,
    // nonempty unmarked directories and databases without this schema marker.
    // New data are seeded only from :/fixtures/history_reports.json.
    void initialize(QString directory);
    // Empty local-result profile, separate from the legacy anonymous fixture.
    // Explicit absolute sibling directories under an existing canonical parent;
    // catalog must already be marked/valid. No fixture seed, path discovery or
    // migration of a private-candidate/foreign history database.
    void initializeLocalResults(QString historyDirectory, QString catalogDirectory);
    void reserveLocalTasks(qint64 requestId, HistoryReservationBatch batch);
    void commitComputedResult(qint64 requestId, HistoryComputedResult result);
    void query(qint64 requestId, HistoryFilter filter);
    void detail(qint64 requestId, qint64 reportId);
    void savePatient(qint64 requestId, HistoryPatient patient);
    // Local anonymous identity only; Guest cannot hide reports or write display preferences.
    void setSession(AccountSession session);
    // Original deletion is visible=0, never a physical cascade into results/tasks.
    void hideReports(qint64 requestId, HistoryIds reportIds);
    void loadDisplay(qint64 requestId);
    void saveDisplay(qint64 requestId, HistoryDisplayColumns columns);
    // Empty selectedIds exports every matching report, independent of page.
    // Nonempty selectedIds are limited to 100 unique, matching report IDs.
    // Output stays inside the marked directory's exports/ folder, in GBK.
    void exportCsv(qint64 requestId, HistoryFilter filter,
                   HistoryIds selectedIds, QString path);
    void close();

signals:
    void ready(qint64 requestId, QString databasePath); // Lifecycle ID is 0.
    void queryReady(qint64 requestId, HistoryFilter filter,
                    HistoryReports reports, qint64 totalCount);
    void detailReady(qint64 requestId, HistoryReport report);
    void patientSaved(qint64 requestId, HistoryPatient patient);
    void localTasksReserved(qint64 requestId, HistoryIds actualReportIds);
    void computedResultCommitted(qint64 requestId, HistoryComputedResult persisted);
    void reportsHidden(qint64 requestId, HistoryIds reportIds);
    void displayLoaded(qint64 requestId, HistoryDisplayColumns columns, HistoryDisplayColumns defaults);
    void displaySaved(qint64 requestId, HistoryDisplayColumns columns);
    void csvExported(qint64 requestId, QString path,
                     qint64 reportCount, qint64 rowCount);
    void error(qint64 requestId, QString operation, QString message);
    // A consumer must clear the matching active request's old current-format
    // values at request start/error. Old request IDs must not clear newer data.
    // Request 0 globally invalidates the source before owner-thread release or
    // initialization, including failed reopening. It does not mean closed().
    void currentFormatInvalidated(qint64 requestId, quint64 requestRevision, QString reason);
    void currentFormatRead(qint64 requestId, HistoryCurrentFormatSource source);
    void closed();

private:
    bool checkThread(qint64 requestId, const QString &operation);
    bool checkReady(qint64 requestId, const QString &operation);
    bool validateFilter(const HistoryFilter &filter, QString *message) const;
    bool prepareDirectory(const QString &directory, QString *message);
    bool createAndSeed(QString *message);
    bool validateDatabase(QString *message);
    bool readReport(qint64 id, HistoryReport *report, QString *message,
                    const ProjectCurrentFormatSnapshot *format = nullptr);
    bool readResults(qint64 rowId, HistoryResults *results, QString *message,
                     const ProjectCurrentFormatSnapshot *format = nullptr);
    bool readCurrentFormat(ProjectCurrentFormatSnapshot *format, QString *message);
    bool readCurrentResults(qint64 rowId, const ProjectCurrentFormatSnapshot &format,
                            HistoryResults *results, QString *message);
    HistoryCurrentFormatSource currentFormatSource(const ProjectCurrentFormatSnapshot &format) const;
    bool displayDefaults(HistoryDisplayColumns *columns, QString *message,
                         const ProjectCurrentFormatSnapshot *format = nullptr);
    bool readDisplay(HistoryDisplayColumns *columns, HistoryDisplayColumns *defaults, QString *message,
                     const ProjectCurrentFormatSnapshot *format = nullptr);
    bool canWriteRemaining(qint64 requestId, const QString &operation);
    void releaseDatabase(QString reason = QString());
    bool checkResultIngest(qint64 requestId, const QString &operation);
    bool createEmptyResultSchema(QString *message);
    bool validateResultSchema(QString *message);
    bool validateReservations(const HistoryReservationBatch &batch, QString *message) const;
    bool resultProject(qint64 projectId, const QString &name, const QString &instrumentId,
                       bool requireEnabled, QString *message);

    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_directory;
    bool m_ready = false;
    AccountSession m_session;
    bool m_resultIngest = false;
    QString m_resultStoreId;
    QString m_resultCatalogDirectory;
    std::unique_ptr<ResultProjectReferences> m_resultProjects;
    quint64 m_formatRequestRevision = 0;
};

#endif
