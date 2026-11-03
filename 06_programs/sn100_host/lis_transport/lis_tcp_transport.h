#ifndef SN100_LIS_TCP_TRANSPORT_H
#define SN100_LIS_TCP_TRANSPORT_H

#include "data/lis_settings_store.h"
#include "lis_mllp_codec.h"
#include <QElapsedTimer>
#include <QMap>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QTimer>
#include <memory>

class QTcpServer;
class QTcpSocket;

namespace Sn100Lis
{
// 只做TCP和原MLLP字节收发；ACK、任务/结果归属由后续业务接收者处理。
// 创建和configure都不连接；核过实际端点后，调用者才显式openConfirmed。
class LisTcpTransport : public QObject
{
    Q_OBJECT
public:
    explicit LisTcpTransport(QObject *parent = nullptr);
    ~LisTcpTransport() override;
    bool configure(const LisSettings &settings, bool reconnect, int reconnectMilliseconds, QString *error);
    QString configurationToken() const { return configurationToken_; }
    quint64 generation() const { return generation_; }
    QString state() const { return state_; }
    bool isActive() const { return armed_; }
    bool openConfirmed(const QString &expectedConfigurationToken, QString *error);
    bool sendMessage(quint64 messageId, quint64 expectedGeneration, const QString &body, QString *error);
    void stop(const QString &reason = QString());

signals:
    void stateChanged(QString state, quint64 generation, int connectedPeers);
    void error(QString operation, quint64 generation, quint64 peerId, QString detail);
    void wireBytes(QString direction, quint64 generation, quint64 peerId, QByteArray bytes);
    void messageReceived(quint64 generation, quint64 peerId, QByteArray body, QString text);
    // WrittenToSocket仅表示本地socket写完；不是LIS的ACK或结果已上传确认。
    void sendFinished(quint64 messageId, quint64 generation, quint64 peerId, QString outcome, QString detail);
    void messageDiscarded(quint64 generation, quint64 peerId, int count, QString reason);
    void businessTimeoutTick(quint64 generation, qint64 elapsedMilliseconds);

private:
    struct Peer;
    struct PendingSend
    {
        quint64 id = 0, generation = 0;
        QByteArray frame;
        QSet<quint64> targets;
    };
    struct Received
    {
        quint64 generation = 0, peer = 0;
        QByteArray body;
    };
    QTcpServer *server_ = nullptr;
    QMap<quint64,std::shared_ptr<Peer>> peers_;
    QQueue<PendingSend> sends_;
    QQueue<Received> received_;
    QTimer cycle_, reconnectTimer_, connectDeadline_;
    QElapsedTimer clock_;
    QString configurationToken_, host_, mode_, state_ = QStringLiteral("Stopped");
    quint16 port_ = 0;
    bool utf16_ = false, reconnect_ = false, armed_ = false;
    bool stopping_ = false, changing_ = false, destroying_ = false;
    int reconnectMilliseconds_ = 3000, phase_ = 0;
    quint64 generation_ = 0, nextPeer_ = 0, lastMessage_ = 0;
    qint64 queuedBytes_ = 0, receivedBytes_ = 0;
    void publish();
    bool checkThread(QString *error) const;
    void beginClient();
    void retryClient();
    void acceptClients();
    void attachPeer(QTcpSocket *socket, bool client);
    void readPeer(quint64 id);
    void losePeer(quint64 id, const QString &reason);
    void advanceWrite(const std::shared_ptr<Peer> &peer);
    void written(quint64 id, qint64 bytes);
    void cycle();
    void dispatchOne();
    void deliverReceived();
};
}
#endif
