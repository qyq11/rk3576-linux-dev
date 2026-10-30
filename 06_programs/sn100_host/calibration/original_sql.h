#ifndef SN100_CALIBRATION_ORIGINAL_SQL_H
#define SN100_CALIBRATION_ORIGINAL_SQL_H

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QUuid>
#include <QUrl>

// 原库只连接已部署的工作副本；这里不建表、不补列、不生成默认参数。
namespace Sn100OriginalSql
{
inline bool error(QString *message, const QString &text) { *message = text; return false; }
inline bool regular(const QString &path)
{
    if (!QDir::isAbsolutePath(path)) return false;
    for (QFileInfo entry(path); ; entry.setFile(entry.absolutePath()))
    {
        if (entry.isSymLink()) return false;
        if (entry.absoluteFilePath() == entry.absolutePath()) break;
    }
    return QFileInfo::exists(path);
}
inline bool root(QString *path, QString *message)
{
    *path = QDir::cleanPath(*path);
    if (!regular(*path) || !QFileInfo(*path).isDir() ||
        path->contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive) ||
        path->split('/').contains(QStringLiteral("baseline")))
        return error(message, QStringLiteral("请选择原目录分类下已部署的可写工作根，不能使用参考原件或链接路径。"));
    QFile marker(QDir(*path).filePath(QStringLiteral(".sn100-original-work-v1")));
    if (!regular(marker.fileName()) || !marker.open(QIODevice::ReadOnly) ||
        marker.readAll() != QByteArray("sn100-original-work-v1\n"))
        return error(message, QStringLiteral("原工作根缺少明确部署标记，未自动改动原文件。"));
    *path = QFileInfo(*path).canonicalFilePath();
    return true;
}
inline bool query(QSqlQuery *q, const QString &sql, const QVariantList &values, QString *message)
{
    if (!q->prepare(sql)) return error(message, q->lastError().text());
    for (const QVariant &v : values) q->addBindValue(v);
    if (!q->exec()) return error(message, q->lastError().text());
    return true;
}
inline bool execute(QSqlDatabase db, const QString &sql, const QVariantList &values, QString *message, int count = -1)
{
    QSqlQuery q(db);
    if (!query(&q, sql, values, message)) return false;
    if (count >= 0 && q.numRowsAffected() != count)
        return error(message, QStringLiteral("原记录已变化或已不存在，请重新读取；本次写入未完成。"));
    return true;
}
inline bool open(QSqlDatabase *db, QString *connection, const QString &path, QString *message)
{
    if (!regular(path) || !QFileInfo(path).isFile())
        return error(message, QStringLiteral("所需原数据库不存在或路径含链接，未创建替代数据库。"));
    *connection = QStringLiteral("OriginalBusiness-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    *db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), *connection);
    db->setDatabaseName(QStringLiteral("file:")+QString::fromLatin1(QUrl::toPercentEncoding(path,"/"))+QStringLiteral("?mode=rw"));
    db->setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI;QSQLITE_BUSY_TIMEOUT=1000"));
    if (!db->open()) return error(message, db->lastError().text());
    return true;
}
inline QString existingUri(const QString &path)
{
    return QStringLiteral("file:")+QString::fromLatin1(QUrl::toPercentEncoding(path,"/"))+QStringLiteral("?mode=rw");
}
inline QString fingerprint(const QString &root, const QStringList &files, QString *message)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(root.toUtf8()); hash.addData("\0", 1);
    for (const QString &relative : files)
    {
        const QString path = QDir(root).filePath(relative);
        for (const QString &suffix : {QString(), QStringLiteral("-wal")})
        {
            if (!suffix.isEmpty() && !QFileInfo::exists(path + suffix)) continue;
            QFile file(path + suffix);
            if (!regular(file.fileName()) || !file.open(QIODevice::ReadOnly) || !hash.addData(&file))
            { error(message, QStringLiteral("无法核对原数据库读入版本，请重新打开工作目录。")); return {}; }
            hash.addData(relative.toUtf8()); hash.addData(suffix.toUtf8()); hash.addData("\0", 1);
        }
    }
    return QString::fromLatin1(hash.result().toHex());
}
inline QMap<QString, QVariant> row(const QSqlQuery &q)
{
    QMap<QString, QVariant> values;
    const QSqlRecord record = q.record();
    for (int index = 0; index < record.count(); ++index) values.insert(record.fieldName(index), q.value(index));
    return values;
}
inline bool columns(QSqlDatabase db, const QString &table, const QStringList &required, QString *message)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT * FROM ") + table + QStringLiteral(" LIMIT 0")))
        return error(message, QStringLiteral("原数据表不存在或不能读取：") + table);
    for (const QString &name : required)
        if (q.record().indexOf(name) < 0) return error(message, QStringLiteral("原表缺少所需字段：") + table + '/' + name);
    return true;
}
}
#endif
