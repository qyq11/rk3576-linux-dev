#ifndef SN100_LIS_CONNECTION_CONTROLLER_H
#define SN100_LIS_CONNECTION_CONTROLLER_H

#include "data/lis_settings_store.h"
#include "lis_connection_values.h"
#include <QMap>
#include <QSet>

namespace Sn100Lis
{
class LisTcpTransport;

// 与异步socket同属GUI线程；设置仍由原Store线程读取，不增加SQL或线程。
// 完整正文只交给业务入口。本片不生成ACK，不保存任务或报告上传状态。
class LisConnectionController : public QObject
{
    Q_OBJECT
public:
    explicit LisConnectionController(LisSettingsStore *store, bool reconnect,
                                     int reconnectMilliseconds, QObject *parent = nullptr);
    ~LisConnectionController() override;
    ConnectionState state() const { return state_; }
    bool isStopped() const { return stoppedReported_; }
    bool openConfirmed(quint64 expectedRevision, const QString &expectedToken, QString *error);
    bool sendMessage(quint64 expectedRevision, quint64 expectedGeneration,
                     const QString &body, quint64 *request, QString *error);
    void stop(const QString &reason);
    void shutdown();

signals:
    void stateChanged(Sn100Lis::ConnectionState state);
    void messageReceived(Sn100Lis::ReceivedMessage message);
    void sendFinished(Sn100Lis::SendOutcome result);
    void wireBytes(Sn100Lis::ConfigurationIdentity configuration, QString direction,
                   quint64 generation, quint64 peer, QByteArray bytes);
    void messageDiscarded(Sn100Lis::ConfigurationIdentity configuration,
                          quint64 generation, quint64 peer, int count, QString reason);
    void businessTimeoutTick(Sn100Lis::ConfigurationIdentity configuration,
                             quint64 generation, qint64 elapsedMilliseconds);
    void error(Sn100Lis::ConfigurationIdentity configuration, QString operation,
               quint64 generation, quint64 peer, QString detail);
    void stopped();

private:
    struct PendingSend
    {
        ConfigurationIdentity configuration;
        quint64 generation = 0;
        QSet<quint64> peers;
    };
    LisTcpTransport *transport_ = nullptr;
    QMap<quint64, PendingSend> pending_;
    ConfigurationIdentity configuration_;
    ConnectionState state_;
    QString effectiveKey_, latestReadFingerprint_;
    bool reconnect_ = false, initialized_ = false, applying_ = false;
    bool closing_ = false, stoppedReported_ = false, trusted_ = false, openingRequested_ = false;
    bool stopping_ = false, opening_ = false;
    int reconnectMilliseconds_ = 3000;
    int sendNotificationDepth_ = 0;
    quint64 operation_ = 0, nextRevision_ = 0, nextRequest_ = 0;
    bool checkThread(QString *error) const;
    static QString effectiveKey(const LisSettings &settings);
    void receiveCommitted(const LisSettings &settings, bool saved);
    void settingsError(const QString &operation, const QString &detail);
    void reportStoppedIfDrained();
    void publish(const QString &state, quint64 generation, int peers, const QString &detail = QString());
    void receiveSendResult(quint64 request, quint64 generation, quint64 peer,
                           const QString &outcome, const QString &detail);
};
}
#endif
