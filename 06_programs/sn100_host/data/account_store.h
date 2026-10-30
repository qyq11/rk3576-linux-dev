#ifndef SN100_ACCOUNT_STORE_H
#define SN100_ACCOUNT_STORE_H

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QMap>
#include <memory>
namespace OriginalBusiness { class Database; }

// Deliberately excludes pwd: queued UI snapshots and formal evidence are public
// account fields only. The original pwd column stays in the isolated temp DB.
struct AccountRow
{
    qint64 id = 0;
    QString name;
    int role = 0;
    int isDefault = 0;
    QString roleLabel; // 原模式从user.db读取显示名称，不按旧静态翻译猜测。
};

using AccountRows = QList<AccountRow>;

enum class AccountSessionSource
{
    Guest,
    Simulated,
    Password,
    Configured // 原config_Debug明确指定的默认角色；用户名仍为Guest，不冒充账户表身份。
};

struct AccountSession
{
    QString name;
    int role = 0;
    bool actionReady = true;
    // 是否已经建立可操作的会话；source区分口令认证、原配置默认角色及旧独立模拟。
    bool authenticated = false;
    AccountSessionSource source = AccountSessionSource::Guest;
    QString roleLabel;
    QString originalPolicySource; // 已读取角色/权限的原工作根；空值沿用旧独立模式。
    QStringList grantedPowerIds; // 原user_powers的明确授权，不根据角色高低补权。
};

Q_DECLARE_METATYPE(AccountRow)
Q_DECLARE_METATYPE(AccountRows)
Q_DECLARE_METATYPE(AccountSession)
Q_DECLARE_METATYPE(AccountSessionSource)

class AccountStore : public QObject
{
    Q_OBJECT

public:
    explicit AccountStore(QObject *parent = nullptr);
    ~AccountStore() override;

    static void registerTypes();
    static QString roleName(int role);
    static bool hasOriginalPower(const AccountSession &session, const QString &powerId);

public slots:
    // Nonempty bootstrapPassword seeds the five anonymous accounts only when
    // user_Information.db does not yet exist. Existing databases are never reset.
    // Passwords are transient inputs and are never included in signals/logs.
    void initialize(QString absoluteDataDir, QString bootstrapPassword = QString());
    void initializeOriginal(QString root);
    // 仅当原config_Debug.ini关闭登录时建立其默认角色，不接受外部传入角色或用户名。
    // 使用loginFinished回执；空口令登录另按原窗口同样采用默认角色。
    void applyOriginalConfiguredSession(quint64 requestId);
    void setSession(QString name, bool actionReady);
    // 密码认证与模拟身份选择分开，任何结果都不携带输入密码。
    void loadLoginDefault(quint64 requestId);
    void authenticate(quint64 requestId, QString name, QString password, bool remember);
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
    void defaultLoginLoaded(quint64 requestId, QString name, QString warning);
    void loginFinished(quint64 requestId, bool accepted, bool defaultSaved,
                       QString error, QString warning);
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
    bool loginAvailable(QString *message) const;
    bool saveDefaultLoginName(const QString &name, QString *message);
    bool beginWrite(QString *message);
    bool readAccount(const QString &name, AccountRecord *record, bool *found,
                     QString *message);
    bool refreshSession(QString *message);
    bool allowedRole(int role, bool update) const;
    bool refreshOriginalPolicy(QString *message);
    bool establishConfiguredSession(bool requireLoginDisabled, QString *message);
    bool commitWrite(const QString &operation, QString *message);
    void clearSession();
    void releaseDatabase();

    QSqlDatabase m_database;
    QString m_connectionName;
    AccountSession m_session;
    qint64 m_sessionId = 0;
    bool m_ready = false;
    QString m_originalRoot;
    QMap<int, QString> m_originalRoles;
    std::unique_ptr<OriginalBusiness::Database> m_originalDatabase;
    bool m_configuredRequiresDisabled = false;
};

#endif
