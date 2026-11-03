#ifndef SN100_TASK_REAGENT_CONTEXT_H
#define SN100_TASK_REAGENT_CONTEXT_H

#include "data/account_store.h"
#include "reagents/original_reagent_level.h"
#include "reagents/original_task_reagent_display.h"
#include <QMap>
#include <QQueue>
#include <QSet>
#include <chrono>
#include <memory>
class QTimer;

// 仅主窗口线程的有界记录。原包只存devicePackets_一次，这里不另存payload。
namespace TaskReagentRouting
{
using Clock=std::chrono::steady_clock;
enum class Stage { WaitingForN, Backend, Page };

struct Action
{
    quint64 id=0, packet=0, connection=0, epoch=0, processedSerial=0;
    OriginalTaskReagents::Action expected=OriginalTaskReagents::Action::None;
    QString root, payloadSha256;
    qint64 payloadBytes=0;
    OriginalTaskReagents::RuntimeBasis seed, basis;
    std::shared_ptr<const OriginalTaskReagents::Commit> receipt;
    OriginalTaskReagents::RuntimeAdoption adoption;
    QByteArray receiptSignature;
    Stage stage=Stage::WaitingForN;
    bool basisEligible=false, dispatched=false, processed=false, cancelled=false;
    bool observed=false, receiptAuthorized=false, receiptConflict=false, pageRequested=false, enqueueing=false, ackSeen=false, readerPending=false;
    bool displayUnknown=false, transactionUnknown=false, unscoped=false, waitWarned=false;
    QMap<QString,qint64> unresolvedHoles;
    QString message;
    Clock::time_point arrived=Clock::now();
};

struct SourceTicket
{
    quint64 epoch=0, connection=0, identityRevision=0, observationPermission=0, afterProcessedSerial=0;
    QString root, payloadSha256;
    OriginalLevels::MaximumSource kind=OriginalLevels::MaximumSource::Unknown;
    bool queued=false, full=false;
    QMap<QString,qint64> rows;
    QMap<QString,quint64> beforeRevision;
    Clock::time_point deadline;
};

struct SourceEvidence
{
    quint64 identityRevision=0, runtimeRevision=0, afterProcessedSerial=0;
    qint64 rowId=0;
    QString root;
    OriginalLevels::MaximumSource kind=OriginalLevels::MaximumSource::Unknown;
};

struct DisplayLatch
{
    QString root;
    quint64 generation=0, afterProcessedSerial=0;
    bool unscoped=false;
    QMap<QString,qint64> holes;
};

struct TransactionFact
{
    quint64 actionId=0, packet=0, connection=0, epoch=0;
    QString root, receiptId, payloadSha256;
    bool receiptAvailable=false, committed=false;
    OriginalTaskReagents::TransactionState transaction=OriginalTaskReagents::TransactionState::NotStarted;
    bool unscoped=false;
    QMap<QString,qint64> holes; // 独立保留事务待核范围，新的液面来源不能解除它。
};

struct Terminal
{
    quint64 id=0, packet=0, connection=0, epoch=0;
    QString root, receiptId, basisToken, payloadSha256;
    QByteArray receiptSignature;
    QByteArray basisSignature;
    OriginalTaskReagents::Action expected=OriginalTaskReagents::Action::None;
    bool committed=false, snapshotAccepted=false, runtimeApplied=false;
};

struct Bridge
{
    QMap<quint64,Action> actions; // 仅活跃动作与原32包/1MiB合计记账，B历史不占业务包预算。
    QQueue<quint64> waiting;
    QMap<quint64,quint64> nReaders; // 包编号对应连接编号；只在实际液面读取结束后移除。
    quint64 nextAction=0, epoch=1, identityRevision=0, fenceToken=0, fenceConnection=0, processedSerial=0;
    bool attached=false, closing=false, closeRetryNeeded=false, identityChanging=false, pumpQueued=false, fenceClosed=false, awaitFreshQj=false;
    bool readerCloseIntent=false, readerCloseRetryPosted=false;
    QTimer *actionDeadlineTimer=nullptr;
    OriginalTaskReagents::TaskIdentity identity;
    AccountSession identitySession;
    std::shared_ptr<const OriginalLevels::Context> verifiedSource;
    std::shared_ptr<SourceTicket> sourceTicket;
    QMap<QString,SourceEvidence> confirmedSources; // 只登记当前活动盘孔名，真实身份变化时清空。
    QMap<QString,DisplayLatch> displayUnknown; // 最多8个资料根；超界另留全局未知闩锁，不拒绝合法任务。
    bool globalDisplayUnknown=false, transactionOverflow=false;
    quint64 displayGeneration=0, globalAfterProcessedSerial=0, independentFullIdentity=0, independentFullSerial=0;
    QMap<QString,TransactionFact> transactions; // 正常路径首个C即阻后续Tt/TR，最多32事实。
    QMap<quint64,Terminal> terminal; // 最多32份结束元数据；淘汰不清B/C，不持payload或typed全行。
};

bool sameTaskSession(const AccountSession &a,const AccountSession &b);
QByteArray taskBasisBytes(const OriginalTaskReagents::RuntimeBasis &basis);
QByteArray taskCommitSignature(const OriginalTaskReagents::Commit &receipt);
}
#endif
