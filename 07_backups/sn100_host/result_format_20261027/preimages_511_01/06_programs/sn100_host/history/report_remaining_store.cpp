#include "history_store.h"
#include "result_project_references.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace
{
class RemainingTransaction
{
public:
    explicit RemainingTransaction(QSqlDatabase &database) : m_database(database) {}
    ~RemainingTransaction() { if (m_active) m_database.rollback(); }
    bool begin(QString *error)
    {
        m_active = m_database.transaction();
        if (!m_active) *error = m_database.lastError().text();
        return m_active;
    }
    bool commit(QString *error)
    {
        if (!m_database.commit()) { *error = m_database.lastError().text(); return false; }
        m_active = false;
        return true;
    }
private:
    QSqlDatabase &m_database;
    bool m_active = false;
};

QString resultKey(const QString &project, int type)
{
    return QStringLiteral("result:") + project + QChar(0x1f) + QString::number(type);
}
}

void HistoryStore::setSession(AccountSession session)
{
    if (!checkThread(0, QStringLiteral("setSession"))) return;
    m_session = session;
    if (m_resultProjects) m_resultProjects->setSession(session);
}

bool HistoryStore::canWriteRemaining(qint64 requestId, const QString &operation)
{
    if (!checkReady(requestId, operation)) return false;
    // Original report has no role gate. This additional offline write guard is
    // explicit: all authenticated legacy roles 1..5 may edit, Guest is read-only.
    // actionReady is a device-running flag and does not govern local metadata.
    if (!m_session.authenticated || m_session.role < 1 || m_session.role > 5
        || m_session.source == AccountSessionSource::Guest)
    {
        emit error(requestId, operation, QStringLiteral("Guest仅可读取报告；请先选择已认证的本地身份。"));
        return false;
    }
    return true;
}

void HistoryStore::hideReports(qint64 requestId, HistoryIds reportIds)
{
    const QString operation = QStringLiteral("hideReports");
    if (!canWriteRemaining(requestId, operation)) return;
    QSet<qint64> unique;
    if (reportIds.isEmpty() || reportIds.size() > 100)
    {
        emit error(requestId, operation, QStringLiteral("请选择1至100份报告。"));
        return;
    }
    for (qint64 id : reportIds)
    {
        if (id <= 0 || unique.contains(id))
        {
            emit error(requestId, operation, QStringLiteral("报告编号无效或重复；未隐藏任何报告。"));
            return;
        }
        unique.insert(id);
    }
    QString message;
    RemainingTransaction transaction(m_database);
    if (!transaction.begin(&message)) { emit error(requestId, operation, message); return; }
    for (qint64 id : reportIds)
    {
        QSqlQuery update(m_database);
        update.prepare(QStringLiteral("UPDATE report_data SET visible=0 WHERE id=? AND visible=1"));
        update.addBindValue(id);
        if (!update.exec() || update.numRowsAffected() != 1)
        {
            emit error(requestId, operation, update.lastError().isValid() ? update.lastError().text()
                : QStringLiteral("所选报告已隐藏或不存在；整批未改，请重新查询。"));
            return;
        }
    }
    if (!transaction.commit(&message)) { emit error(requestId, operation, message); return; }
    emit reportsHidden(requestId, reportIds);
}

bool HistoryStore::displayDefaults(HistoryDisplayColumns *columns, QString *message)
{
    columns->clear();
    // Keep the already accepted independent-page defaults. Keys are field IDs,
    // never translated titles, project display names or visual row numbers.
    columns->append({QStringLiteral("patient:name"), QStringLiteral("姓名"), 1, true, false});
    columns->append({QStringLiteral("patient:age"), QStringLiteral("年龄"), 1, true, false});
    columns->append({QStringLiteral("patient:sex"), QStringLiteral("性别"), 1, true, false});
    columns->append({QStringLiteral("patient:state"), QStringLiteral("状态"), 1, true, false});
    QMap<QString, bool> required;
    // The result-ingest profile has no anonymous display fixture. Unknown main
    // units remain required, using the same existing safe default below.
    if (!m_resultIngest)
    {
        QFile contract(QStringLiteral(":/fixtures/history_display_contract.json"));
        if (!contract.open(QIODevice::ReadOnly))
        { *message = QStringLiteral("匿名主显示合同缺失。"); return false; }
        const QJsonObject root = QJsonDocument::fromJson(contract.readAll()).object();
        if (root.value(QStringLiteral("schema")).toString() != QStringLiteral("sn100-anonymous-print-v1"))
        { *message = QStringLiteral("匿名主显示合同版本错误。"); return false; }
        for (const QJsonValue &value : root.value(QStringLiteral("columns")).toArray())
        {
            const QJsonObject column = value.toObject();
            // Missing main_result is unknown. Preserve it, rather than guessing
            // that an unknown column can be hidden from an old result.
            if (column.contains(QStringLiteral("main_result")))
                required.insert(resultKey(column.value(QStringLiteral("project")).toString(),
                    column.value(QStringLiteral("result_type")).toInt()), column.value(QStringLiteral("main_result")).toBool());
        }
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT project,result_type,label,unit FROM report_columns ORDER BY position LIMIT 65")))
    { *message = query.lastError().text(); return false; }
    int count = 0;
    while (query.next())
    {
        if (++count > 64) { *message = QStringLiteral("显示列超过64项，未截断。"); return false; }
        const QString key = resultKey(query.value(0).toString(), query.value(1).toInt());
        const QString unit = query.value(3).toString();
        columns->append({key, query.value(2).toString() + (unit.isEmpty() ? QString() : QChar(' ') + unit),
            2, true, required.value(key, true)});
    }
    if (query.lastError().isValid()) { *message = query.lastError().text(); return false; }
    return true;
}

bool HistoryStore::readDisplay(HistoryDisplayColumns *columns, HistoryDisplayColumns *defaults, QString *message)
{
    if (!displayDefaults(defaults, message)) return false;
    *columns = *defaults;
    QSqlQuery exists(m_database);
    if (!exists.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name='report_view_preferences'")))
    { *message = exists.lastError().text(); return false; }
    if (!exists.next()) return true;
    exists.finish();
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT column_key,group_id,position,visible FROM report_view_preferences ORDER BY group_id,position")))
    { *message = query.lastError().text(); return false; }
    QMap<QString, HistoryDisplayColumn> available;
    for (const auto &column : *defaults) available.insert(column.key, column);
    HistoryDisplayColumns saved;
    QSet<QString> seen;
    while (query.next())
    {
        const QString key = query.value(0).toString();
        if (!available.contains(key) || seen.contains(key)
            || available.value(key).group != query.value(1).toInt()
            || query.value(2).toInt() < 0 || (query.value(3).toInt() != 0 && query.value(3).toInt() != 1))
        { *message = QStringLiteral("保存的显示列合同不匹配，未忽略损坏设置。"); return false; }
        auto column = available.value(key);
        column.visible = query.value(3).toBool();
        if (column.required && !column.visible)
        { *message = QStringLiteral("主显示单位或未知主显示列不可隐藏。"); return false; }
        saved.append(column);
        seen.insert(key);
    }
    if (query.lastError().isValid()) { *message = query.lastError().text(); return false; }
    // No live catalog is read here. New anonymous result columns keep their
    // own saved unit/factor/precision; a display preference never rewrites them.
    for (const auto &column : *defaults) if (!seen.contains(column.key)) saved.append(column);
    *columns = saved;
    return true;
}

void HistoryStore::loadDisplay(qint64 requestId)
{
    const QString operation = QStringLiteral("loadDisplay");
    if (!checkReady(requestId, operation)) return;
    QString message;
    HistoryDisplayColumns columns, defaults;
    RemainingTransaction transaction(m_database);
    if (!transaction.begin(&message) || !readDisplay(&columns, &defaults, &message) || !transaction.commit(&message))
    { emit error(requestId, operation, message); return; }
    emit displayLoaded(requestId, columns, defaults);
}

void HistoryStore::saveDisplay(qint64 requestId, HistoryDisplayColumns columns)
{
    const QString operation = QStringLiteral("saveDisplay");
    if (!canWriteRemaining(requestId, operation)) return;
    QString message;
    RemainingTransaction transaction(m_database);
    if (!transaction.begin(&message)) { emit error(requestId, operation, message); return; }
    HistoryDisplayColumns defaults;
    if (!displayDefaults(&defaults, &message)) { emit error(requestId, operation, message); return; }
    QMap<QString, HistoryDisplayColumn> available;
    for (const auto &column : defaults) available.insert(column.key, column);
    QSet<QString> seen;
    if (columns.size() != defaults.size())
    { emit error(requestId, operation, QStringLiteral("显示列数量与已保存的历史合同不符。")); return; }
    for (auto &column : columns)
    {
        if (!available.contains(column.key) || seen.contains(column.key)
            || column.group != available.value(column.key).group
            || (!column.visible && available.value(column.key).required))
        { emit error(requestId, operation, QStringLiteral("显示列重复、未知或试图隐藏主显示单位。")); return; }
        seen.insert(column.key);
        // Titles and required flags are immutable server metadata, not UI input.
        const bool visible = column.visible;
        column = available.value(column.key);
        column.visible = visible;
    }
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS report_view_preferences("
        "column_key TEXT PRIMARY KEY,group_id INTEGER NOT NULL,position INTEGER NOT NULL,"
        "visible INTEGER NOT NULL,UNIQUE(group_id,position))"))
        || !query.exec(QStringLiteral("DELETE FROM report_view_preferences")))
    { emit error(requestId, operation, query.lastError().text()); return; }
    QMap<int, int> positions;
    for (const auto &column : columns)
    {
        query.prepare(QStringLiteral("INSERT INTO report_view_preferences VALUES(?,?,?,?)"));
        query.addBindValue(column.key);
        query.addBindValue(column.group);
        query.addBindValue(positions[column.group]++);
        query.addBindValue(column.visible ? 1 : 0);
        if (!query.exec()) { emit error(requestId, operation, query.lastError().text()); return; }
    }
    if (!transaction.commit(&message)) { emit error(requestId, operation, message); return; }
    emit displaySaved(requestId, columns);
}
