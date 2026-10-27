#include "calibration/calibration_store.h"
#include "projects/projects_store.h"
#include "reagents/reagent_store.h"
#include "quality_control/qc_store.h"
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QSemaphore>
#include <QSignalSpy>
#include <QSqlDriver>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <functional>
#include <atomic>

namespace
{
bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!condition() && elapsed.elapsed() < 5000) { QCoreApplication::processEvents(); QTest::qWait(3); }
    return condition();
}
AccountSession engineer()
{
    AccountSession session; session.name = "anonymous-reference-engineer";
    session.authenticated = true; session.actionReady = true; session.role = 4;
    session.source = AccountSessionSource::Simulated; return session;
}
class Database
{
public:
    explicit Database(QString path)
    {
        name = "reference-test-" + QUuid::createUuid().toString();
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
        QSqlQuery query(value); if (!query.exec(sql) || !query.next()) return {};
        return query.value(0);
    }
    QString name;
    QSqlDatabase value;
};
template<class Store> class Worker
{
public:
    Worker() : store(new Store), ready(store,&Store::ready), ok(store,&Store::succeeded), errors(store,&Store::error)
    {
        store->moveToThread(&thread);
        QObject::connect(&thread,&QThread::finished,store,&QObject::deleteLater);
        thread.start();
    }
    ~Worker()
    {
        if (thread.isRunning()) { QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection); thread.quit(); thread.wait(); }
    }
    bool initialize(const std::function<void()> &action, bool expected = true)
    {
        ready.clear(); errors.clear();
        QMetaObject::invokeMethod(store,action,Qt::QueuedConnection);
        if (!waitFor([this] { return ready.count() || errors.count(); })) return false;
        if (errors.count()) qInfo() << "INITIALIZE_REJECTED" << errors.last();
        return expected ? ready.count() == 1 && errors.isEmpty() : ready.isEmpty() && errors.count() == 1;
    }
    bool call(const std::function<void()> &action, bool expected = true)
    {
        ok.clear(); errors.clear();
        QMetaObject::invokeMethod(store,action,Qt::QueuedConnection);
        if (!waitFor([this] { return ok.count() || errors.count(); })) return false;
        if (ok.count()) qInfo() << "COMMITTED" << ok.last();
        if (errors.count()) qInfo() << "REJECTED" << errors.last();
        return expected ? ok.count() == 1 && errors.isEmpty() : ok.isEmpty() && errors.count() == 1;
    }
    void close() { QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection); }
    Store *store;
    QThread thread;
    QSignalSpy ready,ok,errors;
};

// 测试专用：使用已装QSQLITE所用的系统SQLite动态库，不新增生产依赖。
// 在真实自动ROLLBACK回调中设置有界barrier，验证第二worker不能越过操作锁。
class RollbackGate
{
public:
    explicit RollbackGate(Worker<CalibrationStore> &owner) : worker(owner), library("libsqlite3.so.0") {}
    ~RollbackGate()
    {
        proceed.release();
        if (native && api)
            QMetaObject::invokeMethod(worker.store,[this](){api(native,nullptr,nullptr);},Qt::BlockingQueuedConnection);
    }
    bool install()
    {
        if (!library.load()) return false;
        api=reinterpret_cast<Api>(library.resolve("sqlite3_rollback_hook"));
        if (!api) return false;
        QStringList connections;
        for (const QString &name:QSqlDatabase::connectionNames())
            if (name.startsWith(QStringLiteral("calibration-local-"))) connections.append(name);
        if (connections.size()!=1) return false; // 安装时第二worker尚未打开本地连接。
        const QString connection=connections.first();
        bool installed=false;
        QMetaObject::invokeMethod(worker.store,[this,connection,&installed]()
        {
            const QVariant handle=QSqlDatabase::database(connection,false).driver()->handle();
            if (!handle.isValid() || QByteArray(handle.typeName())!="sqlite3*") return;
            QVariant mutableHandle=handle;
            native=*static_cast<void**>(mutableHandle.data());
            api(native,[](void *context)
            {
                auto *gate=static_cast<RollbackGate*>(context);
                if (gate->fired.exchange(true)) return;
                gate->entered.release();
                gate->continued=gate->proceed.tryAcquire(1,3000);
            },this);
            installed=true;
        },Qt::BlockingQueuedConnection);
        return installed;
    }
    Worker<CalibrationStore> &worker;
    QLibrary library;
    using Api=void* (*)(void*,void (*)(void*),void*);
    Api api=nullptr;
    void *native=nullptr;
    QSemaphore entered,proceed;
    std::atomic<bool> fired{false},continued{false};
};
class Fixture
{
public:
    Fixture() : directory(QDir(qEnvironmentVariable("SN100_REFERENCE_TEST_ROOT")).filePath("case-XXXXXX"))
    {
        directory.setAutoRemove(false);
    }
    bool initialize()
    {
        if (!directory.isValid()) return false;
        catalogDirectory = directory.filePath("catalog");
        catalogPath = QDir(catalogDirectory).filePath("catalog.db");
        calibrationDirectory = directory.filePath("calibration");
        localPath = QDir(calibrationDirectory).filePath("calibration_local.db");
        if (!projects.initialize([this] { projects.store->initialize(catalogDirectory,true); })) return false;
        QMetaObject::invokeMethod(projects.store,[this] { projects.store->setSession(engineer()); },Qt::BlockingQueuedConnection);
        if (!reagents.initialize([this] { reagents.store->initialize(catalogDirectory); })) return false;
        QMetaObject::invokeMethod(reagents.store,[this] { reagents.store->setSession(engineer()); },Qt::BlockingQueuedConnection);
        const ProjectRecords baseline = readProjects();
        if (baseline.isEmpty()) return false;
        if (!projects.call([this,baseline] { projects.store->copy(1,baseline.first().id,"GUARD_A"); })) return false;
        projectId = projects.ok.last().at(2).toLongLong();
        if (!projects.call([this,baseline] { projects.store->copy(2,baseline.first().id,"GUARD_B"); })) return false;
        otherProjectId = projects.ok.last().at(2).toLongLong();
        {
            // 明确匿名夹具新增原枚举Original，不把已有Time猜成同一种结果。
            // 后续业务修改和保护验收均通过真实ProjectsStore/ReagentStore。
            Database catalog(catalogPath);
            for (qint64 id : {projectId,otherProjectId})
                if (!catalog.exec(QStringLiteral("INSERT INTO project_result VALUES(%1,'Original','GUARD_RAW',2,0,999,0,999,'','s',0)").arg(id))) return false;
        }
        ReagentBatch batch; batch.projectId = projectId; batch.code = "GUARD_BATCH"; batch.batchGroup = "ANON_R1|ANON_R2";
        if (!reagents.call([this,batch] { reagents.store->saveBatch(batch); })) return false;
        const ReagentSnapshot snapshot = readReagents();
        for (const ReagentBatch &saved : snapshot.batches) if (saved.projectId == projectId) batchId = saved.id;
        if (!batchId || !openCalibration(calibration,calibrationDirectory)) return false;
        QMetaObject::invokeMethod(calibration.store,[this] { calibration.store->setSession(engineer()); },Qt::BlockingQueuedConnection);
        qInfo().noquote() << "FIXTURE" << directory.path() << "project_id=" << projectId << "batch_id=" << batchId;
        return true;
    }
    bool openCalibration(Worker<CalibrationStore> &worker, const QString &path, bool expected = true)
    {
        const bool result = worker.initialize([&worker,path,this] { worker.store->initialize(path,catalogPath); },expected);
        if (result && expected) QMetaObject::invokeMethod(worker.store,[&worker] { worker.store->setSession(engineer()); },Qt::BlockingQueuedConnection);
        return result;
    }
    ProjectRecords readProjects()
    {
        QSignalSpy loaded(projects.store,&ProjectsStore::snapshotReady);
        QMetaObject::invokeMethod(projects.store,[this] { projects.store->load(101); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count(); })) return {};
        return qvariant_cast<ProjectRecords>(loaded.last().at(1));
    }
    ProjectRecord project(qint64 id)
    {
        for (const ProjectRecord &record : readProjects()) if (record.id == id) return record;
        return {};
    }
    ReagentSnapshot readReagents()
    {
        QSignalSpy loaded(reagents.store,&ReagentStore::loaded);
        QMetaObject::invokeMethod(reagents.store,[this] { reagents.store->load(); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count(); })) return {};
        return qvariant_cast<ReagentSnapshot>(loaded.last().at(0));
    }
    ReagentBatch batch()
    {
        for (const ReagentBatch &record : readReagents().batches) if (record.id == batchId) return record;
        return {};
    }
    CalibrationRecord record(bool withBatch = true, bool otherProject = false)
    {
        CalibrationRecord record; record.projectId = otherProject ? otherProjectId : projectId;
        record.projectName = otherProject ? "GUARD_B" : "GUARD_A";
        if (withBatch) { record.batchId = batchId; record.batchCode = "GUARD_BATCH"; record.batchGroup = "ANON_R1|ANON_R2"; }
        record.bloodBatch = "ANON_CAL"; record.method = "未拟合"; record.xTransform = "x"; record.yTransform = "y";
        record.concentrationUnit = "a.u."; record.resultUnit = "s"; record.unitRatio = 1;
        record.points = {{1,"OFFLINE_RATE_A",100.125,{12.25,12.75}},{2,"OFFLINE_RATE_B",50,{18,20}}};
        return record;
    }
    CalibrationRecords readCalibration(Worker<CalibrationStore> &worker)
    {
        QSignalSpy loaded(worker.store,&CalibrationStore::snapshotReady);
        QMetaObject::invokeMethod(worker.store,[&worker] { worker.store->load(102); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count(); })) return {};
        return qvariant_cast<CalibrationRecords>(loaded.last().at(1));
    }
    int projectHolds(QString owner = "calibration")
    {
        Database catalog(catalogPath);
        return catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='"+owner+"'").toInt();
    }
    int batchHolds()
    {
        Database catalog(catalogPath);
        return catalog.scalar("SELECT count(*) FROM catalog_batch_reference WHERE owner_module='calibration'").toInt();
    }
    bool addQcReference()
    {
        if (!qc.initialize([this] { qc.store->initialize(directory.filePath("qc")); })) return false;
        QSignalSpy bound(qc.store,&QcStore::catalogBound), supplied(qc.store,&QcStore::catalogReady);
        QcProjectRef ref; ref.id = projectId; ref.name = "GUARD_A";
        const ProjectRecord actual = project(projectId);
        for (const ProjectResult &result : actual.results)
        {
            if (result.type != "Original" && result.type != "Calibration") continue;
            QcResultRef value; value.type = result.type == "Original" ? 1 : 2;
            value.name = result.name; value.unit = result.unit; value.decimals = result.decimals;
            value.ratio = result.unit == "a.u.x10" ? 10 : 1; ref.results.append(value);
        }
        QMetaObject::invokeMethod(qc.store,[this,ref] { qc.store->bindCatalog(catalogDirectory); qc.store->setCatalog(1,{ref}); },Qt::QueuedConnection);
        if (!waitFor([&] { return bound.count() && supplied.count(); })) return false;
        QcControl control; control.name = "匿名质控"; control.manufacturer = "匿名厂家";
        if (!qc.call([this,control] { qc.store->saveControl(2,control); })) return false;
        qcControlId = qc.ok.last().at(2).toLongLong();
        return qc.call([this,ref] { qc.store->setProjects(3,qcControlId,{ref}); });
    }
    QTemporaryDir directory;
    Worker<ProjectsStore> projects;
    Worker<ReagentStore> reagents;
    Worker<CalibrationStore> calibration;
    Worker<QcStore> qc;
    QString catalogDirectory,catalogPath,calibrationDirectory,localPath;
    qint64 projectId = 0,otherProjectId = 0,batchId = 0,qcControlId = 0;
};
}

class CalibrationReferenceTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(!qEnvironmentVariable("SN100_REFERENCE_TEST_ROOT").isEmpty());
        QVERIFY(QDir().mkpath(qEnvironmentVariable("SN100_REFERENCE_TEST_ROOT")));
        ProjectsStore::registerTypes(); ReagentStore::registerTypes(); CalibrationStore::registerTypes(); QcStore::registerTypes();
    }
    void realCatalogGuardsAndSoftHiddenHistory()
    {
        Fixture f; QVERIFY(f.initialize()); const CalibrationRecord input = f.record();
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); })); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        ProjectRecord renamed = f.project(f.projectId); renamed.name = "RENAMED_A";
        QVERIFY(f.projects.call([&] { f.projects.store->save(2,renamed); },false));
        ReagentBatch batch = f.batch(); batch.code = "RENAMED_BATCH";
        QVERIFY(f.reagents.call([&] { f.reagents.store->saveBatch(batch); },false));
        batch = f.batch(); batch.batchGroup = "ANON_R3|ANON_R4";
        QVERIFY(f.reagents.call([&] { f.reagents.store->saveBatch(batch); },false));
        QVERIFY(f.reagents.call([&] { f.reagents.store->removeBatch(f.batchId); },false));
        CalibrationRecord saved = f.readCalibration(f.calibration).first();
        QVERIFY(f.calibration.call([&] { f.calibration.store->copy(3,saved.id,saved.revision); }));
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        for (const CalibrationRecord &record : f.readCalibration(f.calibration))
            QVERIFY(f.calibration.call([&] { f.calibration.store->remove(4,record.id,record.revision); }));
        QCOMPARE(f.readCalibration(f.calibration).size(),0); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        Database local(f.localPath); QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records WHERE visible=0").toInt(),2);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(),4);
        // 无批次项目的删除也确由calibration项目hold阻止，避免把reagent自身外键误算成通过。
        const CalibrationRecord noBatch = f.record(false,true);
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(5,noBatch); }));
        QVERIFY(f.projects.call([&] { f.projects.store->remove(6,f.otherProjectId); },false));
    }
    void historicalUnitsAndQcSnapshotSurviveCurrentChanges()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.addQcReference());
        const CalibrationRecord input = f.record(); QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); }));
        ProjectRecord changed = f.project(f.projectId);
        for (ProjectResult &result : changed.results) if (result.type == "Calibration") { result.unit = "a.u.x10"; result.decimals = 4; }
        QVERIFY(f.projects.call([&] { f.projects.store->save(2,changed); }));
        CalibrationRecord historical = f.readCalibration(f.calibration).first();
        QCOMPARE(historical.concentrationUnit,QString("a.u.")); QCOMPARE(historical.unitRatio,1.0);
        historical.points[0].measurements[0] = 12.5;
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(3,historical); }));
        historical = f.readCalibration(f.calibration).first();
        QVERIFY(f.calibration.call([&] { f.calibration.store->copy(4,historical.id,historical.revision); }));
        QCOMPARE(f.readCalibration(f.calibration).last().unitRatio,1.0);
        QCOMPARE(f.readCalibration(f.calibration).last().points.first().concentration,100.125);
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(5,input); },false));
        CalibrationRecord current = input; current.concentrationUnit = "a.u.x10"; current.unitRatio = 10;
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(6,current); }));
        historical = f.readCalibration(f.calibration).first(); historical.concentrationUnit = "a.u.x10"; historical.unitRatio = 10;
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(7,historical); },false));
        QSignalSpy loaded(f.qc.store,&QcStore::snapshotReady); QMetaObject::invokeMethod(f.qc.store,[&] { f.qc.store->load(8); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return loaded.count(); }));
        const QcControls controls = qvariant_cast<QcControls>(loaded.first().at(1));
        for (const QcResultRef &result : controls.first().projects.first().resultRefs) if (result.type == 2)
        { QCOMPARE(result.unit,QString("a.u.")); QCOMPARE(result.ratio,1.0); QCOMPARE(result.decimals,2); }
        qInfo() << "HISTORICAL_RAW_UNCHANGED" << f.readCalibration(f.calibration).first().points.first().concentration << "a.u." << 1;
    }
    void catalogAndLocalLocksRecover()
    {
        Fixture f; QVERIFY(f.initialize()); CalibrationRecord input = f.record();
        Database catalog(f.catalogPath), local(f.localPath);
        QVERIFY(catalog.exec("BEGIN IMMEDIATE"));
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); },false));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),0); QCOMPARE(f.projectHolds(),0);
        QVERIFY(catalog.exec("ROLLBACK"));
        QVERIFY(local.exec("BEGIN IMMEDIATE"));
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,input); },false)); QCOMPARE(f.projectHolds(),0);
        QVERIFY(local.exec("ROLLBACK"));
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(3,input); })); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
    }
    void partialWriteFailureReleasesOnlyNewHolds()
    {
        Fixture f; QVERIFY(f.initialize()); Database local(f.localPath); const CalibrationRecord input = f.record();
        QVERIFY(local.exec("CREATE TRIGGER reject_point BEFORE INSERT ON calibration_points WHEN NEW.point_index=2 BEGIN SELECT RAISE(ABORT,'reference-test partial point failure'); END"));
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); },false));
        QCOMPARE(f.projectHolds(),0); QCOMPARE(f.batchHolds(),0); QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(),0);
        QVERIFY(local.exec("DROP TRIGGER reject_point")); QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,input); }));
        QVERIFY(local.exec("CREATE TRIGGER reject_point BEFORE INSERT ON calibration_points BEGIN SELECT RAISE(ABORT,'reference-test repeat failure'); END"));
        CalibrationRecord existing = f.readCalibration(f.calibration).first(); existing.points[0].concentration = 99;
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(3,existing); },false));
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1); QCOMPARE(f.readCalibration(f.calibration).first().points.first().concentration,100.125);
        const CalibrationRecord fresh = f.record(false,true);
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(4,fresh); },false)); QCOMPARE(f.projectHolds(),1);
        QVERIFY(f.projects.call([&] { f.projects.store->remove(5,f.otherProjectId); }));
    }
    void localCommitFailureRecoversReservations()
    {
        Fixture f; QVERIFY(f.initialize()); Database local(f.localPath); const CalibrationRecord input = f.record();
        QVERIFY(local.exec("BEGIN")); QSqlQuery reader(local.value); QVERIFY(reader.exec("SELECT * FROM calibration_records")); reader.next();
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); },false));
        QCOMPARE(f.projectHolds(),0); QCOMPARE(f.batchHolds(),0);
        reader.finish(); QVERIFY(local.exec("ROLLBACK")); QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,input); }));
    }
    void reservationCommitFailureCannotLeaveLocalRecord()
    {
        Fixture f; QVERIFY(f.initialize()); Database catalog(f.catalogPath), local(f.localPath); const CalibrationRecord input = f.record();
        QVERIFY(catalog.exec("BEGIN")); QSqlQuery reader(catalog.value); QVERIFY(reader.exec("SELECT * FROM project_base")); QVERIFY(reader.next());
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); },false));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        reader.finish(); QVERIFY(catalog.exec("ROLLBACK")); QCOMPARE(f.projectHolds(),0); QCOMPARE(f.batchHolds(),0);
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,input); }));
    }
    void taggedV3MigrationReconcilesHiddenHistory()
    {
        Fixture f; QVERIFY(f.initialize()); const CalibrationRecord input = f.record();
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); }));
        const CalibrationRecord saved = f.readCalibration(f.calibration).first();
        QVERIFY(f.calibration.call([&] { f.calibration.store->remove(2,saved.id,saved.revision); })); f.calibration.close();
        Database local(f.localPath), catalog(f.catalogPath);
        const QString raw = local.scalar("SELECT measurements FROM calibration_points ORDER BY point_index LIMIT 1").toString();
        QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='calibration'"));
        QVERIFY(catalog.exec("DELETE FROM catalog_batch_reference WHERE owner_module='calibration'"));
        QVERIFY(local.exec("DELETE FROM calibration_metadata WHERE key IN('dataset_uuid','reference_protocol')"));
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory)); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        QCOMPARE(f.readCalibration(f.calibration).size(),0); QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records WHERE visible=0").toInt(),1);
        QCOMPARE(local.scalar("SELECT measurements FROM calibration_points ORDER BY point_index LIMIT 1").toString(),raw);
        const QString identity = local.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString(); QVERIFY(!identity.isEmpty());
        f.calibration.close(); QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory));
        QCOMPARE(local.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString(),identity);
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
    }
    void unknownHistoricalIdentityFailsClosed()
    {
        Fixture f; QVERIFY(f.initialize()); const CalibrationRecord input = f.record(); QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); })); f.calibration.close();
        Database catalog(f.catalogPath), local(f.localPath);
        QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='calibration'"));
        QVERIFY(catalog.exec("DELETE FROM catalog_batch_reference WHERE owner_module='calibration'"));
        QVERIFY(catalog.exec("UPDATE project_base SET project_en_name='BROKEN_IDENTITY' WHERE id="+QString::number(f.projectId)));
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory,false));
        QCOMPARE(local.scalar("SELECT project_name FROM calibration_records").toString(),QString("GUARD_A"));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(),2); QCOMPARE(f.projectHolds(),0); QCOMPARE(f.batchHolds(),0);
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,input); },false));
        QVERIFY(catalog.exec("UPDATE project_base SET project_en_name='GUARD_A' WHERE id="+QString::number(f.projectId)));
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory)); QCOMPARE(f.projectHolds(),1);
    }
    void missingProtocolSchemaAndIncompleteIdentityFailClosed()
    {
        Fixture f; QVERIFY(f.initialize()); f.calibration.close(); Database local(f.localPath), catalog(f.catalogPath);
        QVERIFY(local.exec("DELETE FROM calibration_metadata WHERE key='dataset_uuid'"));
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory,false));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_metadata WHERE key='dataset_uuid'").toInt(),0);
        QVERIFY(local.exec("DELETE FROM calibration_metadata WHERE key='reference_protocol'"));
        QVERIFY(catalog.exec("DROP TABLE catalog_batch_reference"));
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory,false));
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,f.record()); },false));
    }
    void disabledProjectKeepsHistoryButRejectsNewRecord()
    {
        Fixture f; QVERIFY(f.initialize()); const CalibrationRecord input = f.record(); QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); }));
        ProjectRecord disabled = f.project(f.projectId); disabled.enabled = false;
        QVERIFY(f.projects.call([&] { f.projects.store->save(2,disabled); }));
        f.calibration.close(); QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory));
        CalibrationRecord historical = f.readCalibration(f.calibration).first(); historical.points[0].measurements[0] = 12.5;
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(3,historical); },false));
        historical = f.readCalibration(f.calibration).first(); QVERIFY(f.calibration.call([&] { f.calibration.store->copy(4,historical.id,historical.revision); },false));
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(5,input); },false));
        Database local(f.localPath);
        const QString raw=local.scalar("SELECT measurements FROM calibration_points ORDER BY point_index LIMIT 1").toString();
        const qint64 revision=local.scalar("SELECT revision FROM calibration_records").toLongLong();
        QVERIFY(f.calibration.call([&] { f.calibration.store->remove(6,historical.id,historical.revision); },false));
        QVERIFY(f.calibration.errors.last().at(2).toString().contains(QStringLiteral("停用")));
        QCOMPARE(local.scalar("SELECT visible FROM calibration_records").toInt(),1);
        QCOMPARE(local.scalar("SELECT revision FROM calibration_records").toLongLong(),revision);
        QCOMPARE(local.scalar("SELECT measurements FROM calibration_points ORDER BY point_index LIMIT 1").toString(),raw);
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
    }
    void datasetsAndQcCannotReleaseEachOthersHolds()
    {
        Fixture f; QVERIFY(f.initialize()); QVERIFY(f.addQcReference());
        Worker<CalibrationStore> second; const QString secondPath = f.directory.filePath("calibration_second");
        QVERIFY(f.openCalibration(second,secondPath)); const CalibrationRecord input = f.record();
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); }));
        QVERIFY(second.call([&] { second.store->save(1,input); }));
        QCOMPARE(f.projectHolds(),2); QCOMPARE(f.batchHolds(),2); QCOMPARE(f.projectHolds("quality_control"),1);
        Database firstLocal(f.localPath), secondLocal(QDir(secondPath).filePath("calibration_local.db"));
        const QString firstId = firstLocal.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString();
        const QString secondId = secondLocal.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString(); QVERIFY(firstId != secondId);
        QVERIFY(firstLocal.exec("CREATE TRIGGER reject_point BEFORE INSERT ON calibration_points BEGIN SELECT RAISE(ABORT,'cross-dataset failure'); END"));
        CalibrationRecord existing = f.readCalibration(f.calibration).first(); existing.points[0].measurements[0] = 12.5;
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(2,existing); },false));
        QCOMPARE(f.projectHolds(),2); QCOMPARE(f.batchHolds(),2); QCOMPARE(f.projectHolds("quality_control"),1);
        QVERIFY(f.qc.call([&] { f.qc.store->removeControl(4,f.qcControlId); }));
        QCOMPARE(f.projectHolds("quality_control"),0); QCOMPARE(f.projectHolds(),2); QCOMPARE(f.batchHolds(),2);
        second.close(); QVERIFY(f.openCalibration(second,secondPath)); QCOMPARE(f.projectHolds(),2); QCOMPARE(f.batchHolds(),2);
        qInfo() << "DATASET_IDS" << firstId << secondId << "calibration project/batch holds" << f.projectHolds() << f.batchHolds();
    }
    void sameDatasetConcurrentFailureCannotReleaseCommittedHold()
    {
        Fixture f; QVERIFY(f.initialize()); Worker<CalibrationStore> second; QVERIFY(f.openCalibration(second,f.calibrationDirectory)); Database local(f.localPath);
        QVERIFY(local.exec("CREATE TRIGGER reject_point BEFORE INSERT ON calibration_points WHEN (SELECT blood_batch FROM calibration_records WHERE id=NEW.record_id)='FAIL' BEGIN SELECT RAISE(ABORT,'first writer fails'); END"));
        CalibrationRecord fail = f.record(); fail.bloodBatch = "FAIL"; const CalibrationRecord success = f.record();
        f.calibration.ok.clear(); f.calibration.errors.clear(); second.ok.clear(); second.errors.clear();
        QMetaObject::invokeMethod(f.calibration.store,[&] { f.calibration.store->save(1,fail); },Qt::QueuedConnection);
        QMetaObject::invokeMethod(second.store,[&] { second.store->save(2,success); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return f.calibration.errors.count() == 1 && second.ok.count() == 1; }));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),1); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        QCOMPARE(f.readCalibration(second).first().bloodBatch,QString("ANON_CAL"));
    }
    void automaticRollbackKeepsOperationSerializationAcrossTwoWorkers()
    {
        Fixture f; QVERIFY(f.initialize());
        RollbackGate gate(f.calibration); QVERIFY(gate.install());
        Worker<CalibrationStore> second; QVERIFY(f.openCalibration(second,f.calibrationDirectory));
        Database local(f.localPath);
        QVERIFY(local.exec("CREATE TRIGGER automatic_rollback BEFORE INSERT ON calibration_points WHEN (SELECT blood_batch FROM calibration_records WHERE id=NEW.record_id)='FAIL' BEGIN SELECT RAISE(ROLLBACK,'forced automatic rollback'); END"));
        CalibrationRecord fail=f.record(); fail.bloodBatch="FAIL";
        const CalibrationRecord success=f.record();
        f.calibration.ok.clear(); f.calibration.errors.clear();
        QMetaObject::invokeMethod(f.calibration.store,[&](){f.calibration.store->save(1,fail);},Qt::QueuedConnection);
        QVERIFY(waitFor([&](){return gate.entered.available()>0;}));
        QVERIFY(second.call([&](){second.store->save(2,success);},false));
        QVERIFY(second.errors.last().at(2).toString().contains(QStringLiteral("操作保护锁")));
        gate.proceed.release();
        QVERIFY(waitFor([&](){return f.calibration.errors.count()==1;}));
        QVERIFY(gate.continued.load());
        QVERIFY(f.calibration.errors.last().at(2).toString().contains("forced automatic rollback"));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(),0);
        QCOMPARE(f.projectHolds(),0); QCOMPARE(f.batchHolds(),0);
        QVERIFY(second.call([&](){second.store->save(3,success);}));
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        QCOMPARE(f.readCalibration(second).first().bloodBatch,QString("ANON_CAL"));
        qInfo()<<"AUTOMATIC_ROLLBACK barrier: second real worker rejected by operation lock; after failed writer cleanup explicit retry commits with both holds";
    }
    void automaticRollbackCleanupFailureRetainsExplicitProtection()
    {
        Fixture f; QVERIFY(f.initialize()); Database local(f.localPath),catalog(f.catalogPath);
        QVERIFY(local.exec("CREATE TRIGGER automatic_rollback BEFORE INSERT ON calibration_points BEGIN SELECT RAISE(ROLLBACK,'forced rollback for cleanup'); END"));
        QVERIFY(catalog.exec("CREATE TRIGGER fail_cleanup BEFORE DELETE ON catalog_batch_reference WHEN OLD.owner_module='calibration' BEGIN SELECT RAISE(ABORT,'forced cleanup refusal'); END"));
        QVERIFY(f.calibration.call([&](){f.calibration.store->save(1,f.record());},false));
        const QString error=f.calibration.errors.last().at(2).toString();
        QVERIFY(error.contains("forced rollback for cleanup"));
        QVERIFY(error.contains(QStringLiteral("新增保护预约保留")));
        QVERIFY(error.contains("forced cleanup refusal"));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        f.calibration.close();
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory,false));
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        QVERIFY(f.calibration.errors.last().at(2).toString().contains(QStringLiteral("无对应本地记录")));
    }
    void sameCanonicalFileAllowsTwoWorkersAndSameBinding()
    {
        Fixture f; QVERIFY(f.initialize()); Worker<CalibrationStore> second;
        QVERIFY(f.openCalibration(second,QDir(f.calibrationDirectory).filePath("../calibration")));
        const CalibrationRecord input=f.record();
        QVERIFY(f.calibration.call([&](){f.calibration.store->save(1,input);}));
        QVERIFY(second.call([&](){second.store->save(2,input);}));
        QCOMPARE(f.readCalibration(second).size(),2); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        Database local(f.localPath),catalog(f.catalogPath);
        const QString dataset=local.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString();
        QCOMPARE(catalog.scalar("SELECT value FROM catalog_metadata WHERE key='calibration_dataset_path:"+dataset+"'").toString(),QFileInfo(f.localPath).canonicalFilePath());
    }
    void sameUuidPhysicalCopyFailsClosedWithoutChangingIdentity()
    {
        Fixture f; QVERIFY(f.initialize()); f.calibration.close();
        const QString copiedDirectory=f.directory.filePath("copied_calibration");
        QVERIFY(QDir().mkpath(copiedDirectory));
        const QString copiedPath=QDir(copiedDirectory).filePath("calibration_local.db");
        QVERIFY(QFile::copy(f.localPath,copiedPath));
        Database original(f.localPath),copied(copiedPath),catalog(f.catalogPath);
        const QString dataset=original.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString();
        QCOMPARE(copied.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString(),dataset);
        Worker<CalibrationStore> second;
        QVERIFY(f.openCalibration(second,copiedDirectory,false));
        QVERIFY(second.errors.last().at(2).toString().contains(QStringLiteral("同UUID副本")));
        QCOMPARE(copied.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString(),dataset);
        QCOMPARE(copied.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        QCOMPARE(catalog.scalar("SELECT value FROM catalog_metadata WHERE key='calibration_dataset_path:"+dataset+"'").toString(),QFileInfo(f.localPath).canonicalFilePath());
        QCOMPARE(f.projectHolds(),0); QCOMPARE(f.batchHolds(),0);
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory));
        QVERIFY(f.calibration.call([&](){f.calibration.store->save(1,f.record());}));
        QVERIFY(second.call([&](){second.store->save(2,f.record());},false));
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
        QCOMPARE(copied.scalar("SELECT count(*) FROM calibration_records").toInt(),0);
        qInfo()<<"CLONE_FAIL_CLOSED same UUID different physical path rejected even while original closed; original path reopen succeeds, no UUID/hold reset";
    }
    void destroyedTypesOrHistoricalUnitCannotBeCopied_data()
    {
        QTest::addColumn<int>("damage");
        QTest::newRow("missing-original-type") << 0;
        QTest::newRow("missing-calibration-type") << 1;
        QTest::newRow("missing-historical-unit") << 2;
    }
    void destroyedTypesOrHistoricalUnitCannotBeCopied()
    {
        QFETCH(int,damage);
        Fixture f; QVERIFY(f.initialize()); const CalibrationRecord input = f.record();
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); }));
        const CalibrationRecord historical = f.readCalibration(f.calibration).first();
        Database catalog(f.catalogPath), local(f.localPath);
        if (damage == 0) QVERIFY(catalog.exec("DELETE FROM project_result WHERE project_id="+QString::number(f.projectId)+" AND result_type='Original'"));
        else if (damage == 1) QVERIFY(catalog.exec("DELETE FROM project_result WHERE project_id="+QString::number(f.projectId)+" AND result_type='Calibration'"));
        else
        {
            ProjectRecord current = f.project(f.projectId);
            for (ProjectResult &result : current.results) if (result.type == "Calibration") result.unit = "a.u.x10";
            QVERIFY(f.projects.call([&] { f.projects.store->save(2,current); }));
            // 明确制造损坏夹具，不走产品行为：删仍被其他项目引用的历史单位。
            QVERIFY(catalog.exec("PRAGMA foreign_keys=OFF")); QVERIFY(catalog.exec("DELETE FROM option_unit WHERE unit='a.u.'"));
        }
        QVERIFY(f.calibration.call([&] { f.calibration.store->copy(3,historical.id,historical.revision); },false));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),1);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(),2);
        QCOMPARE(f.readCalibration(f.calibration).first().points.first().concentration,100.125);
        QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
    }
    void orphanReservationIsRetainedUntilExplicitReconciliation()
    {
        Fixture f; QVERIFY(f.initialize()); const CalibrationRecord input = f.record();
        QVERIFY(f.calibration.call([&] { f.calibration.store->save(1,input); })); f.calibration.close();
        Database local(f.localPath), catalog(f.catalogPath);
        const QString dataset = local.scalar("SELECT value FROM calibration_metadata WHERE key='dataset_uuid'").toString();
        const QString orphan = QStringLiteral("dataset:%1:project:%2:batch:0").arg(dataset).arg(f.otherProjectId);
        QSignalSpy held(f.projects.store,&ProjectsStore::projectReferenceSucceeded);
        QMetaObject::invokeMethod(f.projects.store,[&] { f.projects.store->holdProjectReference(2,"calibration",orphan,f.otherProjectId); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return held.count() == 1; })); QCOMPARE(f.projectHolds(),2);
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory,false)); QCOMPARE(f.projectHolds(),2);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(),1);
        // 已用本地身份表核对无对应记录，显式调用所属项目库API释放这一笔。
        held.clear();
        QMetaObject::invokeMethod(f.projects.store,[&] { f.projects.store->releaseProjectReference(3,"calibration",orphan); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return held.count() == 1; }));
        QVERIFY(f.openCalibration(f.calibration,f.calibrationDirectory)); QCOMPARE(f.projectHolds(),1); QCOMPARE(f.batchHolds(),1);
    }
};
QTEST_GUILESS_MAIN(CalibrationReferenceTest)
#include "calibration_reference_test.moc"
