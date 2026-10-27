#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QComboBox>
#include "samples/material/qtmaterialradiobutton.h"
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
#include "data/lis_settings_store.h"
#include "shell/frmmain.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmlissetting.h"

namespace
{
template<class T> T *control(QObject *parent, const char *name)
{
    return parent->findChild<T *>(QString::fromLatin1(name));
}
const QString first = QStringLiteral("ANON_PORT_A");
const QString second = QStringLiteral("ANON_PORT_B");

QString runtimeRoot()
{
    const QString path = qEnvironmentVariable("SN100_LIS_TEST_ROOT");
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
    QThread hospitalThread, accountThread, lisThread;
    HospitalStore *hospital = new HospitalStore;
    AccountStore *accounts = new AccountStore;
    LisSettingsStore *lis = new LisSettingsStore([]()
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
        lis->moveToThread(&lisThread);
        QObject::connect(&hospitalThread, &QThread::finished, hospital, &QObject::deleteLater);
        QObject::connect(&accountThread, &QThread::finished, accounts, &QObject::deleteLater);
        QObject::connect(&lisThread, &QThread::finished, lis, &QObject::deleteLater);
        window = new frmMain(hospital, nullptr, accounts);
        window->attachLisPage(new frmLisSetting(lis));
        const QString hospitalRoot = QDir(root).filePath(QStringLiteral("hospital"));
        const QString accountRoot = QDir(root).filePath(QStringLiteral("account"));
        const QString lisRoot = QDir(root).filePath(QStringLiteral("settings"));
        window->setDataDirectory(hospitalRoot);
        // 重开沿用已有匿名库；再次提供初始密码会被账户防重播种规则拒绝。
        const QString bootstrap = QFileInfo::exists(QDir(accountRoot).filePath("user_Information.db"))
            ? QString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        { hospital->initialize(QDir(hospitalRoot).filePath("hospital_Information.db"), QDir(hospitalRoot).filePath("config.ini")); });
        QObject::connect(&accountThread, &QThread::started, accounts, [=]() { accounts->initialize(accountRoot, bootstrap); });
        QObject::connect(&lisThread, &QThread::started, lis, [=]() { lis->initialize(lisRoot); });
        host.setWidget(window);
        host.resize(1600,900);
        host.show();
        hospitalThread.start(); accountThread.start(); lisThread.start();
    }
    ~Session()
    {
        qInfo() << "LIS_TEST_TEARDOWN lis";
        QMetaObject::invokeMethod(lis,"close",Qt::BlockingQueuedConnection);
        qInfo() << "LIS_TEST_TEARDOWN accounts";
        QMetaObject::invokeMethod(accounts,"close",Qt::BlockingQueuedConnection);
        qInfo() << "LIS_TEST_TEARDOWN hospital";
        QMetaObject::invokeMethod(hospital,"close",Qt::BlockingQueuedConnection);
        lisThread.quit(); accountThread.quit(); hospitalThread.quit();
        lisThread.wait(); accountThread.wait(); hospitalThread.wait();
        qInfo() << "LIS_TEST_TEARDOWN threads_stopped";
        host.takeWidget(); delete window;
    }
    bool ready() const
    {
        return window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
            && window->accountPage()->isReady() && !window->accountPage()->isBusy()
            && window->lisPage()->isReady() && !window->lisPage()->isBusy();
    }
    void session(const QString &name)
    { QMetaObject::invokeMethod(accounts,"setSession",Qt::QueuedConnection,Q_ARG(QString,name),Q_ARG(bool,true)); }
};
}

class LisShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void finalPageAllRolesAndDraftGuards();
    void busyGuardsAndCommittedReopen();
    void actualProductCliInitializationAndExit();
};

void LisShellTest::initTestCase()
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"),11));
    QApplication::setQuitOnLastWindowClosed(false);
    QVERIFY(QFileInfo(qEnvironmentVariable("SN100_LIS_APP")).isExecutable());
    QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_LIS_EVIDENCE")));
    QDir().mkpath(qEnvironmentVariable("SN100_LIS_EVIDENCE"));
}

void LisShellTest::finalPageAllRolesAndDraftGuards()
{
    QTemporaryDir directory(runtimeRoot() + QStringLiteral("/roles_XXXXXX"));
    Session s(directory.path());
    QTRY_VERIFY(s.ready());
    QVERIFY(s.window->selectMainPage(6));
    QVERIFY(s.window->selectUserPage(4));
    auto *page = s.window->lisPage();
    QVERIFY(page->isVisible());
    QVERIFY(!page->property("offlineUnavailable").toBool());
    QCOMPARE(control<QStackedWidget>(s.window,"stkUserCenter")->widget(4), static_cast<QWidget *>(page));
    const QStringList actors = {QString(), "offlineSend", "offlineTest", "offlineCheck", "offlineEngineer", "offlineAdmin"};
    for (int role = 0; role < actors.size(); ++role)
    {
        s.session(actors.at(role));
        QTRY_COMPARE(s.window->accountSession().role, role);
        QTRY_VERIFY(s.ready());
        QVERIFY(control<QWidget>(page,"widget_5")->isEnabled());
        QVERIFY(s.window->accountSession().actionReady);
    }
    auto *address = control<QLineEdit>(page,"lblTcpIp");
    control<QtMaterialRadioButton>(page,"radioBoxTcp")->setChecked(true);
    address->setText("localhost");
    answer(QMessageBox::No);
    QVERIFY(!s.window->selectUserPage(1));
    QCOMPARE(s.window->currentUserPage(),4);
    QVERIFY(page->isDirty());
    answer(QMessageBox::No);
    QVERIFY(!s.window->requestLogout());
    answer(QMessageBox::Yes);
    QVERIFY(s.window->requestLogout());
    QTRY_VERIFY(!s.window->accountSession().authenticated);
    QVERIFY(!page->isDirty());
    QVERIFY(address->text().isEmpty());
    // 仅截图等待原Material选中动画结束，不作为保存/响应计时屏障。
    QTest::qWait(250);
    const QString evidence = qEnvironmentVariable("SN100_LIS_EVIDENCE");
    QVERIFY(s.host.grab().save(QDir(evidence).filePath("lis_1600_top.png")));
    auto *save = control<QPushButton>(page,"btnSave");
    s.host.ensureWidgetVisible(save,12,12);
    QVERIFY(s.host.viewport()->rect().contains(s.host.viewport()->mapFromGlobal(save->mapToGlobal(save->rect().center()))));
    control<QtMaterialRadioButton>(page,"radioBoxTcp")->setChecked(true);
    address->setText("127.0.0.1");
    control<QLineEdit>(page,"lblTcpPort")->setText("12345");
    QTest::mouseClick(save,Qt::LeftButton);
    QTRY_COMPARE(page->committedSettings().ip,QStringLiteral("127.0.0.1"));
    QVERIFY(s.window->selectUserPage(1));
    QVERIFY(s.window->selectUserPage(4));
    QCOMPARE(page->committedSettings().port,QStringLiteral("12345"));
    QTest::qWait(250);
    QVERIFY(s.host.grab().save(QDir(evidence).filePath("lis_1600_scrolled.png")));
    s.host.resize(1920,1080);
    QTest::qWait(80);
    QVERIFY(s.host.grab().save(QDir(evidence).filePath("lis_1920.png")));
}

void LisShellTest::busyGuardsAndCommittedReopen()
{
    QTemporaryDir directory(runtimeRoot() + QStringLiteral("/guards_XXXXXX"));
    {
        Session s(directory.path());
        QTRY_VERIFY(s.ready());
        s.session(QStringLiteral("offlineEngineer"));
        QTRY_VERIFY(s.window->accountSession().authenticated);
        QTRY_VERIFY(s.ready());
        QVERIFY(s.window->enablePasswordLogin());
        QVERIFY(s.window->selectUserPage(4));
        auto *page = s.window->lisPage();
        auto *save = control<QPushButton>(page,"btnSave");
        s.host.ensureWidgetVisible(save,12,12);
        control<QComboBox>(page,"cbxCom")->setCurrentText(first);
        QTest::mouseClick(save,Qt::LeftButton);
        QVERIFY(page->isBusy());
        QVERIFY(!s.window->selectUserPage(1));
        QVERIFY(!s.window->selectMainPage(2));
        QVERIFY(!s.window->requestLogin());
        QVERIFY(!s.window->requestLogout());
        QVERIFY(!s.window->close());
        QTRY_COMPARE(page->committedSettings().commName,first);
        QVERIFY(s.window->selectMainPage(0));
        QVERIFY(s.window->selectMainPage(6));
    }
    Session reopened(directory.path());
    QTRY_VERIFY(reopened.ready());
    QCOMPARE(reopened.window->lisPage()->committedSettings().commName,first);
}

void LisShellTest::actualProductCliInitializationAndExit()
{
    const QString application = qEnvironmentVariable("SN100_LIS_APP");
    const QString evidence = qEnvironmentVariable("SN100_LIS_EVIDENCE");
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
        if (mode != "legacy") args << "--lis-settings";
        if (mode == "full") args << "--printer-settings" << "--history" << "--catalog" << "--samples" << "--instrument" << "--password-login" << "--status-loopback";
        if (mode == "invalid")
        {
            QDir().mkpath(directory.filePath("settings"));
            QFile file(directory.filePath("settings/lis.ini"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[lis]\nname=untagged\n"); file.close();
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
            QVERIFY(output.contains("LIS_SETTINGS_READY"));
            QCOMPARE(output.count("LIS_SETTINGS_CLOSED"),1);
            QCOMPARE(output.count("PRINTER_SETTINGS_CLOSED"),1);
            for (const QByteArray &store : {"HOSPITAL","ACCOUNT","HISTORY","PROJECT","REAGENT","SAMPLE","MAINTENANCE"})
                QCOMPARE(output.count(store+"_STORE_CLOSED"),1);
            QVERIFY(output.contains("STATUS_LOOPBACK_STOPPED true"));
        }
        if (mode == "legacy") QVERIFY(!QFileInfo::exists(directory.filePath("settings")));
        if (mode == "invalid")
        {
            QVERIFY(!output.contains("SHELL_READY"));
            QFile original(directory.filePath("settings/lis.ini"));
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(),QByteArray("[lis]\nname=untagged\n"));
        }
        QFile log(QDir(evidence).filePath(mode+"_product.log"));
        QVERIFY(log.open(QIODevice::WriteOnly)); log.write(output);
        QFile receipt(QDir(evidence).filePath(mode+"_exit.txt"));
        QVERIFY(receipt.open(QIODevice::WriteOnly)); receipt.write(QByteArray::number(product.exitCode())+'\n');
    }
}

QTEST_MAIN(LisShellTest)
#include "lis_settings_shell_test.moc"
