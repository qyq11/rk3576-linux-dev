#include "projects/projects_store.h"
#include "quality_control/qc_store.h"
#include <QtTest>
#include <QDir>
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
#include <atomic>
#include <functional>
#include <memory>

namespace
{
bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 6000)
    { QCoreApplication::processEvents(); QTest::qWait(2); }
    return condition();
}
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
        QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection);
        thread.quit(); thread.wait();
    }
    bool initialize(const std::function<void()> &action)
    {
        ready.clear(); errors.clear(); QMetaObject::invokeMethod(store,action,Qt::QueuedConnection);
        return waitFor([this] { return ready.count() || errors.count(); }) && ready.count()==1 && errors.isEmpty();
    }
    bool call(const std::function<void()> &action, bool expected=true)
    {
        ok.clear(); errors.clear(); QMetaObject::invokeMethod(store,action,Qt::QueuedConnection);
        if (!waitFor([this] { return ok.count() || errors.count(); })) return false;
        if (errors.count()) qInfo() << "EXPECTED_OR_ACTUAL_ERROR" << errors.last();
        return expected ? ok.count()==1 && errors.isEmpty() : errors.count()==1 && ok.isEmpty();
    }
    Store *store;
    QThread thread;
    QSignalSpy ready,ok,errors;
};

// Test-only SQLite API from official C-interface documentation:
// https://www.sqlite.org/c3ref/set_authorizer.html
// https://www.sqlite.org/c3ref/c_alter_table.html (TRANSACTION22, READ20)
// https://www.sqlite.org/c3ref/c_deny.html (DENY1); rescode.html (OK0).
// Callback only gates compilation, never queries/modifies its own connection.
struct sqlite3;
class BeginGate
{
public:
    explicit BeginGate(Worker<ProjectsStore> &owner) : worker(owner),library("libsqlite3.so.0") {}
    ~BeginGate()
    {
        proceed.release();
        if (native && api)
            QMetaObject::invokeMethod(worker.store,[this] { api(native,nullptr,nullptr); },Qt::BlockingQueuedConnection);
    }
    bool installBeforeOtherConnections()
    {
        if (!library.load()) return false;
        api=reinterpret_cast<Api>(library.resolve("sqlite3_set_authorizer"));
        if (!api) return false;
        const QStringList connections=QSqlDatabase::connectionNames();
        if (connections.size()!=1 || !connections.first().startsWith("Projects-")) return false;
        const QString connection=connections.first();
        bool installed=false;
        QMetaObject::invokeMethod(worker.store,[this,connection,&installed]
        {
            QVariant handle=QSqlDatabase::database(connection,false).driver()->handle();
            if (!handle.isValid() || QByteArray(handle.typeName())!="sqlite3*") return;
            native=*static_cast<sqlite3**>(handle.data());
            installed=api(native,&BeginGate::authorize,this)==0;
        },Qt::BlockingQueuedConnection);
        return installed;
    }
    static int authorize(void *context,int action,const char *first,const char *,const char *,const char *)
    {
        auto *gate=static_cast<BeginGate*>(context);
        if (action==20 && first && qstricmp(first,"option_unit")==0 && gate->denyRead.exchange(false)) return 1;
        if (action==22 && first && qstricmp(first,"BEGIN")==0 && gate->armed.exchange(false))
        {
            gate->entered.release();
            gate->continued=gate->proceed.tryAcquire(1,5000);
            if (!gate->continued) return 1;
        }
        return 0;
    }
    Worker<ProjectsStore> &worker;
    QLibrary library;
    using Callback=int (*)(void*,int,const char*,const char*,const char*,const char*);
    using Api=int (*)(sqlite3*,Callback,void*);
    Api api=nullptr;
    sqlite3 *native=nullptr;
    std::atomic_bool armed{false},continued{false},denyRead{false};
    QSemaphore entered,proceed;
};

class Database
{
public:
    explicit Database(const QString &path)
    {
        name="project-atomic-audit-"+QUuid::createUuid().toString();
        db=QSqlDatabase::addDatabase("QSQLITE",name); db.setDatabaseName(path); db.open();
    }
    ~Database() { db.close(); db=QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool exec(const QString &sql)
    {
        QSqlQuery query(db); const bool result=query.exec(sql);
        if (!result) qWarning() << "FIXTURE_SQL" << query.lastError().text();
        return result;
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(db); if (!query.exec(sql) || !query.next()) return {}; return query.value(0);
    }
    QString name;
    QSqlDatabase db;
};
class Fixture
{
public:
    Fixture() : directory(QDir(qEnvironmentVariable("SN100_PROJECT_ATOMIC_ROOT")).filePath("case-XXXXXX"))
    { directory.setAutoRemove(false); }
    bool initialize()
    {
        if (!directory.isValid()) return false;
        catalogDirectory=directory.filePath("catalog"); catalogPath=QDir(catalogDirectory).filePath("catalog.db");
        if (!a.initialize([this] { a.store->initialize(catalogDirectory,true); })) return false;
        AccountSession actor; actor.name="anonymousEngineer"; actor.role=4; actor.authenticated=true; actor.actionReady=true;
        QMetaObject::invokeMethod(a.store,[this,actor] { a.store->setSession(actor); },Qt::BlockingQueuedConnection);
        if (!a.call([this] { a.store->copy(1,1,"ATOMIC_OLD"); })) return false;
        projectId=a.ok.last().at(2).toLongLong(); initial=project();
        if (initial.id!=projectId) return false;
        gate.reset(new BeginGate(a));
        if (!gate->installBeforeOtherConnections()) return false;
        if (!b.initialize([this] { b.store->initialize(catalogDirectory,false); })) return false;
        QMetaObject::invokeMethod(b.store,[this,actor] { b.store->setSession(actor); },Qt::BlockingQueuedConnection);
        if (!qc.initialize([this] { qc.store->initialize(directory.filePath("qc")); })) return false;
        QSignalSpy bound(qc.store,&QcStore::catalogBound);
        QMetaObject::invokeMethod(qc.store,[this] { qc.store->bindCatalog(catalogDirectory); },Qt::QueuedConnection);
        if (!waitFor([&] { return bound.count() || qc.errors.count(); }) || bound.count()!=1) return false;
        QcControl control; control.name="QC_ATOMIC"; control.manufacturer="匿名厂家";
        if (!qc.call([this,control] { qc.store->saveControl(2,control); })) return false;
        controlId=qc.ok.last().at(2).toLongLong();
        qInfo().noquote() << "ANONYMOUS_RUNTIME" << directory.path() << "stable_project" << projectId;
        return true;
    }
    ProjectRecord project()
    {
        QSignalSpy loaded(a.store,&ProjectsStore::snapshotReady);
        QMetaObject::invokeMethod(a.store,[this] { a.store->load(50); },Qt::QueuedConnection);
        if (!waitFor([&] { return loaded.count(); })) return {};
        for (const auto &value:qvariant_cast<ProjectRecords>(loaded.last().at(1))) if(value.id==projectId) return value;
        return {};
    }
    bool link(const ProjectRecord &value)
    {
        QcProjectRef ref; ref.id=value.id; ref.name=value.name;
        for (const auto &result:value.results) if(result.type=="Calibration")
            ref.results.append({2,result.name,result.unit,result.decimals,result.unit=="a.u.x10"?10.0:1.0});
        QSignalSpy catalog(qc.store,&QcStore::catalogReady);
        QMetaObject::invokeMethod(qc.store,[this,ref] { qc.store->setCatalog(3,{ref}); },Qt::QueuedConnection);
        if (!waitFor([&] { return catalog.count(); })) return false;
        return qc.call([this,ref] { qc.store->setProjects(4,controlId,{ref}); });
    }
    QTemporaryDir directory;
    Worker<ProjectsStore> a,b;
    Worker<QcStore> qc;
    std::unique_ptr<BeginGate> gate; // Destruct first, unregister on A before workers close.
    QString catalogDirectory,catalogPath;
    qint64 projectId=0,controlId=0;
    ProjectRecord initial;
};
}

class ProjectReferenceAtomicTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const QString runtime=qEnvironmentVariable("SN100_PROJECT_ATOMIC_ROOT");
        QVERIFY(QDir::isAbsolutePath(runtime)); QVERIFY(QDir().mkpath(runtime));
        ProjectsStore::registerTypes(); QcStore::registerTypes();
    }
    void renamedThenReferencedCannotBeOverwritten_data()
    {
        QTest::addColumn<bool>("fullNameOnly");
        QTest::newRow("project-name") << false;
        QTest::newRow("full-name-only") << true;
    }
    void renamedThenReferencedCannotBeOverwritten()
    {
        QFETCH(bool,fullNameOnly);
        Fixture f; QVERIFY(f.initialize()); ProjectRecord stale=f.initial, changed=f.initial;
        stale.lisCode="STALE";
        if(fullNameOnly) changed.fullName=QStringLiteral("并发全称"); else changed.name="ATOMIC_NEW";
        f.a.ok.clear(); f.a.errors.clear(); f.gate->armed=true;
        QMetaObject::invokeMethod(f.a.store,[&] { f.a.store->save(10,stale); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return f.gate->entered.available()>0; }));
        QVERIFY(f.b.call([&] { f.b.store->save(11,changed); }));
        QVERIFY(f.link(changed));
        f.gate->proceed.release();
        QVERIFY(waitFor([&] { return f.a.ok.count() || f.a.errors.count(); }));
        QVERIFY(f.gate->continued.load());
        const ProjectRecord actual=f.project();
        Database catalog(f.catalogPath),local(f.directory.filePath("qc/quality_control.db"));
        qInfo() << "ATOMIC_INTERLEAVING" << "fullname_only" << fullNameOnly
                << "writerA_success" << f.a.ok.count() << "writerA_error" << f.a.errors.count()
                << "saved_names" << actual.name << actual.fullName << "expected_names" << changed.name << changed.fullName
                << "holds" << catalog.scalar("SELECT count(*) FROM catalog_project_reference")
                << "qc_saved_name" << local.scalar("SELECT proj_name FROM quality_item_proj");
        QCOMPARE(f.a.errors.count(),1); QCOMPARE(f.a.ok.count(),0);
        QCOMPARE(actual.name,changed.name); QCOMPARE(actual.fullName,changed.fullName);
        QCOMPARE(actual.lisCode,f.initial.lisCode);
        QCOMPARE(catalog.scalar("SELECT count(*) FROM catalog_project_reference").toInt(),1);
        QCOMPARE(local.scalar("SELECT proj_name FROM quality_item_proj").toString(),changed.name);
        QSignalSpy rebound(f.qc.store,&QcStore::catalogBound);
        QMetaObject::invokeMethod(f.qc.store,[&] { f.qc.store->bindCatalog(f.catalogDirectory); },Qt::QueuedConnection);
        QVERIFY(waitFor([&] { return rebound.count() || f.qc.errors.count(); })); QCOMPARE(rebound.count(),1);
    }
    void normalSaveHeldNamesAndIdentityRemainCompatible()
    {
        Fixture f; QVERIFY(f.initialize()); auto value=f.initial;
        value.fullName=QStringLiteral("正常全称"); value.instrumentProjectId="FORBIDDEN"; value.fixed=true;
        value.results[0].unit="a.u.x10"; value.results[1].unit="a.u.x10"; value.results[1].decimals=6;
        QVERIFY(f.a.call([&] { f.a.store->save(20,value); }));
        const auto saved=f.project(); QCOMPARE(saved.fullName,value.fullName);
        QCOMPARE(saved.instrumentProjectId,f.initial.instrumentProjectId); QCOMPARE(saved.fixed,f.initial.fixed);
        QCOMPARE(saved.results[0].unit,f.initial.results[0].unit); QCOMPARE(saved.results[1].unit,QString("a.u.x10"));
        QCOMPARE(saved.results[1].decimals,6); QVERIFY(f.link(saved));
        value=saved; value.name="HELD_RENAME";
        QVERIFY(f.a.call([&] { f.a.store->save(21,value); },false));
        value=saved; value.fullName=QStringLiteral("不得覆盖");
        QVERIFY(f.a.call([&] { f.a.store->save(22,value); },false));
        value=saved; value.lisCode="RETRY";
        QVERIFY(f.a.call([&] { f.a.store->save(23,value); })); QCOMPARE(f.project().lisCode,QString("RETRY"));
    }
    void everyValidationAndReadFailureRollsBackForRetry()
    {
        Fixture f; QVERIFY(f.initialize()); auto value=f.initial;
        value.id=99999; QVERIFY(f.a.call([&] { f.a.store->save(30,value); },false));
        value=f.initial; value.results.removeLast(); QVERIFY(f.a.call([&] { f.a.store->save(31,value); },false));
        value=f.initial; value.results[0].type="INVALID"; QVERIFY(f.a.call([&] { f.a.store->save(32,value); },false));
        value=f.initial; value.name="../invalid"; QVERIFY(f.a.call([&] { f.a.store->save(33,value); },false));
        value=f.initial; f.gate->denyRead=true;
        QVERIFY(f.a.call([&] { f.a.store->save(34,value); },false)); QVERIFY(!f.gate->denyRead.load());
        value.lisCode="AFTER"; QVERIFY(f.a.call([&] { f.a.store->save(35,value); }));
        QCOMPARE(f.project().lisCode,QString("AFTER"));
    }
    void actualSqlLocksWriteAndCommitFailuresRecover()
    {
        Fixture f; QVERIFY(f.initialize()); auto value=f.initial; value.fullName="WRITE_FAIL";
        Database external(f.catalogPath);
        QVERIFY(external.exec("CREATE TRIGGER reject_atomic_update BEFORE UPDATE ON project_base WHEN NEW.project_cn_name='WRITE_FAIL' BEGIN SELECT RAISE(ABORT,'atomic write failure'); END"));
        QVERIFY(f.a.call([&] { f.a.store->save(40,value); },false));
        QCOMPARE(f.project().fullName,f.initial.fullName); QVERIFY(external.exec("DROP TRIGGER reject_atomic_update"));
        value=f.initial; value.lisCode="LOCK";
        QVERIFY(external.exec("BEGIN IMMEDIATE"));
        QVERIFY(f.a.call([&] { f.a.store->save(41,value); },false));
        QVERIFY(external.exec("ROLLBACK")); QVERIFY(f.a.call([&] { f.a.store->save(42,value); }));
        QVERIFY(external.exec("BEGIN")); QSqlQuery reader(external.db);
        QVERIFY(reader.exec("SELECT * FROM project_base")); QVERIFY(reader.next());
        value.lisCode="COMMIT"; QVERIFY(f.a.call([&] { f.a.store->save(43,value); },false));
        reader.finish(); QVERIFY(external.exec("ROLLBACK"));
        QCOMPARE(f.project().lisCode,QString("LOCK"));
        QVERIFY(f.a.call([&] { f.a.store->save(44,value); })); QCOMPARE(f.project().lisCode,QString("COMMIT"));
    }
};
QTEST_GUILESS_MAIN(ProjectReferenceAtomicTest)
#include "project_reference_atomic_test.moc"
