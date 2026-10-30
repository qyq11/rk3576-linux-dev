#ifndef SN100_ORIGINAL_BUSINESS_DATABASE_H
#define SN100_ORIGINAL_BUSINESS_DATABASE_H
#include <QMap>
#include <QSqlDatabase>
#include <QVariant>

// 所有对象只在调用者所属数据库工作线程使用；不建表、不转换原文件。
namespace OriginalBusiness
{
using Row = QMap<QString, QVariant>;
using Rows = QList<Row>;
bool workingRoot(const QString &root, QString *message);
class Database
{
public:
    Database();
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    bool open(const QString &root, const QString &relativeFile, bool writable, QString *message);
    bool rows(const QString &table, const QStringList &columns, Rows *result, QString *message);
    bool execute(const QString &sql, const QVariantList &values, QString *message, int expectedRows = -1);
    bool begin(QString *message);
    bool commit(QString *message);
    void rollback();
    QSqlDatabase db;
private:
    QString connection_;
};
}
#endif
