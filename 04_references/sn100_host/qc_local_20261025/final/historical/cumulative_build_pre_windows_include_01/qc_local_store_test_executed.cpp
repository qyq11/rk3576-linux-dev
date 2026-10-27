#include "qc_store.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QCryptographicHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
#include <QUuid>
#include <functional>

namespace
{
bool await(const std::function<bool()> &condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 4000) { QCoreApplication::processEvents(); QTest::qWait(2); }
    return condition();
}
QByteArray digest(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex();
}
class Fixture
{
public:
    QTemporaryDir directory;
    QThread thread;
    QcStore *store = nullptr;
    QSignalSpy *ok = nullptr, *errors = nullptr, *snapshots = nullptr;
    QSqlDatabase qc, catalog;
    QString qcName, catalogName, initializationError;
    QcProjectRefs projects;
    explicit Fixture() : directory(QDir(qEnvironmentVariable("SN100_QC_TEST_ROOT")).filePath("case-XXXXXX"))
    {
        if (!directory.isValid()) { initializationError = "test directory unavailable"; return; }
        const QString catalogRoot = directory.filePath("catalog");
        QDir().mkpath(catalogRoot);
        QFile tag(QDir(catalogRoot).filePath(".sn100-catalog-offline-v1")); tag.open(QIODevice::WriteOnly); tag.write("sn100-catalog-offline-v1"); tag.close();
        catalogName = "fixtureCatalog-" + QUuid::createUuid().toString();
        catalog = QSqlDatabase::addDatabase("QSQLITE",catalogName); catalog.setDatabaseName(QDir(catalogRoot).filePath("catalog.db"));
        if (!catalog.open()) { initializationError = catalog.lastError().text(); return; }
        QSqlQuery query(catalog);
        const QStringList sql{
            "PRAGMA foreign_keys=ON",
            "CREATE TABLE catalog_metadata(key TEXT PRIMARY KEY,value TEXT)",
            "INSERT INTO catalog_metadata VALUES('schema','sn100-catalog-offline-v1')",
            "CREATE TABLE project_base(id INTEGER PRIMARY KEY,project_en_name TEXT NOT NULL UNIQUE)",
            "CREATE TABLE option_unit(unit TEXT PRIMARY KEY,ratio REAL)",
            "CREATE TABLE project_result(project_id INTEGER,result_type TEXT,result_name TEXT,decimal_point INTEGER,unit_name TEXT)",
            "CREATE TABLE catalog_project_reference(owner_module TEXT,reference_key TEXT,project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE RESTRICT,PRIMARY KEY(owner_module,reference_key))",
            "INSERT INTO project_base VALUES(11,'ANON_A'),(22,'ANON_B')",
            "INSERT INTO option_unit VALUES('s',1),('a.u.x10',10)",
            "INSERT INTO project_result VALUES(11,'Original','ANON_A',2,'s'),(11,'Calibration','ANON_AC',3,'a.u.x10'),(22,'INR','ANON_B',4,'s')"};
        for (const QString &statement : sql) if (!query.exec(statement)) { initializationError = query.lastError().text(); return; }
        QcProjectRef a; a.id = 11; a.name = "ANON_A";
        QcResultRef original; original.type = 1; original.name = "ANON_A"; original.unit = "s";
        QcResultRef calibration; calibration.type = 2; calibration.name = "ANON_AC"; calibration.unit = "a.u.x10"; calibration.decimals = 3; calibration.ratio = 10;
        a.results = {original,calibration};
        QcProjectRef b; b.id = 22; b.name = "ANON_B";
        QcResultRef inr; inr.type = 4; inr.name = "ANON_B"; inr.unit = "s"; inr.decimals = 4; b.results = {inr};
        projects = {a,b};
        store = new QcStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        ok = new QSignalSpy(store, &QcStore::succeeded); errors = new QSignalSpy(store, &QcStore::error); snapshots = new QSignalSpy(store, &QcStore::snapshotReady);
        QSignalSpy ready(store,&QcStore::ready), bound(store,&QcStore::catalogBound), supplied(store,&QcStore::catalogReady);
        thread.start();
        QMetaObject::invokeMethod(store, [this] { store->initialize(directory.filePath("qc")); }, Qt::QueuedConnection);
        if (!await([&] { return ready.count() || errors->count(); }) || ready.isEmpty()) { initializationError = "QC initialization failed"; return; }
        QMetaObject::invokeMethod(store, [this,catalogRoot] { store->bindCatalog(catalogRoot); store->setCatalog(1,projects); }, Qt::QueuedConnection);
        if (!await([&] { return (bound.count() && supplied.count()) || errors->count(); }) || bound.isEmpty() || supplied.isEmpty()) { initializationError = errors->isEmpty() ? "catalog bind timed out" : errors->last().at(2).toString(); return; }
        qcName = "fixtureQc-" + QUuid::createUuid().toString();
        qc = QSqlDatabase::addDatabase("QSQLITE",qcName); qc.setDatabaseName(directory.filePath("qc/quality_control.db"));
        if (!qc.open()) initializationError = qc.lastError().text();
    }
    ~Fixture()
    {
        if (store && thread.isRunning()) { QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection); thread.quit(); thread.wait(); }
        delete ok; delete errors; delete snapshots;
        qc.close(); qc = QSqlDatabase(); if (!qcName.isEmpty()) QSqlDatabase::removeDatabase(qcName);
        catalog.close(); catalog = QSqlDatabase(); if (!catalogName.isEmpty()) QSqlDatabase::removeDatabase(catalogName);
    }
    bool call(const std::function<void()> &action, bool success = true)
    {
        ok->clear(); errors->clear();
        QMetaObject::invokeMethod(store, action, Qt::QueuedConnection);
        if (!await([this] { return !ok->isEmpty() || !errors->isEmpty(); })) return false;
        if (!ok->isEmpty()) qInfo() << "COMMITTED" << ok->last();
        if (!errors->isEmpty()) qInfo() << "REJECTED" << errors->last();
        if (success && !errors->isEmpty()) qWarning() << "Unexpected error:" << errors->last();
        return success ? ok->count() == 1 && errors->isEmpty() : errors->count() == 1 && ok->isEmpty();
    }
    QcControls read()
    {
        snapshots->clear(); errors->clear();
        QMetaObject::invokeMethod(store, [this] { store->load(900); }, Qt::QueuedConnection);
        if (!await([this] { return !snapshots->isEmpty() || !errors->isEmpty(); }) || snapshots->isEmpty()) return {};
        return qvariant_cast<QcControls>(snapshots->last().at(1));
    }
    qint64 addControl(QString name = "质控A")
    {
        QcControl control; control.name = name; control.manufacturer = "匿名厂家";
        if (!call([this,control] { store->saveControl(2,control); })) return 0;
        return ok->last().at(2).toLongLong();
    }
    qint64 batch(qint64 controlId, QString name)
    {
        QcBatch batch; batch.controlId = controlId; batch.name = name; batch.expirationDate = "2027/01/31";
        if (!call([this,batch] { store->saveBatch(3,batch); })) return 0;
        return ok->last().at(2).toLongLong();
    }
    bool sql(const QString &statement, bool inCatalog = false)
    {
        QSqlQuery query(inCatalog ? catalog : qc);
        const bool result = query.exec(statement);
        if (!result) qWarning() << statement << query.lastError().text();
        return result;
    }
    QVariant scalar(const QString &statement, bool inCatalog = false)
    {
        QSqlQuery query(inCatalog ? catalog : qc); if (!query.exec(statement) || !query.next()) return {};
        return query.value(0);
    }
};
}

class QcLocalStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QcStore::registerTypes();
        QVERIFY2(!qEnvironmentVariable("SN100_QC_TEST_ROOT").isEmpty(), "Set explicit dedicated SN100_QC_TEST_ROOT");
        QVERIFY(QDir().mkpath(qEnvironmentVariable("SN100_QC_TEST_ROOT")));
    }
    void validation()
    {
        QcControl control; control.name = "匿名质控A"; control.manufacturer = "厂家01";
        QVERIFY(QcStore::validateControl(control).isEmpty());
        const QStringList invalid{"", " padded", "bad/name", "A\nB", QString(17,'a'), QString("A")+QChar(0x200b)};
        for (const QString &name : invalid) { control.name = name; QVERIFY2(!QcStore::validateControl(control).isEmpty(),qPrintable(name)); }
        QcBatch batch; batch.controlId = 1; batch.name = "BATCH_A"; batch.expirationDate = "2026/02/30";
        QVERIFY(!QcStore::validateBatch(batch).isEmpty()); batch.expirationDate = "2024/02/29"; QVERIFY(QcStore::validateBatch(batch).isEmpty());
        QcRule rule; rule.id=1; rule.associationId=1; rule.resultType=1; rule.target="1.25";
        QVERIFY(QcStore::validateRules({rule}).isEmpty());
        for (const QString &invalidNumber : QStringList{"1\n", "1\r\n", "1\t", "NaN", "1e2", "-1", "10000", "1.00001"})
        {
            rule.target=invalidNumber;
            QVERIFY2(!QcStore::validateRules({rule}).isEmpty(), qPrintable(QStringLiteral("must reject numeric text: ")+invalidNumber.toHtmlEscaped()));
        }
    }
    void controlSaveReopenStableIdsAndUsed()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError));
        const qint64 first = f.addControl(), second = f.addControl("质控B"); QVERIFY(first && second && first != second);
        QcControl edit = f.read().at(1); edit.name = "质控乙"; edit.used = false;
        QcControl duplicate = edit; duplicate.name = "质控A";
        QVERIFY(f.call([&] { f.store->saveControl(3,duplicate); },false));
        QVERIFY(f.call([&] { f.store->saveControl(4,edit); }));
        QVERIFY(f.call([&] { f.store->saveControl(5,edit); })); // unchanged save is valid
        QCOMPARE(f.read().at(1).id,second); QCOMPARE(f.read().at(1).name,QString("质控乙")); QVERIFY(f.read().at(1).used);
        QVERIFY(f.call([&] { f.store->saveControl(6,QcControl()); },false));
        QVERIFY(f.sql("UPDATE quality_control_list SET is_used=0 WHERE id="+QString::number(first)));
        QCOMPARE(f.read().at(0).used,false); QCOMPARE(f.read().at(1).id,second);
        QSignalSpy closed(f.store,&QcStore::closed), ready(f.store,&QcStore::ready);
        QMetaObject::invokeMethod(f.store,[&] { f.store->close(); f.store->initialize(f.directory.filePath("qc")); },Qt::QueuedConnection);
        QVERIFY(await([&] { return closed.count() && ready.count(); }));
        QCOMPARE(f.read().at(1).id,second); QCOMPARE(f.read().at(1).name,QString("质控乙"));
        QVERIFY(f.call([&] { f.store->removeControl(7,second); })); QCOMPARE(f.read().size(),1);
    }
    void associationRulesAndDurableReferences()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        QVERIFY(f.call([&] { f.store->setProjects(4,id,f.projects); }));
        QcControls snapshot = f.read(); QCOMPARE(snapshot.first().projects.size(),2); QCOMPARE(snapshot.first().projects.first().rules.size(),2);
        QCOMPARE(snapshot.first().projects.first().projectId,qint64(11)); QCOMPARE(snapshot.first().projects.first().rules.first().resultType,1);
        QCOMPARE(snapshot.first().projects.first().resultRefs.at(1).unit,QString("a.u.x10"));
        QCOMPARE(snapshot.first().projects.first().resultRefs.at(1).ratio,10.0);
        QCOMPARE(snapshot.first().projects.first().resultRefs.at(1).decimals,3);
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),2);
        const qint64 association = snapshot.first().projects.first().id;
        QVERIFY(f.call([&] { f.store->setProjects(5,id,f.projects); })); QCOMPARE(f.read().first().projects.first().id,association);
        QcProjectRefs duplicate{f.projects.first(),f.projects.first()};
        QVERIFY(f.call([&] { f.store->setProjects(6,id,duplicate); },false));
        QcProjectRefs unknown = {f.projects.first()}; unknown[0].results[0].type = 5;
        QVERIFY(f.call([&] { f.store->setProjects(7,id,unknown); },false));
        QcProjectRefs forged = {f.projects.first()}; forged[0].results[0].ratio = 10;
        QVERIFY(f.call([&] { f.store->setProjects(8,id,forged); },false));
        QVERIFY(f.call([&] { f.store->setProjects(9,id,{}); })); QCOMPARE(f.read().first().projects.size(),0);
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM quality_rules").toInt(),0); QCOMPARE(f.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),0);
    }
    void unitSnapshotAndDatasetReservationIsolation()
    {
        Fixture first, second;
        QVERIFY2(first.initializationError.isEmpty(),qPrintable(first.initializationError));
        QVERIFY2(second.initializationError.isEmpty(),qPrintable(second.initializationError));
        QSignalSpy rebound(second.store,&QcStore::catalogBound);
        const QString sharedCatalog = first.directory.filePath("catalog");
        QMetaObject::invokeMethod(second.store,[&] { second.store->bindCatalog(sharedCatalog); },Qt::QueuedConnection);
        QVERIFY(await([&] { return rebound.count() == 1; }));
        const qint64 a = first.addControl(), b = second.addControl(); QCOMPARE(a,b);
        QVERIFY(first.call([&] { first.store->setProjects(4,a,{first.projects.first()}); }));
        QVERIFY(second.call([&] { second.store->setProjects(4,b,{second.projects.first()}); }));
        QCOMPARE(first.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),2);
        const QString firstIdentity = first.scalar("SELECT value FROM qc_metadata WHERE key='dataset_uuid'").toString();
        const QString secondIdentity = second.scalar("SELECT value FROM qc_metadata WHERE key='dataset_uuid'").toString();
        QVERIFY(!firstIdentity.isEmpty() && firstIdentity != secondIdentity);
        QVERIFY(first.sql("UPDATE option_unit SET ratio=25 WHERE unit='a.u.x10'",true));
        QVERIFY(first.sql("UPDATE project_result SET decimal_point=6 WHERE result_type='Calibration'",true));
        QCOMPARE(first.read().first().projects.first().resultRefs.at(1).ratio,10.0);
        QCOMPARE(first.read().first().projects.first().resultRefs.at(1).decimals,3);
        qInfo() << "Stored unit snapshot" << first.read().first().projects.first().resultRefs.at(1).unit
                << first.read().first().projects.first().resultRefs.at(1).ratio << first.read().first().projects.first().resultRefs.at(1).decimals;
        QSignalSpy reopened(first.store,&QcStore::ready);
        QMetaObject::invokeMethod(first.store,[&] { first.store->close(); first.store->initialize(first.directory.filePath("qc")); first.store->bindCatalog(sharedCatalog); },Qt::QueuedConnection);
        QVERIFY(await([&] { return reopened.count() == 1; }));
        QCOMPARE(first.scalar("SELECT value FROM qc_metadata WHERE key='dataset_uuid'").toString(),firstIdentity);
        QCOMPARE(first.read().first().projects.first().resultRefs.at(1).ratio,10.0);
        QVERIFY(first.call([&] { first.store->removeControl(5,a); }));
        QCOMPARE(first.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),1);
        QVERIFY(first.scalar("SELECT reference_key FROM catalog_project_reference",true).toString().contains(secondIdentity));
        QVERIFY(second.call([&] { second.store->removeControl(6,b); }));
        QCOMPARE(first.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),0);
    }
    void readAndOpeningNeverRepairOrWrite()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        QVERIFY(f.batch(id,"LOT_A")); QVERIFY(f.batch(id,"LOT_B"));
        QVERIFY(f.sql("UPDATE quality_control_batch SET cur_batch=0,prv_batch=1")); // 原缺陷中可能已有两个previous
        const QString path = f.directory.filePath("qc/quality_control.db");
        const QByteArray before = digest(path); QVERIFY(!before.isEmpty());
        for (int i = 0; i < 3; ++i) QCOMPARE(f.read().first().batches.size(),2);
        QCOMPARE(digest(path),before);
        QSignalSpy ready(f.store,&QcStore::ready);
        QMetaObject::invokeMethod(f.store,[&] { f.store->close(); f.store->initialize(f.directory.filePath("qc")); },Qt::QueuedConnection);
        QVERIFY(await([&] { return ready.count() == 1; }));
        QCOMPARE(f.read().first().batches.size(),2); QCOMPARE(digest(path),before);
        qInfo() << "Open/read SHA256 unchanged" << before;
    }
    void failedCatalogRebindCannotUsePreviousConnection()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        f.errors->clear();
        QMetaObject::invokeMethod(f.store,[&] { f.store->bindCatalog(f.directory.filePath("missing_catalog")); },Qt::QueuedConnection);
        QVERIFY(await([&] { return f.errors->count() == 1; }));
        QVERIFY(f.call([&] { f.store->setProjects(5,id,{f.projects.first()}); },false));
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),0);
        QSignalSpy bound(f.store,&QcStore::catalogBound);
        QMetaObject::invokeMethod(f.store,[&] { f.store->bindCatalog(f.directory.filePath("catalog")); },Qt::QueuedConnection);
        QVERIFY(await([&] { return bound.count() == 1; }));
        QVERIFY(f.call([&] { f.store->setProjects(6,id,{f.projects.first()}); }));
    }
    void nameAndRulesSaveOneTransaction()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        QVERIFY(f.call([&] { f.store->setProjects(4,id,{f.projects.first()}); }));
        QcControl draft = f.read().first(); draft.name = "质控修改"; QcRule &rule = draft.projects[0].rules[0];
        rule.target = "12.3400"; rule.sdValue = "0.125"; rule.used = false; rule.strategies = {1,2,0,1,2,0,1,2,0};
        QVERIFY(f.sql("CREATE TRIGGER reject_rule BEFORE UPDATE ON quality_rules BEGIN SELECT RAISE(ABORT,'test rule write failure'); END"));
        QVERIFY(f.call([&] { f.store->saveControl(5,draft); },false));
        QCOMPARE(f.read().first().name,QString("质控A")); QVERIFY(f.read().first().projects.first().rules.first().target.isEmpty());
        QVERIFY(f.sql("DROP TRIGGER reject_rule")); QVERIFY(f.call([&] { f.store->saveControl(6,draft); }));
        const QcControl actual = f.read().first(); QCOMPARE(actual.name,draft.name); QCOMPARE(actual.projects[0].rules[0].target,QString("12.3400"));
        QCOMPARE(actual.projects[0].rules[0].strategies,rule.strategies); QVERIFY(!actual.projects[0].rules[0].used);
        draft.projects[0].rules[0].target = "10000"; QVERIFY(f.call([&] { f.store->saveControl(7,draft); },false));
        draft.projects[0].rules[0].target = "1.12345"; QVERIFY(f.call([&] { f.store->saveControl(8,draft); },false));
        draft.projects[0].rules[0].target.clear(); draft.projects[0].rules[0].id += 999; draft.name = "不应提交";
        QVERIFY(f.call([&] { f.store->saveControl(9,draft); },false)); QCOMPARE(f.read().first().name,QString("质控修改"));
    }
    void batchDatesGlobalUniquenessAndPersistence()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 a = f.addControl(), b = f.addControl("质控B");
        const qint64 first = f.batch(a,"LOT_A"); QVERIFY(first); QVERIFY(f.batch(a,"LOT_B"));
        QcBatch duplicate; duplicate.controlId = b; duplicate.name = "LOT_A"; duplicate.expirationDate = "2027/01/31";
        QVERIFY(f.call([&] { f.store->saveBatch(4,duplicate); },false));
        QcBatch invalid = duplicate; invalid.name = "LOT_C"; invalid.expirationDate = "2026/02/30";
        QVERIFY(f.call([&] { f.store->saveBatch(5,invalid); },false));
        invalid.expirationDate = "2020/01/01"; QVERIFY(f.call([&] { f.store->saveBatch(6,invalid); })); // 保存历史过期批次，不证明可用于检测
        const QcControls records = f.read(); QCOMPARE(records.first().batches.size(),2); QVERIFY(records.first().batches.first().current); QVERIFY(!records.first().batches.at(1).current);
        QcBatch edit = records.first().batches.at(1); edit.name = "LOT_B_EDIT"; edit.expirationDate = "2028/02/29"; edit.current = true;
        QVERIFY(f.call([&] { f.store->saveBatch(7,edit); }));
        QCOMPARE(f.read().first().batches.at(1).name,edit.name); QVERIFY(!f.read().first().batches.at(1).current); // edit不能偷改默认
        QVERIFY(f.call([&] { f.store->removeBatch(8,edit.id); })); QCOMPARE(f.read().first().batches.size(),1);
        QVERIFY(f.call([&] { f.store->removeBatch(9,first); },false));
    }
    void currentPreviousSwitchNoOpAndForeignId()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 control = f.addControl(), other = f.addControl("质控B");
        const qint64 a = f.batch(control,"LOT_A"), b = f.batch(control,"LOT_B"), c = f.batch(other,"LOT_C"); QVERIFY(a && b && c);
        QVERIFY(f.call([&] { f.store->switchBatch(4,control,b); }));
        QcBatches batches = f.read().first().batches; QVERIFY(batches[0].previous && !batches[0].current); QVERIFY(batches[1].current && !batches[1].previous);
        QVERIFY(f.sql("CREATE TABLE update_audit(count INTEGER)")); QVERIFY(f.sql("INSERT INTO update_audit VALUES(0)"));
        QVERIFY(f.sql("CREATE TRIGGER audit_update AFTER UPDATE ON quality_control_batch BEGIN UPDATE update_audit SET count=count+1; END"));
        QVERIFY(f.call([&] { f.store->switchBatch(5,control,b); })); QCOMPARE(f.scalar("SELECT count FROM update_audit").toInt(),0);
        QVERIFY(f.call([&] { f.store->switchBatch(6,control,c); },false)); QVERIFY(f.call([&] { f.store->switchBatch(7,control,999); },false));
        QCOMPARE(f.scalar("SELECT count FROM update_audit").toInt(),0);
        QVERIFY(f.call([&] { f.store->switchBatch(8,control,a); })); batches = f.read().first().batches; QVERIFY(batches[0].current && batches[1].previous);
    }
    void currentSwitchRollbackAndNoLoadRepair()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 control = f.addControl();
        const qint64 a = f.batch(control,"LOT_A"), b = f.batch(control,"LOT_B"); QVERIFY(a && b);
        QVERIFY(f.sql("CREATE TRIGGER reject_promotion BEFORE UPDATE ON quality_control_batch WHEN NEW.id="+QString::number(b)+" AND NEW.cur_batch=1 BEGIN SELECT RAISE(ABORT,'test middle failure'); END"));
        QVERIFY(f.call([&] { f.store->switchBatch(4,control,b); },false));
        QcBatches batches = f.read().first().batches; QVERIFY(batches[0].current && !batches[0].previous && !batches[1].current && !batches[1].previous);
        QVERIFY(f.sql("DROP TRIGGER reject_promotion")); QVERIFY(f.call([&] { f.store->switchBatch(5,control,b); }));
        QVERIFY(f.sql("UPDATE quality_control_batch SET cur_batch=0,prv_batch=0"));
        batches = f.read().first().batches; QVERIFY(!batches[0].current && !batches[1].current);
        QCOMPARE(f.scalar("SELECT SUM(cur_batch) FROM quality_control_batch").toInt(),0);
        QVERIFY(f.call([&] { f.store->switchBatch(6,control,a); }));
        QVERIFY(f.sql("UPDATE quality_control_batch SET cur_batch=1 WHERE id="+QString::number(b)));
        QVERIFY(f.call([&] { f.store->switchBatch(7,control,a); },false)); QCOMPARE(f.scalar("SELECT SUM(cur_batch) FROM quality_control_batch").toInt(),2);
    }
    void historyReferencesProtectNamesAndDeletion()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        QVERIFY(f.call([&] { f.store->setProjects(4,id,{f.projects.first()}); }));
        const qint64 batchId = f.batch(id,"LOT_A"); QVERIFY(batchId);
        QVERIFY(f.sql("INSERT INTO quality_data(control_name,project_name,control_batch,result_val,date,time) VALUES('质控A','ANON_A','LOT_A','12.3-13.4','2026/10/25','12:34:56')"));
        QcControl draft = f.read().first(); draft.name = "新名称";
        QVERIFY(f.call([&] { f.store->saveControl(5,draft); },false)); QVERIFY(f.call([&] { f.store->removeControl(6,id); },false));
        QVERIFY(f.call([&] { f.store->setProjects(7,id,{}); },false));
        QcBatch batch = f.read().first().batches.first(); batch.name = "LOT_RENAME";
        QVERIFY(f.call([&] { f.store->saveBatch(8,batch); },false));
        QVERIFY(f.sql("UPDATE quality_control_batch SET cur_batch=0")); QVERIFY(f.call([&] { f.store->removeBatch(9,batchId); },false));
        QCOMPARE(f.scalar("SELECT result_val FROM quality_data").toString(),QString("12.3-13.4")); QCOMPARE(f.scalar("SELECT COUNT(*) FROM quality_item_proj").toInt(),1);
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),1);
    }
    void sqliteLocksAndFailedAssociationRecover()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        QVERIFY(f.sql("BEGIN IMMEDIATE"));
        QcControl draft = f.read().first(); draft.manufacturer = "变更厂家";
        QVERIFY(f.call([&] { f.store->saveControl(4,draft); },false));
        QVERIFY(f.call([&] { f.store->setProjects(5,id,{f.projects.first()}); },false));
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),0); // failed QC save释放仅新增预约
        QVERIFY(f.sql("ROLLBACK")); QVERIFY(f.call([&] { f.store->saveControl(6,draft); }));
        QVERIFY(f.sql("BEGIN IMMEDIATE",true)); QVERIFY(f.call([&] { f.store->setProjects(7,id,{f.projects.first()}); },false));
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM quality_item_proj").toInt(),0);
        QVERIFY(f.sql("ROLLBACK",true)); QVERIFY(f.call([&] { f.store->setProjects(8,id,{f.projects.first()}); }));
        QVERIFY(f.sql("CREATE TRIGGER reject_association BEFORE INSERT ON quality_item_proj BEGIN SELECT RAISE(ABORT,'test association failure'); END"));
        QVERIFY(f.call([&] { f.store->setProjects(9,id,f.projects); },false));
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM catalog_project_reference",true).toInt(),1); // existing hold never被失败回收
        QCOMPARE(f.scalar("SELECT COUNT(*) FROM quality_item_proj").toInt(),1);
        QVERIFY(f.sql("DROP TRIGGER reject_association")); QVERIFY(f.call([&] { f.store->setProjects(10,id,f.projects); }));
    }
    void commitFailureRollsBackAndRetry()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); const qint64 id = f.addControl();
        // rollback journal下读事务允许BEGIN IMMEDIATE/UPDATE，但阻止写者COMMIT。
        QVERIFY(f.sql("BEGIN")); QSqlQuery reader(f.qc); QVERIFY(reader.exec("SELECT * FROM quality_control_list")); QVERIFY(reader.next());
        QcControl draft; draft.id = id; draft.name = "质控A"; draft.manufacturer = "新厂家";
        QVERIFY(f.call([&] { f.store->saveControl(4,draft); },false));
        reader.finish(); QVERIFY(f.sql("ROLLBACK")); QCOMPARE(f.scalar("SELECT factor_name FROM quality_control_list").toString(),QString("匿名厂家"));
        QVERIFY(f.call([&] { f.store->saveControl(5,draft); })); QCOMPARE(f.scalar("SELECT factor_name FROM quality_control_list").toString(),QString("新厂家"));
    }
    void wrongThreadAndExplicitClose()
    {
        Fixture f; QVERIFY2(f.initializationError.isEmpty(),qPrintable(f.initializationError)); f.errors->clear();
        f.store->load(123); QCOMPARE(f.errors->count(),1); QCOMPARE(f.errors->first().at(0).toLongLong(),qint64(123));
        QSignalSpy closed(f.store,&QcStore::closed); QMetaObject::invokeMethod(f.store,"close",Qt::QueuedConnection);
        QVERIFY(await([&] { return closed.count() == 1; }));
        QVERIFY(f.call([&] { f.store->saveControl(4,QcControl()); },false));
    }
};
QTEST_GUILESS_MAIN(QcLocalStoreTest)
#include "qc_local_store_test.moc"
