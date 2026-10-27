#ifndef SN100_PROJECT_PAGE_H
#define SN100_PROJECT_PAGE_H
#include "projects_store.h"
#include <QWidget>
#include <QPointer>

namespace Ui
{
class frmSystemMaterial;
}
class ProjectFormatDialog;
class HistoryCurrentDisplayState;
class QDialog;
class ProjectPage : public QWidget
{
    Q_OBJECT
  public:
    explicit ProjectPage(ProjectsStore *store, QWidget *parent = nullptr);
    ~ProjectPage() override;
    bool isReady() const
    {
        return m_ready;
    }
    bool isBusy() const
    {
        return m_busy;
    }
    bool hasUnsavedChanges() const;
    bool bindCurrentDisplayState(HistoryCurrentDisplayState *state);
    bool prepareToLeave();
    ProjectRecords projects() const
    {
        return m_projects;
    }
    ProjectUnits units() const
    {
        return m_units;
    }
  public slots:
    void setSession(AccountSession session);
    void reload();
  signals:
    void snapshotChanged(ProjectRecords projects, ProjectUnits units);
    void statusChanged(QString message);
    void busyChanged(bool busy);
    void operationFinished(QString operation, bool success);
    void currentFormatInvalidated(QString reason);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void applyStyle();
    void updateActions();
    void showProject(qint64 id);
    void openResultFormat();
    void openOriginalResultFormat();
    void openOriginalParameters();
    void openProjectMenu(const QPoint &position);
    void openOriginalDirectory();
    void importOriginalProject();
    void exportOriginalProjects(bool allProjects);
    void createProject();
    void importProjects();
    void manageUnits();
    void cancel();
    bool canStartOperation(bool requireProject = false) const;
    qint64 beginOperation(const QString &operation);
    void requestLoad();
    void updateResultSelection(const QString &type, bool used);
    ProjectRecord editedProject() const;
    void markDirty();
    void save();
    void copy();
    void remove();
    void report(const QString &message);
    void setBusy(bool busy);
    Ui::frmSystemMaterial *ui;
    QPointer<ProjectsStore> m_store;
    ProjectAdvancedEditor *m_advanced;
    ProjectRecords m_projects;
    ProjectRecord m_draftProject;
    ProjectUnits m_units;
    AccountSession m_session;
    qint64 m_selectedId = 0;
    qint64 m_request = 0;
    qint64 m_selectAfterLoad = 0;
    qint64 m_beforeCreateId = 0;
    QPointer<ProjectFormatDialog> m_formatDialog;
    QPointer<QDialog> m_auxiliaryDialog;
    QString m_pendingOperation;
    QString m_catalogDatabasePath;
    QString m_advancedSourcePath; // 高级控件只在同一资料源、同一项目且原值完全相同时复用。
    bool m_formatRefreshAfterClose = false;
    bool m_ready = false;
    bool m_busy = false;
    bool m_dirty = false;
    bool m_populating = false;
    bool m_creating = false;
    bool m_refreshRequired = false;
    bool m_promptOpen = false;
    bool m_originalProfile = false;
    bool m_advancedEdited = false;
    QString m_originalRoot;
};
#endif
