#ifndef SN100_ORIGINAL_TASK_REAGENT_DISPLAY_H
#define SN100_ORIGINAL_TASK_REAGENT_DISPLAY_H

#include "samples/original_task_reagent_receipt.h"

// 页面采用的值回执。数据库是否提交与运行量是否采用分开，不能据此重发或退量。
namespace OriginalTaskReagents
{
// Root按真实连接、资料和机型/协议来源登记；检测状态与N观察号不属于任务身份。
struct TaskIdentity
{
    bool available=false;
    quint64 connection=0, revision=0;
    QString root, actualModel, configuredModel, protocolToken, themeFingerprint;
    QString capacityRelativeFile, capacityFingerprint, capacityFileState;
    int configuredPositions=0, reportedPositions=0;
    bool capacityCalibrationActive=false;
};

struct HoleRuntimeAdoption
{
    qint64 rowId=0;
    QString hole;
    bool runtimeApplied=false, available=false, changed=false;
    quint64 revision=0;
    QString reason;
};

struct RuntimeAdoption
{
    OperationSource source=OperationSource::DevicePacket;
    quint64 userRequestId=0;
    QString originDigest;
    quint64 actionId=0, arrivalPacketId=0, connection=0, permissionRevision=0;
    QString receiptId, basisToken, root, payloadSha256;
    TransactionState transaction=TransactionState::NotStarted;
    bool committed=false, snapshotAccepted=false, runtimeApplied=false, duplicate=false;
    QList<HoleRuntimeAdoption> holes;
    QString message;
};

// 只核凭据形状、完整原行链与固定投影，不开库、不重新分配/扣量。
bool verifyDisplayCommit(const Commit &receipt, QString *message);
}
Q_DECLARE_METATYPE(OriginalTaskReagents::TaskIdentity)
Q_DECLARE_METATYPE(OriginalTaskReagents::RuntimeAdoption)
#endif
