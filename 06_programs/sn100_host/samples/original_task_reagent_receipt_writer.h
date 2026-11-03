#ifndef SN100_ORIGINAL_TASK_REAGENT_RECEIPT_WRITER_H
#define SN100_ORIGINAL_TASK_REAGENT_RECEIPT_WRITER_H
#include "original_task_reagent_receipt.h"
#include "original_task_codec.h"
#include "reagents/original_reagent_repository.h"
#include <QSqlDatabase>

namespace OriginalTaskReagents
{
TaskOrigin taskOrigin(const OriginalTasks::Task &task);

// 仅在原Session工作线程使用。声明在Database之前，返回时Database先关闭，
// 本对象再核after/交纯值回调；更外层两把QLock在它之后才释放。
class ReceiptWriter
{
public:
    ReceiptWriter(Action action, const QString &root, const QByteArray &payload, const RuntimeBasis &basis,
                  Commit *result, CommitHandler committed);
    ~ReceiptWriter();
    ReceiptWriter(const ReceiptWriter &)=delete;
    ReceiptWriter &operator=(const ReceiptWriter &)=delete;
    Commit &data() { return data_; }
    void transactionStarted();
    void captureBefore(QSqlDatabase database, const OriginalReagentState &observed);
    void matchedUsedWrite(const OriginalBusiness::Row &before, const QString &usedText);
    void captureTransactionAfter(QSqlDatabase database);
    void commitFailed();
    void commitSeen();
    void finish(); // writer已关闭后显式调用；析构也兜底，不重复回调。
private:
    Commit data_;
    Commit *result_=nullptr;
    CommitHandler committed_;
    bool enabled_=false, finished_=false, beforeVerified_=false, transactionAfterVerified_=false;
    TypedPlate transactionAfter_;
    void verifyAfter();
    void appendProblem(const QString &message);
};
}
#endif
