// 在RK创建匿名原表，核质控/校准页面与同一原工作根；不产生检测或拟合结果。
#include <QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <memory>
#include <functional>
#include "samples/tests/original_task_flow_fixture.h"
#include "data/account_store.h"
#include "data/hospital_store.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "quality_control/quality_control_page.h"
#include "calibration/calibration_page.h"
#include "shell/frmmain.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"
#include <QLockFile>

class OriginalQualityPagesRkTest : public QObject
{
    Q_OBJECT
    OriginalTaskFlowFixture fixture;
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<QScrollArea> host;
    QThread worker;
    HospitalStore *hospital = nullptr;
    AccountStore *accounts = nullptr;
    ProjectsStore *projects = nullptr;
    ReagentStore *reagents = nullptr;
    QcStore *quality = nullptr;
    CalibrationStore *calibration = nullptr;
    frmMain *shell = nullptr;
    QualityControlPage *qualityPage = nullptr;
    CalibrationPage *calibrationPage = nullptr;
    struct InitFailureCleanup
    {
        std::function<void()> close;
        ~InitFailureCleanup() { if (QTest::currentTestFailed()) close(); }
    };
    QByteArray digest(const QString &relative)
    {
        QFile file(fixture.root + '/' + relative);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    }
    void createQualityData()
    {
        QVERIFY(fixture.database("db/systemInfo/user.db", {
            "CREATE TABLE user_role(id INTEGER PRIMARY KEY,role TEXT)", "INSERT INTO user_role VALUES(4,'匿名工程师')",
            "CREATE TABLE user_powers(id INTEGER PRIMARY KEY,describe TEXT,can_role_id TEXT)", "INSERT INTO user_powers VALUES(1,'原曲线权限','4')"}));
        QVERIFY(fixture.database("db/userSet/user_Information.db", {
            "CREATE TABLE user_information(id INTEGER PRIMARY KEY,name VARCHAR,role INTEGER,pwd VARCHAR,isdefault INTEGER)"}));
        QVERIFY(fixture.database("db/userSet/hospital_Information.db", {
            "CREATE TABLE hospital_information(id INTEGER PRIMARY KEY,column_one VARCHAR,column_two VARCHAR,column_three VARCHAR,type VARCHAR)"}));
        { QFile file(fixture.root + "/config_Debug.ini"); QVERIFY(file.open(QIODevice::Append));
          QVERIFY(file.write("\n[login]\nopen=false\ndefaultPermission=4\n") > 0); }
        QVERIFY(fixture.database("db/userSet/reagent_Information.db", {
            "INSERT INTO reagent_batch_group VALUES(1,'PT','Default')"}));
        QVERIFY(fixture.database("result/quality.db", {
            "CREATE TABLE quality_control_list(id INTEGER PRIMARY KEY,item_name TEXT,is_used INTEGER,factor_name TEXT,vendor_extra TEXT)",
            "INSERT INTO quality_control_list VALUES(10,'QC-A',1,'ANON_FACTORY','KEEP')",
            "CREATE TABLE quality_item_proj(id INTEGER PRIMARY KEY,item_id INTEGER,proj_name TEXT)", "INSERT INTO quality_item_proj VALUES(30,10,'PT')",
            "CREATE TABLE quality_control_batch(id INTEGER PRIMARY KEY,fid INTEGER,batch_name TEXT,expiration_date TEXT,cur_batch INTEGER,prv_batch INTEGER)",
            "INSERT INTO quality_control_batch VALUES(20,10,'LOT-Q','2031-01-01',1,0)",
            "CREATE TABLE quality_rules(id INTEGER PRIMARY KEY,item_proj_id INTEGER,type INTEGER,name TEXT,used INTEGER,upper_limit TEXT,lower_limit TEXT,upper_bound TEXT,lower_bound TEXT,sd_value TEXT,target TEXT,r1_2s INTEGER,r1_3s INTEGER,r2_2s INTEGER,r4_1s INTEGER,r5_5s INTEGER,rR4s INTEGER,r7T INTEGER,r10X INTEGER,limit_control INTEGER)",
            "INSERT INTO quality_rules VALUES(40,30,1,'Time',1,'20','0','20','0','1','10',0,0,0,0,0,0,0,0,0)"}));
        QVERIFY(fixture.database("result/quality_Data.db", {
            "CREATE TABLE quality_data(id INTEGER PRIMARY KEY,control_name TEXT,project_name TEXT,control_batch TEXT,result_val TEXT,date TEXT,time TEXT)",
            "INSERT INTO quality_data VALUES(1,'QC-A','PT','LOT-Q','[PT||=Original-10.25:1-41]','2026/10/27','12:00:00')"}));
        QVERIFY(fixture.database("result/calibration.db", {
            "CREATE TABLE calibraiton_total_count(id INTEGER PRIMARY KEY,total_count INTEGER)", "INSERT INTO calibraiton_total_count VALUES(1,101)",
            "CREATE TABLE calibration_base(id INTEGER PRIMARY KEY,calibration_id INTEGER,project_name TEXT,calibration_datetime TEXT,boold_batch TEXT,point_size INTEGER,repeat_number INTEGER,calculate_id TEXT,is_checked INTEGER,is_edited INTEGER,result_unit TEXT,con_unit TEXT,x_change TEXT,y_change TEXT,visible INTEGER,vendor_extra TEXT)",
            "INSERT INTO calibration_base VALUES(1,100,'PT','20261027120000','CAL-A',1,2,'直线法',0,0,'s','s','x','y',1,'KEEP')",
            "CREATE TABLE calibration_data(id INTEGER PRIMARY KEY,calibration_id INTEGER,point_index INTEGER,sample_rate TEXT,concentration TEXT,result_flag TEXT,result_value TEXT,only_count TEXT,vendor_extra TEXT)",
            "INSERT INTO calibration_data VALUES(1,100,1,'1/1','100.000000123456','0|4','12.3456789012345|****','91|92','KEEP')"}));
    }
    void click(QObject *parent, const char *name)
    {
        auto *button = parent->findChild<QPushButton *>(QString::fromLatin1(name));
        QVERIFY(button);
        QTRY_VERIFY_WITH_TIMEOUT(button->isVisible() && button->isEnabled(), 15000);
        if (auto *mask = parent->findChild<CusDialogMask *>("mask")) QTRY_VERIFY(mask->isReadyForInput());
        QTest::mouseClick(button, Qt::LeftButton);
    }
    void withAnswer(QMessageBox::StandardButton answer, const std::function<void()> &action)
    {
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [answer]()
        {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                if (auto *button = box->button(answer)) QTest::mouseClick(button, Qt::LeftButton);
        });
        timer.start(30); action(); timer.stop();
    }
    QDialog *manager()
    {
        auto *dialog = qualityPage->findChild<QDialog *>("QcControlManager");
        return dialog && dialog->isVisible() ? dialog : nullptr;
    }
    void closeWorkers()
    {
        if (!worker.isRunning()) return;
        if (shell) { delete host->takeWidget(); shell = nullptr; }
        for (QObject *object : {static_cast<QObject *>(calibration), static_cast<QObject *>(quality),
             static_cast<QObject *>(reagents), static_cast<QObject *>(projects), static_cast<QObject *>(accounts), static_cast<QObject *>(hospital)})
            QMetaObject::invokeMethod(object, "close", Qt::BlockingQueuedConnection);
        worker.quit(); QVERIFY(worker.wait(5000));
        calibration = nullptr; quality = nullptr; reagents = nullptr; projects = nullptr; accounts = nullptr; hospital = nullptr;
        qualityPage = nullptr; calibrationPage = nullptr;
    }
    QByteArray runMain()
    {
        QProcess process;
        process.start(qEnvironmentVariable("SN100_FORMAL_MAIN_BINARY"), {
            "--offline", "--accounts", "--catalog", "--quality-control", "--calibration", "--password-login",
            "--original-db-root", fixture.root, "--data-dir", fixture.root, "--viewport", "1600x900"});
        if (!process.waitForStarted(5000)) return {};
        QByteArray output;
        QElapsedTimer timer; timer.start();
        while (!output.contains("SHELL_READY") && process.state() != QProcess::NotRunning && timer.elapsed() < 20000)
        { process.waitForReadyRead(100); output += process.readAllStandardError(); output += process.readAllStandardOutput(); }
        if (process.state() != QProcess::NotRunning) process.terminate();
        if (!process.waitForFinished(10000) && process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(3000); }
        output += process.readAllStandardError(); output += process.readAllStandardOutput();
        qInfo().noquote() << "ACTUAL_ORIGINAL_QUALITY_MAIN" << output;
        if (process.exitStatus() != QProcess::NormalExit) return {};
        output += "\nACTUAL_EXIT=" + QByteArray::number(process.exitCode());
        return output;
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("只在RK执行；电脑只编辑、检查和ARM64交叉编译。");
#endif
        HospitalStore::registerTypes(); AccountStore::registerTypes(); ProjectsStore::registerTypes();
        ReagentStore::registerTypes(); QcStore::registerTypes(); CalibrationStore::registerTypes();
        host.reset(new QScrollArea); host->resize(1600, 900); host->setWidgetResizable(false);
    }
    void init()
    {
        InitFailureCleanup failed{[this]() { closeWorkers(); }};
        directory.reset(new QTemporaryDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT") + "/quality_pages_XXXXXX"));
        QVERIFY(directory->isValid()); fixture.create(directory->path()); QVERIFY(!QTest::currentTestFailed());
        createQualityData(); QVERIFY(!QTest::currentTestFailed());
        hospital = new HospitalStore; accounts = new AccountStore; projects = new ProjectsStore;
        reagents = new ReagentStore; quality = new QcStore; calibration = new CalibrationStore;
        for (QObject *object : {static_cast<QObject *>(hospital), static_cast<QObject *>(accounts), static_cast<QObject *>(projects),
             static_cast<QObject *>(reagents), static_cast<QObject *>(quality), static_cast<QObject *>(calibration)})
        { object->moveToThread(&worker); connect(&worker, &QThread::finished, object, &QObject::deleteLater); }
        shell = new frmMain(hospital, nullptr, accounts); shell->setProperty("originalDataRoot", fixture.root);
        shell->attachCatalogPages(new ProjectPage(projects), new ReagentsPage(reagents));
        qualityPage = new QualityControlPage(quality); shell->attachQualityControlPage(qualityPage);
        calibrationPage = new CalibrationPage(calibration); shell->attachCalibrationPage(calibrationPage);
        connect(shell, &frmMain::sessionChanged, quality, &QcStore::setSession, Qt::QueuedConnection);
        connect(accounts, &AccountStore::ready, accounts, [this]() { accounts->applyOriginalConfiguredSession(0); });
        host->setWidget(shell); host->show();
        worker.start();
        QMetaObject::invokeMethod(hospital, [this]() { hospital->initializeOriginal(fixture.root); accounts->initializeOriginal(fixture.root); quality->initializeOriginal(fixture.root); projects->initializeOriginal(fixture.root, fixture.root + "/catalog"); }, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(shell->projectPage()->isReady() && !shell->projectPage()->isBusy(), 15000);
        QMetaObject::invokeMethod(reagents, [this]() { reagents->initializeOriginal(fixture.root); }, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(shell->reagentPage()->isReady() && !shell->reagentPage()->isBusy() && qualityPage->isReady() && !qualityPage->isBusy(), 15000);
        const auto records = shell->projectPage()->projects(); const auto units = shell->projectPage()->units();
        const auto snapshot = shell->reagentPage()->snapshot();
        QMetaObject::invokeMethod(calibration, [this, records, units, snapshot]()
        { calibration->initializeOriginal(fixture.root); calibration->setOriginalCatalog(records, units, snapshot); }, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(calibrationPage->isReady() && !calibrationPage->isBusy(), 15000);
        QTRY_VERIFY(shell->accountSession().authenticated);
        QCOMPARE(shell->accountSession().originalPolicySource, fixture.root);
    }
    void cleanup() { closeWorkers(); directory.reset(); }
    void cleanupTestCase() { host.reset(); }
    void originalPagesReadRealIdsAndFields()
    {
        const QString output = qEnvironmentVariable("SN100_GUI_EVIDENCE_DIR");
        QVERIFY(shell->selectMainPage(4)); QCOMPARE(qualityPage->controls().size(), 1);
        QCOMPARE(qualityPage->controls().first().id, qint64(10));
        QCOMPARE(qualityPage->controls().first().projects.first().projectName, QStringLiteral("PT"));
        QCOMPARE(qualityPage->controls().first().batches.first().id, qint64(20));
        QSignalSpy history(quality, &QcStore::historyReady);
        QMetaObject::invokeMethod(quality, [this]() { quality->loadHistory(9988, 30); }, Qt::QueuedConnection);
        QTRY_VERIFY(!history.isEmpty());
        const auto reply = qvariant_cast<QcHistoryReply>(history.last().at(1));
        QVERIFY(reply.complete()); QCOMPARE(reply.projectId(), qint64(7)); QCOMPARE(reply.sourceRowCount(), qint64(1));
        QTRY_VERIFY(!qualityPage->isBusy());
        if (QDir::isAbsolutePath(output)) QVERIFY(shell->grab().save(output + "/original_quality_page.png"));
        QVERIFY(shell->selectMainPage(3)); QCOMPARE(calibrationPage->records().size(), 1);
        QCOMPARE(calibrationPage->currentRecord().id, qint64(100));
        QCOMPARE(calibrationPage->currentRecord().projectId, qint64(7));
        QCOMPARE(calibrationPage->currentRecord().points.first().originalValues.value("result_value").toString(), QStringLiteral("12.3456789012345|****"));
        QVERIFY(!QFileInfo::exists(fixture.root + "/quality_control"));
        QVERIFY(!QFileInfo::exists(fixture.root + "/calibration"));
        // 旧项目接口仍建立空的兼容目录；原项目没有转换进它，页面使用原编号7。
        QCOMPARE(fixture.value("catalog/catalog.db", "SELECT COUNT(*) FROM project_base").toInt(), 0);
        if (QDir::isAbsolutePath(output)) QVERIFY(shell->grab().save(output + "/original_calibration_page.png"));
    }
    void qualityCancelSaveAndWriteLockRecovery()
    {
        QVERIFY(shell->selectMainPage(4)); const auto before = digest("result/quality.db");
        click(qualityPage, "btnAddControl"); QTRY_VERIFY(manager());
        auto *field = manager()->findChild<QLineEdit *>("ldtFactor"); QVERIFY(field);
        field->setText("CANCEL");
        withAnswer(QMessageBox::Discard, [this]() { click(manager(), "btn_cancel"); });
        QTRY_VERIFY(!qualityPage->findChild<QDialog *>("QcControlManager")); QCOMPARE(digest("result/quality.db"), before);
        click(qualityPage, "btnAddControl"); QTRY_VERIFY(manager());
        field = manager()->findChild<QLineEdit *>("ldtFactor"); QVERIFY(field); field->setText("RK_FACTORY");
        QLockFile lock(fixture.root + "/.sn100-original-project-write.lock"); QVERIFY(lock.tryLock());
        click(manager(), "btn_save");
        QTRY_VERIFY(qualityPage->findChild<CusMessageBox *>("QcWarningMessage"));
        auto *warning = qualityPage->findChild<CusMessageBox *>("QcWarningMessage");
        QTRY_VERIFY(warning->isReadyForInput()); click(warning, "btnSure"); QTRY_VERIFY(!qualityPage->hasWarning());
        QCOMPARE(digest("result/quality.db"), before); QCOMPARE(field->text(), QStringLiteral("RK_FACTORY"));
        lock.unlock(); click(manager(), "btn_save");
        QTRY_COMPARE(fixture.value("result/quality.db", "SELECT factor_name FROM quality_control_list WHERE id=10").toString(), QStringLiteral("RK_FACTORY"));
        QTRY_VERIFY(!qualityPage->isBusy());
        QCOMPARE(fixture.value("result/quality.db", "SELECT vendor_extra FROM quality_control_list WHERE id=10").toString(), QStringLiteral("KEEP"));
        click(manager(), "btnClose"); QTRY_VERIFY(!qualityPage->findChild<QDialog *>("QcControlManager"));
        click(qualityPage, "btnAddControl"); QTRY_VERIFY(manager());
        QCOMPARE(manager()->findChild<QLineEdit *>("ldtFactor")->text(), QStringLiteral("RK_FACTORY"));
        click(manager(), "btnClose"); QTRY_VERIFY(!qualityPage->findChild<QDialog *>("QcControlManager"));
    }
    void calibrationCancelSaveAndExternalConflict()
    {
        QVERIFY(shell->selectMainPage(3)); auto *table = calibrationPage->findChild<QTableWidget *>("tbCalibrationResult");
        QVERIFY(table && table->rowCount() == 1); const auto before = digest("result/calibration.db");
        table->item(0, 2)->setText("999"); QVERIFY(calibrationPage->hasUnsavedChanges());
        click(calibrationPage, "btnCancel"); QCOMPARE(digest("result/calibration.db"), before);
        QVERIFY(!calibrationPage->hasUnsavedChanges());
        table->item(0, 2)->setText("125.5");
        QVERIFY(fixture.database("result/calibration.db", {"UPDATE calibration_base SET vendor_extra='EXTERNAL' WHERE id=1"}));
        withAnswer(QMessageBox::Save, [this]() { click(calibrationPage, "btnSave"); });
        QTRY_VERIFY(!calibrationPage->isBusy()); QVERIFY(calibrationPage->hasUnsavedChanges());
        QCOMPARE(fixture.value("result/calibration.db", "SELECT concentration FROM calibration_data WHERE id=1").toString(), QStringLiteral("100.000000123456"));
        click(calibrationPage, "btnCancel"); calibrationPage->reload(); QTRY_VERIFY(!calibrationPage->isBusy());
        table->item(0, 2)->setText("125.5");
        withAnswer(QMessageBox::Save, [this]() { click(calibrationPage, "btnSave"); });
        QTRY_VERIFY(!calibrationPage->isBusy() && !calibrationPage->hasUnsavedChanges());
        QCOMPARE(fixture.value("result/calibration.db", "SELECT concentration FROM calibration_data WHERE id=1").toString(), QStringLiteral("125.5"));
        QCOMPARE(fixture.value("result/calibration.db", "SELECT result_value FROM calibration_data WHERE id=1").toString(), QStringLiteral("12.3456789012345|****"));
        QCOMPARE(fixture.value("result/calibration.db", "SELECT only_count FROM calibration_data WHERE id=1").toString(), QStringLiteral("91|92"));
        QCOMPARE(fixture.value("result/calibration.db", "SELECT vendor_extra FROM calibration_base WHERE id=1").toString(), QStringLiteral("EXTERNAL"));
        QCOMPARE(fixture.value("result/calibration.db", "SELECT is_checked FROM calibration_base WHERE id=1").toInt(), 0);
    }
    void actualMainOriginalQualityAndCalibrationRestart()
    {
        closeWorkers();
        for (int cycle = 0; cycle < 2; ++cycle)
        {
            const auto output = runMain(); QVERIFY(output.contains("SHELL_READY"));
            QVERIFY(output.contains("THREADS_STOPPED true")); QVERIFY(output.contains("ACTUAL_EXIT=0"));
            QVERIFY(!QFileInfo::exists(fixture.root + "/quality_control"));
            QVERIFY(!QFileInfo::exists(fixture.root + "/calibration"));
            QCOMPARE(fixture.value("catalog/catalog.db", "SELECT COUNT(*) FROM project_base").toInt(), 0);
        }
    }
    void missingOriginalQualityFailsWithoutCreatingReplacement()
    {
        closeWorkers(); QVERIFY(QFile::remove(fixture.root + "/result/quality.db"));
        const auto output = runMain(); QVERIFY(!output.contains("SHELL_READY")); QVERIFY(output.contains("ACTUAL_EXIT=3"));
        QVERIFY(!QFileInfo::exists(fixture.root + "/result/quality.db"));
        QVERIFY(!QFileInfo::exists(fixture.root + "/quality_control"));
    }
};
QTEST_MAIN(OriginalQualityPagesRkTest)
#include "original_quality_pages_rk_test.moc"
