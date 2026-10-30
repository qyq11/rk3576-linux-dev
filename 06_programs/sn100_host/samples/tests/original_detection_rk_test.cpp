#include "original_detection_session.h"
#include "original_files/original_business_database.h"
#include "original_files/original_ini.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>
#include <memory>

class OriginalDetectionRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    QString root;
    OriginalSampleRepository samples;
    OriginalDetectionSession detection;

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
    void oneSample(const QString &name = QStringLiteral("S01"))
    {
        SampleRack rack;
        rack.name = name;
        for (int i = 1; i <= 8; ++i)
        {
            SampleHole hole;
            hole.number = i;
            if (i == 1)
            {
                hole.sampleId = QStringLiteral("17");
                SampleTask task;
                task.projectId = 7; task.rate = QStringLiteral("1/2"); task.repeats = 2;
                hole.tasks.append(task);
            }
            rack.holes.append(hole);
        }
        qint64 saved = 0;
        QString message;
        QVERIFY2(samples.saveRack(rack, &saved, &message), qPrintable(message));
        QVERIFY(saved > 0);
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("本驱动只允许在RK执行，SQL只用于创建匿名夹具和故障注入。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
    }
    void init()
    {
        detection = OriginalDetectionSession();
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
        QString message;
        QVERIFY2(samples.initialize(root, &message), qPrintable(message));
        QVERIFY2(detection.initialize(&samples, &message), qPrintable(message));
    }
    void cleanup() { directory.reset(); }

    void readsOriginalCombinationAndKeepsOrderAndRepeatedProjects()
    {
        SampleSnapshot snapshot;
        QString message;
        QVERIFY(samples.load(&snapshot, &message));
        QCOMPARE(snapshot.combinations.size(),1);
        QCOMPARE(snapshot.combinations.first().orderedProjectIds, QList<qint64>({7,7}));
        QCOMPARE(samples.ordinaryHoles(),8);
        QCOMPARE(samples.emergencyHoles(),3);
        auto combination = snapshot.combinations.first();
        combination.name = QStringLiteral("SavedPT");
        qint64 saved = 0;
        QVERIFY(samples.saveCombination(combination, &saved, &message));
        QCOMPARE(value("db/userSet/sample_Combination.db", "SELECT name FROM sample_combination WHERE id=1").toString(),QStringLiteral("SavedPT"));
        QCOMPARE(value("db/userSet/sample_Combination.db", "SELECT project_contain FROM sample_combination WHERE id=1").toString(),QStringLiteral("PT;PT"));
    }
    void reservesStableNumbersAndOriginalReagentAmountBeforeResponding()
    {
        oneSample();
        QString message;
        QVERIFY2(detection.prepare(true, &message), qPrintable(message));
        QByteArray response;
        QVERIFY2(detection.answerRackRequest("Tt[S01][1][1][BAR99][0]", &response, &message),qPrintable(message));
        QCOMPARE(response, QByteArray("Tt[S01][2][SAM][41][1][cup][BAR99][PT][0][0][1/2][Default][s_1][1][0][][SAM][42][1][cup][BAR99][PT][0][0][1/2][Default][s_1][1][0][]"));
        QCOMPARE(value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),43);
        QCOMPARE(value("result/results.db","SELECT row_total_count FROM total_count WHERE id=1").toInt(),23);
        QCOMPARE(value("result/results.db","SELECT project_total_count FROM total_count WHERE id=1").toInt(),99);
        QCOMPARE(value("result/results.db","SELECT sn FROM report_data WHERE row_id=21").toInt(),17);
        QCOMPARE(value("result/results.db","SELECT sample_id FROM report_data WHERE row_id=21").toString(),QStringLiteral("BAR99"));
        QCOMPARE(value("result/results.db","SELECT sample_id FROM report_results WHERE task_id=41").toString(),QStringLiteral("17"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toDouble(),1900.0);
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        QVERIFY(detection.answerRackRequest("Tt[S01][1][1][BAR99][2][41][42]", &response, &message));
        QCOMPARE(response,QByteArray("Tt[S01][0]"));
        QCOMPARE(value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),43);
    }
    void singleReagentUpdateKeepsTheOriginalFifteenFieldsAndNumericPrecision()
    {
        QVERIFY(database("db/userSet/reagent_Setting.db",{
            "UPDATE reagent_setting SET amount_uL='123.456789',used_ul='100',max_ul='5000',priority='7',warning_ul='500',out_ul='100',datetime='2026/10/27 12:34:56',out_date='2031/01/01' WHERE id=1"}));
        OriginalReagentRepository repository; OriginalReagentState state; QString message; QByteArray payload;
        QVERIFY(repository.initialize(root,&message)); QVERIFY(repository.load(&state,&message));
        QVERIFY2(repository.singleReagentUpdatePayload(state,"R_1",true,&payload,&message),qPrintable(message));
        QCOMPARE(payload,QByteArray("Us[r_1][r_reagent][R1][Default][B1][123.457][7][1][2026/10/27 12:34:56][2031/01/01][500][100][5000][5000][1]"));
        QVERIFY(repository.singleReagentUpdatePayload(state,"r_1",false,&payload,&message));
        QVERIFY(payload.endsWith("[5000][5000][0]"));
        QVERIFY(!repository.singleReagentUpdatePayload(state,"r_99",true,&payload,&message));
        state.holes[0].insert("state","unrecognized");
        QVERIFY(!repository.singleReagentUpdatePayload(state,"r_1",true,&payload,&message));
    }
    void missingContainerDoesNotReserveAnyNumber()
    {
        oneSample(); QString message; QByteArray response;
        QVERIFY(detection.prepare(true, &message));
        QVERIFY(detection.answerRackRequest("Tt[S01][1][1][empty][0]",&response,&message));
        QCOMPARE(response,QByteArray("Tt[S01][0]"));
        QCOMPARE(value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),41);
    }
    void disabledRackScannerSkipsAnEmptyRackAndUsesFirstReadyRack()
    {
        SampleRack empty;
        empty.name = QStringLiteral("S01");
        for (int i = 1; i <= 8; ++i) { SampleHole hole; hole.number=i; empty.holes.append(hole); }
        QString message; qint64 saved = 0;
        QVERIFY(samples.saveRack(empty,&saved,&message));
        oneSample(QStringLiteral("S02"));
        QFile config(QDir(root).filePath(QStringLiteral("config.ini")));
        QVERIFY(config.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(config.write("[userOption]\nisSampleScanClose=true\nisSampleHoleScanClose=false\nisSampleHoleScanOpenHoleNoneWork=true\n") > 0);
        config.close();
        detection = OriginalDetectionSession();
        QVERIFY(detection.initialize(&samples,&message));
        QVERIFY(detection.prepare(true,&message));
        QByteArray response;
        QVERIFY2(detection.answerRackRequest("Tt[][1][1][BAR99][0]",&response,&message),qPrintable(message));
        QVERIFY(response.startsWith("Tt[S02][2]"));
        QCOMPARE(value("result/results.db","SELECT count(*) FROM report_results").toInt(),2);
    }
    void enabledRackScannerDoesNotAssignAnUnknownRack()
    {
        oneSample(); QString message; QByteArray response;
        QVERIFY(detection.prepare(true,&message));
        QVERIFY(detection.answerRackRequest("Tt[][1][1][BAR99][0]",&response,&message));
        QCOMPARE(response,QByteArray("Tt[][0]"));
        QCOMPARE(value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),41);
        QCOMPARE(value("result/results.db","SELECT count(*) FROM report_results").toInt(),0);
    }
    void reagentReplyWritesOriginalFieldsAndRejectsMalformedReply()
    {
        OriginalReagentRepository repository;
        QString message;
        QVERIFY(repository.initialize(root,&message));
        const QByteArray single("Ur[1][r_1][r_reagent][R1][BATCH_A][B1][12.5][100][1500][2][1][2026/10/27 15:00:00][2027/10/27][500][5000][1450.5][1]");
        QVERIFY2(repository.applyDeviceReply(single,&message),qPrintable(message));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT reagent_batch FROM reagent_setting WHERE id=1").toString(),QStringLiteral("BATCH_A"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toString(),QStringLiteral("1450.5"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT reagent_name FROM reagent_setting WHERE id=2").toString(),QStringLiteral("Cleaner"));
        QVERIFY(!repository.applyDeviceReply("Ur[2][r_1][r_reagent]",&message));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toString(),QStringLiteral("1450.5"));
        QVERIFY(repository.applyDeviceReply("Ur[0][0]",&message));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT count(*) FROM reagent_setting WHERE state='r_none'").toInt(),4);
    }
    void progressAndRawResultUseTheReservedTaskAndDoNotInventComputedValues()
    {
        oneSample(); QString message; QByteArray response;
        QVERIFY2(detection.prepare(true,&message),qPrintable(message));
        QVERIFY2(detection.answerRackRequest("Tt[S01][1][1][BAR99][0]",&response,&message),qPrintable(message));
        QVERIFY(detection.receiveDevicePayload("TD[S01][1][41]",&message));
        const QByteArray raw("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][1][1][37.1][r_1][1/2][100][1][s_1][][][][405nm-1,2,3,4]");
        QVERIFY2(detection.receiveDevicePayload(raw,&message),qPrintable(message));
        const QString path = QDir(root).filePath(QStringLiteral("result/") + QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral("/41"));
        OriginalIni::Settings saved;
        QVERIFY(OriginalIni::read(path,&saved,&message));
        QCOMPARE(saved.values.value(QStringLiteral("base/task_id")).toLongLong(),qint64(41));
        QCOMPARE(saved.values.value(QStringLiteral("base/sample_id")).toString(),QStringLiteral("BAR99"));
        QCOMPARE(saved.values.value(QStringLiteral("rk/preset_sample_id")).toString(),QStringLiteral("17"));
        QCOMPARE(saved.values.value(QStringLiteral("spectrum/405nm")).toString(),QStringLiteral("1,2,3,4"));
        QCOMPARE(saved.values.value(QStringLiteral("rk/calculation_state")).toString(),QStringLiteral("waiting"));
        QCOMPARE(saved.values.value(QStringLiteral("rk_unit_ratios/s")).toString(),QStringLiteral("1"));
        QVERIFY(!saved.values.contains(QStringLiteral("result/original")));
        QCOMPARE(value("result/results.db","SELECT original_value FROM report_results WHERE task_id=41").toString(),QString());
        QCOMPARE(value("result/results.db","SELECT statue FROM report_data WHERE row_id=21").toInt(),0);
        QVERIFY(detection.receiveDevicePayload(raw,&message));
        QByteArray conflict = raw;
        conflict.replace("1,2,3,4","9,9,9,9");
        QVERIFY(!detection.receiveDevicePayload(conflict,&message));
        QVERIFY(!detection.receiveDevicePayload("TD[S02][1][41]",&message));
        QVERIFY(detection.receiveDevicePayload("TD[S01][1][41]",&message));
        SampleSnapshot snapshot;
        QVERIFY(samples.load(&snapshot,&message));
        for (const auto &rack : snapshot.racks) if (rack.name == QStringLiteral("S01"))
            QCOMPARE(sampleTaskStates(rack.holes[0].tasks[0])[0],SampleTaskState::Finished);
    }
    void failedResultInsertRollsBackCountersAndReagentAmount()
    {
        oneSample(); QString message; QByteArray response;
        QVERIFY(detection.prepare(true, &message));
        OriginalBusiness::Database failure;
        QVERIFY(failure.open(root,"result/results.db",true,&message));
        QVERIFY(failure.execute("CREATE TRIGGER test_reject_result BEFORE INSERT ON report_results BEGIN SELECT RAISE(ABORT,'injected write failure'); END",{},&message));
        QVERIFY(!detection.answerRackRequest("Tt[S01][1][1][BAR99][0]",&response,&message));
        QVERIFY(response.isEmpty());
        QCOMPARE(value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),41);
        QCOMPARE(value("result/results.db","SELECT count(*) FROM report_data").toInt(),0);
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toDouble(),2000.0);
    }
};
QTEST_GUILESS_MAIN(OriginalDetectionRkTest)
#include "original_detection_rk_test.moc"
