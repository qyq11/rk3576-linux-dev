#include "original_alarm_receiver.h"
#include "alarm_history/original_alarm_events.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <limits>
#include <utility>

namespace AlarmHistory
{
namespace
{
constexpr int maximumPendingBatches = 32;
constexpr qint64 maximumPendingRows = 4096;
constexpr qint64 maximumPendingBytes = 2 * 1024 * 1024;
QJsonObject encodedRecoveryBatch(quint64 requestId, const Rows &rows, bool submittedToStore);
bool writeRecoveryFile(const QString &originalRoot, const QString &sourceToken,
    const QJsonArray &batches, const QString &reason, QString *savedFile, QString *error);

qint64 textBytes(const Rows &rows)
{
    qint64 bytes = 0;
    for (const Row &row : rows)
        for (const QString &value : {row.errorCode, row.description, row.measure,
                                     row.dateTime, row.user})
            bytes += value.toUtf8().size();
    return bytes;
}
}

OriginalAlarmReceiver::OriginalAlarmReceiver(Store *writer,
    QVector<OriginalRuntime::AlarmRule> rules, QString originalRoot, QObject *parent)
    : QObject(parent), writer_(writer), rules_(std::move(rules)),
      originalRoot_(QDir::cleanPath(originalRoot))
{
    expectedPath_ = QDir(originalRoot_).filePath(QStringLiteral("db/userSet/abnormal_Log.db"));
    Store::registerTypes();
    if (!writer) return;
    QThread *writerThread = writer->thread();
    if (writerThread && writerThread != thread() &&
        (!QCoreApplication::instance() || writerThread != QCoreApplication::instance()->thread()))
    {
        // 与SQL共用已有专用线程，构造对象时无父对象/子对象，不创建额外线程。
        auto *recovery = new OriginalAlarmRecoveryWriter(originalRoot_);
        recoveryWriter_ = recovery;
        recovery->moveToThread(writerThread);
        connect(writerThread, &QThread::finished, recovery, &QObject::deleteLater);
        connect(this, &OriginalAlarmReceiver::requestRecovery,
                recovery, &OriginalAlarmRecoveryWriter::save, Qt::QueuedConnection);
        connect(recovery, &OriginalAlarmRecoveryWriter::completed, this,
                [this](quint64 recoveryId, QString savedFile, QString error)
        {
            const auto it = recoveries_.find(recoveryId);
            if (it == recoveries_.end())
            {
                reportStorageError(tr("收到没有在途请求的告警恢复写入回复，未据此声明已留证。"));
                return;
            }
            const quint64 requestId = it->requestId;
            recoveryRows_ -= it->batch.rows.size();
            recoveryBytes_ -= it->batch.bytes;
            recoveries_.erase(it);
            if (!error.isEmpty()) reportStorageError(tr("告警恢复证据写入失败：") + error);
            emit recoveryFinished(requestId, savedFile, error);
            finishStopIfDrained();
        }, Qt::QueuedConnection);
        connect(recovery, &QObject::destroyed, this, [this]()
        {
            recoveryWriter_ = nullptr;
            const auto unconfirmed = recoveries_;
            recoveries_.clear();
            recoveryRows_ = recoveryBytes_ = 0;
            if (!unconfirmed.isEmpty())
            {
                const QString error = tr("告警恢复工作对象在写入确认前释放，证据保存结果待核。");
                reportStorageError(error);
                for (const auto &pending : unconfirmed)
                    emit recoveryFinished(pending.requestId, QString(), error);
            }
            finishStopIfDrained();
        }, Qt::QueuedConnection);
    }
    connect(this, &OriginalAlarmReceiver::requestOpen,
            writer, &Store::openOriginal, Qt::QueuedConnection);
    connect(this, &OriginalAlarmReceiver::requestAppend,
            writer, &Store::appendOriginalEvents, Qt::QueuedConnection);
    connect(this, &OriginalAlarmReceiver::requestSourceClose,
            writer, &Store::close, Qt::QueuedConnection);
    connect(writer, &Store::opened, this, [this](QString token, QString path)
    {
        opening_ = false;
        if (token.isEmpty() || QFileInfo(expectedPath_).canonicalFilePath() != path)
        {
            const QString error = tr("新告警库打开的来源与指定原资料不一致，未写入。");
            reportStorageError(error);
            failAll(error);
            stopRequested_ = true;
            finishStopIfDrained();
            return;
        }
        sourceToken_ = token;
        ready_ = true;
        emit sourceReadyChanged(isReady());
        submitPending();
        finishStopIfDrained();
    }, Qt::QueuedConnection);
    connect(writer, &Store::openFailed, this, [this](Error, QString message)
    {
        opening_ = false;
        ready_ = false;
        emit sourceReadyChanged(false);
        reportStorageError(tr("原告警库未能打开：") + message);
        failAll(tr("原告警库未能打开，该批新告警没有写入：") + message);
        stopRequested_ = true;
        finishStopIfDrained();
    }, Qt::QueuedConnection);
    connect(writer, &Store::originalEventsAppended, this,
            [this](quint64 requestId, qint64 rows) { finishBatch(requestId, rows, QString()); },
            Qt::QueuedConnection);
    connect(writer, &Store::originalEventsError, this,
            [this](quint64 requestId, QString message) { finishBatch(requestId, 0, message); },
            Qt::QueuedConnection);
    connect(writer, &Store::closed, this, [this]()
    {
        ready_ = false;
        opening_ = false;
        sourceClosed_ = true;
        emit sourceReadyChanged(false);
        if (!pending_.isEmpty())
        {
            const QString error = tr("告警库在提交确认前关闭，剩余批次的写入结果待核。");
            reportStorageError(error);
            failAll(error);
        }
        sourceToken_.clear();
        stopRequested_ = true;
        finishStopIfDrained();
    }, Qt::QueuedConnection);
    connect(writer, &QObject::destroyed, this, [this]()
    {
        writer_ = nullptr;
        sourceClosed_ = true;
        ready_ = false;
        opening_ = false;
        emit sourceReadyChanged(false);
        if (!pending_.isEmpty())
        {
            const QString error = tr("告警工作对象在提交确认前释放，剩余批次的写入结果待核。");
            reportStorageError(error);
            failAll(error);
        }
        stopRequested_ = true;
        finishStopIfDrained();
    }, Qt::QueuedConnection);
}

bool OriginalAlarmReceiver::openOriginal(QString originalRoot)
{
    QThread *writerThread = writer_ ? writer_->thread() : nullptr;
    if (QThread::currentThread() != thread() || started_ || stopRequested_ || !writer_ ||
        !writerThread || writerThread == thread() || !writerThread->isRunning() ||
        (QCoreApplication::instance() && writerThread == QCoreApplication::instance()->thread()) ||
        !QDir::isAbsolutePath(originalRoot) || originalRoot.contains(QChar::Null) ||
        QDir::cleanPath(originalRoot) != originalRoot_)
    {
        const QString error = tr("新告警接收器须在所属线程指定一次有效原资料，且使用已运行的独立告警工作线程。");
        reportStorageError(error);
        if (QThread::currentThread() == thread() && !started_)
        {
            stopRequested_ = true;
            failAll(error);
            finishStopIfDrained();
        }
        return false;
    }
    started_ = true;
    opening_ = true;
    expectedPath_ = QDir(originalRoot).filePath(QStringLiteral("db/userSet/abnormal_Log.db"));
    emit requestOpen(originalRoot);
    return true;
}

void OriginalAlarmReceiver::receivePayload(QByteArray payload, QString currentUser,
                                          QDateTime receivedAt)
{
    // 当前查询与其他消息不入新事件通道，特别不能把Qk/QK轮询写成历史。
    if (!payload.startsWith("Ql") && !payload.startsWith("QL")) return;
    if (QThread::currentThread() != thread())
    {
        reportStorageError(tr("新告警必须在接收器所属线程投递，未跨线程读取账户上下文。"));
        return;
    }
    Sn100Protocol::ReadOnlyMessage parsed;
    Rows rows;
    QString error;
    if (!Sn100Protocol::parseReadOnlyMessage(payload, &parsed, &error) ||
        !originalNewEvents(parsed, rules_, currentUser, receivedAt, &rows, &error))
    {
        reportStorageError(tr("新告警格式或接收上下文无效，未写入历史：") + error);
        return;
    }
    // 合法零数量通知没有历史行，不能把空批交给Store后制造写入错误。
    if (rows.isEmpty())
    {
        if (parsed.command == "QL") emit emptyBoardNotificationReceived();
        return;
    }
    if (nextRequest_ == std::numeric_limits<quint64>::max())
    {
        error = tr("新告警请求编号已耗尽，未复用旧编号；本批没有写入历史。");
        reportStorageError(error);
        reportFailedBatch(0, rows, false, error);
        return;
    }
    const quint64 requestId = ++nextRequest_;
    emit newEventsReceived(requestId, rows);
    // 设备自动连接与SQL线程启动没有先后保证，首包也先有界保留，等来源打开。
    if (stopRequested_ || stopped_ || !writer_)
    {
        error = tr("新告警存储未启动或已停止，本批没有写入历史。");
        reportStorageError(error);
        reportFailedBatch(requestId, rows, false, error);
        return;
    }
    const qint64 bytes = textBytes(rows);
    if (pending_.size() >= maximumPendingBatches ||
        pendingRows_ + rows.size() > maximumPendingRows ||
        pendingBytes_ + bytes > maximumPendingBytes)
    {
        error = tr("待确认新告警超过32批、4096条或2MiB上限，本批未入队，也未假报历史已保存。");
        reportStorageError(error);
        reportFailedBatch(requestId, rows, false, error);
        return;
    }
    Pending pending;
    pending.rows = rows;
    pending.bytes = bytes;
    pending_.insert(requestId, pending);
    pendingRows_ += rows.size();
    pendingBytes_ += bytes;
    submitPending();
}

void OriginalAlarmReceiver::submitPending()
{
    if (!ready_ || !writer_ || sourceToken_.isEmpty()) return;
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
    {
        if (it->submitted) continue;
        it->submitted = true;
        emit requestAppend(it.key(), sourceToken_, it->rows);
    }
}

void OriginalAlarmReceiver::finishBatch(quint64 requestId, qint64 rows, const QString &error)
{
    const auto it = pending_.find(requestId);
    if (it == pending_.end())
    {
        reportStorageError(tr("收到没有在途批次的新告警写入回复，未把迟到或重复回复算作成功。"));
        return;
    }
    const Pending pending = it.value();
    pendingRows_ -= pending.rows.size();
    pendingBytes_ -= pending.bytes;
    pending_.erase(it);
    QString failure = error;
    if (failure.isEmpty() && rows != pending.rows.size())
        failure = tr("告警库确认的条数与本批不一致，写入结果待核，未自动重发。");
    if (failure.isEmpty())
        emit historyCommitted(requestId, pending.rows);
    else
    {
        reportStorageError(tr("新告警历史追加未确认：") + failure);
        reportFailedBatch(requestId, pending.rows, pending.submitted, failure);
    }
    finishStopIfDrained();
}

void OriginalAlarmReceiver::failAll(const QString &error)
{
    const auto failed = pending_;
    pending_.clear();
    pendingRows_ = 0;
    pendingBytes_ = 0;
    for (auto it = failed.cbegin(); it != failed.cend(); ++it)
        reportFailedBatch(it.key(), it->rows, it->submitted, error);
}

void OriginalAlarmReceiver::reportStorageError(const QString &error)
{
    storageErrors_ = true;
    // 正式启动器会收集此输出；存储失败须同时留日志和通知界面，不能只弹一次提示。
    qWarning().noquote() << "ORIGINAL_ALARM_HISTORY_ERROR" << error;
    emit storageError(error);
}

void OriginalAlarmReceiver::requestStop()
{
    if (QThread::currentThread() != thread())
    {
        reportStorageError(tr("请在告警接收器所属线程请求正常停止。"));
        return;
    }
    if (stopped_) return;
    stopRequested_ = true;
    emit sourceReadyChanged(false);
    if (!started_ && !pending_.isEmpty())
    {
        const QString error = tr("告警来源尚未启动即请求退出，本批未交SQL，须保留恢复证据。");
        reportStorageError(error);
        failAll(error);
    }
    // 排队批次仍保留收到时的用户名和时间，注销及退出都不重新归属。
    submitPending();
    finishStopIfDrained();
}

void OriginalAlarmReceiver::finishStopIfDrained()
{
    if (!stopRequested_ || opening_ || !pending_.isEmpty() || !recoveries_.isEmpty() || stopped_) return;
    if (!writer_ || !started_ || sourceClosed_)
    {
        if (stopCompletionQueued_) return;
        stopCompletionQueued_ = true;
        QTimer::singleShot(0, this, [this]()
        {
            stopCompletionQueued_ = false;
            if (!pending_.isEmpty() || !recoveries_.isEmpty() || opening_ || stopped_) return;
            stopped_ = true;
            emit stopped();
        });
        return;
    }
    if (closeRequested_) return;
    closeRequested_ = true;
    emit requestSourceClose();
}

void OriginalAlarmReceiver::reportFailedBatch(quint64 requestId, Rows rows,
    bool submittedToStore, const QString &error)
{
    queueRecovery(requestId, rows, submittedToStore, error);
    // GUI只处理元信息和展示；完整恢复文件由同一专用工作线程保存。
    emit historyFailed(requestId, rows, submittedToStore, error);
}

void OriginalAlarmReceiver::queueRecovery(quint64 requestId, Rows rows,
    bool submittedToStore, const QString &reason)
{
    const qint64 bytes = textBytes(rows);
    QThread *worker = recoveryWriter_ ? recoveryWriter_->thread() : nullptr;
    QString error;
    if (!worker || worker == thread() || !worker->isRunning())
        error = tr("告警恢复工作线程不可用，本批未保留恢复文件。");
    else if (nextRecovery_ == std::numeric_limits<quint64>::max())
        error = tr("告警恢复请求编号耗尽，本批未保留恢复文件。");
    else if (recoveries_.size() >= maximumPendingBatches ||
             recoveryRows_ + rows.size() > maximumPendingRows ||
             recoveryBytes_ + bytes > maximumPendingBytes)
        error = tr("告警恢复待写超过32批、4096条或2MiB上限，本批未入恢复队列，也未假报已留证。");
    if (!error.isEmpty())
    {
        reportStorageError(error);
        emit recoveryFinished(requestId, QString(), error);
        return;
    }
    RecoveryPending recovery;
    recovery.requestId = requestId;
    recovery.batch.rows = rows;
    recovery.batch.bytes = bytes;
    recovery.batch.submitted = submittedToStore;
    recovery.sourceToken = sourceToken_;
    recovery.reason = reason;
    const quint64 recoveryId = ++nextRecovery_;
    recoveries_.insert(recoveryId, recovery);
    recoveryRows_ += rows.size();
    recoveryBytes_ += bytes;
    stopped_ = false;
    emit requestRecovery(recoveryId, requestId, rows, submittedToStore, sourceToken_, reason);
}

bool OriginalAlarmReceiver::savePendingRecovery(QString reason, QString *savedFile, QString *error) const
{
    // 仅供GUI事件循环已结束后的最后兜底。正常运行中的文件写入走requestRecovery。
    QJsonArray batches;
    for (auto it = pending_.cbegin(); it != pending_.cend(); ++it)
        batches.append(encodedRecoveryBatch(it.key(), it->rows, it->submitted));
    for (auto it = recoveries_.cbegin(); it != recoveries_.cend(); ++it)
    {
        QJsonObject batch = encodedRecoveryBatch(it->requestId, it->batch.rows, it->batch.submitted);
        batch.insert(QStringLiteral("recovery_request_id"), QString::number(it.key()));
        batch.insert(QStringLiteral("recovery_write_outcome"), QStringLiteral("unconfirmed"));
        batch.insert(QStringLiteral("recovery_reason"), it->reason);
        batch.insert(QStringLiteral("source_token"), it->sourceToken);
        batches.append(batch);
    }
    return writeRecoveryFile(originalRoot_, sourceToken_, batches, reason, savedFile, error);
}

namespace
{
QJsonObject encodedRecoveryBatch(quint64 requestId, const Rows &values, bool submittedToStore)
{
    QJsonArray rows;
    for (const Row &row : values)
        rows.append(QJsonObject{
            {QStringLiteral("id"), QString::number(row.id)},
            {QStringLiteral("error_code"), row.errorCode},
            {QStringLiteral("error_describe"), row.description},
            {QStringLiteral("measure"), row.measure},
            {QStringLiteral("date_time"), row.dateTime},
            {QStringLiteral("corrective_action"), row.correctiveAction},
            {QStringLiteral("use_user"), row.user}});
    return QJsonObject{
        {QStringLiteral("request_id"), QString::number(requestId)},
        {QStringLiteral("submitted_to_store"), submittedToStore},
        {QStringLiteral("write_outcome"), submittedToStore ? QStringLiteral("unconfirmed") : QStringLiteral("not_submitted")},
        {QStringLiteral("rows"), rows}};
}

bool writeRecoveryFile(const QString &originalRoot, const QString &sourceToken,
    const QJsonArray &batches, const QString &reason, QString *savedFile, QString *error)
{
    if (savedFile) savedFile->clear();
    if (error) error->clear();
    if (batches.isEmpty()) return true;
    const auto reject = [error](const QString &message)
    {
        if (error) *error = message;
        return false;
    };
    const QFileInfo root(originalRoot);
    if (!root.isAbsolute() || !root.isDir() || root.isSymLink() ||
        root.canonicalFilePath() != originalRoot || originalRoot.contains(QChar::Null))
        return reject(QObject::tr("告警恢复证据的原资料根无效或经过链接，未改用其他保存位置。"));
    QString directory = originalRoot;
    for (const QString &component : {QStringLiteral("rk"), QStringLiteral("alarm_recovery")})
    {
        const QString next = QDir(directory).filePath(component);
        const QFileInfo before(next);
        if (before.isSymLink() || (before.exists() && !before.isDir()))
            return reject(QObject::tr("告警恢复证据目录经过链接或不是目录，未写入。"));
        if (!before.exists() && !QDir(directory).mkdir(component))
            return reject(QObject::tr("无法创建板端告警恢复证据目录。"));
        const QFileInfo after(next);
        if (!after.isDir() || after.isSymLink() || after.canonicalFilePath() != next)
            return reject(QObject::tr("告警恢复证据目录的实际路径不符，未写入。"));
        directory = next;
    }
    if (!QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return reject(QObject::tr("无法把告警恢复证据目录限制为仅当前用户访问，未写入。"));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QJsonObject recovery{
        {QStringLiteral("schema_version"), 1},
        {QStringLiteral("created_at_utc"), now.toString(Qt::ISODateWithMs)},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("original_root"), originalRoot},
        {QStringLiteral("history_database"), QDir(originalRoot).filePath(QStringLiteral("db/userSet/abnormal_Log.db"))},
        {QStringLiteral("source_token"), sourceToken},
        {QStringLiteral("automatic_replay_allowed"), false},
        {QStringLiteral("batches"), batches}};
    const QByteArray bytes = QJsonDocument(recovery).toJson(QJsonDocument::Indented);
    const QString fileName = now.toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")) + QLatin1Char('-') +
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json");
    const QString path = QDir(directory).filePath(fileName);
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        return reject(QObject::tr("告警恢复证据文件名已存在，未覆盖。"));
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return reject(file.errorString());
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
    {
        file.cancelWriting();
        return reject(QObject::tr("无法把告警恢复证据文件限制为仅当前用户读写，未提交。"));
    }
    if (file.write(bytes) != bytes.size())
    {
        const QString message = file.errorString();
        file.cancelWriting();
        return reject(message);
    }
    if (!file.commit()) return reject(file.errorString());
    if (savedFile) *savedFile = path;
    return true;
}
}

OriginalAlarmRecoveryWriter::OriginalAlarmRecoveryWriter(QString originalRoot)
    : originalRoot_(std::move(originalRoot))
{
}

void OriginalAlarmRecoveryWriter::save(quint64 recoveryId, quint64 eventRequestId, Rows rows,
    bool submittedToStore, QString sourceToken, QString reason)
{
    QString savedFile, error;
    if (QThread::currentThread() != thread() ||
        (QCoreApplication::instance() && thread() == QCoreApplication::instance()->thread()))
        error = tr("告警恢复文件必须在专用工作线程保存，未在GUI线程写入。");
    else if (!writeRecoveryFile(originalRoot_, sourceToken,
            QJsonArray{encodedRecoveryBatch(eventRequestId, rows, submittedToStore)},
            reason, &savedFile, &error) && error.isEmpty())
        error = tr("告警恢复证据未能提交，且缺少具体失败原因，不能确认已保存。");
    emit completed(recoveryId, savedFile, error);
}
}
