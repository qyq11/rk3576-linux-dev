#include "samples/original_project_device_repository.h"
#include "original_files/original_business_database.h"
#include <QDir>
#include <QFile>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <memory>
class OriginalProjectDeviceRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    QString root;
    OriginalProjectDeviceRepository device;
    QByteArray reply() const
    {
        return "UP[t][PT][9][1][5][120][0][110][none][7][10][405][3][1][20][0][0][IBS][4][Default][30][0][1][0][C1][1][PT]"
               "[1][1][1/2][3][10][20][2][4][6][8][9][10][11][12][1][0][1][0][2][3][Cleaner][AfterS]"
               "[1][1][4][R1][8][1][2][50][3][4][1][5][6][Cleaner][AfterR]"
               "[1][1][Original][Time][2][s][0][999][0][999][1][1][1][PT][2]";
    }
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
        QFAIL("原项目设备同步读写仅在RK ARM64检查。");
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
        QStringList updates;
        for (const auto &column : QStringList{"sample_add_length","blender_type","blender_time_sec","suck_release_count","mix_amount_ul","second_IBS_amount_ul",
             "action_amount_ul","before_clean_amount_ul","after_clean_amount_ul","calibration_option","sample_add_mode","before_clean_frequency",
             "after_clean_frequency","before_clean_name","after_clean_name"})
            updates.append("ALTER TABLE project_sample_rate ADD COLUMN "+column+" TEXT DEFAULT ''");
        for (const auto &column : QStringList{"reagent_add_length","reagent_warmup_length","before_clean_frequency","after_clean_frequency","blender_type",
             "blender_time_sec","needle_select","before_clean_amount_ul","after_clean_amount_ul","before_clean_name","after_clean_name"})
            updates.append("ALTER TABLE project_reagent ADD COLUMN "+column+" TEXT DEFAULT ''");
        updates << "ALTER TABLE project_sample_rate ADD COLUMN unknown_vendor_column TEXT DEFAULT 'KEEP'"
                << "INSERT INTO project_base VALUES('min_test_time','10'),('max_test_time','120'),('extra_vendor_key','KEEP')"
                << "INSERT INTO AlgParam VALUES('extra_vendor_algorithm','KEEP')";
        QVERIFY(database("project/PT.db",updates));
        QString message;
        QVERIFY2(device.initialize(root,&message),qPrintable(message));
    }
    void cleanup() { directory.reset(); }
    void readsFullOriginalRowsAndProjectsOnlyTheActualUpFields()
    {
        QVERIFY(database("project/PT.db",{"UPDATE project_sample_rate SET after_clean_name='AfterS',before_clean_name='Cleaner',sample_add_length='9'"}));
        QList<OriginalDeviceProject> projects; QString message;
        QVERIFY2(device.load(&projects,&message),qPrintable(message));
        QCOMPARE(projects.size(),1);
        QCOMPARE(projects[0].wire.samples[0].value("after_clean_name"),QStringLiteral("AfterS"));
        const auto sent=OriginalProjectWire::parametersInUpdate(projects[0].wire);
        QVERIFY(!sent.samples[0].contains("after_clean_name"));
        QVERIFY(!sent.samples[0].contains("sample_add_length"));
        QCOMPARE(sent.samples[0].value("before_clean_name"),QStringLiteral("Cleaner"));
        QByteArray payload; QVERIFY(OriginalProjectWire::updatePayload(projects[0].wire,&payload,&message));
        QVERIFY(!payload.contains("AfterS"));
    }
    void receivedProjectUpdatesOriginalTablesAndPreservesUnknownFields()
    {
        QVERIFY(database("project/PT.db",{
            "INSERT INTO project_sample_rate(xuhao,sample_rate,sample_default_option,sample_option,IBS_amount_ul,sample_amount_ul) VALUES('2','1/4','0','0','10','20')",
            "INSERT INTO project_results VALUES('5','MDA','Other','2','s','0','999','0','999','0','0','0','')"}));
        QList<OriginalDeviceProject> projects; QString message;
        QVERIFY(device.load(&projects,&message));
        QVERIFY2(device.applyDeviceProject(reply(),"PT",projects[0].record.originalFingerprint,&message),qPrintable(message));
        QCOMPARE(value("project/PT.db","SELECT value FROM project_base WHERE name='min_test_time'").toString(),QStringLiteral("5"));
        QCOMPARE(value("project/PT.db","SELECT after_clean_name FROM project_sample_rate WHERE xuhao='1'").toString(),QStringLiteral("AfterS"));
        QCOMPARE(value("project/PT.db","SELECT needle_select FROM project_reagent WHERE xuhao='1'").toString(),QStringLiteral("1"));
        QCOMPARE(value("project/PT.db","SELECT count(*) FROM project_sample_rate").toInt(),1);
        QCOMPARE(value("project/PT.db","SELECT unknown_vendor_column FROM project_sample_rate WHERE xuhao='1'").toString(),QStringLiteral("KEEP"));
        QCOMPARE(value("project/PT.db","SELECT value FROM project_base WHERE name='extra_vendor_key'").toString(),QStringLiteral("KEEP"));
        QCOMPARE(value("project/PT.db","SELECT value FROM AlgParam WHERE key='extra_vendor_algorithm'").toString(),QStringLiteral("KEEP"));
        QCOMPARE(value("project/PT.db","SELECT count(*) FROM project_results WHERE result_id='5'").toInt(),1);
    }
    void aLaterDraftOrWrongProjectReplyCannotOverwriteCurrentData()
    {
        QList<OriginalDeviceProject> projects; QString message;
        QVERIFY(device.load(&projects,&message));
        QVERIFY(database("project/PT.db",{"UPDATE project_base SET value='88' WHERE name='min_test_time'"}));
        QVERIFY(!device.applyDeviceProject(reply(),"PT",projects[0].record.originalFingerprint,&message));
        QCOMPARE(value("project/PT.db","SELECT value FROM project_base WHERE name='min_test_time'").toString(),QStringLiteral("88"));
        QVERIFY(!device.applyDeviceProject(reply(),"Other",projects[0].record.originalFingerprint,&message));
    }
    void aReagentWriteFailureRollsBackEarlierBaseAndSampleChanges()
    {
        QVERIFY(database("project/PT.db",{"CREATE TRIGGER reject_reagent BEFORE UPDATE ON project_reagent BEGIN SELECT RAISE(ABORT,'injected failure'); END"}));
        QList<OriginalDeviceProject> projects; QString message;
        QVERIFY(device.load(&projects,&message));
        auto changed=reply(); changed.replace("[10][20][2]","[10][25][2]");
        QVERIFY(!device.applyDeviceProject(changed,"PT",projects[0].record.originalFingerprint,&message));
        QCOMPARE(value("project/PT.db","SELECT value FROM project_base WHERE name='min_test_time'").toString(),QStringLiteral("10"));
        QCOMPARE(value("project/PT.db","SELECT sample_amount_ul FROM project_sample_rate WHERE xuhao='1'").toString(),QStringLiteral("20"));
    }
    void syncChecksTheSentParametersRatherThanDeviceMetadataOrUnknownColumns()
    {
        OriginalDeviceProject sent; OriginalProjectWire::Project received; QString message;
        QVERIFY(OriginalProjectWire::parseDeviceProject(reply(),&sent.wire,&message));
        received=sent.wire;
        sent.wire.samples[0].insert("extra_vendor_key","KEEP");
        received.base.insert("change_time","device-time"); received.base.insert("collect_rate_ms","22");
        QVERIFY2(OriginalProjectDeviceRepository::matchesSentParameters(sent,received,&message),qPrintable(message));
        received.samples[0].insert("sample_amount_ul","21");
        QVERIFY(!OriginalProjectDeviceRepository::matchesSentParameters(sent,received,&message));
    }
};
QTEST_GUILESS_MAIN(OriginalProjectDeviceRkTest)
#include "original_project_device_rk_test.moc"
