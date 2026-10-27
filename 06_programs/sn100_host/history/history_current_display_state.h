#ifndef SN100_HISTORY_CURRENT_DISPLAY_STATE_H
#define SN100_HISTORY_CURRENT_DISPLAY_STATE_H

#include "history_store.h"

// An optional GUI-thread value model, with no QWidget/SQL access. The owner
// calls expectQuery/expectDetail BEFORE queueing the existing HistoryStore call.
// Each accepted reply can feed the existing vertical display value objects.
class HistoryCurrentDisplayState : public QObject
{
    Q_OBJECT
public:
    explicit HistoryCurrentDisplayState(HistoryStore *store, QObject *parent = nullptr);
    bool expectQuery(qint64 requestId);
    bool expectDetail(qint64 requestId);
    bool valid() const { return valid_; }
    HistoryReports reports() const { return reports_; }
    HistoryReport detail() const { return detail_; }
    HistoryCurrentFormatSource source() const { return source_; }
public slots:
    // Connect the actual project writer's save/replace/remove/copy completion
    // (and failed edit) to this before issuing a fresh query. Pending old SQL
    // replies then cannot revalidate the previous configuration.
    void invalidateConfiguration(QString reason);
signals:
    void cleared(QString reason);
    void queryAccepted(qint64 requestId, HistoryReports reports, qint64 totalCount);
    void detailAccepted(qint64 requestId, HistoryReport report);
    void rejected(qint64 requestId, QString reason);
private:
    enum class Kind { None, Query, Detail };
    bool expect(qint64 requestId, Kind kind);
    void clear(const QString &reason);
    bool sameSource(const HistoryCurrentFormatSource &source) const;
    bool checkReports(const HistoryReports &reports) const;
    qint64 lastRequest_ = 0, activeRequest_ = 0;
    Kind kind_ = Kind::None;
    quint64 revision_ = 0;
    bool sourceReceived_ = false, valid_ = false, closed_ = false;
    HistoryCurrentFormatSource source_;
    HistoryReports reports_;
    HistoryReport detail_;
};

#endif
