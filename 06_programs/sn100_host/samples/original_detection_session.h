#ifndef SN100_ORIGINAL_DETECTION_SESSION_H
#define SN100_ORIGINAL_DETECTION_SESSION_H
#include "original_sample_repository.h"
#include "reagents/original_reagent_repository.h"
#include "original_task_reagent_receipt.h"
#include <QStringList>

// 与SampleStore同一个工作线程，准备任务和占用原报告编号后才返回待发送内容。
class OriginalDetectionSession
{
public:
    bool initialize(OriginalSampleRepository *samples, QString *message);
    bool prepare(bool linuxNew, QString *message);
    void cancelUnsent();
    bool answerRackRequest(const QByteArray &request, QByteArray *response, QString *message,
        const OriginalTaskReagents::RuntimeBasis &basis = {}, OriginalTaskReagents::Commit *receipt = nullptr,
        OriginalTaskReagents::CommitHandler committed = {});
    bool receiveDevicePayload(const QByteArray &payload, QString *message,
        const OriginalTaskReagents::RuntimeBasis &basis = {}, OriginalTaskReagents::Commit *receipt = nullptr,
        OriginalTaskReagents::CommitHandler committed = {});
    bool reagentConfiguration(QByteArray *payload, QString *message);
    bool rackConfiguration(QByteArray *payload, QString *message);
    void markConnectionUncertain();
    bool prepared() const { return prepared_; }
    bool hasIssuedTasks() const { return !issuedRacks_.isEmpty(); }
    bool hasUnfinishedIssuedTasks() const
    {
        // TD9只是进度；完整Tr保存或TR提交才释放旧结果归属。
        for (const auto &task : issuedTasks_)
            if (!finishedTasks_.contains(task.taskId)) return true;
        return false;
    }
    QList<OriginalTasks::Task> issuedTasks() const { return issuedTasks_; }
private:
    OriginalSampleRepository *samples_ = nullptr; // 所有者SampleStore保证会话先于仓库释放。
    OriginalReagentRepository reagents_;
    SampleSnapshot snapshot_;
    ProjectRecords projects_;
    ProjectUnits units_;
    QString root_;
    bool prepared_ = false, linuxNew_ = false;
    bool scannerClosed_ = false, rackScannerClosed_ = false, allowMissingBarcode_ = true;
    bool sendByReportRow_ = true, horizontalReport_ = true;
    bool deleteSentTasks_ = false;
    QStringList multiBottleExceptions_; // 原config.ini/Reagent/exMultil，沿原QVariant.toStringList读取。
    QSet<qint64> issuedRacks_;
    QList<OriginalTasks::Task> issuedTasks_;
    QSet<qint64> finishedTasks_; // 仅Tr完整保存或TR已提交；TD阶段9不在此释放占用。
    QMap<qint64, QString> storedResultPayloadHashes_; // 原文件、数据库和旧预设均成功后才登记。
    QSet<qint64> addedReagentTasks_, uncertainTasks_;
    QMap<qint64, int> taskProgress_, taskErrors_;
    QMap<qint64, OriginalBusiness::Rows> reservedReagentRows_;
    bool receiveTaskErrors(const QByteArray &payload, QString *message,
        const OriginalTaskReagents::RuntimeBasis &basis, OriginalTaskReagents::Commit *receipt,
        OriginalTaskReagents::CommitHandler committed);
    void releaseFinishedRack(const QString &rack);
    bool tasksForRack(const SampleRack &rack, const OriginalTasks::RackRequest *request,
                      const OriginalReagentState &reagents, QList<OriginalTasks::Task> *tasks,
                      OriginalReagentAllocation::AmountWork *work, QString *message) const;
    bool reserve(const OriginalReagentState &observed, QList<OriginalTasks::Task> *tasks,
                 const OriginalReagentAllocation::AmountWork &work, QString *message, const QByteArray &request,
                 const OriginalTaskReagents::RuntimeBasis &basis, OriginalTaskReagents::Commit *receipt,
                 OriginalTaskReagents::CommitHandler committed);
};
#endif
