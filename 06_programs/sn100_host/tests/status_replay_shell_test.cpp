#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <functional>
#include "data/hospital_store.h"
#include "protocol/status_replay.h"
#include "shell/frmmain.h"
#include "ui/frmhospinfosetting.h"

namespace
{
template<class T> T *find(QObject *root, const char *name)
{
    return root->findChild<T *>(QString::fromLatin1(name));
}

bool waitUntil(const std::function<bool()> &predicate, int timeout = 15000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout) QTest::qWait(10);
    return predicate();
}

class HospitalFixture
{
public:
    HospitalFixture() : store(new HospitalStore)
    {
        store->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        worker.start();
    }
    ~HospitalFixture()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        if (!worker.wait(5000)) qFatal("Test hospital worker failed to stop");
    }
    bool initialize(frmMain &window, const QString &directory)
    {
        if (!QDir().mkpath(directory)) return false;
        window.setDataDirectory(directory);
        HospitalStore *target = store;
        const bool queued = QMetaObject::invokeMethod(store, [target, directory]()
        {
            target->initialize(QDir(directory).filePath(QStringLiteral("hospital_Information.db")),
                               QDir(directory).filePath(QStringLiteral("config.ini")));
        }, Qt::QueuedConnection);
        return queued && waitUntil([&]() { return window.hospitalPage()->isReady() && !window.hospitalPage()->isBusy(); });
    }
    QThread worker;
    HospitalStore *store;
};

void checkControls(frmMain &window)
{
    for (const char *name : {"btnStart", "btnPause", "btnCloseBeep", "btnAbnormal", "btnLis"})
    {
        auto *button = find<QWidget>(&window, name);
        QVERIFY2(button, name);
        QVERIFY2(!button->isEnabled(), name);
    }
    QVERIFY(find<QWidget>(&window, "btnShutDown")->isEnabled());
    for (const char *name : {"lblCupHave", "lblWasteCup", "lblTemp", "lblWaterHave", "lblWasteWater"})
    {
        auto *label = find<QLabel>(&window, name);
        QVERIFY2(label && !label->isEnabled(), name);
    }
}
}

class StatusReplayShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void originalPathAndMapping();
    void timelineAndViewports();
    void productLifecycleAndInputFailure();
private:
    QString directory_, fixture_, app_;
};

void StatusReplayShellTest::initTestCase()
{
    directory_ = QString::fromUtf8(qgetenv("SN100_STATUS_REPLAY_EVIDENCE_DIR"));
    fixture_ = QString::fromUtf8(qgetenv("SN100_STATUS_REPLAY_FIXTURE"));
    app_ = QString::fromUtf8(qgetenv("SN100_STATUS_REPLAY_APP"));
    QVERIFY(QDir::isAbsolutePath(directory_) && !QFileInfo::exists(directory_));
    QVERIFY(QFileInfo(fixture_).isFile() && QFileInfo(app_).isExecutable());
    QVERIFY(QDir().mkpath(directory_));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
}

void StatusReplayShellTest::originalPathAndMapping()
{
    HospitalFixture hospital;
    frmMain window(hospital.store);
    QVERIFY(hospital.initialize(window, QDir(directory_).filePath(QStringLiteral("mapping_hospital"))));
    QVERIFY(window.selectMainPage(7));
    auto *label = find<QLabel>(&window, "lblDivState");
    QVERIFY(label && !label->toolTip().contains(QStringLiteral("文件回放")));
    const QRect original = label->geometry();
    const AccountSession identity = window.accountSession();
    Sn100Protocol::ReplayState state;
    state.loaded = state.fresh = state.hasSnapshot = true;
    state.eventCount = 10; state.eventIndex = 3; state.sourceTimeMs = 20;
    state.action = QStringLiteral("rx"); state.linkState = QStringLiteral("has-simulated-status");
    state.snapshot.model = QStringLiteral("SIMULATED");
    state.snapshot.mainStatus = 2; state.snapshot.ledStatus = 1; state.snapshot.errorWorkType = 0;
    window.setStatusReplayState(state);
    QCOMPARE(window.property("statusReplayText").toString(), QStringLiteral("回放/准备就绪"));
    QVERIFY(label->toolTip().contains(QStringLiteral("真实仪器状态未知")));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#38BF65")));
    QCOMPARE(label->geometry(), original);
    checkControls(window);
    QCOMPARE(window.accountSession().authenticated, identity.authenticated);
    QCOMPARE(window.accountSession().actionReady, identity.actionReady);
    QVERIFY(QMetaObject::invokeMethod(window.hospitalPage(), "statusChanged", Qt::DirectConnection,
        Q_ARG(QString, QStringLiteral("匿名后台失败")), Q_ARG(bool, true)));
    QVERIFY(label->toolTip().contains(QStringLiteral("文件回放")));
    QVERIFY(window.selectMainPage(6));
    QVERIFY(!label->toolTip().contains(QStringLiteral("文件回放")));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#E83323")));
    QVERIFY(window.selectMainPage(7));
    QVERIFY(label->toolTip().contains(QStringLiteral("文件回放")));
    state.snapshot.ledStatus = 5; state.snapshot.mainStatus = 5;
    window.setStatusReplayState(state);
    QCOMPARE(window.property("statusReplayText").toString(), QStringLiteral("回放/暂停中"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("42,118,242")));
    state.snapshot.ledStatus = 6;
    window.setStatusReplayState(state);
    QCOMPARE(window.property("statusReplayText").toString(), QStringLiteral("回放/试剂不足"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("249,192,75")));
    state.snapshot.ledStatus = 3; state.snapshot.mainStatus = 5;
    window.setStatusReplayState(state);
    QCOMPARE(window.property("statusReplayText").toString(), QStringLiteral("回放/异常停止"));
    state.fresh = state.hasSnapshot = false;
    state.linkState = QStringLiteral("status-timeout");
    window.setStatusReplayState(state);
    QCOMPARE(window.property("statusReplayText").toString(), QStringLiteral("回放/已超时"));
    QVERIFY(!window.property("statusReplayFresh").toBool());
    state.finished = true;
    window.setStatusReplayState(state);
    QCOMPARE(window.property("statusReplayText").toString(), QStringLiteral("回放/已结束"));
    checkControls(window);
}

void StatusReplayShellTest::timelineAndViewports()
{
    HospitalFixture hospital;
    auto *window = new frmMain(hospital.store);
    QScrollArea host;
    host.setWidget(window); host.setWidgetResizable(false);
    QVERIFY(hospital.initialize(*window, QDir(directory_).filePath(QStringLiteral("viewport_hospital"))));
    QVERIFY(window->selectMainPage(7));
    Sn100Protocol::StatusReplay replay;
    QString error;
    QVERIFY2(replay.loadFile(fixture_, &error), qPrintable(error));
    int fresh = 0;
    int applied = 0;
    qint64 maximumApplyNs = 0, totalApplyNs = 0;
    host.resize(1600, 900); host.show();
    while (replay.step())
    {
        QElapsedTimer applyClock; applyClock.start();
        window->setStatusReplayState(replay.current());
        const qint64 elapsedNs = applyClock.nsecsElapsed();
        maximumApplyNs = qMax(maximumApplyNs, elapsedNs); totalApplyNs += elapsedNs; ++applied;
        checkControls(*window);
        if (replay.current().fresh)
        {
            ++fresh;
            QVERIFY(window->property("statusReplayFresh").toBool());
            if (fresh == 1)
            {
                QTest::qWait(100);
                QVERIFY(host.grab().save(QDir(directory_).filePath(QStringLiteral("replay_1600.png"))));
                host.resize(1920, 1080);
                QTest::qWait(100);
                QVERIFY(host.grab().save(QDir(directory_).filePath(QStringLiteral("replay_1920.png"))));
            }
        }
    }
    QVERIFY(fresh > 0 && replay.current().finished);
    QVERIFY(!window->property("statusReplayFresh").toBool());
    QCOMPARE(window->property("statusReplayText").toString(), QStringLiteral("回放/已结束"));
    QVERIFY(host.grab().save(QDir(directory_).filePath(QStringLiteral("replay_finished.png"))));
    QFile metrics(QDir(directory_).filePath(QStringLiteral("apply_metrics.json")));
    QVERIFY(metrics.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    metrics.write(QJsonDocument(QJsonObject{{"applied_events", applied},
        {"maximum_apply_ns", double(maximumApplyNs)},
        {"mean_apply_ns", applied ? double(totalApplyNs) / applied : 0},
        {"scope", "Synchronous state-to-widget update only; excludes paint, event queue, file virtual time, network and instrument latency"}}).toJson());
}

void StatusReplayShellTest::productLifecycleAndInputFailure()
{
    const QString runtime = QDir(directory_).filePath(QStringLiteral("product_runtime"));
    QProcess process;
    process.start(app_, {QStringLiteral("--offline"), QStringLiteral("--data-dir"), runtime,
        QStringLiteral("--status-replay"), fixture_});
    QByteArray output;
    const auto collect = [&]()
    {
        output += process.readAllStandardOutput(); output += process.readAllStandardError();
        return output.contains("SHELL_READY") && output.contains("finished= true");
    };
    const bool replayed = waitUntil(collect);
    process.terminate();
    const bool stopped = waitUntil([&]() { collect(); return process.state() == QProcess::NotRunning; });
    if (!stopped) { process.kill(); process.waitForFinished(5000); }
    collect();
    QFile log(QDir(directory_).filePath(QStringLiteral("product.log")));
    QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::NewOnly)); log.write(output); log.close();
    QVERIFY2(replayed && stopped, output.constData());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 0);
    QVERIFY(output.contains("STATUS_REPLAY_PLANNED_TX") && output.contains("STATUS_REPLAY_FILE_RX"));
    QVERIFY(output.contains("STATUS_REPLAY_STOPPED true") && output.contains("THREAD_STOPPED true"));
    const QString rejectedRuntime = QDir(directory_).filePath(QStringLiteral("must_not_initialize"));
    QProcess invalid;
    invalid.start(app_, {QStringLiteral("--data-dir"), rejectedRuntime, QStringLiteral("--status-replay"), QStringLiteral("relative.json")});
    QVERIFY(waitUntil([&]() { return invalid.state() == QProcess::NotRunning; }));
    QCOMPARE(invalid.exitCode(), 2);
    const QByteArray rejected = invalid.readAllStandardError() + invalid.readAllStandardOutput();
    QVERIFY(rejected.contains("STATUS_REPLAY_INPUT_FAILED"));
    QVERIFY(!QFileInfo::exists(rejectedRuntime));
}

QTEST_MAIN(StatusReplayShellTest)
#include "status_replay_shell_test.moc"
