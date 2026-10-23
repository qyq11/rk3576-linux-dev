#include "read_only_client.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <functional>

using namespace Sn100Protocol;

namespace
{
// Independent synthetic literals: framing/length/XOR calculated without the
// production encoder. These bytes have never been captured from an instrument.
const QByteArray kStatusQuery = QByteArray::fromHex("0203000600000001516a3a0405");
const QByteArray kAlarmQuery = QByteArray::fromHex("0203000600000001516b3b0405");
const QByteArray kConsumablesQuery = QByteArray::fromHex("02030006000000015148180405");
const QByteArray kStatus = QByteArray::fromHex(
    "0607002000000001516a5b345d5b53494d554c415445445d5b325d5b315d5b305d5b305d570809");
const QByteArray kInvalidStatus = QByteArray::fromHex(
    "0607002100000001516a5b345d5b53494d554c415445445d5b39395d5b315d5b305d5b305d650809");
const QByteArray kAlarm = QByteArray::fromHex("0607000900000001516b5b305d0d0809");
const QByteArray kAlarmNotification = QByteArray::fromHex("0607000900000001516c5b305d0a0809");
const QByteArray kChannel = QByteArray::fromHex("060700090000000151715b305d170809");
const QByteArray kConsumables = QByteArray::fromHex("0607000c0000000151485b305d5b305d180809");

class ClientPeer : public QObject
{
public:
    ClientPeer()
    {
        connect(&server, &QTcpServer::newConnection, this, [this]()
        {
            while(server.hasPendingConnections())
            {
                QTcpSocket *accepted = server.nextPendingConnection();
                socket = accepted;
                receivedOnCurrent.clear();
                ++connections;
                connect(accepted, &QTcpSocket::readyRead, this, [this, accepted]()
                {
                    const QByteArray bytes = accepted->readAll();
                    received.append(bytes);
                    if(socket == accepted)
                        receivedOnCurrent.append(bytes);
                });
            }
        });
    }

    bool listen()
    {
        return server.listen(QHostAddress::LocalHost, 0);
    }

    QTcpServer server;
    QPointer<QTcpSocket> socket;
    QByteArray received;
    QByteArray receivedOnCurrent;
    int connections = 0;
};

bool spinUntil(const std::function<bool()> &ready, int timeoutMs = 1500)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while(!ready() && elapsed.elapsed() < timeoutMs)
        QTest::qWait(1);
    return ready();
}

bool openConnection(ReadOnlyClient *client, ClientPeer *peer, QString *error)
{
    QSignalSpy connected(client, &ReadOnlyClient::connected);
    const int previousConnections = peer->connections;
    if(!client->connectLoopback(peer->server.serverPort(), error))
        return false;
    if(!spinUntil([&]() { return connected.count() == 1 && peer->connections > previousConnections; }))
    {
        if(error) *error = QStringLiteral("Test loopback connection did not complete within 1500ms");
        return false;
    }
    return true;
}

int successes(const QSignalSpy &finished)
{
    int count = 0;
    for(const auto &arguments : finished)
        count += arguments.at(1).toBool() ? 1 : 0;
    return count;
}

bool finishedAs(const QList<QVariant> &arguments, ReadOnlyQuery query, bool success, quint64 generation)
{
    return qvariant_cast<ReadOnlyQuery>(arguments.at(0)) == query
            && arguments.at(1).toBool() == success
            && !arguments.at(2).toString().isEmpty()
            && arguments.at(3).toULongLong() == generation;
}
}

class ReadOnlyClientTest : public QObject
{
    Q_OBJECT

private slots:
    void connectionDoesNotAutomaticallyQuery();
    void validStatusIsObservedBeforeSuccessfulFinish();
    void onePendingRequestRejectsConcurrentQuery();
    void queueRejectionDoesNotLeavePendingRequest();
    void timeoutRetriesExactlyOnceThenFails();
    void overdueStatusCannotTurnTimeoutIntoSuccess();
    void timeoutReceiverNewRequestIsNotFinishedByOldObservation();
    void alarmWaitsForMatchingCurrentAlarm();
    void malformedStatusDoesNotRefreshOrFinish();
    void consumablesRequestUsesOnlyReadOnlyLiteral();
    void disconnectCancelsRequestAndFreshness();
    void reconnectRejectsOldHalfStatus();
    void invalidQueryDoesNotWriteOrBecomePending();
    void observedReceiverCanClose();
    void observedReceiverCanReconnectWithoutFinishingNewRequest();
    void queuedReceiverCanReconnectWithoutWritingOldQuery();
    void finishedReceiverCanReconnectWithoutClearingNewRequest();
};

void ReadOnlyClientTest::connectionDoesNotAutomaticallyQuery()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client;
    QSignalSpy queued(&client, &ReadOnlyClient::queryQueued);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QTest::qWait(40);
    QCOMPARE(peer.received, QByteArray());
    QCOMPARE(queued.count(), 0);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QVERIFY(!client.linkState().isEmpty());
}

void ReadOnlyClientTest::validStatusIsObservedBeforeSuccessfulFinish()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy observed(&client, &ReadOnlyClient::messageObserved);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QSignalSpy queued(&client, &ReadOnlyClient::queryQueued);
    QStringList order;
    connect(&client, &ReadOnlyClient::messageObserved, this,
            [&](ReadOnlyMessage message, quint64)
    {
        order.append("observed:" + QString::fromLatin1(message.command));
    });
    connect(&client, &ReadOnlyClient::requestFinished, this,
            [&](ReadOnlyQuery, bool success, QString, quint64)
    {
        order.append(success ? "success" : "failure");
    });
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    const quint64 generation = client.generation();
    QVERIFY2(client.request(ReadOnlyQuery::Status, &error), qPrintable(error));
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QCOMPARE(finished.count(), 0);
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return finished.count() == 1; }));
    QCOMPARE(observed.count(), 1);
    QCOMPARE(qvariant_cast<ReadOnlyMessage>(observed.first().at(0)).command, QByteArray("Qj"));
    QCOMPARE(observed.first().at(1).toULongLong(), generation);
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Status, true, generation));
    QCOMPARE(order, QStringList({"observed:Qj", "success"}));
    QVERIFY(!client.requestPending());
    QVERIFY(client.hasFreshStatus());
    QCOMPARE(queued.count(), 1);
    QCOMPARE(qvariant_cast<ReadOnlyQuery>(queued.first().at(0)), ReadOnlyQuery::Status);
    QCOMPARE(queued.first().at(1).toInt(), 1);
    QCOMPARE(queued.first().at(2).toULongLong(), generation);
    qInfo().noquote() << "CLIENT actual peer RX generation=" << generation
                      << "HEX=" << peer.received.toHex(' ') << "PAYLOAD=Qj";
    qInfo().noquote() << "CLIENT actual peer TX HEX=" << kStatus.toHex(' ')
                      << "OBSERVED=Qj[4][SIMULATED][2][1][0][0] order=" << order.join(',');
}

void ReadOnlyClientTest::onePendingRequestRejectsConcurrentQuery()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(!client.request(ReadOnlyQuery::Alarm, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(client.requestPending());
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QTest::qWait(20);
    QCOMPARE(peer.received, kStatusQuery);
    QCOMPARE(finished.count(), 0);
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return !client.requestPending(); }));
    QCOMPARE(successes(finished), 1);
}

void ReadOnlyClientTest::queueRejectionDoesNotLeavePendingRequest()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 1, 12);
    QSignalSpy queued(&client, &ReadOnlyClient::queryQueued);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(!client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QTest::qWait(40);
    QCOMPARE(peer.received, QByteArray());
    QCOMPARE(queued.count(), 0);
}

void ReadOnlyClientTest::timeoutRetriesExactlyOnceThenFails()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 30, 1);
    QSignalSpy queued(&client, &ReadOnlyClient::queryQueued);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    const quint64 generation = client.generation();
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return finished.count() == 1; }, 1000));
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Status, false, generation));
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QVERIFY(spinUntil([&]() { return peer.received.size() >= 2 * kStatusQuery.size(); }));
    QTest::qWait(100);
    QCOMPARE(peer.received, kStatusQuery + kStatusQuery);
    QCOMPARE(queued.count(), 2);
    QCOMPARE(queued.at(0).at(1).toInt(), 1);
    QCOMPARE(queued.at(1).at(1).toInt(), 2);
    QCOMPARE(finished.count(), 1);
    qInfo().noquote() << "CLIENT timeout_ms=30 maxRetries=1 generation=" << generation
                      << "actual peer RX=" << peer.received.toHex(' ')
                      << "attempts=1,2 finished=false pending=false fresh=false";
}

void ReadOnlyClientTest::overdueStatusCannotTurnTimeoutIntoSuccess()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QSignalSpy observed(&client, &ReadOnlyClient::messageObserved);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    const quint64 generation = client.generation();
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    // Deliberately make this test event loop late: the production client must
    // consult the monotonic deadline when input arrives, even before timer tick.
    QTest::qSleep(5);
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    peer.socket->flush();
    QVERIFY(spinUntil([&]() { return observed.count() == 1 && finished.count() == 1; }));
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Status, false, generation));
    QVERIFY(!client.requestPending());
    // A valid unsolicited/late observation remains observable. It cannot
    // retroactively turn the already-expired request into a successful one.
    QCOMPARE(successes(finished), 0);
    qInfo() << "CLIENT overdue deadline_ms=1 artificial_event_loop_delay_ms=5"
            << "finished_success=false valid_late_observations=" << observed.count();
}

void ReadOnlyClientTest::timeoutReceiverNewRequestIsNotFinishedByOldObservation()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    bool replacementAccepted = false;
    bool firstTimeout = true;
    bool pendingAtOldObservation = false;
    bool closed = false;
    QString replacementError;
    connect(&client, &ReadOnlyClient::requestFinished, this,
            [&](ReadOnlyQuery query, bool success, QString, quint64)
    {
        if(firstTimeout && query == ReadOnlyQuery::Status && !success)
        {
            firstTimeout = false;
            replacementAccepted = client.request(ReadOnlyQuery::Status, &replacementError);
        }
    });
    connect(&client, &ReadOnlyClient::messageObserved, this, [&](ReadOnlyMessage, quint64)
    {
        pendingAtOldObservation = client.requestPending();
        // Close next turn, after observe() has completed. A missing request
        // revision guard would falsely finish the replacement before this.
        QTimer::singleShot(0, &client, [&]()
        {
            client.close();
            closed = true;
        });
    });
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QTest::qSleep(5);
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    peer.socket->flush();
    QVERIFY(spinUntil([&]() { return closed; }));
    QVERIFY2(replacementAccepted, qPrintable(replacementError));
    QVERIFY(pendingAtOldObservation);
    QCOMPARE(successes(finished), 0);
    QVERIFY(!client.requestPending());
    qInfo().noquote() << "CLIENT timeout direct slot started replacement=true"
                      << "old observation replacement_pending=true false_successes=" << successes(finished)
                      << "actual peer RX=" << peer.received.toHex(' ');
}

void ReadOnlyClientTest::alarmWaitsForMatchingCurrentAlarm()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy observed(&client, &ReadOnlyClient::messageObserved);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.request(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kAlarmQuery; }));
    const QByteArray otherMessages = kChannel + kAlarmNotification;
    QCOMPARE(peer.socket->write(otherMessages), qint64(otherMessages.size()));
    QVERIFY(spinUntil([&]() { return observed.count() == 2; }));
    QCOMPARE(qvariant_cast<ReadOnlyMessage>(observed.at(0).at(0)).command, QByteArray("Qq"));
    QCOMPARE(qvariant_cast<ReadOnlyMessage>(observed.at(1).at(0)).command, QByteArray("Ql"));
    QCOMPARE(finished.count(), 0);
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(peer.socket->write(kAlarm), qint64(kAlarm.size()));
    QVERIFY(spinUntil([&]() { return finished.count() == 1; }));
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Alarm, true, client.generation()));
    QCOMPARE(observed.count(), 3);
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    qInfo().noquote() << "CLIENT actual peer RX=" << peer.received.toHex(' ')
                      << "observed=Qq,Ql,Qk matching completion=Qk only";
}

void ReadOnlyClientTest::malformedStatusDoesNotRefreshOrFinish()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy observed(&client, &ReadOnlyClient::messageObserved);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QSignalSpy notices(&client, &ReadOnlyClient::notice);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QCOMPARE(peer.socket->write(kInvalidStatus), qint64(kInvalidStatus.size()));
    QVERIFY(spinUntil([&]() { return notices.count() > 0; }));
    QCOMPARE(observed.count(), 0);
    QCOMPARE(finished.count(), 0);
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return finished.count() == 1; }));
    QCOMPARE(successes(finished), 1);
    QVERIFY(client.hasFreshStatus());
    qInfo().noquote() << "CLIENT invalid status main=99 rejected; notice="
                      << notices.last().at(0).toString() << "later valid Qj completed";
}

void ReadOnlyClientTest::consumablesRequestUsesOnlyReadOnlyLiteral()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy observed(&client, &ReadOnlyClient::messageObserved);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.request(ReadOnlyQuery::Consumables, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kConsumablesQuery; }));
    QCOMPARE(peer.socket->write(kConsumables), qint64(kConsumables.size()));
    QVERIFY(spinUntil([&]() { return finished.count() == 1; }));
    QCOMPARE(observed.count(), 1);
    QCOMPARE(qvariant_cast<ReadOnlyMessage>(observed.first().at(0)).command, QByteArray("QH"));
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Consumables, true, client.generation()));
    QVERIFY(!client.hasFreshStatus());
    qInfo().noquote() << "CLIENT actual peer RX=" << peer.received.toHex(' ')
                      << "actual peer TX=" << kConsumables.toHex(' ') << "matching completion=QH";
}

void ReadOnlyClientTest::disconnectCancelsRequestAndFreshness()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QSignalSpy disconnected(&client, &ReadOnlyClient::disconnected);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return client.hasFreshStatus() && !client.requestPending(); }));
    finished.clear();
    QVERIFY(client.request(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery + kAlarmQuery; }));
    const quint64 generation = client.generation();
    peer.socket->abort();
    QVERIFY(spinUntil([&]() { return disconnected.count() == 1; }));
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(finished.count(), 1);
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Alarm, false, generation));
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.generation() > generation);
    QTest::qWait(20);
    QCOMPARE(peer.receivedOnCurrent, QByteArray());
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
}

void ReadOnlyClientTest::reconnectRejectsOldHalfStatus()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy observed(&client, &ReadOnlyClient::messageObserved);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    const quint64 oldGeneration = client.generation();
    QCOMPARE(peer.socket->write(kStatus.left(10)), qint64(10));
    QTest::qWait(20);
    client.close();
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    finished.clear();
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(client.generation() > oldGeneration);
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.receivedOnCurrent == kStatusQuery; }));
    const QByteArray tail = kStatus.mid(10);
    QCOMPARE(peer.socket->write(tail), qint64(tail.size()));
    QTest::qWait(30);
    QCOMPARE(observed.count(), 0);
    QCOMPARE(finished.count(), 0);
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return finished.count() == 1; }));
    QVERIFY(finishedAs(finished.first(), ReadOnlyQuery::Status, true, client.generation()));
    QCOMPARE(observed.count(), 1);
}

void ReadOnlyClientTest::invalidQueryDoesNotWriteOrBecomePending()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client;
    QString error;
    QVERIFY(!client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    QVERIFY(!client.request(static_cast<ReadOnlyQuery>(999), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!client.requestPending());
    QTest::qWait(20);
    QCOMPARE(peer.received, QByteArray());
}

void ReadOnlyClientTest::observedReceiverCanClose()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    int observations = 0;
    connect(&client, &ReadOnlyClient::messageObserved, this, [&](ReadOnlyMessage, quint64)
    {
        ++observations;
        client.close();
    });
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QCOMPARE(peer.socket->write(kStatus + kStatus), qint64(2 * kStatus.size()));
    QVERIFY(spinUntil([&]() { return observations == 1; }));
    QTest::qWait(20);
    QCOMPARE(observations, 1);
    QCOMPARE(successes(finished), 0);
    QVERIFY(!client.requestPending());
    QVERIFY(!client.hasFreshStatus());
}

void ReadOnlyClientTest::observedReceiverCanReconnectWithoutFinishingNewRequest()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    const quint64 oldGeneration = client.generation();
    QString reentryError;
    connect(&client, &ReadOnlyClient::connected, this, [&](quint64 generation)
    {
        if(generation != oldGeneration)
            client.request(ReadOnlyQuery::Alarm, &reentryError);
    });
    connect(&client, &ReadOnlyClient::messageObserved, this,
            [&](ReadOnlyMessage message, quint64 generation)
    {
        if(generation == oldGeneration && message.command == "Qj")
        {
            client.close();
            client.connectLoopback(peer.server.serverPort(), &reentryError);
        }
    });
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return peer.connections == 2 && peer.receivedOnCurrent == kAlarmQuery; }));
    QVERIFY2(reentryError.isEmpty(), qPrintable(reentryError));
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(successes(finished), 0);
    QCOMPARE(peer.socket->write(kAlarm), qint64(kAlarm.size()));
    QVERIFY(spinUntil([&]() { return successes(finished) == 1; }));
    QVERIFY(finishedAs(finished.last(), ReadOnlyQuery::Alarm, true, client.generation()));
}

void ReadOnlyClientTest::queuedReceiverCanReconnectWithoutWritingOldQuery()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    const quint64 oldGeneration = client.generation();
    QString reentryError;
    connect(&client, &ReadOnlyClient::connected, this, [&](quint64 generation)
    {
        if(generation != oldGeneration)
            client.request(ReadOnlyQuery::Alarm, &reentryError);
    });
    connect(&client, &ReadOnlyClient::queryQueued, this,
            [&](ReadOnlyQuery query, int, quint64 generation)
    {
        if(generation == oldGeneration && query == ReadOnlyQuery::Status)
        {
            client.close();
            client.connectLoopback(peer.server.serverPort(), &reentryError);
        }
    });
    client.request(ReadOnlyQuery::Status, &error);
    QVERIFY(spinUntil([&]() { return peer.connections == 2 && peer.receivedOnCurrent == kAlarmQuery; }));
    QVERIFY2(reentryError.isEmpty(), qPrintable(reentryError));
    QCOMPARE(peer.received, kAlarmQuery);
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(successes(finished), 0);
    QCOMPARE(peer.socket->write(kAlarm), qint64(kAlarm.size()));
    QVERIFY(spinUntil([&]() { return successes(finished) == 1; }));
    QVERIFY(finishedAs(finished.last(), ReadOnlyQuery::Alarm, true, client.generation()));
}

void ReadOnlyClientTest::finishedReceiverCanReconnectWithoutClearingNewRequest()
{
    ClientPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyClient client(nullptr, 1000, 0);
    QSignalSpy finished(&client, &ReadOnlyClient::requestFinished);
    QString error;
    QVERIFY2(openConnection(&client, &peer, &error), qPrintable(error));
    const quint64 oldGeneration = client.generation();
    QString reentryError;
    connect(&client, &ReadOnlyClient::connected, this, [&](quint64 generation)
    {
        if(generation != oldGeneration)
            client.request(ReadOnlyQuery::Alarm, &reentryError);
    });
    connect(&client, &ReadOnlyClient::requestFinished, this,
            [&](ReadOnlyQuery query, bool success, QString, quint64 generation)
    {
        if(generation == oldGeneration && query == ReadOnlyQuery::Status && success)
        {
            client.close();
            client.connectLoopback(peer.server.serverPort(), &reentryError);
        }
    });
    QVERIFY(client.request(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.received == kStatusQuery; }));
    QCOMPARE(peer.socket->write(kStatus), qint64(kStatus.size()));
    QVERIFY(spinUntil([&]() { return peer.connections == 2 && peer.receivedOnCurrent == kAlarmQuery; }));
    QVERIFY2(reentryError.isEmpty(), qPrintable(reentryError));
    QVERIFY(client.requestPending());
    QVERIFY(!client.hasFreshStatus());
    QCOMPARE(successes(finished), 1);
    QCOMPARE(peer.socket->write(kAlarm), qint64(kAlarm.size()));
    QVERIFY(spinUntil([&]() { return successes(finished) == 2; }));
    QVERIFY(finishedAs(finished.last(), ReadOnlyQuery::Alarm, true, client.generation()));
    QVERIFY(!client.requestPending());
}

QTEST_GUILESS_MAIN(ReadOnlyClientTest)
#include "read_only_client_test.moc"
