#include "reagents/original_reagent_editor.h"
#include "reagents/reagent_store.h"
#include "original_files/original_ini.h"
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtTest>
#include <memory>

class OriginalReagentFlowRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    QString root;
    OriginalReagentEditor editor;
    bool bytes(const QString &name, const QByteArray &content)
    {
        QFile file(QDir(root).filePath(name));
        return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(content) == content.size();
    }
    bool database(const QString &file, const QStringList &commands)
    {
        const QString connection = QUuid::createUuid().toString();
        bool success = true;
        {
            auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(QDir(root).filePath(file));
            success = db.open();
            if (success)
            {
                QSqlQuery query(db);
                for (const auto &command : commands)
                    if (!query.exec(command))
                    { qWarning().noquote() << "FIXTURE_SCHEMA_ERROR" << query.lastError().text(); success = false; break; }
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        return success;
    }
    QVariant value(const QString &file, const QString &sql)
    {
        OriginalBusiness::Database connection;
        QString message;
        if (!connection.open(root, file, false, &message)) return {};
        QSqlQuery query(connection.db);
        if (!query.exec(sql) || !query.next()) return {};
        return query.value(0);
    }
    QStringList columns(const QStringList &names)
    {
        QStringList result;
        for (const auto &name : names) result.append('"' + name + QStringLiteral("\" TEXT"));
        return result;
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("原试剂业务只允许在RK ARM64核对。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
    }
    void init()
    {
        directory.reset(new QTemporaryDir(QDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")).filePath(QStringLiteral("original_detection_XXXXXX"))));
        QVERIFY(directory->isValid()); root = directory->path();
        for (const auto &name : {"db/userSet","db/systemInfo","project","result"}) QVERIFY(QDir(root).mkpath(QString::fromLatin1(name)));
        QVERIFY(bytes(QStringLiteral(".sn100-original-work-v1"), "sn100-original-work-v1\n"));
        QVERIFY(bytes(QStringLiteral("config.ini"), "# test settings\n[userOption]\nisSampleScanClose=false\nisSampleHoleScanClose=false\nisSampleHoleScanOpenHoleNoneWork=true\n"));
        QVERIFY(bytes(QStringLiteral("config_Debug.ini"), "[userOption]\nSendTaskByRowID=true\n"));
        QVERIFY(bytes(QStringLiteral("theme_config.ini"), "[theme]\nSampleHoleCount=8\nEmergencyHoleCount=3\nReportTableType=0\nReagentPanelChannelCount=20\n"));
        QVERIFY(database(QStringLiteral("db/systemInfo/unit.db"), {"CREATE TABLE option_unit(id INTEGER PRIMARY KEY,unit TEXT,type TEXT,ratio TEXT)","INSERT INTO option_unit VALUES(1,'s','Original','1')"}));
        // 原单位文件名按正式Repo约定，保留匿名数据、原结构。
        const QString unitPath = QDir(root).filePath(QStringLiteral("db/systemInfo/option_Unit.db"));
        QVERIFY(QFile::rename(QDir(root).filePath(QStringLiteral("db/systemInfo/unit.db")), unitPath));
        QStringList projectSql{
            "CREATE TABLE project_base(name TEXT,value TEXT)",
            "INSERT INTO project_base VALUES('xuhao','7'),('project_id','9'),('project_en_name','PT'),('project_cn_name','PT'),('is_used','1'),('fixed','0'),('IBS_name','IBS'),('test_analysis_method','凝固法'),('is_related','0')",
            "CREATE TABLE AlgParam(key TEXT,value TEXT)",
            "CREATE TABLE project_sample_rate(xuhao TEXT,sample_rate TEXT,sample_default_option TEXT,sample_option TEXT,IBS_amount_ul TEXT,sample_amount_ul TEXT)",
            "INSERT INTO project_sample_rate VALUES('1','1/2','1','1','10','20')",
            "CREATE TABLE project_reagent(xuhao TEXT,reagent_name TEXT,reagent_amount_ul TEXT)",
            "INSERT INTO project_reagent VALUES('1','R1','50')",
            "CREATE TABLE project_results(result_id TEXT,result_type TEXT,result_name TEXT,decimal_point TEXT,unit_name TEXT,test_low_range TEXT,test_up_range TEXT,normal_low_range TEXT,normal_up_range TEXT,is_used TEXT,display TEXT,main_result TEXT,lis_code TEXT)",
            "INSERT INTO project_results VALUES('1','Original','Time','2','s','0','999','0','999','1','1','1','PT')",
            "CREATE TABLE project_mda(name TEXT,rate_select TEXT)","CREATE TABLE project_quality(control_name TEXT,sample_rate TEXT)"};
        QVERIFY(database(QStringLiteral("project/PT.db"), projectSql));
        const QStringList reagentFields{"reagent_name","reagent_code","reagent_type","banding_needle","ref_use_ul","manufacturer","dect_clean","cleaner_name","valid","warning_ul","out_ul","useage_ul","fixed","check_limit"};
        QVERIFY(database(QStringLiteral("db/userSet/reagent_Information.db"), {
            "CREATE TABLE reagent_list(id INTEGER PRIMARY KEY," + columns(reagentFields).join(',') + ")",
            "INSERT INTO reagent_list(id,reagent_name,reagent_type,valid,out_ul,warning_ul,check_limit) VALUES(1,'R1','r_reagent','1','100','500','0'),(2,'Cleaner','cleaner','1','100','500','0'),(3,'IBS','s_ibs','1','100','500','0')",
            "CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY,project_name TEXT,batch_group TEXT)",
            "CREATE TABLE reagent_barcode(id INTEGER PRIMARY KEY,reagent_name TEXT,reagent_barcode TEXT,reagent_limit_date TEXT,bottle_type TEXT,isi_value TEXT)"}));
        QVERIFY(database(QStringLiteral("db/systemInfo/reagent_Bottle_Type.db"), {
            "CREATE TABLE reagent_bottle_type(id INTEGER PRIMARY KEY,bottle_type_name TEXT,area TEXT,bottle_real_height_mm TEXT,max_amount_ul TEXT,is_adpter TEXT,bottom_diameter TEXT,angle TEXT)",
            "INSERT INTO reagent_bottle_type VALUES(1,'B1','r_area','30','5000','0','10','0')"}));
        const QStringList holeFields{"hole_name","area","reagent_type","reagent_name","reagent_batch","bottle_type","amount_uL","priority","state","datetime","out_date","formula_level","used_ul","out_ul","warning_ul","max_ul"};
        QVERIFY(database(QStringLiteral("db/userSet/reagent_Setting.db"), {
            "CREATE TABLE reagent_setting(id INTEGER PRIMARY KEY," + columns(holeFields).join(',') + ",unknown_vendor_column TEXT DEFAULT 'KEEP')",
            "INSERT INTO reagent_setting(id,hole_name,area,reagent_type,reagent_name,reagent_batch,bottle_type,amount_uL,priority,state,used_ul) VALUES(1,'r_1','r_area','r_reagent','R1','Default','B1','2000','1','r_normal','2000'),(2,'r_2','r_area','cleaner','Cleaner','Default','B1','2000','1','r_normal','2000'),(3,'s_1','s_area','s_ibs','IBS','Default','B1','2000','1','r_normal','2000'),(4,'s_2','s_area','cleaner','Cleaner','Default','B1','2000','1','r_normal','2000')"}));
        QVERIFY(database(QStringLiteral("db/userSet/sample_Combination.db"), {"CREATE TABLE sample_combination(id INTEGER PRIMARY KEY,name TEXT,project_contain TEXT)","INSERT INTO sample_combination VALUES(1,'DoublePT','PT;PT')"}));
        QVERIFY(database(QStringLiteral("result/results.db"), {
            "CREATE TABLE total_count(id INTEGER PRIMARY KEY,total_count INTEGER,project_total_count INTEGER,row_total_count INTEGER,cup_conut INTEGER,cup_limit_count INTEGER,auto_string INTEGER,count INTEGER)",
            "INSERT INTO total_count VALUES(1,41,99,21,5,6,7,8)",
            "CREATE TABLE report_data(id INTEGER PRIMARY KEY,row_id INTEGER UNIQUE,sn INTEGER,type INTEGER,statue INTEGER,uploaded INTEGER,is_auto_id INTEGER,sample_hole TEXT,sample_id TEXT,data_time TEXT,visible INTEGER,state TEXT)",
            "CREATE TABLE report_results(id INTEGER PRIMARY KEY,fid INTEGER,project_name TEXT,task_id INTEGER UNIQUE,uploaded INTEGER,calibration_id TEXT,abnormal_type TEXT,rate TEXT,sample_id TEXT,data_time TEXT,original_value TEXT,calibration_value TEXT,rate_value TEXT,inr_value TEXT,mda_value TEXT,formula_value TEXT)"}));
        QVERIFY(database(QStringLiteral("db/userSet/isi_Value.db"), {
            "CREATE TABLE isi_value(id INTEGER PRIMARY KEY,batch_group TEXT,isi_value TEXT,normal_value TEXT,unknown_vendor_column TEXT DEFAULT 'KEEP')"}));
        QVERIFY(database(QStringLiteral("result/calibration.db"), {
            "CREATE TABLE calibration_batch_group(id INTEGER PRIMARY KEY,project_name TEXT,batch_group TEXT,calibration_id INTEGER)"}));
        QVERIFY(database(QStringLiteral("db/userSet/reagent_Information.db"), {
            "ALTER TABLE reagent_list ADD COLUMN unknown_vendor_column TEXT DEFAULT 'KEEP'"}));
        QString message;
        QVERIFY2(editor.initialize(root,&message),qPrintable(message));
    }
    void cleanup() { directory.reset(); }
    void originalSnapshotUsesNamesAndRetainsFractionalAmounts()
    {
        ReagentSnapshot snapshot; ReagentProjects projects; QString message;
        QVERIFY2(editor.load(&snapshot,&projects,&message),qPrintable(message));
        QVERIFY(snapshot.originalProfile);
        QCOMPARE(projects.size(),1); QCOMPARE(projects.first().id,qint64(7));
        QCOMPARE(snapshot.holes.first().name,QStringLiteral("R_1"));
        QCOMPARE(snapshot.reagents.first().type,QStringLiteral("试剂"));
        QCOMPARE(snapshot.holes.first().originalBatch,QStringLiteral("Default"));
        QCOMPARE(snapshot.holes.first().remainingUl,2000.0);
        auto hole=snapshot.holes.first(); hole.expiry="2031-01-01"; hole.remainingUl=1450.5;
        QVERIFY2(editor.saveHole(hole,&message),qPrintable(message));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toString(),QStringLiteral("1450.5"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
    }
    void reagentEditsPreserveRawAndUnknownColumnsAndBlockReferencedRename()
    {
        ReagentSnapshot snapshot; ReagentProjects projects; QString message;
        QVERIFY(editor.load(&snapshot,&projects,&message));
        auto reagent=snapshot.reagents.first(); reagent.manufacturer="MakerA"; reagent.warningUl=700;
        reagent.originalValues.insert("banding_needle","1"); reagent.originalValues.insert("dect_clean","1");
        reagent.originalValues.insert("cleaner_name","Cleaner");
        QVERIFY2(editor.saveReagent(reagent,&message),qPrintable(message));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT manufacturer FROM reagent_list WHERE id=1").toString(),QStringLiteral("MakerA"));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT banding_needle FROM reagent_list WHERE id=1").toString(),QStringLiteral("1"));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT unknown_vendor_column FROM reagent_list WHERE id=1").toString(),QStringLiteral("KEEP"));
        QVERIFY(editor.load(&snapshot,&projects,&message)); reagent=snapshot.reagents.first(); reagent.name="OtherR";
        QVERIFY(!editor.saveReagent(reagent,&message));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT reagent_name FROM reagent_list WHERE id=1").toString(),QStringLiteral("R1"));
        QVERIFY(!editor.remove("reagent",1,&message));
    }
    void clearsAssignmentsWithoutDeletingPhysicalHoleRows()
    {
        QString message;
        QVERIFY2(editor.clearHole("R_1",&message),qPrintable(message));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT count(*) FROM reagent_setting").toInt(),4);
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT state FROM reagent_setting WHERE id=1").toString(),QStringLiteral("r_none"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        QVERIFY(!editor.clearHole("R_99",&message));
    }
    void savesOriginalBarcodeAndBottleNamesWithoutCatalogForeignKeys()
    {
        QString message;
        ReagentBarcode barcode; barcode.reagentId=1; barcode.bottleId=1; barcode.code="BATCH-A"; barcode.expiry="2031-01-01";
        QVERIFY2(editor.saveBarcode(barcode,&message),qPrintable(message));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT reagent_name FROM reagent_barcode").toString(),QStringLiteral("R1"));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT bottle_type FROM reagent_barcode").toString(),QStringLiteral("B1"));
        ReagentSnapshot snapshot; ReagentProjects projects;
        QVERIFY(editor.load(&snapshot,&projects,&message));
        QVERIFY(!editor.remove("bottle",1,&message));
        ReagentBottle bottle; bottle.name="SampleBottle"; bottle.area="s_area"; bottle.capacityUl=3000; bottle.heightMm=20;
        QVERIFY2(editor.saveBottle(bottle,&message),qPrintable(message));
        QCOMPARE(value("db/systemInfo/reagent_Bottle_Type.db","SELECT max_amount_ul FROM reagent_bottle_type WHERE bottle_type_name='SampleBottle'").toInt(),3000);
    }
    void externalWriteRejectsStaleDraftWithoutOverwritingTheOtherWriter()
    {
        ReagentSnapshot snapshot; ReagentProjects projects; QString message;
        QVERIFY(editor.load(&snapshot,&projects,&message));
        auto reagent=snapshot.reagents.first(); reagent.manufacturer="Stale";
        QVERIFY(database("db/userSet/reagent_Information.db",{"UPDATE reagent_list SET manufacturer='OtherWriter' WHERE id=1"}));
        QVERIFY(!editor.saveReagent(reagent,&message));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT manufacturer FROM reagent_list WHERE id=1").toString(),QStringLiteral("OtherWriter"));
    }
    void batchAndIsiUseOriginalTablesAndCommitTogether()
    {
        ReagentBatch batch; batch.projectId=7; batch.code="A"; batch.batchGroup="A|B"; batch.metadataEdited=true;
        batch.isiValue="1.0300"; batch.normalValue="12.0000"; QString message;
        QVERIFY2(editor.saveBatch(batch,&message),qPrintable(message));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT project_name FROM reagent_batch_group").toString(),QStringLiteral("PT"));
        QCOMPARE(value("db/userSet/isi_Value.db","SELECT isi_value FROM isi_value").toString(),QStringLiteral("1.0300"));
        QCOMPARE(value("db/userSet/isi_Value.db","SELECT unknown_vendor_column FROM isi_value").toString(),QStringLiteral("KEEP"));
    }
    void isiWriteFailureRollsBackTheBatchInsert()
    {
        QVERIFY(database("db/userSet/isi_Value.db",{"CREATE TRIGGER reject_isi BEFORE INSERT ON isi_value BEGIN SELECT RAISE(ABORT,'injected failure'); END"}));
        ReagentSnapshot snapshot; ReagentProjects projects; QString message;
        QVERIFY(editor.load(&snapshot,&projects,&message));
        ReagentBatch batch; batch.projectId=7; batch.code="A"; batch.batchGroup="A"; batch.metadataEdited=true;
        batch.isiValue="1"; batch.normalValue="12";
        QVERIFY(!editor.saveBatch(batch,&message));
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT count(*) FROM reagent_batch_group").toInt(),0);
        QCOMPARE(value("db/userSet/isi_Value.db","SELECT count(*) FROM isi_value").toInt(),0);
    }
    void storeDispatchesOriginalWritesAndRejectsBusyOrUnprivilegedChanges()
    {
        // 与实际工作线程相同的QObject/SQL归属规则；本场景用当前RK测试线程承载存储对象。
        ReagentStore store;
        QSignalSpy loaded(&store,&ReagentStore::loaded),errors(&store,&ReagentStore::error),saved(&store,&ReagentStore::succeeded);
        store.initializeOriginal(root);
        QVERIFY(!loaded.isEmpty()); QVERIFY(errors.isEmpty());
        auto snapshot=qvariant_cast<ReagentSnapshot>(loaded.last()[0]);
        auto reagent=snapshot.reagents.first(); reagent.manufacturer="WorkerMaker";
        store.saveReagent(reagent); QCOMPARE(errors.size(),1); QCOMPARE(saved.size(),0);
        AccountSession session; session.authenticated=true; session.actionReady=true; session.role=5;
        store.setSession(session); store.setInstrumentBusy(true); store.saveReagent(reagent);
        QCOMPARE(errors.size(),2); QCOMPARE(saved.size(),0);
        store.setInstrumentBusy(false); store.saveReagent(reagent);
        QCOMPARE(saved.size(),1);
        QCOMPARE(value("db/userSet/reagent_Information.db","SELECT manufacturer FROM reagent_list WHERE id=1").toString(),QStringLiteral("WorkerMaker"));
        store.close();
    }
};
QTEST_GUILESS_MAIN(OriginalReagentFlowRkTest)
#include "original_reagent_flow_rk_test.moc"
