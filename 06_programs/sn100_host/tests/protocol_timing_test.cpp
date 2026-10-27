#include "read_only_client.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <array>

using namespace Sn100Protocol;

namespace
{
// 独立合成金字节，未由生产encoder生成，也不是仪器抓包。
const QByteArray kQuery = QByteArray::fromHex("0203000600000001516b3b0405");
const QByteArray kReply = QByteArray::fromHex("0607000900000001516b5b305d0d0809");
constexpr int kTimeoutMs = 120;
constexpr int kPostmortemMs = 250;

struct TimingEntry
{
    QString stage;
    qint64 collectorNs = 0, sourceNs = -1;
    quint64 generation = 0, revision = 0;
    qint64 value = 0, extra = 0, applicationPending = -1, socketPending = -1;
    QByteArray bytes;
};

class TimingCapture
{
public:
    TimingCapture() { clock.start(); }

    TimingEntry *add(const QString &stage)
    {
        if(size == entries.size()) { overflow = true; return nullptr; }
        TimingEntry &entry = entries[size++];
        entry.stage = stage;
        entry.collectorNs = clock.nsecsElapsed();
        return &entry;
    }

    void wire(const QString &stage, const QByteArray &bytes)
    {
        if(TimingEntry *entry = add(stage)) entry->bytes = bytes;
    }

    QJsonArray json() const
    {
        QJsonArray result;
        for(size_t index = 0; index < size; ++index)
        {
            const TimingEntry &entry = entries[index];
            QJsonObject value{{"stage", entry.stage}, {"collector_ns", double(entry.collectorNs)},
                {"source_elapsed_ns", double(entry.sourceNs)}, {"generation", double(entry.generation)},
                {"revision", double(entry.revision)}, {"value", double(entry.value)}, {"extra", double(entry.extra)},
                {"application_pending", double(entry.applicationPending)}, {"socket_pending", double(entry.socketPending)}};
            if(!entry.bytes.isEmpty()) value.insert("hex", QString::fromLatin1(entry.bytes.toHex(' ')));
            result.append(value);
        }
        return result;
    }

    int count(const QString &stage) const
    {
        int total = 0;
        for(size_t index = 0; index < size; ++index) if(entries[index].stage == stage) ++total;
        return total;
    }

    QElapsedTimer clock;
    std::array<TimingEntry, 2048> entries;
    size_t size = 0;
    bool overflow = false;
};

struct ScenarioResult
{
    QJsonObject evidence;
    QString error;
    bool success = false, pendingAfterClose = true, overflow = false;
    int attempts = 0, finished = 0, observed = 0, transportEvents = 0, clientEvents = 0;
    QByteArray peerRead;
};

ScenarioResult runScenario(int stallMs, bool enableTiming)
{
    TimingCapture capture;
    ScenarioResult result;
    QEventLoop loop;
    ReadOnlyClient client(nullptr, kTimeoutMs, 1);
    ReadOnlyTcpTransport *transport = client.findChild<ReadOnlyTcpTransport *>();
    if(!transport) { result.error = "client transport child missing"; return result; }
    client.setTimingEnabled(enableTiming);
    transport->setTimingEnabled(enableTiming);
    QTcpServer server;
    server.setProxy(QNetworkProxy::NoProxy);
    QPointer<QTcpSocket> peer;
    bool clientConnected = false, requestStarted = false, guardExpired = false;
    int peerFrames = 0;
    QByteArray queryAssembly;
    QString terminalDetail;
    auto fail = [&](const QString &error)
    {
        if(result.error.isEmpty()) result.error = error;
        loop.quit();
    };
    QObject::connect(transport, &ReadOnlyTcpTransport::timingEvent, &loop,
            [&](QString stage, qint64 sourceNs, quint64 generation, qint64 value, qint64 extra,
                qint64 applicationPending, qint64 socketPending)
    {
        ++result.transportEvents;
        if(TimingEntry *entry = capture.add("transport." + stage))
        {
            entry->sourceNs = sourceNs; entry->generation = generation;
            entry->value = value; entry->extra = extra;
            entry->applicationPending = applicationPending; entry->socketPending = socketPending;
        }
    });
    QObject::connect(&client, &ReadOnlyClient::timingEvent, &loop,
            [&](QString stage, qint64 sourceNs, quint64 generation, quint64 revision, int attempt, qint64 deadlineMs)
    {
        ++result.clientEvents;
        if(TimingEntry *entry = capture.add("client." + stage))
        {
            entry->sourceNs = sourceNs; entry->generation = generation; entry->revision = revision;
            entry->value = attempt; entry->extra = deadlineMs;
        }
    });
    QObject::connect(transport, &ReadOnlyTcpTransport::wireQueued, &loop,
                     [&](QByteArray bytes, quint64) { capture.wire("client.wire.queued", bytes); });
    QObject::connect(transport, &ReadOnlyTcpTransport::wireReceived, &loop,
                     [&](QByteArray bytes, quint64) { capture.wire("client.wire.read", bytes); });
    QObject::connect(&client, &ReadOnlyClient::queryQueued, &loop,
            [&](ReadOnlyQuery query, int attempt, quint64 generation)
    {
        ++result.attempts;
        if(TimingEntry *entry = capture.add("client.query.queued"))
        { entry->value = attempt; entry->generation = generation; }
        if(query != ReadOnlyQuery::Alarm || attempt != result.attempts || attempt > 2)
        { fail("unexpected query/attempt"); return; }
        if(attempt == 2 && stallMs)
        {
            capture.add("driver.injected_stall.begin");
            // 只在测试驱动故意饿死同线程事件循环，复现排队与处理的区别。
            QTest::qSleep(stallMs);
            capture.add("driver.injected_stall.end");
        }
    });
    QObject::connect(&client, &ReadOnlyClient::messageObserved, &loop,
            [&](ReadOnlyMessage message, quint64)
    {
        ++result.observed;
        capture.wire("client.observed", message.command);
        if(message.command != "Qk" || message.data.value("declared_count").toInt(-1) != 0)
            fail("unexpected observation");
    });
    QObject::connect(&client, &ReadOnlyClient::notice, &loop,
            [&](QString detail, quint64) { capture.wire("client.notice", detail.toUtf8()); });
    QObject::connect(&client, &ReadOnlyClient::requestFinished, &loop,
            [&](ReadOnlyQuery query, bool success, QString detail, quint64)
    {
        ++result.finished;
        result.success = success;
        terminalDetail = detail;
        capture.add(success ? "driver.terminal.success" : "driver.terminal.failure");
        if(query != ReadOnlyQuery::Alarm || result.finished != 1)
        { fail("unexpected/multiple request terminal"); return; }
        // 截止点结论冻结；仅继续处理已有I/O取证，不发新任务、不改失败为成功。
        QTimer::singleShot(kPostmortemMs, &loop, [&]() { loop.quit(); });
    });
    auto startRequest = [&]()
    {
        if(requestStarted || !clientConnected || !peer) return;
        requestStarted = true;
        capture.add("driver.request.begin");
        QString error;
        if(!client.request(ReadOnlyQuery::Alarm, &error)) fail(error);
    };
    QObject::connect(&client, &ReadOnlyClient::connected, &loop, [&](quint64)
    { clientConnected = true; startRequest(); });
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&]()
    {
        peer = server.nextPendingConnection();
        if(!peer) { fail("missing owned peer"); return; }
        peer->setProxy(QNetworkProxy::NoProxy);
        QObject::connect(peer, &QTcpSocket::bytesWritten, &loop, [&](qint64 bytes)
        { if(TimingEntry *entry = capture.add("peer.bytesWritten")) entry->value = bytes; });
        QObject::connect(peer, &QTcpSocket::readyRead, &loop, [&]()
        {
            if(TimingEntry *entry = capture.add("peer.readyRead.enter")) entry->value = peer->bytesAvailable();
            const QByteArray bytes = peer->readAll();
            capture.wire("peer.read.actual", bytes);
            result.peerRead += bytes;
            queryAssembly += bytes;
            while(queryAssembly.size() >= kQuery.size())
            {
                if(queryAssembly.left(kQuery.size()) != kQuery) { fail("query golden bytes mismatch"); return; }
                queryAssembly.remove(0, kQuery.size());
                if(++peerFrames == 1) { capture.add("peer.first_reply.deliberately_withheld"); continue; }
                if(peerFrames > 2) { fail("more than one retry"); return; }
                capture.wire("peer.reply.literal", kReply);
                const qint64 written = peer->write(kReply);
                if(TimingEntry *entry = capture.add("peer.write.returned")) entry->value = written;
                if(written != kReply.size()) { fail("peer write rejected"); return; }
            }
        });
        startRequest();
    });
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, [&]()
    { guardExpired = true; fail("scenario guard expired"); });
    QString connectError;
    if(!server.listen(QHostAddress::LocalHost, 0) || !client.connectLoopback(server.serverPort(), &connectError))
    { result.error = "owned loopback failed: " + connectError; return result; }
    guard.start(3000);
    loop.exec();
    guard.stop();
    capture.add("driver.postmortem.end");
    client.close();
    if(peer) peer->abort();
    result.pendingAfterClose = client.requestPending();
    result.overflow = capture.overflow;
    result.evidence = {{"schema", "sn100-o05-controlled-timing-v1"}, {"qt_version", qVersion()},
        {"timeout_ms", kTimeoutMs}, {"max_retries", 1}, {"injected_stall_ms", stallMs},
        {"injection_point", "attempt2 queryQueued direct receiver; async pump is scheduled but receiver has not returned"},
        {"postmortem_ms", kPostmortemMs}, {"timing_enabled", enableTiming}, {"capture_capacity", 2048},
        {"capture_overflow", capture.overflow}, {"guard_expired", guardExpired},
        {"collector_clock", "one QElapsedTimer for every event in this case, nanoseconds since capture construction"},
        {"source_clock", "each source_elapsed_ns is its client/transport QElapsedTimer; origins differ; do not subtract between sources"},
        {"client_timing_fields", "value=attempt, extra=deadline_ms on client clock"},
        {"transport_timing_fields", "write.begin value=requested extra=frame remaining; write.returned value=accepted extra=requested; bytesWritten value=progress"},
        {"evidence_limit", "synthetic loopback mechanism only; queued/write/bytesWritten are not peer ACK or instrument evidence; cannot attribute the old1150.178s failure"},
        {"attempts", result.attempts}, {"finished", result.finished}, {"request_success", result.success},
        {"terminal_detail", terminalDetail}, {"observations", result.observed}, {"peer_frames", peerFrames},
        {"peer_rx_hex", QString::fromLatin1(result.peerRead.toHex(' '))},
        {"query_hex", QString::fromLatin1(kQuery.toHex(' '))}, {"reply_hex", QString::fromLatin1(kReply.toHex(' '))},
        {"pending_after_close", result.pendingAfterClose}, {"transport_events", result.transportEvents},
        {"client_events", result.clientEvents}, {"error", result.error}, {"events", capture.json()}};
    return result;
}

bool saveEvidence(const QString &path, const QJsonObject &evidence)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(evidence).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

class ProtocolTimingTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_output = qEnvironmentVariable("SN100_PROTOCOL_TIMING_OUTPUT");
        QVERIFY2(QDir::isAbsolutePath(m_output) && QDir(m_output).exists(),
                 "Set SN100_PROTOCOL_TIMING_OUTPUT to an existing isolated absolute output directory");
    }

    void controlledRetry_data()
    {
        QTest::addColumn<int>("stallMs");
        QTest::addColumn<bool>("timingEnabled");
        QTest::addColumn<bool>("expectedSuccess");
        QTest::newRow("baseline") << 0 << true << true;
        QTest::newRow("short40") << 40 << true << true;
        QTest::newRow("over240") << 240 << true << false;
        QTest::newRow("default_off") << 0 << false << true;
    }

    void controlledRetry()
    {
        QFETCH(int, stallMs);
        QFETCH(bool, timingEnabled);
        QFETCH(bool, expectedSuccess);
        const ScenarioResult result = runScenario(stallMs, timingEnabled);
        const QString path = m_output + "/" + QString::fromLatin1(QTest::currentDataTag()) + ".json";
        QVERIFY2(saveEvidence(path, result.evidence), qPrintable(path));
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.overflow);
        QCOMPARE(result.attempts, 2);
        QCOMPARE(result.finished, 1);
        QCOMPARE(result.success, expectedSuccess);
        QCOMPARE(result.observed, 1);
        QCOMPARE(result.peerRead, kQuery + kQuery);
        QVERIFY(!result.pendingAfterClose);
        if(timingEnabled)
        { QVERIFY(result.transportEvents > 0); QVERIFY(result.clientEvents > 0); }
        else
        { QCOMPARE(result.transportEvents, 0); QCOMPARE(result.clientEvents, 0); }
        qInfo().noquote() << "O05 timing" << QTest::currentDataTag() << "timeout_ms=120 retries=1"
                         << "attempts=" << result.attempts << "success=" << result.success
                         << "observations=" << result.observed << "peer_RX=" << result.peerRead.toHex(' ')
                         << "evidence=" << path;
    }
private:
    QString m_output;
};

QTEST_GUILESS_MAIN(ProtocolTimingTest)
#include "protocol_timing_test.moc"
