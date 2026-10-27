#include <QtTest>
#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QDateEdit>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>
#include "data/hospital_store.h"
#include "instrument/instrument_page.h"
#include "instrument/maintenance_store.h"
#include "protocol/status_replay.h"
#include "shell/frmmain.h"
#include "ui/cusmessagebox.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

namespace
{
template<class T> T *control(QObject *root, const char *name)
{
    return root->findChild<T *>(QString::fromLatin1(name));
}

bool until(const std::function<bool()> &predicate, int timeout = 10000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout) QTest::qWait(10);
    return predicate();
}

AccountSession identity(int role)
{
    AccountSession session;
    session.role = role;
    session.authenticated = role != 0;
    session.actionReady = true;
    if (session.authenticated) session.name = QStringLiteral("anonymous_maintenance");
    return session;
}

// Click the actual original confirmation buttons after the mask animation.
class DialogAnswers : public QObject
{
public:
    DialogAnswers()
    {
        timer.setInterval(5);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            auto *box = qobject_cast<CusMessageBox *>(QApplication::activeModalWidget());
            if (!box || !box->isReadyForInput()) return;
            auto *cancel = control<QPushButton>(box, "btnCancel");
            const bool question = cancel && cancel->isVisible();
            auto *button = control<QPushButton>(box,
                question && answer == QMessageBox::No ? "btnCancel" : "btnSure");
            if (!button) return;
            if (question) ++questions;
            answer = QMessageBox::Yes;
            QTest::mouseClick(button, Qt::LeftButton);
        });
        timer.start();
    }
    QTimer timer;
    QMessageBox::StandardButton answer = QMessageBox::Yes;
    int questions = 0;
};

class PageFixture
{
public:
    explicit PageFixture(const QString &directory) : store(new MaintenanceStore), page(new InstrumentPage(store))
    {
        MaintenanceStore::registerTypes();
        store->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        QObject::connect(&worker, &QThread::started, store, [this, directory]()
        { store->initialize(directory); });
        QObject::connect(store, &MaintenanceStore::ready, page, [this]() { ready = true; });
        host.setWidget(page);
        host.setWidgetResizable(false);
        host.resize(1600,900);
        host.show();
        worker.start();
    }
    ~PageFixture()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        if (!worker.wait(5000)) qFatal("Maintenance test worker did not stop");
        host.takeWidget();
        delete page;
    }
    bool waitReady()
    {
        return until([this]() { return ready && !page->isBusy(); });
    }
    void login(int role)
    {
        page->setSession(identity(role));
        // A queued no-op after setSession ensures the store has seen it too.
        QMetaObject::invokeMethod(store, []() {}, Qt::BlockingQueuedConnection);
    }
    QThread worker;
    MaintenanceStore *store;
    InstrumentPage *page;
    QScrollArea host;
    bool ready = false;
};

class ShellFixture
{
public:
    explicit ShellFixture(const QString &directory) : hospital(new HospitalStore), accounts(new AccountStore),
        maintenance(new MaintenanceStore), shell(new frmMain(hospital,nullptr,accounts)),
        page(new InstrumentPage(maintenance))
    {
        HospitalStore::registerTypes();
        AccountStore::registerTypes();
        MaintenanceStore::registerTypes();
        shell->setDataDirectory(QDir(directory).filePath(QStringLiteral("hospital")));
        shell->attachInstrumentPage(page);
        host.setWidget(shell);
        host.setWidgetResizable(false);
        host.resize(1600,900);
        host.show();
        const QList<QPair<QObject *,QThread *>> stores = {{hospital,&hospitalThread},
            {accounts,&accountThread},{maintenance,&maintenanceThread}};
        for (const auto &store : stores)
        {
            store.first->moveToThread(store.second);
            QObject::connect(store.second,&QThread::finished,store.first,&QObject::deleteLater);
        }
        QObject::connect(&hospitalThread,&QThread::started,hospital,[this,directory]()
        {
            const QDir root(QDir(directory).filePath(QStringLiteral("hospital")));
            hospital->initialize(root.filePath(QStringLiteral("hospital_Information.db")),root.filePath(QStringLiteral("config.ini")));
        });
        const QString transientSecret = QUuid::createUuid().toString();
        QObject::connect(&accountThread,&QThread::started,accounts,[this,directory,transientSecret]()
        { accounts->initialize(QDir(directory).filePath(QStringLiteral("account")),transientSecret); });
        QObject::connect(&maintenanceThread,&QThread::started,maintenance,[this,directory]()
        { maintenance->initialize(QDir(directory).filePath(QStringLiteral("instrument"))); });
        QObject::connect(maintenance,&MaintenanceStore::ready,page,[this]() { maintenanceReady = true; });
        hospitalThread.start();accountThread.start();maintenanceThread.start();
    }
    ~ShellFixture()
    {
        const QList<QPair<QObject *,QThread *>> stores = {{maintenance,&maintenanceThread},
            {accounts,&accountThread},{hospital,&hospitalThread}};
        for (const auto &store : stores)
        {
            QMetaObject::invokeMethod(store.first,"close",Qt::BlockingQueuedConnection);
            store.second->quit();
            if (!store.second->wait(5000)) qFatal("Instrument shell test worker did not stop");
        }
        host.takeWidget();delete shell;
    }
    bool ready()
    {
        return until([this]() { return maintenanceReady && !page->isBusy()
            && shell->hospitalPage()->isReady() && !shell->hospitalPage()->isBusy()
            && shell->accountPage()->isReady() && !shell->accountPage()->isBusy(); });
    }
    QThread hospitalThread,accountThread,maintenanceThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    MaintenanceStore *maintenance;
    frmMain *shell;
    InstrumentPage *page;
    QScrollArea host;
    bool maintenanceReady = false;
};

QString cellText(QTableWidget *table, int row, int column)
{
    const auto *item = table->item(row,column);
    return item ? item->text() : QString();
}
}

class InstrumentPageTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void originalCanvasAndNavigation();
    void permissionsAndUnknownDevice();
    void manualRecordsAndReopen();
    void cancellationAndBusyGuard();
    void lockedDatabaseRecovers();
    void replayNeverEnablesHardware();
    void readOnlySourceReplacementKeepsPermissionsAndExpires();
    void shellIntegrationAndLifecycleGuards();
private:
    QString evidence_;
};

void InstrumentPageTest::initTestCase()
{
    evidence_ = QString::fromUtf8(qgetenv("SN100_INSTRUMENT_EVIDENCE_DIR"));
    QVERIFY(QDir::isAbsolutePath(evidence_));
    QVERIFY(!QFileInfo::exists(evidence_));
    QVERIFY(QDir().mkpath(evidence_));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"),11));
    QApplication::setQuitOnLastWindowClosed(false);
}

void InstrumentPageTest::originalCanvasAndNavigation()
{
    QFile form(QFINDTESTDATA("../instrument/frmdriverset.ui"));
    QVERIFY(form.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(form.readAll(),QCryptographicHash::Sha256).toHex(),
        QByteArray("4e0f24fa7e99e459a123bced7855d25399b2ce7f6e23cfb3f8cd2f36fb879f4e"));
    PageFixture fixture(QDir(evidence_).filePath(QStringLiteral("canvas")));
    QVERIFY(fixture.waitReady());
    fixture.login(5);
    auto *page = fixture.page;
    QCOMPARE(page->size(),QSize(1751,968));
    QCOMPARE(control<QWidget>(page,"driversetFunMenu")->geometry(),QRect(1525,0,226,968));
    QCOMPARE(control<QWidget>(page,"wgtDriverSetBg")->geometry(),QRect(10,10,1481,951));
    auto *stack = control<QStackedWidget>(page,"stackedWidget");
    QVERIFY(stack);
    QCOMPARE(stack->count(),8);
    QCOMPARE(stack->currentIndex(),1);
    const QList<QPair<const char *,int>> destinations = {{"btnPort",0},{"btnStatus",1},
        {"btnMaintain",2},{"btnVersion",3},{"btn_board_status",4},
        {"btnFunTest",6},{"btn_CommunicationTest",7}};
    QElapsedTimer elapsed;
    qint64 maximumNs = 0;
    int operations = 0;
    for (const auto &destination : destinations)
    {
        auto *button = control<QAbstractButton>(page,destination.first);
        QVERIFY(button && button->isEnabled());
        const QRect geometry = button->geometry();
        elapsed.start();
        button->click();
        maximumNs = qMax(maximumNs,elapsed.nsecsElapsed());
        ++operations;
        QVERIFY(until([&]() { return !page->isBusy(); }));
        QCOMPARE(stack->currentIndex(),destination.second);
        QCOMPARE(button->geometry(),geometry);
        QVERIFY(fixture.host.grab().save(QDir(evidence_).filePath(
            QStringLiteral("page_%1.png").arg(destination.second))));
    }
    for (const char *name : {"tableDayMaintain","tableWeekMaintain","tableMonthMaintain"})
    {
        auto *table = control<QTableWidget>(page,name);
        QVERIFY(table);
        QCOMPARE(table->columnCount(),32);
        QCOMPARE(table->columnWidth(1),41);
    }
    QCOMPARE(control<QTableWidget>(page,"tableDayMaintain")->rowCount(),6);
    QCOMPARE(control<QTableWidget>(page,"tableWeekMaintain")->rowCount(),3);
    QCOMPARE(control<QTableWidget>(page,"tableMonthMaintain")->rowCount(),2);
    QFile metrics(QDir(evidence_).filePath(QStringLiteral("navigation_metrics.json")));
    QVERIFY(metrics.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    metrics.write(QJsonDocument(QJsonObject{{"operations",operations},{"maximum_sync_click_ns",double(maximumNs)},
        {"scope","Synchronous navigation call only; excludes paint, physical input and instrument latency"}}).toJson());
}

void InstrumentPageTest::permissionsAndUnknownDevice()
{
    PageFixture fixture(QDir(evidence_).filePath(QStringLiteral("permissions")));
    QVERIFY(fixture.waitReady());
    auto *page = fixture.page;
    for (int role : {0,1,2,3,4,5})
    {
        fixture.login(role);
        for (const char *name : {"btnPort","btnFunTest","btn_CommunicationTest"})
            QCOMPARE(!control<QWidget>(page,name)->isHidden(),role == 5);
        QCOMPARE(!control<QWidget>(page,"btn_board_status")->isHidden(),role >= 4);
        QVERIFY(control<QWidget>(page,"btnHil")->isHidden());
    }
    fixture.login(5);
    const QStringList hardwareNames = {"btnLongClean","btnTcpConnect","btnSaveTcpSet","btnPrint",
        "btnOpenBen_E","btnCloseBen_f","btnSampleWater_A1","btnSampleWater_A0",
        "btnSampleWaster_c1","btnSampleWaster_c0","btnReagent3Fa_F0","btnReagent3Fa_F1",
        "btnReagentWater_B1","btnReagentWater_B0","btnReagentWaster_e1","btnReagentWaster_e0",
        "btnClearCup_C","btnServosSample_M","btnServosReagent_N","btn_WaterEmpty"};
    for (const QString &name : hardwareNames)
    {
        auto *button = page->findChild<QAbstractButton *>(name);
        QVERIFY2(button,qPrintable(name));
        QVERIFY2(!button->isEnabled(),qPrintable(name));
        QVERIFY2(!button->toolTip().isEmpty(),qPrintable(name));
    }
    int protectedActions = 0;
    for (auto *button : page->findChildren<QAbstractButton *>())
    {
        if (!button->property("requiresInstrument").toBool()) continue;
        ++protectedActions;
        QVERIFY2(!button->isEnabled(),qPrintable(button->objectName()));
        QVERIFY2(!button->toolTip().isEmpty(),qPrintable(button->objectName()));
    }
    QVERIFY(protectedActions > 50);
    for (const char *name : {"lblReagentOpen","lblDuilentOpen","lblEmergencyOpen","lblEnvirTemp",
        "lblLeftNeedleTemp","lblRightNeedleTemp","lblRPlateTemp","lblTPlateTemp",
        "lblSamR_Ge","lblSamYW_base","lblRNeedle1_sensor","lbl6Plate_Temp",
        "lblVersionZk","lblDriverModel","label_isNew_3","label_isNew_4","lbl6Main_machineLight"})
    {
        auto *label = control<QLabel>(page,name);
        QVERIFY2(label,name);
        QVERIFY2(label->text().contains(QStringLiteral("未知")),qPrintable(QString::fromLatin1(name)+":"+label->text()));
    }
    // Read-only configuration fields must not seed an old external endpoint.
    for (const char *name : {"lblTcpIp","lblTcpPort"})
    {
        auto *edit = control<QLineEdit>(page,name);
        QVERIFY(edit);
        QVERIFY(!edit->isEnabled() || edit->isReadOnly());
        QVERIFY(!edit->text().contains(QStringLiteral("192.168")));
    }
}

void InstrumentPageTest::manualRecordsAndReopen()
{
    const QString directory = QDir(evidence_).filePath(QStringLiteral("manual"));
    const int day = QDate::currentDate().day();
    {
        PageFixture fixture(directory);
        DialogAnswers answers;
        QVERIFY(fixture.waitReady());
        fixture.login(1);
        control<QAbstractButton>(fixture.page,"btnMaintain")->click();
        QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
        const QList<QPair<const char *,QPair<const char *,int>>> actions = {
            {"btnCleanCup",{"tableDayMaintain",1}}, {"btnWasteWater",{"tableDayMaintain",2}},
            {"btnWasteCup",{"tableDayMaintain",3}}, {"btnCleanPool",{"tableWeekMaintain",1}},
            {"btnCleanDriver",{"tableWeekMaintain",2}}, {"btnCleanNeedle",{"tableMonthMaintain",1}}};
        QSignalSpy completed(fixture.page,&InstrumentPage::operationFinished);
        for (const auto &action : actions)
        {
            auto *button = control<QAbstractButton>(fixture.page,action.first);
            QVERIFY(button && button->isEnabled());
            const int before = completed.count();
            button->click();
            QVERIFY(until([&]() { return completed.count()>before && !fixture.page->isBusy(); }));
            QVERIFY(completed.last().at(1).toBool());
            const auto *table = control<QTableWidget>(fixture.page,action.second.first);
            QVERIFY(table->item(action.second.second,day));
            QCOMPARE(table->item(action.second.second,day)->text(),QString::fromUtf8("✔"));
        }
        QCOMPARE(answers.questions,6);
        QVERIFY(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),4,day).isEmpty());
        QVERIFY(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),5,day).isEmpty());
        auto *date = control<QDateEdit>(fixture.page,"dateSelect");
        QVERIFY(date);
        date->setDate(QDate::currentDate().addMonths(-1));
        QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
        QVERIFY(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,day).isEmpty());
        date->setDate(QDate::currentDate());
        QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
        QCOMPARE(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,day),QString::fromUtf8("✔"));
        QVERIFY(fixture.host.grab().save(QDir(evidence_).filePath(QStringLiteral("maintenance_saved.png"))));
    }
    PageFixture reopened(directory);
    QVERIFY(reopened.waitReady());
    QCOMPARE(cellText(control<QTableWidget>(reopened.page,"tableDayMaintain"),1,day),QString::fromUtf8("✔"));
    QCOMPARE(cellText(control<QTableWidget>(reopened.page,"tableWeekMaintain"),2,day),QString::fromUtf8("✔"));
    QCOMPARE(cellText(control<QTableWidget>(reopened.page,"tableMonthMaintain"),1,day),QString::fromUtf8("✔"));
}

void InstrumentPageTest::cancellationAndBusyGuard()
{
    PageFixture fixture(QDir(evidence_).filePath(QStringLiteral("cancel")));
    DialogAnswers answers;
    QVERIFY(fixture.waitReady());
    fixture.login(1);
    control<QAbstractButton>(fixture.page,"btnMaintain")->click();
    QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
    QSignalSpy saved(fixture.store,&MaintenanceStore::succeeded);
    answers.answer = QMessageBox::No;
    control<QAbstractButton>(fixture.page,"btnCleanCup")->click();
    QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
    QCOMPARE(saved.count(),0);
    QVERIFY(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,QDate::currentDate().day()).isEmpty());
    // Hold the worker briefly to make an actual queued read observable.
    QVERIFY(QMetaObject::invokeMethod(fixture.store, []() { QThread::msleep(150); },Qt::QueuedConnection));
    auto *date = control<QDateEdit>(fixture.page,"dateSelect");
    date->setDate(QDate::currentDate().addMonths(-1));
    QVERIFY(fixture.page->isBusy());
    QVERIFY(!fixture.page->prepareToLeave());
    QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
    QVERIFY(fixture.page->prepareToLeave());
    fixture.login(0);
    QVERIFY(!control<QAbstractButton>(fixture.page,"btnCleanCup")->isEnabled());
}

void InstrumentPageTest::lockedDatabaseRecovers()
{
    const QString directory = QDir(evidence_).filePath(QStringLiteral("locked"));
    PageFixture fixture(directory);
    DialogAnswers answers;
    QVERIFY(fixture.waitReady());
    fixture.login(1);
    control<QAbstractButton>(fixture.page,"btnMaintain")->click();
    QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
    const QString connection = QStringLiteral("instrument_test_lock");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),connection);
        database.setDatabaseName(QDir(directory).filePath(QStringLiteral("maintenance.db")));
        QVERIFY2(database.open(),qPrintable(database.lastError().text()));
        QSqlQuery lock(database);
        QVERIFY2(lock.exec(QStringLiteral("BEGIN IMMEDIATE")),qPrintable(lock.lastError().text()));
        QSignalSpy completed(fixture.page,&InstrumentPage::operationFinished);
        control<QAbstractButton>(fixture.page,"btnCleanCup")->click();
        QVERIFY(until([&]() { return !completed.isEmpty() && !fixture.page->isBusy(); }));
        QVERIFY(!completed.last().at(1).toBool());
        QVERIFY(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,QDate::currentDate().day()).isEmpty());
        QVERIFY(lock.exec(QStringLiteral("ROLLBACK")));
        completed.clear();
        control<QAbstractButton>(fixture.page,"btnCleanCup")->click();
        QVERIFY(until([&]() { return !completed.isEmpty() && !fixture.page->isBusy(); }));
        QVERIFY(completed.last().at(1).toBool());
        QCOMPARE(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,QDate::currentDate().day()),QString::fromUtf8("✔"));
        // A failed historical read must clear the old month's checkmarks.
        QVERIFY(lock.exec(QStringLiteral("BEGIN EXCLUSIVE")));
        completed.clear();
        auto *date = control<QDateEdit>(fixture.page,"dateSelect");
        date->setDate(QDate::currentDate().addMonths(-1));
        QVERIFY(until([&]() { return !completed.isEmpty() && !fixture.page->isBusy(); }));
        QVERIFY(!completed.last().at(1).toBool());
        QVERIFY(!fixture.page->property("maintenanceLoaded").toBool());
        QVERIFY(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,QDate::currentDate().day()).isEmpty());
        QVERIFY(lock.exec(QStringLiteral("ROLLBACK")));
        completed.clear();
        date->setDate(QDate::currentDate());
        QVERIFY(until([&]() { return !completed.isEmpty() && !fixture.page->isBusy(); }));
        QVERIFY(completed.last().at(1).toBool());
        QVERIFY(fixture.page->property("maintenanceLoaded").toBool());
        QCOMPARE(cellText(control<QTableWidget>(fixture.page,"tableDayMaintain"),1,QDate::currentDate().day()),QString::fromUtf8("✔"));
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
}

void InstrumentPageTest::replayNeverEnablesHardware()
{
    PageFixture fixture(QDir(evidence_).filePath(QStringLiteral("replay")));
    QVERIFY(fixture.waitReady());
    fixture.login(5);
    Sn100Protocol::ReplayState state;
    state.loaded = state.fresh = state.hasSnapshot = true;
    state.snapshot.model = QStringLiteral("SN600");
    state.snapshot.mainStatus = 2;
    state.snapshot.ledStatus = 1;
    state.snapshot.errorWorkType = 0;
    state.action = QStringLiteral("rx");
    fixture.page->setReplayState(state);
    QVERIFY(fixture.page->property("instrumentReplayFresh").toBool());
    QCOMPARE(control<QLabel>(fixture.page,"lblDriverModel")->text(),QStringLiteral("回放/SN600"));
    QCOMPARE(control<QLabel>(fixture.page,"lbl6Main_emergencyCover_2")->text(),QStringLiteral("回放/准备就绪"));
    control<QAbstractButton>(fixture.page,"btn_board_status")->click();
    QCOMPARE(control<QStackedWidget>(fixture.page,"stackedWidget")->currentIndex(),5);
    QVERIFY(!control<QAbstractButton>(fixture.page,"btnTcpConnect")->isEnabled());
    QVERIFY(!control<QAbstractButton>(fixture.page,"btnLongClean")->isEnabled());
    for (const char *name : {"lblEnvirTemp","lblLeftNeedleTemp","lblSamR_Ge","lbl6Plate_Temp"})
        QVERIFY2(control<QLabel>(fixture.page,name)->text().contains(QStringLiteral("未知")),name);
    state.fresh = state.hasSnapshot = false;
    state.finished = true;
    fixture.page->setReplayState(state);
    QVERIFY(!fixture.page->property("instrumentReplayFresh").toBool());
    for (const char *name : {"lblDriverModel","lbl6Main_emergencyCover_2","lbl6Main_emergencyCover_7"})
        QVERIFY2(control<QLabel>(fixture.page,name)->text().contains(QStringLiteral("未知")),name);
    QVERIFY(fixture.host.grab().save(QDir(evidence_).filePath(QStringLiteral("replay_expired.png"))));
}

void InstrumentPageTest::readOnlySourceReplacementKeepsPermissionsAndExpires()
{
    PageFixture fixture(QDir(evidence_).filePath(QStringLiteral("readonly_source")));
    QVERIFY(fixture.waitReady());
    auto *page = fixture.page;
    QSignalSpy saved(fixture.store,&MaintenanceStore::succeeded);
    QList<QAbstractButton *> protectedActions;
    for (auto *button : page->findChildren<QAbstractButton *>())
        if (button->property("requiresInstrument").toBool()) protectedActions.append(button);
    QVERIFY(protectedActions.size() > 50);

    Sn100Protocol::ReplayState replay;
    replay.loaded = replay.fresh = replay.hasSnapshot = true;
    replay.sourceTimeMs = 250;
    replay.snapshot.model = QStringLiteral("SN600");
    replay.snapshot.mainStatus = 2;
    replay.snapshot.ledStatus = 1;
    replay.snapshot.errorWorkType = 0;
    Sn100Protocol::StatusSnapshot simulated = replay.snapshot;
    simulated.model = QStringLiteral("SN450");
    simulated.mainStatus = 5;
    // Plausible raw temperatures/flags in unknown subsystem fields must never
    // become a live measurement through a guessed board/field mapping.
    simulated.subsystems.append({73,{QStringLiteral("99.9"),QStringLiteral("1"),QStringLiteral("0")}});
    const QString prefix = QStringLiteral("模拟");
    const QString source = QStringLiteral("本机回环模拟，只读状态；真实仪器未知。连接代次2。");

    for (int role : {0,4})
    {
        fixture.login(role);
        QList<QPair<QWidget *,QPair<bool,bool>>> permissions;
        for (const char *name : {"btnPort","btnFunTest","btn_CommunicationTest","btn_board_status",
             "btnCleanCup","btnCleanNeedle","dateSelect"})
        {
            auto *widget = control<QWidget>(page,name);
            QVERIFY2(widget,name);
            permissions.append({widget,{widget->isHidden(),widget->isEnabled()}});
        }
        page->setReplayState(replay);
        QVERIFY(page->property("instrumentReplayFresh").toBool());
        QVERIFY(page->property("instrumentStatusFresh").toBool());
        auto *model = control<QLabel>(page,"lblDriverModel");
        auto *main = control<QLabel>(page,"lbl6Main_emergencyCover_2");
        QVERIFY(model && main);
        QCOMPARE(model->text(),QStringLiteral("回放/SN600"));
        QVERIFY(model->toolTip().contains(QStringLiteral("文件回放")));

        page->setReadOnlyStatus(simulated,true,prefix,source);
        QVERIFY(page->property("instrumentStatusFresh").toBool());
        QCOMPARE(model->text(),QStringLiteral("模拟/SN450"));
        QCOMPARE(main->text(),QStringLiteral("模拟/测试中"));
        QCOMPARE(model->toolTip(),source);
        QCOMPARE(main->toolTip(),source);
        QVERIFY(!model->toolTip().contains(QStringLiteral("回放")));
        for (const char *name : {"lblEnvirTemp","lblLeftNeedleTemp","lblSamR_Ge","lblSamYW_base",
             "lblRNeedle1_sensor","lbl6Plate_Temp","lblVersionZk"})
        {
            auto *label = control<QLabel>(page,name);
            QVERIFY2(label,name);
            QVERIFY2(label->text().contains(QStringLiteral("未知")),name);
        }
        for (auto *button : protectedActions)
        {
            QVERIFY2(!button->isEnabled(),qPrintable(button->objectName()));
            QVERIFY2(!button->toolTip().isEmpty(),qPrintable(button->objectName()));
        }

        // A dropped/expired source withdraws its old values even when its last
        // snapshot still contains a ready/running state and known model.
        page->setReadOnlyStatus(simulated,false,prefix,source);
        QVERIFY(!page->property("instrumentStatusFresh").toBool());
        for (const char *name : {"lblDriverModel","lbl6Main_emergencyCover_2",
             "lbl6Main_emergencyCover_7","lbl6Main_startButton_3","lbl6Main_plateButton_5"})
        {
            auto *label = control<QLabel>(page,name);
            QVERIFY2(label,name);
            QVERIFY2(label->text().contains(QStringLiteral("未知")),name);
            QVERIFY2(!label->toolTip().contains(QStringLiteral("连接代次2")),name);
        }
        for (const auto &permission : permissions)
        {
            QCOMPARE(permission.first->isHidden(),permission.second.first);
            QCOMPARE(permission.first->isEnabled(),permission.second.second);
        }
        for (auto *button : protectedActions)
            QVERIFY2(!button->isEnabled(),qPrintable(button->objectName()));
    }
    QCOMPARE(saved.count(),0);
    replay.fresh = replay.hasSnapshot = false;
    replay.finished = true;
    page->setReplayState(replay);
    QVERIFY(!page->property("instrumentReplayFresh").toBool());
    QVERIFY(!page->property("instrumentStatusFresh").toBool());
}

void InstrumentPageTest::shellIntegrationAndLifecycleGuards()
{
    ShellFixture fixture(QDir(evidence_).filePath(QStringLiteral("shell")));
    QVERIFY(fixture.ready());
    QCOMPARE(fixture.shell->instrumentPage(),fixture.page);
    QVERIFY(fixture.shell->selectMainPage(7));
    QCOMPARE(fixture.shell->currentMainPage(),7);
    for (auto *label : fixture.shell->findChildren<QLabel *>())
        QVERIFY(!label->isVisible() || !label->text().contains(QStringLiteral("尚未接入（脱机）")));
    QVERIFY(control<QWidget>(fixture.page,"btnFunTest")->isHidden());
    QVERIFY(QMetaObject::invokeMethod(fixture.accounts,"setSession",Qt::QueuedConnection,
        Q_ARG(QString,QStringLiteral("offlineAdmin")),Q_ARG(bool,true)));
    QVERIFY(until([&]() { return fixture.shell->accountSession().authenticated
        && !control<QWidget>(fixture.page,"btnFunTest")->isHidden(); }));
    QSignalSpy rejected(fixture.shell,&frmMain::navigationRejected);
    connect(fixture.shell,&frmMain::navigationRejected,this,[](const QString &reason)
    { qInfo().noquote() << "INSTRUMENT_NAVIGATION_REJECTED" << reason; });
    QVERIFY(QMetaObject::invokeMethod(fixture.maintenance,[]() { QThread::msleep(150); },Qt::QueuedConnection));
    control<QDateEdit>(fixture.page,"dateSelect")->setDate(QDate::currentDate().addMonths(-1));
    QVERIFY(fixture.page->isBusy());
    QVERIFY(!fixture.shell->selectMainPage(6));
    QVERIFY(!fixture.shell->requestLogout());
    QVERIFY(!fixture.shell->close());
    QCOMPARE(fixture.shell->currentMainPage(),7);
    QVERIFY(rejected.count()>=3);
    QVERIFY(until([&]() { return !fixture.page->isBusy(); }));
    Sn100Protocol::ReplayState state;
    state.loaded = state.fresh = state.hasSnapshot = true;
    state.snapshot.model = QStringLiteral("SN600");
    state.snapshot.mainStatus = 2;state.snapshot.ledStatus = 1;state.snapshot.errorWorkType = 0;
    fixture.shell->setStatusReplayState(state);
    QVERIFY(control<QLabel>(fixture.shell,"lblDivState")->toolTip().contains(QStringLiteral("真实仪器状态未知")));
    QVERIFY(control<QLabel>(fixture.page,"lblDriverModel")->text().contains(QStringLiteral("SN600")));
    state.fresh = state.hasSnapshot = false;
    state.finished = true;
    fixture.shell->setStatusReplayState(state);
    QVERIFY(control<QLabel>(fixture.page,"lblDriverModel")->text().contains(QStringLiteral("未知")));
    QVERIFY(!control<QAbstractButton>(fixture.shell,"btnStart")->isEnabled());
    QVERIFY(fixture.host.grab().save(QDir(evidence_).filePath(QStringLiteral("integrated_instrument.png"))));
    QVERIFY(fixture.shell->requestLogout());
    QVERIFY(until([&]() { return !fixture.shell->accountSession().authenticated; }));
    QVERIFY(control<QWidget>(fixture.page,"btnFunTest")->isHidden());
    QVERIFY(fixture.shell->selectMainPage(6));
    // Clearing identity starts an asynchronous account-list refresh. The shell
    // keeps its close guard until that real read (and all other pages) settles.
    QVERIFY(until([&]() { return !fixture.shell->accountPage()->isBusy()
        && !fixture.shell->hospitalPage()->isBusy() && !fixture.page->isBusy(); }));
    QVERIFY(fixture.shell->close());
}

QTEST_MAIN(InstrumentPageTest)
#include "instrument_page_test.moc"
