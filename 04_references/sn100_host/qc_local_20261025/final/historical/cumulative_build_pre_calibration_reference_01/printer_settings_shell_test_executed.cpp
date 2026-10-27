#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include "data/account_store.h"
#include "data/hospital_store.h"
#include "data/printer_settings_store.h"
#include "shell/frmmain.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmprintersetting.h"

namespace
{
template<class T> T *control(QObject *parent, const char *name)
{
    return parent->findChild<T *>(QString::fromLatin1(name));
}
const QString first = QStringLiteral("匿名本地打印设备");
const QString second = QStringLiteral("匿名备用打印设备");

QString runtimeRoot()
{
    const QString path = qEnvironmentVariable("SN100_PRINTER_TEST_ROOT");
    if (!QDir::isAbsolutePath(path)) qFatal("Explicit isolated runtime root required");
    QDir().mkpath(path);
    return path;
}

void answer(QMessageBox::StandardButton button)
{
    QTimer::singleShot(20, qApp, [button]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(button)->click();
    });
}

struct Session
{
    QThread hospitalThread, accountThread, printerThread;
    HospitalStore *hospital = new HospitalStore;
    AccountStore *accounts = new AccountStore;
    PrinterSettingsStore *printer = new PrinterSettingsStore([]()
    {
        QThread::msleep(80);
        return QStringList{first, second};
    });
    frmMain *window;
    QScrollArea host;
    explicit Session(const QString &root)
    {
        HospitalStore::registerTypes(); AccountStore::registerTypes();
        hospital->moveToThread(&hospitalThread);
        accounts->moveToThread(&accountThread);
        printer->moveToThread(&printerThread);
        QObject::connect(&hospitalThread, &QThread::finished, hospital, &QObject::deleteLater);
        QObject::connect(&accountThread, &QThread::finished, accounts, &QObject::deleteLater);
        QObject::connect(&printerThread, &QThread::finished, printer, &QObject::deleteLater);
        window = new frmMain(hospital, nullptr, accounts);
        window->attachPrinterPage(new frmPrinterSetting(printer));
        const QString hospitalRoot = QDir(root).filePath(QStringLiteral("hospital"));
        const QString accountRoot = QDir(root).filePath(QStringLiteral("account"));
        const QString printerRoot = QDir(root).filePath(QStringLiteral("settings"));
        window->setDataDirectory(hospitalRoot);
        // 重开沿用已有匿名库；再次提供初始密码会被账户防重播种规则拒绝。
        const QString bootstrap = QFileInfo::exists(QDir(accountRoot).filePath("user_Information.db"))
            ? QString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        { hospital->initialize(QDir(hospitalRoot).filePath("hospital_Information.db"), QDir(hospitalRoot).filePath("config.ini")); });
        QObject::connect(&accountThread, &QThread::started, accounts, [=]() { accounts->initialize(accountRoot, bootstrap); });
        QObject::connect(&printerThread, &QThread::started, printer, [=]() { printer->initialize(printerRoot); });
        host.setWidget(window);
        host.resize(1600,900);
        host.show();
        hospitalThread.start(); accountThread.start(); printerThread.start();
    }
    ~Session()
    {
        qInfo() << "PRINTER_TEST_TEARDOWN printer";
        QMetaObject::invokeMethod(printer,"close",Qt::BlockingQueuedConnection);
        qInfo() << "PRINTER_TEST_TEARDOWN accounts";
        QMetaObject::invokeMethod(accounts,"close",Qt::BlockingQueuedConnection);
        qInfo() << "PRINTER_TEST_TEARDOWN hospital";
        QMetaObject::invokeMethod(hospital,"close",Qt::BlockingQueuedConnection);
        printerThread.quit(); accountThread.quit(); hospitalThread.quit();
        printerThread.wait(); accountThread.wait(); hospitalThread.wait();
        qInfo() << "PRINTER_TEST_TEARDOWN threads_stopped";
        host.takeWidget(); delete window;
    }
    bool ready() const
    {
        return window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
            && window->accountPage()->isReady() && !window->accountPage()->isBusy()
            && window->printerPage()->isReady() && !window->printerPage()->isBusy();
    }
    void session(const QString &name)
    { QMetaObject::invokeMethod(accounts,"setSession",Qt::QueuedConnection,Q_ARG(QString,name),Q_ARG(bool,true)); }
};
}

class PrinterShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void finalPageAllRolesAndDraftGuards();
    void busyGuardsAndCommittedReopen();
    void actualProductCliInitializationAndExit();
};

void PrinterShellTest::initTestCase()
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"),11));
    QApplication::setQuitOnLastWindowClosed(false);
    QVERIFY(QFileInfo(qEnvironmentVariable("SN100_PRINTER_APP")).isExecutable());
    QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_PRINTER_EVIDENCE")));
    QDir().mkpath(qEnvironmentVariable("SN100_PRINTER_EVIDENCE"));
}

void PrinterShellTest::finalPageAllRolesAndDraftGuards()
{
    QTemporaryDir directory(runtimeRoot() + QStringLiteral("/roles_XXXXXX"));
    Session s(directory.path());
    QTRY_VERIFY(s.ready());
    QVERIFY(s.window->selectMainPage(6));
    QVERIFY(s.window->selectUserPage(3));
    auto *page = s.window->printerPage();
    QVERIFY(page->isVisible());
    QVERIFY(!page->property("offlineUnavailable").toBool());
    QCOMPARE(control<QStackedWidget>(s.window,"stkUserCenter")->widget(3), static_cast<QWidget *>(page));
    const QStringList actors = {QString(), "offlineSend", "offlineTest", "offlineCheck", "offlineEngineer", "offlineAdmin"};
    for (int role = 0; role < actors.size(); ++role)
    {
        s.session(actors.at(role));
        QTRY_COMPARE(s.window->accountSession().role, role);
        QTRY_VERIFY(s.ready());
        QVERIFY(control<QListWidget>(page,"lstProject")->isEnabled());
        QVERIFY(s.window->accountSession().actionReady);
    }
    auto *list = control<QListWidget>(page,"lstProject");
    list->setCurrentRow(1);
    answer(QMessageBox::No);
    QVERIFY(!s.window->selectUserPage(1));
    QCOMPARE(s.window->currentUserPage(),3);
    QVERIFY(page->isDirty());
    answer(QMessageBox::No);
    QVERIFY(!s.window->requestLogout());
    QVERIFY(s.window->accountSession().authenticated);
    answer(QMessageBox::Yes);
    QVERIFY(s.window->requestLogout());
    QTRY_VERIFY(!s.window->accountSession().authenticated);
    QVERIFY(!page->isDirty());
    QVERIFY(control<QLineEdit>(page,"ldtPrinterSelect")->text().isEmpty());
    const QString evidence = qEnvironmentVariable("SN100_PRINTER_EVIDENCE");
    QVERIFY(s.host.grab().save(QDir(evidence).filePath("printer_1600_top.png")));
    auto *apply = control<QPushButton>(page,"btnApply");
    s.host.ensureWidgetVisible(apply,12,12);
    // 1600x900沿用原1920x1080外层滚动，不把对裁切控件的直接投递当可达。
    QVERIFY(s.host.viewport()->rect().contains(s.host.viewport()->mapFromGlobal(
        apply->mapToGlobal(apply->rect().center()))));
    list->setCurrentRow(0);
    QTest::mouseClick(apply,Qt::LeftButton);
    QTRY_COMPARE(page->committedName(),first);
    QVERIFY(s.window->selectUserPage(1));
    QVERIFY(s.window->selectUserPage(3));
    QCOMPARE(page->committedName(),first);
    QVERIFY(s.host.grab().save(QDir(evidence).filePath("printer_1600.png")));
    s.host.resize(1920,1080);
    QTest::qWait(80);
    QVERIFY(s.host.grab().save(QDir(evidence).filePath("printer_1920.png")));
}

void PrinterShellTest::busyGuardsAndCommittedReopen()
{
    QTemporaryDir directory(runtimeRoot() + QStringLiteral("/guards_XXXXXX"));
    {
        Session s(directory.path());
        QTRY_VERIFY(s.ready());
        s.session(QStringLiteral("offlineEngineer"));
        QTRY_VERIFY(s.window->accountSession().authenticated);
        // 身份信号会先于账户页排队重载完成；不能把它当GUI空闲屏障。
        QTRY_VERIFY(s.ready());
        QVERIFY(s.window->enablePasswordLogin());
        QVERIFY(s.window->selectUserPage(3));
        auto *page = s.window->printerPage();
        s.host.ensureWidgetVisible(control<QPushButton>(page,"btnApply"),12,12);
        control<QListWidget>(page,"lstProject")->setCurrentRow(0);
        QTest::mouseClick(control<QPushButton>(page,"btnApply"),Qt::LeftButton);
        QVERIFY(page->isBusy());
        qInfo() << "PRINTER_TEST_STAGE saving";
        QVERIFY(!s.window->selectUserPage(1));
        qInfo() << "PRINTER_TEST_STAGE user_guard";
        QVERIFY(!s.window->selectMainPage(2));
        qInfo() << "PRINTER_TEST_STAGE main_guard";
        QVERIFY(!s.window->requestLogin());
        qInfo() << "PRINTER_TEST_STAGE login_guard";
        QVERIFY(!s.window->requestLogout());
        qInfo() << "PRINTER_TEST_STAGE logout_guard";
        QVERIFY(!s.window->close());
        qInfo() << "PRINTER_TEST_STAGE close_guard";
        QTRY_COMPARE(page->committedName(),first);
        qInfo() << "PRINTER_TEST_STAGE committed";
        QVERIFY(s.window->selectMainPage(0));
        qInfo() << "PRINTER_TEST_STAGE page0";
        QVERIFY(s.window->selectMainPage(6));
        qInfo() << "PRINTER_TEST_STAGE page6";
        QCOMPARE(page->committedName(),first);
    }
    Session reopened(directory.path());
    qInfo() << "PRINTER_TEST_STAGE reopened_created";
    QTRY_VERIFY(reopened.ready());
    qInfo() << "PRINTER_TEST_STAGE reopened_ready";
    QCOMPARE(reopened.window->printerPage()->committedName(),first);
}

void PrinterShellTest::actualProductCliInitializationAndExit()
{
    const QString application = qEnvironmentVariable("SN100_PRINTER_APP");
    const QString evidence = qEnvironmentVariable("SN100_PRINTER_EVIDENCE");
    for (const QString &mode : {QStringLiteral("full"),QStringLiteral("legacy"),QStringLiteral("invalid")})
    {
        QTemporaryDir directory(runtimeRoot() + QStringLiteral("/product_") + mode + QStringLiteral("_XXXXXX"));
        QTemporaryFile bootstrap(directory.filePath(QStringLiteral(".anonymous-bootstrap-XXXXXX")));
        QVERIFY(bootstrap.open());
        const QByteArray transientSecret = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
        QCOMPARE(bootstrap.write(transientSecret),qint64(transientSecret.size()));
        QVERIFY(bootstrap.flush());
        QStringList args = {"--offline","--accounts","--data-dir",directory.path(),"--viewport","1600x900"};
        args << "--seed-anonymous" << "--test-secret-file" << bootstrap.fileName();
        if (mode != "legacy") args << "--printer-settings";
        if (mode == "full") args << "--history" << "--catalog" << "--samples" << "--instrument" << "--password-login" << "--status-loopback";
        if (mode == "invalid")
        {
            QDir().mkpath(directory.filePath("settings"));
            QFile file(directory.filePath("settings/printer.ini"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[printer]\nname=untagged\n"); file.close();
        }
        QProcess product;
        product.setProcessChannelMode(QProcess::MergedChannels);
        product.start(application,args);
        QTRY_VERIFY(product.state() == QProcess::Running || product.state() == QProcess::NotRunning);
        QByteArray output;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 12000 && !output.contains("SHELL_READY") && product.state()!=QProcess::NotRunning)
        { QTest::qWait(20); output += product.readAll(); }
        if (mode != "invalid")
        {
            QVERIFY2(output.contains("SHELL_READY"),output.constData());
            product.terminate();
        }
        QTRY_COMPARE(product.state(),QProcess::NotRunning);
        output += product.readAll();
        QCOMPARE(product.exitStatus(),QProcess::NormalExit);
        QCOMPARE(product.exitCode(),mode == "invalid" ? 3 : 0);
        if (mode == "full")
        {
            QVERIFY(output.contains("PRINTER_SETTINGS_READY"));
            QCOMPARE(output.count("PRINTER_SETTINGS_CLOSED"),1);
            for (const QByteArray &store : {"HOSPITAL","ACCOUNT","HISTORY","PROJECT","REAGENT","SAMPLE","MAINTENANCE"})
                QCOMPARE(output.count(store+"_STORE_CLOSED"),1);
            QVERIFY(output.contains("STATUS_LOOPBACK_STOPPED true"));
        }
        if (mode == "legacy") QVERIFY(!QFileInfo::exists(directory.filePath("settings")));
        if (mode == "invalid")
        {
            QVERIFY(!output.contains("SHELL_READY"));
            QFile original(directory.filePath("settings/printer.ini"));
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(),QByteArray("[printer]\nname=untagged\n"));
        }
        QFile log(QDir(evidence).filePath(mode+"_product.log"));
        QVERIFY(log.open(QIODevice::WriteOnly)); log.write(output);
        QFile receipt(QDir(evidence).filePath(mode+"_exit.txt"));
        QVERIFY(receipt.open(QIODevice::WriteOnly)); receipt.write(QByteArray::number(product.exitCode())+'\n');
    }
}

QTEST_MAIN(PrinterShellTest)
#include "printer_settings_shell_test.moc"
