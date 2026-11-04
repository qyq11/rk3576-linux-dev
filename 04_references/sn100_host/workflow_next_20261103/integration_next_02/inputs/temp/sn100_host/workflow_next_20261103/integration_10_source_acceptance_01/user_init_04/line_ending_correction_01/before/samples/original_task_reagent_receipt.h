#ifndef SN100_ORIGINAL_TASK_REAGENT_RECEIPT_H
#define SN100_ORIGINAL_TASK_REAGENT_RECEIPT_H

#include <QByteArray>
#include <QCryptographicHash>
#include <QDataStream>
#include "sample_types.h"
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <limits>

// 仅值拷贝；不依赖N、界面对象或SQL连接。used是任务预扣后的可分配剩余量。
namespace OriginalTaskReagents
{
enum class Action { None, RackReservation, UnusedReservationReturn };
enum class TransactionState { NotStarted, NotCommitted, CommitUnconfirmed, Committed };

// 内部来源不改变线协议。用户操作没有设备原包，也没有真实到包号。
enum class OperationSource { DevicePacket, UserOperation };
struct UserOperation
{
    quint64 requestId=0, connection=0;
    Action allowedAction=Action::None;
    QString root, accountName, policyRoot, sampleSnapshotDigest;
    int role=0, sessionSource=0;
    bool authenticated=false;
    QStringList grantedPowerIds;
    QMap<QString,QString> configurationVersions, projectVersions;
};
inline QByteArray userOperationBytes(const UserOperation &u)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<QStringLiteral("SN100-user-operation-v1")<<u.sampleSnapshotDigest;
    out<<u.requestId<<u.connection<<int(u.allowedAction)<<u.root<<u.accountName<<u.policyRoot
       <<u.role<<u.sessionSource<<u.authenticated<<u.grantedPowerIds<<u.configurationVersions<<u.projectVersions;
    return bytes;
}
// 只绑定已保存的任务输入；TB剩余时间和组合目录不改变这次输入身份。
inline QString sampleTaskInputDigest(const SampleSnapshot &snapshot)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<QStringLiteral("SN100-sample-task-input-v1")<<snapshot.racks.size();
    for (const auto &rack : snapshot.racks)
    {
        out<<rack.id<<rack.name<<rack.emergency<<rack.holes.size();
        for (const auto &hole : rack.holes)
        {
            out<<hole.number<<hole.sampleId<<hole.container<<hole.revise<<hole.sampleType<<hole.tasks.size();
            for (const auto &task : hole.tasks)
            {
                out<<task.projectId<<task.rate<<task.repeats<<task.prolonged<<task.calibrationPoint<<task.calibrationConcentration<<task.states.size();
                for (const auto state : task.states) out<<int(state);
            }
            const auto &c=hole.calibration;
            out<<c.projectId<<c.groupToken<<c.calibratorBatch<<c.reagentBatchGroup<<c.autoDilution<<c.pointCount<<c.repeatCount
               <<c.baselineConcentration<<c.rates<<c.concentrations<<c.holes;
            const auto &q=hole.quality.selected;
            out<<hole.quality.slotToken<<hole.quality.changeToken<<q.datasetId<<q.controlId<<q.batchId
               <<q.controlName<<q.batchName<<q.sampleId<<q.projects.size();
            for (const auto &project : q.projects) out<<project.associationId<<project.projectId;
        }
    }
    return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
}
inline QString userOperationDigest(const UserOperation &u)
{ return QString::fromLatin1(QCryptographicHash::hash(userOperationBytes(u),QCryptographicHash::Sha256).toHex()); }

struct HoleRuntimeBasis
{
    bool available=false;
    qint64 rowId=0;
    QString hole;
    QVariantMap originalIdentity;
    quint64 revision=0;
    QString maximumSource; // GUI原来源的明确名称；Backend只回交，不从DB建立来源。
    double maximumMicrolitres=std::numeric_limits<double>::quiet_NaN();
    double amountMicrolitres=std::numeric_limits<double>::quiet_NaN();
    double usedMicrolitres=std::numeric_limits<double>::quiet_NaN();
    QString state;
    QString unavailableReason;
};

struct RuntimeBasis
{
    OperationSource source=OperationSource::DevicePacket;
    UserOperation userOperation;
    QString originDigest; // 原设备字节或用户意图各自的摘要；不把用户意图放进payloadSha256。
    bool available=false;
    quint64 actionId=0, arrivalPacketId=0, connection=0, basisRevision=0, permissionRevision=0;
    QString root, payloadSha256, basisToken;
    QString protocolToken, actualModel, configuredModel, themeFingerprint;
    QString capacityRelativeFile, capacityFingerprint, capacityFileState;
    int configuredPositions=0, reportedPositions=0, mainStatus=-1;
    bool capacityCalibrationActive=false;
    QMap<QString,QString> versions; // Information/Bottle/Setting三库，与任务结果事务三库分开。
    QMap<QString,HoleRuntimeBasis> holes;
    QString unavailableReason;
};

inline bool validTaskOrigin(const RuntimeBasis &b)
{
    if (!b.actionId || !b.connection || b.root.isEmpty() || b.originDigest.isEmpty()) return false;
    if (b.source==OperationSource::DevicePacket)
        return b.arrivalPacketId && !b.payloadSha256.isEmpty() && b.originDigest==b.payloadSha256 &&
            userOperationBytes(b.userOperation)==userOperationBytes(UserOperation());
    if (b.source!=OperationSource::UserOperation || b.arrivalPacketId || !b.payloadSha256.isEmpty()) return false;
    const auto &u=b.userOperation;
    if (!u.requestId || u.connection!=b.connection || u.root!=b.root || !u.authenticated ||
        u.role<0 || u.role>5 || u.accountName.isEmpty() || u.policyRoot!=u.root ||
        (u.allowedAction!=Action::None && u.allowedAction!=Action::RackReservation) ||
        u.sampleSnapshotDigest.isEmpty() || u.configurationVersions.size()!=3 || u.projectVersions.isEmpty() || b.originDigest!=userOperationDigest(u)) return false;
    for (const auto &key : {QStringLiteral("config.ini"),QStringLiteral("config_Debug.ini"),QStringLiteral("theme_config.ini")})
        if (u.configurationVersions.value(key).isEmpty()) return false;
    for (auto p=u.projectVersions.cbegin();p!=u.projectVersions.cend();++p)
        if (p.key().isEmpty() || p.value().isEmpty()) return false;
    return true;
}
inline bool taskOriginAllows(const RuntimeBasis &b,Action action)
{
    if (!validTaskOrigin(b)) return false;
    if (b.source==OperationSource::DevicePacket)
        return action==Action::RackReservation || action==Action::UnusedReservationReturn;
    return action==b.userOperation.allowedAction; // 用户意图不能申请TR退量。
}
inline QByteArray taskOriginBytes(const RuntimeBasis &b)
{
    QByteArray bytes; QDataStream out(&bytes,QIODevice::WriteOnly); out.setVersion(QDataStream::Qt_5_12);
    out<<int(b.source)<<b.originDigest<<userOperationBytes(b.userOperation);
    return bytes;
}

struct TypedRow
{
    QVariantMap values; // 保留原QVariant类型和null，未知厂商列不转成QString。
    QMap<QString,QString> sqliteTypes; // 由同次SELECT typeof逐列取得，不能从绑定类型推断。
};
struct TypedPlate
{
    bool complete=false;
    QString table;
    QStringList columns;
    QList<TypedRow> rows;
};

struct SqlHoleWrite
{
    qint64 rowId=0;
    QString hole;
    QVariant predicateUsed; // 原WHERE绑定值，空TEXT与NULL不互换。
    QVariant writtenUsed; // 本次真实QString绑定原文，不从after重算或再次格式化。
    QStringList columns; // 本片仅used_ul；行数匹配成功后才加入此集合。
};
struct RefreshHole
{
    qint64 rowId=0;
    QString hole;
    QVariant storedUsed;
    QString sqliteType;
    bool trustedBasis=false;
    bool displayConversionVerified=false;
    double usedMicrolitres=std::numeric_limits<double>::quiet_NaN();
    QString message;
};

struct TaskOrigin
{
    qint64 taskId=0, reportRow=0, projectId=0, calibrationId=0;
    QString rack, type, projectName, projectFile, projectFingerprint;
    int hole=0, repeatIndex=0, calibrationPoint=0;
    QList<QPair<QString,QString>> reagentSlots;
    QMap<qint64,double> reservedByHole, returnedByHole;
    bool skippedAdded=false;
};

struct Commit
{
    QString receiptId; // 本进程一次writer尝试的本地标识，不是设备请求号或跨进程对账资格。
    Action action=Action::None;
    RuntimeBasis basis;
    QString root, payloadSha256, originDigest, settingTable;
    TransactionState transaction=TransactionState::NotStarted;
    bool committed=false;
    bool reagentSqlWritten=false; // 是否实际执行used UPDATE；不等同于整笔COMMIT。
    bool postCommitVerified=false; // 仅锁内after全盘/三库/列范围，不等于页面显示或N可写。
    bool basisMatchesDatabaseBefore=false; // 只核原root/版本/孔身份；不证明GUI当前revision/角色/连接可采用。
    QMap<QString,QString> beforeVersions, afterVersions;
    TypedPlate beforePlate, afterPlate;
    QList<SqlHoleWrite> sqlWrites;
    QList<RefreshHole> refreshHoles; // 整盘after；不能把它当SQL写孔列表。
    QList<TaskOrigin> tasks;
    QList<qint64> alreadyHandledTaskIds, skippedAddedTaskIds, refusedUncertainTaskIds;
    QString proofMessage;
};
using CommitHandler=std::function<void(const Commit &)>;
// 回调只保存内存/排值信号；不得SQL重入或持资料锁BlockingQueued询问GUI。
}

Q_DECLARE_METATYPE(OriginalTaskReagents::RuntimeBasis)
Q_DECLARE_METATYPE(OriginalTaskReagents::Commit)
#endif
