#include "tcp_transport.h"
#include "read_only_messages.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <QDir>
#include <algorithm>

using namespace Sn100Protocol;

namespace
{
double percentile(QList<double> values, int percent)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    return values.at(qMin(values.size() - 1, (values.size() * percent + 99) / 100 - 1));
}

class LoopbackDemo : public QObject
{
public:
    LoopbackDemo(QString directory, int stressCount, int durationMs)
        : m_directory(directory), m_trace(directory + QStringLiteral("/wire.jsonl")),
          m_stressTarget(stressCount), m_durationMs(durationMs)
    {
        m_server.setProxy(QNetworkProxy::NoProxy);
        m_heartbeat.setTimerType(Qt::PreciseTimer);
        m_heartbeat.setInterval(10);
        m_stressTimer.setInterval(10);
        connect(&m_heartbeat, &QTimer::timeout, this, [this]()
        {
            const qint64 now = m_clock.nsecsElapsed();
            m_heartbeatGaps.append(double(now - m_previousHeartbeatNs) / 1000000.0);
            m_previousHeartbeatNs = now;
        });
        connect(&m_server, &QTcpServer::newConnection, this, [this]() { acceptPeer(); });
        connect(&m_transport, &ReadOnlyTcpTransport::connected, this, [this](quint64 generation)
        {
            log(QStringLiteral("client.connected"), {}, generation);
            for (ReadOnlyQuery query : {ReadOnlyQuery::Status, ReadOnlyQuery::Alarm, ReadOnlyQuery::Consumables})
            {
                QString error;
                if (!m_transport.queueQuery(query, &error)) { fail(error); return; }
            }
        });
        connect(&m_transport, &ReadOnlyTcpTransport::wireQueued, this,
                [this](QByteArray bytes, quint64 generation)
        {
            m_maximumClientQueued = qMax(m_maximumClientQueued, m_transport.pendingWriteBytes());
            log(QStringLiteral("client.TX.queued"), bytes, generation);
        });
        connect(&m_transport, &ReadOnlyTcpTransport::wireReceived, this,
                [this](QByteArray bytes, quint64 generation)
        {
            log(QStringLiteral("client.RX.actual"), bytes, generation);
            // wireReceived在codec feed前通知；本轮结束后观察半帧，不把清理值当自然排空。
            QTimer::singleShot(0, this, [this, generation]()
            {
                if (!m_finishing && m_transport.generation() == generation)
                    m_maximumWire = qMax(m_maximumWire, m_transport.bufferedBytes());
            });
        });
        connect(&m_transport, &ReadOnlyTcpTransport::bytesDelivered, this, [this](qint64 bytes, quint64 generation)
        {
            m_socketWritten += bytes;
            log(QStringLiteral("client.bytesWritten.not_peer_ack"), {}, generation,
                {{QStringLiteral("bytes"), double(bytes)}});
        });
        connect(&m_transport, &ReadOnlyTcpTransport::messageReceived, this,
                [this](QByteArray message, quint64 generation) { observeMessage(message, generation); });
        connect(&m_transport, &ReadOnlyTcpTransport::errorOccurred, this,
                [this](QString error, quint64 generation)
        {
            log(QStringLiteral("client.error"), {}, generation, {{QStringLiteral("error"), error}});
            fail(error);
        });
        connect(&m_transport, &ReadOnlyTcpTransport::disconnected, this, [this](quint64 generation)
        {
            log(QStringLiteral("client.disconnected"), {}, generation);
            if (!m_finishing) fail(QStringLiteral("完成前意外断线"));
        });
        connect(&m_stressTimer, &QTimer::timeout, this, [this]() { sendStress(); });
    }

    void start()
    {
        if (!m_trace.open(QIODevice::WriteOnly) || !m_server.listen(QHostAddress::LocalHost, 0))
        {
            fail(QStringLiteral("无法创建新日志或本进程回环监听"));
            return;
        }
        m_clock.start();
        m_heartbeat.start();
        QTextStream(stdout) << "SIMULATED PEER owned by this process, loopback only, NoProxy; port="
                            << m_server.serverPort() << Qt::endl;
        QString error;
        if (!m_transport.connectLoopback(m_server.serverPort(), &error)) fail(error);
        QTimer::singleShot(m_durationMs + 10000, this, [this]()
        {
            if (!m_finishing) fail(QStringLiteral("有限回环实验超时"));
        });
    }

private:
    void fail(const QString &message)
    {
        if (m_finishing) return;
        m_failed = true;
        QTextStream(stderr) << "LOOPBACK_ERROR " << message << Qt::endl;
        finish();
    }

    void log(const QString &kind, const QByteArray &bytes, quint64 generation, QJsonObject detail = {})
    {
        if (m_logFailed) return;
        detail.insert(QStringLiteral("sequence"), m_logCount++);
        detail.insert(QStringLiteral("elapsed_ms"), double(m_clock.isValid() ? m_clock.elapsed() : 0));
        detail.insert(QStringLiteral("generation"), double(generation));
        detail.insert(QStringLiteral("kind"), kind);
        detail.insert(QStringLiteral("evidence"), QStringLiteral("local-synthetic-peer"));
        if (!bytes.isEmpty()) detail.insert(QStringLiteral("hex"), QString::fromLatin1(bytes.toHex(' ').toUpper()));
        const QByteArray line = QJsonDocument(detail).toJson(QJsonDocument::Compact) + '\n';
        m_logBytes += line.size();
        if (m_logBytes > 16 * 1024 * 1024 || m_trace.write(line) != line.size())
        {
            m_logFailed = true;
            fail(QStringLiteral("日志写入失败或超过本轮16MiB上限"));
            return;
        }
        if (m_logCount <= 24) QTextStream(stdout) << line;
    }

    void acceptPeer()
    {
        if (m_peer)
        {
            m_server.nextPendingConnection()->deleteLater();
            fail(QStringLiteral("实验只允许一个本进程模拟客户端"));
            return;
        }
        m_peer = m_server.nextPendingConnection();
        m_peer->setProxy(QNetworkProxy::NoProxy);
        m_peer->setReadBufferSize(65536);
        connect(m_peer, &QTcpSocket::readyRead, this, [this]()
        {
            // 实验端同样限量读取；不假定一次TCP write对应一次readyRead。
            const QByteArray bytes = m_peer->read(4096);
            m_peerReceived.append(bytes);
            log(QStringLiteral("server.RX.actual"), bytes, m_transport.generation());
            if (m_peerReceived.size() > 39)
            {
                fail(QStringLiteral("模拟对端收到白名单三帧以外的数据"));
                return;
            }
            const DecodeBatch batch = m_peerDecoder.feed(bytes, m_clock.elapsed());
            if (!batch.errors.isEmpty()) { fail(batch.errors.join(QLatin1Char(';'))); return; }
            for (const QByteArray &command : batch.messages)
            {
                const QList<QByteArray> expected{QByteArray("Qj"), QByteArray("Qk"), QByteArray("QH")};
                if (m_queryCount >= expected.size() || command != expected.at(m_queryCount++))
                {
                    fail(QStringLiteral("对端实际查询载荷/顺序不符"));
                    return;
                }
            }
            if (m_queryCount == 3 && !m_responseStarted)
            {
                m_responseStarted = true;
                // 三份独立固定TX字节，避免只用同一encoder/decoder自证查询正确。
                const QByteArray literal = QByteArray::fromHex(
                    "0203000600000001516a3a0405"
                    "0203000600000001516b3b0405"
                    "02030006000000015148180405");
                if (m_peerReceived != literal) { fail(QStringLiteral("对端实际TX字节与literal不一致")); return; }
                sendInitialReplies();
            }
        });
    }

    bool sendPeerBytes(const QByteArray &bytes)
    {
        if (!m_peer || m_peer->state() != QAbstractSocket::ConnectedState ||
            m_peer->bytesToWrite() + bytes.size() > 32768) return false;
        const qint64 accepted = m_peer->write(bytes);
        if (accepted != bytes.size()) { fail(QStringLiteral("模拟端写入未完整接受")); return false; }
        log(QStringLiteral("server.TX.queued.not_peer_ack"), bytes, m_transport.generation());
        m_maximumServerQueued = qMax(m_maximumServerQueued, m_peer->bytesToWrite());
        return true;
    }

    void sendInitialReplies()
    {
        const QList<QByteArray> replies{
            "Qj[4][SIM-LOOPBACK][2][1][0][0]",
            "Qq[3][Base][11 22 33 44][1][123][1][999]",
            "Qk[1][SIM-E1][A|B]",
            QStringLiteral("QK[1][SIM-E2][模拟提示][仅供观察，不执行动作]").toUtf8(),
            "QH[2][SIM-PT][5][SIM-PT][7][1][SIM-PT][3]",
            "Ql[0]",
            "QL[0]"
        };
        for (const QByteArray &reply : replies)
            for (const QByteArray &frame : encodeMessage(reply, Direction::DeviceToHost)) m_initialWire.append(frame);
        // 首块仅两字节；之后一次写多个完整帧，分别验证TCP分段与粘包。
        if (!sendPeerBytes(m_initialWire.left(2))) { fail(QStringLiteral("初始分段写入失败")); return; }
        QTimer::singleShot(5, this, [this]()
        {
            if (!m_finishing && !sendPeerBytes(m_initialWire.mid(2))) fail(QStringLiteral("后续分段写入失败"));
        });
    }

    void observeMessage(const QByteArray &message, quint64 generation)
    {
        if (m_finishing) return;
        QElapsedTimer operation;
        operation.start();
        ReadOnlyMessage parsed;
        QString error;
        if (!parseReadOnlyMessage(message, &parsed, &error)) { fail(error); return; }
        m_parseTimes.append(double(operation.nsecsElapsed()) / 1000000.0);
        log(QStringLiteral("client.parsed"), message, generation,
            {{QStringLiteral("command"), QString::fromLatin1(parsed.command)}, {QStringLiteral("data"), parsed.data}});
        const QList<QByteArray> initialCommands{"Qj", "Qq", "Qk", "QK", "QH", "Ql", "QL"};
        if (m_messageCount < initialCommands.size() && parsed.command != initialCommands.at(m_messageCount))
        {
            fail(QStringLiteral("诊断消息解析顺序不符"));
            return;
        }
        bool fieldsMatch = true;
        const QJsonArray alarms = parsed.data.value(QStringLiteral("alarms")).toArray();
        if (m_messageCount == 0)
            fieldsMatch = parsed.data.value(QStringLiteral("main_status")).toInt(-1) == 2;
        else if (m_messageCount == 1)
        {
            const QJsonObject channels = parsed.data.value(QStringLiteral("channels")).toObject();
            fieldsMatch = channels.value(QStringLiteral("Base")) == QStringLiteral("11 22 33 44") &&
                    channels.value(QStringLiteral("1")) == QStringLiteral("123") &&
                    parsed.data.value(QStringLiteral("duplicate_channels")).toInt() == 1;
        }
        else if (m_messageCount == 2)
            fieldsMatch = alarms.size() == 1 && alarms.first().toObject().value(QStringLiteral("error_code")) == QStringLiteral("SIM-E1") &&
                    alarms.first().toObject().value(QStringLiteral("raw_parameters")) == QStringLiteral("A|B");
        else if (m_messageCount == 3)
            fieldsMatch = alarms.size() == 1 && alarms.first().toObject().value(QStringLiteral("description")) == QStringLiteral("模拟提示") &&
                    alarms.first().toObject().value(QStringLiteral("measure")) == QStringLiteral("仅供观察，不执行动作");
        else if (m_messageCount == 4)
            fieldsMatch = parsed.data.value(QStringLiteral("has")).toObject().value(QStringLiteral("SIM-PT")) == QStringLiteral("7") &&
                    parsed.data.value(QStringLiteral("used")).toObject().value(QStringLiteral("SIM-PT")) == QStringLiteral("3");
        else if (m_messageCount < 7)
            fieldsMatch = alarms.isEmpty() && parsed.data.value(QStringLiteral("notification")).toBool();
        else
            fieldsMatch = parsed.data.value(QStringLiteral("channels")).toObject().value(QStringLiteral("Base")) == QStringLiteral("123 456 789 012");
        if (!fieldsMatch) { fail(QStringLiteral("回环实际解析字段与固定匿名输入不一致")); return; }
        ++m_messageCount;
        m_maximumClientQueued = qMax(m_maximumClientQueued, m_transport.pendingWriteBytes());
        m_maximumWire = qMax(m_maximumWire, m_transport.bufferedBytes());
        if (m_messageCount == initialCommands.size())
        {
            m_stressClock.start();
            if (!m_stressTarget) { finish(); return; }
            m_stressTimer.start();
        }
        else if (m_messageCount > initialCommands.size())
        {
            if (parsed.command != "Qq") { fail(QStringLiteral("压力流包含非Qq消息")); return; }
            ++m_stressReceived;
            if (m_stressReceived == m_stressTarget && m_stressSent == m_stressTarget) finish();
        }
    }

    void sendStress()
    {
        if (m_finishing || m_stressSent >= m_stressTarget) return;
        const int desired = qMin(m_stressTarget, int(qint64(m_stressTarget) * m_stressClock.elapsed() / qMax(1, m_durationMs)));
        const int batch = qMin(32, desired - m_stressSent);
        if (batch <= 0) return;
        QByteArray wire;
        const QByteArray frame = encodeMessage(QByteArray("Qq[1][Base][123 456 789 012]"), Direction::DeviceToHost).first();
        for (int index = 0; index < batch; ++index) wire.append(frame);
        if (sendPeerBytes(wire)) m_stressSent += batch;
    }

    void finish()
    {
        if (m_finishing) return;
        m_finishing = true;
        m_stressTimer.stop();
        m_heartbeat.stop();
        const qint64 pendingBeforeClose = m_transport.pendingWriteBytes();
        const int wireBeforeClose = m_transport.bufferedBytes();
        const int assemblyBeforeClose = m_transport.assemblyBytes();
        m_transport.close();
        if (m_peer) m_peer->abort();
        m_server.close();
        const bool complete = !m_failed && !m_logFailed && m_queryCount == 3 && m_messageCount == 7 + m_stressTarget &&
                m_stressReceived == m_stressTarget && pendingBeforeClose == 0 && wireBeforeClose == 0 &&
                assemblyBeforeClose == 0 && m_transport.pendingWriteBytes() == 0 &&
                m_transport.bufferedBytes() == 0 && m_transport.assemblyBytes() == 0;
        QJsonObject summary{{QStringLiteral("schema"), QStringLiteral("sn100-loopback-result-v1")},
            {QStringLiteral("evidence"), QStringLiteral("local-synthetic-peer")},
            {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
            {QStringLiteral("architecture"), QSysInfo::buildCpuArchitecture()},
            {QStringLiteral("actual_elapsed_ms"), double(m_clock.isValid() ? m_clock.elapsed() : 0)},
            {QStringLiteral("requested_stress_duration_ms"), m_durationMs},
            {QStringLiteral("stress_messages_sent"), m_stressSent}, {QStringLiteral("stress_messages_received"), m_stressReceived},
            {QStringLiteral("readonly_queries_received_by_peer"), m_queryCount},
            {QStringLiteral("peer_rx_hex"), QString::fromLatin1(m_peerReceived.toHex(' ').toUpper())},
            {QStringLiteral("client_socket_bytesWritten_not_peer_ack"), double(m_socketWritten)},
            {QStringLiteral("parsed_messages"), m_messageCount}, {QStringLiteral("parse_p95_ms"), percentile(m_parseTimes, 95)},
            {QStringLiteral("parse_max_ms"), percentile(m_parseTimes, 100)},
            {QStringLiteral("heartbeat_interval_ms"), 10}, {QStringLiteral("heartbeat_samples"), m_heartbeatGaps.size()},
            {QStringLiteral("heartbeat_gap_p95_ms"), percentile(m_heartbeatGaps, 95)},
            {QStringLiteral("heartbeat_gap_max_ms"), percentile(m_heartbeatGaps, 100)},
            {QStringLiteral("maximum_server_bytesToWrite"), double(m_maximumServerQueued)},
            {QStringLiteral("maximum_client_pending_after_observation"), double(m_maximumClientQueued)},
            {QStringLiteral("maximum_wire_after_observation"), m_maximumWire},
            {QStringLiteral("remaining_client_write_bytes"), double(m_transport.pendingWriteBytes())},
            {QStringLiteral("remaining_client_wire_bytes"), m_transport.bufferedBytes()},
            {QStringLiteral("remaining_client_assembly_bytes"), m_transport.assemblyBytes()},
            {QStringLiteral("client_write_bytes_before_close"), double(pendingBeforeClose)},
            {QStringLiteral("client_wire_bytes_before_close"), wireBeforeClose},
            {QStringLiteral("client_assembly_bytes_before_close"), assemblyBeforeClose},
            {QStringLiteral("log_bytes"), double(m_logBytes)}, {QStringLiteral("passed"), complete},
            {QStringLiteral("hardware_verified"), false}};
        const bool logSaved = !m_logFailed && m_trace.commit();
        summary.insert(QStringLiteral("outputs_saved"), logSaved);
        QSaveFile result(m_directory + QStringLiteral("/summary.json"));
        const QByteArray bytes = QJsonDocument(summary).toJson(QJsonDocument::Indented);
        const bool saved = logSaved && result.open(QIODevice::WriteOnly) && result.write(bytes) == bytes.size() && result.commit();
        if (!saved)
        {
            summary.insert(QStringLiteral("passed"), false);
            summary.insert(QStringLiteral("outputs_saved"), false);
            QTextStream(stderr) << "OUTPUT_ERROR incomplete logs/summary; this run failed" << Qt::endl;
        }
        QTextStream(stdout) << "RESULT " << QJsonDocument(summary).toJson(QJsonDocument::Compact) << Qt::endl;
        QCoreApplication::exit(complete && saved ? 0 : 3);
    }

    QString m_directory;
    QSaveFile m_trace;
    ReadOnlyTcpTransport m_transport;
    QTcpServer m_server;
    QTcpSocket *m_peer = nullptr;
    FrameDecoder m_peerDecoder{Direction::HostToDevice};
    QElapsedTimer m_clock;
    QElapsedTimer m_stressClock;
    QTimer m_heartbeat;
    QTimer m_stressTimer;
    QByteArray m_peerReceived;
    QByteArray m_initialWire;
    QList<double> m_parseTimes;
    QList<double> m_heartbeatGaps;
    qint64 m_previousHeartbeatNs = 0;
    qint64 m_logBytes = 0;
    qint64 m_socketWritten = 0;
    qint64 m_maximumServerQueued = 0;
    qint64 m_maximumClientQueued = 0;
    int m_maximumWire = 0;
    int m_queryCount = 0;
    int m_messageCount = 0;
    int m_logCount = 0;
    int m_stressTarget;
    int m_durationMs;
    int m_stressSent = 0;
    int m_stressReceived = 0;
    bool m_responseStarted = false;
    bool m_failed = false;
    bool m_logFailed = false;
    bool m_finishing = false;
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sn100_protocol_loopback"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("O05-2 只连接本进程回环模拟端，记录双端实际TX/RX，无仪器动作"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("output-dir"), QStringLiteral("新的绝对输出目录，禁止覆盖"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("stress"), QStringLiteral("合成Qq额外接收次数0..100000"), QStringLiteral("count"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("duration-ms"), QStringLiteral("压力发送目标持续时间1..120000ms"), QStringLiteral("milliseconds"), QStringLiteral("1000")});
    parser.process(app);
    bool stressOk = false, durationOk = false;
    const int stress = parser.value(QStringLiteral("stress")).toInt(&stressOk);
    const int duration = parser.value(QStringLiteral("duration-ms")).toInt(&durationOk);
    const QString directory = QDir::cleanPath(parser.value(QStringLiteral("output-dir")));
    if (!QDir::isAbsolutePath(directory) || !stressOk || !durationOk || stress < 0 || stress > 100000 || duration < 1 || duration > 120000)
    {
        QTextStream(stderr) << "INPUT_ERROR new absolute output directory, stress0..100000 and duration1..120000 required" << Qt::endl;
        return 2;
    }
    const QFileInfo target(directory);
    if (target.fileName().isEmpty() || !QDir().mkpath(target.absolutePath()) || !QDir(target.absolutePath()).mkdir(target.fileName()))
    {
        QTextStream(stderr) << "OUTPUT_ERROR destination must be new; previous evidence is preserved" << Qt::endl;
        return 2;
    }
    LoopbackDemo demo(directory, stress, duration);
    QTimer::singleShot(0, &demo, [&demo]() { demo.start(); });
    return app.exec();
}
