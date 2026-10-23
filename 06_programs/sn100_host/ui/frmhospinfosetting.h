#ifndef SN100_FRMHOSPINFOSETTING_H
#define SN100_FRMHOSPINFOSETTING_H

#include <QWidget>
#include "data/hospital_store.h"

class QAbstractButton;
class QButtonGroup;
class QCloseEvent;
class QEvent;

namespace Ui
{
class frmHospInfoSetting;
}

// Migrated from the reference frmuserset/frmhospinfosetting page.
// Only the hospital dictionary slice is present; the store runs in its own thread.
class frmHospInfoSetting : public QWidget
{
    Q_OBJECT

public:
    explicit frmHospInfoSetting(HospitalStore *store, QWidget *parent = nullptr);
    ~frmHospInfoSetting() override;

    bool isReady() const { return ready_; }
    bool isBusy() const { return busy_; }
    bool hasUnsavedChanges() const { return dirty_; }
    void setDataDirectory(const QString &directory);
    // The shell embeds the original 1441x871 page instead of its experiment host.
    void setEmbeddedInShell();
    QString statusText() const;
    // Refuse an in-flight operation; confirmed discard restores the committed
    // snapshot without writing or starting another asynchronous request.
    bool requestLeave(const QString &action);

signals:
    void requestLoad(QString type);
    void requestSave(QString type, QString hospitalName, HospitalRows rows);
    void requestRemove(QString type, qint64 id);
    void operationFinished(QString operation, bool success);
    void statusChanged(QString message, bool error);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void on_btnAdd_clicked();
    void on_btnDel_clicked();
    void on_btnSave_clicked();
    void on_btnCancel_clicked();
    void on_btnReload_clicked();

private:
    Ui::frmHospInfoSetting *ui;
    QButtonGroup *categoryButtons_;
    QString type_ = QStringLiteral("keshi");
    QString displayedType_ = QStringLiteral("keshi");
    QString displayedHospitalName_;
    HospitalRows displayedRows_;
    QString pendingOperation_ = QStringLiteral("initialize");
    QString reloadMessage_;
    QString operationWarning_;
    bool ready_ = false;
    bool busy_ = true;
    bool dirty_ = false;
    bool rendering_ = false;

    void setBusy(bool busy);
    void setDirty();
    void showStatus(const QString &message, bool error = false);
    bool confirmDiscard(const QString &action);
    void selectCategory(QAbstractButton *button);
    void checkCurrentCategory();
    void beginLoad(const QString &operation, const QString &message);
    void renderRows(const QString &hospitalName, const HospitalRows &rows);
    void addLineData(const HospitalRow &data, bool isNew);
    HospitalRows collectRows(QString *error) const;
    bool isDoctorType() const;
};

#endif
