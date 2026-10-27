#include "data/hospital_store.h"
#include "instrument/instrument_page.h"
#include "protocol/status_loopback.h"
#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "ui/frmhospinfosetting.h"
#include <QAbstractButton>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMap>
#include <QProcess>
#include <QScrollArea>
#include <QTemporaryFile>
#include <QThread>
#include <QUuid>
#include <QtTest>
#include <functional>

using namespace Sn100Protocol;
namespace
{
// Independent anonymous golden bytes, shared with the already verified protocol fixture.
const QByteArray queryFrame = QByteArray::fromHex("0203000600000001516a3a0405");
const QByteArray statusFrame =
    QByteArray::fromHex("0607002000000001516a5b345d5b53494d554c415445445d5b325d5b315d5b305d5b305d570809");
const QByteArray statusMessage("Qj[4][SIMULATED][2][1][0][0]");

template <class T> T *find(QObject *root, const char *name)
{
    return root->findChild<T *>(QString::fromLatin1(name));
}
QString labelText(QObject *root, const char *name)
{
    auto *label = find<QLabel>(root, name);
    return label ? label->text() : QStringLiteral("<missing label>");
}
bool waitUntil(const std::function<bool()> &predicate, int timeout = 15000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout)
    {
        QTest::qWait(10);
    }
    return predicate();
}
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
    {
        return {};
    }
    return hash.result().toHex();
}
bool writeJson(const QString &path, const QJsonObject &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           file.write(QJsonDocument(value).toJson()) > 0;
}
class HospitalFixture
{
  public:
    HospitalFixture() : store(new HospitalStore)
    {
        HospitalStore::registerTypes();
        store->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        worker.start();
    }
    ~HospitalFixture()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        if (!worker.wait(5000))
        {
            qFatal("Loopback shell test hospital worker failed to stop");
        }
    }
    bool initialize(frmMain &window, const QString &directory)
    {
        if (!QDir().mkpath(directory))
        {
            return false;
        }
        window.setDataDirectory(directory);
        const bool queued = QMetaObject::invokeMethod(
            store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, QDir(directory).filePath(QStringLiteral("hospital_Information.db"))),
            Q_ARG(QString, QDir(directory).filePath(QStringLiteral("config.ini"))));
        return queued &&
               waitUntil([&]()
                         { return window.hospitalPage()->isReady() && !window.hospitalPage()->isBusy(); });
    }
    QThread worker;
    HospitalStore *store;
};
class ProductProcess : public QProcess
{
  public:
    ~ProductProcess()
    {
        if (state() == QProcess::NotRunning)
        {
            return;
        }
        terminate();
        if (!waitUntil([this]() { return state() == QProcess::NotRunning; }, 10000))
        {
            kill();
            waitForFinished(5000);
        }
    }
};
void checkDeviceBoundary(frmMain &window)
{
    for (const char *name : {"btnStart", "btnPause"})
    {
        const auto buttons = window.findChildren<QAbstractButton *>(QString::fromLatin1(name));
        QVERIFY2(!buttons.isEmpty(), name);
        for (QAbstractButton *button : buttons)
        {
            QVERIFY2(!button->isEnabled(), name);
        }
    }
    for (const char *name : {"btnShutDown", "btnCloseBeep", "btnAbnormal", "btnLis"})
    {
        auto *button = find<CusBadgeButton>(&window, name);
        QVERIFY2(button && !button->isEnabled(), name);
    }
    for (const char *name : {"lblCupHave", "lblWasteCup", "lblTemp", "lblWaterHave", "lblWasteWater"})
    {
        auto *label = find<QLabel>(&window, name);
        QVERIFY2(label && !label->isEnabled(), name);
    }
    if (auto *page = window.instrumentPage())
    {
        int deviceActions = 0;
        for (QAbstractButton *button : page->findChildren<QAbstractButton *>())
        {
            if (!button->property("requiresInstrument").toBool())
            {
                continue;
            }
            ++deviceActions;
            QVERIFY2(!button->isEnabled(), qPrintable(button->objectName()));
        }
        QVERIFY(deviceActions > 0);
        auto *sensor = find<QLabel>(page, "lbl6SNeedle_sensor");
        QVERIFY(sensor);
        QCOMPARE(sensor->text(), QStringLiteral("未知"));
    }
}
} // namespace

class StatusLoopbackShellTest : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase();
    void mappingAndDeviceBoundary();
    void controllerAndBackgroundLifecycle();
    void fullProductAndMutualExclusion();

  private:
    QString evidence_, runtime_, app_, fixture_;
    QByteArray appHash_;
};

void StatusLoopbackShellTest::initTestCase()
{
    evidence_ = qEnvironmentVariable("SN100_LOOPBACK_EVIDENCE_DIR");
    runtime_ = qEnvironmentVariable("SN100_LOOPBACK_TEST_ROOT");
    app_ = qEnvironmentVariable("SN100_LOOPBACK_APP");
    appHash_ = qgetenv("SN100_LOOPBACK_APP_SHA256").toLower();
    fixture_ = qEnvironmentVariable("SN100_LOOPBACK_REPLAY_FIXTURE");
    QVERIFY(QDir::isAbsolutePath(evidence_) && !QFileInfo::exists(evidence_));
    QVERIFY(QDir::isAbsolutePath(runtime_) && !QFileInfo::exists(runtime_));
    QVERIFY(QDir::isAbsolutePath(app_) && QFileInfo(app_).isExecutable());
    QVERIFY(QDir::isAbsolutePath(fixture_) && QFileInfo(fixture_).isFile());
    QCOMPARE(appHash_.size(), 64);
    QCOMPARE(fileHash(app_), appHash_);
    QVERIFY(QDir().mkpath(evidence_) && QDir().mkpath(runtime_));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
    StatusLoopbackWorker::registerTypes();
    qInfo() << "environment" << qVersion() << QGuiApplication::platformName() << "app_sha256" << appHash_;
}

void StatusLoopbackShellTest::mappingAndDeviceBoundary()
{
    HospitalFixture hospital;
    auto *window = new frmMain(hospital.store);
    QScrollArea host;
    host.setWidget(window);
    host.setWidgetResizable(false);
    window->attachInstrumentPage(new InstrumentPage(nullptr));
    QVERIFY(hospital.initialize(*window, QDir(runtime_).filePath(QStringLiteral("mapping_hospital"))));
    QVERIFY(window->selectMainPage(7));
    auto *label = find<QLabel>(window, "lblDivState");
    QVERIFY(label);
    const QRect original = label->geometry();
    const AccountSession identity = window->accountSession();
    LoopbackStatusState state;
    state.runId = 7;
    state.revision = 1;
    state.running = state.connected = state.fresh = state.hasSnapshot = true;
    state.connectionGeneration = 2;
    state.port = 49152; // Typed display fixture only; the actual controller chooses LocalHost:0.
    state.linkState = QStringLiteral("has-simulated-status");
    state.snapshot.model = QStringLiteral("SIMULATED");
    state.snapshot.mainStatus = 2;
    state.snapshot.ledStatus = 1;
    state.snapshot.errorWorkType = 0;
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/准备就绪"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#38BF65")));
    QVERIFY(label->toolTip().contains(QStringLiteral("127.0.0.1:49152")));
    QVERIFY(label->toolTip().contains(QStringLiteral("真实仪器状态未知")));
    QVERIFY(!label->toolTip().contains(QStringLiteral("文件回放")));
    QCOMPARE(label->geometry(), original);
    auto *page = window->instrumentPage();
    QCOMPARE(labelText(page, "lblDriverModel"), QStringLiteral("模拟/SIMULATED"));
    QCOMPARE(labelText(page, "lbl6Main_emergencyCover_2"), QStringLiteral("模拟/准备就绪"));
    QCOMPARE(labelText(page, "lbl6Main_startButton_3"), QStringLiteral("模拟/未测试"));
    QCOMPARE(labelText(page, "lbl6Main_plateButton_5"), QStringLiteral("模拟/无错误"));
    checkDeviceBoundary(*window);
    QCOMPARE(window->accountSession().name, identity.name);
    QCOMPARE(window->accountSession().role, identity.role);
    QCOMPARE(window->accountSession().authenticated, identity.authenticated);
    QCOMPARE(window->accountSession().actionReady, identity.actionReady);

    state.snapshot.mainStatus = 5;
    state.snapshot.ledStatus = 5;
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/暂停中"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("42,118,242")));
    host.resize(1600, 900);
    host.show();
    QTest::qWait(100);
    QVERIFY(host.grab().save(QDir(evidence_).filePath(QStringLiteral("loopback_mapping_1600.png"))));
    host.resize(1920, 1080);
    QTest::qWait(100);
    QVERIFY(host.grab().save(QDir(evidence_).filePath(QStringLiteral("loopback_mapping_1920.png"))));
    state.snapshot.mainStatus = 6;
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/已暂停"));
    state.fresh = state.hasSnapshot = false;
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/未知"));
    QCOMPARE(labelText(page, "lblDriverModel"), QStringLiteral("未知"));
    QVERIFY(!page->property("instrumentStatusFresh").toBool());
    state.fresh = state.hasSnapshot = true;
    state.error = QStringLiteral("匿名致命失败");
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/失败"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#DB4437")));
    QVERIFY(!window->property("statusLoopbackFresh").toBool());
    QCOMPARE(labelText(page, "lblDriverModel"), QStringLiteral("未知"));
    state.error.clear();
    state.traceOverflow = true;
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/记录超限"));
    QVERIFY(!window->property("statusLoopbackFresh").toBool());
    QCOMPARE(labelText(page, "lblDriverModel"), QStringLiteral("未知"));
    state.traceOverflow = false;
    state.running = state.connected = false;
    state.linkState = QStringLiteral("stopped");
    window->setLoopbackStatusState(state);
    QCOMPARE(window->property("statusLoopbackText").toString(), QStringLiteral("模拟/已停止"));
    QVERIFY(!window->property("statusLoopbackFresh").toBool());
    checkDeviceBoundary(*window);
    QCOMPARE(window->accountSession().name, identity.name);
    QCOMPARE(window->accountSession().role, identity.role);
    QCOMPARE(window->accountSession().authenticated, identity.authenticated);
    QCOMPARE(window->accountSession().actionReady, identity.actionReady);
    QVERIFY(writeJson(QDir(evidence_).filePath(QStringLiteral("mapping.json")),
                      {{"source", "Typed anonymous state fixture, not a network observation"},
                       {"original_label_geometry_preserved", true},
                       {"known_instrument_fields", 4},
                       {"sensor_mapping_inferred", false},
                       {"device_actions_enabled", false}}));
}

void StatusLoopbackShellTest::controllerAndBackgroundLifecycle()
{
    HospitalFixture hospital;
    auto *window = new frmMain(hospital.store);
    QScrollArea host;
    host.setWidget(window);
    host.resize(1600, 900);
    host.show();
    window->attachInstrumentPage(new InstrumentPage(nullptr));
    QVERIFY(hospital.initialize(*window, QDir(runtime_).filePath(QStringLiteral("controller_hospital"))));
    QVERIFY(window->selectMainPage(7));
    const AccountSession identity = window->accountSession();
    StatusLoopbackController controller;
    connect(&controller, &StatusLoopbackController::snapshotChanged, window,
            &frmMain::setLoopbackStatusState);
    QSignalSpy errors(&controller, &StatusLoopbackController::error);
    QSignalSpy started(&controller, &StatusLoopbackController::started);
    QSignalSpy stopped(&controller, &StatusLoopbackController::stopped);
    QVector<LoopbackWireEvent> wires;
    bool wireOverflow = false;
    connect(&controller, &StatusLoopbackController::wireObserved, window,
            [&](LoopbackWireEvent event)
            {
                if (wires.size() >= 512)
                {
                    wireOverflow = true;
                }
                else
                {
                    wires.append(event);
                }
            });
    controller.start();
    QVERIFY(waitUntil([&]()
                      { return controller.current().fresh && controller.current().requestsSucceeded > 0; }));
    QCOMPARE(started.size(), 1);
    QVERIFY(errors.isEmpty());
    const auto first = controller.current();
    QVERIFY(first.running && first.connected && first.port > 0 && first.hasSnapshot);
    QCOMPARE(first.rawQj, statusMessage);
    QVERIFY(first.hasRequestResult && first.lastRequestSuccess);
    QVERIFY(window->property("statusLoopbackFresh").toBool());
    checkDeviceBoundary(*window);
    QVERIFY(window->selectMainPage(6));
    QVERIFY(QMetaObject::invokeMethod(window->hospitalPage(), "statusChanged", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("匿名医院后台状态")), Q_ARG(bool, true)));
    auto *label = find<QLabel>(window, "lblDivState");
    QVERIFY(label && label->toolTip().contains(QStringLiteral("匿名医院后台状态")));
    QVERIFY(waitUntil([&]() { return controller.current().observations > first.observations; }, 4000));
    QVERIFY(label->toolTip().contains(QStringLiteral("匿名医院后台状态")));
    QVERIFY(!label->toolTip().contains(QStringLiteral("本机模拟端")));
    QVERIFY(window->selectMainPage(7));
    QVERIFY(label->toolTip().contains(QStringLiteral("本机模拟端")));
    const quint64 generation = controller.current().connectionGeneration;
    controller.reconnect();
    QVERIFY(waitUntil(
        [&]()
        { return controller.current().fresh && controller.current().connectionGeneration > generation; }));
    QCOMPARE(controller.current().runId, first.runId);
    const quint64 reconnected = controller.current().connectionGeneration;
    controller.stop();
    QVERIFY(waitUntil(
        [&]() { return stopped.size() == 1 && !controller.isRunning() && !controller.isStopping(); }));
    QVERIFY(!controller.current().fresh && !controller.current().hasSnapshot);
    QVERIFY(!window->property("statusLoopbackFresh").toBool());
    QCOMPARE(labelText(window->instrumentPage(), "lblDriverModel"), QStringLiteral("未知"));
    QTest::qWait(150);
    QVERIFY(!controller.current().fresh);
    controller.start();
    QVERIFY(
        waitUntil([&]() { return controller.current().fresh && controller.current().runId > first.runId; }));
    QCOMPARE(started.size(), 2);
    const auto restarted = controller.current();
    QVERIFY(restarted.hasRequestResult && restarted.lastRequestSuccess);
    QCOMPARE(restarted.rawQj, statusMessage);
    controller.stop();
    QVERIFY(waitUntil([&]() { return stopped.size() == 2 && !controller.isRunning(); }));
    QVERIFY(controller.shutdownAndWait());
    QVERIFY(!controller.current().fresh && !controller.current().connected);
    QVERIFY(errors.isEmpty() && !wireOverflow && !wires.isEmpty());
    checkDeviceBoundary(*window);
    QCOMPARE(window->accountSession().name, identity.name);
    QCOMPARE(window->accountSession().role, identity.role);
    QCOMPARE(window->accountSession().authenticated, identity.authenticated);
    QCOMPARE(window->accountSession().actionReady, identity.actionReady);

    QMap<QString, QByteArray> streams;
    QJsonArray wireJson;
    for (const auto &event : wires)
    {
        const QString key =
            QStringLiteral("%1/%2/%3").arg(event.runId).arg(event.connectionGeneration).arg(event.direction);
        streams[key] += event.bytes;
        wireJson.append(QJsonObject{{"run_id", double(event.runId)},
                                    {"generation", double(event.connectionGeneration)},
                                    {"sequence", double(event.sequence)},
                                    {"elapsed_ms", double(event.elapsedMs)},
                                    {"direction", event.direction},
                                    {"hex", QString::fromLatin1(event.bytes.toHex(' ').toUpper())}});
    }
    int txStreams = 0, rxStreams = 0;
    for (auto it = streams.cbegin(); it != streams.cend(); ++it)
    {
        const bool tx = it.key().endsWith(QStringLiteral("/TX"));
        const QByteArray expected = tx ? queryFrame : statusFrame;
        QVERIFY(!it.value().isEmpty() && it.value().size() % expected.size() == 0);
        QCOMPARE(it.value(), expected.repeated(it.value().size() / expected.size()));
        if (tx)
        {
            ++txStreams;
        }
        else
        {
            ++rxStreams;
        }
    }
    QVERIFY(txStreams >= 3 && rxStreams >= 3);
    QVERIFY(writeJson(QDir(evidence_).filePath(QStringLiteral("controller_wire.json")),
                      {{"source", "Actual self-owned LocalHost:0 TCP observations, no external endpoint"},
                       {"first_run", double(first.runId)},
                       {"reconnected_generation", double(reconnected)},
                       {"restart_run", double(restarted.runId)},
                       {"raw_qj", QString::fromLatin1(statusMessage)},
                       {"background_observations_continued", true},
                       {"hospital_status_not_overwritten", true},
                       {"stopped_without_stale_status", true},
                       {"request_terminal_success", true},
                       {"wire", wireJson}}));
}

void StatusLoopbackShellTest::fullProductAndMutualExclusion()
{
    QTemporaryFile secret(QDir(runtime_).filePath(QStringLiteral("bootstrap_XXXXXX.secret")));
    QVERIFY(secret.open());
    const QByteArray value = QUuid::createUuid().toByteArray();
    QCOMPARE(secret.write(value), qint64(value.size()));
    QVERIFY(secret.flush());
    secret.close();
    ProductProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(app_, {"--offline", "--accounts", "--history", "--catalog", "--samples", "--instrument",
                         "--status-loopback", "--seed-anonymous", "--seed-catalog-demo", "--test-secret-file",
                         secret.fileName(), "--data-dir",
                         QDir(runtime_).filePath(QStringLiteral("full_product")), "--viewport", "1600x900"});
    QVERIFY(process.waitForStarted(10000));
    QByteArray output;
    const auto collect = [&]()
    {
        output += process.readAll();
        return output.contains("SHELL_READY") && output.contains("STATUS_LOOPBACK_STARTED") &&
               output.contains("STATUS_LOOPBACK_RX");
    };
    const bool observed = waitUntil(collect, 25000);
    QVERIFY(secret.remove());
    process.terminate();
    const bool stopped = waitUntil(
        [&]()
        {
            collect();
            return process.state() == QProcess::NotRunning;
        },
        15000);
    if (!stopped)
    {
        process.kill();
        process.waitForFinished(5000);
    }
    collect();
    const bool secretLeaked = output.contains(value);
    QByteArray safeOutput = output;
    safeOutput.replace(value, QByteArray("<redacted-bootstrap-secret>"));
    QFile log(QDir(evidence_).filePath(QStringLiteral("product.log")));
    QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(log.write(safeOutput), qint64(safeOutput.size()));
    log.close();
    QVERIFY2(observed && stopped, safeOutput.constData());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY(!secretLeaked);
    QVERIFY(output.indexOf("SHELL_READY") < output.indexOf("STATUS_LOOPBACK_STARTED"));
    QVERIFY(output.contains("STATUS_LOOPBACK_TX") && output.contains("self_owned_peer=127.0.0.1"));
    for (const auto *marker : {"STATUS_LOOPBACK_STOPPED true", "SAMPLE_STORE_CLOSED", "PROJECT_STORE_CLOSED",
                               "REAGENT_STORE_CLOSED", "HISTORY_STORE_CLOSED", "ACCOUNT_STORE_CLOSED",
                               "HOSPITAL_STORE_CLOSED", "MAINTENANCE_STORE_CLOSED", "THREADS_STOPPED true"})
    {
        QVERIFY2(output.contains(marker), marker);
    }
    QCOMPARE(fileHash(app_), appHash_);

    const QString rejectedRoot = QDir(runtime_).filePath(QStringLiteral("must_not_initialize"));
    ProductProcess invalid;
    invalid.setProcessChannelMode(QProcess::MergedChannels);
    invalid.start(app_, {"--offline", "--accounts", "--history", "--catalog", "--samples", "--instrument",
                         "--status-loopback", "--status-replay", fixture_, "--data-dir", rejectedRoot});
    QVERIFY(invalid.waitForStarted(10000));
    QVERIFY(waitUntil([&]() { return invalid.state() == QProcess::NotRunning; }));
    QCOMPARE(invalid.exitStatus(), QProcess::NormalExit);
    QCOMPARE(invalid.exitCode(), 2);
    const QByteArray rejected = invalid.readAll();
    QVERIFY(rejected.contains("mutually exclusive"));
    QVERIFY(!QFileInfo::exists(rejectedRoot));
    QVERIFY(writeJson(QDir(evidence_).filePath(QStringLiteral("product_lifecycle.json")),
                      {{"application_sha256", QString::fromLatin1(appHash_)},
                       {"normal_product_exit", 0},
                       {"loopback_stopped", true},
                       {"stores_closed", 7},
                       {"secret_removed_and_not_logged", true},
                       {"mutually_exclusive_exit", 2},
                       {"rejected_runtime_created", false}}));
}

QTEST_MAIN(StatusLoopbackShellTest)
#include "status_loopback_shell_test.moc"
