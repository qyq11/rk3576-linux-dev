#ifndef SN100_ORIGINAL_TASK_REAGENT_RECEIPT_H
#define SN100_ORIGINAL_TASK_REAGENT_RECEIPT_H

#include <QByteArray>
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
    QVariant writtenUsed; // 本次真实g15 QString绑定，不从after重算。
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
    QString root, payloadSha256, settingTable;
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
