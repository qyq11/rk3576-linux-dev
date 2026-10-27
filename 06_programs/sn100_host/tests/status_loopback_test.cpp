#include "status_loopback.h"
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>
#include <atomic>

using namespace Sn100Protocol;
namespace
{
// 独立手算金字节，沿用已验匿名client夹具；不是仪器抓包或生产encoder输出。
const QByteArray kQuery = QByteArray::fromHex("0203000600000001516a3a0405");
const QByteArray kStatus =
    QByteArray::fromHex("0607002000000001516a5b345d5b53494d554c415445445d5b325d5b315d5b305d5b305d570809");

class FaultWorker : public StatusLoopbackWorker
{
  public:
    enum Mode
    {
        HalfBad,
        DropFirst,
        DropAll,
        Late,
        OldHalf,
        Flood
    };
    FaultWorker(Mode mode) : StatusLoopbackWorker(9), mode_(mode) {}

  protected:
    void replyStatus(QTcpSocket *peer, quint64 generation) override
    {
        ++queries_;
        if (mode_ == DropAll || (mode_ == DropFirst && queries_ == 1))
        {
            return;
        }
        if (mode_ == Late)
        {
            if (queries_ == 1)
            {
                QTimer::singleShot(310, peer,
                                   [this, peer, generation]() { writePeer(peer, generation, kStatus); });
            }
            return;
        }
        if (mode_ == OldHalf && queries_ == 1)
        {
            writePeer(peer, generation, kStatus.left(10));
            QTimer::singleShot(20, peer, [peer]() { peer->abort(); });
            return;
        }
        if (mode_ == HalfBad)
        {
            QByteArray bad = kStatus;
            bad[bad.size() - 3] = char(bad.at(bad.size() - 3) ^ 1);
            writePeer(peer, generation, bad + kStatus.left(10));
            QTimer::singleShot(25, peer,
                               [this, peer, generation]() { writePeer(peer, generation, kStatus.mid(10)); });
            return;
        }
        if (mode_ == Flood)
        {
            auto *timer = new QTimer(peer);
            timer->setInterval(1);
            QObject::connect(timer, &QTimer::timeout, peer,
                             [this, peer, generation]()
                             {
                                 if (peer->bytesToWrite() == 0)
                                 {
                                     writePeer(peer, generation, QByteArray(4096, 'X'));
                                 }
                             });
            timer->start();
            return;
        }
        StatusLoopbackWorker::replyStatus(peer, generation);
    }

  private:
    Mode mode_;
    int queries_ = 0;
};

struct Harness
{
    explicit Harness(StatusLoopbackWorker *value, bool ack = true) : worker(value)
    {
        StatusLoopbackWorker::registerTypes();
        worker->moveToThread(&thread);
        QObject::connect(&thread, &QThread::started, worker, &StatusLoopbackWorker::start);
        QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        QObject::connect(worker, &StatusLoopbackWorker::stopped, &thread, &QThread::quit,
                         Qt::DirectConnection);
        QObject::connect(worker, &StatusLoopbackWorker::stateReady, &receiver,
                         [this, ack](LoopbackStatusState state, quint64 revision)
                         {
                             last = state;
                             if (states.size() < 512)
                             {
                                 states.append(state);
                             }
                             else
                             {
                                 overflow = true;
                             }
                             if (ack && revision && worker)
                             {
                                 QMetaObject::invokeMethod(worker, "acknowledgeState", Qt::QueuedConnection,
                                                           Q_ARG(quint64, state.runId),
                                                           Q_ARG(quint64, revision));
                             }
                         });
        QObject::connect(worker, &StatusLoopbackWorker::wireBatchReady, &receiver,
                         [this, ack](quint64 run, quint64 batchId, const LoopbackWireBatch &batch)
                         {
                             ++batches;
                             if (batch.size() > 128)
                             {
                                 overflow = true;
                             }
                             for (const auto &event : batch)
                             {
                                 if (event.bytes.size() > 4096 || wires.size() >= 512)
                                 {
                                     overflow = true;
                                 }
                                 else
                                 {
                                     wires.append(event);
                                 }
                             }
                             if (ack && batchId && worker)
                             {
                                 QMetaObject::invokeMethod(worker, "acknowledgeWire", Qt::QueuedConnection,
                                                           Q_ARG(quint64, run), Q_ARG(quint64, batchId));
                             }
                         });
        QObject::connect(worker, &StatusLoopbackWorker::error, &receiver,
                         [this](QString error)
                         {
                             if (errors.size() < 16)
                             {
                                 errors.append(error);
                             }
                             else
                             {
                                 overflow = true;
                             }
                         });
        thread.start();
    }
    ~Harness()
    {
        if (thread.isRunning() && worker)
        {
            QMetaObject::invokeMethod(worker, "stop", Qt::QueuedConnection);
        }
        thread.wait();
    }
    QByteArray tx() const
    {
        QByteArray bytes;
        for (const auto &event : wires)
        {
            if (event.direction == "TX")
            {
                bytes += event.bytes;
            }
        }
        return bytes;
    }
    QThread thread;
    QObject receiver;
    QPointer<StatusLoopbackWorker> worker;
    LoopbackStatusState last;
    QVector<LoopbackStatusState> states;
    QVector<LoopbackWireEvent> wires;
    QStringList errors;
    int batches = 0;
    bool overflow = false;
};

std::atomic<int> wireLines{0};
std::atomic<int> largestLine{0};
QtMessageHandler previousHandler = nullptr;
void captureLog(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (message.startsWith("STATUS_LOOPBACK_WIRE"))
    {
        ++wireLines;
        int before = largestLine.load();
        while (before < message.size() && !largestLine.compare_exchange_weak(before, message.size()))
        {
        }
    }
    if (previousHandler)
    {
        previousHandler(type, context, message);
    }
}
} // namespace

class StatusLoopbackTest : public QObject
{
    Q_OBJECT
  private slots:
    void normalOwnedPeerAndGoldenWire();
    void socketAndTimerAffinity();
    void halfBadThenValid();
    void retriesExactlyOnce();
    void timeoutThenLateObservationKeepsFailure();
    void reconnectDiscardsOldHalf();
    void pendingStopCancelsAndReleasesThread();
    void controllerRestartAndBusy();
    void workerDelayDoesNotBlockMainHeartbeat();
    void unconsumedTraceIsBoundedAndStops();
    void shutdownWithoutGuiAckStillLogsWire();
};

void StatusLoopbackTest::normalOwnedPeerAndGoldenWire()
{
    StatusLoopbackController controller;
    QSignalSpy wire(&controller, &StatusLoopbackController::wireObserved);
    QSignalSpy stopped(&controller, &StatusLoopbackController::stopped);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(controller.current().fresh, 2000);
    const auto state = controller.current();
    QVERIFY(state.port > 0);
    QVERIFY(state.connected && state.hasSnapshot && state.lastRequestSuccess);
    QCOMPARE(state.snapshot.model, QString("SIMULATED"));
    QCOMPARE(state.snapshot.mainStatus, 2);
    QCOMPARE(state.rawQj, QByteArray("Qj[4][SIMULATED][2][1][0][0]"));
    QTRY_VERIFY(wire.size() >= 2);
    QByteArray tx, rx;
    for (const auto &arguments : wire)
    {
        const auto event = qvariant_cast<LoopbackWireEvent>(arguments.first());
        if (event.direction == "TX")
        {
            tx += event.bytes;
        }
        else if (event.direction == "RX")
        {
            rx += event.bytes;
        }
    }
    QCOMPARE(tx, kQuery);
    QCOMPARE(rx, kStatus);
    qInfo().noquote() << "LOOPBACK_GOLDEN actual_TX=" << tx.toHex(' ') << "actual_RX=" << rx.toHex(' ');
    QTRY_COMPARE_WITH_TIMEOUT(controller.current().requestsSucceeded, quint64(2), 2500);
    QCOMPARE(controller.current().queriesQueued, quint64(2));
    QTRY_VERIFY(wire.size() >= 4);
    QByteArray periodicTx;
    for (const auto &arguments : wire)
    {
        const auto event = qvariant_cast<LoopbackWireEvent>(arguments.first());
        if (event.direction == "TX")
        {
            periodicTx += event.bytes;
        }
    }
    QCOMPARE(periodicTx, kQuery + kQuery);
    controller.stop();
    QTRY_COMPARE(stopped.size(), 1);
    QVERIFY(!controller.isRunning() && !controller.current().fresh && !controller.current().pending);
}

void StatusLoopbackTest::socketAndTimerAffinity()
{
    Harness harness(new StatusLoopbackWorker(8));
    QTRY_VERIFY(harness.last.fresh);
    bool checked = false, correct = false;
    QMetaObject::invokeMethod(
        harness.worker,
        [&]()
        {
            bool value = QThread::currentThread() == &harness.thread;
            int sockets = 0, timers = 0;
            for (QObject *object : harness.worker->findChildren<QObject *>())
            {
                value = value && object->thread() == &harness.thread;
                sockets += qobject_cast<QTcpSocket *>(object) != nullptr;
                timers += qobject_cast<QTimer *>(object) != nullptr;
            }
            value = value && sockets == 2 && timers >= 3;
            QMetaObject::invokeMethod(&harness.receiver,
                                      [&, value]()
                                      {
                                          correct = value;
                                          checked = true;
                                      });
        },
        Qt::QueuedConnection);
    QTRY_VERIFY(checked);
    QVERIFY(correct);
}

void StatusLoopbackTest::halfBadThenValid()
{
    Harness harness(new FaultWorker(FaultWorker::HalfBad));
    QTRY_VERIFY(harness.last.fresh);
    QCOMPARE(harness.last.observations, quint64(1));
    QCOMPARE(harness.last.requestsSucceeded, quint64(1));
    QCOMPARE(harness.tx(), kQuery);
    QVERIFY(!harness.overflow);
}

void StatusLoopbackTest::retriesExactlyOnce()
{
    Harness harness(new FaultWorker(FaultWorker::DropFirst));
    QTRY_VERIFY(harness.last.fresh);
    QCOMPARE(harness.last.queriesQueued, quint64(2));
    QCOMPARE(harness.tx(), kQuery + kQuery);
    QCOMPARE(harness.last.requestsFailed, quint64(0));
}

void StatusLoopbackTest::timeoutThenLateObservationKeepsFailure()
{
    Harness harness(new FaultWorker(FaultWorker::Late));
    QTRY_COMPARE(harness.last.requestsFailed, quint64(1));
    QVERIFY(!harness.last.fresh && !harness.last.pending);
    QTRY_VERIFY(harness.last.fresh);
    QCOMPARE(harness.last.requestsSucceeded, quint64(0));
    QCOMPARE(harness.last.requestsFailed, quint64(1));
    QVERIFY(!harness.last.lastRequestSuccess);
    QCOMPARE(harness.last.queriesQueued, quint64(2));
    QCOMPARE(harness.tx(), kQuery + kQuery);
}

void StatusLoopbackTest::reconnectDiscardsOldHalf()
{
    Harness harness(new FaultWorker(FaultWorker::OldHalf));
    QTRY_VERIFY(harness.last.requestsFailed == 1 && !harness.last.connected);
    const auto oldGeneration = harness.last.connectionGeneration;
    QVERIFY(!harness.last.hasSnapshot);
    QMetaObject::invokeMethod(harness.worker, "reconnect", Qt::QueuedConnection);
    QTRY_VERIFY(harness.last.fresh);
    QVERIFY(harness.last.connectionGeneration > oldGeneration);
    QCOMPARE(harness.last.observations, quint64(1));
    QCOMPARE(harness.last.requestsSucceeded, quint64(1));
    QCOMPARE(harness.tx(), kQuery + kQuery);
}

void StatusLoopbackTest::pendingStopCancelsAndReleasesThread()
{
    Harness harness(new FaultWorker(FaultWorker::DropAll));
    QTRY_VERIFY(harness.last.pending);
    QMetaObject::invokeMethod(harness.worker, "stop", Qt::QueuedConnection);
    QTRY_VERIFY(!harness.thread.isRunning());
    QTRY_VERIFY(!harness.last.running);
    QVERIFY(!harness.last.fresh && !harness.last.hasSnapshot && !harness.last.pending);
    QCOMPARE(harness.last.requestsFailed, quint64(1));
    QVERIFY(harness.last.detail.contains("cancelled"));
}

void StatusLoopbackTest::controllerRestartAndBusy()
{
    StatusLoopbackController controller;
    QSignalSpy errors(&controller, &StatusLoopbackController::error);
    QSignalSpy stopped(&controller, &StatusLoopbackController::stopped);
    controller.start();
    controller.start();
    QCOMPARE(errors.count(), 1);
    QTRY_VERIFY(controller.current().fresh);
    const auto oldRun = controller.current().runId;
    controller.stop();
    QTRY_COMPARE(stopped.count(), 1);
    controller.start();
    QTRY_VERIFY(controller.current().fresh);
    QVERIFY(controller.current().runId > oldRun);
    controller.stop();
    QTRY_COMPARE(stopped.count(), 2);
}

void StatusLoopbackTest::workerDelayDoesNotBlockMainHeartbeat()
{
    Harness harness(new StatusLoopbackWorker(6));
    QTRY_VERIFY(harness.last.fresh);
    QTimer heartbeat;
    heartbeat.setInterval(5);
    int ticks = 0;
    connect(&heartbeat, &QTimer::timeout, this, [&]() { ++ticks; });
    heartbeat.start();
    QMetaObject::invokeMethod(harness.worker, []() { QThread::msleep(180); }, Qt::QueuedConnection);
    QTest::qWait(80);
    QVERIFY(ticks >= 5);
    qInfo() << "LOOPBACK_MAIN_HEARTBEAT during_worker_stall_ticks=" << ticks;
}

void StatusLoopbackTest::unconsumedTraceIsBoundedAndStops()
{
    Harness harness(new FaultWorker(FaultWorker::Flood), false);
    QTRY_VERIFY_WITH_TIMEOUT(!harness.thread.isRunning(), 3000);
    QTRY_VERIFY(harness.last.traceOverflow);
    QVERIFY(!harness.last.running && !harness.last.fresh);
    QVERIFY(harness.errors.join(';').contains("trace_budget"));
    QVERIFY(harness.last.error.contains("trace_budget"));
    QCOMPARE(harness.last.linkState, QString("error"));
    QCOMPARE(harness.last.detail, harness.last.error);
    QVERIFY(harness.batches <= 2); // 一批未ACK + 停止时的有界尾批；没有无界queued批次。
    QVERIFY(harness.wires.size() <= 144);
    QVERIFY(harness.states.size() <= 2);
    QVERIFY(!harness.overflow);
}

void StatusLoopbackTest::shutdownWithoutGuiAckStillLogsWire()
{
    wireLines = 0;
    largestLine = 0;
    previousHandler = qInstallMessageHandler(captureLog);
    StatusLoopbackController controller;
    controller.start();
    QThread::msleep(350); // 故意不消费主线程queued状态/wire，worker仍独立完成请求。
    const bool stopped = controller.shutdownAndWait();
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
    QVERIFY(stopped && !controller.isRunning());
    QVERIFY(wireLines.load() >= 2);
    QVERIFY(largestLine.load() <= 3 * 4096 + 256);
    QCOMPARE(controller.current().wireEvents, quint64(2));
    QCOMPARE(controller.current().wireBytes, quint64(kQuery.size() + kStatus.size()));
    QCOMPARE(controller.current().requestsSucceeded, quint64(1));
    QTest::qWait(30);
    QVERIFY(!controller.current().running && !controller.current().fresh);
    qInfo() << "LOOPBACK_SHUTDOWN no_gui_ack_actual_wire_lines=" << wireLines.load();
}

QTEST_GUILESS_MAIN(StatusLoopbackTest)
#include "status_loopback_test.moc"
