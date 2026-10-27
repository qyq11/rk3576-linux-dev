#include "read_only_client.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkProxy>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextStream>
#include <QTimer>

using namespace Sn100Protocol;

// 独立有限实验：本进程模拟端故意不回第一次Qj，验证只读重试及后续查询。
// 不接受仪器地址，不读取旧配置；线上无事务ID，匹配类型观测不能证明仪器ACK。
class ClientDemo : public QObject
{
public:
    explicit ClientDemo(QString directory)
        : m_directory(directory), m_trace(directory + "/wire.jsonl"), m_client(nullptr, 50, 1),
          m_peerDecoder(Direction::HostToDevice)
    {
        m_server.setProxy(QNetworkProxy::NoProxy);
        connect(&m_server, &QTcpServer::newConnection, this, [this]() { acceptPeer(); });
        connect(&m_client, &ReadOnlyClient::connected, this, [this](quint64)
        {
            log("client.connected");
            issue(ReadOnlyQuery::Status);
        });
        connect(&m_client, &ReadOnlyClient::queryQueued, this,
                [this](ReadOnlyQuery query, int attempt, quint64)
        {
            ++m_attempts;
            log("client.query.queued.not_peer_ack", queryPayload(query), {{"attempt", attempt}});
        });
        connect(&m_client, &ReadOnlyClient::notice, this, [this](QString detail, quint64)
        {
            log("client.notice", {}, {{"detail", detail}});
        });
        connect(&m_client, &ReadOnlyClient::messageObserved, this, [this](ReadOnlyMessage message, quint64)
        {
            ++m_observations;
            log("client.RX.parsed", message.command, {{"data", message.data}});
        });
        connect(&m_client, &ReadOnlyClient::requestFinished, this,
                [this](ReadOnlyQuery query, bool success, QString detail, quint64)
        {
            log("client.request.finished", queryPayload(query), {{"success", success}, {"detail", detail}});
            if (!success) { finish(false, detail); return; }
            ++m_completed;
            // 排到下一事件轮，先完整交付本次messageObserved再启动下一个请求。
            QTimer::singleShot(0, this, [this, query]()
            {
                if (m_finished) return;
                if (query == ReadOnlyQuery::Status) issue(ReadOnlyQuery::Alarm);
                else if (query == ReadOnlyQuery::Alarm) issue(ReadOnlyQuery::Consumables);
                else finish(true, {});
            });
        });
        connect(&m_client, &ReadOnlyClient::disconnected, this, [this](quint64)
        {
            if (!m_finished) finish(false, "client closed before experiment completed");
        });
    }

    void start()
    {
        m_clock.start();
        if (!m_trace.open(QIODevice::WriteOnly) || !m_server.listen(QHostAddress::LocalHost, 0))
        { finish(false, "cannot create trace or owned loopback listener"); return; }
        QTextStream(stdout) << "SIMULATED owned loopback peer; no instrument ACK claim; port="
                            << m_server.serverPort() << Qt::endl;
        QString error;
        if (!m_client.connectLoopback(m_server.serverPort(), &error)) finish(false, error);
        QTimer::singleShot(3000, this, [this]() { finish(false, "experiment deadline"); });
    }

private:
    void log(const QString &kind, const QByteArray &bytes = {}, QJsonObject fields = {})
    {
        if (m_finished) return;
        fields.insert("sequence", ++m_sequence);
        fields.insert("kind", kind);
        fields.insert("elapsed_ms", double(m_clock.elapsed()));
        fields.insert("evidence", "local-synthetic-peer");
        if (!bytes.isEmpty()) fields.insert("hex", QString::fromLatin1(bytes.toHex(' ').toUpper()));
        const QByteArray line = QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n';
        if (m_logFailed) return;
        if (m_trace.size() + line.size() > 1024 * 1024 || m_trace.write(line) != line.size())
        { m_logFailed = true; finish(false, "trace write failure or 1MiB budget exceeded"); return; }
        QTextStream(stdout) << line;
    }

    void issue(ReadOnlyQuery query)
    {
        QString error;
        if (!m_client.request(query, &error)) finish(false, error);
    }

    void acceptPeer()
    {
        if (m_peer) { m_server.nextPendingConnection()->deleteLater(); finish(false, "extra peer"); return; }
        m_peer = m_server.nextPendingConnection();
        m_peer->setProxy(QNetworkProxy::NoProxy);
        m_peer->setReadBufferSize(4096);
        connect(m_peer, &QTcpSocket::readyRead, this, [this]()
        {
            const QByteArray bytes = m_peer->read(4096);
            m_actualQueries += bytes;
            log("peer.RX.actual", bytes);
            const DecodeBatch batch = m_peerDecoder.feed(bytes, m_clock.elapsed());
            if (!batch.errors.isEmpty()) { finish(false, batch.errors.join(';')); return; }
            for (const QByteArray &query : batch.messages)
            {
                log("peer.RX.command", query);
                if (query == "Qj")
                {
                    if (++m_statusQueries == 1) log("peer.deliberately.withheld.first.Qj");
                    else if (m_statusQueries == 2) reply("Qj[4][SIMULATED][2][1][0][0]");
                    else { finish(false, "unexpected further status retry"); return; }
                }
                else if (query == "Qk")
                {
                    // 这些同流通知不能提前完成Qk；只读协调器仍等严格同命令。
                    reply("Qq[0]"); reply("Ql[0]"); reply("QK[0]"); reply("Qk[0]");
                }
                else if (query == "QH") reply("QH[0][0]");
                else { finish(false, "unexpected query"); return; }
            }
        });
    }

    void reply(const QByteArray &message)
    {
        for (const QByteArray &frame : encodeMessage(message, Direction::DeviceToHost))
        {
            log("peer.TX.queued.not_client_ack", frame, {{"payload", QString::fromUtf8(message)}});
            if (m_peer->write(frame) != frame.size()) { finish(false, "peer queue rejected"); return; }
        }
    }

    void finish(bool candidate, QString error)
    {
        if (m_finished || m_finishing) return;
        m_finishing = true;
        // 独立固定金字节：实际收到Qj、Qj重试、Qk和QH，每帧13字节。
        const QByteArray expected = QByteArray::fromHex(
            "0203000600000001516a3a04050203000600000001516a3a0405"
            "0203000600000001516b3b040502030006000000015148180405");
        bool passed = candidate && m_actualQueries == expected && m_statusQueries == 2 && m_completed == 3
                && m_attempts == 4 && m_observations == 6 && m_client.hasFreshStatus()
                && !m_client.requestPending() && m_peerDecoder.bufferedBytes() == 0
                && m_peerDecoder.assemblyBytes() == 0 && !m_logFailed;
        if (!passed && error.isEmpty()) error = "experiment values or output flush mismatch";
        log("experiment.result", {}, {{"passed", passed}, {"error", error}});
        const bool traceSaved = !m_logFailed && m_trace.flush();
        if (!traceSaved) { passed = false; error = "trace save failed"; }
        m_finished = true;
        m_trace.close();
        const QString beforeClose = m_client.linkState();
        m_client.close();
        if (m_peer) m_peer->abort();
        m_server.close();
        QJsonObject summary{{"passed", passed}, {"error", error}, {"elapsed_ms", double(m_clock.elapsed())},
            {"actual_peer_rx_hex", QString::fromLatin1(m_actualQueries.toHex())}, {"actual_peer_rx_bytes", m_actualQueries.size()},
            {"attempts", m_attempts}, {"completed_queries", m_completed}, {"observations", m_observations},
            {"state_before_close", beforeClose}, {"state_after_close", m_client.linkState()},
            {"fresh_status_after_close", m_client.hasFreshStatus()}, {"instrument_verified", false},
            {"outputs_saved", traceSaved},
            {"meaning", "Latest matching type observation only; no transaction ID or instrument ACK proof"}};
        QSaveFile output(m_directory + "/summary.json");
        const QByteArray data = QJsonDocument(summary).toJson();
        const bool saved = output.open(QIODevice::WriteOnly) && output.write(data) == data.size() && output.commit();
        if (!saved) { summary.insert("outputs_saved", false); summary.insert("passed", false); }
        QTextStream(stdout) << QJsonDocument(summary).toJson(QJsonDocument::Compact) << Qt::endl;
        if (!saved) QTextStream(stderr) << "OUTPUT_ERROR cannot save final summary" << Qt::endl;
        QCoreApplication::exit(saved && traceSaved ? (passed ? 0 : 1) : 3);
    }

    QString m_directory;
    QFile m_trace;
    ReadOnlyClient m_client;
    QTcpServer m_server;
    QTcpSocket *m_peer = nullptr;
    FrameDecoder m_peerDecoder;
    QElapsedTimer m_clock;
    QByteArray m_actualQueries;
    int m_sequence = 0, m_attempts = 0, m_completed = 0, m_observations = 0, m_statusQueries = 0;
    bool m_finished = false, m_finishing = false, m_logFailed = false;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("O05-2 owned loopback read-only request/retry demonstration");
    parser.addHelpOption();
    parser.addOption({"output-dir", "New absolute output directory (existing directory protected)", "path"});
    parser.process(app);
    const QString directory = QDir::cleanPath(parser.value("output-dir"));
    const QFileInfo destination(directory);
    if (!parser.positionalArguments().isEmpty() || !QDir::isAbsolutePath(directory)
            || destination.exists() || !QDir(destination.absolutePath()).exists()
            || !QDir(destination.absolutePath()).mkdir(destination.fileName()))
    { QTextStream(stderr) << "--output-dir requires a new absolute directory with an existing parent" << Qt::endl; return 2; }
    ClientDemo demo(directory);
    QTimer::singleShot(0, &demo, [&demo]() { demo.start(); });
    return app.exec();
}
