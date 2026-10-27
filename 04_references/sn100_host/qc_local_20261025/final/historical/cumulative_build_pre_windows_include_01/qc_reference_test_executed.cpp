#include "quality_control/qc_store.h"
#include "projects/projects_store.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <atomic>
#include <functional>

namespace
{
bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!condition() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(); QTest::qWait(3); }
    return condition();
}
QByteArray sha256(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex();
}
class Database
{
public:
    explicit Database(const QString &path)
    {
        name = "qc-reference-test-" + QUuid::createUuid().toString();
        value = QSqlDatabase::addDatabase("QSQLITE",name); value.setDatabaseName(path);
        value.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100"); value.open(); exec("PRAGMA foreign_keys=ON");
    }
    ~Database() { value.close(); value = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool exec(const QString &sql)
    {
        QSqlQuery query(value); const bool ok = query.exec(sql);
        if (!ok) qWarning() << "FIXTURE_SQL" << sql << query.lastError().text();
        return ok;
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(value); if (!query.exec(sql) || !query.next()) return {}; return query.value(0);
    }
    QString name;
    QSqlDatabase value;
};
template<class Store> class Worker
{
public:
    Worker() : store(new Store),ready(store,&Store::ready),ok(store,&Store::succeeded),errors(store,&Store::error)
    {
        store->moveToThread(&thread); QObject::connect(&thread,&QThread::finished,store,&QObject::deleteLater); thread.start();
    }
    ~Worker() { close(); thread.quit(); thread.wait(); }
    void close() { if (thread.isRunning()) QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection); }
    bool initialize(const std::function<void()> &action)
    {
        ready.clear(); errors.clear(); QMetaObject::invokeMethod(store,action,Qt::QueuedConnection);
        if (!waitFor([this] { return ready.count() || errors.count(); })) return false;
        if (errors.count()) qInfo() << "INITIALIZE_REJECTED" << errors.last();
        return ready.count() == 1 && errors.isEmpty();
    }
    bool call(const std::function<void()> &action, bool success = true)
    {
        ok.clear(); errors.clear(); QMetaObject::invokeMethod(store,action,Qt::QueuedConnection);
        if (!waitFor([this] { return ok.count() || errors.count(); })) return false;
        if (ok.count()) qInfo() << "COMMITTED" << ok.last();
        if (errors.count()) qInfo() << "REJECTED" << errors.last();
        return success ? ok.count() == 1 && errors.isEmpty() : errors.count() == 1 && ok.isEmpty();
    }
    Store *store;
    QThread thread;
    QSignalSpy ready,ok,errors;
};
class Barrier
{
public:
    Barrier(QcStore *store, const QString &phase, qint64 controlId) : worker(store)
    {
        connection = QObject::connect(store,&QcStore::referenceCheckpoint,store,
            [this,phase,controlId](const QString &actual,qint64 id)
        {
            if (actual == phase && id == controlId && !used.exchange(true))
            { entered.release(); resume.acquire(); }
        },Qt::DirectConnection);
    }
    ~Barrier()
    {
        resume.release(); QObject::disconnect(connection);
        // 即便某个断言提前退出，也先让已进入屏障的worker回调结束再销毁信号量。
        if (worker->thread()->isRunning()) QMetaObject::invokeMethod(worker,[] {},Qt::BlockingQueuedConnection);
    }
    bool parked() { return waitFor([this] { return entered.available() > 0; }); }
    void release() { resume.release(); }
    QcStore *worker;
    QMetaObject::Connection connection;
    QSemaphore entered,resume;
    std::atomic_bool used{false};
};
class Fixture
{
public:
    Fixture() : directory(QDir(qEnvironmentVariable("SN100_QC_REFERENCE_ROOT")).filePath("case-XXXXXX"))
    { directory.setAutoRemove(false); }
    bool initialize()
    {
        if (!directory.isValid()) return false;
        catalogDirectory = directory.filePath("catalog"); catalogPath = QDir(catalogDirectory).filePath("catalog.db");
        qcDirectory = directory.filePath("qc"); localPath = QDir(qcDirectory).filePath("quality_control.db");
        if (!projects.initialize([this] { projects.store->initialize(catalogDirectory,true); })) return false;
        AccountSession session; session.name="anonymous-engineer"; session.role=4; session.authenticated=true; session.actionReady=true;
        QMetaObject::invokeMethod(projects.store,[this,session] { projects.store->setSession(session); },Qt::BlockingQueuedConnection);
        const ProjectRecords source = readProjects(); if (source.isEmpty()) return false;
        if (!projects.call([this,source] { projects.store->copy(1,source.first().id,"QC_GUARD_A"); })) return false;
        firstId = projects.ok.last().at(2).toLongLong();
        if (!projects.call([this,source] { projects.store->copy(2,source.first().id,"QC_GUARD_B"); })) return false;
        secondId = projects.ok.last().at(2).toLongLong();
        refs = {reference(project(firstId)),reference(project(secondId))};
        if (!open(qc,qcDirectory) || !bind(qc)) return false;
        QcControl control; control.name="QC_A"; control.manufacturer="匿名厂家";
        if (!qc.call([this,control] { qc.store->saveControl(3,control); })) return false;
        controlId = qc.ok.last().at(2).toLongLong();
        qInfo().noquote() << "FIXTURE" << directory.path() << "control_id=" << controlId << "project_id=" << firstId;
        return true;
    }
    ProjectRecords readProjects()
    {
        QSignalSpy loaded(projects.store,&ProjectsStore::snapshotReady);
        QMetaObject::invokeMethod(projects.store,[this] { projects.store->load(10); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count(); })) return {};
        return qvariant_cast<ProjectRecords>(loaded.last().at(1));
    }
    ProjectRecord project(qint64 id)
    {
        for (const ProjectRecord &record : readProjects()) if (record.id==id) return record;
        return {};
    }
    QcProjectRef reference(const ProjectRecord &project)
    {
        QcProjectRef ref; ref.id=project.id; ref.name=project.name;
        // 原始Time不是Original；此夹具使用真实项目已配置的Calibration结果。
        for (const ProjectResult &result : project.results) if (result.type=="Calibration")
        {
            QcResultRef value; value.type=2; value.name=result.name; value.unit=result.unit; value.decimals=result.decimals;
            value.ratio=result.unit=="a.u.x10"?10:1; ref.results.append(value);
        }
        return ref;
    }
    bool open(Worker<QcStore> &worker,const QString &path)
    { return worker.initialize([&worker,path] { worker.store->initialize(path); }); }
    bool bind(Worker<QcStore> &worker,bool expected=true)
    {
        QSignalSpy bound(worker.store,&QcStore::catalogBound), supplied(worker.store,&QcStore::catalogReady);
        worker.errors.clear();
        QMetaObject::invokeMethod(worker.store,[this,&worker] { worker.store->bindCatalog(catalogDirectory); },Qt::QueuedConnection);
        if (!waitFor([&] { return bound.count() || worker.errors.count(); })) return false;
        if (worker.errors.count()) qInfo() << "BIND_REJECTED" << worker.errors.last();
        if (!expected) return bound.isEmpty() && worker.errors.count()==1;
        if (bound.count()!=1 || worker.errors.count()) return false;
        QMetaObject::invokeMethod(worker.store,[this,&worker] { worker.store->setCatalog(11,refs); },Qt::QueuedConnection);
        return waitFor([&] { return supplied.count()==1; });
    }
    bool associate(Worker<QcStore> &worker,const QcProjectRefs &selected,bool expected=true)
    { return worker.call([&worker,this,selected] { worker.store->setProjects(20,controlId,selected); },expected); }
    QcControls read(Worker<QcStore> &worker)
    {
        QSignalSpy loaded(worker.store,&QcStore::snapshotReady);
        QMetaObject::invokeMethod(worker.store,[&worker] { worker.store->load(12); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count(); })) return {};
        return qvariant_cast<QcControls>(loaded.last().at(1));
    }
    bool loadRejected(Worker<QcStore> &worker)
    {
        QSignalSpy loaded(worker.store,&QcStore::snapshotReady); worker.errors.clear();
        QMetaObject::invokeMethod(worker.store,[&worker] { worker.store->load(13); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count() || worker.errors.count(); })) return false;
        return loaded.isEmpty() && worker.errors.count()==1;
    }
    int holds()
    { Database catalog(catalogPath); return catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='quality_control'").toInt(); }
    QString key()
    { Database catalog(catalogPath); return catalog.scalar("SELECT reference_key FROM catalog_project_reference WHERE owner_module='quality_control' ORDER BY reference_key LIMIT 1").toString(); }
    bool addHistory()
    {
        QcBatch batch; batch.controlId=controlId; batch.name="QC_LOT"; batch.expirationDate="2027/01/31";
        if (!qc.call([this,batch] { qc.store->saveBatch(30,batch); })) return false;
        Database local(localPath);
        return local.exec("INSERT INTO quality_data(control_name,project_name,control_batch,result_val,date,time) VALUES('QC_A','QC_GUARD_A','QC_LOT','12.3-13.4','2026/10/25','12:34:56')");
    }
    QTemporaryDir directory;
    Worker<ProjectsStore> projects;
    Worker<QcStore> qc;
    QString catalogDirectory,catalogPath,qcDirectory,localPath;
    qint64 firstId=0,secondId=0,controlId=0;
    QcProjectRefs refs;
};
}

class QcReferenceTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(!qEnvironmentVariable("SN100_QC_REFERENCE_ROOT").isEmpty());
        QVERIFY(QDir().mkpath(qEnvironmentVariable("SN100_QC_REFERENCE_ROOT")));
        ProjectsStore::registerTypes(); QcStore::registerTypes();
    }
    void restoreMissingHoldProtectsInactiveHistoryAndSnapshot()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); QVERIFY(f.addHistory());
        Database local(f.localPath),catalog(f.catalogPath);
        QVERIFY(local.exec("UPDATE quality_control_list SET is_used=0"));
        ProjectRecord current=f.project(f.firstId); current.enabled=false;
        for (ProjectResult &result:current.results) if(result.type=="Calibration") { result.unit="a.u.x10"; result.decimals=4; }
        QVERIFY(f.projects.call([&] { f.projects.store->save(31,current); }));
        QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='quality_control'"));
        const QByteArray before=sha256(f.localPath); f.qc.close(); QVERIFY(f.open(f.qc,f.qcDirectory)); QVERIFY(f.loadRejected(f.qc));
        QVERIFY(f.bind(f.qc)); QCOMPARE(sha256(f.localPath),before); QCOMPARE(f.holds(),1);
        const QcControl saved=f.read(f.qc).first(); QVERIFY(!saved.used);
        QCOMPARE(saved.projects.first().resultRefs.first().unit,QString("a.u.")); QCOMPARE(saved.projects.first().resultRefs.first().ratio,1.0);
        QCOMPARE(saved.projects.first().resultRefs.first().decimals,2); QCOMPARE(local.scalar("SELECT result_val FROM quality_data").toString(),QString("12.3-13.4"));
        current=f.project(f.firstId); current.name="RENAMED_A"; QVERIFY(f.projects.call([&] { f.projects.store->save(32,current); },false));
        QVERIFY(f.projects.call([&] { f.projects.store->remove(33,f.firstId); },false));
        qInfo() << "REOPEN_QC_SHA_UNCHANGED" << before << "hold" << f.key();
    }
    void corruptedIdentityFailsClosedWithoutLocalWrites_data()
    {
        QTest::addColumn<int>("damage");
        QTest::newRow("project-renamed")<<0; QTest::newRow("project-removed")<<1;
        QTest::newRow("missing-history-association")<<2; QTest::newRow("missing-control")<<3;
        QTest::newRow("missing-snapshot")<<4; QTest::newRow("invalid-snapshot")<<5;
        QTest::newRow("mismatched-stable-project-id")<<6; QTest::newRow("unknown-history-name")<<7;
    }
    void corruptedIdentityFailsClosedWithoutLocalWrites()
    {
        QFETCH(int,damage); Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); QVERIFY(f.addHistory()); f.qc.close();
        Database local(f.localPath),catalog(f.catalogPath);
        if (damage<=1)
        {
            QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='quality_control'"));
            if(damage==0) { ProjectRecord renamed=f.project(f.firstId); renamed.name="MOVED_NAME"; QVERIFY(f.projects.call([&] { f.projects.store->save(40,renamed); })); }
            else QVERIFY(f.projects.call([&] { f.projects.store->remove(41,f.firstId); }));
        }
        else if(damage==2)
        {
            QVERIFY(local.exec("DELETE FROM quality_rules")); QVERIFY(local.exec("DELETE FROM qc_project_snapshot")); QVERIFY(local.exec("DELETE FROM quality_item_proj"));
        }
        else if(damage==3) { QVERIFY(local.exec("PRAGMA foreign_keys=OFF")); QVERIFY(local.exec("DELETE FROM quality_control_list")); }
        else if(damage==4) QVERIFY(local.exec("DELETE FROM qc_project_snapshot"));
        else if(damage==5) QVERIFY(local.exec("UPDATE qc_project_snapshot SET ratio=-1"));
        else if(damage==6) QVERIFY(local.exec("UPDATE quality_item_proj SET project_id="+QString::number(f.secondId)));
        else QVERIFY(local.exec("UPDATE quality_data SET project_name='UNKNOWN_HISTORY'"));
        const QByteArray before=sha256(f.localPath); QVERIFY(f.open(f.qc,f.qcDirectory)); QVERIFY(f.bind(f.qc,false)); QVERIFY(f.loadRejected(f.qc));
        QCOMPARE(sha256(f.localPath),before); QCOMPARE(local.scalar("SELECT count(*) FROM quality_data").toInt(),1);
    }
    void orphanHoldIsRetainedAndOtherDatasetIsNotReleased()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); const QString oldKey=f.key();
        Worker<QcStore> second; const QString secondPath=f.directory.filePath("qc_second"); QVERIFY(f.open(second,secondPath)); QVERIFY(f.bind(second));
        QcControl control; control.name="QC_B"; control.manufacturer="匿名厂家";
        QVERIFY(second.call([&] { second.store->saveControl(40,control); })); const qint64 secondControl=second.ok.last().at(2).toLongLong();
        QVERIFY(second.call([&] { second.store->setProjects(41,secondControl,{f.refs.last()}); })); QCOMPARE(f.holds(),2);
        f.qc.close(); Database local(f.localPath);
        QVERIFY(local.exec("DELETE FROM quality_rules")); QVERIFY(local.exec("DELETE FROM qc_project_snapshot")); QVERIFY(local.exec("DELETE FROM quality_item_proj"));
        QVERIFY(f.open(f.qc,f.qcDirectory)); QVERIFY(f.bind(f.qc,false)); QVERIFY(f.loadRejected(f.qc)); QCOMPARE(f.holds(),2);
        QSignalSpy released(f.projects.store,&ProjectsStore::projectReferenceSucceeded);
        QMetaObject::invokeMethod(f.projects.store,[&] { f.projects.store->releaseProjectReference(42,"quality_control",oldKey); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return released.count()==1; })); QVERIFY(f.bind(f.qc)); QCOMPARE(f.holds(),1);
        QCOMPARE(f.read(second).first().projects.first().projectId,f.secondId);
    }
    void startupLocksAndReservationCommitFailuresRecover()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); f.qc.close();
        Database catalog(f.catalogPath); QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='quality_control'"));
        const QByteArray before=sha256(f.localPath); QVERIFY(f.open(f.qc,f.qcDirectory));
        QVERIFY(catalog.exec("BEGIN IMMEDIATE")); QVERIFY(f.bind(f.qc,false)); QVERIFY(f.loadRejected(f.qc));
        QVERIFY(catalog.exec("ROLLBACK")); QVERIFY(f.bind(f.qc)); QCOMPARE(f.holds(),1); QCOMPARE(sha256(f.localPath),before);
        QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='quality_control'")); f.qc.close(); QVERIFY(f.open(f.qc,f.qcDirectory));
        QVERIFY(catalog.exec("BEGIN")); QSqlQuery reader(catalog.value); QVERIFY(reader.exec("SELECT * FROM project_base")); QVERIFY(reader.next());
        QVERIFY(f.bind(f.qc,false)); reader.finish(); QVERIFY(catalog.exec("ROLLBACK")); QCOMPARE(f.holds(),0);
        QVERIFY(f.bind(f.qc)); QCOMPARE(f.holds(),1); QCOMPARE(sha256(f.localPath),before);
    }
    void deterministicDetachAndReattachKeepsNewAssociationProtected()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()}));
        Worker<QcStore> second; QVERIFY(f.open(second,f.qcDirectory)); QVERIFY(f.bind(second));
        Barrier barrier(f.qc.store,"after-detach-commit",f.controlId); f.qc.ok.clear(); f.qc.errors.clear();
        QMetaObject::invokeMethod(f.qc.store,[&] { f.qc.store->setProjects(50,f.controlId,{}); },Qt::QueuedConnection);
        QVERIFY(barrier.parked()); QVERIFY(f.associate(second,{f.refs.first()})); barrier.release();
        QVERIFY(waitFor([&] { return f.qc.ok.count()==1 || f.qc.errors.count(); })); QVERIFY(f.qc.errors.isEmpty());
        QCOMPARE(f.holds(),1); QCOMPARE(f.read(second).first().projects.size(),1);
        ProjectRecord renamed=f.project(f.firstId); renamed.name="UNSAFE_RENAME"; QVERIFY(f.projects.call([&] { f.projects.store->save(51,renamed); },false));
        qInfo()<<"DETERMINISTIC_DETACH_REATTACH"<<"second writer committed before first cleanup"<<f.key();
    }
    void deterministicFailedWriterCannotReleaseSuccessfulWriter_data()
    {
        QTest::addColumn<QString>("rollbackKind"); QTest::newRow("statement-abort")<<QString("ABORT"); QTest::newRow("automatic-transaction-rollback")<<QString("ROLLBACK");
    }
    void deterministicFailedWriterCannotReleaseSuccessfulWriter()
    {
        QFETCH(QString,rollbackKind); Fixture f; QVERIFY(f.initialize()); Worker<QcStore> second; QVERIFY(f.open(second,f.qcDirectory)); QVERIFY(f.bind(second));
        Database local(f.localPath);
        QVERIFY(local.exec(QStringLiteral("CREATE TRIGGER reject_second BEFORE INSERT ON quality_item_proj WHEN NEW.project_id=%1 BEGIN SELECT RAISE(%2,'forced %2 after first association'); END").arg(f.secondId).arg(rollbackKind)));
        Barrier barrier(f.qc.store,"after-failed-write",f.controlId); f.qc.ok.clear(); f.qc.errors.clear();
        QMetaObject::invokeMethod(f.qc.store,[&] { f.qc.store->setProjects(60,f.controlId,f.refs); },Qt::QueuedConnection);
        QVERIFY(barrier.parked()); QVERIFY(f.associate(second,{f.refs.first()})); barrier.release();
        QVERIFY(waitFor([&] { return f.qc.errors.count()==1; })); QVERIFY(f.qc.ok.isEmpty()); QCOMPARE(f.holds(),1);
        QCOMPARE(local.scalar("SELECT count(*) FROM quality_item_proj").toInt(),1); QCOMPARE(f.read(second).first().projects.first().projectId,f.firstId);
        ProjectRecord free=f.project(f.secondId); free.name="FREE_SECOND"; QVERIFY(f.projects.call([&] { f.projects.store->save(61,free); }));
        ProjectRecord held=f.project(f.firstId); held.name="UNSAFE_FIRST"; QVERIFY(f.projects.call([&] { f.projects.store->save(62,held); },false));
        qInfo()<<"DETERMINISTIC_FAILURE_CLEANUP"<<rollbackKind<<"second writer committed before cleanup"<<f.key();
    }
    void deletingControlCannotReleaseAnotherControlsReference()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); Worker<QcStore> second; QVERIFY(f.open(second,f.qcDirectory)); QVERIFY(f.bind(second));
        Barrier barrier(f.qc.store,"after-detach-commit",f.controlId); f.qc.ok.clear(); f.qc.errors.clear();
        QMetaObject::invokeMethod(f.qc.store,[&] { f.qc.store->removeControl(70,f.controlId); },Qt::QueuedConnection);
        QVERIFY(barrier.parked()); QVERIFY(f.associate(second,{f.refs.first()},false));
        QcControl added; added.name="QC_READD"; added.manufacturer="匿名厂家";
        QVERIFY(second.call([&] { second.store->saveControl(71,added); })); const qint64 newId=second.ok.last().at(2).toLongLong(); QVERIFY(newId!=f.controlId);
        QVERIFY(second.call([&] { second.store->setProjects(72,newId,{f.refs.first()}); })); barrier.release();
        QVERIFY(waitFor([&] { return f.qc.ok.count()==1; })); QCOMPARE(f.holds(),1); QCOMPARE(f.read(second).first().id,newId);
    }
    void failedCleanupKeepsHoldAndRequiresReconciliation()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); const QString key=f.key();
        Database catalog(f.catalogPath); QVERIFY(catalog.exec("CREATE TRIGGER reject_cleanup BEFORE DELETE ON catalog_project_reference WHEN OLD.owner_module='quality_control' BEGIN SELECT RAISE(ROLLBACK,'forced cleanup rollback'); END"));
        QSignalSpy warning(f.qc.store,&QcStore::warning); QVERIFY(f.associate(f.qc,{})); QVERIFY(waitFor([&]{return warning.count()==1;}));
        QCOMPARE(f.holds(),1); QCOMPARE(f.read(f.qc).first().projects.size(),0); qInfo()<<"CONSERVATIVE_WARNING"<<warning.first();
        f.qc.close(); QVERIFY(f.open(f.qc,f.qcDirectory)); QVERIFY(f.bind(f.qc,false)); QCOMPARE(f.holds(),1);
        QVERIFY(catalog.exec("DROP TRIGGER reject_cleanup")); QSignalSpy released(f.projects.store,&ProjectsStore::projectReferenceSucceeded);
        QMetaObject::invokeMethod(f.projects.store,[&] { f.projects.store->releaseProjectReference(80,"quality_control",key); },Qt::QueuedConnection);
        QVERIFY(waitFor([&]{return released.count()==1;})); QVERIFY(f.bind(f.qc)); QCOMPARE(f.holds(),0);
    }
    void localLockAndCommitFailureDoNotLeakNewHolds()
    {
        Fixture f; QVERIFY(f.initialize()); Database local(f.localPath);
        QVERIFY(local.exec("BEGIN IMMEDIATE")); QVERIFY(f.associate(f.qc,{f.refs.first()},false)); QCOMPARE(f.holds(),0); QVERIFY(local.exec("ROLLBACK"));
        QVERIFY(local.exec("BEGIN")); QSqlQuery reader(local.value); QVERIFY(reader.exec("SELECT * FROM quality_control_list")); QVERIFY(reader.next());
        QVERIFY(f.associate(f.qc,{f.refs.first()},false)); QCOMPARE(f.holds(),0);
        reader.finish(); QVERIFY(local.exec("ROLLBACK")); QCOMPARE(local.scalar("SELECT count(*) FROM quality_item_proj").toInt(),0);
        QVERIFY(f.associate(f.qc,{f.refs.first()})); QCOMPARE(f.holds(),1);
    }
    void physicalCloneCannotShareDatasetKeys()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.associate(f.qc,{f.refs.first()})); f.qc.close();
        const QString clone=f.directory.filePath("qc_clone"); QVERIFY(QDir().mkpath(clone));
        QVERIFY(QFile::copy(f.localPath,QDir(clone).filePath("quality_control.db")));
        QVERIFY(QFile::copy(QDir(f.qcDirectory).filePath(".sn100-qc-offline-v1"),QDir(clone).filePath(".sn100-qc-offline-v1")));
        Worker<QcStore> copy; QVERIFY(f.open(copy,clone)); const QByteArray before=sha256(QDir(clone).filePath("quality_control.db"));
        QVERIFY(f.bind(copy,false)); QVERIFY(f.loadRejected(copy)); QCOMPARE(sha256(QDir(clone).filePath("quality_control.db")),before); QCOMPARE(f.holds(),1);
        QVERIFY(f.open(f.qc,f.qcDirectory)); QVERIFY(f.bind(f.qc)); QCOMPARE(f.holds(),1);
        Database local(f.localPath),catalog(f.catalogPath),cloned(QDir(clone).filePath("quality_control.db"));
        const QString uuid=local.scalar("SELECT value FROM qc_metadata WHERE key='dataset_uuid'").toString();
        QCOMPARE(cloned.scalar("SELECT value FROM qc_metadata WHERE key='dataset_uuid'").toString(),uuid);
        QCOMPARE(catalog.scalar("SELECT value FROM catalog_metadata WHERE key='quality_control_dataset_path:"+uuid+"'").toString(),QFileInfo(f.localPath).canonicalFilePath());
    }
};
QTEST_GUILESS_MAIN(QcReferenceTest)
#include "qc_reference_test.moc"
