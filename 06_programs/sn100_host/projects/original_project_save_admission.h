#ifndef SN100_ORIGINAL_PROJECT_SAVE_ADMISSION_H
#define SN100_ORIGINAL_PROJECT_SAVE_ADMISSION_H
#include "original_project_save_notice.h"
#include "data/account_store.h"
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <chrono>
#include <memory>

// 仅限同进程各线程使用的单调毫秒值；不是日期时间，也不保存到业务数据库。
inline qint64 originalProjectSaveMonotonicNow()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline quint64 nextOriginalProjectSaveTicket()
{
    static std::atomic<quint64> sequence{0};
    return ++sequence;
}
inline bool sameOriginalProjectSaveActor(const AccountSession &a, const AccountSession &b)
{
    // 运行态另有原编辑守卫；它的变化不冒充注销或换账户。
    return a.authenticated==b.authenticated && a.name==b.name && a.role==b.role && a.source==b.source &&
        a.originalPolicySource==b.originalPolicySource && a.grantedPowerIds==b.grantedPowerIds;
}
inline bool sameOriginalProjectSave(const OriginalProjectSaveNotice &a, const OriginalProjectSaveNotice &b)
{
    return a.source==b.source && a.writeRequest==b.writeRequest && a.root==b.root &&
        a.originalFileName==b.originalFileName && a.projectId==b.projectId && a.beforeFingerprint==b.beforeFingerprint;
}

// 跨线程只共享这份纯内存许可；不持QObject、数据库或文件锁。
class OriginalProjectSavePermit
{
public:
    enum State { Granted, Claimed, Committed, Released, Revoked, Accepted };
    const quint64 ticket;
    const OriginalProjectSaveNotice proposal;
    const qint64 sourceDeadline;
    explicit OriginalProjectSavePermit(quint64 value, const OriginalProjectSaveNotice &notice, qint64 expiresAt)
        : ticket(value), proposal(notice), sourceDeadline(expiresAt),
          deadline_(qMin(sourceDeadline,originalProjectSaveMonotonicNow()+10000)) {}
    int state() const { return state_.load(); }
    bool expired() const { return sourceDeadline<=0 || originalProjectSaveMonotonicNow()>=deadline_; }
    bool claim(const OriginalProjectSaveNotice &notice)
    {
        // 仓库校验也消耗源请求期限；必须在锁内最终认领前再看同一单调截止点。
        if (!sameOriginalProjectSave(proposal,notice) || expired()) { revoke(); return false; }
        int expected=Granted;
        return state_.compare_exchange_strong(expected,Claimed);
    }
    void revoke() { int expected=Granted; state_.compare_exchange_strong(expected,Revoked); }
    void releaseUncommitted()
    {
        int expected=Granted;
        if (!state_.compare_exchange_strong(expected,Released))
        { expected=Claimed; state_.compare_exchange_strong(expected,Released); }
    }
    void committed(const OriginalProjectSaveNotice &notice)
    {
        QMutexLocker lock(&mutex_);
        if (state()!=Claimed || !notice.commitSeen || !sameOriginalProjectSave(proposal,notice)) return;
        committedNotice_=notice;
        state_.store(Committed);
    }
    OriginalProjectSaveNotice committedNotice() const
    { QMutexLocker lock(&mutex_); return committedNotice_; }
    bool accept(const OriginalProjectSaveNotice &notice)
    {
        QMutexLocker lock(&mutex_);
        if (state()!=Committed || !sameOriginalProjectSave(committedNotice_,notice) ||
            committedNotice_.committedFingerprint!=notice.committedFingerprint ||
            committedNotice_.verifiedSnapshot!=notice.verifiedSnapshot) return false;
        state_.store(Accepted); return true;
    }
private:
    std::atomic<int> state_{Granted};
    const qint64 deadline_;
    mutable QMutex mutex_;
    OriginalProjectSaveNotice committedNotice_;
};
struct OriginalProjectSaveAdmission
{
    quint64 ticket=0;
    OriginalProjectSaveNotice proposal;
    qint64 sourceDeadline=0;
    bool allowed=false;
    QString message;
    std::shared_ptr<OriginalProjectSavePermit> permit;
};
Q_DECLARE_METATYPE(OriginalProjectSaveAdmission)
#endif
