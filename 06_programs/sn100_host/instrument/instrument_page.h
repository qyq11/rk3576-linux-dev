#ifndef SN100_INSTRUMENT_PAGE_H
#define SN100_INSTRUMENT_PAGE_H

#include <QByteArray>
#include <QDate>
#include <QPointer>
#include <QWidget>
#include "data/account_store.h"
#include "maintenance_store.h"
#include "original_status_values.h"
#include "original_files/original_system_profile.h"
#include "protocol/status_replay.h"
#include <QElapsedTimer>
namespace OriginalRuntime { struct RuntimeSettings; }

class QPushButton;
class QTableWidget;
class DeviceControl;
namespace Ui { class frmDriverSet; }

// 原仪器操作画布：维护记录、连接配置和值展示；设备动作仍按实际发送链接入。
class InstrumentPage : public QWidget
{
    Q_OBJECT
public:
    explicit InstrumentPage(MaintenanceStore *store, QWidget *parent = nullptr);
    ~InstrumentPage() override;
    bool isBusy() const { return busy_ || connectionBusy_; }
    bool isReady() const { return ready_; }
    bool prepareToLeave();
    // 只读取并严格转换通讯测试输入，不发送、不检查命令权限或设备状态。
    // 文档段换行映为LF；不解析文字形式的\xHH，失败时清空输出并说明位置。
    bool prepareCommunicationAsciiPayload(QByteArray *payload, QString *message) const;
    void setManualCommunicationConsumerReady(bool ready);
    QString manualCommunicationRequestBlocker(quint64 request, quint64 session, quint64 connection,
                                               const QByteArray &payload) const;
    void receiveManualCommunicationResult(quint64 request, quint64 session, quint64 connection,
                                           bool terminal, const QString &stage, const QString &message);
    void setSession(AccountSession session);
    bool setConnectionConfigurationFile(const QString &absoluteFile);
    void attachDeviceControl(DeviceControl *control, bool connectAtStartup);
    void setReplayState(const Sn100Protocol::ReplayState &state);
    void setReadOnlyStatus(const Sn100Protocol::StatusSnapshot &snapshot, bool fresh,
                           const QString &prefix, const QString &sourceDetail);
    bool applyOriginalRuntime(const OriginalRuntime::RuntimeSettings &settings, QString *message);
    // 通道行/开关来自原库只读快照；本接口不写通道配置、不发设备命令。
    bool configureOriginalAdChannels(const OriginalRuntime::Channels &channels, int testChannels, QString *message);
    // 仅在原signalReceiveDriverModel对应条件成立时调用，保留450nm重载表头。
    bool reloadOriginalAdChannels(int testChannels, QString *message);
    bool receiveOriginalAd(const QByteArray &payload, quint64 connectionGeneration, QString *message);
    bool applyCommittedOriginalChannels(const OriginalRuntime::Channels &channels, quint64 generation, QString *message);

signals:
    void statusChanged(QString message);
    void busyChanged(bool busy);
    void operationFinished(QString operation, bool success);
    // 值副本是同一点击的正文与身份；不是设备到包号或任务提交凭据。
    void manualCommunicationRequested(quint64 request, quint64 session, quint64 connection,
                                      AccountSession identity, QByteArray payload);
    void manualCommunicationRevoked(quint64 request, quint64 session, quint64 connection, QString reason);

private:
    Ui::frmDriverSet *ui;
    QPointer<MaintenanceStore> store_;
    AccountSession session_;
    bool ready_ = false;
    bool busy_ = false;
    QDate requestedMonth_;
    QString replayModel_;
    QList<QPushButton *> manualButtons_;
    QString connectionFile_, connectionFingerprint_, committedAddress_, committedPort_;
    quint64 connectionRequest_ = 0;
    bool connectionBusy_ = false;
    bool connectionDirty_ = false;
    QPointer<DeviceControl> deviceControl_;
    bool autoConnectDevice_ = false;
    QStringList blockingAlarmCodes_;
    bool alarmPolicyAvailable_ = false;
    bool originalCommunicationConfigured_ = false;
    bool manualCommunicationConsumerReady_ = false;
    quint64 manualCommunicationSession_ = 0;
    quint64 manualCommunicationPending_ = 0, manualCommunicationConnection_ = 0;
    QByteArray manualCommunicationPayload_;
    bool showCommunication_ = true, showOnlyCommunication_ = false, maskCommunication_ = false;
    QStringList shownCommunication_, maskedCommunication_;
    quint64 sentCommunication_ = 0, receivedCommunication_ = 0, communicationGeneration_ = 0;
    QElapsedTimer communicationClock_;
    Sn100Protocol::FrameDecoder sentDecoder_{Sn100Protocol::Direction::HostToDevice};
    Sn100Protocol::StatusSnapshot displayedSnapshot_;
    OriginalInstrument::StatusDisplay displayedValues_;
    bool displayedStatusFresh_ = false;
    QString displayedPrefix_, displayedDetail_;
    int configuredReagentPositions_ = 20;
    OriginalRuntime::Channels originalAdChannels_;
    int originalAdChannelCount_ = 0;
    bool originalAdConfigured_ = false, originalAdHeadersReloaded_ = false;
    quint64 originalAdGeneration_ = 0;
    QString originalAdCheckedColour_ = QStringLiteral("42,118,242,255");

    void applyOriginalStyle();
    void disableDeviceActions();
    void clearDeviceValues(bool initializeCommunication = false);
    void updatePermissions();
    void setBusy(bool busy);
    void loadSelectedMonth();
    void initializeMaintenanceTable(QTableWidget *table, const QStringList &names,
                                    const QStringList &actions, const QStringList &texts);
    void showMaintenanceRecords(const MaintenanceRecords &records);
    void recordManualMaintenance(QString action, QString text);
    void refreshSensors();
    void report(QString message);
    void loadConnectionConfiguration();
    void saveConnectionConfiguration();
    void setConnectionBusy(bool busy);
    void restoreConnectionDraft();
    void updateDeviceButtons();
    void appendOriginalCommunication(bool transmitted, const QByteArray &payload);
    void refreshCommunicationAsciiInput();
    void requestManualCommunication();
    void cancelManualCommunication(const QString &reason);
    void showManualCommunicationMessage(const QString &stage, const QString &message);
    void refreshOriginalAdChannels();
    void clearOriginalAdValues(const QString &reason);
};

#endif
