#include "status_session.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>
#include <algorithm>
#include <memory>

using namespace Sn100Protocol;

namespace
{
constexpr qint64 kMaximumLogBytes = 32 * 1024 * 1024;

bool strictHex(const QString &text, QByteArray *bytes)
{
    QByteArray digits;
    for (QChar character : text)
    {
        if (character.isSpace()) continue;
        const ushort code = character.unicode();
        if (!((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f') ||
              (code >= 'A' && code <= 'F'))) return false;
        digits.append(char(code));
    }
    if (digits.isEmpty() || digits.size() % 2 || digits.size() > kMaxFeedBytes * 2) return false;
    *bytes = QByteArray::fromHex(digits);
    return true;
}

double percentile(QList<double> values, int percent)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    return values.at(qMin(values.size() - 1, (values.size() * percent + 99) / 100 - 1));
}

bool writeJson(const QString &path, const QJsonObject &object)
{
    QSaveFile output(path);
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return output.open(QIODevice::WriteOnly) && output.write(bytes) == bytes.size() && output.commit();
}

class ReplayRunner : public QObject
{
public:
    ReplayRunner(QJsonArray events, QString outputDirectory, int stressMessages)
        : m_events(events), m_directory(outputDirectory), m_stressMessages(stressMessages),
          m_log(m_directory + QStringLiteral("/events.jsonl")),
          m_trace(m_directory + QStringLiteral("/tx_rx.txt"))
    {
        m_heartbeat.setTimerType(Qt::PreciseTimer);
        m_heartbeat.setInterval(10);
        connect(&m_heartbeat, &QTimer::timeout, this, [this]()
        {
            const qint64 now = m_clock.nsecsElapsed();
            m_heartbeatGaps.append(double(now - m_previousHeartbeat) / 1000000.0);
            m_previousHeartbeat = now;
        });
    }

    void start()
    {
        if (!m_log.open(QIODevice::WriteOnly) || !m_trace.open(QIODevice::WriteOnly))
        {
            finishWithError(QStringLiteral("无法打开新输出文件"));
            return;
        }
        m_clock.start();
        m_heartbeat.start();
        QTimer::singleShot(0, this, [this]() { step(); });
    }

private:
    void finishWithError(const QString &message)
    {
        m_heartbeat.stop();
        QTextStream(stderr) << "REPLAY_ERROR " << message << Qt::endl;
        m_log.cancelWriting();
        m_trace.cancelWriting();
        QCoreApplication::exit(3);
    }

    bool record(const QJsonObject &input, const SessionEvents &events, bool display)
    {
        QJsonArray tx;
        QJsonArray snapshots;
        const qint64 at = qint64(input.value(QStringLiteral("at_ms")).toDouble());
        const QString prefix = QStringLiteral("SIM t=%1 seq=%2 ").arg(at).arg(m_recordCount);
        QString trace;
        if (input.value(QStringLiteral("action")) == QStringLiteral("rx"))
        {
            trace += prefix + QStringLiteral("RX ") + input.value(QStringLiteral("hex")).toString() + QLatin1Char('\n');
        }
        for (const QByteArray &frame : events.transmitted)
        {
            const QString hex = QString::fromLatin1(frame.toHex(' ').toUpper());
            tx.append(hex);
            trace += prefix + QStringLiteral("TX ") + hex + QLatin1Char('\n');
        }
        for (const StatusSnapshot &status : events.statuses)
        {
            snapshots.append(status.toJson());
            trace += prefix + QStringLiteral("STATUS model=%1 main=%2 led=%3 error=%4\n")
                    .arg(status.model).arg(status.mainStatus).arg(status.ledStatus).arg(status.errorWorkType);
        }
        for (const QString &notice : events.notices)
        {
            trace += prefix + notice + QLatin1Char('\n');
        }
        bool expected = true;
        if (input.contains(QStringLiteral("expect_link_state")))
            expected &= input.value(QStringLiteral("expect_link_state")).toString() == m_session.linkState();
        if (input.contains(QStringLiteral("expect_status_count")))
            expected &= input.value(QStringLiteral("expect_status_count")).toInt(-1) == events.statuses.size();
        if (input.contains(QStringLiteral("expect_tx_count")))
            expected &= input.value(QStringLiteral("expect_tx_count")).toInt(-1) == events.transmitted.size();
        if (input.contains(QStringLiteral("expect_main_status")))
            expected &= !events.statuses.isEmpty() && input.value(QStringLiteral("expect_main_status")).toInt(-1)
                    == events.statuses.last().mainStatus;
        QJsonObject output{{QStringLiteral("sequence"), m_recordCount}, {QStringLiteral("input"), input},
            {QStringLiteral("tx_hex"), tx}, {QStringLiteral("statuses"), snapshots},
            {QStringLiteral("notices"), QJsonArray::fromStringList(events.notices)},
            {QStringLiteral("link_state"), m_session.linkState()},
            {QStringLiteral("fresh_status"), m_session.hasFreshStatus()},
            {QStringLiteral("expectations_met"), expected}};
        const QByteArray line = QJsonDocument(output).toJson(QJsonDocument::Compact) + '\n';
        const QByteArray traceBytes = trace.toUtf8();
        m_logBytes += line.size() + traceBytes.size();
        if (m_logBytes > kMaximumLogBytes || m_log.write(line) != line.size() ||
            m_trace.write(traceBytes) != traceBytes.size())
        {
            finishWithError(QStringLiteral("输出失败或超过本轮32MiB日志预算，本次回放不算通过"));
            return false;
        }
        if (display) QTextStream(stdout) << trace;
        if (!expected) ++m_failedExpectations;
        m_statusCount += events.statuses.size();
        m_transmitCount += events.transmitted.size();
        m_noticeCount += events.notices.size();
        m_maximumWire = qMax(m_maximumWire, m_session.bufferedBytes());
        m_maximumAssembly = qMax(m_maximumAssembly, m_session.assemblyBytes());
        ++m_recordCount;
        return true;
    }

    void step()
    {
        if (m_eventIndex < m_events.size())
        {
            const QJsonObject event = m_events.at(m_eventIndex++).toObject();
            const qint64 at = qint64(event.value(QStringLiteral("at_ms")).toDouble());
            const QString action = event.value(QStringLiteral("action")).toString();
            SessionEvents output;
            if (action == QStringLiteral("connect")) output = m_session.open(at);
            else if (action == QStringLiteral("query-status")) output = m_session.queryStatus(at);
            else if (action == QStringLiteral("advance")) output = m_session.advance(at);
            else if (action == QStringLiteral("disconnect")) output = m_session.disconnect(at);
            else
            {
                QByteArray bytes;
                strictHex(event.value(QStringLiteral("hex")).toString(), &bytes);
                output = m_session.receive(bytes, at);
                // 压力测试只重放已由完整解析器认可的合成状态，仍走同一个receive入口。
                if (!output.statuses.isEmpty() && m_session.bufferedBytes() == 0)
                {
                    FrameDecoder check;
                    const DecodeBatch candidate = check.feed(bytes, at);
                    if (candidate.messages.size() == 1 && candidate.errors.isEmpty()) m_stressFrame = bytes;
                }
            }
            m_virtualTime = at;
            if (!record(event, output, true)) return;
            QTimer::singleShot(1, this, [this]() { step(); });
            return;
        }
        if (m_stressIndex < m_stressMessages)
        {
            if (m_stressFrame.isEmpty())
            {
                finishWithError(QStringLiteral("压力回放需要夹具内至少一份完整合法单包Qj状态"));
                return;
            }
            if (m_stressIndex == 0)
            {
                m_session.open(++m_virtualTime);
                QTextStream(stdout) << "STRESS repeating literal RX, every frame saved to tx_rx.txt; count="
                                    << m_stressMessages << Qt::endl;
            }
            for (int batch = 0; batch < 20 && m_stressIndex < m_stressMessages; ++batch)
            {
                const QJsonObject event{{QStringLiteral("at_ms"), double(++m_virtualTime)},
                    {QStringLiteral("action"), QStringLiteral("rx")},
                    {QStringLiteral("hex"), QString::fromLatin1(m_stressFrame.toHex(' ').toUpper())},
                    {QStringLiteral("expect_status_count"), 1},
                    {QStringLiteral("expect_tx_count"), 0}};
                QElapsedTimer operation;
                operation.start();
                const SessionEvents output = m_session.receive(m_stressFrame, m_virtualTime);
                m_receiveTimes.append(double(operation.nsecsElapsed()) / 1000000.0);
                if (!record(event, output, m_stressIndex == 0 || m_stressIndex == m_stressMessages - 1)) return;
                ++m_stressIndex;
            }
            QTimer::singleShot(1, this, [this]() { step(); });
            return;
        }
        finish();
    }

    void finish()
    {
        m_heartbeat.stop();
        const qint64 elapsed = m_clock.elapsed();
        const QJsonObject summary{{QStringLiteral("schema"), QStringLiteral("sn100-protocol-result-v1")},
            {QStringLiteral("evidence"), QStringLiteral("simulated-source-derived")},
            {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
            {QStringLiteral("architecture"), QSysInfo::buildCpuArchitecture()},
            {QStringLiteral("host_cpu_architecture"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("os"), QSysInfo::prettyProductName()},
            {QStringLiteral("records"), m_recordCount}, {QStringLiteral("status_observations"), m_statusCount},
            {QStringLiteral("simulated_tx_frames"), m_transmitCount}, {QStringLiteral("notices"), m_noticeCount},
            {QStringLiteral("failed_expectations"), m_failedExpectations},
            {QStringLiteral("stress_messages"), m_stressIndex}, {QStringLiteral("elapsed_ms"), double(elapsed)},
            {QStringLiteral("receive_p95_ms"), percentile(m_receiveTimes, 95)},
            {QStringLiteral("receive_max_ms"), percentile(m_receiveTimes, 100)},
            {QStringLiteral("heartbeat_interval_ms"), 10},
            {QStringLiteral("heartbeat_samples"), m_heartbeatGaps.size()},
            {QStringLiteral("heartbeat_gap_p95_ms"), percentile(m_heartbeatGaps, 95)},
            {QStringLiteral("heartbeat_gap_max_ms"), percentile(m_heartbeatGaps, 100)},
            {QStringLiteral("remaining_wire_bytes"), m_session.bufferedBytes()},
            {QStringLiteral("remaining_assembly_bytes"), m_session.assemblyBytes()},
            {QStringLiteral("maximum_wire_bytes_after_feed"), m_maximumWire},
            {QStringLiteral("maximum_assembly_bytes_after_feed"), m_maximumAssembly},
            {QStringLiteral("log_bytes"), double(m_logBytes)},
            {QStringLiteral("final_link_state"), m_session.linkState()},
            {QStringLiteral("hardware_verified"), false}};
        if (!m_log.commit() || !m_trace.commit() || !writeJson(m_directory + QStringLiteral("/summary.json"), summary))
        {
            finishWithError(QStringLiteral("提交输出文件失败，不能登记回放通过"));
            return;
        }
        QTextStream(stdout) << "RESULT " << QJsonDocument(summary).toJson(QJsonDocument::Compact) << Qt::endl;
        QCoreApplication::exit(m_failedExpectations ? 4 : 0);
    }

    QJsonArray m_events;
    QString m_directory;
    int m_stressMessages;
    QSaveFile m_log;
    QSaveFile m_trace;
    StatusSession m_session;
    QTimer m_heartbeat;
    QElapsedTimer m_clock;
    QList<double> m_heartbeatGaps;
    QList<double> m_receiveTimes;
    QByteArray m_stressFrame;
    qint64 m_previousHeartbeat = 0;
    qint64 m_virtualTime = 0;
    qint64 m_logBytes = 0;
    int m_eventIndex = 0;
    int m_stressIndex = 0;
    int m_recordCount = 0;
    int m_statusCount = 0;
    int m_transmitCount = 0;
    int m_noticeCount = 0;
    int m_failedExpectations = 0;
    int m_maximumWire = 0;
    int m_maximumAssembly = 0;
};

bool validateReplay(const QJsonObject &object, QString *error)
{
    if (object.value(QStringLiteral("schema")) != QStringLiteral("sn100-protocol-replay-v1") ||
        object.value(QStringLiteral("evidence")) != QStringLiteral("simulated-source-derived") ||
        !object.value(QStringLiteral("events")).isArray())
    {
        *error = QStringLiteral("输入必须是声明合成来源的 sn100-protocol-replay-v1 夹具");
        return false;
    }
    const QJsonArray events = object.value(QStringLiteral("events")).toArray();
    if (events.isEmpty() || events.size() > 10000)
    {
        *error = QStringLiteral("事件数量必须为1..10000");
        return false;
    }
    double previous = -1;
    for (const QJsonValue &value : events)
    {
        if (!value.isObject()) { *error = QStringLiteral("事件必须是对象"); return false; }
        const QJsonObject event = value.toObject();
        const QJsonValue timestamp = event.value(QStringLiteral("at_ms"));
        const double at = timestamp.toDouble(-1);
        const QString action = event.value(QStringLiteral("action")).toString();
        if (!timestamp.isDouble() || at < previous || at < 0 || at > 3600000 || at != qint64(at))
        {
            *error = QStringLiteral("at_ms必须为0..3600000非递减整数");
            return false;
        }
        previous = at;
        if (action == QStringLiteral("rx"))
        {
            QByteArray bytes;
            if (!strictHex(event.value(QStringLiteral("hex")).toString(), &bytes))
            {
                *error = QStringLiteral("RX必须是完整十六进制字节，最多65536字节");
                return false;
            }
        }
        else if (action != QStringLiteral("connect") && action != QStringLiteral("disconnect") &&
                 action != QStringLiteral("query-status") && action != QStringLiteral("advance"))
        {
            *error = QStringLiteral("未知事件动作；本程序仅提供模拟连接、Qj查询、RX和时间推进");
            return false;
        }
        const QStringList linkStates{QStringLiteral("disconnected"), QStringLiteral("awaiting-status"),
            QStringLiteral("status-timeout"), QStringLiteral("connected-status-unknown"),
            QStringLiteral("has-simulated-status")};
        for (auto item = event.constBegin(); item != event.constEnd(); ++item)
        {
            if (!item.key().startsWith(QStringLiteral("expect_"))) continue;
            if (item.key() == QStringLiteral("expect_link_state"))
            {
                if (!item.value().isString() || !linkStates.contains(item.value().toString()))
                {
                    *error = QStringLiteral("expect_link_state不是支持的连接状态");
                    return false;
                }
            }
            else if (item.key() == QStringLiteral("expect_status_count") ||
                     item.key() == QStringLiteral("expect_tx_count") ||
                     item.key() == QStringLiteral("expect_main_status"))
            {
                const double expected = item.value().toDouble(-1);
                const int maximum = item.key() == QStringLiteral("expect_main_status") ? 6 : 65536;
                if (!item.value().isDouble() || expected < 0 || expected > maximum || expected != qint64(expected))
                {
                    *error = QStringLiteral("expect计数/主状态必须是支持范围内的整数");
                    return false;
                }
            }
            else
            {
                *error = QStringLiteral("未知expect字段，拒绝忽略可能拼错的验收条件：") + item.key();
                return false;
            }
        }
    }
    return true;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sn100_protocol_replay"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SN100 O05-1 源码契约模拟回放：无网络、无仪器、无控制动作"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("input"), QStringLiteral("合成JSON夹具的绝对路径"), QStringLiteral("file")});
    parser.addOption({QStringLiteral("output-dir"), QStringLiteral("新的绝对输出目录，拒绝覆盖已有结果"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("stress"), QStringLiteral("额外重复合法状态的次数，0..10000"), QStringLiteral("count"), QStringLiteral("0")});
    parser.process(app);
    const QString inputPath = parser.value(QStringLiteral("input"));
    const QString outputPath = QDir::cleanPath(parser.value(QStringLiteral("output-dir")));
    bool countOk = false;
    const int stress = parser.value(QStringLiteral("stress")).toInt(&countOk);
    QFile input(inputPath);
    if (!QDir::isAbsolutePath(inputPath) || !QDir::isAbsolutePath(outputPath) ||
        !countOk || stress < 0 || stress > 10000 || !input.open(QIODevice::ReadOnly) || input.size() > 1024 * 1024)
    {
        QTextStream(stderr) << "INPUT_ERROR absolute input/output paths, JSON <=1MiB and stress 0..10000 required" << Qt::endl;
        return 2;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input.readAll(), &parseError);
    QString error;
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        !validateReplay(document.object(), &error))
    {
        QTextStream(stderr) << "INPUT_ERROR " << parseError.errorString() << " " << error << Qt::endl;
        return 2;
    }
    const QFileInfo destination(outputPath);
    // 独占创建最终目录；两个并行进程不能都通过检查后覆盖同一批结果。
    if (destination.fileName().isEmpty() || !QDir().mkpath(destination.absolutePath()) ||
        !QDir(destination.absolutePath()).mkdir(destination.fileName()))
    {
        QTextStream(stderr) << "OUTPUT_ERROR output directory must be new; existing results are preserved" << Qt::endl;
        return 2;
    }
    ReplayRunner runner(document.object().value(QStringLiteral("events")).toArray(), outputPath, stress);
    QTimer::singleShot(0, &runner, [&runner]() { runner.start(); });
    return app.exec();
}
