#ifndef SN100_SAMPLE_PAGE_H
#define SN100_SAMPLE_PAGE_H
#include "projects/projects_store.h"
#include "reagents/reagent_types.h"
#include "sample_store.h"
#include <QColor>
#include <QPointer>
#include <QThread>
#include <QWidget>
class QDialog;
class QTableWidget;
namespace Ui
{
class frmSampleMaterial;
class frmSampleSettingMaterial;
} // namespace Ui
class SamplePage : public QWidget
{
    Q_OBJECT
  public:
    explicit SamplePage(SampleStore *store, QWidget *parent = nullptr);
    ~SamplePage() override;
    bool isReady() const
    {
        return ready_;
    }
    bool isBusy() const
    {
        return busy_;
    }
    bool hasUnsavedChanges() const
    {
        return dirty_ || dialogDirty_;
    }
    bool prepareToLeave();
    SampleStore *taskStore() const { return store_; }
    bool originalProfile() const { return originalProfile_; }
    // 只记录以后真正新孔的默认值，不重写快照、草稿或已选容器。
    bool configureOriginalDefaultContainer(bool containerIsTube)
    {
        if (QThread::currentThread() != thread()) return false;
        originalDefaultContainerIsTube_ = containerIsTube;
        return true;
    }
    void configureLayout(int ordinary, int emergency)
    { ordinaryHoles_=ordinary; emergencyHoles_=emergency; }
    SampleSnapshot snapshot() const
    {
        return snapshot_;
    }
  public slots:
    void setProjects(ProjectRecords projects);
    void setReagents(ReagentSnapshot reagents);
    void setSession(AccountSession session);
    void setRackSelectedColour(QColor colour);
    void reload();
    void setQualityChoices(qint64 requestId, SampleQcPreset::Snapshot choices, QString message);
    void invalidateQualityChoices(QString message);
  signals:
    void snapshotChanged(SampleSnapshot snapshot);
    void requestQualityChoices(qint64 requestId);
    void qualityChoicesChanged();
    void busyChanged(bool busy);
    void statusChanged(QString message);
    void operationFinished(QString operation, bool success);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void style();
    void setBusy(bool busy);
    void actions();
    void report(const QString &message);
    void showRack(qint64 id);
    void selectRack(qint64 id, bool openEditor);
    void refreshRackList();
    void reloadRuntimeSnapshot();
    void refreshTable();
    void refreshProjects();
    void refreshProjectButtons();
    void selectHole(int row);
    void addRack();
    void saveRack();
    void removeRack();
    void clearRacks();
    void clearHole();
    void resetTasks(SampleTaskResetAction action);
    bool canEditHole(int row) const;
    void detail();
    void refreshCalibrationButtons();
    void calibration(qint64 projectId);
    void repeat();
    void renameRack();
    void combinations();
    bool closeEditor();
    QString projectName(qint64 id) const;
    const ProjectRecord *project(qint64 id) const;
    QString defaultRate(qint64 id) const;
    void applyCombination(const SampleCombination &combination);
    void markDirty();
    SampleQcPreset::Snapshot qualityChoiceSnapshot() const;
    Ui::frmSampleMaterial *ui;
    Ui::frmSampleSettingMaterial *setting_;
    QWidget *settingWidget_;
    SampleStore *store_;
    ProjectRecords projects_;
    ProjectUnits originalUnits_;
    ReagentSnapshot reagents_;
    AccountSession session_;
    SampleSnapshot snapshot_;
    bool originalProfile_ = false, pendingDeviceReload_ = false;
    bool originalDefaultContainerIsTube_ = false; // 旧独立入口继续用杯，不增加配置拒绝门。
    int ordinaryHoles_ = 8, emergencyHoles_ = 3;
    QColor rackSelectedColour_ = QColor(QStringLiteral("#B7D7FF"));
    SampleRack editing_;
    QPointer<QDialog> dialog_;
    bool initialized_ = false, ready_ = false, busy_ = false, dirty_ = false, dialogDirty_ = false,
         populating_ = false;
    qint64 request_ = 0, selectAfter_ = 0;
    // 新增直接带回后端运行态快照；确认旧草稿期间仍保留请求号，阻止重复新增。
    qint64 pendingAddRequest_ = 0, runtimeSnapshotRequest_ = 0;
    bool openAddedRack_ = false;
    int row_ = 0;
    qint64 qualityChoicesRequest_ = 0;
    qint64 calibrationParametersRequest_ = 0;
    SampleQcPreset::Snapshot qualityChoices_;
    QString qualityChoicesError_, qualityWarning_;
    bool qualityProtectionBound_ = false;
};
#endif
