#ifndef SN100_REAGENTS_PAGE_H
#define SN100_REAGENTS_PAGE_H
#include "reagent_store.h"
#include <QPointer>
#include <QWidget>
class QDialog;
namespace Ui
{
class frmReagentMaterial;
}
class ReagentsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ReagentsPage(ReagentStore* store, QWidget* parent = nullptr);
    ~ReagentsPage() override;
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
        return dirty_;
    }
    bool prepareToLeave();
    ReagentSnapshot snapshot() const
    {
        return snapshot_;
    }
    ReagentStore *taskStore() const { return store_; }
public slots:
    void setProjects(ReagentProjects projects);
    void setSession(AccountSession session);
    void openReagentEditor();
    void openHoleEditor();
    void openBatchEditor();
    void openBottleEditor();
    void reloadOriginal();
    void setInstrumentBusy(bool busy);
signals:
    void snapshotChanged(ReagentSnapshot snapshot);
    void readyChanged(bool ready);
    void busyChanged(bool busy);
    void operationMessage(QString operation, QString message);
    void operationFinished(QString operation, bool success);
    void requestLoad();
    void requestSession(AccountSession session);
    void requestSaveReagent(ReagentRecord reagent);
    void requestRemoveReagent(qint64 id);
    void requestSaveBottle(ReagentBottle bottle);
    void requestRemoveBottle(qint64 id);
    void requestSaveBatch(ReagentBatch batch);
    void requestRemoveBatch(qint64 id);
    void requestSaveBarcode(ReagentBarcode barcode);
    void requestRemoveBarcode(qint64 id);
    void requestSaveHole(ReagentHole hole);
    void requestClearHole(QString name);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    Ui::frmReagentMaterial* ui;
    ReagentStore *store_ = nullptr;
    bool originalProfile_ = false;
    bool instrumentBusy_ = false, pendingOriginalReload_ = false;
    QString pendingSavedOperation_;
    ReagentSnapshot snapshot_;
    ReagentProjects projects_;
    AccountSession session_;
    QPointer<QDialog> dialog_;
    bool ready_ = false, busy_ = false, dirty_ = false;
    QString selectedHole_ = QStringLiteral("R_1");
    void selectHole(QString name);
    void setBusy(bool busy);
    void showOffline(QString action);
    bool canEdit() const;
    void applyPermissions();
    bool allowEditor();
    void initializeDialog(QDialog* dialog);
    void markDirty();
    void openBarcodeEditor(qint64 reagentId, qint64 barcodeId = 0);
};
#endif
