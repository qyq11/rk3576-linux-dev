#ifndef SN100_ORIGINAL_DETECTION_SESSION_H
#define SN100_ORIGINAL_DETECTION_SESSION_H
#include "original_sample_repository.h"
#include "reagents/original_reagent_repository.h"

// 与SampleStore同一个工作线程，准备任务和占用原报告编号后才返回待发送内容。
class OriginalDetectionSession
{
public:
    bool initialize(OriginalSampleRepository *samples, QString *message);
    bool prepare(bool linuxNew, QString *message);
    void cancelUnsent();
    bool answerRackRequest(const QByteArray &request, QByteArray *response, QString *message);
    bool receiveDevicePayload(const QByteArray &payload, QString *message);
    bool prepared() const { return prepared_; }
    bool hasIssuedTasks() const { return !issuedRacks_.isEmpty(); }
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
    QSet<qint64> issuedRacks_;
    QList<OriginalTasks::Task> issuedTasks_;
    QSet<qint64> finishedTasks_;
    bool tasksForRack(const SampleRack &rack, const OriginalTasks::RackRequest *request,
                      const OriginalReagentState &reagents, QList<OriginalTasks::Task> *tasks,
                      QMap<qint64, double> *remaining, QString *message) const;
    bool reserve(const OriginalReagentState &observed, QList<OriginalTasks::Task> *tasks,
                 const QMap<qint64, double> &remaining, QString *message);
};
#endif
