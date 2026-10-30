#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <algorithm>

static QByteArray hash(const QByteArray &bytes) { return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex(); }
static QByteArray fileHash(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly)?hash(f.readAll()):QByteArray(); }
static QByteArray rowBytes(const QSqlQuery &query)
{
    QByteArray bytes; QDataStream stream(&bytes,QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_5_15);
    for(int i=0;i<query.record().count();++i) stream << query.value(i);
    return bytes;
}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv); if(argc!=3) return 2;
    const QString path=QString::fromLocal8Bit(argv[1]);
    if(QFileInfo(path).isSymLink() || !QFileInfo(path).isAbsolute()) return 2;
    const QByteArray before=fileHash(path); if(before.isEmpty()) return 2;
    auto db=QSqlDatabase::addDatabase("QSQLITE");db.setDatabaseName(path);
    db.setConnectOptions("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=1000");
    if(!db.open() || !db.transaction()) return 3;
    QSqlQuery schema(db); if(!schema.exec("SELECT type,name,tbl_name,sql FROM sqlite_master ORDER BY type,name")) return 3;
    QByteArray schemaBytes; while(schema.next()) schemaBytes+=rowBytes(schema);
    QSqlQuery tables(db); if(!tables.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")) return 3;
    QStringList names;while(tables.next())names.append(tables.value(0).toString());
    QJsonArray results; QJsonObject sequences;
    for(const QString &name:names)
    {
        QString quoted=name;quoted.replace('"',"\"\"");QSqlQuery query(db);
        if(!query.exec("SELECT * FROM \""+quoted+"\"")) return 3;
        QList<QByteArray> rows;while(query.next()) rows.append(rowBytes(query));
        std::sort(rows.begin(),rows.end());QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
        for(const auto &row:rows)stream<<row;
        results.append(QJsonObject{{"table",name},{"rows",rows.size()},{"sha256",QString::fromLatin1(hash(bytes))}});
        if(name=="sqlite_sequence")
        {
            QSqlQuery counters(db);if(!counters.exec("SELECT name,seq FROM sqlite_sequence ORDER BY name"))return 3;
            while(counters.next())sequences.insert(counters.value(0).toString(),counters.value(1).toDouble());
        }
    }
    if(!db.commit())return 3;db.close();
    const QByteArray after=fileHash(path);if(after!=before)return 4;
    const QJsonObject out{{"readOnly",true},{"fileSha256",QString::fromLatin1(before)},
        {"schemaSha256",QString::fromLatin1(hash(schemaBytes))},{"tables",results},
        {"automaticNumberCounters",sequences},{"recordValuesPrinted",false}};
    QFile output(QString::fromLocal8Bit(argv[2]));
    if(!output.open(QIODevice::WriteOnly)||output.write(QJsonDocument(out).toJson())<=0)return 5;
    return 0;
}
