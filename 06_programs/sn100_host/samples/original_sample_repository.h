#ifndef SN100_ORIGINAL_SAMPLE_REPOSITORY_H
#define SN100_ORIGINAL_SAMPLE_REPOSITORY_H
#include "projects/original_project_repository.h"
#include "samples/sample_types.h"
#include "original_task_codec.h"
#include <QSet>

// 原样本架是本次运行内的任务，原组合存sample_Combination.db；不把原项目写进早期catalog。
class OriginalSampleRepository
{
public:
    bool initialize(const QString &root, QString *message);
    bool load(SampleSnapshot *snapshot, QString *message);
    bool saveRack(SampleRack rack, qint64 *savedId, QString *message);
    bool removeRack(qint64 rackId, QString *message);
    bool clearOrdinary(QString *message);
    bool saveCombination(SampleCombination combination, qint64 *savedId, QString *message);
    bool removeCombination(qint64 id, QString *message);
    void setRackInFlight(qint64 rackId, bool inFlight);
    bool updateDeviceTask(const OriginalTasks::Task &task, SampleTaskState state, QString *message);
    int ordinaryHoles() const { return ordinaryHoles_; }
    int emergencyHoles() const { return emergencyHoles_; }
    QString root() const { return root_; }
    ProjectRecords projects() const { return projects_; }
    ProjectUnits units() const { return units_; }
    SampleSnapshot snapshot() const { return snapshot_; }
private:
    QString root_;
    OriginalProjectRepository projectRepository_;
    ProjectRecords projects_;
    ProjectUnits units_;
    SampleSnapshot snapshot_;
    qint64 nextRackId_ = 2;
    int ordinaryHoles_ = 8, emergencyHoles_ = 3;
    QSet<qint64> inFlight_;
    QString combinationVersion_;
    bool refreshProjects(QString *message);
    bool validateRack(const SampleRack &rack, QString *message) const;
    bool editable(qint64 id, QString *message) const;
};
#endif
