#ifndef SN100_INSTRUMENT_PAGE_H
#define SN100_INSTRUMENT_PAGE_H

#include <QDate>
#include <QPointer>
#include <QWidget>
#include "data/account_store.h"
#include "maintenance_store.h"
#include "protocol/status_replay.h"

class QPushButton;
class QTableWidget;
namespace Ui { class frmDriverSet; }

// 原仪器操作画布。只接人工维护记录和值展示，没有设备发送接口。
class InstrumentPage : public QWidget
{
    Q_OBJECT
public:
    explicit InstrumentPage(MaintenanceStore *store, QWidget *parent = nullptr);
    ~InstrumentPage() override;
    bool isBusy() const { return busy_; }
    bool isReady() const { return ready_; }
    bool prepareToLeave();
    void setSession(AccountSession session);
    void setReplayState(const Sn100Protocol::ReplayState &state);
    void setReadOnlyStatus(const Sn100Protocol::StatusSnapshot &snapshot, bool fresh,
                           const QString &prefix, const QString &sourceDetail);

signals:
    void statusChanged(QString message);
    void busyChanged(bool busy);
    void operationFinished(QString operation, bool success);

private:
    Ui::frmDriverSet *ui;
    QPointer<MaintenanceStore> store_;
    AccountSession session_;
    bool ready_ = false;
    bool busy_ = false;
    QDate requestedMonth_;
    QString replayModel_;
    QList<QPushButton *> manualButtons_;

    void applyOriginalStyle();
    void disableDeviceActions();
    void clearDeviceValues();
    void updatePermissions();
    void setBusy(bool busy);
    void loadSelectedMonth();
    void initializeMaintenanceTable(QTableWidget *table, const QStringList &names,
                                    const QStringList &actions, const QStringList &texts);
    void showMaintenanceRecords(const MaintenanceRecords &records);
    void recordManualMaintenance(QString action, QString text);
    void refreshSensors();
    void report(QString message);
};

#endif
