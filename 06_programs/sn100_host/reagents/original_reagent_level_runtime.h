#ifndef SN100_ORIGINAL_REAGENT_LEVEL_RUNTIME_H
#define SN100_ORIGINAL_REAGENT_LEVEL_RUNTIME_H

#include "original_reagent_level.h"
#include "original_task_reagent_display.h"

namespace OriginalLevels
{
// 只维护已知的孔位运行值；单线程使用，不开库、不发命令。
// 连接/身份许可由调用者核对，失效时须invalidate，不能给旧包重贴新revision。
class RuntimeProvider
{
public:
    void observeSnapshot(const ReagentSnapshot &snapshot);
    CurrentHoleRuntime current(const QString &hole) const;
    QString diagnostic(const QString &hole) const;
    void invalidate(const QString &hole = QString());
    bool confirmUr(const QByteArray &payload, bool linuxNew, const ReagentSnapshot &snapshot, QString *message);
    bool confirmManualHole(const QString &hole, const ReagentSnapshot &snapshot, QString *message);
    // 仅供已确认committed的展示入口：after须绑定保存指纹，revision须为可信前版+1。
    // 纯prepare的空保存指纹不是提交凭据；本入口不替代writer或外层连接/许可核对。
    // 接受后current(hole)可能因全局时钟取得更高revision；后续发布须读current，不能重贴after。
    bool acceptObservation(const QString &beforeSettingFingerprint, const CurrentHoleRuntime &after,
                           const ReagentSnapshot &persistedSnapshot, QString *message);

    // 任务专口：调用者先冻结真实动作上下文；采用须在普通observeSnapshot之前。
    OriginalTaskReagents::RuntimeBasis freezeTaskBasis(OriginalTaskReagents::RuntimeBasis seed, QString *message,
                                                      const QStringList &activeHoles) const;
    void cancelTaskBasis(const QString &token);
    bool taskBasisCurrent(const OriginalTaskReagents::RuntimeBasis &basis, QString *message) const;
    OriginalTaskReagents::RuntimeAdoption acceptTaskCommit(const OriginalTaskReagents::Commit &receipt,
                                                          const ReagentSnapshot &persistedSnapshot);
    OriginalTaskReagents::RuntimeAdoption rejectTaskCommit(const OriginalTaskReagents::Commit &receipt, const QString &reason);
    // 仅真实COMMIT结果无法确认时撤仍属原basis的旧来源；不把未知改成已提交或采用。
    OriginalTaskReagents::RuntimeAdoption markTaskUnconfirmed(const OriginalTaskReagents::Commit &receipt, const QString &reason);

private:
    void put(CurrentHoleRuntime runtime, const QString &reason);
    OriginalTaskReagents::RuntimeAdoption rejectTaskReceipt(const OriginalTaskReagents::Commit &receipt,
                                                           const QString &reason, bool mayWithdraw);
    ReagentSnapshot snapshot_, previousSnapshot_;
    QMap<QString, CurrentHoleRuntime> runtimes_, previousRuntimes_;
    QMap<QString, quint64> previousTransitionRevisions_;
    QMap<QString, quint64> acceptedObservationRevisions_; // 每孔最后消费的N输入revision，拒绝重复重用前版。
    QMap<QString, QString> diagnostics_;
    quint64 revision_ = 0;
    bool observed_ = false, snapshotValid_ = false, previousValid_ = false;
    QString acceptedTaskReceipt_;
    QByteArray acceptedTaskSignature_;
    OriginalTaskReagents::RuntimeAdoption acceptedTaskAdoption_;
    mutable OriginalTaskReagents::RuntimeBasis frozenTaskBasis_;
    mutable QByteArray frozenTaskSignature_;
    QByteArray cancelledTaskSignature_;
    QString cancelledTaskToken_;
};
}
#endif
