#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QTableWidget>
#include <QUuid>
#include <functional>
#include <algorithm>
#include "shell/frmmain.h"
#include "data/hospital_store.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"

namespace
{
bool until(const std::function<bool()> &predicate, int limit = 15000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!predicate() && elapsed.elapsed() < limit) QTest::qWait(10);
    return predicate();
}

qint64 directoryBytes(const QString &directory)
{
    qint64 bytes=0;
    QDirIterator files(directory,QDir::Files|QDir::Hidden,QDirIterator::Subdirectories);
    while(files.hasNext()) { files.next();bytes+=files.fileInfo().size(); }
    return bytes;
}

// Exercise the real shell with five independently owned SQL workers.
// The test creates only a new anonymous runtime; account secrets stay in memory.
class CatalogSession
{
public:
    explicit CatalogSession(QString root) : hospital(new HospitalStore), accounts(new AccountStore),
        projects(new ProjectsStore), reagents(new ReagentStore), samples(new SampleStore)
    {
        HospitalStore::registerTypes();AccountStore::registerTypes();
        ProjectsStore::registerTypes();ReagentStore::registerTypes();
        SampleStore::registerTypes();
        workers = {{hospital,&hospitalThread},{accounts,&accountThread},
                   {projects,&projectThread},{reagents,&reagentThread},{samples,&sampleThread}};
        for (const auto &worker : workers)
        {
            worker.first->moveToThread(worker.second);
            QObject::connect(worker.second,&QThread::finished,worker.first,&QObject::deleteLater);
        }
        shell = new frmMain(hospital,nullptr,accounts);
        shell->setDataDirectory(QDir(root).filePath("hospital"));
        projectPage = new ProjectPage(projects);
        reagentPage = new ReagentsPage(reagents);
        shell->attachCatalogPages(projectPage,reagentPage);
        samplePage=new SamplePage(samples);
        shell->attachSamplePage(samplePage);
        host.setWidget(shell);host.resize(1600,900);host.show();
        const QString catalog = QDir(root).filePath("catalog");
        QObject::connect(&hospitalThread,&QThread::started,hospital,[=]()
        {
            const QDir dir(QDir(root).filePath("hospital"));
            hospital->initialize(dir.filePath("hospital_Information.db"),dir.filePath("config.ini"));
        });
        const QString secret = QFileInfo::exists(QDir(root).filePath("account/user_Information.db"))
            ? QString() : QUuid::createUuid().toString();
        QObject::connect(&accountThread,&QThread::started,accounts,[=]()
        { accounts->initialize(QDir(root).filePath("account"),secret); });
        QObject::connect(&projectThread,&QThread::started,projects,[=]() { projects->initialize(catalog,true); });
        QObject::connect(projects,&ProjectsStore::ready,reagents,[=](QString) { reagents->initialize(catalog); });
        QObject::connect(reagents,&ReagentStore::ready,samples,[=]() { samples->initialize(catalog); });
        for (const auto &worker : workers) worker.second->start();
    }
    ~CatalogSession()
    {
        for (const auto &worker : workers)
        {
            QMetaObject::invokeMethod(worker.first,"close",Qt::BlockingQueuedConnection);
            worker.second->quit();worker.second->wait();
        }
        host.takeWidget();delete shell;
    }
    bool ready()
    {
        return until([&]() { return projectPage->isReady() && !projectPage->isBusy()
            && reagentPage->isReady() && !reagentPage->isBusy()
            && samplePage->isReady() && !samplePage->isBusy()
            && shell->hospitalPage()->isReady() && !shell->hospitalPage()->isBusy()
            && shell->accountPage()->isReady() && !shell->accountPage()->isBusy(); });
    }
    bool login()
    {
        QMetaObject::invokeMethod(accounts,"setSession",Qt::QueuedConnection,
            Q_ARG(QString,QStringLiteral("offlineEngineer")),Q_ARG(bool,true));
        return until([&]() { return shell->accountSession().role==4 && !shell->accountPage()->isBusy(); });
    }
    QThread hospitalThread,accountThread,projectThread,reagentThread,sampleThread;
    HospitalStore *hospital;AccountStore *accounts;ProjectsStore *projects;ReagentStore *reagents;
    SampleStore *samples;SamplePage *samplePage;
    ProjectPage *projectPage;ReagentsPage *reagentPage;frmMain *shell;QScrollArea host;
    QList<QPair<QObject *,QThread *>> workers;
};
}

class CatalogShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void referencesAndGuards();
    void mixedLoad();
    void productStartupFailure();
    void unknownInitialActorFails();
    void samplePresetWorkflow();
    void fullProductStartupAndShutdown();
private:
    QString output;
};

void CatalogShellTest::initTestCase()
{
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Noto Sans CJK SC",11));
    QApplication::setQuitOnLastWindowClosed(false);
    output = qEnvironmentVariable("SN100_CATALOG_EVIDENCE_DIR");
    QVERIFY(QDir::isAbsolutePath(output));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(QDir().mkpath(output));
}

void CatalogShellTest::referencesAndGuards()
{
    CatalogSession session(output);
    QVERIFY(session.ready());
    QVERIFY(!session.shell->accountSession().authenticated);
    QVERIFY(!session.projectPage->projects().isEmpty());
    QSignalSpy projectErrors(session.projects,&ProjectsStore::error);
    QSignalSpy projectSuccess(session.projects,&ProjectsStore::succeeded);
    QSignalSpy reagentErrors(session.reagents,&ReagentStore::error);
    QSignalSpy reagentSuccess(session.reagents,&ReagentStore::succeeded);
    const qint64 sourceId = session.projectPage->projects().first().id;
    QVERIFY(QMetaObject::invokeMethod(session.projects,"copy",Qt::QueuedConnection,
        Q_ARG(qint64,9001),Q_ARG(qint64,sourceId),Q_ARG(QString,QStringLiteral("LOCAL_A"))));
    QVERIFY(until([&]() { return !projectErrors.isEmpty(); }));
    QCOMPARE(projectSuccess.size(),0);
    QVERIFY(session.login());
    QVERIFY(QMetaObject::invokeMethod(session.projects,"copy",Qt::QueuedConnection,
        Q_ARG(qint64,9002),Q_ARG(qint64,sourceId),Q_ARG(QString,QStringLiteral("LOCAL_A"))));
    QVERIFY(until([&]() { return !projectSuccess.isEmpty(); }));
    const qint64 projectId = projectSuccess.last().at(2).toLongLong();
    QVERIFY(projectId>sourceId);
    session.projectPage->reload();
    QVERIFY(until([&]() { return !session.projectPage->isBusy() && session.projectPage->projects().size()==2; }));

    ReagentRecord reagent;reagent.name=QStringLiteral("匿名试剂A");
    QVERIFY(QMetaObject::invokeMethod(session.reagents,"saveReagent",Qt::QueuedConnection,Q_ARG(ReagentRecord,reagent)));
    QVERIFY(until([&]() { return session.reagentPage->snapshot().reagents.size()==1; }));
    const qint64 reagentId=session.reagentPage->snapshot().reagents.first().id;
    ReagentBottle bottle;bottle.name=QStringLiteral("匿名瓶型");bottle.capacityUl=5000;
    QVERIFY(QMetaObject::invokeMethod(session.reagents,"saveBottle",Qt::QueuedConnection,Q_ARG(ReagentBottle,bottle)));
    QVERIFY(until([&]() { return session.reagentPage->snapshot().bottles.size()==1; }));
    ReagentBatch batch;batch.projectId=projectId;batch.code="OFFLINE-01";batch.batchGroup="A01|B01";
    QVERIFY(QMetaObject::invokeMethod(session.reagents,"saveBatch",Qt::QueuedConnection,Q_ARG(ReagentBatch,batch)));
    QVERIFY(until([&]() { return session.reagentPage->snapshot().batches.size()==1; }));
    const auto snapshot=session.reagentPage->snapshot();
    ReagentHole hole;hole.name="R_1";hole.area="r_area";hole.reagentId=reagentId;
    hole.bottleId=snapshot.bottles.first().id;hole.batchId=snapshot.batches.first().id;
    hole.expiry="2030-01-01";hole.amountUl=5000;hole.remainingUl=4000;
    QVERIFY(QMetaObject::invokeMethod(session.reagents,"saveHole",Qt::QueuedConnection,Q_ARG(ReagentHole,hole)));
    QVERIFY(until([&]() { return session.reagentPage->snapshot().holes.size()==1; }));
    QCOMPARE(session.reagentPage->snapshot().holes.first().remainingUl,4000);

    projectErrors.clear();reagentErrors.clear();
    QVERIFY(QMetaObject::invokeMethod(session.projects,"remove",Qt::QueuedConnection,Q_ARG(qint64,9003),Q_ARG(qint64,projectId)));
    QVERIFY(until([&]() { return projectErrors.size()==1; }));
    QVERIFY(QMetaObject::invokeMethod(session.reagents,"removeReagent",Qt::QueuedConnection,Q_ARG(qint64,reagentId)));
    QVERIFY(until([&]() { return reagentErrors.size()==1; }));
    QCOMPARE(session.reagentPage->snapshot().reagents.size(),1);

    QVERIFY(session.shell->selectMainPage(5));
    auto *name=session.projectPage->findChild<QLineEdit *>("project_cn_name");
    QVERIFY(name && name->isEnabled());
    const QString committed=name->text();
    name->setText(QStringLiteral("未保存项目名称"));
    QVERIFY(session.projectPage->hasUnsavedChanges());
    QTimer::singleShot(50,[]()
    { if(auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) dialog->done(QMessageBox::Cancel); });
    QVERIFY(!session.shell->selectMainPage(0));
    QCOMPARE(session.shell->currentMainPage(),5);
    QVERIFY(session.projectPage->hasUnsavedChanges());
    QTimer::singleShot(50,[]()
    { if(auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) dialog->done(QMessageBox::Discard); });
    QVERIFY(session.shell->selectMainPage(0));
    QVERIFY(!session.projectPage->hasUnsavedChanges());
    QCOMPARE(name->text(),committed);

    // Capture exact original canvases; actual Windows theme equality is separate.
    QVERIFY(session.reagentPage->grab().save(QDir(output).filePath("reagents.png")));
    QVERIFY(session.shell->selectMainPage(5));
    QVERIFY(session.projectPage->grab().save(QDir(output).filePath("projects.png")));
    session.host.resize(1920,1080);QTest::qWait(100);
    QVERIFY(session.host.grab().save(QDir(output).filePath("shell_1920.png")));
    session.host.resize(1600,900);QTest::qWait(100);
    QVERIFY(session.host.grab().save(QDir(output).filePath("shell_1600.png")));

    QVERIFY(session.shell->requestLogout());
    QVERIFY(until([&]() { return !session.shell->accountSession().authenticated; }));
    projectErrors.clear();
    QVERIFY(QMetaObject::invokeMethod(session.projects,"remove",Qt::QueuedConnection,Q_ARG(qint64,9004),Q_ARG(qint64,projectId)));
    QVERIFY(until([&]() { return projectErrors.size()==1; }));
    QVERIFY(session.shell->close());
    QFile result(QDir(output).filePath("integration.json"));
    QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    const QJsonObject values={{"guest_write_rejected",true},{"project_id",double(projectId)},
        {"reagent_remaining_ul_local_input",4000},{"cross_connection_reference_deletion_rejected",true},
        {"draft_cancel_and_discard",true},{"logout_write_rejected",true},
        {"original_windows_visual_comparison_performed",false},{"instrument_connected",false},
        {"platform",QGuiApplication::platformName()}};
    QVERIFY(result.write(QJsonDocument(values).toJson())>0);
}

void CatalogShellTest::mixedLoad()
{
    const QString directory=QDir(output).filePath(QStringLiteral("mixed_load"));
    CatalogSession session(directory);
    QVERIFY(session.ready());QVERIFY(session.login());
    auto *name=session.projectPage->findChild<QLineEdit *>("project_cn_name");
    auto *save=session.projectPage->findChild<QPushButton *>("btnSave");
    auto *sampleTable=session.samplePage->findChild<QTableWidget *>("tblSetting");
    auto *sampleSave=session.samplePage->findChild<QPushButton *>("btnMenuSave");
    QVERIFY(name && save && sampleTable && sampleSave);
    const qint64 bytesBefore=directoryBytes(directory);
    QSignalSpy outcomes(session.projectPage,&ProjectPage::operationFinished);
    QElapsedTimer clock;clock.start();
    qint64 last=clock.nsecsElapsed();
    QList<double> heartbeats,completion,sampleCompletion;
    QTimer heartbeat;heartbeat.setTimerType(Qt::PreciseTimer);heartbeat.setInterval(10);
    connect(&heartbeat,&QTimer::timeout,this,[&]()
    {
        const qint64 now=clock.nsecsElapsed();heartbeats.append(double(now-last)/1000000.0);last=now;
    });
    heartbeat.start();
    int writes=0,sampleWrites=0;
    while(clock.elapsed()<60000)
    {
        QVERIFY(session.shell->selectMainPage(5));
        name->setText(QStringLiteral("离线测量%1").arg(writes%2));
        outcomes.clear();QElapsedTimer operation;operation.start();
        QTest::mouseClick(save,Qt::LeftButton);
        QVERIFY(until([&]() { return !session.projectPage->isBusy() && !outcomes.isEmpty(); }));
        QVERIFY(!session.projectPage->hasUnsavedChanges());
        bool saved=false;
        for(const auto &outcome:outcomes)
            if(outcome.at(0).toString()==QStringLiteral("save") && outcome.at(1).toBool()) saved=true;
        QVERIFY(saved);
        completion.append(double(operation.nsecsElapsed())/1000000.0);++writes;
        QVERIFY(session.shell->selectMainPage(1));
        sampleTable->item(0,1)->setText(QStringLiteral("L00%1").arg(sampleWrites%2));
        QElapsedTimer sampleClock;sampleClock.start();sampleSave->click();
        QVERIFY(until([&]() { return !session.samplePage->isBusy() && !session.samplePage->hasUnsavedChanges(); }));
        sampleCompletion.append(double(sampleClock.nsecsElapsed())/1000000.0);++sampleWrites;
        QVERIFY(session.shell->selectMainPage(0));QTest::qWait(25);
        QVERIFY(session.shell->selectMainPage(6));QTest::qWait(25);
    }
    heartbeat.stop();
    const auto distribution=[](QList<double> values)
    {
        std::sort(values.begin(),values.end());
        const auto percentile=[&](double fraction)
        { return values.isEmpty()?0.0:values.at(qMin(values.size()-1,int(fraction*(values.size()-1)))); };
        return QJsonObject{{"samples",values.size()},{"p95_ms",percentile(0.95)},
            {"p99_ms",percentile(0.99)},{"max_ms",values.isEmpty()?0.0:values.last()}};
    };
    QFile result(QDir(output).filePath(QStringLiteral("mixed_load.json")));
    QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    const QJsonObject values={{"elapsed_ms",double(clock.elapsed())},{"successful_project_commits",writes},
        {"successful_sample_commits",sampleWrites},{"sample_save_complete",distribution(sampleCompletion)},
        {"runtime_files_bytes_before",double(bytesBefore)},
        {"runtime_files_bytes_after",double(directoryBytes(directory))},
        {"project_save_complete",distribution(completion)},{"timer_10ms_intervals",distribution(heartbeats)},
        {"navigation",QStringLiteral("Project/sample/reagent/user pages via the real shell navigation handler")},
        {"scope",QStringLiteral("WSL QtTest load includes driver overhead; timer is an event-loop proxy, not pixel latency or RK qualification")}};
    QVERIFY(result.write(QJsonDocument(values).toJson())>0);
    QVERIFY(writes>0);QVERIFY(heartbeats.size()>100);
    QVERIFY(session.shell->close());
}

void CatalogShellTest::productStartupFailure()
{
    const QString application=qEnvironmentVariable("SN100_SHELL_APP");
    QVERIFY(QFileInfo(application).isAbsolute() && QFileInfo(application).isExecutable());
    const QDir runtime(QDir(output).filePath(QStringLiteral("invalid_catalog_runtime")));
    const QDir catalog(runtime.filePath(QStringLiteral("catalog")));
    QVERIFY(QDir().mkpath(catalog.path()));
    QFile sentinel(catalog.filePath(QStringLiteral("preserve.txt")));
    QVERIFY(sentinel.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    const QByteArray original("unmarked directory: preserve this exact content\n");
    QCOMPARE(sentinel.write(original),qint64(original.size()));sentinel.close();
    QTemporaryFile secret(QDir(output).filePath(QStringLiteral("private_XXXXXX.secret")));
    QVERIFY(secret.open());
    const QByteArray value=QUuid::createUuid().toByteArray();
    QCOMPARE(secret.write(value),qint64(value.size()));QVERIFY(secret.flush());secret.close();
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(application,{"--offline","--accounts","--history","--catalog","--samples","--data-dir",runtime.path(),
        "--seed-anonymous","--test-secret-file",secret.fileName(),"--viewport","1600x900"});
    QVERIFY(process.waitForStarted(10000));
    const bool exited=until([&]() { return process.state()==QProcess::NotRunning; },20000);
    if(!exited){process.kill();process.waitForFinished(5000);}
    QVERIFY(exited);
    const QByteArray log=process.readAll();
    QVERIFY(secret.remove());
    QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),3);
    QVERIFY(!log.contains("SHELL_READY"));QVERIFY(log.contains("THREADS_STOPPED true"));
    QVERIFY(!log.contains(value));
    QVERIFY(sentinel.open(QIODevice::ReadOnly));QCOMPARE(sentinel.readAll(),original);
    QVERIFY(!QFileInfo::exists(catalog.filePath(QStringLiteral("catalog.db"))));
    QFile outputLog(QDir(output).filePath(QStringLiteral("startup_failure.log")));
    QVERIFY(outputLog.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    QCOMPARE(outputLog.write(log),qint64(log.size()));
}

void CatalogShellTest::unknownInitialActorFails()
{
    const QString application=qEnvironmentVariable("SN100_SHELL_APP");
    QVERIFY(QFileInfo(application).isExecutable());
    const QString runtime=QDir(output).filePath(QStringLiteral("unknown_actor_runtime"));
    QTemporaryFile secret(QDir(output).filePath(QStringLiteral("private_actor_XXXXXX.secret")));
    QVERIFY(secret.open());
    const QByteArray value=QUuid::createUuid().toByteArray();
    QCOMPARE(secret.write(value),qint64(value.size()));QVERIFY(secret.flush());secret.close();
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(application,{"--offline","--accounts","--history","--catalog","--samples","--seed-catalog-demo",
        "--data-dir",runtime,"--seed-anonymous","--test-secret-file",secret.fileName(),
        "--test-session","missingOfflineActor","--viewport","1600x900"});
    QVERIFY(process.waitForStarted(10000));
    const bool exited=until([&]() { return process.state()==QProcess::NotRunning; },20000);
    if(!exited){process.kill();process.waitForFinished(5000);}
    QVERIFY(exited);
    const QByteArray log=process.readAll();QVERIFY(secret.remove());
    QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),3);
    QVERIFY(log.contains("ACCOUNT_ERROR session"));QVERIFY(!log.contains("SHELL_READY"));
    QVERIFY(log.contains("THREADS_STOPPED true"));QVERIFY(!log.contains(value));
    QFile outputLog(QDir(output).filePath(QStringLiteral("unknown_actor.log")));
    QVERIFY(outputLog.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    QCOMPARE(outputLog.write(log),qint64(log.size()));
}

void CatalogShellTest::samplePresetWorkflow()
{
    const QString runtime=QDir(output).filePath("sample_workflow");
    qint64 savedId=0,projectId=0;
    {
        CatalogSession session(runtime);QVERIFY(session.ready());
        QVERIFY(session.shell->selectMainPage(1));
        QSignalSpy errors(session.samples,&SampleStore::error);
        QSignalSpy saved(session.samples,&SampleStore::succeeded);
        SampleRack rack;rack.name="S01";
        for(int i=1;i<=8;++i){SampleHole hole;hole.number=i;rack.holes.append(hole);}
        QVERIFY(QMetaObject::invokeMethod(session.samples,"saveRack",Qt::QueuedConnection,Q_ARG(qint64,8001),Q_ARG(SampleRack,rack)));
        QVERIFY(until([&](){return errors.size()==1;}));QCOMPARE(saved.size(),0);
        QVERIFY(session.login());
        QSignalSpy copied(session.projects,&ProjectsStore::succeeded);
        QVERIFY(QMetaObject::invokeMethod(session.projects,"copy",Qt::QueuedConnection,Q_ARG(qint64,8006),
            Q_ARG(qint64,session.projectPage->projects().first().id),Q_ARG(QString,QStringLiteral("LOCAL_S"))));
        QVERIFY(until([&](){return copied.size()==1;}));projectId=copied.first().at(2).toLongLong();
        session.projectPage->reload();QVERIFY(until([&](){return !session.projectPage->isBusy()&&session.projectPage->projects().size()==2;}));
        ProjectRecord project;for(const auto &value:session.projectPage->projects())if(value.id==projectId)project=value;
        QCOMPARE(project.id,projectId);
        QVERIFY(!project.sampleRates.isEmpty());
        rack.holes[0].sampleId="A001";
        rack.holes[0].tasks.append({project.id,project.sampleRates.first().sampleRate,2,true});
        QVERIFY(QMetaObject::invokeMethod(session.samples,"saveRack",Qt::QueuedConnection,Q_ARG(qint64,8002),Q_ARG(SampleRack,rack)));
        QVERIFY(until([&](){return saved.size()==1;}));savedId=saved.last().at(2).toLongLong();
        session.samplePage->reload();
        QVERIFY(until([&](){return !session.samplePage->isBusy() && session.samplePage->snapshot().racks.size()==2;}));
        // The original next-rack button selects the newly committed ordinary rack.
        auto *next=session.samplePage->findChild<QPushButton *>("btnNextRack");QVERIFY(next);
        auto *previous=session.samplePage->findChild<QPushButton *>("btnPrvRack");QVERIFY(previous);
        auto *racks=session.samplePage->findChild<QListWidget *>("rackList");QVERIFY(racks);
        next->click();
        auto *table=session.samplePage->findChild<QTableWidget *>("tblSetting");QVERIFY(table);
        QVERIFY(until([&](){return racks->currentItem()&&racks->row(racks->currentItem())==1
            && racks->visualItemRect(racks->currentItem()).top()==0;}));
        QCOMPARE(racks->currentItem()->sizeHint().height(),session.samplePage->height());
        QCOMPARE(racks->verticalScrollBarPolicy(),Qt::ScrollBarAlwaysOff);
        QCOMPARE(racks->itemWidget(racks->currentItem())->findChild<QLabel *>("lblRackId")->text(),QString("预设1:S01"));
        QVERIFY(previous->isVisible());QVERIFY(!next->isVisible());
        previous->click();
        QVERIFY(until([&](){return racks->currentItem()&&racks->row(racks->currentItem())==0
            && racks->visualItemRect(racks->currentItem()).top()==0;}));
        QCOMPARE(table->rowCount(),3);
        QCOMPARE(racks->itemWidget(racks->currentItem())->findChild<QLabel *>("lblRackId")->text(),QString("急诊"));
        QVERIFY(!previous->isVisible());QVERIFY(next->isVisible());next->click();
        QCOMPARE(table->rowCount(),8);QCOMPARE(table->item(0,1)->text(),QString("A001"));
        table->item(0,1)->setText("A002");QVERIFY(session.samplePage->hasUnsavedChanges());
        QTimer::singleShot(30,[](){if(auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))dialog->done(QMessageBox::Cancel);});
        QVERIFY(!session.shell->selectMainPage(0));QCOMPARE(session.shell->currentMainPage(),1);
        QTimer::singleShot(30,[](){if(auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))dialog->done(QMessageBox::Cancel);});
        QVERIFY(!session.shell->requestLogout());QVERIFY(session.shell->accountSession().authenticated);
        auto *save=session.samplePage->findChild<QPushButton *>("btnMenuSave");QVERIFY(save&&save->isEnabled());
        save->click();QVERIFY(until([&](){return !session.samplePage->isBusy()&&!session.samplePage->hasUnsavedChanges();}));
        SampleCombination combination;combination.name="PAIR";combination.orderedProjectIds={projectId,projectId};
        saved.clear();
        QVERIFY(QMetaObject::invokeMethod(session.samples,"saveCombination",Qt::QueuedConnection,Q_ARG(qint64,8003),Q_ARG(SampleCombination,combination)));
        QVERIFY(until([&](){return saved.size()==1;}));
        session.samplePage->reload();QVERIFY(until([&](){return !session.samplePage->isBusy()&&session.samplePage->snapshot().combinations.size()==1;}));
        QCOMPARE(session.samplePage->snapshot().combinations.first().orderedProjectIds,QList<qint64>({projectId,projectId}));
        QSignalSpy projectErrors(session.projects,&ProjectsStore::error);
        QVERIFY(QMetaObject::invokeMethod(session.projects,"remove",Qt::QueuedConnection,Q_ARG(qint64,8004),Q_ARG(qint64,projectId)));
        QVERIFY(until([&](){return projectErrors.size()==1;}));
        QCOMPARE(session.samplePage->property("reagentSource").toString(),QString("manual-local; consumption-map-unavailable"));
        auto *back=session.samplePage->findChild<QPushButton *>("btnMenuReturn");QVERIFY(back);back->click();
        for(int i=0;i<racks->count();++i)QCOMPARE(racks->item(i)->sizeHint().height(),136);
        QCOMPARE(racks->verticalScrollBarPolicy(),Qt::ScrollBarAsNeeded);
        QVERIFY(!previous->isVisible());QVERIFY(!next->isVisible());
        auto *edit=session.samplePage->findChild<QPushButton *>("btnEditRack");QVERIFY(edit&&edit->isVisible());edit->click();
        QVERIFY(until([&](){return racks->currentItem()&&racks->row(racks->currentItem())==1
            && racks->visualItemRect(racks->currentItem()).top()==0;}));
        QCOMPARE(racks->currentItem()->sizeHint().height(),session.samplePage->height());
        auto *add=session.samplePage->findChild<QWidget *>("btnAdd");QVERIFY(add&&add->isVisible());
        QTest::mouseClick(add,Qt::LeftButton);
        QVERIFY(until([&](){return racks->currentItem()&&racks->count()==3
            && racks->itemWidget(racks->currentItem())->property("localDraft").toBool();}));
        QCOMPARE(table->rowCount(),8);QCOMPARE(session.samplePage->snapshot().racks.size(),2);
        QVERIFY(racks->itemWidget(racks->currentItem())->findChild<QLabel *>("lblRackId")->text().contains("S02"));
        QTimer::singleShot(30,[](){if(auto *dialog=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))dialog->done(QMessageBox::Discard);});
        QVERIFY(session.shell->selectMainPage(0));QCOMPARE(racks->count(),2);
        QVERIFY(session.shell->selectMainPage(1));next->click();
        QVERIFY(until([&](){return racks->currentItem()&&racks->row(racks->currentItem())==1
            && racks->visualItemRect(racks->currentItem()).top()==0;}));
        QVERIFY(session.samplePage->grab().save(QDir(output).filePath("samples.png")));
        session.host.resize(1920,1080);QTest::qWait(100);
        QVERIFY(session.host.grab().save(QDir(output).filePath("samples_shell_1920.png")));
        QVERIFY(session.shell->requestLogout());QVERIFY(until([&](){return !session.shell->accountSession().authenticated;}));
        errors.clear();
        QVERIFY(QMetaObject::invokeMethod(session.samples,"removeRack",Qt::QueuedConnection,Q_ARG(qint64,8005),Q_ARG(qint64,savedId)));
        QVERIFY(until([&](){return errors.size()==1;}));QVERIFY(session.shell->close());
    }
    {
        CatalogSession reopened(runtime);QVERIFY(reopened.ready());
        bool found=false;
        for(const auto &rack:reopened.samplePage->snapshot().racks)if(rack.id==savedId)
        {
            found=true;QCOMPARE(rack.holes.first().sampleId,QString("A002"));
            QCOMPARE(rack.holes.first().tasks.first().projectId,projectId);
            QCOMPARE(rack.holes.first().tasks.first().repeats,2);QVERIFY(rack.holes.first().tasks.first().prolonged);
        }
        QVERIFY(found);QVERIFY(reopened.shell->close());
    }
    QFile result(QDir(output).filePath("sample_integration.json"));QVERIFY(result.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    const QJsonObject values={{"sample_id_read_after_reopen",QStringLiteral("A002")},{"repeats",2},{"prolonged",true},
        {"project_id",double(projectId)},{"rack_id",double(savedId)},{"ordered_combination_repeats",2},
        {"guest_and_logout_write_rejected",true},{"task_and_combination_project_fk_rejected",true},
        {"current_rack_card_tracks_next_previous",true},{"overview_compact_and_editor_position_restored",true},
        {"new_rack_draft_visible_but_not_in_committed_snapshot",true},{"discard_removes_draft_card",true},
        {"draft_cancel_blocks_navigation_and_logout",true},{"instrument_submission",false}};
    QVERIFY(result.write(QJsonDocument(values).toJson())>0);
}

void CatalogShellTest::fullProductStartupAndShutdown()
{
    QTemporaryFile secret(QDir(output).filePath("full_product_XXXXXX.secret"));QVERIFY(secret.open());
    const QByteArray value=QUuid::createUuid().toByteArray();
    QCOMPARE(secret.write(value),qint64(value.size()));QVERIFY(secret.flush());secret.close();
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(qEnvironmentVariable("SN100_SHELL_APP"),{"--offline","--accounts","--history","--catalog","--samples",
        "--seed-catalog-demo","--seed-anonymous","--test-secret-file",secret.fileName(),
        "--data-dir",QDir(output).filePath("full_product_runtime"),"--viewport","1600x900"});
    QVERIFY(process.waitForStarted(10000));QByteArray log;
    QVERIFY(until([&](){log+=process.readAll();return log.contains("SHELL_READY")||process.state()==QProcess::NotRunning;},20000));
    QVERIFY(log.contains("SHELL_READY"));process.terminate();
    const bool exited=until([&](){return process.state()==QProcess::NotRunning;},20000);
    if(!exited){process.kill();process.waitForFinished(5000);}QVERIFY(exited);log+=process.readAll();
    QCOMPARE(process.exitStatus(),QProcess::NormalExit);QCOMPARE(process.exitCode(),0);
    QVERIFY(secret.remove());QVERIFY(!log.contains(value));
    for(const auto *name:{"SAMPLE_STORE_CLOSED","PROJECT_STORE_CLOSED","REAGENT_STORE_CLOSED","HISTORY_STORE_CLOSED","ACCOUNT_STORE_CLOSED","HOSPITAL_STORE_CLOSED","THREADS_STOPPED true"})QVERIFY2(log.contains(name),name);
    QFile file(QDir(output).filePath("full_product.log"));QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
    QCOMPARE(file.write(log),qint64(log.size()));
}

QTEST_MAIN(CatalogShellTest)
#include "catalog_shell_test.moc"
