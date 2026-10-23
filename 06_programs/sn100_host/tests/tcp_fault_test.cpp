#include <QtGlobal>
#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

#include "tcp_transport.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QPointer>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <functional>

using namespace Sn100Protocol;

namespace
{
const QByteArray kStatusQuery = QByteArray::fromHex("0203000600000001516a3a0405");
const QByteArray kAlarmQuery = QByteArray::fromHex("0203000600000001516b3b0405");
const QByteArray kConsumablesQuery = QByteArray::fromHex("02030006000000015148180405");

bool spinUntil(const std::function<bool()> &ready, int timeoutMs = 1500)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while(!ready() && elapsed.elapsed() < timeoutMs)
        QTest::qWait(1);
    return ready();
}

int nativeSetReceiveBuffer(QTcpSocket *socket, int bytes)
{
#ifdef Q_OS_WIN
    return ::setsockopt(SOCKET(socket->socketDescriptor()), SOL_SOCKET, SO_RCVBUF,
                        reinterpret_cast<const char *>(&bytes), sizeof(bytes));
#else
    return ::setsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_RCVBUF,
                        &bytes, sizeof(bytes));
#endif
}

int nativeResetOnClose(QTcpSocket *socket)
{
    const linger reset = {1, 0};
#ifdef Q_OS_WIN
    return ::setsockopt(SOCKET(socket->socketDescriptor()), SOL_SOCKET, SO_LINGER,
                        reinterpret_cast<const char *>(&reset), sizeof(reset));
#else
    return ::setsockopt(int(socket->socketDescriptor()), SOL_SOCKET, SO_LINGER,
                        &reset, sizeof(reset));
#endif
}

struct WritePlan
{
    // -2 = unmodified real Qt writeData; -1/0 are explicit test injection.
    QList<qint64> returns;
    qint64 fallback = -2;
    bool holdConnecting = false;
};

struct SocketTrace
{
    QList<qint64> requested;
    QList<qint64> returned;
    QByteArray acceptedByBase;
    bool loopbackAddress = false;
    bool noProxy = false;
    int connectionCalls = 0;
};

// Only this test target creates this subclass. The production transport's
// factory creates a normal QTcpSocket and exposes no CLI fault switches.
class FaultSocket : public QTcpSocket
{
public:
    FaultSocket(const WritePlan &plan, const QSharedPointer<SocketTrace> &trace, QObject *parent)
        : QTcpSocket(parent), m_plan(plan), m_trace(trace)
    {
    }

    void connectToHost(const QHostAddress &address, quint16 port, OpenMode mode = ReadWrite) override
    {
        ++m_trace->connectionCalls;
        m_trace->loopbackAddress = address == QHostAddress(QHostAddress::LocalHost);
        m_trace->noProxy = proxy().type() == QNetworkProxy::NoProxy;
        if(m_plan.holdConnecting)
        {
            // Controlled ConnectingState/deadline injection, not a claim of an
            // OS network black hole or an attempted external connection.
            setSocketState(QAbstractSocket::ConnectingState);
            return;
        }
        QTcpSocket::connectToHost(address, port, mode);
    }

protected:
    qint64 writeData(const char *data, qint64 length) override
    {
        m_trace->requested.append(length);
        const qint64 planned = m_plan.returns.isEmpty() ? m_plan.fallback : m_plan.returns.takeFirst();
        if(planned == -1)
        {
            setErrorString(QStringLiteral("INJECTED writeData negative return"));
            m_trace->returned.append(-1);
            return -1;
        }
        if(planned == 0)
        {
            m_trace->returned.append(0);
            return 0;
        }
        const qint64 amount = planned > 0 ? qMin(planned, length) : length;
        const qint64 written = QTcpSocket::writeData(data, amount);
        m_trace->returned.append(written);
        if(written > 0)
            m_trace->acceptedByBase.append(data, int(written));
        return written;
    }

private:
    WritePlan m_plan;
    QSharedPointer<SocketTrace> m_trace;
};

class FaultTransport : public ReadOnlyTcpTransport
{
public:
    explicit FaultTransport(int timeoutMs = 100, int maxQueuedBytes = 128)
        : ReadOnlyTcpTransport(nullptr, maxQueuedBytes, 4096, timeoutMs)
    {
    }

    QQueue<WritePlan> plans;
    QList<QSharedPointer<SocketTrace>> traces;
    QPointer<FaultSocket> lastSocket;
    bool returnNull = false;

protected:
    QTcpSocket *createSocket(QObject *parent) override
    {
        if(returnNull) return nullptr;
        const WritePlan plan = plans.isEmpty() ? WritePlan() : plans.dequeue();
        const auto trace = QSharedPointer<SocketTrace>::create();
        traces.append(trace);
        lastSocket = new FaultSocket(plan, trace, parent);
        return lastSocket;
    }
};

class LoopbackPeer : public QObject
{
public:
    explicit LoopbackPeer(bool consume = true) : m_consume(consume)
    {
        connect(&server, &QTcpServer::newConnection, this, [this]()
        {
            while(server.hasPendingConnections())
            {
                QTcpSocket *accepted = server.nextPendingConnection();
                socket = accepted;
                receivedOnCurrent.clear();
                ++connections;
                if(!m_consume)
                {
                    accepted->setReadBufferSize(1);
                    receiveBufferResult = nativeSetReceiveBuffer(accepted, 1024);
                    continue;
                }
                connect(accepted, &QTcpSocket::readyRead, this, [this, accepted]()
                {
                    const QByteArray bytes = accepted->readAll();
                    allReceived.append(bytes);
                    if(socket == accepted) receivedOnCurrent.append(bytes);
                });
            }
        });
    }

    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    QTcpServer server;
    QPointer<QTcpSocket> socket;
    QByteArray allReceived;
    QByteArray receivedOnCurrent;
    int connections = 0;
    int receiveBufferResult = -1;

private:
    bool m_consume;
};

bool openConnection(FaultTransport *transport, LoopbackPeer *peer, QString *error)
{
    const int previousConnections = peer->connections;
    if(!transport->connectLoopback(peer->server.serverPort(), error)) return false;
    return spinUntil([&]() { return transport->isConnected() && peer->connections > previousConnections; });
}

bool recoverWithGoldenStatus(FaultTransport *transport, LoopbackPeer *peer, QString *error)
{
    if(!openConnection(transport, peer, error)) return false;
    if(!transport->queueQuery(ReadOnlyQuery::Status, error)) return false;
    return spinUntil([&]()
    {
        return peer->receivedOnCurrent == kStatusQuery && transport->pendingWriteBytes() == 0;
    });
}

bool errorContains(const QSignalSpy &errors, const char *category)
{
    for(const auto &arguments : errors)
    {
        if(arguments.at(0).toString().contains(QString::fromLatin1(category))) return true;
    }
    return false;
}
}

class TcpFaultTest : public QObject
{
    Q_OBJECT

private slots:
    void shortWriteActuallyReachesPeerWithoutDuplication();
    void zeroWriteWaitsAndThenRecovers();
    void permanentZeroHitsDeadlineAndReconnects();
    void negativeWriteFailsOnceAndReconnects();
    void shortThenNegativeDropsOldSuffixAndQueue();
    void connectingDeadlineIsControlledAndReconnects();
    void closeDuringZeroWriteCancelsOldTimerAndQueue();
    void errorReceiverCanReconnect();
    void disconnectedReceiverCanReconnect();
    void nullFactoryIsRejectedAndLaterRealConnectionWorks();
    void realPeerResetAndReconnect();
    void realNonReadingPeerBackpressureAndRecovery();
};

void TcpFaultTest::shortWriteActuallyReachesPeerWithoutDuplication()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(500);
    WritePlan plan;
    plan.fallback = 2;
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Consumables, &error));
    const QByteArray expected = kStatusQuery + kAlarmQuery + kConsumablesQuery;
    QVERIFY(spinUntil([&]() { return peer.allReceived == expected && transport.pendingWriteBytes() == 0; }));
    const auto trace = transport.traces.first();
    QCOMPARE(trace->requested.first(), qint64(13));
    QCOMPARE(trace->returned.first(), qint64(2));
    QVERIFY(trace->returned.size() > 3);
    QCOMPARE(trace->acceptedByBase, expected);
    QVERIFY(trace->loopbackAddress && trace->noProxy);
    QCOMPARE(errors.count(), 0);
    qInfo().noquote() << "INJECTED writeData cap=2 requested_first=13 returned_first=2 calls="
                      << trace->returned.size() << "actual peer RX=" << peer.allReceived.toHex(' ');
}

void TcpFaultTest::zeroWriteWaitsAndThenRecovers()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(500);
    WritePlan plan;
    plan.returns = {0};
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return peer.allReceived == kStatusQuery && transport.pendingWriteBytes() == 0; }));
    const auto trace = transport.traces.first();
    QCOMPARE(trace->returned, QList<qint64>({0, 13}));
    QCOMPARE(trace->acceptedByBase, kStatusQuery);
    QCOMPARE(errors.count(), 0);
    qInfo().noquote() << "INJECTED writeData returns=0,13 actual peer RX=" << peer.allReceived.toHex(' ');
}

void TcpFaultTest::permanentZeroHitsDeadlineAndReconnects()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(60);
    WritePlan plan;
    plan.fallback = 0;
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 oldGeneration = transport.generation();
    QElapsedTimer waited;
    waited.start();
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return disconnected.count() == 1; }));
    QVERIFY(errorContains(errors, "transport.write_timeout"));
    const qint64 timeoutElapsedMs = waited.elapsed();
    QVERIFY(timeoutElapsedMs >= 60);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.first().at(0).toULongLong(), oldGeneration);
    QCOMPARE(peer.allReceived, QByteArray());
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QVERIFY(transport.traces.first()->returned.size() <= 10);
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
    QTest::qWait(80);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(peer.receivedOnCurrent, kStatusQuery);
    qInfo().noquote() << "INJECTED permanent-zero calls=" << transport.traces.first()->returned.size()
                      << "deadline_ms=60 actual_timeout_ms=" << timeoutElapsedMs
                      << "old_generation=" << oldGeneration << "recovered_generation=" << transport.generation()
                      << "recovered actual peer RX=" << peer.receivedOnCurrent.toHex(' ');
}

void TcpFaultTest::negativeWriteFailsOnceAndReconnects()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport;
    WritePlan plan;
    plan.returns = {-1};
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return disconnected.count() == 1; }));
    QCOMPARE(transport.traces.first()->returned, QList<qint64>({-1}));
    QVERIFY(errorContains(errors, "transport.write:"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QCOMPARE(peer.allReceived, QByteArray());
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.receivedOnCurrent, kStatusQuery);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 1);
    qInfo() << "INJECTED writeData=-1 terminates exactly once; true loopback reconnect sends only golden Qj";
}

void TcpFaultTest::shortThenNegativeDropsOldSuffixAndQueue()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport;
    WritePlan plan;
    plan.returns = {3, -1};
    transport.plans.enqueue(plan);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Consumables, &error));
    QVERIFY(spinUntil([&]() { return disconnected.count() == 1; }));
    const auto trace = transport.traces.first();
    QCOMPARE(trace->returned, QList<qint64>({3, -1}));
    QCOMPARE(trace->acceptedByBase, kAlarmQuery.left(3));
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.receivedOnCurrent, kStatusQuery);
    qInfo() << "INJECTED short=3 then -1; old partial Qk and queued QH discarded, new connection receives Qj only";
}

void TcpFaultTest::connectingDeadlineIsControlledAndReconnects()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(60);
    WritePlan plan;
    plan.holdConnecting = true;
    transport.plans.enqueue(plan);
    QSignalSpy connected(&transport, &ReadOnlyTcpTransport::connected);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QElapsedTimer waited;
    waited.start();
    QVERIFY(transport.connectLoopback(peer.server.serverPort(), &error));
    const quint64 oldGeneration = transport.generation();
    QVERIFY(transport.lastSocket);
    QCOMPARE(transport.lastSocket->state(), QAbstractSocket::ConnectingState);
    QVERIFY(!transport.queueQuery(ReadOnlyQuery::Status, &error));
    QVERIFY(spinUntil([&]() { return disconnected.count() == 1; }));
    QVERIFY(errorContains(errors, "transport.connect_timeout"));
    const qint64 timeoutElapsedMs = waited.elapsed();
    QVERIFY(timeoutElapsedMs >= 60);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(connected.count(), 0);
    QCOMPARE(peer.connections, 0);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QVERIFY(transport.traces.first()->loopbackAddress && transport.traces.first()->noProxy);
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.generation() > oldGeneration);
    QCOMPARE(connected.count(), 1);
    QTest::qWait(80);
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 1);
    qInfo() << "INJECTED ConnectingState deadline_ms=60 actual_timeout_ms=" << timeoutElapsedMs
            << "no OS black-hole claim; subsequent real Qj reconnect passed";
}

void TcpFaultTest::closeDuringZeroWriteCancelsOldTimerAndQueue()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(60);
    WritePlan plan;
    plan.fallback = 0;
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return !transport.traces.first()->returned.isEmpty(); }));
    transport.close();
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
    QTest::qWait(100);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(peer.receivedOnCurrent, kStatusQuery);
}

void TcpFaultTest::errorReceiverCanReconnect()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport;
    WritePlan plan;
    plan.returns = {-1};
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 oldGeneration = transport.generation();
    QString reentryError;
    bool accepted = false;
    connect(&transport, &ReadOnlyTcpTransport::errorOccurred, this, [&](QString, quint64 generation)
    {
        if(generation == oldGeneration)
            accepted = transport.connectLoopback(peer.server.serverPort(), &reentryError);
    });
    connect(&transport, &ReadOnlyTcpTransport::connected, this, [&](quint64 generation)
    {
        if(generation != oldGeneration)
            transport.queueQuery(ReadOnlyQuery::Status, &reentryError);
    });
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return peer.connections == 2 && peer.receivedOnCurrent == kStatusQuery; }));
    QVERIFY2(accepted, qPrintable(reentryError));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(transport.isConnected());
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
}

void TcpFaultTest::disconnectedReceiverCanReconnect()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(40);
    WritePlan plan;
    plan.fallback = 0;
    transport.plans.enqueue(plan);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    const quint64 oldGeneration = transport.generation();
    QString reentryError;
    bool accepted = false;
    connect(&transport, &ReadOnlyTcpTransport::disconnected, this, [&](quint64 generation)
    {
        if(generation == oldGeneration)
            accepted = transport.connectLoopback(peer.server.serverPort(), &reentryError);
    });
    connect(&transport, &ReadOnlyTcpTransport::connected, this, [&](quint64 generation)
    {
        if(generation != oldGeneration)
            transport.queueQuery(ReadOnlyQuery::Status, &reentryError);
    });
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return peer.connections == 2 && peer.receivedOnCurrent == kStatusQuery; }));
    QVERIFY2(accepted, qPrintable(reentryError));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(transport.isConnected());
}

void TcpFaultTest::nullFactoryIsRejectedAndLaterRealConnectionWorks()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport;
    transport.returnNull = true;
    QString error;
    QVERIFY(!transport.connectLoopback(peer.server.serverPort(), &error));
    QVERIFY(error.contains("transport.factory"));
    QVERIFY(!transport.isConnected());
    QCOMPARE(peer.connections, 0);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    transport.returnNull = false;
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
}

void TcpFaultTest::realPeerResetAndReconnect()
{
    LoopbackPeer peer;
    QVERIFY(peer.listen());
    FaultTransport transport(500);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QVERIFY(transport.queueQuery(ReadOnlyQuery::Alarm, &error));
    QVERIFY(spinUntil([&]() { return peer.receivedOnCurrent == kAlarmQuery; }));
    const quint64 oldGeneration = transport.generation();
    QCOMPARE(nativeResetOnClose(peer.socket), 0);
    peer.socket->abort();
    QVERIFY(spinUntil([&]() { return disconnected.count() == 1; }));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.first().at(0).toULongLong(), oldGeneration);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    QVERIFY2(recoverWithGoldenStatus(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.receivedOnCurrent, kStatusQuery);
    qInfo().noquote() << "REAL local peer SO_LINGER={1,0}+abort reset, observed error="
                      << errors.first().at(0).toString() << "recovered actual peer RX="
                      << peer.receivedOnCurrent.toHex(' ');
}

void TcpFaultTest::realNonReadingPeerBackpressureAndRecovery()
{
    LoopbackPeer peer(false);
    QVERIFY(peer.listen());
    FaultTransport transport(100, 65536);
    QSignalSpy errors(&transport, &ReadOnlyTcpTransport::errorOccurred);
    QSignalSpy disconnected(&transport, &ReadOnlyTcpTransport::disconnected);
    QString error;
    QVERIFY2(openConnection(&transport, &peer, &error), qPrintable(error));
    QCOMPARE(peer.receiveBufferResult, 0);
    transport.lastSocket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, 1024);
    qint64 maximumPending = 0;
    int accepted = 0;
    int rejected = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer producer;
    producer.setInterval(1);
    connect(&producer, &QTimer::timeout, this, [&]()
    {
        if(!transport.isConnected() || accepted >= 100000)
        {
            producer.stop();
            return;
        }
        for(int index = 0; index < 100; ++index)
        {
            if(!transport.queueQuery(ReadOnlyQuery::Status, &error))
            {
                ++rejected;
                break;
            }
            ++accepted;
            maximumPending = qMax(maximumPending, transport.pendingWriteBytes());
        }
    });
    producer.start();
    const bool timedOut = spinUntil([&]() { return disconnected.count() > 0; }, 3000);
    producer.stop();
    qInfo() << "REAL non-reading loopback peer SO_RCVBUF=1024 Qt_readBuffer=1 sendBuffer=1024"
            << "accepted_queries=" << accepted << "backpressure_rejections=" << rejected
            << "max_pending_bytes=" << maximumPending << "terminated=" << timedOut
            << "elapsed_ms=" << elapsed.elapsed();
    if(!timedOut)
    {
        transport.close();
        QSKIP("Bounded real non-reader attempt did not force OS write stall on this host; not claimed as covered");
    }
    QVERIFY(errorContains(errors, "transport.write_timeout"));
    QCOMPARE(errors.count(), 1);
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(maximumPending <= 65536);
    QVERIFY(accepted > 0);
    QVERIFY(rejected > 0);
    // This scenario did not inject write returns: all virtual calls delegated
    // their complete input to Qt. The stalled bytesWritten progress is real.
    QCOMPARE(transport.traces.first()->requested, transport.traces.first()->returned);
    QCOMPARE(transport.pendingWriteBytes(), qint64(0));
    LoopbackPeer recovery;
    QVERIFY(recovery.listen());
    QVERIFY2(recoverWithGoldenStatus(&transport, &recovery, &error), qPrintable(error));
    QCOMPARE(recovery.receivedOnCurrent, kStatusQuery);
    qInfo().noquote() << "REAL non-reader write_timeout recovery actual peer RX="
                      << recovery.receivedOnCurrent.toHex(' ');
}

QTEST_GUILESS_MAIN(TcpFaultTest)
#include "tcp_fault_test.moc"
