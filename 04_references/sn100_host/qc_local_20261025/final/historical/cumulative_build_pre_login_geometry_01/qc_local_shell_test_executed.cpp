#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>
#include "data/account_store.h"
#include "data/hospital_store.h"
#include "projects/project_page.h"
#include "quality_control/quality_control_page.h"
#include "ui/cusmessagebox.h"
#include "reagents/reagents_page.h"
#include "shell/frmmain.h"
#include "ui/cusdialogmask.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

namespace
{
bool until(const std::function<bool()> &condition, int timeout = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) QTest::qWait(10);
    return condition();
}
template<class T> T *control(QObject *root, const char *name)
{
    return root ? root->findChild<T *>(QString::fromLatin1(name)) : nullptr;
}
bool click(QObject *root, const char *name)
{
    if (!until([]()
    {
        for (QWidget *widget : QApplication::allWidgets())
            if (widget->objectName() == QStringLiteral("QcWarningMessage") && widget->isVisible()) return false;
        return true;
    })) return false;
    auto *button = control<QPushButton>(root, name);
    if (!button || !until([button]() { return button->isVisible() && button->isEnabled(); })) return false;
    if (auto *mask = control<CusDialogMask>(root, "mask"))
        if (!until([mask]() { return mask->isReadyForInput(); })) return false;
    QTest::mouseClick(button, Qt::LeftButton);
    return true;
}
void type(QLineEdit *edit, const QString &text)
{
    if (!until([]()
    {
        for (QWidget *widget : QApplication::allWidgets())
            if (widget->objectName() == QStringLiteral("QcWarningMessage") && widget->isVisible()) return false;
        return true;
    })) qFatal("QC warning was not acknowledged");
    QWidget *ancestor = edit;
    while (ancestor && !qobject_cast<QDialog *>(ancestor)) ancestor = ancestor->parentWidget();
    if (auto *mask = control<CusDialogMask>(ancestor, "mask"))
        if (!until([mask]() { return mask->isReadyForInput(); })) qFatal("QC dialog did not become ready");
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(edit, text);
}
void answer(QMessageBox::StandardButton button)
{
    QTimer::singleShot(20, qApp, [button]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (auto *target = box->button(button)) QTest::mouseClick(target, Qt::LeftButton);
    });
}

// The same five owned workers used by the real shell paths. No SQL connection
// is borrowed from a page, and no production configuration is read.
class ShellFixture
{
public:
    explicit ShellFixture(const QString &root)
        : hospital(new HospitalStore), accounts(new AccountStore), projects(new ProjectsStore),
          reagents(new ReagentStore), quality(new QcStore), root(root)
    {
        HospitalStore::registerTypes(); AccountStore::registerTypes();
        ProjectsStore::registerTypes(); ReagentStore::registerTypes(); QcStore::registerTypes();
        workers = {{hospital, &hospitalThread}, {accounts, &accountThread}, {projects, &projectThread},
                   {reagents, &reagentThread}, {quality, &qualityThread}};
        for (const auto &entry : workers)
        {
            entry.first->moveToThread(entry.second);
            QObject::connect(entry.second, &QThread::finished, entry.first, &QObject::deleteLater);
        }
        window = new frmMain(hospital, nullptr, accounts);
        const QString hospitalRoot = QDir(root).filePath(QStringLiteral("hospital"));
        const QString accountRoot = QDir(root).filePath(QStringLiteral("account"));
        const QString catalogRoot = QDir(root).filePath(QStringLiteral("catalog"));
        const QString qualityRoot = QDir(root).filePath(QStringLiteral("quality_control"));
        window->setDataDirectory(hospitalRoot);
        projectPage = new ProjectPage(projects);
        reagentPage = new ReagentsPage(reagents);
        page = new QualityControlPage(quality);
        window->attachCatalogPages(projectPage, reagentPage);
        window->attachQualityControlPage(page);
        window->enablePasswordLogin();
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        { hospital->initialize(QDir(hospitalRoot).filePath("hospital_Information.db"), QDir(hospitalRoot).filePath("config.ini")); });
        const QString bootstrap = QFileInfo::exists(QDir(accountRoot).filePath("user_Information.db"))
            ? QString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        QObject::connect(&accountThread, &QThread::started, accounts, [=]() { accounts->initialize(accountRoot, bootstrap); });
        QObject::connect(&projectThread, &QThread::started, projects, [=]() { projects->initialize(catalogRoot, true); });
        QObject::connect(projects, &ProjectsStore::ready, reagents, [=](QString) { reagents->initialize(catalogRoot); });
        QObject::connect(projects, &ProjectsStore::ready, quality, [=](QString) { quality->initialize(qualityRoot); });
        QObject::connect(quality, &QcStore::ready, quality, [=](QString path)
        { qualityDatabasePath = path; quality->bindCatalog(catalogRoot); });
        QObject::connect(quality, &QcStore::catalogBound, page, [this](QString)
        { bound = true; projectPage->reload(); });
        host.setWidget(window);
        host.setWidgetResizable(false);
        host.resize(1600, 900);
        host.show();
        for (const auto &entry : workers) entry.second->start();
    }
    ~ShellFixture()
    {
        for (const auto &entry : workers)
        {
            QMetaObject::invokeMethod(entry.first, "close", Qt::BlockingQueuedConnection);
            entry.second->quit();
            if (!entry.second->wait(5000)) qFatal("QC shell fixture worker failed to stop");
        }
        host.takeWidget();
        delete window;
    }
    bool ready() const
    {
        return until([this]()
        {
            return bound && page->isReady() && !page->isBusy() && projectPage->isReady() && !projectPage->isBusy()
                && reagentPage->isReady() && !reagentPage->isBusy()
                && window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
                && window->accountPage()->isReady() && !window->accountPage()->isBusy();
        });
    }
    void session(const QString &actor)
    { QMetaObject::invokeMethod(accounts, "setSession", Qt::QueuedConnection, Q_ARG(QString, actor), Q_ARG(bool, true)); }
    QDialog *manager() const { return control<QDialog>(page, "QcControlManager"); }
    bool add(const QString &name)
    {
        if (!manager() && !click(page, "btnAddControl")) return false;
        if (!until([this]() { return manager(); }) || !click(manager(), "btn_add_control_item")) return false;
        if (!until([this]() { return control<QDialog>(page, "QcControlAdd"); })) return false;
        auto *dialog = control<QDialog>(page, "QcControlAdd");
        type(control<QLineEdit>(dialog, "ldt_control_name"), name);
        type(control<QLineEdit>(dialog, "ldt_factor"), QStringLiteral("ANON_FACTOR"));
        if (!click(dialog, "btnSave")) return false;
        return until([this, name]()
        {
            for (const QcControl &entry : page->controls())
                if (entry.name == name && !page->isBusy() && !control<QDialog>(page, "QcControlAdd")) return true;
            return false;
        });
    }
    QThread hospitalThread, accountThread, projectThread, reagentThread, qualityThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    ProjectsStore *projects;
    ReagentStore *reagents;
    QcStore *quality;
    frmMain *window;
    ProjectPage *projectPage;
    ReagentsPage *reagentPage;
    QualityControlPage *page;
    QScrollArea host;
    QList<QPair<QObject *, QThread *>> workers;
    QString root, qualityDatabasePath;
    bool bound = false;
};

class TestLock
{
public:
    explicit TestLock(const QString &path)
    {
        name = QStringLiteral("QcShellTestLock-%1").arg(QUuid::createUuid().toString());
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        database.setDatabaseName(path);
        if (!database.open()) qFatal("QC shell anonymous lock database could not open");
    }
    ~TestLock()
    {
        database.rollback(); database.close(); database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    bool begin() { QSqlQuery query(database); return query.exec(QStringLiteral("BEGIN IMMEDIATE")); }
    bool release() { return database.rollback(); }
    QString name;
    QSqlDatabase database;
};
}

class QcLocalShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void menuRolesAndHiddenDraftGuards();
    void busyFailureRetryAndReopen();
    void actualProductOptionsAndNormalExit();
private:
    QString runtime, evidence, application;
};

void QcLocalShellTest::initTestCase()
{
    auto *warnings = new QTimer(this);
    connect(warnings, &QTimer::timeout, this, []()
    {
        for (QWidget *widget : QApplication::allWidgets())
        {
            auto *box = qobject_cast<CusMessageBox *>(widget);
            if (!box || box->objectName() != QStringLiteral("QcWarningMessage")
                || !box->isVisible() || !box->isReadyForInput()
                || box->property("testAcknowledged").toBool()) continue;
            box->setProperty("testAcknowledged", true);
            qInfo().noquote() << "QC_SHELL_WARNING_ACK" << box->getMsg();
            QTest::mouseClick(control<QPushButton>(box, "btnSure"), Qt::LeftButton);
            break;
        }
    });
    warnings->start(20);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
    runtime = qEnvironmentVariable("SN100_QC_TEST_ROOT");
    evidence = qEnvironmentVariable("SN100_QC_EVIDENCE");
    application = qEnvironmentVariable("SN100_QC_APP");
    QVERIFY(QDir::isAbsolutePath(runtime));
    QVERIFY(QDir::isAbsolutePath(evidence));
    QVERIFY(QDir().mkpath(runtime));
    QVERIFY(QDir().mkpath(evidence));
    QVERIFY(QFileInfo(application).isExecutable());
}

void QcLocalShellTest::menuRolesAndHiddenDraftGuards()
{
    QTemporaryDir directory(runtime + QStringLiteral("/shell_roles_XXXXXX"));
    ShellFixture fixture(directory.path());
    QVERIFY(fixture.ready());
    QVERIFY(fixture.window->selectMainPage(4));
    QCOMPARE(fixture.window->qualityControlPage(), fixture.page);
    QVERIFY(fixture.page->isVisible());
    auto *stack = control<QStackedWidget>(fixture.window, "stkCenterAdptive");
    auto *viewport = qobject_cast<QScrollArea *>(stack->widget(4));
    QVERIFY(viewport && viewport->widget() == fixture.page);
    const QStringList actors = {QString(), "offlineSend", "offlineTest", "offlineCheck", "offlineEngineer", "offlineAdmin"};
    for (int role = 0; role < actors.size(); ++role)
    {
        fixture.session(actors.at(role));
        QTRY_COMPARE(fixture.window->accountSession().role, role);
        QVERIFY(fixture.ready());
        QVERIFY(control<QPushButton>(fixture.page, "btnAddControl")->isEnabled());
        QVERIFY(fixture.window->accountSession().actionReady);
    }
    QVERIFY(fixture.add(QStringLiteral("QC_SHELL")));
    auto *manager = fixture.manager();
    type(control<QLineEdit>(manager, "ldtControlName"), QStringLiteral("UNSAVED"));
    answer(QMessageBox::Cancel);
    QVERIFY(!fixture.window->selectMainPage(0));
    QCOMPARE(fixture.window->currentMainPage(), 4);
    answer(QMessageBox::Cancel);
    QVERIFY(!fixture.window->requestLogout());
    answer(QMessageBox::Cancel);
    QVERIFY(!fixture.window->requestLogin());
    answer(QMessageBox::Cancel);
    QVERIFY(!fixture.window->close());
    answer(QMessageBox::Discard);
    QVERIFY(fixture.window->selectMainPage(0));
    QTRY_VERIFY(!fixture.page->isDirty() && !fixture.manager());
    // A hidden page can still own a draft through an existing child dialog.
    // The manual entry simulates that lifecycle state; global guards must scan it.
    fixture.page->openManager();
    QTRY_VERIFY(fixture.manager());
    type(control<QLineEdit>(fixture.manager(), "ldtFactor"), QStringLiteral("HIDDEN_DRAFT"));
    answer(QMessageBox::Cancel);
    QVERIFY(!fixture.window->requestLogout());
    answer(QMessageBox::Discard);
    QVERIFY(fixture.window->requestLogout());
    QTRY_VERIFY(!fixture.window->accountSession().authenticated && !fixture.page->isDirty());
    QVERIFY(fixture.window->selectMainPage(4));
    QVERIFY(fixture.host.grab().save(QDir(evidence).filePath(QStringLiteral("qc_shell_1600_top.png"))));
    fixture.host.resize(1920, 1080);
    QTest::qWait(50);
    QVERIFY(fixture.host.grab().save(QDir(evidence).filePath(QStringLiteral("qc_shell_1920.png"))));
    qInfo() << "QC_SHELL_ROLES" << "Guest,Send,Test,Check,Engineer,Admin" << "draft_cancel_discard_logout";
}

void QcLocalShellTest::busyFailureRetryAndReopen()
{
    QTemporaryDir directory(runtime + QStringLiteral("/shell_busy_XXXXXX"));
    {
        ShellFixture fixture(directory.path());
        QVERIFY(fixture.ready());
        fixture.session(QStringLiteral("offlineEngineer"));
        QTRY_COMPARE(fixture.window->accountSession().role, 4);
        QVERIFY(fixture.window->selectMainPage(4));
        QVERIFY(fixture.add(QStringLiteral("QC_BUSY")));
        auto *manager = fixture.manager();
        type(control<QLineEdit>(manager, "ldtControlName"), QStringLiteral("QC_RETRY"));
        TestLock lock(fixture.qualityDatabasePath);
        QVERIFY(lock.begin());
        QSignalSpy results(fixture.page, &QualityControlPage::operationFinished);
        QVERIFY(click(manager, "btn_save"));
        QVERIFY(fixture.page->isBusy());
        QVERIFY(!fixture.window->selectMainPage(0));
        QVERIFY(!fixture.window->selectUserPage(1));
        QVERIFY(!fixture.window->requestLogin());
        QVERIFY(!fixture.window->requestLogout());
        QVERIFY(!fixture.window->close());
        QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
        QCOMPARE(results.last().at(1).toBool(), false);
        QCOMPARE(control<QLineEdit>(manager, "ldtControlName")->text(), QStringLiteral("QC_RETRY"));
        QCOMPARE(fixture.page->controls().first().name, QStringLiteral("QC_BUSY"));
        QVERIFY(lock.release());
        results.clear();
        QVERIFY(click(manager, "btn_save"));
        QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
        QCOMPARE(results.last().at(1).toBool(), true);
        QCOMPARE(fixture.page->controls().first().name, QStringLiteral("QC_RETRY"));
        QVERIFY(click(manager, "btn_cancel"));
        QTRY_VERIFY(!fixture.manager());
        QVERIFY(fixture.window->close());
    }
    ShellFixture reopened(directory.path());
    QVERIFY(reopened.ready());
    QCOMPARE(reopened.page->controls().size(), 1);
    QCOMPARE(reopened.page->controls().first().name, QStringLiteral("QC_RETRY"));
}

void QcLocalShellTest::actualProductOptionsAndNormalExit()
{
    for (const QString &mode : {QStringLiteral("full"), QStringLiteral("legacy"), QStringLiteral("dependency"), QStringLiteral("invalid")})
    {
        QTemporaryDir directory(runtime + QStringLiteral("/product_") + mode + QStringLiteral("_XXXXXX"));
        QVERIFY(directory.isValid());
        QTemporaryFile secret(directory.filePath(QStringLiteral(".anonymous-bootstrap-XXXXXX")));
        QVERIFY(secret.open());
        const QByteArray password = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
        QCOMPARE(secret.write(password), qint64(password.size()));
        QVERIFY(secret.flush());
        QStringList args = {"--offline", "--accounts", "--seed-anonymous", "--test-secret-file", secret.fileName(),
                            "--data-dir", directory.path(), "--viewport", "1600x900"};
        if (mode != QStringLiteral("dependency")) args << "--catalog" << "--seed-catalog-demo";
        if (mode != QStringLiteral("legacy")) args << "--quality-control";
        if (mode == QStringLiteral("full"))
            args << "--history" << "--samples" << "--instrument" << "--password-login" << "--printer-settings" << "--lis-settings";
        if (mode == QStringLiteral("invalid"))
        {
            QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("quality_control"))));
            QFile sentinel(directory.filePath(QStringLiteral("quality_control/unmarked.txt")));
            QVERIFY(sentinel.open(QIODevice::WriteOnly));
            QCOMPARE(sentinel.write("preserve-anonymous-sentinel\n"), qint64(28));
        }
        QProcess product;
        product.setProcessChannelMode(QProcess::MergedChannels);
        product.start(application, args);
        QTRY_VERIFY(product.state() == QProcess::Running || product.state() == QProcess::NotRunning);
        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15000 && !output.contains("SHELL_READY") && product.state() != QProcess::NotRunning)
        {
            QTest::qWait(20);
            output += product.readAll();
        }
        if (mode == QStringLiteral("full") || mode == QStringLiteral("legacy"))
        {
            QVERIFY2(output.contains("SHELL_READY"), output.constData());
            product.terminate();
        }
        QTRY_COMPARE(product.state(), QProcess::NotRunning);
        output += product.readAll();
        QCOMPARE(product.exitStatus(), QProcess::NormalExit);
        const int expectedExit = mode == QStringLiteral("dependency") ? 2 : mode == QStringLiteral("invalid") ? 3 : 0;
        QCOMPARE(product.exitCode(), expectedExit);
        if (mode == QStringLiteral("full"))
        {
            QVERIFY(output.contains("QUALITY_CONTROL_READY"));
            QCOMPARE(output.count("QUALITY_CONTROL_STORE_CLOSED"), 1);
            QVERIFY(output.contains("THREADS_STOPPED true"));
            for (const QByteArray &store : {"HOSPITAL", "ACCOUNT", "HISTORY", "PROJECT", "REAGENT", "SAMPLE", "MAINTENANCE"})
                QCOMPARE(output.count(store + "_STORE_CLOSED"), 1);
        }
        if (mode == QStringLiteral("legacy")) QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("quality_control"))));
        if (mode == QStringLiteral("dependency"))
        {
            QVERIFY(!output.contains("SHELL_READY"));
            QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("quality_control"))));
        }
        if (mode == QStringLiteral("invalid"))
        {
            QVERIFY(!output.contains("SHELL_READY"));
            QFile sentinel(directory.filePath(QStringLiteral("quality_control/unmarked.txt")));
            QVERIFY(sentinel.open(QIODevice::ReadOnly));
            QCOMPARE(sentinel.readAll(), QByteArray("preserve-anonymous-sentinel\n"));
        }
        QFile log(QDir(evidence).filePath(mode + QStringLiteral("_product.log")));
        QVERIFY(log.open(QIODevice::WriteOnly));
        QCOMPARE(log.write(output), qint64(output.size()));
        QFile receipt(QDir(evidence).filePath(mode + QStringLiteral("_exit.txt")));
        QVERIFY(receipt.open(QIODevice::WriteOnly));
        receipt.write(QByteArray::number(product.exitCode()) + '\n');
    }
}

QTEST_MAIN(QcLocalShellTest)
#include "qc_local_shell_test.moc"
