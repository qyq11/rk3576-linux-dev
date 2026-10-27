#ifndef SN100_FRMLISSETTING_H
#define SN100_FRMLISSETTING_H

#include <QWidget>
#include "data/lis_settings_store.h"
class QCloseEvent;
namespace Ui { class frmLisSetting; }

class frmLisSetting : public QWidget
{
    Q_OBJECT
public:
    explicit frmLisSetting(LisSettingsStore *store, QWidget *parent = nullptr);
    ~frmLisSetting() override;
    bool isReady() const { return ready_; }
    bool isBusy() const { return busy_; }
    bool isDirty() const { return ready_ && draft_ != committed_; }
    LisSettings committedSettings() const { return committed_; }
    LisSettings draftSettings() const { return draft_; }
    QString statusText() const { return status_; }
    bool requestLeave(const QString &action);

signals:
    void requestSave(LisSettings settings);
    void operationFinished(QString operation, bool success);
    void busyChanged(bool busy);
    void statusChanged(QString message, bool error);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_btnSave_clicked();

private:
    Ui::frmLisSetting *ui;
    LisSettings committed_;
    LisSettings draft_;
    QStringList available_;
    QString status_;
    bool ready_ = false;
    bool busy_ = true;
    bool rendering_ = false;
    void setBusy(bool busy);
    void reportStatus(const QString &message, bool error = false);
    void render(LisSettings settings, QStringList ports);
    void showMode();
    void editDraft();
};

#endif
