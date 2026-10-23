#include "read_only_client.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QPointer>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>
#include <array>
#include <cmath>

using namespace Sn100Protocol;

namespace
{
// 固定容量，不把小时级每次心跳/查询存入不断增长的容器。
struct IntervalHistogram
{
    std::array<quint64, 2002> bins{};
    quint64 count = 0;
    double maximum = 0;
    void add(double ms)
    {
        ++bins[size_t(qBound(0, int(qMin(ms, 2001.0)), 2001))];
        ++count;
        maximum = qMax(maximum, ms);
    }
    QJsonObject json() const
    {
        quint64 cumulative = 0;
        int bucket = 0;
        for (; bucket < 2002; ++bucket)
        {
            cumulative += bins[size_t(bucket)];
            if (cumulative >= (count * 95 + 99) / 100) break;
        }
        return {{"samples", double(count)}, {"maximum_ms", maximum},
                {"p95_bucket_upper_ms", count ? double(qMin(bucket + 1, 2002)) : 0},
                {"p95_overflow", count && bucket == 2001}, {"bucket_width_ms", 1}};
    }
};

bool saveJson(const QString &path, const QJsonObject &value)
{
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(value).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QByteArray goldenQuery(ReadOnlyQuery query)
{
    if (query == ReadOnlyQuery::Status) return QByteArray::fromHex("0203000600000001516a3a0405");
    if (query == ReadOnlyQuery::Alarm) return QByteArray::fromHex("0203000600000001516b3b0405");
    return QByteArray::fromHex("02030006000000015148180405");
}

class Soak : public QObject
{
public:
    Soak(QString directory, int seconds, int rate, int reconnectSeconds, int retryEvery,
         int segmentBytes, int logBudgetBytes)
        : m_directory(directory), m_durationMs(qint64(seconds) * 1000),
          m_reconnectMs(qint64(reconnectSeconds) * 1000), m_segmentBytes(segmentBytes), m_logBudgetBytes(logBudgetBytes),
          m_rate(rate), m_retryEvery(retryEvery),
          m_client(nullptr, 120, 1), m_peerDecoder(Direction::HostToDevice)
    {
        m_server.setProxy(QNetworkProxy::NoProxy);
        m_sendTimer.setInterval(1000 / rate);
        m_sendTimer.setTimerType(Qt::PreciseTimer);
        m_queryTimer.setInterval(1000);
        m_heartbeat.setInterval(10);
        m_heartbeat.setTimerType(Qt::PreciseTimer);
        m_progress.setInterval(10000);
        connect(&m_server, &QTcpServer::newConnection, this, [this]() { acceptPeer(); });
        connect(&m_sendTimer, &QTimer::timeout, this, [this]() { sendDiagnostic(); });
        connect(&m_queryTimer, &QTimer::timeout, this, [this]() { issueQuery(); });
        connect(&m_progress, &QTimer::timeout, this, [this]()
        {
            if (!m_log.flush() || !saveJson(m_directory + "/progress.json", snapshot("running")))
                finish(false, "progress/log save failed", true);
        });
        connect(&m_heartbeat, &QTimer::timeout, this, [this]()
        {
            const qint64 now = m_clock.nsecsElapsed();
            m_heartbeatGaps.add(double(now - m_previousHeartbeatNs) / 1000000.0);
            m_previousHeartbeatNs = now;
            service();
        });
        connect(&m_client, &ReadOnlyClient::queryQueued, this, [this](ReadOnlyQuery query, int attempt, quint64 generation)
        {
            if (attempt < 1 || attempt > 2) { finish(false, "unbounded request retry"); return; }
            ++m_attempts;
            m_expectedQueryBytes += goldenQuery(query);
            log("client.TX.queued.not_peer_ack", goldenQuery(query), {{"attempt", attempt}, {"generation", double(generation)}});
        });
        connect(&m_client, &ReadOnlyClient::connected, this, [this](quint64 generation)
        {
            ++m_connections;
            m_recovering = false;
            m_recoveryStarted = -1;
            m_sendTimer.start();
            m_queryTimer.start();
            m_nextReconnect = m_clock.elapsed() + m_reconnectMs;
            if (!log("client.connected", {}, {{"generation", double(generation)}})) return;
            issueQuery();
        });
        connect(&m_client, &ReadOnlyClient::disconnected, this, [this](quint64 generation)
        {
            log("client.disconnected", {}, {{"generation", double(generation)}});
            if (!m_recovering && !m_finishing) finish(false, "unexpected client disconnect");
        });
        connect(&m_client, &ReadOnlyClient::notice, this, [this](QString message, quint64 generation)
        {
            log("client.notice", {}, {{"message", message}, {"generation", double(generation)}});
            finish(false, "unexpected client notice: " + message);
        });
        connect(&m_client, &ReadOnlyClient::messageObserved, this, [this](ReadOnlyMessage message, quint64 generation)
        {
            if (!log("client.RX.parsed", message.command, {{"data", message.data}, {"generation", double(generation)}})) return;
            if (message.command == "Qq")
            {
                const QString expected = QString::number(m_received + 1);
                if (message.data.value("channels").toObject().value("Base").toString() != expected)
                { finish(false, "Qq sequence mismatch, expected " + expected); return; }
                ++m_received;
            }
            else if (message.command == "Qj")
            {
                if (message.data.value("model").toString() != "SIMULATED" ||
                    message.data.value("main_status").toInt(-1) != 2 ||
                    message.data.value("led_status").toInt(-1) != 1 ||
                    message.data.value("error_work_type").toInt(-1) != 0)
                    finish(false, "Qj field mismatch");
            }
            else if (message.command == "Qk")
            {
                if (message.data.value("declared_count").toInt(-1) != 0 ||
                    !message.data.value("alarms").isArray() || !message.data.value("alarms").toArray().isEmpty())
                    finish(false, "Qk field mismatch");
            }
            else if (message.command == "QH")
            {
                if (message.data.value("declared_has_count").toInt(-1) != 0 ||
                    message.data.value("declared_used_count").toInt(-1) != 0 ||
                    !message.data.value("has").isObject() || !message.data.value("has").toObject().isEmpty() ||
                    !message.data.value("used").isObject() || !message.data.value("used").toObject().isEmpty())
                    finish(false, "QH field mismatch");
            }
            else
                finish(false, "unexpected observation command");
        });
        connect(&m_client, &ReadOnlyClient::requestFinished, this,
                [this](ReadOnlyQuery query, bool success, QString detail, quint64 generation)
        {
            if (!log("client.request.finished", queryPayload(query), {{"success", success}, {"detail", detail}, {"generation", double(generation)}})) return;
            if (success)
            {
                ++m_completed;
                m_queryLatency.add(double(m_clock.elapsed() - m_requestStarted));
            }
            else if (m_recovering || m_finishing) ++m_cancelled;
            else finish(false, "unexpected request failure: " + detail);
        });
    }

    void start()
    {
        m_clock.start();
        m_startedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if (!openSegment() || !m_server.listen(QHostAddress::LocalHost, 0))
        { finish(false, "cannot open segment or owned loopback listener", true); return; }
        if (!log("experiment.started", {}, {{"duration_ms", double(m_durationMs)}, {"rate", m_rate},
            {"reconnect_ms", double(m_reconnectMs)}, {"retry_every", m_retryEvery}, {"port", m_server.serverPort()}})) return;
        m_heartbeat.start();
        m_progress.start();
        if (!saveJson(m_directory + "/progress.json", snapshot("running")))
        { finish(false, "initial progress save failed", true); return; }
        connectClient();
    }

private:
    bool openSegment()
    {
        if (m_log.isOpen())
        {
            if (!m_log.flush()) return false;
            m_log.close();
        }
        m_log.setFileName(m_directory + QString("/wire_%1.jsonl").arg(++m_segment, 4, 10, QLatin1Char('0')));
        return m_log.open(QIODevice::WriteOnly | QIODevice::NewOnly);
    }

    bool log(const QString &kind, const QByteArray &bytes = {}, QJsonObject detail = {})
    {
        if (m_logFailed || m_finished) return false;
        detail.insert("sequence", double(++m_events));
        detail.insert("elapsed_ms", double(m_clock.elapsed()));
        detail.insert("kind", kind);
        if (!bytes.isEmpty()) detail.insert("hex", QString::fromLatin1(bytes.toHex(' ').toUpper()));
        const QByteArray line = QJsonDocument(detail).toJson(QJsonDocument::Compact) + '\n';
        if (m_logBytes + line.size() > m_logBudgetBytes ||
            (m_log.size() + line.size() > m_segmentBytes && !openSegment()) ||
            m_log.write(line) != line.size())
        {
            m_logFailed = true;
            finish(false, "log budget exceeded or write failed", true);
            return false;
        }
        m_logBytes += line.size();
        if (m_events <= 12) QTextStream(stdout) << line;
        return true;
    }

    void connectClient()
    {
        QString error;
        if (!m_client.connectLoopback(m_server.serverPort(), &error)) finish(false, error);
    }

    void acceptPeer()
    {
        QTcpSocket *socket = m_server.nextPendingConnection();
        if (m_peer) { socket->deleteLater(); finish(false, "extra peer connection"); return; }
        m_peer = socket;
        socket->setProxy(QNetworkProxy::NoProxy);
        socket->setReadBufferSize(65536);
        m_peerDecoder.reset();
        const QPointer<QTcpSocket> peer(socket);
        connect(socket, &QTcpSocket::disconnected, this, [this, peer]()
        {
            if (m_peer == peer) m_peer = nullptr;
            if (peer) peer->deleteLater();
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, peer]()
        {
            if (!peer || m_peer != peer || m_finishing) return;
            const QByteArray bytes = peer->read(4096);
            if (!log("peer.RX.actual", bytes)) return;
            // 不用同一编码器生成期望TX，逐字节核对独立literal查询金样本。
            if (bytes.size() > m_expectedQueryBytes.size() || !m_expectedQueryBytes.startsWith(bytes))
            { finish(false, "actual query wire differs from independent golden bytes"); return; }
            m_expectedQueryBytes.remove(0, bytes.size());
            m_peerRxBytes += bytes.size();
            const DecodeBatch batch = m_peerDecoder.feed(bytes, m_clock.elapsed());
            if (!batch.errors.isEmpty()) { finish(false, batch.errors.join(';')); return; }
            for (const QByteArray &query : batch.messages)
            {
                if (query != queryPayload(m_query)) { finish(false, "peer query type mismatch"); return; }
                if (++m_peerAttempts == 1 && m_retryEvery && m_logicalQueries % m_retryEvery == 0)
                { ++m_withheld; log("peer.deliberately.withheld.first.reply", query); continue; }
                if (query == "Qj") reply("Qj[4][SIMULATED][2][1][0][0]");
                else if (query == "Qk") reply("Qk[0]");
                else if (query == "QH") reply("QH[0][0]");
                else { finish(false, "unexpected non-readonly query"); return; }
            }
        });
    }

    void reply(const QByteArray &message)
    {
        if (!m_peer) { finish(false, "no synthetic peer"); return; }
        for (const QByteArray &frame : encodeMessage(message, Direction::DeviceToHost))
        {
            if (m_peer->bytesToWrite() + frame.size() > 32768)
            { finish(false, "synthetic peer write budget exceeded"); return; }
            if (!log("peer.TX.queued.not_client_ack", frame)) return;
            const QPointer<QTcpSocket> peer = m_peer;
            if (!peer || peer->write(frame) != frame.size()) { finish(false, "synthetic peer write rejected"); return; }
            if (m_finishing || !peer) return;
            m_serverQueueMax = qMax(m_serverQueueMax, peer->bytesToWrite());
        }
    }

    void sendDiagnostic()
    {
        if (m_recovering || m_draining || m_finishing) return;
        ++m_sent;
        reply("Qq[1][Base][" + QByteArray::number(m_sent) + "]");
    }

    void issueQuery()
    {
        if (m_recovering || m_draining || m_finishing || m_client.requestPending()) return;
        m_query = static_cast<ReadOnlyQuery>(m_logicalQueries % 3);
        ++m_logicalQueries;
        m_peerAttempts = 0;
        m_requestStarted = m_clock.elapsed();
        QString error;
        if (!m_client.request(m_query, &error)) finish(false, error);
    }

    void service()
    {
        if (m_finishing) return;
        const qint64 now = m_clock.elapsed();
        if (now >= m_durationMs && !m_draining)
        {
            m_draining = true;
            m_sendTimer.stop();
            m_queryTimer.stop();
            if (!log("experiment.draining")) return;
        }
        if (m_draining)
        {
            if (m_received == m_sent && !m_client.requestPending() && m_expectedQueryBytes.isEmpty() && !m_recovering)
                finish(true, {});
            else if (now >= m_durationMs + 10000) finish(false, "final drain deadline");
            return;
        }
        if (m_recoveryStarted >= 0 && now - m_recoveryStarted > 5000)
        { finish(false, "recovery deadline"); return; }
        if (!m_reconnectMs || m_nextReconnect < 0 || m_recovering || now < m_nextReconnect) return;
        // 排空已发诊断，再显式丢弃一段旧连接半帧。此前业务消息不能被吞掉。
        m_sendTimer.stop();
        m_queryTimer.stop();
        if (m_received != m_sent || !m_expectedQueryBytes.isEmpty())
        {
            if (now - m_nextReconnect > 2000) finish(false, "pre-reconnect drain deadline");
            return;
        }
        m_recovering = true;
        m_recoveryStarted = now;
        ++m_recoveries;
        const quint64 generation = m_client.generation();
        if (!m_peer || m_peer->write(QByteArray::fromHex("0607")) != 2)
        { finish(false, "cannot queue old connection half-frame"); return; }
        if (!log("peer.TX.queued.old.half_frame", QByteArray::fromHex("0607"), {{"generation", double(generation)}})) return;
        QTimer::singleShot(20, this, [this, generation]()
        {
            if (m_finishing || m_client.generation() != generation) return;
            m_client.close();
            if (!m_expectedQueryBytes.isEmpty())
            {
                // 20ms故障窗口内可能恰好又有重试入队。取消时这些字节尚无
                // 对端RX证据，只能记为未确认，不能带到新连接冒认其查询。
                if (!log("client.TX.unconfirmed_on_cancel", m_expectedQueryBytes,
                    {{"generation", double(generation)}})) return;
                m_cancelledWireBytes += quint64(m_expectedQueryBytes.size());
                m_expectedQueryBytes.clear();
            }
            if (m_peer)
            {
                QTcpSocket *old = m_peer;
                m_peer = nullptr;
                old->abort();
                old->deleteLater();
            }
            if (m_client.hasFreshStatus() || m_client.requestPending())
            { finish(false, "old connection state survived close"); return; }
            QTimer::singleShot(20, this, [this]() { if (!m_finishing) connectClient(); });
        });
    }

    QJsonObject snapshot(const QString &state) const
    {
        return {{"schema", "sn100-o05-3-soak-v1"}, {"state", state}, {"started_utc", m_startedUtc},
            {"updated_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {"pid", double(QCoreApplication::applicationPid())}, {"qt_version", qVersion()},
            {"elapsed_ms", double(m_clock.elapsed())}, {"duration_ms", double(m_durationMs)}, {"rate", m_rate},
            {"reconnect_ms", double(m_reconnectMs)}, {"retry_every", m_retryEvery},
            {"diagnostics_sent", double(m_sent)}, {"diagnostics_received", double(m_received)},
            {"logical_queries", double(m_logicalQueries)}, {"query_attempts", double(m_attempts)},
            {"completed_queries", double(m_completed)}, {"cancelled_queries", double(m_cancelled)},
            {"deliberately_withheld_replies", double(m_withheld)}, {"peer_query_rx_bytes", double(m_peerRxBytes)},
            {"cancelled_unconfirmed_query_bytes", double(m_cancelledWireBytes)},
            {"connections", double(m_connections)}, {"planned_recoveries", double(m_recoveries)},
            {"peer_queue_max_bytes", double(m_serverQueueMax)}, {"expected_query_wire_remaining", m_expectedQueryBytes.size()},
            {"request_pending", m_client.requestPending()}, {"fresh_status", m_client.hasFreshStatus()},
            {"link_state", m_client.linkState()}, {"log_bytes", double(m_logBytes)}, {"log_segments", m_segment},
            {"segment_bytes", double(m_segmentBytes)}, {"log_budget_bytes", double(m_logBudgetBytes)},
            {"minimum_load_fraction", 0.90}, {"maximum_allowed_gap_ms", 120000},
            {"heartbeat_interval_ms", 10}, {"heartbeat", m_heartbeatGaps.json()}, {"query_latency", m_queryLatency.json()},
            {"hardware_verified", false}, {"meaning", "Synthetic matching-type observations; no instrument ACK or realtime acceptance"}};
    }

    void finish(bool candidate, QString error, bool outputError = false)
    {
        if (m_finishing || m_finished) return;
        m_finishing = true;
        m_sendTimer.stop(); m_queryTimer.stop(); m_heartbeat.stop(); m_progress.stop();
        bool passed = candidate && m_clock.elapsed() >= m_durationMs && m_sent == m_received && m_completed > 0
                && m_sent >= quint64(qMax(qint64(1), m_durationMs * m_rate * 9 / 10000))
                && m_heartbeatGaps.count >= quint64(m_durationMs * 9 / 100) && m_heartbeatGaps.maximum <= 120000
                && m_logicalQueries == m_completed + m_cancelled
                && m_peerRxBytes + m_cancelledWireBytes == m_attempts * 13
                && m_expectedQueryBytes.isEmpty() && !m_client.requestPending() && !m_recovering
                && m_peerDecoder.bufferedBytes() == 0 && m_peerDecoder.assemblyBytes() == 0 && !m_logFailed;
        if (!passed && error.isEmpty()) error = "final experiment invariant mismatch";
        log("experiment.result", {}, {{"passed", passed}, {"error", error}});
        const bool traceSaved = !m_logFailed && m_log.flush();
        if (!traceSaved) { passed = false; outputError = true; error = "trace save failed"; }
        m_log.close();
        QJsonObject summary = snapshot(passed ? "passed" : "failed");
        m_finished = true;
        m_client.close();
        if (m_peer) m_peer->abort();
        m_server.close();
        summary.insert("fresh_status_after_close", m_client.hasFreshStatus());
        summary.insert("pending_after_close", m_client.requestPending());
        summary.insert("link_state_after_close", m_client.linkState());
        summary.insert("passed", passed);
        summary.insert("error", error);
        summary.insert("outputs_saved", traceSaved && !outputError);
        const bool progressSaved = saveJson(m_directory + "/progress.json", summary);
        if (!progressSaved)
        {
            summary.insert("state", "failed"); summary.insert("passed", false); summary.insert("outputs_saved", false);
            summary.insert("error", "terminal progress save failed"); outputError = true;
        }
        const bool summarySaved = saveJson(m_directory + "/summary.json", summary);
        if (!summarySaved)
        {
            summary.insert("state", "failed"); summary.insert("passed", false); summary.insert("outputs_saved", false);
            summary.insert("error", "terminal summary save failed"); outputError = true;
            if (progressSaved) saveJson(m_directory + "/progress.json", summary);
        }
        QTextStream(stdout) << QJsonDocument(summary).toJson(QJsonDocument::Compact) << Qt::endl;
        QCoreApplication::exit(outputError ? 3 : passed ? 0 : 1);
    }

    QString m_directory, m_startedUtc;
    qint64 m_durationMs, m_reconnectMs, m_nextReconnect = -1, m_recoveryStarted = -1;
    qint64 m_segmentBytes, m_logBudgetBytes;
    int m_rate, m_retryEvery, m_segment = 0, m_peerAttempts = 0;
    ReadOnlyClient m_client;
    QTcpServer m_server;
    QPointer<QTcpSocket> m_peer;
    FrameDecoder m_peerDecoder;
    QTimer m_sendTimer, m_queryTimer, m_heartbeat, m_progress;
    QElapsedTimer m_clock;
    QFile m_log;
    QByteArray m_expectedQueryBytes;
    ReadOnlyQuery m_query = ReadOnlyQuery::Status;
    IntervalHistogram m_heartbeatGaps, m_queryLatency;
    qint64 m_previousHeartbeatNs = 0, m_requestStarted = 0, m_logBytes = 0, m_serverQueueMax = 0;
    quint64 m_sent = 0, m_received = 0, m_events = 0, m_logicalQueries = 0, m_attempts = 0;
    quint64 m_completed = 0, m_cancelled = 0, m_withheld = 0, m_connections = 0, m_recoveries = 0, m_peerRxBytes = 0;
    quint64 m_cancelledWireBytes = 0;
    bool m_recovering = false, m_draining = false, m_finishing = false, m_finished = false, m_logFailed = false;
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("O05 owned loopback soak, bounded logs and recovery; no instrument endpoint");
    parser.addHelpOption();
    parser.addOption({"output-dir", "New absolute output directory", "path"});
    parser.addOption({"duration-seconds", "Real elapsed duration 1..86400", "seconds", "7200"});
    parser.addOption({"rate", "Qq messages per second 1..50", "count", "10"});
    parser.addOption({"reconnect-seconds", "Intentional partial-frame reconnect interval, 0 disables", "seconds", "0"});
    parser.addOption({"retry-every", "Withhold the first reply every N logical queries, 0 disables", "count", "10"});
    parser.addOption({"segment-bytes", "Log segment budget1024..4194304, default4MiB", "bytes", "4194304"});
    parser.addOption({"log-budget-bytes", "Total log budget8192..536870912, default512MiB", "bytes", "536870912"});
    parser.process(app);
    auto number = [&parser](const QString &name, int low, int high)
    {
        bool ok = false;
        const QString raw = parser.value(name);
        const int value = raw.toInt(&ok);
        return ok && !raw.startsWith('+') && value >= low && value <= high ? value : -1;
    };
    const int seconds = number("duration-seconds", 1, 86400), rate = number("rate", 1, 50);
    const int reconnectSeconds = number("reconnect-seconds", 0, 3600), retryEvery = number("retry-every", 0, 10000);
    const int segmentBytes = number("segment-bytes", 1024, 4194304), logBudgetBytes = number("log-budget-bytes", 8192, 536870912);
    const QString directory = QDir::cleanPath(parser.value("output-dir"));
    const QFileInfo destination(directory);
    if (!parser.positionalArguments().isEmpty() || seconds < 0 || rate < 0 || reconnectSeconds < 0 || retryEvery < 0 ||
        segmentBytes < 0 || logBudgetBytes < segmentBytes || (qint64(logBudgetBytes) + segmentBytes - 1) / segmentBytes > 4096 ||
        !QDir::isAbsolutePath(directory) || destination.exists() || !QDir(destination.absolutePath()).exists() ||
        !QDir(destination.absolutePath()).mkdir(destination.fileName()))
    { QTextStream(stderr) << "Invalid arguments or output directory (must be new and absolute)" << Qt::endl; return 2; }
    Soak experiment(directory, seconds, rate, reconnectSeconds, retryEvery, segmentBytes, logBudgetBytes);
    QTimer::singleShot(0, &experiment, [&experiment]() { experiment.start(); });
    return app.exec();
}
