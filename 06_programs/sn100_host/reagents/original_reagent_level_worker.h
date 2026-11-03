#ifndef SN100_ORIGINAL_REAGENT_LEVEL_WORKER_H
#define SN100_ORIGINAL_REAGENT_LEVEL_WORKER_H

#include "original_reagent_level.h"
#include <QObject>
#include <atomic>
#include <memory>
#include <mutex>

namespace OriginalLevels
{
struct ObservationPresentation
{
    Context context;
    ReagentSnapshot snapshot;
    ReagentProjects projects;
    CurrentHoleRuntime after;
    ReagentTestCount selectedHoleTests;
    OriginalRuntime::ReagentCapacityResult capacity;
    QString previousSettingFingerprint;
    QString savedFingerprint;
};

// 构造后由调用者移到既有ReagentStore线程；本对象不创建线程或设备连接。
// 普通public方法仅操作内存或纯报文解码；prepareSource槽须排入所属存储线程。
class ObservationWorker : public QObject
{
    Q_OBJECT
public:
    explicit ObservationWorker(QObject *parent=nullptr);
    ~ObservationWorker() override;

    // 仅实际有效Qj的提供者调用。相同语义只续新鲜期限，不给旧报文更换连接代次。
    bool setObservationContext(Context context, int freshMs, QString *message=nullptr);
    // 任务只暂停新N准入，不撤销已受理N或原Ur回读的来源；开门后仍须新的实际Qj。
    // token在本worker内递增；相同连接和令牌的重复调用不重放开关，revoke也不清除此门。
    bool setTaskAdmissionFence(quint64 connection, quint64 token, bool blocked, QString *message=nullptr);
    // 任务期间的实际Qj只更新来源事实和期限，不签发新N准入。
    bool setTaskObservationFacts(Context context, int freshMs, quint64 token, QString *message=nullptr);
    void revoke();
    // runtime须来自独立原控件快照；不能从max_ul或整数显示模型临时补造。
    bool publishRuntime(quint64 arrivalConnection, const CurrentHoleRuntime &runtime, QString *message=nullptr);
    void invalidateRuntime(quint64 arrivalConnection, const QString &hole=QString());
    bool submit(quint64 arrivalConnection, quint64 localPacketId, QByteArray payload, QString *message=nullptr);
    bool isBusy() const;
    bool isContextCurrent(const Context &context) const; // GUI消费排队展示前再次核真实单调期限。
    // 原Ur等待只关闭N写入，实际Qj的到达语义仍供Ur来源认证；本方法不读库。
    void setReagentReadbackPending(quint64 connection, bool pending);

public slots:
    // 只预读主题及选定容量版本，不签发Qj许可；结果按request交调用者核对。
    void prepareSource(quint64 request, QString root);

signals:
    void sourceReady(quint64 request, OriginalLevels::Context source, QString message);
    // loaded仅表示后台重读核对通过；排队展示仍须核当前permissionRevision、连接和前/后指纹。
    void processed(OriginalLevels::Context context, bool committed, bool loaded, QString fingerprint, QString message);
    void presentationReady(OriginalLevels::ObservationPresentation presentation);

private:
    struct Permit;
    struct Job;
    mutable std::mutex mutex_;
    std::shared_ptr<Permit> permit_;
    QMap<QString,std::shared_ptr<const CurrentHoleRuntime>> runtimes_;
    std::shared_ptr<const Job> pending_;
    quint64 idConnection_=0, lastAcceptedPacket_=0;
    quint64 readbackConnection_=0;
    bool readbackPending_=true;
    quint64 taskFenceConnection_=0, taskFenceToken_=0;
    bool taskFenceBlocked_=false;
    bool observationAdmission_=false;

    bool setObservationContextImpl(Context context, int freshMs, quint64 taskToken, bool taskFacts, QString *message);
    bool currentLocked(const std::shared_ptr<const Job> &job) const;
    bool current(const std::shared_ptr<const Job> &job) const;
    void invalidateRelatedLocked(const std::shared_ptr<const Job> &job, const QString &savedFingerprint, bool committed);
    void process(const std::shared_ptr<const Job> &job);
};
}

Q_DECLARE_METATYPE(OriginalLevels::ObservationPresentation)

#endif
