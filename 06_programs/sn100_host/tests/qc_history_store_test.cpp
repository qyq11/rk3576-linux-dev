// 真实worker和隔离匿名库验证；夹具写入不构成产品手工录点/导入接口。
#include "quality_control/qc_store.h"
#include "projects/projects_store.h"
#include <QtTest>
#include <QCryptographicHash>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <cmath>
#include <functional>

namespace
{
bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QTest::qWait(3); }
    return condition();
}
QByteArray hash(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex();
}
// Qt5 QSignalSpy内部DirectConnection会在worker写QList；本测试的记录只在主线程排队修改。
class QueuedSignalRecords : public QObject, public QList<QVariantList>
{
public:
    template<class Sender,class... Arguments>
    QueuedSignalRecords(Sender *sender,void (Sender::*signal)(Arguments...))
    {
        connection=QObject::connect(sender,signal,this,[this](Arguments... arguments)
        {
            requireOwner();append(QVariantList{QVariant::fromValue(arguments)...});
        },Qt::QueuedConnection);
        if(!connection)qFatal("History test could not connect queued recorder");
    }
    ~QueuedSignalRecords() override
    {
        requireOwner();QObject::disconnect(connection);QCoreApplication::removePostedEvents(this,QEvent::MetaCall);
    }
    void flush(){requireOwner();QCoreApplication::sendPostedEvents(this,QEvent::MetaCall);}
    void clear(){requireOwner();QList<QVariantList>::clear();}
private:
    void requireOwner() const
    {
        if(QThread::currentThread()!=thread())qFatal("History signal records accessed outside owning test thread");
    }
    QMetaObject::Connection connection;
};
class Database
{
public:
    explicit Database(const QString &path)
    {
        name = QUuid::createUuid().toString(); db = QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(path); db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100"); db.open();
    }
    ~Database() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool exec(const QString &sql, const QVariantList &values = {})
    {
        QSqlQuery query(db);
        if (!query.prepare(sql)) { qWarning()<<query.lastError(); return false; }
        for (const QVariant &value:values) query.addBindValue(value);
        if (!query.exec()) { qWarning()<<query.lastError(); return false; }
        return true;
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(db); if (!query.exec(sql) || !query.next()) return {};
        return query.value(0);
    }
    QSqlDatabase db;
    QString name;
};
class Fixture
{
public:
    Fixture() : directory(QDir(qEnvironmentVariable("SN100_QC_HISTORY_TEST_ROOT")).filePath("case-XXXXXX")),
        projects(new ProjectsStore),qc(new QcStore),ready(qc,&QcStore::historyReady),errors(qc,&QcStore::historyError)
    {
        directory.setAutoRemove(false);
        projects->moveToThread(&thread); qc->moveToThread(&thread);
        QObject::connect(&thread,&QThread::finished,projects,&QObject::deleteLater);
        QObject::connect(&thread,&QThread::finished,qc,&QObject::deleteLater); thread.start();
    }
    ~Fixture()
    {
        QMetaObject::invokeMethod(qc,"close",Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(projects,"close",Qt::BlockingQueuedConnection); thread.quit(); thread.wait();
    }
    bool initialize()
    {
        if (!directory.isValid()) return false;
        catalogDirectory=directory.filePath("catalog"); catalogPath=QDir(catalogDirectory).filePath("catalog.db");
        qcDirectory=directory.filePath("qc"); qcPath=QDir(qcDirectory).filePath("quality_control.db");
        QueuedSignalRecords projectReady(projects,&ProjectsStore::ready),projectRows(projects,&ProjectsStore::snapshotReady),projectSaved(projects,&ProjectsStore::succeeded);
        QMetaObject::invokeMethod(projects,[&] { projects->initialize(catalogDirectory,true); },Qt::QueuedConnection);
        if (!waitFor([&] { return projectReady.count(); })) return false;
        AccountSession session;session.role=4;session.name="anonymous-engineer";session.authenticated=true;session.actionReady=true;
        QMetaObject::invokeMethod(projects,[&] { projects->setSession(session);projects->load(1); },Qt::QueuedConnection);
        if (!waitFor([&] { return projectRows.count(); })) return false;
        const ProjectRecords source=qvariant_cast<ProjectRecords>(projectRows.first().at(1));
        QMetaObject::invokeMethod(projects,[&,source] { projects->copy(2,source.first().id,"PROJ_QC_DEMO"); },Qt::QueuedConnection);
        if (!waitFor([&] { return projectSaved.count()==1; })) return false;
        projectId=projectSaved.first().at(2).toLongLong();
        // 显式匿名夹具增加原Original字段，绝不把既有Time猜成Original；不是产品写入能力。
        Database catalog(catalogPath);
        if (!catalog.exec("INSERT INTO project_result VALUES(?,'Original','ANON_RESULT',1,0,999,0,999,'','s',0)",{projectId})) return false;
        projectRows.clear();QMetaObject::invokeMethod(projects,[&] { projects->load(3); },Qt::QueuedConnection);
        if (!waitFor([&] { return projectRows.count(); })) return false;
        const auto records=qvariant_cast<ProjectRecords>(projectRows.first().at(1));
        const auto units=qvariant_cast<ProjectUnits>(projectRows.first().at(2));
        for (const ProjectRecord &record:records) if(record.id==projectId)
        {
            QcProjectRef ref;ref.id=record.id;ref.name=record.name;
            for(const ProjectResult &result:record.results)
            {
                const int type=result.type=="Original"?1:result.type=="Calibration"?2:0;if(!type)continue;
                QcResultRef value;value.type=type;value.name=result.name;value.unit=result.unit;value.decimals=result.decimals;
                for(const ProjectUnit &unit:units)if(unit.name==result.unit)value.ratio=unit.ratio;
                ref.results.append(value);
            }
            refs={ref};
        }
        QueuedSignalRecords localReady(qc,&QcStore::ready),bound(qc,&QcStore::catalogBound),supplied(qc,&QcStore::catalogReady);
        QMetaObject::invokeMethod(qc,[&] { qc->initialize(qcDirectory);qc->bindCatalog(catalogDirectory);qc->setCatalog(4,refs); },Qt::QueuedConnection);
        if(!waitFor([&]{return localReady.count()&&bound.count()&&supplied.count();}))return false;
        QcControl control;control.name="QC_DEMO";control.manufacturer="匿名厂家";
        if(!write([&,control]{qc->saveControl(5,control);}))return false;
        controlId=savedId;
        if(!write([&]{qc->setProjects(6,controlId,refs);}))return false;
        QueuedSignalRecords snapshot(qc,&QcStore::snapshotReady);
        QMetaObject::invokeMethod(qc,[&]{qc->load(7);},Qt::QueuedConnection);
        if(!waitFor([&]{return snapshot.count();}))return false;
        associationId=qvariant_cast<QcControls>(snapshot.first().at(1)).first().projects.first().id;
        QcBatch previous;previous.controlId=controlId;previous.name="LOT_PREV";previous.expirationDate="2027/01/31";
        if(!write([&,previous]{qc->saveBatch(8,previous);}))return false;
        previousId=savedId;
        QcBatch current=previous;current.name="LOT_CUR";
        if(!write([&,current]{qc->saveBatch(9,current);}))return false;
        currentId=savedId;
        if(!write([&]{qc->switchBatch(10,controlId,currentId);}))return false;
        qInfo().noquote()<<"FIXTURE"<<directory.path()<<"association="<<associationId;
        return true;
    }
    bool write(const std::function<void()> &action)
    {
        QueuedSignalRecords saved(qc,&QcStore::succeeded),failed(qc,&QcStore::error);
        QMetaObject::invokeMethod(qc,action,Qt::QueuedConnection);
        if(!waitFor([&]{return saved.count()||failed.count();})||!failed.isEmpty())return false;
        savedId=saved.first().at(2).toLongLong();return true;
    }
    QString encoded(int value)const
    {return QStringLiteral("[PROJ_QC_DEMO|0|=Original-%1:1/1-101|Calibration-%2:1/1-101]").arg(value).arg(value*10);}
    bool row(const QString &batch,const QString &date,const QString &time,const QString &value,
             const QString &control="QC_DEMO",const QString &project="PROJ_QC_DEMO")
    {
        Database local(qcPath);
        return local.exec("INSERT INTO quality_data(control_name,project_name,control_batch,date,time,result_val) VALUES(?,?,?,?,?,?)",
            {control,project,batch,date,time,value});
    }
    bool golden()
    {return row("LOT_PREV","2026/10/23","09:00:00",encoded(10))&&row("LOT_CUR","2026/10/24","10:00:00",encoded(12))&&row("LOT_CUR","2026/10/25","11:00:00",encoded(14));}
    bool isolationMetadata(qint64 *alternateAssociation,qint64 *otherControlAssociation)
    {
        QueuedSignalRecords copied(projects,&ProjectsStore::succeeded),loaded(projects,&ProjectsStore::snapshotReady);
        QMetaObject::invokeMethod(projects,[&]{projects->copy(20,projectId,"PROJ_QC_ALT");},Qt::QueuedConnection);
        if(!waitFor([&]{return copied.count();}))return false;
        const qint64 alternateId=copied.first().at(2).toLongLong();
        QMetaObject::invokeMethod(projects,[&]{projects->load(21);},Qt::QueuedConnection);
        if(!waitFor([&]{return loaded.count();}))return false;
        const auto records=qvariant_cast<ProjectRecords>(loaded.first().at(1));
        const auto units=qvariant_cast<ProjectUnits>(loaded.first().at(2));
        for(const auto &record:records)if(record.id==alternateId)
        {
            QcProjectRef ref;ref.id=record.id;ref.name=record.name;
            for(const auto &result:record.results)
            {
                const int type=result.type=="Original"?1:result.type=="Calibration"?2:0;if(!type)continue;
                QcResultRef value;value.type=type;value.name=result.name;value.unit=result.unit;value.decimals=result.decimals;
                for(const auto &unit:units)if(unit.name==result.unit)value.ratio=unit.ratio;
                ref.results.append(value);
            }
            refs.append(ref);
        }
        QueuedSignalRecords supplied(qc,&QcStore::catalogReady);
        QMetaObject::invokeMethod(qc,[&]{qc->setCatalog(22,refs);},Qt::QueuedConnection);
        if(!waitFor([&]{return supplied.count();}))return false;
        if(!write([&]{qc->setProjects(23,controlId,refs);}))return false;
        QcControl control;control.name="QC_OTHER";control.manufacturer="匿名厂家";
        if(!write([&,control]{qc->saveControl(24,control);}))return false;
        const qint64 otherId=savedId;
        if(!write([&]{qc->setProjects(25,otherId,{refs.first()});}))return false;
        QcBatch otherBatch;otherBatch.controlId=otherId;otherBatch.name="LOT_OTHER";otherBatch.expirationDate="2027/01/31";
        if(!write([&,otherBatch]{qc->saveBatch(26,otherBatch);}))return false;
        QcBatch old;old.controlId=controlId;old.name="LOT_OLD";old.expirationDate="2025/01/01";
        if(!write([&,old]{qc->saveBatch(27,old);}))return false;
        QueuedSignalRecords snapshot(qc,&QcStore::snapshotReady);
        QMetaObject::invokeMethod(qc,[&]{qc->load(28);},Qt::QueuedConnection);
        if(!waitFor([&]{return snapshot.count();}))return false;
        for(const auto &saved:qvariant_cast<QcControls>(snapshot.first().at(1)))for(const auto &association:saved.projects)
        {
            if(saved.id==controlId&&association.projectId==alternateId)*alternateAssociation=association.id;
            if(saved.id==otherId)*otherControlAssociation=association.id;
        }
        return *alternateAssociation>0&&*otherControlAssociation>0;
    }
    bool history(qint64 request,qint64 association,bool success=true)
    {
        synchronize();
        ready.clear();errors.clear();
        QMetaObject::invokeMethod(qc,[&,request,association]{qc->loadHistory(request,association);},Qt::QueuedConnection);
        if(!waitFor([&]{return ready.count()||errors.count();}))return false;
        synchronize();
        if(errors.count())qInfo()<<"HISTORY_ERROR"<<errors.first();
        return success?ready.count()==1&&errors.isEmpty():errors.count()==1&&ready.isEmpty();
    }
    QcHistoryReply reply()const{return qvariant_cast<QcHistoryReply>(ready.first().at(1));}
    void synchronize()
    {
        QMetaObject::invokeMethod(qc,[]{},Qt::BlockingQueuedConnection);ready.flush();errors.flush();
    }
    QTemporaryDir directory;
    QThread thread;
    ProjectsStore *projects;
    QcStore *qc;
    QueuedSignalRecords ready,errors;
    QString catalogDirectory,catalogPath,qcDirectory,qcPath;
    QcProjectRefs refs;
    qint64 projectId=0,controlId=0,associationId=0,currentId=0,previousId=0,savedId=0;
};
}

class QcHistoryStoreTest:public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(!qEnvironmentVariable("SN100_QC_HISTORY_TEST_ROOT").isEmpty());
        QVERIFY(QDir().mkpath(qEnvironmentVariable("SN100_QC_HISTORY_TEST_ROOT")));
        ProjectsStore::registerTypes();QcStore::registerTypes();
    }
    void realGoldenQueryImmutableUnknownUnitsAndNoWrites()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());
        const auto local=hash(f.qcPath),catalog=hash(f.catalogPath);
        QVERIFY(f.history(100,f.associationId));const auto reply=f.reply();
        QCOMPARE(f.ready.first().at(0).toLongLong(),qint64(100));QVERIFY(reply.complete());
        QCOMPARE(reply.controlId(),f.controlId);QCOMPARE(reply.projectId(),f.projectId);QCOMPARE(reply.associationId(),f.associationId);
        QCOMPARE(reply.currentBatch().id,f.currentId);QCOMPARE(reply.previousBatch().id,f.previousId);
        QCOMPARE(reply.sourceRowCount(),3);QCOMPARE(reply.rows().at(0).date,QString("2026/10/23"));QCOMPARE(reply.rows().at(1).date,QString("2026/10/25"));
        QCOMPARE(reply.rows().at(2).date,QString("2026/10/24"));QCOMPARE(reply.series().size(),2);
        const auto series=reply.series().first();QCOMPARE(series.validRawCount(),3);QVERIFY(series.selection().legacyCardVisible);
        QCOMPARE(series.selection().plotOrder.at(0).result.number,10.0);QCOMPARE(series.selection().plotOrder.at(1).result.number,14.0);
        QVERIFY(!series.summary().ok);QCOMPARE(series.summary().errorCode,QString("historical_unit_unavailable"));QCOMPARE(series.summary().unknownUnits,3);
        for(const auto &unit:reply.unitEvidence()){QVERIFY(unit.source==Sn100QcHistory::UnitSource::UnknownLegacy);QVERIFY(unit.unit.isEmpty());QVERIFY(std::isnan(unit.ratio));}
        auto changed=reply.rows();changed[0].resultValue="not-persisted";QVERIFY(reply.rows()[0].resultValue!="not-persisted");
        auto selection=series.selection();selection.plotOrder.clear();QCOMPARE(reply.series().first().selection().plotOrder.size(),3);
        QCOMPARE(hash(f.qcPath),local);QCOMPARE(hash(f.catalogPath),catalog);
    }
    void sameDayDuplicatesRetainedInDeterministicOrder()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());
        QVERIFY(f.row("LOT_CUR","2026/10/25","12:00:00",f.encoded(15)));
        QVERIFY(f.row("LOT_CUR","2026/10/25","12:00:00",f.encoded(16)));
        QVERIFY(f.history(101,f.associationId));const auto selection=f.reply().series().first().selection();
        QCOMPARE(selection.plotOrder.size(),5);QCOMPARE(selection.plotOrder[1].result.number,16.0);QCOMPARE(selection.plotOrder[2].result.number,15.0);
        Database local(f.qcPath);QCOMPARE(local.scalar("SELECT count(*) FROM quality_data").toInt(),5);
    }
    void stableControlProjectAndBatchScopeIsolated()
    {
        Fixture f;QVERIFY(f.initialize());qint64 alternate=0,otherControl=0;QVERIFY(f.isolationMetadata(&alternate,&otherControl));QVERIFY(f.golden());
        QVERIFY(f.row("LOT_OTHER","2026/10/26","10:00:00",f.encoded(999),"QC_OTHER"));
        QVERIFY(f.row("LOT_CUR","2026/10/26","10:00:00","[PROJ_QC_ALT|0|=Original-888:1/1-8]","QC_DEMO","PROJ_QC_ALT"));
        QVERIFY(f.row("LOT_OLD","2026/10/26","10:00:00",f.encoded(777)));
        QVERIFY(f.history(120,f.associationId));QCOMPARE(f.reply().sourceRowCount(),3);QCOMPARE(f.reply().excludedRowCount(),0);
        QVERIFY(f.history(121,alternate));QCOMPARE(f.reply().sourceRowCount(),1);QCOMPARE(f.reply().series().first().selection().plotOrder.first().result.number,888.0);
        QVERIFY(f.history(122,otherControl));QCOMPARE(f.reply().sourceRowCount(),1);QCOMPARE(f.reply().series().first().selection().plotOrder.first().result.number,999.0);
        Database local(f.qcPath);QCOMPARE(local.scalar("SELECT count(*) FROM quality_data").toInt(),6);
    }
    void identicalNamesAndIdsInOtherDatasetDoNotCrossRead()
    {
        Fixture first,second;QVERIFY(first.initialize());QVERIFY(second.initialize());
        QVERIFY(first.row("LOT_CUR","2026/10/25","10:00:00",first.encoded(10)));
        QVERIFY(second.row("LOT_CUR","2026/10/25","10:00:00",second.encoded(99)));
        QCOMPARE(first.associationId,second.associationId);QCOMPARE(first.controlId,second.controlId);
        QVERIFY(first.history(123,first.associationId));QVERIFY(second.history(123,second.associationId));
        QVERIFY(first.reply().datasetId()!=second.reply().datasetId());
        QCOMPARE(first.reply().series().first().selection().plotOrder.first().result.number,10.0);
        QCOMPARE(second.reply().series().first().selection().plotOrder.first().result.number,99.0);
    }
    void missingMalformedAndInvalidDateAreExplicitNotZero()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.row("LOT_CUR","2026/10/25","10:00:00","[PROJ_QC_DEMO|0|=Calibration-100:1/1-1]"));
        QVERIFY(f.row("LOT_CUR","2026/10/24","10:00:00","broken"));
        QVERIFY(f.row("LOT_CUR","2026/02/30","10:00:00",f.encoded(99)));
        QVERIFY(f.history(102,f.associationId));const auto reply=f.reply();
        QCOMPARE(reply.sourceRowCount(),3);QCOMPARE(reply.excludedRowCount(),1);
        const auto original=reply.series().first();QCOMPARE(original.validRawCount(),0);QCOMPARE(original.selection().invalidResultCount,2);
        for(const auto &point:original.selection().plotOrder){QVERIFY(!point.result.valid);QVERIFY(std::isnan(point.result.number));}
        QCOMPARE(reply.series().last().validRawCount(),1);QCOMPARE(reply.rows().last().date,QString("2026/02/30"));
    }
    void readRequiresLiveBindingAndDoesNotRepairHold()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());Database catalog(f.catalogPath);
        QVERIFY(catalog.exec("DELETE FROM catalog_project_reference WHERE owner_module='quality_control'"));
        QVERIFY(f.history(103,f.associationId,false));QCOMPARE(f.errors.first().at(2).toString(),QString("catalog_reference_missing"));
        QCOMPARE(catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='quality_control'").toInt(),0);
        QMetaObject::invokeMethod(f.qc,"close",Qt::BlockingQueuedConnection);
        QueuedSignalRecords localReady(f.qc,&QcStore::ready);QMetaObject::invokeMethod(f.qc,[&]{f.qc->initialize(f.qcDirectory);},Qt::QueuedConnection);
        QVERIFY(waitFor([&]{return localReady.count();}));QVERIFY(f.history(104,f.associationId,false));
        QCOMPARE(f.errors.first().at(2).toString(),QString("catalog_not_bound"));
    }
    void schemaAndPathMismatchFailClosed()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());Database catalog(f.catalogPath);const auto before=hash(f.qcPath);
        QVERIFY(catalog.exec("UPDATE catalog_metadata SET value='wrong-schema' WHERE key='schema'"));
        QVERIFY(f.history(105,f.associationId,false));QCOMPARE(f.errors.first().at(2).toString(),QString("catalog_schema_mismatch"));
        QVERIFY(catalog.exec("UPDATE catalog_metadata SET value='sn100-catalog-offline-v1' WHERE key='schema'"));
        QVERIFY(catalog.exec("UPDATE catalog_metadata SET value='wrong-path' WHERE key LIKE 'quality_control_dataset_path:%'"));
        QVERIFY(f.history(106,f.associationId,false));QCOMPARE(f.errors.first().at(2).toString(),QString("dataset_path_mismatch"));QCOMPARE(hash(f.qcPath),before);
    }
    void unmarkedCatalogAndWrongThreadDoNotQuerySql()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());const auto before=hash(f.qcPath);
        const QString unmarked=f.directory.filePath("unmarked");QVERIFY(QDir().mkpath(unmarked));
        QueuedSignalRecords bound(f.qc,&QcStore::catalogBound),bindingError(f.qc,&QcStore::error);
        QMetaObject::invokeMethod(f.qc,[&]{f.qc->bindCatalog(unmarked);},Qt::QueuedConnection);
        QVERIFY(waitFor([&]{return bindingError.count();}));QVERIFY(bound.isEmpty());
        QVERIFY(f.history(124,f.associationId,false));QCOMPARE(f.errors.first().at(2).toString(),QString("catalog_not_bound"));
        f.errors.clear();f.qc->loadHistory(125,f.associationId);QVERIFY(waitFor([&]{return f.errors.count()==1;}));
        QCOMPARE(f.errors.first().at(2).toString(),QString("wrong_thread"));QCOMPARE(hash(f.qcPath),before);
    }
    void actualSqlLocksHaveErrorAndRetryWithoutWrites()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());Database local(f.qcPath),catalog(f.catalogPath);const auto before=hash(f.qcPath);
        QVERIFY(local.exec("BEGIN EXCLUSIVE"));QVERIFY(f.history(107,f.associationId,false));QVERIFY(f.errors.first().at(3).toString().contains("native="));
        QVERIFY(local.exec("ROLLBACK"));QVERIFY(f.history(108,f.associationId));
        QVERIFY(catalog.exec("BEGIN EXCLUSIVE"));QVERIFY(f.history(109,f.associationId,false));QVERIFY(f.errors.first().at(3).toString().contains("locked"));
        QVERIFY(catalog.exec("ROLLBACK"));QVERIFY(f.history(110,f.associationId));QCOMPARE(hash(f.qcPath),before);
    }
    void budgetsFailWithNoPartialReply_data()
    {
        QTest::addColumn<int>("variant");QTest::newRow("single-large")<<0;QTest::newRow("total-large")<<1;
        QTest::newRow("row-limit")<<2;QTest::newRow("date-budget")<<3;QTest::newRow("blob-storage")<<4;
        QTest::newRow("embedded-nul-byte-limit")<<5;QTest::newRow("utf16-metadata-limit")<<6;
    }
    void budgetsFailWithNoPartialReply()
    {
        QFETCH(int,variant);Fixture f;QVERIFY(f.initialize());Database local(f.qcPath);
        QVERIFY(local.exec("BEGIN IMMEDIATE"));
        const int count=variant==1?33:variant==2?10001:1;
        for(int index=0;index<count;++index)
        {
            const QString value=variant==0?QString(65537,'x'):variant==1?QString(65536,'x'):
                variant==5?QString(QChar::Null)+QString(262144,'x'):f.encoded(10);
            const uint emoji=0x1f600;
            const QString date=variant==3?QString(65,'x'):variant==6?QString::fromUcs4(&emoji,1).repeated(40):QString("2026/10/25");
            QVERIFY(local.exec("INSERT INTO quality_data(control_name,project_name,control_batch,date,time,result_val) VALUES('QC_DEMO','PROJ_QC_DEMO','LOT_CUR',?,'10:00:00',?)",{date,value}));
        }
        QVERIFY(local.exec("COMMIT"));if(variant==4)QVERIFY(local.exec("UPDATE quality_data SET result_val=CAST(X'00' AS BLOB)"));
        const auto before=hash(f.qcPath);QVERIFY(f.history(111,f.associationId,false));QVERIFY(f.ready.isEmpty());QCOMPARE(hash(f.qcPath),before);
    }
    void emptyAndPreviousOnlyKeepOriginalCardVisibility()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.history(112,f.associationId));QVERIFY(f.reply().status()==QcHistoryStatus::Empty);
        QVERIFY(f.row("LOT_PREV","2026/10/23","09:00:00",f.encoded(10)));QVERIFY(f.history(113,f.associationId));
        QCOMPARE(f.reply().series().first().selection().previous.size(),1);QVERIFY(!f.reply().series().first().selection().legacyCardVisible);
        Database local(f.qcPath);QVERIFY(local.exec("UPDATE quality_control_batch SET cur_batch=0"));
        const auto before=hash(f.qcPath);QVERIFY(f.history(114,f.associationId));QVERIFY(f.reply().status()==QcHistoryStatus::NoCurrentBatch);
        QCOMPARE(f.reply().sourceRowCount(),0);QCOMPARE(hash(f.qcPath),before);
    }
    void noEnabledResultsIsDistinctFromNoHistory()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());QueuedSignalRecords snapshot(f.qc,&QcStore::snapshotReady);
        QMetaObject::invokeMethod(f.qc,[&]{f.qc->load(126);},Qt::QueuedConnection);QVERIFY(waitFor([&]{return snapshot.count();}));
        auto rules=qvariant_cast<QcControls>(snapshot.first().at(1)).first().projects.first().rules;
        for(auto &rule:rules)rule.used=false;
        QVERIFY(f.write([&]{f.qc->saveRules(127,f.associationId,rules);}));
        QVERIFY(f.history(128,f.associationId));QVERIFY(f.reply().status()==QcHistoryStatus::NoEnabledResults);
        QVERIFY(f.reply().series().isEmpty());Database local(f.qcPath);QCOMPARE(local.scalar("SELECT count(*) FROM quality_data").toInt(),3);
    }
    void historyProtectionAndCloseKeepIdentityTruthful()
    {
        Fixture f;QVERIFY(f.initialize());QVERIFY(f.golden());QueuedSignalRecords failure(f.qc,&QcStore::error);
        QMetaObject::invokeMethod(f.qc,[&]{f.qc->setProjects(200,f.controlId,{});},Qt::QueuedConnection);
        QVERIFY(waitFor([&]{return failure.count();}));QVERIFY(f.history(201,f.associationId));QCOMPARE(f.reply().sourceRowCount(),3);
        QVERIFY(f.history(202,999999,false));QCOMPARE(f.errors.first().at(2).toString(),QString("association_not_found"));
        QMetaObject::invokeMethod(f.qc,"close",Qt::BlockingQueuedConnection);
        QVERIFY(f.history(203,f.associationId,false));QCOMPARE(f.errors.first().at(2).toString(),QString("not_initialized"));
    }
};
QTEST_GUILESS_MAIN(QcHistoryStoreTest)
#include "qc_history_store_test.moc"
