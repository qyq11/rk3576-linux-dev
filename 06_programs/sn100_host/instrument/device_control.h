#ifndef SN100_DEVICE_CONTROL_H
#define SN100_DEVICE_CONTROL_H

#include "protocol/tcp_transport.h"
#include "protocol/status_session.h"
#include <QSet>
#include <QElapsedTimer>
#include <QTimer>

// 真实设备与RK自建对端共用同一控制代码，来源由调用者明确指定。
struct DeviceControlState
{
    bool connected = false;
    bool fresh = false;
    bool outcomeUnknown = false;
    QString address;
    quint16 port = 0;
    QString pending;
    QString message;
    Sn100Protocol::StatusSnapshot status;
};
Q_DECLARE_METATYPE(DeviceControlState)

class DeviceTransport : public Sn100Protocol::ReadOnlyTcpTransport
{
public:
    explicit DeviceTransport(QObject *parent) : ReadOnlyTcpTransport(parent)
    {
        // 一条原任务报文最多128000字节，另留分片封包开销；预算只在连接启动前设置。
        const bool configured = configureMessageQueue(131072);
        Q_ASSERT(configured);
        Q_UNUSED(configured);
    }
    bool open(const QString &address, quint16 port, QString *error)
    { return connectNumericAddress(address, port, error); }
    bool send(const QByteArray &payload, QString *error)
    { return queueMessage(payload, error); }
};

// 只处理连接、原命令及其真实回执，不把socket写入当成设备执行成功。
class DeviceControl : public QObject
{
    Q_OBJECT
public:
    explicit DeviceControl(QObject *parent = nullptr);
    const DeviceControlState &state() const { return state_; }
    quint64 connectionGeneration() const { return transport_.generation(); }
    bool open(const QString &address, quint16 port);
    void close();
    bool query(Sn100Protocol::ReadOnlyQuery query);
    bool start(const QString &businessBlocker = QString());
    bool pause();
    bool silenceAlarm();
    bool sendTaskReply(const QByteArray &payload);
    bool sendOriginalConfiguration(const QByteArray &payload);
    bool maintenanceCommand(const QByteArray &payload);
    bool updateDeviceTime();
    bool confirmObservedState();
    void setAlarmPolicy(const QStringList &blockingCodes, bool available);
    QString startBlocker() const;
    QString pauseBlocker() const;
    static QByteArray startPayload() { return QByteArrayLiteral("Ts"); }
    static QByteArray pausePayload() { return QByteArrayLiteral("Tp[0]"); }
    static QString originalVersion() { return QStringLiteral("20250905016"); }

signals:
    void changed(DeviceControlState state);
    void error(QString message);
    void wireActivity(QString direction, QByteArray bytes);
    void payloadReceived(QByteArray payload);
    void startAccepted(bool success);

private:
    DeviceTransport transport_;
    DeviceControlState state_;
    QElapsedTimer clock_;
    QTimer timer_;
    qint64 statusAt_ = -1, queryAt_ = -1, pendingAt_ = -1;
    QSet<QString> blockingAlarmCodes_;
    bool alarmPolicyAvailable_ = false;
    QString uncertainCommand_;
    QByteArray lastMaintenance_;
    qint64 lastMaintenanceAt_ = -1;
    bool send(const QByteArray &payload, const QString &pending = QString());
    bool reject(const QString &message);
    void receive(const QByteArray &message);
    void tick();
    void publish(const QString &message = QString());
};

#endif
