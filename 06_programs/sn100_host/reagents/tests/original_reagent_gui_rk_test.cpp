#include "reagents/reagents_page.h"
#include "original_files/original_business_database.h"
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QScrollArea>
#include <QDialog>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtTest>
#include <memory>
class OriginalReagentGuiRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    QString root;
    QThread storageThread;
    ReagentStore *store=nullptr;
    ReagentsPage *page=nullptr;
    std::unique_ptr<QScrollArea> host;
    QDialog *visibleDialog()
    {
        for (auto *dialog : page->findChildren<QDialog*>()) if (dialog->isVisible()) return dialog;
        return nullptr;
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
        QFAIL("原试剂页面必须在RK实际显示环境检查。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
        QApplication::setQuitOnLastWindowClosed(false);
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
        store=new ReagentStore;
        store->moveToThread(&storageThread);
        connect(&storageThread,&QThread::finished,store,&QObject::deleteLater);
        connect(&storageThread,&QThread::started,store,[this](){ store->initializeOriginal(root); });
        page=new ReagentsPage(store);
        host.reset(new QScrollArea);
        host->setWidgetResizable(false); page->setFixedSize(1751,968); host->setWidget(page);
        AccountSession session; session.authenticated=true; session.actionReady=true; session.role=5;
        page->setSession(session);
        storageThread.start(); host->showFullScreen();
        QTRY_VERIFY_WITH_TIMEOUT(page->isReady(),10000);
        QVERIFY(page->snapshot().originalProfile);
    }
    void cleanup()
    {
        if (host) { delete host->takeWidget(); page=nullptr; host->close(); host.reset(); }
        if (store && storageThread.isRunning())
        {
            QSignalSpy closed(store,&ReagentStore::closed);
            QMetaObject::invokeMethod(store,"close",Qt::QueuedConnection);
            const bool didClose=!closed.isEmpty() || closed.wait(5000);
            storageThread.quit(); const bool stopped=storageThread.wait(5000);
            store=nullptr;
            QVERIFY(didClose && stopped);
        }
        directory.reset();
    }
    void originalFormsSaveReloadPreserveRowsAndBlockInstrumentBusy()
    {
        QSignalSpy finished(page,&ReagentsPage::operationFinished);
        auto *open=page->findChild<QPushButton*>("btnReagentSet");
        QVERIFY(open && open->isEnabled());
        QTest::mouseClick(open,Qt::LeftButton);
        QTRY_VERIFY(visibleDialog());
        auto *dialog=visibleDialog();
        auto *maker=dialog->findChild<QLineEdit*>("ldtFactor");
        auto *save=dialog->findChild<QPushButton*>("btnSave");
        QVERIFY(maker && maker->isVisible() && save && save->isEnabled());
        maker->setFocus(); maker->selectAll(); QTest::keyClicks(maker,"GUI-Maker-A");
        QTest::mouseClick(save,Qt::LeftButton);
        QTRY_COMPARE(finished.count(),1); QVERIFY(finished.last()[1].toBool());
        QTRY_COMPARE(value("db/userSet/reagent_Information.db","SELECT manufacturer FROM reagent_list WHERE id=1").toString(),QStringLiteral("GUI-Maker-A"));
        QTRY_VERIFY(!page->isBusy());
        // 同一弹窗再次保存必须使用重新读到的指纹，不能把自己的第一次提交误判成外部冲突。
        maker->setFocus(); maker->selectAll(); QTest::keyClicks(maker,"GUI-Maker-B");
        QTest::mouseClick(save,Qt::LeftButton);
        QTRY_COMPARE(finished.count(),2); QVERIFY(finished.last()[1].toBool());
        QTRY_COMPARE(value("db/userSet/reagent_Information.db","SELECT manufacturer FROM reagent_list WHERE id=1").toString(),QStringLiteral("GUI-Maker-B"));
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnClose"),Qt::LeftButton);
        QTRY_VERIFY(!visibleDialog());
        auto *holeButton=page->findChild<QPushButton*>("btnReagentInfoSet");
        QVERIFY(holeButton && holeButton->isEnabled()); QTest::mouseClick(holeButton,Qt::LeftButton);
        QTRY_VERIFY(visibleDialog()); dialog=visibleDialog();
        auto *batch=dialog->findChild<QComboBox*>("cbxBatch");
        auto *expiry=dialog->findChild<QLineEdit*>("ldtOutDate");
        QVERIFY(batch && batch->isEditable() && expiry);
        batch->setCurrentText("GUI-BATCH"); expiry->setText("2031-01-01");
        save=dialog->findChild<QPushButton*>("btnSave"); QVERIFY(save && save->isEnabled());
        QTest::mouseClick(save,Qt::LeftButton);
        QTRY_COMPARE(finished.count(),3); QVERIFY(finished.last()[1].toBool());
        QTRY_COMPARE(value("db/userSet/reagent_Setting.db","SELECT reagent_batch FROM reagent_setting WHERE id=1").toString(),QStringLiteral("GUI-BATCH"));
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toDouble(),5000.0);
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        QTRY_VERIFY(!visibleDialog());
        QTest::mouseClick(holeButton,Qt::LeftButton); QTRY_VERIFY(visibleDialog()); dialog=visibleDialog();
        auto *name=dialog->findChild<QComboBox*>("cbxName"); QVERIFY(name); name->setCurrentIndex(0);
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnSave"),Qt::LeftButton);
        QTRY_COMPARE(finished.count(),4); QVERIFY(finished.last()[1].toBool());
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT count(*) FROM reagent_setting").toInt(),4);
        QCOMPARE(value("db/userSet/reagent_Setting.db","SELECT reagent_name FROM reagent_setting WHERE id=1").toString(),QStringLiteral("none"));
        QTRY_VERIFY(!visibleDialog());
        page->setInstrumentBusy(true); QVERIFY(!open->isEnabled());
        page->openReagentEditor(); QVERIFY(!visibleDialog());
        page->setInstrumentBusy(false); QVERIFY(open->isEnabled());
        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog());
        maker=visibleDialog()->findChild<QLineEdit*>("ldtFactor"); QVERIFY(maker);
        QCOMPARE(maker->text(),QStringLiteral("GUI-Maker-B"));
        QTest::mouseClick(visibleDialog()->findChild<QPushButton*>("btnClose"),Qt::LeftButton);
        QTRY_VERIFY(!visibleDialog());
    }
};
QTEST_MAIN(OriginalReagentGuiRkTest)
#include "original_reagent_gui_rk_test.moc"
