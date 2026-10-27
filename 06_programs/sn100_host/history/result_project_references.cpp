#include "result_project_references.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

namespace
{
const QString owner = QStringLiteral("history_results");
}

ResultProjectReferences::ResultProjectReferences(QObject *workerOwner) :
    store_(new ProjectsStore(workerOwner))
{
}

ResultProjectReferences::~ResultProjectReferences()
{
    // HistoryStore closes/deletes this helper before its worker is stopped.
    if (store_)
    {
        Q_ASSERT(QThread::currentThread() == store_->thread());
        store_->close();
        delete store_;
    }
}

bool ResultProjectReferences::sameThread(QString *message) const
{
    if (!store_ || QThread::currentThread() != store_->thread())
    {
        *message = QStringLiteral("真实项目引用存储必须在历史工作线程调用。");
        return false;
    }
    return true;
}

bool ResultProjectReferences::open(const QString &directory, const AccountSession &session, QString *message)
{
    if (!sameThread(message)) return false;
    QFile marker(QDir(directory).filePath(QStringLiteral(".sn100-catalog-offline-v1")));
    const QFileInfo database(QDir(directory).filePath(QStringLiteral("catalog.db")));
    if (!database.isFile() || database.isSymLink() || QFileInfo(marker.fileName()).isSymLink()
            || !marker.open(QIODevice::ReadOnly) || marker.size() != qint64(sizeof("sn100-catalog-offline-v1\n") - 1)
            || marker.readAll() != "sn100-catalog-offline-v1\n")
    {
        *message = QStringLiteral("需要已存在且标记匹配的隔离项目库；不创建、导入或猜测生产项目数据。");
        return false;
    }
    bool ready = false;
    const auto success = QObject::connect(store_, &ProjectsStore::ready, store_,
        [&ready](const QString &) { ready = true; }, Qt::DirectConnection);
    const auto failure = QObject::connect(store_, &ProjectsStore::error, store_,
        [message](qint64, const QString &, const QString &reason) { *message = reason; }, Qt::DirectConnection);
    store_->initialize(directory, false); // Actual API, own connection, never seed demo projects.
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!ready)
    {
        if (message->isEmpty()) *message = QStringLiteral("真实项目存储没有发出ready；引用保护不可用。");
        return false;
    }
    store_->setSession(session); // Existing supplied session, no test identity or role escalation.
    return true;
}

void ResultProjectReferences::setSession(const AccountSession &session)
{
    if (store_ && QThread::currentThread() == store_->thread()) store_->setSession(session);
}

bool ResultProjectReferences::projects(ProjectRecords *records, QString *message)
{
    if (!sameThread(message)) return false;
    const qint64 request = ++serial_;
    bool received = false;
    const auto success = QObject::connect(store_, &ProjectsStore::snapshotReady, store_,
        [&](qint64 id, ProjectRecords value, const ProjectUnits &)
        {
            if (id == request) { *records = value; received = true; }
        }, Qt::DirectConnection);
    const auto failure = QObject::connect(store_, &ProjectsStore::error, store_,
        [&](qint64 id, const QString &, const QString &reason) { if (id == request) *message = reason; }, Qt::DirectConnection);
    store_->load(request);
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!received && message->isEmpty()) *message = QStringLiteral("真实项目快照未返回，不能接受调用者自行声称的项目身份。");
    return received;
}

bool ResultProjectReferences::currentFormat(ProjectCurrentFormatSnapshot *snapshot, QString *message)
{
    *snapshot = ProjectCurrentFormatSnapshot(); // Never retain a prior successful reply.
    if (!sameThread(message)) return false;
    const qint64 request = ++serial_;
    bool received = false;
    const auto success = QObject::connect(store_, &ProjectsStore::currentResultFormatReady, store_,
        [&](qint64 id, ProjectCurrentFormatSnapshot value)
        {
            if (id == request && value.valid()) { *snapshot = value; received = true; }
        }, Qt::DirectConnection);
    const auto failure = QObject::connect(store_, &ProjectsStore::error, store_,
        [&](qint64 id, const QString &, const QString &reason) { if (id == request) *message = reason; }, Qt::DirectConnection);
    store_->loadCurrentResultFormat(request);
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!received)
    {
        *snapshot = ProjectCurrentFormatSnapshot();
        if (message->isEmpty()) *message = QStringLiteral("真实项目当前格式快照未返回。 ");
    }
    return received;
}

bool ResultProjectReferences::has(qint64 projectId, const QString &key, bool *held, QString *message)
{
    if (!sameThread(message)) return false;
    const qint64 request = ++serial_;
    bool received = false;
    *held = false;
    const auto success = QObject::connect(store_, &ProjectsStore::projectReferencesReady, store_,
        [&](qint64 id, qint64 actualProject, bool exists, const QStringList &references)
        {
            if (id != request) return;
            received = actualProject == projectId && exists;
            *held = received && references.contains(owner + QChar(':') + key);
        }, Qt::DirectConnection);
    const auto failure = QObject::connect(store_, &ProjectsStore::projectReferenceError, store_,
        [&](qint64 id, const QString &, const QString &reason) { if (id == request) *message = reason; }, Qt::DirectConnection);
    store_->checkProjectReferences(request, projectId);
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!received && message->isEmpty()) *message = QStringLiteral("项目不存在或真实引用查询失败。");
    return received;
}

bool ResultProjectReferences::hold(qint64 projectId, const QString &key, bool *newlyHeld, QString *message)
{
    bool previous = false;
    *newlyHeld = false;
    if (!has(projectId, key, &previous, message)) return false;
    // A real read-back already proves the durable hold. A running task may
    // finish when actionReady is false; do not turn a read into a new gated hold.
    if (previous) return true;
    const qint64 request = ++serial_;
    bool acknowledged = false;
    const auto success = QObject::connect(store_, &ProjectsStore::projectReferenceSucceeded, store_,
        [&](qint64 id, const QString &operation)
        { acknowledged = id == request && operation == QStringLiteral("holdProjectReference"); }, Qt::DirectConnection);
    const auto failure = QObject::connect(store_, &ProjectsStore::projectReferenceError, store_,
        [&](qint64 id, const QString &, const QString &reason) { if (id == request) *message = reason; }, Qt::DirectConnection);
    store_->holdProjectReference(request, owner, key, projectId);
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!acknowledged)
    {
        // No ownership proof: do not delete an ambiguous key on rollback. The
        // API can fail after its autocommitted INSERT or reject another binding.
        if (message->isEmpty()) *message = QStringLiteral("真实项目引用登记没有成功回执。");
        *message += QStringLiteral("；该键归属未确认，未擅自释放，需核对项目引用。");
        return false;
    }
    *newlyHeld = true;
    bool verified = false;
    if (!has(projectId, key, &verified, message)) return false;
    if (!verified) *message = QStringLiteral("登记回执后实际引用回查未命中，拒绝预约成功。");
    return verified;
}

bool ResultProjectReferences::release(const QString &key, QString *message)
{
    if (!sameThread(message)) return false;
    const qint64 request = ++serial_;
    bool acknowledged = false;
    const auto success = QObject::connect(store_, &ProjectsStore::projectReferenceSucceeded, store_,
        [&](qint64 id, const QString &operation)
        { acknowledged = id == request && operation == QStringLiteral("releaseProjectReference"); }, Qt::DirectConnection);
    const auto failure = QObject::connect(store_, &ProjectsStore::projectReferenceError, store_,
        [&](qint64 id, const QString &, const QString &reason) { if (id == request) *message = reason; }, Qt::DirectConnection);
    store_->releaseProjectReference(request, owner, key);
    QObject::disconnect(success);
    QObject::disconnect(failure);
    if (!acknowledged && message->isEmpty()) *message = QStringLiteral("项目引用释放失败；保留引用以免误删项目。");
    return acknowledged;
}
