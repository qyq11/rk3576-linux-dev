#include "alarm_history_store.h"
#include "original_files/original_business_database.h"
#include <QDir>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <algorithm>

namespace AlarmHistory
{
namespace
{
constexpr int requestMilliseconds = 30000; // Candidate budget, not an accepted clinical/UI limit.
constexpr int maximumCellBytes = 65536;
const char *const names[] = {"id", "error_code", "error_describe", "measure", "date_time",
                            "corrective_action", "use_user"};
const char *const types[] = {"INTEGER", "VARCHAR", "VARCHAR", "VARCHAR", "DATETIME", "VARCHAR", "VARCHAR"};

bool canonicalDate(const QDate &date)
{
    return date.isValid() && date.year() >= 1 && date.year() <= 9999;
}
bool canonicalTimeText(const QString &text)
{
    if (text.size() != 19)
    {
        return false;
    }
    const QDate date = QDate::fromString(text.left(10), QStringLiteral("yyyy/MM/dd"));
    const QTime time = QTime::fromString(text.mid(11), QStringLiteral("HH:mm:ss"));
    return canonicalDate(date) && time.isValid() &&
        text == date.toString(QStringLiteral("yyyy/MM/dd")) + QLatin1Char(' ') +
                time.toString(QStringLiteral("HH:mm:ss"));
}
QString lowerBound(const Request &request)
{
    return request.first.toString(QStringLiteral("yyyy/MM/dd")) + QStringLiteral(" 00:00:00");
}
QString upperBound(const Request &request)
{
    return request.last.toString(QStringLiteral("yyyy/MM/dd")) + QStringLiteral(" 23:59:59");
}
}

Store::Store(QObject *parent) : QObject(parent), m_deadline(new QTimer(this))
{
    m_deadline->setSingleShot(true);
    connect(m_deadline, &QTimer::timeout, this, [this]()
    {
        if (active())
        {
            fail(Error::ResourceLimit, QStringLiteral("历史查询超过候选30秒预算，未完成；已返回的批不能当作完整结果。"));
        }
    });
}
Store::~Store()
{
    // Owner must deliver close and stop the worker normally before destruction.
    release();
}
void Store::registerTypes()
{
    qRegisterMetaType<Error>();
    qRegisterMetaType<Scope>();
    qRegisterMetaType<Completion>();
    qRegisterMetaType<Key>();
    qRegisterMetaType<Request>();
    qRegisterMetaType<Row>();
    qRegisterMetaType<Rows>();
}
bool Store::inOwnerThread() const { return QThread::currentThread() == thread(); }
bool Store::active() const { return m_phase != Phase::Idle; }

void Store::openOriginal(QString root)
{
    if (!inOwnerThread() || (QCoreApplication::instance() && thread() == QCoreApplication::instance()->thread()))
    { emit openFailed(Error::WrongThread, QStringLiteral("请在历史库工作线程选择原资料。")); return; }
    if (m_database.isValid())
    { emit openFailed(Error::Busy, QStringLiteral("已有历史库连接，请先正常关闭。")); return; }
    QString message;
    {
        // 公共原库入口同时检查整条路径，不能让中间分类目录的链接越出资料根。
        OriginalBusiness::Database validated;
        if (!validated.open(root, QStringLiteral("db/userSet/abnormal_Log.db"), false, &message))
        { emit openFailed(Error::OpenFailed, message); return; }
    }
    const QString path = QDir(root).filePath(QStringLiteral("db/userSet/abnormal_Log.db"));
    if (QFileInfo(path).isSymLink())
    { emit openFailed(Error::OpenFailed, QStringLiteral("原告警历史不能经过符号链接。")); return; }
    m_original = true;
    m_originalRoot = QFileInfo(root).canonicalFilePath();
    m_lastOriginalAppend = 0;
    openExisting(path);
}

void Store::appendOriginalEvents(quint64 requestId, QString sourceToken, Rows rows)
{
    if (!inOwnerThread() || !m_original || m_originalRoot.isEmpty() || !m_database.isOpen() || sourceToken != m_sourceToken || sourceToken.isEmpty())
    { emit originalEventsError(requestId, QStringLiteral("新告警来源或线程已失效，未写入历史。")); return; }
    if (requestId == 0 || requestId <= m_lastOriginalAppend)
    { emit originalEventsError(requestId, QStringLiteral("重复或迟到的新告警追加请求未执行。")); return; }
    if (rows.isEmpty() || rows.size() > 1024 || m_originalEvents.size() >= 32 || m_queuedOriginalRows + rows.size() > 4096)
    { emit originalEventsError(requestId, QStringLiteral("新告警批为空或超出待写队列上限，未截断或假报成功。")); return; }
    for (const Row &row : rows)
    {
        if (row.id != 0 || row.errorCode.isEmpty() || row.errorCode.size() > 64 || row.errorCode != row.errorCode.trimmed() || !canonicalTimeText(row.dateTime) || !row.correctiveAction.isEmpty())
        { emit originalEventsError(requestId, QStringLiteral("只允许追加有编号和明确接收时间的新告警，不能修改旧记录或处理措施。")); return; }
        for (const QString &value : {row.errorCode, row.description, row.measure, row.dateTime, row.user})
            if (value.contains(QChar::Null) || value.toUtf8().size() > maximumCellBytes)
            { emit originalEventsError(requestId, QStringLiteral("新告警字段含空字符或超出单格上限，未截断写入。")); return; }
    }
    qint64 totalBytes = 0;
    const auto bytesIn = [](const Rows &values)
    {
        qint64 bytes = 0;
        for (const Row &row : values) for (const QString &value : {row.errorCode, row.description, row.measure, row.dateTime, row.user}) bytes += value.toUtf8().size();
        return bytes;
    };
    totalBytes += bytesIn(rows);
    for (const auto &batch : m_originalEvents) totalBytes += bytesIn(batch.second);
    if (totalBytes > 2097152)
    { emit originalEventsError(requestId, QStringLiteral("待写告警文本超过2MiB上限，未丢弃后假报成功。")); return; }
    m_lastOriginalAppend = requestId;
    m_queuedOriginalRows += rows.size();
    m_originalEvents.append(qMakePair(requestId, rows));
    scheduleOriginalEvents();
}

void Store::scheduleOriginalEvents()
{
    if (active() || m_originalEvents.isEmpty() || m_appendScheduled) return;
    m_appendScheduled = true;
    QTimer::singleShot(0, this, [this]() { m_appendScheduled = false; processOriginalEvents(); });
}

void Store::processOriginalEvents()
{
    if (active() || m_originalEvents.isEmpty() || !m_original || m_originalRoot.isEmpty()) return;
    const auto batch = m_originalEvents.takeFirst();
    m_queuedOriginalRows -= batch.second.size();
    QString message;
    bool success = false;
    {
        OriginalBusiness::Database writer;
        success = writer.open(m_originalRoot, QStringLiteral("db/userSet/abnormal_Log.db"), true, &message) && writer.begin(&message);
        if (success)
        {
            for (const Row &row : batch.second)
                if (!writer.execute(QStringLiteral("INSERT INTO abnormal_log(error_code,error_describe,measure,date_time,corrective_action,use_user) VALUES(?,?,?,?,?,?)"),
                    {row.errorCode, row.description.isNull() ? QStringLiteral("") : row.description,
                     row.measure.isNull() ? QStringLiteral("") : row.measure, row.dateTime, QStringLiteral(""),
                     row.user.isNull() ? QStringLiteral("") : row.user}, &message, 1))
                { success = false; break; }
            if (success) success = writer.commit(&message);
            if (!success) writer.rollback();
        }
    }
    if (success) emit originalEventsAppended(batch.first, batch.second.size());
    else emit originalEventsError(batch.first, QStringLiteral("原告警追加未提交：") + message);
    scheduleOriginalEvents();
}

void Store::openExisting(QString absolutePath)
{
    if (!inOwnerThread() || (QCoreApplication::instance() && thread() == QCoreApplication::instance()->thread()))
    {
        emit openFailed(Error::WrongThread, QStringLiteral("必须在SQL所属线程打开历史库。"));
        return;
    }
    if (m_database.isValid())
    {
        emit openFailed(Error::Busy, QStringLiteral("已有历史库连接，请先正常关闭。"));
        return;
    }
    const QFileInfo file(absolutePath);
    if (!file.isAbsolute() || !file.exists() || !file.isFile() || file.canonicalFilePath().isEmpty())
    {
        emit openFailed(Error::OpenFailed, QStringLiteral("须显式指定已存在的历史库绝对路径；不会自动造库。"));
        return;
    }
    m_connectionName = QStringLiteral("alarm_history_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(file.canonicalFilePath());
    m_database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=250"));
    if (!m_database.open())
    {
        const QString message = m_database.lastError().text();
        release();
        emit openFailed(Error::OpenFailed, message);
        return;
    }
    QString message;
    Error error = Error::SqlFailure;
    bool queryOnly = false;
    {
        QSqlQuery query(m_database);
        queryOnly = query.exec(QStringLiteral("PRAGMA query_only=ON")) &&
            query.exec(QStringLiteral("PRAGMA query_only")) && query.next() && query.value(0).toInt() == 1;
        if (!queryOnly)
        {
            message = query.lastError().text();
        }
    }
    if (!queryOnly || !compatibleSchema(&error, &message))
    {
        release();
        emit openFailed(error, message.isEmpty() ? QStringLiteral("只读历史库兼容性核查失败。") : message);
        return;
    }
    m_sourceToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_lastRequestId = 0;
    emit opened(m_sourceToken, file.canonicalFilePath());
}

bool Store::compatibleSchema(Error *error, QString *message)
{
    *error = Error::SqlFailure;
    QSqlQuery table(m_database);
    table.prepare(QStringLiteral("SELECT type FROM sqlite_master WHERE name=? COLLATE BINARY"));
    table.addBindValue(QStringLiteral("abnormal_log"));
    if (!table.exec())
    {
        *message = table.lastError().text();
        return false;
    }
    if (!table.next())
    {
        *error = table.lastError().isValid() ? Error::SqlFailure : Error::IncompatibleSchema;
        *message = table.lastError().isValid() ? table.lastError().text() : QStringLiteral("缺少原abnormal_log表。" );
        return false;
    }
    if (table.value(0).toString() != QStringLiteral("table"))
    {
        *error = Error::IncompatibleSchema;
        *message = QStringLiteral("同名abnormal_log对象不是表。" );
        return false;
    }
    QSqlQuery columns(m_database);
    if (!columns.exec(QStringLiteral("PRAGMA table_info(abnormal_log)")))
    {
        *message = columns.lastError().text();
        return false;
    }
    int count = 0;
    while (columns.next())
    {
        if (m_original)
        {
            const QString name = columns.value(1).toString();
            int expected = -1;
            for (int index = 0; index < 7; ++index) if (name == QLatin1String(names[index])) expected = index;
            if (expected < 0) continue;
            if (columns.value(2).toString().trimmed().toUpper() != QLatin1String(types[expected]) || columns.value(5).toInt() != (expected == 0 ? 1 : 0))
            { *error = Error::IncompatibleSchema; *message = QStringLiteral("原告警历史已知字段类型不兼容，未改表。"); return false; }
            ++count; continue;
        }
        if (count >= 7 || columns.value(0).toInt() != count ||
            columns.value(1).toString() != QLatin1String(names[count]) ||
            columns.value(2).toString().trimmed().toUpper() != QLatin1String(types[count]) ||
            columns.value(5).toInt() != (count == 0 ? 1 : 0))
        {
            *error = Error::IncompatibleSchema;
            *message = QStringLiteral("告警历史七列的顺序、声明类型或主键不兼容；未迁移或改表。" );
            return false;
        }
        ++count;
    }
    if (columns.lastError().isValid() || count != 7)
    {
        *error = columns.lastError().isValid() ? Error::SqlFailure : Error::IncompatibleSchema;
        *message = columns.lastError().isValid() ? columns.lastError().text() : QStringLiteral("告警历史列数不兼容。" );
        return false;
    }
    return true;
}

void Store::begin(Request request)
{
    if (!inOwnerThread())
    {
        emit requestRejected(request.key, Error::WrongThread, QStringLiteral("请求不在SQL所属线程。"));
        return;
    }
    if (active())
    {
        emit requestRejected(request.key, Error::Busy, QStringLiteral("上次查询尚未终止；先取消并等回执。"));
        return;
    }
    const bool knownScope = request.scope == Scope::LatestToday20 || request.scope == Scope::DateInterval;
    if (!m_database.isOpen() || request.key.sourceToken != m_sourceToken || m_sourceToken.isEmpty() ||
        request.key.requestId == 0 || request.key.requestId <= m_lastRequestId || request.key.viewGeneration == 0 ||
        !knownScope || !canonicalDate(request.first) || !canonicalDate(request.last) || request.first > request.last ||
        request.batchSize < 1 || request.batchSize > 128 ||
        (request.scope == Scope::LatestToday20 && (!canonicalDate(request.today) ||
          request.first != request.today || request.last != request.today)))
    {
        emit requestRejected(request.key, Error::InvalidRequest, QStringLiteral("历史请求身份、日期、范围或批大小无效。"));
        return;
    }
    m_request = request;
    m_lastRequestId = request.key.requestId;
    m_validated = m_matching = m_expected = m_delivered = 0;
    m_stepCounter = m_awaitingStep = 0;
    m_phase = Phase::Validating;
    m_elapsed.start();
    m_deadline->start(requestMilliseconds);
    if (!m_database.transaction())
    {
        fail(Error::SqlFailure, m_database.lastError().text());
        return;
    }
    m_transaction = true;
    Error error = Error::SqlFailure;
    QString message;
    if (!compatibleSchema(&error, &message))
    {
        fail(error, message);
        return;
    }
    m_scan.reset(new QSqlQuery(m_database));
    m_scan->setForwardOnly(true);
    if (!m_scan->exec(QStringLiteral("SELECT id,error_code,error_describe,measure,date_time,corrective_action,use_user,"
            "typeof(id),typeof(error_code),typeof(error_describe),typeof(measure),typeof(date_time),"
            "typeof(corrective_action),typeof(use_user) FROM abnormal_log ORDER BY id")))
    {
        fail(Error::SqlFailure, m_scan->lastError().text());
        return;
    }
    if (withinBudget())
    {
        m_awaitingStep = ++m_stepCounter;
        emit continuationNeeded(m_request, m_validated, m_awaitingStep);
    }
}

bool Store::checkKey(const Key &key)
{
    if (!inOwnerThread())
    {
        emit requestRejected(key, Error::WrongThread, QStringLiteral("操作不在SQL所属线程。"));
        return false;
    }
    if (!active() || !(key == m_request.key))
    {
        emit requestRejected(key, Error::InvalidRequest, QStringLiteral("已过期或不属于当前历史请求。"));
        return false;
    }
    return true;
}

bool Store::decode(QSqlQuery &query, Row *row, Error *error, QString *message) const
{
    *error = Error::IncompatibleData;
    if (query.value(7).toString() != QStringLiteral("integer") || query.value(0).toLongLong() <= 0)
    {
        *message = QStringLiteral("告警历史id不是正整数。" );
        return false;
    }
    for (int column = 1; column <= 6; ++column)
    {
        const QString type = query.value(column + 7).toString();
        if ((type != QStringLiteral("text") && type != QStringLiteral("null")) ||
            (column == 4 && type != QStringLiteral("text")))
        {
            *message = QStringLiteral("告警历史文本/日期存储类型不兼容，未强制转字符串。" );
            return false;
        }
        if (query.value(column).toString().toUtf8().size() > maximumCellBytes)
        {
            *error = Error::ResourceLimit;
            *message = QStringLiteral("告警历史单元格超过候选64KiB预算，未截断。" );
            return false;
        }
    }
    const QString date = query.value(4).toString();
    if (!canonicalTimeText(date))
    {
        *message = QStringLiteral("历史日期必须为合法yyyy/MM/dd HH:mm:ss；不默默漏掉其他格式。" );
        return false;
    }
    const auto textColumn = [&query](int column)
    {
        return query.value(column).isNull() ? QString() : query.value(column).toString();
    };
    row->id = query.value(0).toLongLong();
    row->errorCode = textColumn(1);
    row->description = textColumn(2);
    row->measure = textColumn(3);
    row->dateTime = date;
    row->correctiveAction = textColumn(5);
    row->user = textColumn(6);
    return true;
}

bool Store::withinBudget()
{
    if (m_elapsed.elapsed() > requestMilliseconds)
    {
        fail(Error::ResourceLimit, QStringLiteral("历史查询超过候选时间预算，未完成。"));
        return false;
    }
    return true;
}
void Store::advance(Key key, quint64 continuationStep)
{
    if (!checkKey(key))
    {
        return;
    }
    if (continuationStep == 0 || continuationStep != m_awaitingStep)
    {
        emit requestRejected(key, Error::InvalidRequest, QStringLiteral("历史批次续取凭据重复或已过期。"));
        return;
    }
    m_awaitingStep = 0;
    if (!withinBudget())
    {
        return;
    }
    if (m_phase == Phase::Validating)
    {
        validateBatch();
    }
    else
    {
        readBatch();
    }
}
void Store::validateBatch()
{
    for (int count = 0; count < m_request.batchSize; ++count)
    {
        if (!m_scan->next())
        {
            if (m_scan->lastError().isValid())
            {
                fail(Error::SqlFailure, m_scan->lastError().text());
            }
            else
            {
                m_scan.reset();
                prepareRows();
            }
            return;
        }
        Row row;
        Error error = Error::IncompatibleData;
        QString message;
        if (!decode(*m_scan, &row, &error, &message))
        {
            fail(error, message);
            return;
        }
        ++m_validated;
        if (row.dateTime >= lowerBound(m_request) && row.dateTime <= upperBound(m_request))
        {
            ++m_matching;
        }
    }
    if (withinBudget())
    {
        m_awaitingStep = ++m_stepCounter;
        emit continuationNeeded(m_request, m_validated, m_awaitingStep);
    }
}
void Store::prepareRows()
{
    m_expected = m_request.scope == Scope::LatestToday20 ? std::min<qint64>(20, m_matching) : m_matching;
    m_phase = Phase::Reading;
    if (!withinBudget())
    {
        return;
    }
    if (m_expected == 0)
    {
        complete();
        return;
    }
    m_rows.reset(new QSqlQuery(m_database));
    m_rows->setForwardOnly(true);
    QString sql = QStringLiteral("SELECT id,error_code,error_describe,measure,date_time,corrective_action,use_user,"
        "typeof(id),typeof(error_code),typeof(error_describe),typeof(measure),typeof(date_time),"
        "typeof(corrective_action),typeof(use_user) FROM abnormal_log "
        "WHERE date_time COLLATE BINARY BETWEEN ? AND ? ORDER BY date_time COLLATE BINARY DESC,id DESC");
    if (m_request.scope == Scope::LatestToday20)
    {
        sql += QStringLiteral(" LIMIT 20");
    }
    m_rows->prepare(sql);
    m_rows->addBindValue(lowerBound(m_request));
    m_rows->addBindValue(upperBound(m_request));
    if (!m_rows->exec())
    {
        fail(Error::SqlFailure, m_rows->lastError().text());
        return;
    }
    if (withinBudget())
    {
        m_awaitingStep = ++m_stepCounter;
        emit continuationNeeded(m_request, m_validated, m_awaitingStep);
    }
}
void Store::readBatch()
{
    Rows batch;
    const qint64 count = std::min<qint64>(m_request.batchSize, m_expected - m_delivered);
    for (qint64 index = 0; index < count; ++index)
    {
        if (!m_rows->next())
        {
            fail(Error::SqlFailure, m_rows->lastError().isValid() ? m_rows->lastError().text() :
                 QStringLiteral("区间结果数少于同快照校验数，未完成。"));
            return;
        }
        Row row;
        Error error = Error::IncompatibleData;
        QString message;
        if (!decode(*m_rows, &row, &error, &message))
        {
            fail(error, message);
            return;
        }
        batch.append(row);
    }
    if (!withinBudget())
    {
        return;
    }
    if (m_delivered + batch.size() == m_expected)
    {
        const bool unexpectedRow = m_rows->next();
        if (unexpectedRow || m_rows->lastError().isValid())
        {
            fail(Error::SqlFailure, m_rows->lastError().isValid() ? m_rows->lastError().text() :
                 QStringLiteral("区间结果数多于同快照校验数，未完成。"));
            return;
        }
    }
    m_delivered += batch.size();
    const bool more = m_delivered < m_expected;
    m_awaitingStep = more ? ++m_stepCounter : 0;
    emit batchReady(m_request, batch, more, m_awaitingStep);
    if (!more)
    {
        complete();
    }
}
void Store::complete()
{
    m_rows.reset();
    if (!m_database.commit())
    {
        fail(Error::SqlFailure, m_database.lastError().text());
        return;
    }
    m_transaction = false;
    const Request request = m_request;
    const qint64 delivered = m_delivered;
    const qint64 matching = m_matching;
    clearRequest();
    emit finished(request, request.scope == Scope::LatestToday20 ? Completion::RecentTodayComplete :
                  Completion::IntervalComplete, delivered, matching);
}
void Store::clearRequest()
{
    m_deadline->stop();
    m_scan.reset();
    m_rows.reset();
    m_phase = Phase::Idle;
    m_awaitingStep = 0;
    scheduleOriginalEvents();
}
void Store::fail(Error error, const QString &message)
{
    const Request request = m_request;
    const qint64 delivered = m_delivered;
    clearRequest();
    if (m_transaction)
    {
        const bool rolledBack = m_database.rollback();
        m_transaction = false;
        if (!rolledBack)
        {
            release();
            emit failed(request, error, message + QStringLiteral("；只读回滚失败，连接已关闭，需重新打开。"), delivered);
            return;
        }
    }
    emit failed(request, error, message, delivered);
}
void Store::cancel(Key key)
{
    if (!checkKey(key))
    {
        return;
    }
    const Request request = m_request;
    const qint64 delivered = m_delivered;
    clearRequest();
    if (m_transaction)
    {
        if (!m_database.rollback())
        {
            m_transaction = false;
            release();
            emit failed(request, Error::SqlFailure, QStringLiteral("取消时只读事务回滚失败。"), delivered);
            return;
        }
        m_transaction = false;
    }
    emit cancelled(request, delivered);
}
void Store::release()
{
    for (const auto &batch : m_originalEvents) emit originalEventsError(batch.first, QStringLiteral("历史来源已关闭，该批新告警未执行追加。"));
    m_originalEvents.clear();
    m_queuedOriginalRows = 0;
    clearRequest();
    if (m_transaction)
    {
        m_database.rollback();
        m_transaction = false;
    }
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = QSqlDatabase();
    m_original = false;
    m_originalRoot.clear();
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_connectionName.clear();
    m_sourceToken.clear();
}
void Store::close()
{
    if (!inOwnerThread())
    {
        emit openFailed(Error::WrongThread, QStringLiteral("须在SQL所属线程关闭历史库。"));
        return;
    }
    if (active())
    {
        cancel(m_request.key);
    }
    release();
    emit closed();
}
}
