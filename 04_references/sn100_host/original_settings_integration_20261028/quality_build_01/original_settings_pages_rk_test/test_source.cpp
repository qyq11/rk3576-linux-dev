// 在RK匿名目录核医院、打印和LIS页面共用原配置时的保存、取消和失败恢复。
#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QProcess>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <memory>
#include "data/hospital_store.h"
#include "data/lis_settings_store.h"
#include "data/printer_settings_store.h"
#include "original_files/original_ini_write.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmlissetting.h"
#include "ui/frmprintersetting.h"
#include "samples/tests/original_task_flow_fixture.h"

class OriginalSettingsPagesRkTest : public QObject
{
    Q_OBJECT
    QThread worker;
    std::unique_ptr<QTemporaryDir> directory;
    HospitalStore *hospital = nullptr;
    PrinterSettingsStore *printer = nullptr;
    LisSettingsStore *lis = nullptr;
    std::unique_ptr<frmHospInfoSetting> hospitalPage;
    std::unique_ptr<frmPrinterSetting> printerPage;
    std::unique_ptr<frmLisSetting> lisPage;
    QString root;

    QByteArray bytes() const
    {
        QFile file(root + "/config.ini");
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    bool write(const QString &path, const QByteArray &value)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
    }
    template<class T> T *control(QObject *parent, const char *name)
    { return parent->findChild<T *>(QString::fromLatin1(name)); }
    void answerDiscard()
    {
        QTimer::singleShot(20, qApp, []()
        {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (box) box->button(QMessageBox::Yes)->click();
        });
    }
    void click(QObject *page, const char *name)
    {
        auto *button = control<QPushButton>(page, name);
        QVERIFY(button);
        QVERIFY(button->isEnabled());
        QTest::mouseClick(button, Qt::LeftButton);
    }
    void saveHospital(const QString &name)
    {
        control<QLineEdit>(hospitalPage.get(), "ldtHospName")->setText(name);
        click(hospitalPage.get(), "btnSave");
        QTRY_VERIFY(!hospitalPage->isBusy());
    }
    QString value(const QString &key)
    {
        OriginalIni::Snapshot snapshot; QString message;
        if (!OriginalIni::readVersioned(root, "config.ini", &snapshot, &message)) return {};
        return snapshot.parsed.values.value(key).toString();
    }

private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("本驱动只允许在RK ARM64运行。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
        HospitalStore::registerTypes();
    }
    void init()
    {
        directory.reset(new QTemporaryDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT") + "/settings_XXXXXX"));
        QVERIFY(directory->isValid()); root = directory->path();
        QVERIFY(QDir().mkpath(root + "/db/userSet"));
        QVERIFY(write(root + "/.sn100-original-work-v1", "sn100-original-work-v1\n"));
        QVERIFY(write(root + "/theme_config.ini", "[hospital]\nname=主题医院\n"));
        QVERIFY(write(root + "/config.ini", "\xef\xbb\xbf# 只含匿名设置，保留注释与换行\r\n"
            "[hospital]\r\nname=原医院\r\n[printer]\r\nname=ANON_PRINTER_A\r\n"
            "[lis]\r\ntype=tcp\r\nip=127.0.0.1\r\nport=05000\r\ncommname=ANON_COM_A\r\nbaudrate=9600\r\n"
            "samMode=0\r\nautoget=false\r\nautosend=false\r\nlink=false\r\n"
            "databits=data8\r\nparity=evenparity\r\nstopbits=oneandhalfstop\r\nflowcontrol=hardwarecontrol\r\n"
            "tcptype=server\r\nsequencenumber=0007\r\nserverID=ANON\r\nmoni=false\r\n"
            "[vendor]\r\nunknown=KEEP  spaces\r\n"));
        const QString connection = "settings-fixture";
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(root + "/db/userSet/hospital_Information.db"); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE hospital_information(id INTEGER PRIMARY KEY AUTOINCREMENT,column_one VARCHAR,column_two VARCHAR,column_three VARCHAR,type VARCHAR,vendor_note TEXT DEFAULT 'KEEP')"));
            QVERIFY(query.exec("INSERT INTO hospital_information(column_one,column_two,column_three,type) VALUES('科室甲','KSJ','','keshi')"));
        }
        QSqlDatabase::removeDatabase(connection);
        hospital = new HospitalStore;
        printer = new PrinterSettingsStore([]() { return QStringList{"ANON_PRINTER_A", "ANON_PRINTER_B"}; });
        lis = new LisSettingsStore([]() { return QStringList{"ANON_COM_A", "ANON_COM_B"}; });
        for (QObject *store : {static_cast<QObject *>(hospital), static_cast<QObject *>(printer), static_cast<QObject *>(lis)})
        {
            store->moveToThread(&worker);
            connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        }
        hospitalPage.reset(new frmHospInfoSetting(hospital));
        printerPage.reset(new frmPrinterSetting(printer));
        lisPage.reset(new frmLisSetting(lis));
        worker.start();
        for (QObject *store : {static_cast<QObject *>(hospital), static_cast<QObject *>(printer), static_cast<QObject *>(lis)})
            QVERIFY(QMetaObject::invokeMethod(store, "initializeOriginal", Qt::QueuedConnection, Q_ARG(QString, root)));
        QTRY_VERIFY(hospitalPage->isReady() && !hospitalPage->isBusy());
        QTRY_VERIFY(printerPage->isReady() && !printerPage->isBusy());
        QTRY_VERIFY(lisPage->isReady() && !lisPage->isBusy());
        hospitalPage->show(); printerPage->show(); lisPage->show();
    }
    void cleanup()
    {
        hospitalPage.reset(); printerPage.reset(); lisPage.reset();
        for (QObject *store : {static_cast<QObject *>(hospital), static_cast<QObject *>(printer), static_cast<QObject *>(lis)})
            QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit(); QVERIFY(worker.wait(5000));
        hospital = nullptr; printer = nullptr; lis = nullptr; directory.reset();
    }
    void readsOriginalValuesAndQtSerialEnums()
    {
        QCOMPARE(control<QLineEdit>(hospitalPage.get(), "ldtHospName")->text(), QStringLiteral("原医院"));
        QCOMPARE(control<QLineEdit>(printerPage.get(), "ldtPrinterSelect")->text(), QStringLiteral("ANON_PRINTER_A"));
        const auto s = lisPage->committedSettings();
        QVERIFY(s.originalProfile && s.serialFramingKnown);
        QCOMPARE(s.dataBits, 8); QCOMPARE(s.parity, 2); QCOMPARE(s.stopBits, 3); QCOMPARE(s.flowControl, 1);
        QCOMPARE(s.port, QStringLiteral("05000")); QCOMPARE(s.sequenceNumber, QStringLiteral("0007"));
        QVERIFY(!QFileInfo::exists(root + "/settings"));
    }
    void cancelDraftsDoesNotWrite()
    {
        const auto original = bytes();
        control<QLineEdit>(hospitalPage.get(), "ldtHospName")->setText(QStringLiteral("未存医院"));
        answerDiscard(); click(hospitalPage.get(), "btnCancel");
        QTRY_VERIFY(!hospitalPage->hasUnsavedChanges());
        control<QLineEdit>(lisPage.get(), "lblTcpPort")->setText("6001");
        answerDiscard(); QVERIFY(lisPage->requestLeave(QStringLiteral("离开")));
        QCOMPARE(bytes(), original);
    }
    void interleavedPagesPreserveEachOthersKeys()
    {
        control<QLineEdit>(lisPage.get(), "lblTcpPort")->setText("6001");
        click(lisPage.get(), "btnSave"); QTRY_VERIFY(!lisPage->isBusy());
        QCOMPARE(value("lis/port"), QStringLiteral("6001"));
        auto *list = control<QListWidget>(printerPage.get(), "lstProject");
        QVERIFY(list); list->setCurrentRow(1); click(printerPage.get(), "btnApply");
        QTRY_VERIFY(!printerPage->isBusy()); QCOMPARE(value("printer/name"), QStringLiteral("ANON_PRINTER_B"));
        saveHospital(QStringLiteral("已存医院"));
        QVERIFY2(!hospitalPage->hasUnsavedChanges(), qPrintable(hospitalPage->statusText()));
        QCOMPARE(value("hospital/name"), QStringLiteral("已存医院"));
        QCOMPARE(value("lis/port"), QStringLiteral("6001"));
        QCOMPARE(value("printer/name"), QStringLiteral("ANON_PRINTER_B"));
        QVERIFY(bytes().startsWith("\xef\xbb\xbf# "));
        QVERIFY(bytes().contains("unknown=KEEP  spaces\r\n"));
    }
    void sameHospitalNameConflictKeepsDraftAndRecovers()
    {
        OriginalIni::Snapshot before, after; QString message;
        QVERIFY(OriginalIni::readVersioned(root, "config.ini", &before, &message));
        QVERIFY(OriginalIni::patch(root, "config.ini", before.fingerprint, {{"hospital/name", QStringLiteral("外部医院")}}, {"hospital/name"}, &after, &message));
        saveHospital(QStringLiteral("本页草稿"));
        QVERIFY(hospitalPage->hasUnsavedChanges());
        QCOMPARE(value("hospital/name"), QStringLiteral("外部医院"));
        answerDiscard(); click(hospitalPage.get(), "btnReload"); QTRY_VERIFY(!hospitalPage->isBusy());
        saveHospital(QStringLiteral("重读后保存"));
        QVERIFY(!hospitalPage->hasUnsavedChanges());
        QCOMPARE(value("hospital/name"), QStringLiteral("重读后保存"));
    }
    void sharedWriteLockFailsWithoutLossThenRetries()
    {
        QLockFile lock(root + "/config.ini.sn100-original-config.lock"); QVERIFY(lock.tryLock());
        const auto original = bytes();
        control<QLineEdit>(lisPage.get(), "lblTcpPort")->setText("6002");
        click(lisPage.get(), "btnSave"); QTRY_VERIFY(!lisPage->isBusy());
        QVERIFY(lisPage->isDirty()); QCOMPARE(bytes(), original);
        lock.unlock(); click(lisPage.get(), "btnSave"); QTRY_VERIFY(!lisPage->isBusy());
        QVERIFY(!lisPage->isDirty()); QCOMPARE(value("lis/port"), QStringLiteral("6002"));
    }
    void onlyCurrentLisModeIsSaved()
    {
        LisSettings draft = lisPage->committedSettings();
        draft.type = "serial"; draft.commName = "ANON_COM_B"; draft.baudrate = "19200";
        draft.ip = "changed-draft"; draft.port = "9999";
        QSignalSpy saved(lis, &LisSettingsStore::saved);
        QVERIFY(QMetaObject::invokeMethod(lis, "save", Qt::QueuedConnection, Q_ARG(LisSettings, draft)));
        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(value("lis/type"), QStringLiteral("serial"));
        QCOMPARE(value("lis/commname"), QStringLiteral("ANON_COM_B"));
        QCOMPARE(value("lis/ip"), QStringLiteral("127.0.0.1"));
        QCOMPARE(value("lis/port"), QStringLiteral("05000"));
        QVERIFY(bytes().contains("parity=evenparity\r\n"));
        QVERIFY(bytes().contains("sequencenumber=0007\r\n"));
    }
    void closeAndReopenReadsCommittedOriginalFiles()
    {
        saveHospital(QStringLiteral("重开医院"));
        QMetaObject::invokeMethod(hospital, "close", Qt::BlockingQueuedConnection);
        QSignalSpy opened(hospital, &HospitalStore::ready);
        QVERIFY(QMetaObject::invokeMethod(hospital, "initializeOriginal", Qt::QueuedConnection, Q_ARG(QString, root)));
        QTRY_COMPARE(opened.count(), 1);
        QTRY_VERIFY(!hospitalPage->isBusy());
        QSignalSpy read(hospital, &HospitalStore::loaded);
        QMetaObject::invokeMethod(hospital, "load", Qt::QueuedConnection, Q_ARG(QString, QStringLiteral("keshi")));
        QTRY_COMPARE(read.count(), 1);
        QCOMPARE(read.first().at(1).toString(), QStringLiteral("重开医院"));
        QVERIFY(!QFileInfo::exists(root + "/hospital/hospital_Information.db"));
    }
    void conflictingLisWriterKeepsDraft()
    {
        OriginalIni::Snapshot before, after; QString message;
        QVERIFY(OriginalIni::readVersioned(root, "config.ini", &before, &message));
        QVERIFY(OriginalIni::patch(root, "config.ini", before.fingerprint, {{"lis/port", "7001"}}, {"lis/port"}, &after, &message));
        control<QLineEdit>(lisPage.get(), "lblTcpPort")->setText("6003");
        click(lisPage.get(), "btnSave"); QTRY_VERIFY(!lisPage->isBusy());
        QVERIFY(lisPage->isDirty()); QCOMPARE(value("lis/port"), QStringLiteral("7001"));
        QCOMPARE(control<QLineEdit>(lisPage.get(), "lblTcpPort")->text(), QStringLiteral("6003"));
    }
    void formalMainOpensOriginalSettingsAndRestarts()
    {
        const QString binary = qEnvironmentVariable("SN100_FORMAL_MAIN_BINARY");
        QVERIFY(QFileInfo(binary).isExecutable());
        OriginalTaskFlowFixture fixture;
        const QString formalRoot = root + "/formal_main";
        fixture.create(formalRoot);
        QVERIFY(!QTest::currentTestFailed());
        QVERIFY(QFile::copy(root + "/db/userSet/hospital_Information.db", formalRoot + "/db/userSet/hospital_Information.db"));
        for (const auto &append : QList<QPair<QString, QByteArray>>{
            {"config.ini", "\n[hospital]\nname=原医院\n[printer]\nname=ANON_PRINTER_A\n[lis]\ntype=tcp\nip=127.0.0.1\nport=05000\ncommname=ANON_COM_A\nbaudrate=9600\nsamMode=0\nautoget=false\nautosend=false\nlink=false\n"},
            {"config_Debug.ini", "\n[login]\nopen=false\ndefaultPermission=0\n"}})
        {
            QFile file(formalRoot + '/' + append.first); QVERIFY(file.open(QIODevice::Append));
            QCOMPARE(file.write(append.second), qint64(append.second.size()));
        }
        QVERIFY(fixture.database("db/userSet/user_Information.db", {
            "CREATE TABLE user_information(id INTEGER PRIMARY KEY AUTOINCREMENT,name VARCHAR,role INTEGER,pwd VARCHAR,isdefault INTEGER)"}));
        QVERIFY(fixture.database("db/systemInfo/user.db", {
            "CREATE TABLE user_role(id INTEGER PRIMARY KEY AUTOINCREMENT,role VARCHAR)",
            "INSERT INTO user_role(id,role) VALUES(0,'匿名默认角色')",
            "CREATE TABLE user_powers(id INTEGER PRIMARY KEY AUTOINCREMENT,describe VARCHAR,can_role_id VARCHAR)"}));
        const auto fileBytes = [&]() { QFile file(formalRoot + "/config.ini"); file.open(QIODevice::ReadOnly); return file.readAll(); };
        const auto original = fileBytes();
        for (int restart = 0; restart < 2; ++restart)
        {
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.setWorkingDirectory(formalRoot);
            process.start(binary, {"--data-dir", formalRoot, "--original-db-root", formalRoot,
                "--accounts", "--catalog", "--samples", "--printer-settings", "--lis-settings", "--viewport", "1600x900"});
            QVERIFY(process.waitForStarted(5000));
            QByteArray output;
            QElapsedTimer deadline; deadline.start();
            while (deadline.elapsed() < 20000 && !output.contains("SHELL_READY") && process.state() != QProcess::NotRunning)
            {
                QTest::qWait(50); output += process.readAll();
            }
            const bool ready = output.contains("SHELL_READY");
            process.terminate();
            const bool closed = process.waitForFinished(10000);
            output += process.readAll();
            qInfo().noquote() << "FORMAL_MAIN_RESTART" << restart << output;
            if (!closed) { process.kill(); process.waitForFinished(); }
            QVERIFY2(ready, output.constData());
            QVERIFY(closed); QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 0);
            QVERIFY(output.contains("ORIGINAL_SETTINGS_READY hospital=true printer= true lis= true network=false"));
            QVERIFY(output.contains("THREADS_STOPPED true"));
            QCOMPARE(fileBytes(), original);
            QVERIFY(!QFileInfo::exists(formalRoot + "/hospital/hospital_Information.db"));
            QVERIFY(!QFileInfo::exists(formalRoot + "/settings/printer.ini"));
            QVERIFY(!QFileInfo::exists(formalRoot + "/settings/lis.ini"));
        }
    }
};
QTEST_MAIN(OriginalSettingsPagesRkTest)
#include "original_settings_pages_rk_test.moc"
