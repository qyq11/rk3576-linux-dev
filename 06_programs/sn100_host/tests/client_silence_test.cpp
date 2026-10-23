#include "read_only_client.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <functional>

using namespace Sn100Protocol;

namespace
{

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
    {
        QTest::qWait(20);
    }
    return predicate();
}

class SilenceScenario : public QObject
{
public:
    SilenceScenario(QString name, bool oneValidStatus, QElapsedTimer *clock)
        : label(name), initialStatus(oneValidStatus), wallClock(clock), client(nullptr, 60000, 0)
    {
        server.setProxy(QNetworkProxy::NoProxy);
        traffic.setInterval(1000);
        traffic.setTimerType(Qt::PreciseTimer);
        connect(&traffic, &QTimer::timeout, this, [this]()
        {
            if (closedAt >= 0 || recoveryMode) return;
            // 正确Qq和语义非法Qj轮流到达；两者都不得刷新有效Qj时钟。
            if (++trafficCount % 2) send("Qq[1][Base][11 22 33 44]");
            else send("Qj[4][SIM-INVALID][99][1][0][0]");
        });
        connect(&server, &QTcpServer::newConnection, this, [this]()
        {
            if (peer && peer->state() == QAbstractSocket::ConnectedState)
            {
                errors.append("unexpected extra peer");
                server.nextPendingConnection()->deleteLater();
                return;
            }
            peer = server.nextPendingConnection();
            peer->setProxy(QNetworkProxy::NoProxy);
            peer->setReadBufferSize(4096);
            peerDecoder.reset();
            const QPointer<QTcpSocket> thisPeer = peer;
            connect(peer, &QTcpSocket::readyRead, this, [this, thisPeer]()
            {
                if (peer != thisPeer || !thisPeer) return;
                const QByteArray bytes = thisPeer->read(4096);
                trace("peer.RX.actual", bytes);
                const auto batch = peerDecoder.feed(bytes, wallClock->elapsed());
                errors.append(batch.errors);
                for (const QByteArray &query : batch.messages)
                {
                    trace("peer.RX.command", query);
                    if (query == "Qj")
                    {
                        ++queriesReceived;
                        if (recoveryMode) send("Qj[4][SIM-RECOVERED][2][1][0][0]");
                        else if (initialStatus && !statusScheduled)
                        {
                            statusScheduled = true;
                            QTimer::singleShot(1500, this, [this]()
                            {
                                if (!recoveryMode && closedAt < 0)
                                {
                                    validSentAt = wallClock->elapsed();
                                    send("Qj[4][SIM-ONE-VALID][2][1][0][0]");
                                }
                            });
                        }
                    }
                    else if (query != "QH") errors.append("query outside Qj/QH test whitelist");
                }
            });
            if (!recoveryMode) traffic.start();
        });
        connect(&client, &ReadOnlyClient::connected, this, [this](quint64 generation)
        {
            if (recoveryMode) recoveryGeneration = generation;
            else connectedAt = wallClock->elapsed();
            trace("client.connected", {}, {{"generation", double(generation)}});
            QString error;
            if (!client.request(ReadOnlyQuery::Status, &error)) errors.append(error);
        });
        connect(&client, &ReadOnlyClient::queryQueued, this,
                [this](ReadOnlyQuery query, int attempt, quint64 generation)
        {
            trace("client.TX.planned.not_peer_ack", queryPayload(query),
                  {{"attempt", attempt}, {"generation", double(generation)}});
        });
        connect(&client, &ReadOnlyClient::messageObserved, this,
                [this](ReadOnlyMessage message, quint64 generation)
        {
            trace("client.RX.parsed", message.command,
                  {{"data", message.data}, {"generation", double(generation)}});
            if (message.command == "Qj")
            {
                if (recoveryMode) recovered = true;
                else if (validObservedAt < 0) validObservedAt = wallClock->elapsed();
                else errors.append("unexpected additional valid Qj before silence");
            }
            else if (message.command == "Qq") ++adObserved;
        });
        connect(&client, &ReadOnlyClient::notice, this, [this](QString detail, quint64 generation)
        {
            trace("client.notice", {}, {{"detail", detail}, {"generation", double(generation)}});
            if (detail.startsWith("client.silence:"))
            {
                ++silenceNotices;
                silenceAt = wallClock->elapsed();
                pendingAtSilence = client.requestPending();
                freshAtSilence = client.hasFreshStatus();
                QString error;
                // 直接槽重入request会先tick；silence guard必须阻止递归关断或新入队。
                const bool accepted = client.request(ReadOnlyQuery::Status, &error);
                silenceReentryRejected = !accepted && !error.isEmpty();
                trace("client.silence.direct_slot_request", {}, {{"accepted", accepted}, {"error", error}});
            }
            else if (detail.startsWith("status.")) ++invalidObserved;
        });
        connect(&client, &ReadOnlyClient::requestFinished, this,
                [this](ReadOnlyQuery query, bool success, QString detail, quint64 generation)
        {
            trace("client.request.finished", queryPayload(query),
                  {{"success", success}, {"detail", detail}, {"generation", double(generation)}});
            if (!recoveryMode && query == ReadOnlyQuery::Consumables && !success &&
                    detail.startsWith("client.cancelled:")) ++pendingCancellations;
        });
        connect(&client, &ReadOnlyClient::disconnected, this, [this](quint64 generation)
        {
            trace("client.disconnected", {}, {{"generation", double(generation)},
                  {"pending", client.requestPending()}, {"fresh", client.hasFreshStatus()}});
            if (!recoveryMode && closedAt < 0)
            {
                closedAt = wallClock->elapsed();
                disconnectedGeneration = generation;
                cleanAfterDisconnect = !client.requestPending() && !client.hasFreshStatus()
                        && client.linkState() == "disconnected";
                traffic.stop();
            }
        });
    }

    ~SilenceScenario() override
    {
        traffic.stop();
        client.close();
        if (peer) peer->abort();
        server.close();
    }

    void trace(const QString &event, const QByteArray &bytes = {}, QJsonObject detail = {})
    {
        detail.insert("scenario", label);
        detail.insert("event", event);
        detail.insert("wall_elapsed_ms", double(wallClock->elapsed()));
        detail.insert("evidence", "local-synthetic-peer-real-200s-clock");
        if (!bytes.isEmpty()) detail.insert("hex", QString::fromLatin1(bytes.toHex(' ').toUpper()));
        qInfo().noquote() << QJsonDocument(detail).toJson(QJsonDocument::Compact);
    }

    void send(const QByteArray &payload)
    {
        if (!peer || peer->state() != QAbstractSocket::ConnectedState)
        {
            errors.append("cannot send to owned peer");
            return;
        }
        for (const auto &frame : encodeMessage(payload, Direction::DeviceToHost))
        {
            trace("peer.TX.queued.not_client_ack", frame, {{"payload", QString::fromUtf8(payload)}});
            if (peer->write(frame) != frame.size()) errors.append("peer failed to accept complete frame");
        }
    }

    bool start()
    {
        QString error;
        return server.listen(QHostAddress::LocalHost, 0)
                && client.connectLoopback(server.serverPort(), &error);
    }

    bool reconnect()
    {
        recoveryMode = true;
        traffic.stop();
        QString error;
        const bool accepted = client.connectLoopback(server.serverPort(), &error);
        if (!accepted) errors.append(error);
        return accepted;
    }

    QJsonObject summary() const
    {
        const qint64 reference = initialStatus ? validObservedAt : connectedAt;
        return {{"scenario", label}, {"connected_ms", double(connectedAt)},
            {"last_valid_Qj_sent_ms", double(validSentAt)}, {"last_valid_Qj_observed_ms", double(validObservedAt)},
            {"silence_notice_ms", double(silenceAt)}, {"disconnected_ms", double(closedAt)},
            {"actual_since_reference_ms", double(closedAt - reference)},
            {"actual_since_last_valid_tx_ms", initialStatus ? double(closedAt - validSentAt) : -1.0},
            {"continuous_traffic_frames", trafficCount}, {"valid_AD_observations", adObserved},
            {"invalid_Qj_notices", invalidObserved}, {"silence_notices", silenceNotices},
            {"pending_at_silence", pendingAtSilence}, {"fresh_at_silence", freshAtSilence},
            {"pending_cancelled_on_disconnect", pendingCancellations}, {"clean_after_disconnect", cleanAfterDisconnect},
            {"silence_reentry_rejected", silenceReentryRejected}, {"recovered", recovered},
            {"disconnected_generation", double(disconnectedGeneration)}, {"recovery_generation", double(recoveryGeneration)},
            {"instrument_verified", false}};
    }

    QString label;
    bool initialStatus;
    QElapsedTimer *wallClock;
    ReadOnlyClient client;
    QTcpServer server;
    QPointer<QTcpSocket> peer;
    FrameDecoder peerDecoder{Direction::HostToDevice};
    QTimer traffic;
    QStringList errors;
    qint64 connectedAt = -1, validSentAt = -1, validObservedAt = -1, silenceAt = -1, closedAt = -1;
    quint64 disconnectedGeneration = 0, recoveryGeneration = 0;
    int trafficCount = 0, queriesReceived = 0, adObserved = 0, invalidObserved = 0;
    int silenceNotices = 0, pendingCancellations = 0;
    bool statusScheduled = false, recoveryMode = false, recovered = false;
    bool pendingAtSilence = false, freshAtSilence = false, cleanAfterDisconnect = false;
    bool silenceReentryRejected = false;
};
}

class ClientSilenceTest : public QObject
{
    Q_OBJECT
private slots:
    void realTwoHundredSeconds();
};

void ClientSilenceTest::realTwoHundredSeconds()
{
    QElapsedTimer wall;
    wall.start();
    SilenceScenario a("A-one-valid-Qj-then-other-traffic", true, &wall);
    SilenceScenario b("B-no-valid-Qj-from-connected", false, &wall);
    QVERIFY(a.start());
    QVERIFY(b.start());
    QVERIFY(waitUntil([&]() { return a.connectedAt >= 0 && b.connectedAt >= 0; }, 3000));
    QVERIFY(waitUntil([&]() { return a.validObservedAt >= 0; }, 5000));
    QVERIFY(a.client.hasFreshStatus());
    QVERIFY(!b.client.hasFreshStatus());

    QTimer pending;
    pending.setSingleShot(true);
    connect(&pending, &QTimer::timeout, this, [&]()
    {
        for (SilenceScenario *scenario : {&a, &b})
        {
            QString error;
            const bool accepted = scenario->client.request(ReadOnlyQuery::Consumables, &error);
            scenario->trace("test.pending_near_silence", {}, {{"accepted", accepted}, {"error", error}});
            if (!accepted) scenario->errors.append(error);
        }
    });
    pending.start(qMax(1, 195000 - int(wall.elapsed())));
    QVERIFY2(waitUntil([&]() { return a.closedAt >= 0 && b.closedAt >= 0; }, 210000),
             "Real 200000ms silence did not disconnect within the host scheduling budget");
    qInfo().noquote() << "SILENCE_BEFORE_RECOVERY_A" << QJsonDocument(a.summary()).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "SILENCE_BEFORE_RECOVERY_B" << QJsonDocument(b.summary()).toJson(QJsonDocument::Compact);
    for (SilenceScenario *scenario : {&a, &b})
    {
        const qint64 reference = scenario->initialStatus ? scenario->validObservedAt : scenario->connectedAt;
        const qint64 actual = scenario->closedAt - reference;
        QVERIFY2(actual >= 200000, qPrintable(QString("silence closed early at %1ms").arg(actual)));
        QVERIFY2(actual <= 210000, "Host scheduling exceeded observation upper bound; this is not an RT measurement");
        QCOMPARE(scenario->silenceNotices, 1);
        QVERIFY(scenario->trafficCount >= 150);
        QVERIFY(scenario->adObserved >= 50);
        QVERIFY(scenario->invalidObserved >= 50);
        QVERIFY(scenario->pendingAtSilence);
        QVERIFY(!scenario->freshAtSilence);
        QCOMPARE(scenario->pendingCancellations, 1);
        QVERIFY(scenario->cleanAfterDisconnect);
        QVERIFY(scenario->silenceReentryRejected);
        QVERIFY2(scenario->errors.isEmpty(), qPrintable(scenario->errors.join(';')));
    }
    QVERIFY(a.reconnect());
    QVERIFY(b.reconnect());
    QVERIFY(waitUntil([&]() { return a.recovered && b.recovered && !a.client.requestPending()
                                    && !b.client.requestPending(); }, 3000));
    QVERIFY(a.client.hasFreshStatus());
    QVERIFY(b.client.hasFreshStatus());
    QVERIFY(a.recoveryGeneration != a.disconnectedGeneration);
    QVERIFY(b.recoveryGeneration != b.disconnectedGeneration);
    qInfo().noquote() << "SILENCE_FINAL_A" << QJsonDocument(a.summary()).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "SILENCE_FINAL_B" << QJsonDocument(b.summary()).toJson(QJsonDocument::Compact);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    qputenv("QTEST_FUNCTION_TIMEOUT", "240000");
    ClientSilenceTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "client_silence_test.moc"
