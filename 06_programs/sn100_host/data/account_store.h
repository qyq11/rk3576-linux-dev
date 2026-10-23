#ifndef SN100_ACCOUNT_STORE_H
#define SN100_ACCOUNT_STORE_H

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

// Deliberately excludes pwd: queued UI snapshots and formal evidence are public
// account fields only. The original pwd column stays in the isolated temp DB.
struct AccountRow
{
    qint64 id = 0;
    QString name;
    int role = 0;
    int isDefault = 0;
};

using AccountRows = QList<AccountRow>;

struct AccountSession
{
    QString name;
    int role = 0;
    bool actionReady = true;
    // Means an existing anonymous identity is selected for this offline test;
    // setSession is a simulation control, not a production login mechanism.
    bool authenticated = false;
};

Q_DECLARE_METATYPE(AccountRow)
Q_DECLARE_METATYPE(AccountRows)
Q_DECLARE_METATYPE(AccountSession)

class AccountStore : public QObject
{
    Q_OBJECT

public:
    explicit AccountStore(QObject *parent = nullptr);
    ~AccountStore() override;

    static void registerTypes();
    static QString roleName(int role);

public slots:
    // Nonempty bootstrapPassword seeds the five anonymous accounts only when
    // user_Information.db does not yet exist. Existing databases are never reset.
    // Passwords are transient inputs and are never included in signals/logs.
    void initialize(QString absoluteDataDir, QString bootstrapPassword = QString());
    void setSession(QString name, bool actionReady);
    // Returns all public rows; the page applies its original role/list filter.
    // No session or bootstrap secret is required for this sanitized read.
    void load();
    void add(QString name, QString password, QString again, int role);
    void remove(QString name, QString password);
    void update(QString oldName, QString oldPassword, QString newName,
                QString password, QString again, int role);
    void close();

signals:
    void ready();
    void sessionChanged(AccountSession session);
    void loaded(AccountRows rows);
    void succeeded(QString operation, bool loggedOut);
    void error(QString operation, QString message);
    void closed();

private:
    struct AccountRecord
    {
        AccountRow row;
        QString password;
    };

    bool checkThread(const QString &operation);
    bool checkReady(const QString &operation);
    bool beginWrite(QString *message);
    bool readAccount(const QString &name, AccountRecord *record, bool *found,
                     QString *message);
    bool refreshSession(QString *message);
    bool allowedRole(int role, bool update) const;
    bool commitWrite(const QString &operation, QString *message);
    void clearSession();
    void releaseDatabase();

    QSqlDatabase m_database;
    QString m_connectionName;
    AccountSession m_session;
    qint64 m_sessionId = 0;
    bool m_ready = false;
};

#endif
