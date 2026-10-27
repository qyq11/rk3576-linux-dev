#include "history_current_display_state.h"
#include <QThread>

HistoryCurrentDisplayState::HistoryCurrentDisplayState(HistoryStore *store, QObject *parent) : QObject(parent)
{
    Q_ASSERT(store);
    if (!store) { closed_ = true; return; }
    HistoryStore::registerTypes();
    connect(store, &HistoryStore::currentFormatInvalidated, this,
        [this](qint64 request, quint64 revision, const QString &reason)
        {
            if (request == 0)
            {
                activeRequest_ = 0; kind_ = Kind::None; clear(reason);
                return;
            }
            if (request != activeRequest_) return;
            clear(reason);
            revision_ = revision;
        }, Qt::QueuedConnection);
    connect(store, &HistoryStore::currentFormatRead, this,
        [this](qint64 request, const HistoryCurrentFormatSource &source)
        {
            if (request != activeRequest_) return;
            if (revision_ == 0 || source.requestRevision != revision_ || source.sourceGeneration == 0
                    || source.historyDatasetUuid.isEmpty() || source.catalogDatabasePath.isEmpty()
                    || source.catalogDirectory.isEmpty() || source.fingerprint.size() != 64
                    || source.catalogVersion != QStringLiteral("sn100-catalog-offline-v1")
                    || source.formatVersion != QStringLiteral("sn100-current-result-format-source-v1")
                    || !source.capturedAtUtc.isValid())
            {
                clear(QStringLiteral("当前格式来源回执不完整。 "));
                return;
            }
            source_ = source;
            sourceReceived_ = true;
        }, Qt::QueuedConnection);
    connect(store, &HistoryStore::queryReady, this,
        [this](qint64 request, const HistoryFilter &, const HistoryReports &reports, qint64 total)
        {
            if (request != activeRequest_ || kind_ != Kind::Query) return;
            if (!sourceReceived_ || !checkReports(reports))
            {
                activeRequest_ = 0; kind_ = Kind::None;
                clear(QStringLiteral("查询与本次格式快照不匹配。 "));
                emit rejected(request, QStringLiteral("查询来源不匹配，旧缓存已失效。 "));
                return;
            }
            reports_ = reports; valid_ = true;
            activeRequest_ = 0; kind_ = Kind::None;
            emit queryAccepted(request, reports_, total);
        }, Qt::QueuedConnection);
    connect(store, &HistoryStore::detailReady, this,
        [this](qint64 request, const HistoryReport &report)
        {
            if (request != activeRequest_ || kind_ != Kind::Detail) return;
            if (!sourceReceived_ || !checkReports(HistoryReports{report}))
            {
                activeRequest_ = 0; kind_ = Kind::None;
                clear(QStringLiteral("明细与本次格式快照不匹配。 "));
                emit rejected(request, QStringLiteral("明细来源不匹配，旧缓存已失效。 "));
                return;
            }
            detail_ = report; valid_ = true;
            activeRequest_ = 0; kind_ = Kind::None;
            emit detailAccepted(request, detail_);
        }, Qt::QueuedConnection);
    connect(store, &HistoryStore::error, this,
        [this](qint64 request, const QString &, const QString &reason)
        {
            if (request != activeRequest_ || activeRequest_ == 0) return;
            activeRequest_ = 0; kind_ = Kind::None; clear(reason);
            emit rejected(request, reason);
        }, Qt::QueuedConnection);
    connect(store, &HistoryStore::closed, this, [this]()
    {
        closed_ = true; activeRequest_ = 0; kind_ = Kind::None;
        clear(QStringLiteral("结果存储已关闭。 "));
    }, Qt::QueuedConnection);
    connect(store, &QObject::destroyed, this, [this]()
    {
        closed_ = true; activeRequest_ = 0; kind_ = Kind::None;
        clear(QStringLiteral("结果工作对象已销毁。 "));
    }, Qt::QueuedConnection);
}

bool HistoryCurrentDisplayState::expect(qint64 request, Kind kind)
{
    if (QThread::currentThread() != thread() || closed_ || request <= lastRequest_) return false;
    lastRequest_ = request;
    activeRequest_ = request;
    kind_ = kind;
    clear(QStringLiteral("正在重新读取，旧当前格式缓存失效。 "));
    return true;
}
bool HistoryCurrentDisplayState::expectQuery(qint64 request) { return expect(request, Kind::Query); }
bool HistoryCurrentDisplayState::expectDetail(qint64 request) { return expect(request, Kind::Detail); }
void HistoryCurrentDisplayState::invalidateConfiguration(QString reason)
{
    if (QThread::currentThread() != thread()) return;
    activeRequest_ = 0; kind_ = Kind::None;
    clear(reason);
}
void HistoryCurrentDisplayState::clear(const QString &reason)
{
    valid_ = false; sourceReceived_ = false; revision_ = 0;
    source_ = HistoryCurrentFormatSource(); reports_.clear(); detail_ = HistoryReport();
    emit cleared(reason);
}
bool HistoryCurrentDisplayState::sameSource(const HistoryCurrentFormatSource &source) const
{
    return source.requestRevision == source_.requestRevision && source.sourceGeneration == source_.sourceGeneration
        && source.historyDatasetUuid == source_.historyDatasetUuid && source.catalogDatabasePath == source_.catalogDatabasePath
        && source.catalogDirectory == source_.catalogDirectory && source.catalogVersion == source_.catalogVersion
        && source.formatVersion == source_.formatVersion && source.fingerprint == source_.fingerprint
        && source.capturedAtUtc == source_.capturedAtUtc;
}
bool HistoryCurrentDisplayState::checkReports(const HistoryReports &reports) const
{
    for (const auto &report : reports) for (const auto &task : report.results)
    {
        if (task.projectId <= 0) return false;
        for (const auto &value : task.storedValues)
            if (value.formatOrigin == HistoryValueFormatOrigin::AnonymousFixture || !sameSource(value.currentSource)) return false;
        for (const auto &value : task.values)
            if (value.formatOrigin == HistoryValueFormatOrigin::AnonymousFixture || !sameSource(value.currentSource)) return false;
    }
    return true;
}
