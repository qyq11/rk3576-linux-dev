#ifndef SN100_ORIGINAL_SAMPLE_REPOSITORY_H
#define SN100_ORIGINAL_SAMPLE_REPOSITORY_H
#include "projects/original_project_repository.h"
#include "samples/sample_types.h"
#include "original_task_codec.h"
#include "projects/original_project_save_notice.h"
#include <QSet>
#include <functional>

// 原样本架是本次运行内的任务，原组合存sample_Combination.db；不把原项目写进早期catalog。
class OriginalSampleRepository
{
public:
    // 主程序显式传已验配置；旧直接调用省略参数时保留既有杯默认和失败语义。
    bool initialize(const QString &root, QString *message, bool newHoleContainerIsTube = false);
    bool load(SampleSnapshot *snapshot, QString *message);
    bool saveRack(SampleRack rack, qint64 *savedId, QString *message);
    bool removeRack(qint64 rackId, QString *message);
    bool clearOrdinary(QString *message);
    bool saveCombination(SampleCombination combination, qint64 *savedId, QString *message);
    bool removeCombination(qint64 id, QString *message);
    void setRackInFlight(qint64 rackId, bool inFlight);
    bool updateDeviceTask(const OriginalTasks::Task &task, SampleTaskState state, QString *message);
    void updateRackTimes(const QMap<QString, int> &seconds);
    bool qualityChoices(SampleQcPreset::Snapshot *choices, QString *message) const;
    bool validateControlHole(const SampleHole &hole, int holeCount, QString *message) const;
    bool saveCalibrationParameters(qint64 projectId, QString expectedVersion, QString lastValues, QString *message,
                                   OriginalProjectSaveNotice *result = nullptr,
                                   const std::function<void(const OriginalProjectSaveNotice &)> &committed = {},
                                   const QString &expectedFileName = QString());
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
