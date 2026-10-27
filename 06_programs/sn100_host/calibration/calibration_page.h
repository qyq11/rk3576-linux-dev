#ifndef SN100_CALIBRATION_PAGE_H
#define SN100_CALIBRATION_PAGE_H
#include "calibration_store.h"
#include "projects/projects_store.h"
#include "reagents/reagent_types.h"
#include <QWidget>
namespace Ui { class frmCalibrationMaterial; }

class CalibrationPage : public QWidget
{
    Q_OBJECT
public:
    explicit CalibrationPage(CalibrationStore *store, QWidget *parent = nullptr);
    ~CalibrationPage() override;
    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_busy; }
    bool hasUnsavedChanges() const { return m_dirty; }
    bool prepareToLeave();
    CalibrationRecord currentRecord() const { return m_record; }
    CalibrationRecords records() const { return m_records; }
public slots:
    void setSession(AccountSession session);
    void setProjects(ProjectRecords projects, ProjectUnits units);
    void setReagents(ReagentSnapshot reagents);
    void reload();
    void startNewRecord();
    void saveCurrent();
    void copyCurrent();
    void deleteCurrent();
    void cancelDraft();
signals:
    void snapshotChanged(CalibrationRecords records);
    void statusChanged(QString message);
    void busyChanged(bool busy);
    void operationFinished(QString operation, bool success);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void applyStyle();
    void updateActions();
    void setBusy(bool busy);
    void report(const QString &message);
    void populateProjects();
    void showProject(qint64 projectId);
    void showRecord(qint64 id);
    void renderRecord();
    void renderBatchReferences();
    bool readEditedRecord(CalibrationRecord *record, QString *message) const;
    void markDirty();
    void updateRawPlot();
    void showRawChart();
    Ui::frmCalibrationMaterial *ui;
    CalibrationStore *m_store;
    ProjectRecords m_projects;
    ProjectUnits m_units;
    ReagentSnapshot m_reagents;
    CalibrationRecords m_records;
    CalibrationRecord m_record;
    AccountSession m_session;
    qint64 m_projectId = 0;
    qint64 m_request = 0;
    qint64 m_selectAfterLoad = 0;
    bool m_ready = false;
    bool m_busy = false;
    bool m_dirty = false;
    bool m_populating = false;
};
#endif
