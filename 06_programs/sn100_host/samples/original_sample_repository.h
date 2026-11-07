#ifndef SN100_ORIGINAL_SAMPLE_REPOSITORY_H
#define SN100_ORIGINAL_SAMPLE_REPOSITORY_H
#include "projects/original_project_repository.h"
#include "samples/sample_types.h"
#include "original_task_codec.h"
#include "projects/original_project_save_notice.h"
#include <QSet>
#include <functional>
#include "original_files/original_ini.h"
#include "lis_business/original_lis_application_plan.h"
class QThread;

// 只关联本次仓库、架孔和输入版本，不是设备任务许可或网络消息来源证明。
struct OriginalLisSampleTarget
{
    QString root, repositoryInstance, rack;
    quint64 initialization = 0, rackRevision = 0, holeRevision = 0, projectRevision = 0;
    qint64 rackId = 0;
    int hole = 0;
    QString taskInputDigest, lisContextDigest, projectSourceDigest;
};

class OriginalSampleRepository;
// 按原项目/表行顺序读取的完整映射值；不暴露可写成员，也不代表应用许可或设备来源。
class OriginalLisMappingSnapshot
{
public:
    bool valid() const { return valid_; }
    QString projectSourceDigest() const { return projectSourceDigest_; }
    QString digest() const { return digest_; }
    const Sn100Lis::OriginalLisMappingProjects &projects() const { return projects_; }
private:
    friend class OriginalSampleRepository;
    bool valid_ = false;
    QString projectSourceDigest_, digest_;
    Sn100Lis::OriginalLisMappingProjects projects_;
};

class OriginalLisPreparedSampleUpdate
{
public:
    bool prepared() const { return prepared_; }
    OriginalLisSampleTarget before() const { return before_; }
    QString mappingDigest() const { return mappingDigest_; }
private:
    friend class OriginalSampleRepository;
    bool prepared_ = false;
    OriginalLisSampleTarget before_;
    SampleHole after_;
    QString mappingDigest_;
};

enum class OriginalLisSampleChangeKind
{
    None, ScanInitialized, QueryStarted, InvalidBarcodeEnded, QueryBound, ReplyReceiving, PatientTasksApplied
};
struct OriginalLisSampleChange
{
    bool memoryApplied = false, queryRequired = false;
    OriginalLisSampleChangeKind kind = OriginalLisSampleChangeKind::None;
    OriginalLisSampleTarget before, after;
};

// 原样本架是本次运行内的任务，原组合存sample_Combination.db；不把原项目写进早期catalog。
class OriginalSampleRepository
{
public:
    OriginalSampleRepository() = default;
    // 仓库身份不能被对象复制或移动克隆；外层unique_ptr移动只转交同一仓库指针。
    OriginalSampleRepository(const OriginalSampleRepository &) = delete;
    OriginalSampleRepository &operator=(const OriginalSampleRepository &) = delete;
    OriginalSampleRepository(OriginalSampleRepository &&) = delete;
    OriginalSampleRepository &operator=(OriginalSampleRepository &&) = delete;
    // 主程序显式传已验配置；旧直接调用省略参数时保留既有杯默认和失败语义。
    bool initialize(const QString &root, QString *message, bool newHoleContainerIsTube = false,
                    OriginalIni::InitializationSourcePtr configurationSource = {});
    OriginalIni::InitializationSourcePtr configurationSource() const { return configurationSource_; }
    bool load(SampleSnapshot *snapshot, QString *message);
    // 孔数须符合本次初始化配置；当前默认容器只用于真正新孔，不刷新项目或数据库。
    bool registerEmptyOrdinaryRack(int ordinaryHoles, bool containerIsTube, qint64 *rackId,
                                   SampleSnapshot *snapshot, QString *message);
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
    // 只读原表及同次INI，不用编辑缓存、不执行未完成复制的恢复；失败输出为无效空快照。
    bool captureOriginalLisMappings(OriginalLisMappingSnapshot *snapshot, QString *message) const;
    // 以下仅在现仓库线程操作同一snapshot；不发送QRY/ACK、不写报告或预留任务。
    bool captureOriginalLisHole(qint64 rackId, int hole, OriginalLisSampleTarget *target,
                                Sn100Lis::OriginalLisHoleValue *oldValue, QString *message) const;
    bool initializeOriginalLisScan(OriginalLisSampleTarget target, QString barcode,
                                   QDateTime startedAt, OriginalLisSampleChange *result, QString *message);
    bool beginOriginalLisQuery(OriginalLisSampleTarget target, QDateTime startedAt,
                               OriginalLisSampleChange *result, QString *message);
    bool bindOriginalLisQuery(OriginalLisSampleTarget target, QString controlId,
                              OriginalLisSampleChange *result, QString *message);
    bool markOriginalLisReceiving(OriginalLisSampleTarget target, QString controlId,
                                 OriginalLisSampleChange *result, QString *message);
    bool prepareOriginalLisApplication(const OriginalLisSampleTarget &target,
        const Sn100Lis::OriginalLisDsrFields &decoded, OriginalLisPreparedSampleUpdate *prepared, QString *message) const;
    bool applyOriginalLisApplication(const OriginalLisPreparedSampleUpdate &prepared,
                                    OriginalLisSampleChange *result, QString *message);
private:
    QString root_;
    OriginalIni::InitializationSourcePtr configurationSource_;
    OriginalProjectRepository projectRepository_;
    ProjectRecords projects_;
    ProjectUnits units_;
    SampleSnapshot snapshot_;
    qint64 nextRackId_ = 2;
    int ordinaryHoles_ = 8, emergencyHoles_ = 3;
    bool initialized_ = false;
    QSet<qint64> inFlight_;
    QString combinationVersion_;
    bool refreshProjects(QString *message);
    bool validateRack(const SampleRack &rack, QString *message) const;
    bool editable(qint64 id, QString *message) const;
    QString lisRepositoryInstance_;
    quint64 lisInitialization_ = 0, lisRevision_ = 0, lisProjectRevision_ = 0;
    bool lisInitialized_ = false, lisRevisionsAvailable_ = true;
    QThread *lisThread_ = nullptr; // 只比较所属线程，不跨线程解引用。
    QMap<qint64, quint64> lisRackRevisions_;
    QMap<qint64, QMap<int, quint64>> lisHoleRevisions_;
    quint64 nextLisRevision();
    void noteLisRackPublished(const SampleRack &rack);
    void noteLisHolePublished(qint64 rackId, int hole);
    void noteLisRackRemoved(qint64 rackId);
    bool originalLisEditable(qint64 rackId, QString *message) const;
    bool originalLisProjectSource(QString *digest, QStringList *files, QString *message) const;
    bool matchOriginalLisTarget(const OriginalLisSampleTarget &target, int *rackIndex, int *holeIndex, QString *message) const;
    bool publishOriginalLisHole(OriginalLisSampleTarget target, SampleHole after,
        OriginalLisSampleChangeKind kind, bool queryRequired, OriginalLisSampleChange *result, QString *message);
};
#endif
