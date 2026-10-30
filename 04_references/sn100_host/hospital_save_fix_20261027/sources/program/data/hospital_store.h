#ifndef HOSPITAL_STORE_H
#define HOSPITAL_STORE_H

#include "hospital_dictionary_query.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QByteArray>
#include <memory>
namespace OriginalBusiness { class Database; }

struct HospitalRow
{
    qint64 id = 0;
    QString columnOne;
    QString columnTwo;
    QString columnThree;
    QString type;
};

using HospitalRows = QList<HospitalRow>;

Q_DECLARE_METATYPE(HospitalRow)
Q_DECLARE_METATYPE(HospitalRows)

// Construct without a parent, move to a QThread, then invoke all slots through
// queued connections. The SQLite connection is created and removed there.
class HospitalStore : public QObject
{
    Q_OBJECT

public:
    explicit HospitalStore(QObject *parent = nullptr);
    ~HospitalStore() override;

    static void registerTypes();
    static bool isAllowedType(const QString &type);

public slots:
    // Both paths must be absolute paths in the explicitly selected data folder.
    // No reference database or configuration is discovered or imported.
    void initialize(QString databasePath, QString iniPath);
    void initializeOriginal(QString root);
    void load(QString type);
    void readAll();
    // Read only committed dictionary rows; reply identity is independent of editor operations.
    void queryCommittedDictionary(HospitalDictionaryRequest request);
    // Upsert the supplied current-category rows. Omitted rows are retained;
    // persistent deletion is only performed by remove(). New rows use id = 0.
    void save(QString type, QString hospitalName, HospitalRows rows);
    void remove(QString type, qint64 id);
    void close();

signals:
    void ready();
    void dictionaryQueryCompleted(HospitalDictionaryReply reply);
    void loaded(QString type, QString hospitalName, HospitalRows rows);
    void allLoaded(QString hospitalName, HospitalRows rows);
    // saved means the authoritative SQLite transaction has committed.
    // A failed derived INI export is reported separately through warning().
    void saved(QString type, QString hospitalName, HospitalRows rows);
    void removed(QString type, qint64 id);
    void error(QString operation, QString message);
    void warning(QString message);
    void closed();

private:
    bool checkThread(const QString &operation);
    bool checkReady(const QString &operation);
    bool readSnapshot(const QString &type, QString *hospitalName,
                      HospitalRows *rows, QString *message);
    bool validateSave(const QString &type, const QString &hospitalName,
                      const HospitalRows &rows, QString *message) const;
    bool exportIni(const QString &hospitalName, QString *message);
    bool refreshOriginalHospitalName(QString *message);
    void saveOriginal(QString type, QString hospitalName, HospitalRows rows);
    void releaseDatabase();

    QSqlDatabase m_database;
    QString m_connectionName;
    QString m_iniPath;
    bool m_ready = false;
    QString m_originalRoot;
    QString m_originalHospitalName;
    QString m_originalIniFingerprint;
    std::unique_ptr<OriginalBusiness::Database> m_originalDatabase;
};

#endif
