#ifndef SN100_ORIGINAL_SYSTEM_PROFILE_H
#define SN100_ORIGINAL_SYSTEM_PROFILE_H

#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace OriginalRuntime
{
struct PermissionRule
{
    QString description;
    QSet<int> roleIds;
};
struct UserDictionary
{
    QMap<int, QString> roles;
    QMap<QString, PermissionRule> powers;
    bool allows(const QString &powerId, int roleId) const;
    QStringList grantedPowers(int roleId) const;
};
// user.db仅含角色/权限字典；账户与口令仍由AccountStore在其工作线程处理。
bool loadUserDictionary(const QString &root, UserDictionary *result, QString *message);

struct Channel
{
    qint64 id = 0;
    QString number;
    bool enabled = false;
    bool warmupOnly = false;
};
struct Channels
{
    QVector<Channel> test;
    QVector<Channel> warm;
    QVector<Channel> cache;
    QString fingerprint;
    QStringList activeTestNumbers(bool includeWarmupOnly = false) const;
};
struct ResultCheckRule
{
    qint64 id = 0;
    QString description;
    QString type;
    QString chineseMessage;
    QString englishMessage;
    int bitIndex = 0;
};
struct AlarmRule
{
    QString id;
    QString code;
    QString description;
    QString measure;
    QString idleAction;
    QString testingAction;
    QString testStatus;
    bool enabled = false;
    QString parameter;
    QStringList originalColumns;
};
struct SystemProfile
{
    Channels channels;
    UserDictionary users;
    QVector<ResultCheckRule> resultChecks;
    QVector<AlarmRule> alarms;
    QStringList messagesForBits(const QVector<bool> &bits, bool english = false) const;
    bool mayStartWithAlarms(const QStringList &codes, QString *reason) const;
};
bool loadSystemProfile(const QString &root, SystemProfile *result, QString *message);
// 原Windows只开放test_channel的is_open修改；其他通道字段与字典保持只读。
bool setTestChannelEnabled(const QString &root, const QString &number, bool enabled,
                           const QString &expectedFingerprint, QString *message);
}

#endif
