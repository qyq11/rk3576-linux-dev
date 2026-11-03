#ifndef SN100_ORIGINAL_SYSTEM_PROFILE_H
#define SN100_ORIGINAL_SYSTEM_PROFILE_H

#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

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

// Uh回读保留出现顺序；重复名称逐条处理，不插入未知通道，不关闭未列通道。
struct ChannelSwitchUpdate
{
    QString number;
    bool enabled = false;
};
enum class ChannelApplyOutcome
{
    Committed,
    Cancelled,
    Failed,
    CommittedReloadFailed
};
struct ChannelApplyResult
{
    ChannelApplyOutcome outcome = ChannelApplyOutcome::Failed;
    Channels channels;
    QString message;
    // 仅成功COMMIT后统计；重复项的每次实际改变各计1，不是最终不同通道数。
    int changedRows = 0;
    int unknownRows = 0;
};
class ChannelCommitPermit;
using ChannelCommitPermitPtr = std::shared_ptr<ChannelCommitPermit>;

// GUI只做原子撤销，不等SQLite。提交票先取得时，后续撤销不能抹去提交事实。
class ChannelCommitPermit
{
public:
    enum class State { Active, Revoked, Committing, Committed };
    ChannelCommitPermit() = default;
    ChannelCommitPermit(const ChannelCommitPermit &) = delete;
    ChannelCommitPermit &operator=(const ChannelCommitPermit &) = delete;
    void revoke() noexcept;
    bool tryBeginCommit() noexcept;
    State state() const noexcept;

private:
    void markCommitted() noexcept;
    std::atomic<State> state_{State::Active};
    friend ChannelApplyResult applyTestChannelReadback(const QString &, const QVector<ChannelSwitchUpdate> &,
                                                      const QString &, const ChannelCommitPermitPtr &);
};

// 同步数据库接口，仅由worker调用。新接口限16MiB、每表1024行、每包1024项。
// 只支持DELETE回滚日志；拒绝WAL/遗留日志，不改变原数据库PRAGMA或旧接口行为。
// 新写入拒绝检测表触发器及启用外键联动的连接，防止is_open改变其他字段/表。
bool readChannelsSnapshot(const QString &root, Channels *result, QString *message);
ChannelApplyResult applyTestChannelReadback(const QString &root, const QVector<ChannelSwitchUpdate> &updates,
                                           const QString &expectedFingerprint, const ChannelCommitPermitPtr &permit);
}

#endif
