#ifndef SN100_PROJECT_FORMAT_DIALOG_H
#define SN100_PROJECT_FORMAT_DIALOG_H
#include "projects_store.h"
#include <QDialog>
#include <QPointer>
#include <functional>
class QLabel;
class QTableWidget;
class QPushButton;
class QCloseEvent;

// Only this editor owns its result draft. It never edits Page's base/advanced draft.
class ProjectFormatDialog : public QDialog
{
    Q_OBJECT
public:
    ProjectFormatDialog(ProjectsStore *store, qint64 projectId, QString databasePath,
                        AccountSession session, std::function<qint64()> nextRequest, QWidget *parent);
    bool hasDraft() const { return m_dirty; }
    void startRead();
    void setSession(AccountSession session);
    void invalidateContext(const QString &reason, bool sourceAvailable = false);
public slots:
    void reject() override;
    void accept() override;
signals:
    void operationBusyChanged(bool busy);
    void draftChanged();
    void committed(qint64 projectId);
    void configurationInvalidated(QString reason);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void populate(ProjectRecord project, ProjectCurrentFormatSnapshot snapshot);
    void addRow(const ProjectResult *result, const ProjectCurrentFormatEntry *selection);
    void changed();
    void updateActions();
    void saveDraft();
    void reloadRequested();
    bool mayClose();
    bool discardConfirmed(const QString &message);
    void message(const QString &text);
    void failed(const QString &text);
    QPointer<ProjectsStore> m_store;
    qint64 m_projectId = 0, m_readRequest = 0, m_saveRequest = 0;
    QString m_databasePath;
    AccountSession m_session;
    std::function<qint64()> m_nextRequest;
    ProjectRecord m_record;
    ProjectCurrentFormatSnapshot m_snapshot;
    QTableWidget *m_table;
    QLabel *m_message;
    QPushButton *m_save, *m_reload, *m_add, *m_remove;
    bool m_dirty = false, m_populating = false, m_reading = false, m_saving = false;
    bool m_available = true, m_closeApproved = false;
};
#endif
