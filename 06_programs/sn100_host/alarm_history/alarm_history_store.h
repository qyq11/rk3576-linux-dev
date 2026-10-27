#ifndef SN100_ALARM_HISTORY_STORE_H
#define SN100_ALARM_HISTORY_STORE_H

#include <QDate>
#include <QElapsedTimer>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <memory>

class QSqlQuery;
class QTimer;

namespace AlarmHistory
{
enum class Error
{
    InvalidRequest, Busy, WrongThread, OpenFailed, IncompatibleSchema,
    IncompatibleData, ResourceLimit, SqlFailure
};
enum class Scope { LatestToday20, DateInterval };
enum class Completion { RecentTodayComplete, IntervalComplete };

struct Key
{
    quint64 requestId = 0;
    quint64 viewGeneration = 0;
    QString sourceToken;
};
inline bool operator==(const Key &left, const Key &right)
{
    return left.requestId == right.requestId && left.viewGeneration == right.viewGeneration &&
        left.sourceToken == right.sourceToken;
}
struct Request
{
    Key key;
    Scope scope = Scope::DateInterval;
    QDate first;
    QDate last;
    QDate today; // Captured by caller on page open, not recomputed during a request.
    int batchSize = 128;
};
struct Row
{
    qint64 id = 0;
    QString errorCode;
    QString description;
    QString measure;
    QString dateTime;
    QString correctiveAction;
    QString user;
};
using Rows = QList<Row>;

// Move a parentless instance to its SQL thread before calling any slot.
// No write/DDL/migration, live-alarm state or device command API exists here.
class Store : public QObject
{
    Q_OBJECT
public:
    explicit Store(QObject *parent = nullptr);
    ~Store() override;
    static void registerTypes();

public slots:
    void openExisting(QString absolutePath);
    void begin(AlarmHistory::Request request);
    void advance(AlarmHistory::Key key, quint64 continuationStep);
    void cancel(AlarmHistory::Key key);
    void close();

signals:
    void opened(QString sourceToken, QString canonicalPath);
    void openFailed(AlarmHistory::Error error, QString message);
    void continuationNeeded(AlarmHistory::Request request, qint64 validatedRows, quint64 continuationStep);
    void batchReady(AlarmHistory::Request request, AlarmHistory::Rows rows, bool more, quint64 continuationStep);
    void finished(AlarmHistory::Request request, AlarmHistory::Completion completion,
                  qint64 deliveredRows, qint64 matchingRows);
    void failed(AlarmHistory::Request request, AlarmHistory::Error error,
                QString message, qint64 deliveredRows);
    void cancelled(AlarmHistory::Request request, qint64 deliveredRows);
    void requestRejected(AlarmHistory::Key key, AlarmHistory::Error error, QString message);
    void closed();

private:
    enum class Phase { Idle, Validating, Reading };
    bool inOwnerThread() const;
    bool active() const;
    bool checkKey(const Key &key);
    bool compatibleSchema(Error *error, QString *message);
    bool decode(QSqlQuery &query, Row *row, Error *error, QString *message) const;
    bool withinBudget();
    void validateBatch();
    void prepareRows();
    void readBatch();
    void complete();
    void fail(Error error, const QString &message);
    void clearRequest();
    void release();
    QSqlDatabase m_database;
    std::unique_ptr<QSqlQuery> m_scan;
    std::unique_ptr<QSqlQuery> m_rows;
    QString m_connectionName;
    QString m_sourceToken;
    Request m_request;
    Phase m_phase = Phase::Idle;
    QTimer *m_deadline;
    QElapsedTimer m_elapsed;
    quint64 m_lastRequestId = 0;
    quint64 m_stepCounter = 0;
    quint64 m_awaitingStep = 0;
    qint64 m_validated = 0;
    qint64 m_matching = 0;
    qint64 m_expected = 0;
    qint64 m_delivered = 0;
    bool m_transaction = false;
};
}

Q_DECLARE_METATYPE(AlarmHistory::Error)
Q_DECLARE_METATYPE(AlarmHistory::Scope)
Q_DECLARE_METATYPE(AlarmHistory::Completion)
Q_DECLARE_METATYPE(AlarmHistory::Key)
Q_DECLARE_METATYPE(AlarmHistory::Request)
Q_DECLARE_METATYPE(AlarmHistory::Row)
Q_DECLARE_METATYPE(AlarmHistory::Rows)
#endif
