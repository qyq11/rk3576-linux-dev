#include "original_files/original_business_database.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>
#include <QUrl>

namespace OriginalBusiness
{
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
bool regularPath(const QString &path, bool directory, QString *message)
{
    const QFileInfo info(path);
    if (!info.isAbsolute() || !info.exists() || (directory ? !info.isDir() : !info.isFile()))
        return fail(message, QStringLiteral("原业务资料不存在或路径类型不符：") + path);
    for (QFileInfo current(info.absoluteFilePath()); ; current.setFile(current.absolutePath()))
    {
        if (current.isSymLink()) return fail(message, QStringLiteral("原业务资料路径不能经过符号链接。"));
        if (current.absoluteFilePath() == current.absolutePath()) break;
    }
    return true;
}
}

bool workingRoot(const QString &root, QString *message)
{
    if (!regularPath(root, true, message)) return false;
    const QStringList parts = QDir::fromNativeSeparators(QDir::cleanPath(root)).split('/');
    if (parts.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive) ||
        parts.contains(QStringLiteral("baseline"), Qt::CaseInsensitive))
        return fail(message, QStringLiteral("不能修改原参考工程或归档基线。"));
    const QString markerPath = QDir(root).filePath(QStringLiteral(".sn100-original-work-v1"));
    if (!regularPath(markerPath, false, message)) return false;
    QFile marker(markerPath);
    if (!marker.open(QIODevice::ReadOnly) || marker.readAll() != "sn100-original-work-v1\n")
        return fail(message, QStringLiteral("原资料工作根没有匹配的写许可标记。"));
    return true;
}

Database::Database() = default;
Database::~Database()
{
    if (db.isValid()) db.close();
    db = QSqlDatabase();
    if (!connection_.isEmpty()) QSqlDatabase::removeDatabase(connection_);
}

bool Database::open(const QString &root, const QString &relativeFile, bool writable, QString *message)
{
    if (!connection_.isEmpty()) return fail(message, QStringLiteral("原业务数据库不能重复打开。"));
    if (!workingRoot(root, message)) return false;
    if (QDir::isAbsolutePath(relativeFile) || relativeFile.contains(QStringLiteral("..")) ||
        relativeFile.contains(QLatin1Char('\\')))
        return fail(message, QStringLiteral("原业务数据库相对路径无效。"));
    const QString path = QDir(root).filePath(relativeFile);
    if (!regularPath(path, false, message)) return false;
    connection_ = QStringLiteral("original-business-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_);
    // 原文件必须已经存在；读写打开也用mode=rw，避免路径检查后文件消失而创建空库。
    db.setDatabaseName(writable
        ? QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded) + QStringLiteral("?mode=rw") : path);
    db.setConnectOptions(writable ? QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_BUSY_TIMEOUT=200")
                                 : QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=200"));
    if (!db.open()) return fail(message, QStringLiteral("原业务数据库无法打开：") + db.lastError().text());
    return true;
}

bool Database::rows(const QString &table, const QStringList &columns, Rows *result, QString *message)
{
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    if (!result || !identifier.match(table).hasMatch()) return fail(message, QStringLiteral("原业务表名无效。"));
    for (const auto &column : columns)
        if (!identifier.match(column).hasMatch()) return fail(message, QStringLiteral("原业务字段名无效。"));
    QSqlQuery query(db);
    QStringList quoted;
    for (const auto &column : columns) quoted << '"' + column + '"';
    if (!query.exec(QStringLiteral("SELECT %1 FROM \"%2\" ORDER BY id").arg(quoted.join(','), table)))
        return fail(message, QStringLiteral("原业务表结构无法读取：") + table + QStringLiteral("；") + query.lastError().text());
    Rows values;
    while (query.next())
    {
        if (values.size() >= 100000) return fail(message, QStringLiteral("原业务记录超过本次10万行读取上限，未截断。"));
        Row row;
        for (int i = 0; i < columns.size(); ++i) row.insert(columns[i], query.value(i));
        values.append(row);
    }
    if (query.lastError().isValid()) return fail(message, query.lastError().text());
    *result = values;
    return true;
}

bool Database::execute(const QString &text, const QVariantList &values, QString *message, int expectedRows)
{
    QSqlQuery query(db);
    if (!query.prepare(text)) return fail(message, query.lastError().text());
    for (const auto &value : values) query.addBindValue(value);
    if (!query.exec()) return fail(message, query.lastError().text());
    if (expectedRows >= 0 && query.numRowsAffected() != expectedRows)
        return fail(message, QStringLiteral("原记录已变化或不存在，未覆盖其他记录。"));
    return true;
}
bool Database::begin(QString *message)
{
    return execute(QStringLiteral("BEGIN IMMEDIATE"), {}, message);
}
bool Database::commit(QString *message)
{
    if (db.commit()) return true;
    return fail(message, db.lastError().text());
}
void Database::rollback() { db.rollback(); }
}
