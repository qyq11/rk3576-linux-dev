#include "frame_codec.h"
#include "tcp_transport.h"

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
// Independently calculated synthetic golden bytes, not an instrument capture.
const QByteArray kStatusQuery = QByteArray::fromHex("0203000600000001516a3a0405");
const QByteArray kAlarmQuery = QByteArray::fromHex("0203000600000001516b3b0405");
const QByteArray kConsumablesQuery = QByteArray::fromHex("02030006000000015148180405");
const QByteArray kAdFrame = QByteArray::fromHex(
    "060700190000000151715b325d5b315d5b3132335d5b325d5b3435365d110809");
const QByteArray kAdMessage("Qq[2][1][123][2][456]");
const QByteArray kAcknowledgement = QByteArray::fromHex("060700090000000154735b315d110809");
const QByteArray kPartZero = QByteArray::fromHex("0607000c0000000251715b325d5b315d210809");
const QByteArray kPartOne = QByteArray::fromHex("06070011000100025b3132335d5b325d5b3435365d300809");

// Only a test peer listens. No legacy configuration or external address is read.
class LoopbackPeer : public QObject
{
public:
    explicit LoopbackPeer(QObject *parent = nullptr) : QObject(parent)
    {
        connect(&server, &QTcpServer::newConnection, this, [this]()
        {
            while(server.hasPendingConnections())
            {
                QTcpSocket *accepted = server.nextPendingConnection();
                socket = accepted;
                ++connections;
                connect(accepted, &QTcpSocket::readyRead, this, [this, accepted]()
                {
                    const QByteArray received = accepted->readAll();
                    allReceived.append(received);
                    if(socket == accepted)
                        receivedOnCurrentConnection.append(received);
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
    QByteArray allReceived;
    QByteArray receivedOnCurrentConnection;
    int connections = 0;
};

bool spinUntil(const std::function<bool()> &ready, int timeoutMs = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while(!ready() && elapsed.elapsed() < timeoutMs)
        QTest::qWait(1);
    return ready();
}

bool openConnection(ReadOnlyTcpTransport *transport, LoopbackPeer *peer, QString *error)
{
    const int previousConnections = peer->connections;
    peer->receivedOnCurrentConnection.clear();
    if(!transport->connectLoopback(peer->server.serverPort(), error))
        return false;
    return spinUntil([transport, peer, previousConnections]()
    {
        return transport->isConnected() && peer->connections > previousConnections && peer->socket;
    });
}

QByteArray joinedSignalBytes(const QSignalSpy &spy)
{
    QByteArray bytes;
    for(const auto &arguments : spy)
        bytes.append(arguments.at(0).toByteArray());
    return bytes;
}

bool allSignalGenerationsEqual(const QSignalSpy &spy, quint64 generation)
{
    for(const auto &arguments : spy)
    {
        if(arguments.last().toULongLong() != generation)
            return false;
    }
    return true;
}
}

class TcpTransportTest : public QObject
{
    Q_OBJECT

private slots:
    void literalQueriesReachActualPeer();
    void oneByteWriteQuantumPreservesQueueOrder();
    void fragmentedReceive_data();
    void fragmentedReceive();
    void bytewiseReceive();
    void coalescedReceive();
    void checksumErrorThenRecovery();
    void multipartReceive();
    void maximumMessageAcrossReceiveChunks();
    void disconnectClearsHalfFrame();
    void disconnectClearsMultipartAssembly();
    void explicitCloseDropsQueuedWrites();
    void reconnectDoesNotPublishOldCallbacks();
    void messageReceiverCanCloseTransport();
    void queueBudgetRejectsWholeQuery();
    void budgetSmallerThanOneFrameRejectsWholeQuery();
    void disconnectedAndInvalidQueriesAreRejected();
    void invalidPortDoesNotConnect();
    void refusedConnectionReportsFailure();
    void peerCloseIsVisibleAndNoAutomaticReconnect();
    void finalBurstBeforePeerFinIsDrained();
    void eventLoopHeartbeatUnderNetworkLoad();
};

void TcpTransportTest::literalQueriesReachActualPeer()
{
    LoopbackPeer peer;
    QVERIFY2(peer.listen(), qPrintable(peer.server.errorString()));
    ReadOnlyTcpTransport transport;
    QSignalSpy connected(&transport, &ReadOnlyTcpTransport::connected);
    QSignalSpy queued(&transport, &ReadOnlyTcpTransport::wireQueued);
    QSignalSpy delivered(&transport, &ReadOnlyTcpTransport::bytesDelivered);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(connected.count(), 1);
    const quint64 generation = transport.generation();
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Consumables, &error));
    const QByteArray expected = kStatusQuery + kAlarmQuery + kConsumablesQuery;
    QVERIFY(spinUntil([&]() { return peer.allReceived.size() == expected.size(); }));
    QCOMPARE(peer.allReceived, expected);
    QCOMPARE(joinedSignalBytes(queued), expected);
    QVERIFY(allSignalGenerationsEqual(queued, generation));
    QVERIFY(spinUntil([&]() { return transport.pendingWriteBytes() == 0; }));
    qint64 deliveredBytes = 0;
    for(const auto &arguments : delivered)
        deliveredBytes += arguments.at(0).toLongLong();
    QCOMPARE(deliveredBytes, qint64(expected.size()));
    QVERIFY(allSignalGenerationsEqual(delivered, generation));
    qInfo().noquote() << "LOOPBACK actual peer RX generation=" << generation
                      << "HEX=" << peer.allReceived.toHex(' ') << "PAYLOADS=Qj,Qk,QH";
}

void TcpTransportTest::oneByteWriteQuantumPreservesQueueOrder()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport(nullptr, 128, 1);
    QSignalSpy queued(&transport, &ReadOnlyTcpTransport::wireQueued);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Consumables, &error));
    const QByteArray expected = kStatusQuery + kAlarmQuery + kConsumablesQuery;
    QVERIFY(spinUntil([&]() { return peer.allReceived.size() == expected.size(); }));
    QCOMPARE(peer.allReceived, expected);
    QCOMPARE(joinedSignalBytes(queued), expected);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    qInfo() << "WRITE quantum=1 bytes=" << expected.size()
            << "actual peer ordered bytes verified; this does not force OS short writes";
}

void TcpTransportTest::fragmentedReceive_data()
{
    QTest::addColumn<int>("split");
    QTest::newRow("header-byte") << 1;
    QTest::newRow("length") << 3;
    QTest::newRow("packet-index") << 5;
    QTest::newRow("payload") << 15;
    QTest::newRow("trailer") << kAdFrame.size() - 1;
}

void TcpTransportTest::fragmentedReceive()
{
    QFETCH(int, split);
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QSignalSpy wire(&transport, &ReadOnlyTcpTransport::wireReceived);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.socket->write(kAdFrame.left(split)), qint64(split));
    QVERIFY(spinUntil([&]() { return joinedSignalBytes(wire).size() == split; }));
    QCOMPARE(received.count(), 0);
    QCOMPARE(peer.socket->write(kAdFrame.mid(split)), qint64(kAdFrame.size() - split));
    QVERIFY(spinUntil([&]() { return received.count() == 1; }));
    QCOMPARE(received.first().at(0).toByteArray(), kAdMessage);
    QCOMPARE(received.first().at(1).toULongLong(), transport.generation());
    QCOMPARE(joinedSignalBytes(wire), kAdFrame);
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
}

void TcpTransportTest::bytewiseReceive()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QSignalSpy wire(&transport, &ReadOnlyTcpTransport::wireReceived);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    for(int offset = 0; offset < kAdFrame.size(); ++offset)
    {
        QCOMPARE(peer.socket->write(kAdFrame.mid(offset, 1)), qint64(1));
        QVERIFY(spinUntil([&]() { return joinedSignalBytes(wire).size() == offset + 1; }, 100));
        QCOMPARE(received.count(), offset == kAdFrame.size() - 1 ? 1 : 0);
    }
    QCOMPARE(received.first().at(0).toByteArray(), kAdMessage);
    QCOMPARE(joinedSignalBytes(wire), kAdFrame);
    qInfo().noquote() << "LOOPBACK actual transport RX generation=" << transport.generation()
                      << "HEX=" << joinedSignalBytes(wire).toHex(' ')
                      << "PAYLOAD=" << received.first().at(0).toByteArray();
}

void TcpTransportTest::coalescedReceive()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const QByteArray bytes = kAdFrame + kAcknowledgement + kAdFrame;
    QCOMPARE(peer.socket->write(bytes), qint64(bytes.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 3; }));
    QCOMPARE(received.at(0).at(0).toByteArray(), kAdMessage);
    QCOMPARE(received.at(1).at(0).toByteArray(), QByteArray("Ts[1]"));
    QCOMPARE(received.at(2).at(0).toByteArray(), kAdMessage);
    QVERIFY(allSignalGenerationsEqual(received, transport.generation()));
}

void TcpTransportTest::checksumErrorThenRecovery()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QByteArray corrupt = kAdFrame;
    corrupt[corrupt.size() - 3] = static_cast<char>(corrupt.at(corrupt.size() - 3) ^ 1);
    const QByteArray input = corrupt + kAdFrame;
    QCOMPARE(peer.socket->write(input), qint64(input.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 1 && !errors.isEmpty(); }));
    QCOMPARE(received.first().at(0).toByteArray(), kAdMessage);
    QVERIFY(transport.isConnected());
    QCOMPARE(transport.bufferedBytes(), 0);
    qInfo().noquote() << "LOOPBACK bad-XOR then recovery input=" << input.toHex(' ')
                      << "ERROR=" << errors.first().at(0).toString()
                      << "OUTPUT=" << received.first().at(0).toByteArray();
}

void TcpTransportTest::multipartReceive()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.socket->write(kPartZero), qint64(kPartZero.size()));
    QVERIFY(spinUntil([&]() { return transport.assemblyBytes() > 0; }));
    QCOMPARE(received.count(), 0);
    QCOMPARE(peer.socket->write(kPartOne), qint64(kPartOne.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 1; }));
    QCOMPARE(received.first().at(0).toByteArray(), kAdMessage);
    QCOMPARE(transport.assemblyBytes(), 0);
}

void TcpTransportTest::maximumMessageAcrossReceiveChunks()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const QByteArray message(kMaxMessageBytes, 'R');
    const auto frames = encodeMessage(message, Direction::DeviceToHost, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(frames.size(), kMaxMessagePackets);
    QByteArray wire;
    for(const auto &frame : frames)
        wire.append(frame);
    QVERIFY(wire.size() > kMaxFeedBytes);
    QCOMPARE(peer.socket->write(wire), qint64(wire.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 1; }));
    QCOMPARE(received.first().at(0).toByteArray(), message);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
    qInfo() << "MAXIMUM RX message_bytes=" << message.size() << "wire_bytes=" << wire.size()
            << "packets=" << frames.size() << "decoded_message_count=" << received.count();
}

void TcpTransportTest::disconnectClearsHalfFrame()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 firstGeneration = transport.generation();
    QCOMPARE(peer.socket->write(kAdFrame.left(10)), qint64(10));
    QVERIFY(spinUntil([&]() { return transport.bufferedBytes() > 0; }));
    peer.socket->abort();
    QVERIFY(spinUntil([&]() { return !transport.isConnected() && !disconnected.isEmpty(); }));
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.generation() > firstGeneration);
    QSignalSpy wire(&transport, &ReadOnlyTcpTransport::wireReceived);
    const QByteArray oldTail = kAdFrame.mid(10);
    QCOMPARE(peer.socket->write(oldTail), qint64(oldTail.size()));
    QVERIFY(spinUntil([&]() { return joinedSignalBytes(wire) == oldTail; }));
    QCOMPARE(received.count(), 0);
    QCOMPARE(peer.socket->write(kAdFrame), qint64(kAdFrame.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 1; }));
    QCOMPARE(received.first().at(1).toULongLong(), transport.generation());
    qInfo() << "HALF-FRAME old generation=" << firstGeneration
            << "new generation=" << transport.generation() << "old tail rejected; new full frame accepted";
}

void TcpTransportTest::disconnectClearsMultipartAssembly()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.socket->write(kPartZero), qint64(kPartZero.size()));
    QVERIFY(spinUntil([&]() { return transport.assemblyBytes() > 0; }));
    peer.socket->abort();
    QVERIFY(spinUntil([&]() { return !transport.isConnected(); }));
    QCOMPARE(transport.assemblyBytes(), 0);
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QSignalSpy wire(&transport, &ReadOnlyTcpTransport::wireReceived);
    QCOMPARE(peer.socket->write(kPartOne), qint64(kPartOne.size()));
    QVERIFY(spinUntil([&]() { return joinedSignalBytes(wire) == kPartOne; }));
    QCOMPARE(received.count(), 0);
    const QByteArray complete = kPartZero + kPartOne;
    QCOMPARE(peer.socket->write(complete), qint64(complete.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 1; }));
    QCOMPARE(received.first().at(0).toByteArray(), kAdMessage);
}

void TcpTransportTest::explicitCloseDropsQueuedWrites()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport(nullptr, 128, 1);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    const quint64 firstGeneration = transport.generation();
    transport.close();
    QVERIFY(!transport.isConnected());
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.generation() > firstGeneration);
    QTest::qWait(20);
    QCOMPARE(peer.receivedOnCurrentConnection, QByteArray());
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Consumables, &error));
    QVERIFY(spinUntil([&]() { return peer.receivedOnCurrentConnection == kConsumablesQuery; }));
    QCOMPARE(peer.receivedOnCurrentConnection, kConsumablesQuery);
}

void TcpTransportTest::reconnectDoesNotPublishOldCallbacks()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 firstGeneration = transport.generation();
    QCOMPARE(peer.socket->write(kAdFrame), qint64(kAdFrame.size()));
    peer.socket->flush();
    peer.socket->abort();
    transport.close();
    QSignalSpy received(&transport, &ReadOnlyTcpTransport::messageReceived);
    QSignalSpy wire(&transport, &ReadOnlyTcpTransport::wireReceived);
    QSignalSpy connected(&transport, &ReadOnlyTcpTransport::connected);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 secondGeneration = transport.generation();
    QVERIFY(secondGeneration > firstGeneration);
    QTest::qWait(20);
    QCOMPARE(received.count(), 0);
    QCOMPARE(wire.count(), 0);
    QCOMPARE(disconnected.count(), 0);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(connected.count(), 1);
    QVERIFY(allSignalGenerationsEqual(connected, secondGeneration));
    QCOMPARE(peer.socket->write(kAdFrame), qint64(kAdFrame.size()));
    QVERIFY(spinUntil([&]() { return received.count() == 1; }));
    QVERIFY(allSignalGenerationsEqual(received, secondGeneration));
    QVERIFY(allSignalGenerationsEqual(wire, secondGeneration));
}

void TcpTransportTest::messageReceiverCanCloseTransport()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    int messages = 0;
    connect(&transport, &ReadOnlyTcpTransport::messageReceived, this,
            [&](const QByteArray &, quint64)
    {
        ++messages;
        transport.close();
    });
    const QByteArray bytes = kAdFrame + kAcknowledgement;
    QCOMPARE(peer.socket->write(bytes), qint64(bytes.size()));
    QVERIFY(spinUntil([&]() { return messages > 0; }));
    QTest::qWait(20);
    QCOMPARE(messages, 1);
    QVERIFY(!transport.isConnected());
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
}

void TcpTransportTest::queueBudgetRejectsWholeQuery()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport(nullptr, 20, 1);
    QSignalSpy queued(&transport, &ReadOnlyTcpTransport::wireQueued);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(transport.pendingWriteBytes() > 0);
    QVERIFY(!transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(transport.pendingWriteBytes() <= 20);
    QVERIFY(spinUntil([&]() { return peer.allReceived == kStatusQuery && transport.pendingWriteBytes() == 0; }));
    QTest::qWait(20);
    QCOMPARE(peer.allReceived, kStatusQuery);
    QCOMPARE(joinedSignalBytes(queued), kStatusQuery);
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Consumables, &error));
    QVERIFY(spinUntil([&]() { return peer.allReceived == kStatusQuery + kConsumablesQuery; }));
    qInfo() << "QUEUE budget=20 accepted=13 rejected=13 partial-rejected-bytes=0; capacity restored after drain";
}

void TcpTransportTest::budgetSmallerThanOneFrameRejectsWholeQuery()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport(nullptr, 12, 1);
    QSignalSpy queued(&transport, &ReadOnlyTcpTransport::wireQueued);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(!transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QTest::qWait(20);
    QCOMPARE(peer.allReceived, QByteArray());
    QCOMPARE(queued.count(), 0);
}

void TcpTransportTest::disconnectedAndInvalidQueriesAreRejected()
{
    ReadOnlyTcpTransport transport;
    QString error;
    QVERIFY(!transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QSignalSpy queued(&transport, &ReadOnlyTcpTransport::wireQueued);
    error.clear();
    QVERIFY(!transport.queueQuery(static_cast<ReadOnlyQuery>(999), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QTest::qWait(20);
    QCOMPARE(peer.allReceived, QByteArray());
    QCOMPARE(queued.count(), 0);
}

void TcpTransportTest::invalidPortDoesNotConnect()
{
    ReadOnlyTcpTransport transport;
    QSignalSpy connected(&transport, &ReadOnlyTcpTransport::connected);
    QString error;
    QVERIFY(!transport.connectLoopback(0, &error));
    QVERIFY(!error.isEmpty());
    QTest::qWait(20);
    QVERIFY(!transport.isConnected());
    QCOMPARE(connected.count(), 0);
}

void TcpTransportTest::refusedConnectionReportsFailure()
{
    // Choose a port from our own successful temporary listen, then close it.
    // This never guesses an existing service or connects outside loopback.
    QTcpServer reservation;
    QVERIFY(reservation.listen(QHostAddress::LocalHost, 0));
    const quint16 port = reservation.serverPort();
    reservation.close();
    ReadOnlyTcpTransport transport(nullptr, 128, 1, 100);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy connected(&transport, &ReadOnlyTcpTransport::connected);
    QString error;
    QVERIFY(transport.connectLoopback(port, &error));
    QVERIFY(spinUntil([&]() { return !errors.isEmpty(); }, 1000));
    QVERIFY(!transport.isConnected());
    QCOMPARE(connected.count(), 0);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QTest::qWait(20);
    QCOMPARE(connected.count(), 0);
    qInfo() << "LOOPBACK reserved-then-closed port failure generation=" << transport.generation()
            << "ERROR=" << errors.first().at(0).toString();
}

void TcpTransportTest::peerCloseIsVisibleAndNoAutomaticReconnect()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 generation = transport.generation();
    peer.socket->disconnectFromHost();
    QVERIFY(spinUntil([&]() { return !transport.isConnected() && disconnected.count() == 1; }));
    QCOMPARE(disconnected.first().at(0).toULongLong(), generation);
    QTest::qWait(30);
    QCOMPARE(peer.connections, 1);
    QVERIFY(!transport.queueQuery(ReadOnlyQuery::Status, &error));
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
}

void TcpTransportTest::finalBurstBeforePeerFinIsDrained()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    const quint64 generation = transport.generation();
    int received = 0;
    int invalid = 0;
    qint64 observedBytes = 0;
    connect(&transport, &ReadOnlyTcpTransport::messageReceived, this,
            [&](const QByteArray &message, quint64 messageGeneration)
    {
        ++received;
        if(message != kAdMessage || messageGeneration != generation)
            ++invalid;
    });
    connect(&transport, &ReadOnlyTcpTransport::wireReceived, this,
            [&](const QByteArray &bytes, quint64)
    {
        observedBytes += bytes.size();
    });
    constexpr int frames = 2000;
    QByteArray bytes;
    for(int index = 0; index < frames; ++index)
        bytes.append(kAdFrame);
    QVERIFY(bytes.size() > 16384);
    QCOMPARE(peer.socket->write(bytes), qint64(bytes.size()));
    // Qt flushes the accepted bytes and only then sends FIN. Valid final data
    // must reach the shared decoder before this connection is discarded.
    peer.socket->disconnectFromHost();
    QVERIFY(spinUntil([&]() { return !transport.isConnected() && disconnected.count() == 1; }));
    qInfo() << "FINAL BURST generation=" << generation << "sent_frames=" << frames
            << "sent_wire_bytes=" << bytes.size() << "observed_wire_bytes=" << observedBytes
            << "received_frames=" << received << "lost_frames=" << frames - received;
    QCOMPARE(invalid, 0);
    QCOMPARE(observedBytes, qint64(bytes.size()));
    QCOMPARE(received, frames);
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
}

void TcpTransportTest::eventLoopHeartbeatUnderNetworkLoad()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    ReadOnlyTcpTransport transport;
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(1);
    qint64 previousTickNs = elapsed.nsecsElapsed();
    qint64 maximumGapNs = 0;
    int heartbeats = 0;
    connect(&heartbeat, &QTimer::timeout, this, [&]()
    {
        const qint64 nowNs = elapsed.nsecsElapsed();
        maximumGapNs = qMax(maximumGapNs, nowNs - previousTickNs);
        previousTickNs = nowNs;
        ++heartbeats;
    });
    int received = 0;
    int invalidMessages = 0;
    connect(&transport, &ReadOnlyTcpTransport::messageReceived, this,
            [&](const QByteArray &message, quint64 generation)
    {
        ++received;
        if(message != kAdMessage || generation != transport.generation())
            ++invalidMessages;
    });
    QByteArray batch;
    for(int index = 0; index < 20; ++index)
        batch.append(kAdFrame);
    QTimer producer;
    producer.setTimerType(Qt::PreciseTimer);
    producer.setInterval(1);
    int batches = 0;
    qint64 submitted = 0;
    connect(&producer, &QTimer::timeout, this, [&]()
    {
        submitted += peer.socket->write(batch);
        if(++batches == 100)
            producer.stop();
    });
    heartbeat.start();
    producer.start();
    QVERIFY(spinUntil([&]() { return received >= 2000; }, 3000));
    heartbeat.stop();
    producer.stop();
    QCOMPARE(received, 2000);
    QCOMPARE(invalidMessages, 0);
    QCOMPARE(submitted, qint64(2000 * kAdFrame.size()));
    QVERIFY(heartbeats >= 20);
    QVERIFY2(maximumGapNs < 500000000, qPrintable(QString("Heartbeat maximum gap %1 ms")
                                               .arg(maximumGapNs / 1000000.0)));
    QCOMPARE(transport.bufferedBytes(), 0);
    QCOMPARE(transport.assemblyBytes(), 0);
    qInfo() << "NETWORK load messages=" << received << "bytes=" << submitted
            << "elapsed_ms=" << elapsed.elapsed() << "heartbeat_period_ms=1 samples=" << heartbeats
            << "maximum_gap_ms=" << maximumGapNs / 1000000.0;
}

QTEST_GUILESS_MAIN(TcpTransportTest)
#include "tcp_transport_test.moc"
