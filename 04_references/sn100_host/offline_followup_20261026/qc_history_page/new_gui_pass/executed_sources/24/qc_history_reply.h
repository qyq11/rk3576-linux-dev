#ifndef SN100_QC_HISTORY_REPLY_H
#define SN100_QC_HISTORY_REPLY_H

// 质控只读历史结果、追溯及描述性统计的线程间值快照。
#include "qc_history_core.h"
#include <QMetaType>

struct QcHistoryBatch
{
    qint64 id = 0;
    QString name;
};

struct QcHistoryResult
{
    qint64 ruleId = 0;
    Sn100QcHistory::ResultType type = Sn100QcHistory::ResultType::Original;
    QString name;
    bool used = false;
    // 只是关联规则配置；历史单位未知时不叠加到原始数值图，不作临床阈值。
    QString target, lowerLimit, upperLimit;
};
using QcHistoryResults = QList<QcHistoryResult>;

enum class QcHistoryStatus
{
    Ready,
    Empty,
    NoCurrentBatch,
    NoEnabledResults
};

class QcStore;

// 一张原结果类型图卡的只读值快照；SQL句柄、QObject及可变页面指针不会跨线程。
class QcHistorySeries
{
public:
    QcHistorySeries() = default;
    QcHistoryResult result() const { return result_; }
    Sn100QcHistory::Selection selection() const { return selection_; }
    Sn100QcHistory::Summary summary() const { return summary_; }
    int validRawCount() const { return validRawCount_; }
private:
    friend class QcStore;
    QcHistoryResult result_;
    Sn100QcHistory::Selection selection_;
    Sn100QcHistory::Summary summary_;
    int validRawCount_ = 0;
};
using QcHistorySeriesList = QList<QcHistorySeries>;

// 所有getter返回值或值列表；调用者修改副本不能改worker已经提交的快照。
class QcHistoryReply
{
public:
    QcHistoryReply() = default;
    QString datasetId() const { return datasetId_; }
    qint64 associationId() const { return associationId_; }
    qint64 controlId() const { return controlId_; }
    qint64 projectId() const { return projectId_; }
    QString controlName() const { return controlName_; }
    QString projectName() const { return projectName_; }
    QcHistoryBatch currentBatch() const { return currentBatch_; }
    QcHistoryBatch previousBatch() const { return previousBatch_; }
    QcHistoryStatus status() const { return status_; }
    QcHistoryResults results() const { return results_; }
    QList<Sn100QcHistory::HistoryRow> rows() const { return rows_; }
    QList<Sn100QcHistory::UnitEvidence> unitEvidence() const { return unitEvidence_; }
    QcHistorySeriesList series() const { return series_; }
    int sourceRowCount() const { return rows_.size(); }
    // 唯一原始行层面的日期/时间等排除；每类型无效结果另见selection.invalidResultCount。
    int excludedRowCount() const { return excludedRowCount_; }
    bool complete() const { return complete_; } // 超预算/失败不返回成功或截断数据。
private:
    friend class QcStore;
    QString datasetId_, controlName_, projectName_;
    qint64 associationId_ = 0, controlId_ = 0, projectId_ = 0;
    QcHistoryBatch currentBatch_, previousBatch_;
    QcHistoryStatus status_ = QcHistoryStatus::Empty;
    QcHistoryResults results_;
    QList<Sn100QcHistory::HistoryRow> rows_;
    QList<Sn100QcHistory::UnitEvidence> unitEvidence_;
    QcHistorySeriesList series_;
    int excludedRowCount_ = 0;
    bool complete_ = false;
};
Q_DECLARE_METATYPE(QcHistoryBatch)
Q_DECLARE_METATYPE(QcHistoryResult)
Q_DECLARE_METATYPE(QcHistoryResults)
Q_DECLARE_METATYPE(QcHistoryStatus)
Q_DECLARE_METATYPE(QcHistorySeries)
Q_DECLARE_METATYPE(QcHistorySeriesList)
Q_DECLARE_METATYPE(QcHistoryReply)

#endif
