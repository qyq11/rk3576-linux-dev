#include "status_replay.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Sn100Protocol;

namespace
{
// 独立literal帧按参考帧头/大端长度/XOR规则构造，未用生产encoder生成。
// 全部是匿名合成输入，不是设备抓包。原型字段依据见既有protocol_replay.json。
const QByteArray kReady = QByteArray::fromHex("0607002000000001516a5b345d5b53494d554c415445445d5b325d5b315d5b305d5b305d570809");
const QByteArray kTesting = QByteArray::fromHex("0607002000000001516a5b345d5b53494d554c415445445d5b355d5b345d5b305d5b305d550809");
const QByteArray kQuery = QByteArray::fromHex("0203000600000001516a3a0405");

QJsonObject replayEvent(qint64 at, const char *action, const QByteArray &bytes = {})
{
    QJsonObject object{{"at_ms", double(at)}, {"action", QString::fromLatin1(action)}};
    if (QByteArray(action) == "rx") object.insert("hex", QString::fromLatin1(bytes.toHex()));
    return object;
}

QByteArray document(const QJsonArray &events)
{
    return QJsonDocument(QJsonObject{{"schema", "sn100-status-replay-v1"}, {"events", events}}).toJson();
}
}

class StatusReplayTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        m_root = qEnvironmentVariable("SN100_STATUS_REPLAY_TEST_DIR");
        QVERIFY2(QDir::isAbsolutePath(m_root) && QDir(m_root).exists()
                 && !m_root.contains("sn100_host_reference", Qt::CaseInsensitive),
                 "Set an existing isolated absolute SN100_STATUS_REPLAY_TEST_DIR");
    }

    void anonymousFixture()
    {
        const QString path = QFINDTESTDATA("fixtures/status_replay.json");
        QVERIFY(!path.isEmpty());
        StatusReplay replay;
        QString error;
        QVERIFY2(replay.loadFile(QFileInfo(path).absoluteFilePath(), &error), qPrintable(error));
        QCOMPARE(replay.current().eventCount, 14);
        int freshSteps = 0, txFrames = 0;
        while (replay.step())
        {
            const ReplayState &state = replay.current();
            if (state.fresh)
            {
                ++freshSteps;
                QVERIFY(state.hasSnapshot);
                QCOMPARE(state.snapshot.model, QStringLiteral("SIMULATED"));
            }
            for (const QByteArray &bytes : state.transmitted) { QCOMPARE(bytes, kQuery); ++txFrames; }
            qInfo().noquote() << "REPLAY_STEP" << state.eventIndex << "recorded_ms" << state.sourceTimeMs
                << state.action << state.linkState << "fresh" << state.fresh
                << "TX" << (state.transmitted.isEmpty() ? QByteArray() : state.transmitted.last().toHex(' '))
                << "RX" << state.received.toHex(' ') << "notices" << state.notices;
        }
        QVERIFY(freshSteps >= 3);
        QCOMPARE(txFrames, 3);
        QVERIFY(replay.current().finished && !replay.current().fresh && !replay.current().hasSnapshot);
        QCOMPARE(replay.current().linkState, QStringLiteral("disconnected"));
    }

    void splitAndCoalesced()
    {
        StatusReplay replay;
        QVERIFY(load(replay, {replayEvent(0, "connect"), replayEvent(1, "rx", kReady.left(7)),
            replayEvent(2, "rx", kReady.mid(7) + kTesting), replayEvent(3, "advance"), replayEvent(4, "disconnect")}));
        QVERIFY(replay.step() && replay.step());
        QVERIFY(!replay.current().fresh);
        QVERIFY(replay.step());
        QVERIFY(replay.current().fresh);
        QCOMPARE(replay.current().snapshot.mainStatus, 5);
        QCOMPARE(replay.current().snapshot.ledStatus, 4);
        QCOMPARE(replay.current().statusTimeMs, qint64(2));
    }

    void badPacketRecovery()
    {
        QByteArray corrupt = kTesting;
        corrupt[corrupt.size() - 3] = char(corrupt.at(corrupt.size() - 3) ^ 1);
        StatusReplay replay;
        QVERIFY(load(replay, {replayEvent(0, "connect"), replayEvent(1, "rx", corrupt + kReady), replayEvent(2, "advance")}));
        QVERIFY(replay.step() && replay.step());
        QVERIFY(replay.current().fresh && !replay.current().notices.isEmpty());
        QCOMPARE(replay.current().snapshot.mainStatus, 2);
        QVERIFY(replay.current().error.isEmpty());
    }

    void timeoutAndSilence()
    {
        StatusReplay replay;
        QVERIFY(load(replay, {replayEvent(0, "connect"), replayEvent(1, "query-status"), replayEvent(121, "advance"),
            replayEvent(241, "advance"), replayEvent(250, "rx", kReady), replayEvent(200250, "advance"), replayEvent(200251, "advance")}));
        QVERIFY(replay.step() && replay.step());
        QCOMPARE(replay.current().transmitted, QList<QByteArray>{kQuery});
        QVERIFY(replay.step());
        QCOMPARE(replay.current().transmitted, QList<QByteArray>{kQuery});
        QVERIFY(replay.step());
        QCOMPARE(replay.current().linkState, QStringLiteral("status-timeout"));
        QVERIFY(!replay.current().fresh && !replay.current().hasSnapshot);
        // 此后只是新的同类观测，不作为先前请求ACK展示。
        QVERIFY(replay.step() && replay.current().fresh);
        QVERIFY(replay.step());
        QCOMPARE(replay.current().linkState, QStringLiteral("disconnected"));
        QVERIFY(!replay.current().fresh && !replay.current().hasSnapshot);
    }

    void disconnectClearsPartialInput()
    {
        StatusReplay replay;
        QVERIFY(load(replay, {replayEvent(0, "connect"), replayEvent(1, "rx", kReady.left(9)), replayEvent(2, "disconnect"),
            replayEvent(3, "connect"), replayEvent(4, "rx", kReady.mid(9)), replayEvent(5, "rx", kTesting), replayEvent(6, "advance")}));
        for (int i = 0; i < 5; ++i) QVERIFY(replay.step());
        QVERIFY(!replay.current().fresh && !replay.current().hasSnapshot);
        QCOMPARE(replay.current().epoch, quint64(3));
        QVERIFY(replay.step() && replay.current().fresh);
        QCOMPARE(replay.current().snapshot.mainStatus, 5);
    }

    void stopAndEndInvalidate()
    {
        StatusReplay replay;
        const QString path = input(document({replayEvent(0, "connect"), replayEvent(1, "rx", kReady), replayEvent(2, "advance")}));
        QVERIFY(!path.isEmpty() && replay.loadFile(path));
        QVERIFY(replay.step() && replay.step() && replay.current().fresh);
        replay.stop();
        QVERIFY(replay.current().finished && replay.current().stopped);
        QVERIFY(!replay.current().fresh && !replay.current().hasSnapshot && !replay.step());
        QCOMPARE(replay.current().snapshot.mainStatus, -1);
        const quint64 stoppedEpoch = replay.current().epoch;
        replay.stop();
        QCOMPARE(replay.current().epoch, stoppedEpoch);
        QVERIFY(replay.loadFile(path));
        while (replay.step()) {}
        QVERIFY(replay.current().finished && !replay.current().stopped);
        QVERIFY(!replay.current().fresh && !replay.current().hasSnapshot);
    }

    void invalidContract_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("json") << QByteArray("{");
        QTest::newRow("top-array") << QByteArray("[]");
        QTest::newRow("wrong-schema") << QByteArray("{\"schema\":\"other\",\"events\":[]}");
        QTest::newRow("empty-events") << document({});
        QJsonObject extra{{"schema", "sn100-status-replay-v1"}, {"events", QJsonArray{replayEvent(0, "connect")}}, {"address", "127.0.0.1"}};
        QTest::newRow("external-field") << QJsonDocument(extra).toJson();
        for (const auto &row : QList<QPair<QString,QJsonObject>>{
            {"unknown-action", replayEvent(0, "start-motor")},
            {"negative-time", replayEvent(-1, "connect")},
            {"late-time", replayEvent(86400001, "connect")},
            {"fractional-time", {{"at_ms", .5}, {"action", "connect"}}},
            {"string-time", {{"at_ms", "1"}, {"action", "connect"}}},
            {"bool-time", {{"at_ms", true}, {"action", "connect"}}},
            {"extra-event-field", {{"at_ms", 0}, {"action", "connect"}, {"hex", "00"}}},
            {"missing-hex", {{"at_ms", 0}, {"action", "rx"}}},
            {"empty-hex", {{"at_ms", 0}, {"action", "rx"}, {"hex", ""}}},
            {"odd-hex", {{"at_ms", 0}, {"action", "rx"}, {"hex", "0"}}},
            {"bad-hex", {{"at_ms", 0}, {"action", "rx"}, {"hex", "gg"}}}})
            QTest::newRow(qPrintable(row.first)) << document({row.second});
        QTest::newRow("backwards") << document({replayEvent(2, "connect"), replayEvent(1, "advance")});
        QTest::newRow("file-budget") << QByteArray(1024 * 1024 + 1, ' ');
        QJsonArray tooMany;
        for (int i = 0; i < 257; ++i) tooMany.append(replayEvent(i, "advance"));
        QTest::newRow("event-budget") << document(tooMany);
        QTest::newRow("rx-budget") << document({replayEvent(0, "rx", QByteArray(8193, '\0'))});
        QJsonArray tooMuchRx;
        for (int i = 0; i < 17; ++i) tooMuchRx.append(replayEvent(i, "rx", QByteArray(8192, '\0')));
        QTest::newRow("total-rx-budget") << document(tooMuchRx);
    }

    void invalidContract()
    {
        QFETCH(QByteArray, bytes);
        StatusReplay replay;
        QString error;
        QVERIFY(!replay.loadFile(input(bytes), &error));
        QVERIFY(!error.isEmpty() && !replay.current().error.isEmpty());
        QVERIFY(!replay.current().loaded && !replay.current().fresh && !replay.step());
    }

    void exactBudgetsAndPathGuards()
    {
        StatusReplay replay;
        QJsonArray bounded;
        for (int i = 0; i < 16; ++i) bounded.append(replayEvent(i, "rx", QByteArray(8192, '\0')));
        QVERIFY(load(replay, bounded));
        QJsonArray maximumEvents;
        for (int i = 0; i < 256; ++i) maximumEvents.append(replayEvent(i == 255 ? 86400000 : i, "advance"));
        QVERIFY(load(replay, maximumEvents));
        while (replay.step()) {}
        QCOMPARE(replay.current().sourceTimeMs, qint64(86400000));
        QString error;
        QVERIFY(!replay.loadFile(QStringLiteral("relative.json"), &error));
        QVERIFY(!replay.loadFile(m_root + "/missing.json", &error));
        QVERIFY(!replay.loadFile(m_root, &error));
        const QString original = input(document({replayEvent(0, "connect")}));
        const QString link = m_root + QStringLiteral("/replay_link.json");
        QVERIFY(QFile::link(original, link));
        QVERIFY(!replay.loadFile(link, &error));
        const QString reserved = m_root + QStringLiteral("/sn100_host_reference_replay.json");
        QVERIFY(QFile::copy(original, reserved));
        QVERIFY(!replay.loadFile(reserved, &error));
    }

private:
    QString input(const QByteArray &contents)
    {
        const QString path = m_root + QStringLiteral("/input_%1.json").arg(++m_files);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(contents) != contents.size()) return {};
        return path;
    }
    bool load(StatusReplay &replay, const QJsonArray &events)
    {
        QString error;
        const bool loaded = replay.loadFile(input(document(events)), &error);
        if (!loaded) qWarning().noquote() << error;
        return loaded;
    }
    QString m_root;
    int m_files = 0;
};

QTEST_GUILESS_MAIN(StatusReplayTest)
#include "status_replay_test.moc"
