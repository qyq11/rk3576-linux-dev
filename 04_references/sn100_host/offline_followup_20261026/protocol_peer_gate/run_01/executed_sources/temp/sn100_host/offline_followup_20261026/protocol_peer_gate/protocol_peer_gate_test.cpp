#include "read_only_client.h"

#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <array>
#include <functional>

using namespace Sn100Protocol;

namespace
{
// Independent synthetic literals, not an instrument capture or encoder output.
const QByteArray kQuery = QByteArray::fromHex("0203000600000001516b3b0405");
const QByteArray kReply = QByteArray::fromHex("0607000900000001516b5b305d0d0809");
constexpr int kTimeoutMs = 120;
constexpr int kGuardMs = 3000;
constexpr int kPostmortemMs = 250;

enum class Gate { Baseline, HoldRead, HoldReply };

struct Entry
{
    QString stage;
    qint64 collectorNs = 0, sourceNs = -1;
    quint64 generation = 0, revision = 0;
    qint64 value = 0, extra = 0, applicationPending = -1, socketPending = -1;
    QByteArray bytes;
};

class Capture
{
public:
    Capture() { clock.start(); }

    Entry *add(const QString &stage, qint64 value = 0, qint64 extra = 0)
    {
        if(size == entries.size()) { overflow = true; return nullptr; }
        Entry &entry = entries[size++];
        entry.stage = stage;
        entry.collectorNs = clock.nsecsElapsed();
        entry.value = value;
        entry.extra = extra;
        return &entry;
    }

    void wire(const QString &stage, const QByteArray &bytes)
    {
        if(Entry *entry = add(stage, bytes.size())) entry->bytes = bytes;
    }

    int count(const QString &stage) const
    {
        int result = 0;
        for(size_t index = 0; index < size; ++index) if(entries[index].stage == stage) ++result;
        return result;
    }

    qint64 at(const QString &stage, int ordinal = 1) const
    {
        for(size_t index = 0; index < size; ++index)
            if(entries[index].stage == stage && --ordinal == 0) return entries[index].collectorNs;
        return -1;
    }

    bool before(const QString &first, const QString &second, int firstOrdinal = 1,
                int secondOrdinal = 1) const
    {
        const qint64 left = at(first, firstOrdinal), right = at(second, secondOrdinal);
        return left >= 0 && right >= left;
    }

    QJsonArray json() const
    {
        QJsonArray result;
        for(size_t index = 0; index < size; ++index)
        {
            const Entry &entry = entries[index];
            QJsonObject row{{"seq", int(index + 1)}, {"stage", entry.stage},
                {"collector_ns", double(entry.collectorNs)}, {"source_elapsed_ns", double(entry.sourceNs)},
                {"generation", double(entry.generation)}, {"revision", double(entry.revision)},
                {"value", double(entry.value)}, {"extra", double(entry.extra)},
                {"application_pending", double(entry.applicationPending)},
                {"socket_pending", double(entry.socketPending)}};
            if(!entry.bytes.isEmpty()) row.insert("hex", QString::fromLatin1(entry.bytes.toHex(' ')));
            result.append(row);
        }
        return result;
    }

    QElapsedTimer clock;
    std::array<Entry, 2048> entries;
    size_t size = 0;
    bool overflow = false;
};

struct Result { QJsonObject evidence, checks; QString error; };

Result runScenario(Gate gate, const QString &name)
{
    Capture capture;
    Result result;
    QEventLoop loop;
    ReadOnlyClient client(nullptr, kTimeoutMs, 1);
    ReadOnlyTcpTransport *transport = client.findChild<ReadOnlyTcpTransport *>();
    if(!transport)
    {
        result.error = "actual client transport child missing";
        result.evidence = {{"error", result.error}};
        return result;
    }
    client.setTimingEnabled(true);
    transport->setTimingEnabled(true);
    QTcpServer server;
    server.setProxy(QNetworkProxy::NoProxy);
    QPointer<QTcpSocket> peer, clientSocket;
    FrameDecoder peerDecoder(Direction::HostToDevice);
    QTimer guard, postmortem;
    guard.setSingleShot(true); guard.setTimerType(Qt::PreciseTimer);
    postmortem.setSingleShot(true); postmortem.setTimerType(Qt::PreciseTimer);
    bool connected = false, started = false, guardExpired = false, postmortemComplete = false;
    bool latched = false, success = false, readHeld = false, replyHeld = false, released = false, cleaning = false;
    bool pendingAtTerminal = true;
    int attempts = 0, finished = 0, observations = 0, frames = 0, replies = 0;
    int framesAtTerminal = -1, observationsAtTerminal = -1, repliesAtTerminal = -1;
    qint64 availableAtTerminal = -1, acceptedBytes = 0, deliveredBytes = 0, peerWrittenBytes = 0;
    QByteArray queuedWire, peerRead, clientRead;
    QString terminalDetail;
    auto fail = [&](const QString &error)
    {
        if(result.error.isEmpty()) result.error = error;
        capture.wire("driver.error", error.toUtf8());
        loop.quit();
    };
    QObject::connect(transport, &ReadOnlyTcpTransport::timingEvent, &loop,
            [&](QString stage, qint64 sourceNs, quint64 generation, qint64 value, qint64 extra,
                qint64 applicationPending, qint64 socketPending)
    {
        if(Entry *entry = capture.add("transport." + stage, value, extra))
        {
            entry->sourceNs = sourceNs; entry->generation = generation;
            entry->applicationPending = applicationPending; entry->socketPending = socketPending;
        }
        if(stage == "write.returned")
        {
            if(value < 0 || value > extra) fail("unexpected real socket write result");
            else acceptedBytes += value;
        }
    });
    QObject::connect(&client, &ReadOnlyClient::timingEvent, &loop,
            [&](QString stage, qint64 sourceNs, quint64 generation, quint64 revision,
                int attempt, qint64 deadlineMs)
    {
        if(Entry *entry = capture.add("client." + stage, attempt, deadlineMs))
        { entry->sourceNs = sourceNs; entry->generation = generation; entry->revision = revision; }
    });
    QObject::connect(transport, &ReadOnlyTcpTransport::wireQueued, &loop,
            [&](QByteArray bytes, quint64)
    {
        capture.wire("client.wire.queued", bytes);
        queuedWire += bytes;
        if(bytes != kQuery) fail("queued bytes differ from independent Qk literal");
    });
    QObject::connect(transport, &ReadOnlyTcpTransport::wireReceived, &loop,
            [&](QByteArray bytes, quint64)
    { capture.wire("client.wire.read", bytes); clientRead += bytes; });
    QObject::connect(transport, &ReadOnlyTcpTransport::bytesDelivered, &loop,
            [&](qint64 bytes, quint64)
    { capture.add("client.socket.progress.not_peer_ack", bytes); deliveredBytes += bytes; });
    QObject::connect(transport, &ReadOnlyTcpTransport::errorOccurred, &loop,
            [&](QString detail, quint64)
    { capture.wire("transport.error.actual", detail.toUtf8()); if(!cleaning) fail(detail); });
    QObject::connect(&client, &ReadOnlyClient::queryQueued, &loop,
            [&](ReadOnlyQuery query, int attempt, quint64 generation)
    {
        ++attempts;
        if(Entry *entry = capture.add("client.query.queued", attempt)) entry->generation = generation;
        if(query != ReadOnlyQuery::Alarm || attempt != attempts || attempt > 2 || latched)
            fail("unexpected query, retry count or query after terminal latch");
    });
    QObject::connect(&client, &ReadOnlyClient::notice, &loop,
            [&](QString detail, quint64) { capture.wire("client.notice", detail.toUtf8()); });
    QObject::connect(&client, &ReadOnlyClient::messageObserved, &loop,
            [&](ReadOnlyMessage message, quint64)
    {
        ++observations;
        capture.wire("client.observed", message.command);
        if(message.command != "Qk" || message.data.value("declared_count").toInt(-1) != 0)
            fail("unexpected parsed observation");
    });
    auto sendReply = [&]()
    {
        if(!peer || replies != 0) { fail("missing peer or duplicate synthetic reply"); return; }
        ++replies;
        capture.wire("peer.reply.write.begin", kReply);
        const qint64 written = peer->write(kReply);
        capture.add("peer.reply.write.returned", written, kReply.size());
        if(written != kReply.size())
        {
            capture.wire("peer.reply.write.error.actual", peer->errorString().toUtf8());
            fail("synthetic peer write not fully accepted by Qt");
        }
    };
    std::function<void()> readPeer;
    readPeer = [&]()
    {
        if(!peer) { fail("owned peer vanished before read"); return; }
        if(gate == Gate::HoldRead && frames == 1 && !released)
        {
            readHeld = true;
            // Deliberately leave bytes in Qt's peer receive buffer. No sleep or blocked event loop.
            capture.add("peer.read.gate.held", peer->bytesAvailable());
            return;
        }
        const QByteArray bytes = peer->readAll();
        if(bytes.isEmpty()) { fail("peer read had no bytes"); return; }
        capture.wire("peer.read.actual", bytes);
        peerRead += bytes;
        if(!(kQuery + kQuery).startsWith(peerRead))
        { fail("peer stream differs from two independent query literals"); return; }
        const DecodeBatch batch = peerDecoder.feed(bytes, capture.clock.elapsed());
        if(!batch.errors.isEmpty() || batch.duplicatePackets != 0)
        { fail("actual peer decoder rejected the query stream"); return; }
        for(const QByteArray &message : batch.messages)
        {
            ++frames;
            capture.wire("peer.frame.decoded", message);
            if(message != "Qk" || frames > 2) { fail("unexpected decoded query/frame count"); return; }
            if(frames == 1) { capture.add("peer.first_reply.withheld"); continue; }
            if(gate == Gate::HoldReply && !released)
            {
                replyHeld = true;
                capture.add("peer.reply.gate.held");
            }
            else sendReply();
        }
    };
    QObject::connect(&client, &ReadOnlyClient::requestFinished, &loop,
            [&](ReadOnlyQuery query, bool terminalSuccess, QString detail, quint64)
    {
        ++finished;
        if(latched || query != ReadOnlyQuery::Alarm)
        { fail("unexpected or repeated request terminal; first result remains latched"); return; }
        latched = true;
        success = terminalSuccess;
        terminalDetail = detail;
        pendingAtTerminal = client.requestPending();
        availableAtTerminal = peer ? peer->bytesAvailable() : -1;
        framesAtTerminal = frames; observationsAtTerminal = observations; repliesAtTerminal = replies;
        capture.add("driver.verdict.latched", terminalSuccess ? 1 : 0);
        capture.add("driver.terminal.peer_buffered", availableAtTerminal, frames);
        capture.add("driver.postmortem.begin");
        postmortem.start(kPostmortemMs);
        if(gate != Gate::Baseline)
        {
            // Release only after the real immutable timeout terminal. Never turn failure into success.
            QTimer::singleShot(0, &loop, [&]()
            {
                released = true;
                capture.add("peer.gate.release", peer ? peer->bytesAvailable() : -1, frames);
                if(gate == Gate::HoldRead) readPeer();
                else if(replyHeld) sendReply();
                else fail("reply gate was not reached before terminal");
            });
        }
    });
    auto startRequest = [&]()
    {
        if(started || !connected || !peer) return;
        started = true;
        capture.add("driver.request.begin");
        QString error;
        if(!client.request(ReadOnlyQuery::Alarm, &error)) fail("actual request rejected: " + error);
    };
    QObject::connect(&client, &ReadOnlyClient::connected, &loop, [&](quint64)
    {
        connected = true;
        capture.add("client.connected");
        clientSocket = transport->findChild<QTcpSocket *>();
        if(!clientSocket) { fail("actual client QTcpSocket missing"); return; }
        capture.add("client.socket.state.actual", int(clientSocket->state()));
        QObject::connect(clientSocket, &QTcpSocket::stateChanged, &loop,
                [&](QAbstractSocket::SocketState state) { capture.add("client.socket.state.actual", int(state)); });
        startRequest();
    });
    QObject::connect(&client, &ReadOnlyClient::disconnected, &loop, [&](quint64)
    { capture.add("client.disconnected.actual"); if(!cleaning) fail("client disconnected before cleanup"); });
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&]()
    {
        if(peer) { fail("extra peer connection"); return; }
        peer = server.nextPendingConnection();
        if(!peer) { fail("missing owned peer"); return; }
        peer->setProxy(QNetworkProxy::NoProxy);
        peer->setReadBufferSize(65536);
        capture.add("peer.accepted");
        capture.add("peer.socket.state.actual", int(peer->state()));
        QObject::connect(peer, &QTcpSocket::stateChanged, &loop,
                [&](QAbstractSocket::SocketState state) { capture.add("peer.socket.state.actual", int(state)); });
        QObject::connect(peer, &QTcpSocket::readyRead, &loop, [&]()
        { capture.add("peer.readyRead.qt_buffered", peer->bytesAvailable()); readPeer(); });
        QObject::connect(peer, &QTcpSocket::bytesWritten, &loop, [&](qint64 bytes)
        { capture.add("peer.bytesWritten.not_client_ack", bytes); peerWrittenBytes += bytes; });
        startRequest();
    });
    QObject::connect(&guard, &QTimer::timeout, &loop, [&]()
    { guardExpired = true; fail("three-second case guard expired"); });
    QObject::connect(&postmortem, &QTimer::timeout, &loop, [&]()
    { postmortemComplete = true; capture.add("driver.postmortem.end"); loop.quit(); });
    guard.start(kGuardMs);
    QString connectError;
    if(!server.listen(QHostAddress::LocalHost, 0)) fail("owned loopback server listen failed");
    else if(!client.connectLoopback(server.serverPort(), &connectError)) fail(connectError);
    if(result.error.isEmpty()) loop.exec();
    guard.stop(); postmortem.stop();
    cleaning = true;
    capture.add("driver.cleanup.begin");
    capture.add("driver.client.close.begin");
    client.close();
    capture.add("driver.client.close.end", client.requestPending() ? 1 : 0, transport->pendingWriteBytes());
    capture.add("driver.peer.abort.begin");
    if(peer) peer->abort();
    capture.add("driver.peer.abort.end", peer ? int(peer->state()) : -1);
    server.close();
    capture.add("driver.cleanup.end");

    const bool expectedFailure = gate != Gate::Baseline;
    const qint64 postmortemNs = capture.at("driver.postmortem.end") - capture.at("driver.postmortem.begin");
    result.checks = {{"no_harness_error", result.error.isEmpty()}, {"no_capture_overflow", !capture.overflow},
        {"case_guard_not_expired", !guardExpired}, {"single_logical_request", capture.count("driver.request.begin") == 1},
        {"one_retry_only", attempts == 2 && capture.count("client.deadline.retry") == 1},
        {"single_immutable_terminal", finished == 1 && latched}, {"expected_terminal", success == !expectedFailure},
        {"failure_is_real_timeout", !expectedFailure || terminalDetail.startsWith("client.timeout:")},
        {"nothing_pending_at_terminal", !pendingAtTerminal},
        {"two_decoded_query_frames", frames == 2 && peerDecoder.bufferedBytes() == 0 && peerDecoder.assemblyBytes() == 0},
        {"query_stream_exact", queuedWire == kQuery + kQuery && peerRead == kQuery + kQuery},
        {"query_local_write_progress_exact", acceptedBytes == 26 && deliveredBytes == 26},
        {"reply_stream_exact", replies == 1 && clientRead == kReply && peerWrittenBytes == kReply.size()},
        {"one_valid_observation", observations == 1},
        {"terminal_snapshot_expected", framesAtTerminal == (gate == Gate::HoldRead ? 1 : 2)
            && observationsAtTerminal == (expectedFailure ? 0 : 1)
            && repliesAtTerminal == (expectedFailure ? 0 : 1)},
        {"full_postmortem_observation", postmortemComplete && postmortemNs >= qint64(kPostmortemMs) * 1000000},
        {"cleanup_after_postmortem", capture.before("driver.postmortem.end", "driver.cleanup.begin")},
        {"local_close_completed", !client.requestPending() && !transport->isConnected()
            && transport->pendingWriteBytes() == 0 && clientSocket && clientSocket->state() == QAbstractSocket::UnconnectedState
            && peer && peer->state() == QAbstractSocket::UnconnectedState},
        {"first_reply_always_withheld", capture.count("peer.first_reply.withheld") == 1},
        {"terminal_has_one_production_trace", capture.count(expectedFailure ? "client.request.failure" : "client.request.success") == 1
            && capture.count(expectedFailure ? "client.request.success" : "client.request.failure") == 0}};
    if(expectedFailure)
    {
        result.checks.insert("release_after_original_failure", released
            && capture.before("driver.verdict.latched", "peer.gate.release"));
        result.checks.insert("reply_and_observation_after_failure", capture.before("peer.gate.release", "peer.reply.write.begin")
            && capture.before("peer.reply.write.begin", "client.observed"));
        result.checks.insert("late_observation_keeps_failure", !success && finished == 1 && observations == 1);
    }
    else result.checks.insert("baseline_has_no_gate", !readHeld && !replyHeld && !released);
    if(gate == Gate::HoldRead)
    {
        result.checks.insert("read_gate_proves_qt_buffer_not_consumed", readHeld && !replyHeld
            && availableAtTerminal == 13 && capture.before("peer.read.gate.held", "driver.verdict.latched")
            && capture.before("peer.gate.release", "peer.frame.decoded", 1, 2));
    }
    if(gate == Gate::HoldReply)
    {
        result.checks.insert("reply_gate_proves_consumption_before_failure", replyHeld && !readHeld
            && availableAtTerminal == 0 && capture.before("peer.frame.decoded", "driver.verdict.latched", 2)
            && capture.before("peer.reply.gate.held", "driver.verdict.latched"));
    }
    result.evidence = {{"schema", "sn100-protocol-peer-gate-case-v1"}, {"case", name}, {"qt_version", qVersion()},
        {"timeout_ms", kTimeoutMs}, {"max_retries", 1}, {"case_guard_ms", kGuardMs},
        {"postmortem_ms", kPostmortemMs}, {"postmortem_observed_ns", double(postmortemNs)},
        {"expected_failure", expectedFailure}, {"request_success", success}, {"terminal_detail", terminalDetail},
        {"attempts", attempts}, {"finished", finished}, {"observations", observations}, {"peer_frames", frames},
        {"transport_accepted_bytes", double(acceptedBytes)}, {"transport_bytesWritten_progress", double(deliveredBytes)},
        {"peer_reply_bytesWritten_progress", double(peerWrittenBytes)}, {"reply_count", replies},
        {"terminal_snapshot", QJsonObject{{"peer_qt_buffered_bytes", double(availableAtTerminal)},
            {"peer_decoded_frames", framesAtTerminal}, {"observations", observationsAtTerminal},
            {"replies_written", repliesAtTerminal}, {"client_pending", pendingAtTerminal}}},
        {"query_hex", QString::fromLatin1(kQuery.toHex(' '))}, {"reply_hex", QString::fromLatin1(kReply.toHex(' '))},
        {"peer_rx_hex", QString::fromLatin1(peerRead.toHex(' '))}, {"client_rx_hex", QString::fromLatin1(clientRead.toHex(' '))},
        {"capture_capacity", 2048}, {"capture_overflow", capture.overflow}, {"guard_expired", guardExpired},
        {"cleanup_snapshot", QJsonObject{{"transport_connected", transport->isConnected()},
            {"transport_pending_bytes", double(transport->pendingWriteBytes())},
            {"client_pending", client.requestPending()},
            {"client_socket_state", clientSocket ? int(clientSocket->state()) : -1},
            {"peer_socket_state", peer ? int(peer->state()) : -1},
            {"socket_state_zero_means", "QAbstractSocket::UnconnectedState; deleteLater need not yet have destroyed the QObject"}}},
        {"error", result.error}, {"checks", result.checks}, {"events", capture.json()},
        {"collector_clock", "one per-case QElapsedTimer; collector_ns alone orders all sources"},
        {"source_clocks", "source_elapsed_ns/deadline_ms are local client/transport clocks; do not subtract across sources"},
        {"timing_fields", "client value=attempt extra=deadline_ms; transport fields retain the real timing signal values"},
        {"wire_semantics", "Qt write acceptance, bytesWritten progress, peer Qt bytesAvailable, application read and parsed observation are distinct; none is TCP ACK"},
        {"limit", "synthetic single-request loopback mechanism; negative arms retain real request failure; does not establish or fix the old1150.178s cause"}};
    QObject::disconnect(&client, nullptr, &loop, nullptr);
    QObject::disconnect(transport, nullptr, &loop, nullptr);
    if(clientSocket) QObject::disconnect(clientSocket, nullptr, &loop, nullptr);
    if(peer) QObject::disconnect(peer, nullptr, &loop, nullptr);
    return result;
}

bool saveEvidence(const QString &path, const QJsonObject &object)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(object).toJson();
    return !QFileInfo::exists(path) && file.open(QIODevice::WriteOnly)
            && file.write(bytes) == bytes.size() && file.commit();
}
}

class ProtocolPeerGateTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_output = qEnvironmentVariable("SN100_PROTOCOL_PEER_GATE_OUTPUT");
        QVERIFY2(QDir::isAbsolutePath(m_output) && QDir(m_output).exists()
                 && QDir(m_output).entryList(QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty(),
                 "Set SN100_PROTOCOL_PEER_GATE_OUTPUT to a new existing empty isolated absolute directory");
    }
    void peerGates_data()
    {
        QTest::addColumn<int>("gate");
        QTest::newRow("baseline") << int(Gate::Baseline);
        QTest::newRow("peer_read_gate") << int(Gate::HoldRead);
        QTest::newRow("peer_reply_gate") << int(Gate::HoldReply);
    }
    void peerGates()
    {
        QFETCH(int, gate);
        const QString name = QString::fromLatin1(QTest::currentDataTag());
        const Result result = runScenario(Gate(gate), name);
        QVERIFY2(saveEvidence(m_output + '/' + name + ".json", result.evidence), "case raw evidence save failed");
        QVERIFY2(!result.checks.isEmpty(), qPrintable(result.error));
        for(auto check = result.checks.begin(); check != result.checks.end(); ++check)
            QVERIFY2(check.value().isBool() && check.value().toBool(), qPrintable(check.key() + ": " + result.error));
    }
private:
    QString m_output;
};

QTEST_GUILESS_MAIN(ProtocolPeerGateTest)
#include "protocol_peer_gate_test.moc"
