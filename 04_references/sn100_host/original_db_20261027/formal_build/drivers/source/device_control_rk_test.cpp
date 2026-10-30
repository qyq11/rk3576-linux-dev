#include "instrument/device_control.h"
#include "instrument/original_device_commands.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <memory>

using namespace Sn100Protocol;
namespace
{
QByteArray status(int main, const QByteArray &alarm = {}, const QByteArray &match = "20250905016")
{
    QByteArray message = "Qj[14][RK_TEST][" + QByteArray::number(main) + "][1][0][" + alarm +
        "][1][0][1][TEST_VERSION][TEST_SERIAL][0][0][0][" + match + "][6]";
    for (const auto &block : QList<QPair<int, int>>{{16,14},{6,3},{17,4},{3,7},{14,24},{12,8}})
    {
        message += '[' + QByteArray::number(block.first) + "][" + QByteArray::number(block.second) + ']';
        for (int i = 0; i < block.second; ++i)
            message += block.first == 16 && i == 13 ? "[0]" : "[1]";
    }
    return message;
}
}

class DeviceControlRkTest : public QObject
{
    Q_OBJECT
    QTcpServer server;
    QPointer<QTcpSocket> peer;
    std::unique_ptr<DeviceControl> control;
    FrameDecoder decoder{Direction::HostToDevice};
    QList<QByteArray> received;
    QByteArray wire;
    QElapsedTimer clock;

    void reply(const QByteArray &message)
    {
        QVERIFY(peer);
        const auto frames = encodeMessage(message, Direction::DeviceToHost);
        QVERIFY(!frames.isEmpty());
        for (const auto &frame : frames)
        {
            qInfo().noquote() << "TEST_PEER_TX" << message << "hex=" << frame.toHex();
            // 人为拆开帧头，检查TCP分段不会提前改状态或丢包。
            peer->write(frame.left(1));
            peer->flush();
            QTest::qWait(2);
            peer->write(frame.mid(1));
        }
    }

private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("本驱动只允许在RK ARM64运行，电脑只交叉编译。");
#endif
        clock.start();
        connect(&server, &QTcpServer::newConnection, this, [this]()
        {
            peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, this, [this]()
            {
                const QByteArray bytes = peer->readAll();
                wire += bytes;
                const auto batch = decoder.feed(bytes, clock.elapsed());
                QVERIFY(batch.errors.isEmpty());
                for (const auto &message : batch.messages)
                {
                    received.append(message);
                    qInfo().noquote() << "TEST_PEER_RX" << message << "hex=" << bytes.toHex();
                }
            });
        });
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    }

    void init()
    {
        decoder.reset();
        received.clear();
        wire.clear();
        control.reset(new DeviceControl);
        QVERIFY(control->open(QStringLiteral("127.0.0.1"), server.serverPort()));
        QTRY_VERIFY(control->state().connected && peer);
        QTRY_VERIFY(received.contains("Qj"));
        QVERIFY(wire.startsWith(QByteArray::fromHex("0203000600000001516a3a0405")));
        reply(status(2));
        QTRY_VERIFY(control->state().fresh);
        QCOMPARE(control->startBlocker(), QString());
    }

    void cleanup()
    {
        control->close();
        control.reset();
        if (peer) { peer->abort(); peer->deleteLater(); peer = nullptr; }
    }

    void startPauseAndResumeUseWindowsCommands()
    {
        QVERIFY(control->start());
        QVERIFY(!control->start());
        QTRY_VERIFY(received.contains("Ts"));
        QCOMPARE(received.count("Ts"), 1);
        QVERIFY(wire.contains(QByteArray::fromHex("02030006000000015473260405")));
        reply("Ts[1]");
        QTRY_COMPARE(control->state().pending, QStringLiteral("start-state"));
        QVERIFY(!control->start());
        reply(status(5));
        QTRY_VERIFY(control->state().pending.isEmpty());
        QVERIFY(control->pause());
        QVERIFY(!control->pause());
        QTRY_VERIFY(received.contains("Tp[0]"));
        QVERIFY(wire.contains(QByteArray::fromHex("020300090000000154705b305d130405")));
        reply(status(6));
        QTRY_VERIFY(control->state().pending.isEmpty());
        QVERIFY(control->start());
        QTRY_COMPARE(received.count("Ts"), 2);
        reply("Ts[0]");
        QTRY_VERIFY(control->state().pending.isEmpty());
        QCOMPARE(control->state().status.mainStatus, 6);
    }

    void staleStatusAndVersionMismatchBlockStart()
    {
        reply(status(2, {}, "OTHER_VERSION"));
        QTRY_VERIFY(control->startBlocker().contains(QStringLiteral("匹配号")));
        QVERIFY(!control->start());
        reply(status(2));
        QTRY_COMPARE(control->startBlocker(), QString());
        QTest::qWait(3200);
        QVERIFY(!control->state().fresh);
        QVERIFY(!control->start());
        QVERIFY(!received.contains("Ts"));
    }

    void disconnectDoesNotRepeatAnUnconfirmedCommand()
    {
        QVERIFY(control->start());
        QTRY_COMPARE(received.count("Ts"), 1);
        peer->disconnectFromHost();
        QTRY_VERIFY(!control->state().connected);
        QVERIFY(control->state().outcomeUnknown);
        peer->deleteLater();
        peer = nullptr;
        QVERIFY(control->open(QStringLiteral("127.0.0.1"), server.serverPort()));
        QTRY_VERIFY(control->state().connected && peer);
        reply(status(2));
        QTRY_VERIFY(control->state().fresh);
        QVERIFY(control->state().outcomeUnknown);
        QVERIFY(!control->start());
        QCOMPARE(received.count("Ts"), 1);
        QVERIFY(control->confirmObservedState());
        QVERIFY(control->start());
        QTRY_COMPARE(received.count("Ts"), 2);
    }

    void timeoutLeavesOutcomeUnknown()
    {
        QVERIFY(control->start());
        QTRY_COMPARE(received.count("Ts"), 1);
        QTest::qWait(10300);
        QVERIFY(control->state().outcomeUnknown);
        QVERIFY(control->state().pending.isEmpty());
        QCOMPARE(received.count("Ts"), 1);
        QVERIFY(!control->maintenanceCommand("Tp[0]"));
    }

    void alarmRulesMatchOriginalTestingColumn()
    {
        control->setAlarmPolicy({QStringLiteral("BLOCK")}, true);
        reply(status(2, "WARN"));
        QTRY_COMPARE(control->state().status.generalFields.at(4), QStringLiteral("WARN"));
        QCOMPARE(control->startBlocker(), QString());
        reply(status(2, "BLOCK"));
        QTRY_VERIFY(!control->startBlocker().isEmpty());
        QVERIFY(!control->start());
        QVERIFY(!received.contains("Ts"));
    }

    void fixedButtonsKeepOriginalValvePolarityAndStop()
    {
        QCOMPARE(originalDeviceButtons().size(), 23);
        QVERIFY(control->maintenanceCommand("MF[1]"));
        QVERIFY(!control->maintenanceCommand("MF[1]"));
        QTRY_VERIFY(received.contains("MF[1]"));
        QVERIFY(control->silenceAlarm());
        QTRY_VERIFY(received.contains("Mz"));
        QVERIFY(wire.contains(QByteArray::fromHex("02030006000000014d7a360405")));
        QVERIFY(!control->maintenanceCommand("ARBITRARY"));
        QVERIFY(control->maintenanceCommand("MP"));
        QTRY_VERIFY(received.contains("MP"));
        reply(status(2));
        QTRY_VERIFY(control->state().pending.isEmpty());
    }
};

QTEST_GUILESS_MAIN(DeviceControlRkTest)
#include "device_control_rk_test.moc"
