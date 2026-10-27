#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPointer>
#include <QPushButton>
#include <QQueue>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>
#include "calibration/calibration_page.h"
#include "calibration/calibrationchart.h"
#include "data/account_store.h"
#include "data/hospital_store.h"
#include "projects/project_page.h"
#include "quality_control/quality_control_page.h"
#include "reagents/reagents_page.h"
#include "shell/frmmain.h"
#include "ui/cusmessagebox.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

namespace
{
bool until(const std::function<bool()> &condition, int limit = 15000)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < limit) QTest::qWait(10);
    return condition();
}
template<class T> T *control(QObject *root, const char *name)
{
    return root ? root->findChild<T *>(QString::fromLatin1(name)) : nullptr;
}
void reveal(QWidget *widget)
{
    for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) scroll->ensureWidgetVisible(widget, 12, 12);
}
bool activateForInput(QWidget *widget, bool focus = false)
{
    if (!widget || !widget->isVisible()) return false;
    QWidget *top = widget->window();
    QWidget *modal = QApplication::activeModalWidget();
    if (modal && modal != top && !modal->isAncestorOf(widget)) return false;
    // The offscreen backend does not restore an active top-level automatically
    // after the native modal closes. Establish the harness window's real Qt
    // activation/focus before sending keyboard input; never bypass a modal.
    top->activateWindow();
    QApplication::setActiveWindow(top);
    if (focus) widget->setFocus(Qt::MouseFocusReason);
    return QApplication::activeWindow() == top;
}
bool click(QObject *root, const char *name)
{
    auto *button = control<QPushButton>(root, name);
    if (!button || !until([button]() { return button->isVisible() && button->isEnabled(); })) return false;
    reveal(button);
    if (!activateForInput(button)) return false;
    QTest::mouseClick(button, Qt::LeftButton);
    return true;
}
void type(QLineEdit *edit, const QString &text)
{
    reveal(edit);
    if (!activateForInput(edit, true)) { qWarning() << "CAL_TEXT_FOCUS_BLOCKED" << edit; return; }
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(edit, text);
}
bool editCell(QTableWidget *table, int row, int column, const QString &value)
{
    auto *item = table->item(row, column);
    if (!item || !(item->flags() & Qt::ItemIsEditable)) return false;
    table->scrollToItem(item);
    reveal(table);
    if (!activateForInput(table, true)) return false;
    const QRect cell = table->visualItemRect(item);
    if (!table->viewport()->rect().contains(cell.center())) return false;
    // QTest::mouseDClick alone does not supply the preceding selection press.
    // Select the real cell, then use the ordinary edit key and the active editor;
    // a hidden/deferred-delete editor from the prior cell is not this input.
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, cell.center());
    QTest::keyClick(table, Qt::Key_F2);
    QPointer<QLineEdit> editor;
    if (!until([table, &editor]()
    {
        editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
        return editor && editor->isVisible() && table->isAncestorOf(editor);
    }))
    {
        qWarning() << "CAL_CELL_EDITOR_MISSING" << row << column << "focus=" << QApplication::focusWidget();
        return false;
    }
    type(editor, value);
    QTest::keyClick(editor, Qt::Key_Return);
    const bool committed = until([item, value, editor]()
    { return item->text() == value && (!editor || !editor->isVisible() || QApplication::focusWidget() != editor); });
    if (!committed) qWarning() << "CAL_CELL_COMMIT_MISMATCH" << row << column << value << item->text();
    return committed;
}

class DialogAnswers : public QObject
{
public:
    DialogAnswers()
    {
        timer.setInterval(5);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            {
                if (pending.isEmpty()) return;
                auto *target = box->button(pending.head());
                if (!target) return;
                pending.dequeue();
                QTest::mouseClick(target, Qt::LeftButton);
                ++answered;
            }
            else if (auto *box = qobject_cast<CusMessageBox *>(QApplication::activeModalWidget()))
            {
                if (!box->isReadyForInput()) return;
                auto *cancel = control<QPushButton>(box, "btnCancel");
                const bool question = cancel && cancel->isVisible();
                if (question && pending.isEmpty()) return;
                const auto response = question ? pending.dequeue() : QMessageBox::Ok;
                auto *target = control<QPushButton>(box,
                    response == QMessageBox::Cancel || response == QMessageBox::No ? "btnCancel" : "btnSure");
                if (target) { QTest::mouseClick(target, Qt::LeftButton); ++answered; }
            }
        });
        timer.start();
    }
    void next(QMessageBox::StandardButton button) { pending.enqueue(button); }
    QTimer timer;
    QQueue<QMessageBox::StandardButton> pending;
    int answered = 0;
};

// Connections below belong only to this test thread, including the fixture
// input transaction and explicit competing locks. Worker handles never cross.
class Database
{
public:
    explicit Database(const QString &path)
    {
        name = QStringLiteral("CalibrationShellTest-%1").arg(QUuid::createUuid().toString());
        value = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        value.setDatabaseName(path);
        value.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=100"));
        if (!value.open()) qFatal("Could not open explicitly isolated calibration test database");
    }
    ~Database()
    {
        value.rollback(); value.close(); value = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
    }
    bool execute(const QString &sql, const QVariantList &values = {})
    {
        QSqlQuery query(value);
        if (!query.prepare(sql)) return false;
        for (const QVariant &argument : values) query.addBindValue(argument);
        const bool success = query.exec();
        if (!success) qWarning().noquote() << "CALIBRATION_TEST_SQL_ERROR" << query.lastError().text();
        return success;
    }
    QVariant scalar(const QString &sql, const QVariantList &values = {})
    {
        QSqlQuery query(value);
        if (!query.prepare(sql)) return {};
        for (const QVariant &argument : values) query.addBindValue(argument);
        if (!query.exec() || !query.next()) return {};
        return query.value(0);
    }
    QString name;
    QSqlDatabase value;
};

class CalibrationShellFixture
{
public:
    explicit CalibrationShellFixture(const QString &root)
        : hospital(new HospitalStore), accounts(new AccountStore), projects(new ProjectsStore),
          reagents(new ReagentStore), quality(new QcStore), calibration(new CalibrationStore), root(root)
    {
        HospitalStore::registerTypes(); AccountStore::registerTypes(); ProjectsStore::registerTypes();
        ReagentStore::registerTypes(); QcStore::registerTypes(); CalibrationStore::registerTypes();
        workers = {{hospital, &hospitalThread}, {accounts, &accountThread}, {projects, &projectThread},
                   {reagents, &reagentThread}, {quality, &qualityThread}, {calibration, &calibrationThread}};
        for (const auto &entry : workers)
        {
            entry.first->moveToThread(entry.second);
            QObject::connect(entry.second, &QThread::finished, entry.first, &QObject::deleteLater);
        }
        window = new frmMain(hospital, nullptr, accounts);
        projectPage = new ProjectPage(projects);
        reagentPage = new ReagentsPage(reagents);
        qualityPage = new QualityControlPage(quality);
        page = new CalibrationPage(calibration);
        window->attachCatalogPages(projectPage, reagentPage);
        window->attachQualityControlPage(qualityPage);
        window->attachCalibrationPage(page);
        window->enablePasswordLogin();
        const QString hospitalRoot = QDir(root).filePath(QStringLiteral("hospital"));
        const QString accountRoot = QDir(root).filePath(QStringLiteral("account"));
        catalogRoot = QDir(root).filePath(QStringLiteral("catalog"));
        const QString qualityRoot = QDir(root).filePath(QStringLiteral("quality_control"));
        localRoot = QDir(root).filePath(QStringLiteral("calibration"));
        window->setDataDirectory(hospitalRoot);
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        { hospital->initialize(QDir(hospitalRoot).filePath("hospital_Information.db"), QDir(hospitalRoot).filePath("config.ini")); });
        const QString secret = QFileInfo::exists(QDir(accountRoot).filePath("user_Information.db"))
            ? QString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        QObject::connect(&accountThread, &QThread::started, accounts, [=]() { accounts->initialize(accountRoot, secret); });
        QObject::connect(&projectThread, &QThread::started, projects, [this]() { projects->initialize(catalogRoot, true); });
        QObject::connect(projects, &ProjectsStore::ready, reagents, [this](QString) { reagents->initialize(catalogRoot); });
        QObject::connect(projects, &ProjectsStore::ready, quality, [this, qualityRoot](QString) { quality->initialize(qualityRoot); });
        QObject::connect(quality, &QcStore::ready, quality, [this](QString) { quality->bindCatalog(catalogRoot); });
        QObject::connect(quality, &QcStore::catalogBound, qualityPage, [this](QString) { qualityBound = true; });
        QObject::connect(calibration, &CalibrationStore::ready, page, [this](QString path) { localPath = path; });
        QObject::connect(calibration, &CalibrationStore::error, page,
                         [this](qint64, QString operation, QString message) { lastError = operation + ":" + message; });
        host.setWidget(window); host.setWidgetResizable(false); host.resize(1600, 900); host.show();
        for (const auto &entry : workers) entry.second->start();
    }
    ~CalibrationShellFixture()
    {
        for (const auto &entry : workers)
        {
            QMetaObject::invokeMethod(entry.first, "close", Qt::BlockingQueuedConnection);
            entry.second->quit();
            if (!entry.second->wait(5000)) qFatal("Calibration shell test worker did not stop normally");
        }
        host.takeWidget(); delete window;
        qInfo() << "CALIBRATION_SHELL_FIXTURE_THREADS_STOPPED";
    }
    void session(const QString &actor, bool ready = true)
    { QMetaObject::invokeMethod(accounts, "setSession", Qt::QueuedConnection, Q_ARG(QString, actor), Q_ARG(bool, ready)); }
    bool initialize()
    {
        if (!until([this]() { return qualityBound && qualityPage->isReady() && !qualityPage->isBusy()
            && projectPage->isReady() && !projectPage->isBusy() && reagentPage->isReady() && !reagentPage->isBusy()
            && window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
            && window->accountPage()->isReady() && !window->accountPage()->isBusy(); })) return false;
        session(QStringLiteral("offlineEngineer"));
        if (!until([this]() { return window->accountSession().role == 4 && !window->accountPage()->isBusy(); })) return false;
        for (const ProjectRecord &project : projectPage->projects())
            if (project.name == QStringLiteral("ANON_CAL")) projectId = project.id;
        if (!projectId)
        {
            // The official stores create every schema and copy the new stable
            // project ID. This narrowly scoped anonymous fixture input is NOT
            // a claim that the production page can edit result types. Root
            // explicitly authorized this test-only Time->Original row change
            // for the copied ANON_CAL ID; production Time remains unknown.
            QSignalSpy copied(projects, &ProjectsStore::succeeded);
            QSignalSpy copyError(projects, &ProjectsStore::error);
            if (projectPage->projects().isEmpty()) return false;
            QMetaObject::invokeMethod(projects, "copy", Qt::QueuedConnection,
                Q_ARG(qint64, qint64(881001)), Q_ARG(qint64, projectPage->projects().first().id), Q_ARG(QString, QStringLiteral("ANON_CAL")));
            if (!until([&]() { return !copied.isEmpty() || !copyError.isEmpty(); }) || !copyError.isEmpty()) return false;
            projectId = copied.last().at(2).toLongLong();
            Database input(catalogPath());
            if (!input.execute("BEGIN IMMEDIATE")) return false;
            bool success = input.execute("UPDATE project_result SET result_type=?,result_name=? WHERE project_id=? AND result_type=?",
                {QStringLiteral("Original"), QStringLiteral("ANON_RAW"), projectId, QStringLiteral("Time")});
            success = success && input.scalar("SELECT count(*) FROM project_result WHERE project_id=? AND result_type='Original' AND unit_name='s'", {projectId}).toInt() == 1;
            success = success && input.execute("UPDATE project_result SET result_name=? WHERE project_id=? AND result_type='Calibration'",
                {QStringLiteral("ANON_CONC"), projectId});
            if (!success || !input.execute("COMMIT")) return false;
            qInfo() << "ANONYMOUS_FIXTURE_INPUT" << "official schema/copy ID" << projectId
                    << "ANON_CAL Original=ANON_RAW/s Calibration=ANON_CONC/a.u.; no production type-edit or Time alias claim";
        }
        projectPage->reload();
        if (!until([this]()
        {
            if (projectPage->isBusy()) return false;
            for (const ProjectRecord &project : projectPage->projects())
                if (project.id == projectId)
                    for (const ProjectResult &result : project.results)
                        if (result.type == QStringLiteral("Original")) return true;
            return false;
        })) return false;
        for (const ReagentBatch &batch : reagentPage->snapshot().batches)
            if (batch.projectId == projectId && batch.code == QStringLiteral("ANON_BATCH")) batchId = batch.id;
        if (!batchId)
        {
            ReagentBatch input;
            input.projectId = projectId; input.code = QStringLiteral("ANON_BATCH"); input.batchGroup = QStringLiteral("ANON_R1|ANON_R2");
            QSignalSpy saved(reagents, &ReagentStore::succeeded), failed(reagents, &ReagentStore::error);
            QMetaObject::invokeMethod(reagents, "saveBatch", Qt::QueuedConnection, Q_ARG(ReagentBatch, input));
            if (!until([&]() { return !saved.isEmpty() || !failed.isEmpty(); }) || !failed.isEmpty()) return false;
            QSignalSpy loaded(reagents, &ReagentStore::loaded);
            QMetaObject::invokeMethod(reagents, "load", Qt::QueuedConnection);
            if (!until([&]() { return !loaded.isEmpty(); })) return false;
            const ReagentSnapshot snapshot = qvariant_cast<ReagentSnapshot>(loaded.last().first());
            for (const ReagentBatch &batch : snapshot.batches)
                if (batch.projectId == projectId && batch.code == QStringLiteral("ANON_BATCH")) batchId = batch.id;
        }
        if (!projectId || !batchId) return false;
        QMetaObject::invokeMethod(calibration, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, localRoot), Q_ARG(QString, catalogPath()));
        if (!until([this]() { return page->isReady() && !page->isBusy(); })) return false;
        if (!window->selectMainPage(3)) return false;
        auto *list = control<QListWidget>(page, "lstProject");
        for (int row = 0; row < list->count(); ++row)
            if (list->item(row)->data(Qt::UserRole).toLongLong() == projectId)
            {
                list->scrollToItem(list->item(row)); reveal(list);
                QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(row)).center());
                return true;
            }
        return false;
    }
    QString catalogPath() const { return QDir(catalogRoot).filePath(QStringLiteral("catalog.db")); }
    QTableWidget *points() const { return control<QTableWidget>(page, "tbCalibrationResult"); }
    bool newDraft(bool accept = true, bool checkModalGuards = false)
    {
        bool visited = false, completed = false, modalGuards = true;
        QTimer::singleShot(0, page, [this, accept, checkModalGuards, &visited, &completed, &modalGuards]()
        {
            auto *dialog = control<QDialog>(page, "calibrationNewDialog");
            completed = true;
            if (!dialog) return;
            visited = true;
            if (checkModalGuards)
            {
                const bool busy = page->isBusy();
                const bool mainDenied = !window->selectMainPage(4), userDenied = !window->selectUserPage(1);
                const bool loginDenied = !window->requestLogin(), logoutDenied = !window->requestLogout();
                const bool closeDenied = !window->close();
                modalGuards = busy && mainDenied && userDenied && loginDenied && logoutDenied && closeDenied;
                qInfo() << "CAL_NEW_MODAL_GUARDS" << busy << mainDenied << userDenied << loginDenied << logoutDenied << closeDenied;
                // Rejected navigation queues shell focus restoration. Let those
                // existing callbacks finish before a user resumes modal input.
                QTest::qWait(0);
                dialog->activateWindow();
            }
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            if (!accept)
            {
                QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
                return;
            }
            type(control<QLineEdit>(dialog, "localBloodBatch"), QStringLiteral("ANON_CALIBRATOR"));
            auto *count = control<QSpinBox>(dialog, "localPointCount");
            auto *repeats = control<QSpinBox>(dialog, "localRepeatCount");
            count->setFocus(); QTest::keyClick(count, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(count, "3");
            repeats->setFocus(); QTest::keyClick(repeats, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(repeats, "2");
            auto *batches = control<QComboBox>(dialog, "localBatch");
            const int index = batches->findData(batchId);
            if (index < 1) { dialog->reject(); modalGuards = false; return; }
            // Arrow-key selection on the focused closed combo is a complete
            // user action. Opening its popup then targeting keys at the combo
            // can leave a popup grab that consumes the later OK/table click.
            batches->setFocus(Qt::TabFocusReason);
            QTest::keyClick(batches, Qt::Key_Home);
            for (int row = 0; row < index; ++row) QTest::keyClick(batches, Qt::Key_Down);
            qInfo() << "CAL_NEW_INPUT" << control<QLineEdit>(dialog, "localBloodBatch")->text()
                    << "points=" << count->value() << "repeats=" << repeats->value()
                    << "batch=" << batches->currentData() << "expected=" << batchId;
            if (batches->currentData().toLongLong() != batchId) { dialog->reject(); modalGuards = false; return; }
            QTest::mouseClick(buttons->button(QDialogButtonBox::Ok), Qt::LeftButton);
        });
        auto *table = control<QTableWidget>(page, "tb_calibration_curve");
        reveal(table);
        if (!activateForInput(table, true)) return false;
        QTest::keyClick(table, Qt::Key_Insert);
        if (!until([&completed]() { return completed; })) return false;
        const bool actual = visited && modalGuards && (accept ? page->hasUnsavedChanges() : !page->hasUnsavedChanges());
        if (!actual) qWarning() << "CAL_NEW_RESULT" << accept << visited << modalGuards << page->hasUnsavedChanges()
                               << "rows=" << points()->rowCount();
        return actual;
    }
    bool fillPoints()
    {
        const QStringList concentrations = {"100.125", "50", "25"};
        const QStringList first = {"12.25", "18.5", "30"}, second = {"12.75", "20", "32"};
        for (int row = 0; row < 3; ++row)
            if (!editCell(points(), row, 1, QStringLiteral("ANON_RATE_%1").arg(row + 1))
                || !editCell(points(), row, 2, concentrations.at(row))
                || !editCell(points(), row, 3, first.at(row)) || !editCell(points(), row, 4, second.at(row))) return false;
        return true;
    }
    bool save(DialogAnswers &answers)
    {
        answers.next(QMessageBox::Save);
        if (!click(page, "btnSave")) return false;
        return until([this]() { return !page->isBusy() && !page->hasUnsavedChanges() && page->currentRecord().id > 0; });
    }
    QThread hospitalThread, accountThread, projectThread, reagentThread, qualityThread, calibrationThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    ProjectsStore *projects;
    ReagentStore *reagents;
    QcStore *quality;
    CalibrationStore *calibration;
    frmMain *window;
    ProjectPage *projectPage;
    ReagentsPage *reagentPage;
    QualityControlPage *qualityPage;
    CalibrationPage *page;
    QScrollArea host;
    QList<QPair<QObject *, QThread *>> workers;
    QString root, catalogRoot, localRoot, localPath, lastError;
    qint64 projectId = 0, batchId = 0;
    bool qualityBound = false;
};
}

class CalibrationShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void menuRolesAndGuardedDraft();
    void rawSaveCopyHideReferencesAndReopen();
    void catalogAndLocalLockFailuresRetainDraft();
    void actualProductFullLegacyInvalidAndDependency();
private:
    QString runtime, evidence, application;
};

void CalibrationShellTest::initTestCase()
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
    runtime = qEnvironmentVariable("SN100_CALIBRATION_TEST_ROOT");
    evidence = qEnvironmentVariable("SN100_CALIBRATION_EVIDENCE");
    application = qEnvironmentVariable("SN100_CALIBRATION_APP");
    QVERIFY(QDir::isAbsolutePath(runtime)); QVERIFY(QDir::isAbsolutePath(evidence));
    QVERIFY(QDir().mkpath(runtime)); QVERIFY(QDir().mkpath(evidence));
    QVERIFY(QFileInfo(application).isExecutable());
}

void CalibrationShellTest::menuRolesAndGuardedDraft()
{
    QTemporaryDir directory(runtime + QStringLiteral("/calibration_roles_XXXXXX"));
    CalibrationShellFixture fixture(directory.path());
    DialogAnswers answers;
    QVERIFY2(fixture.initialize(), qPrintable(fixture.lastError));
    QCOMPARE(fixture.window->currentMainPage(), 3);
    QCOMPARE(fixture.window->calibrationPage(), fixture.page);
    auto *stack = control<QStackedWidget>(fixture.window, "stkCenterAdptive");
    auto *viewport = qobject_cast<QScrollArea *>(stack->widget(3));
    QVERIFY(viewport && viewport->widget() == fixture.page);
    const QStringList actors = {QString(), "offlineSend", "offlineTest", "offlineCheck", "offlineEngineer", "offlineAdmin"};
    for (int role = 0; role < actors.size(); ++role)
    {
        fixture.session(actors.at(role));
        QTRY_COMPARE(fixture.window->accountSession().role, role);
        QTRY_VERIFY(!fixture.window->accountPage()->isBusy());
        QCOMPARE(control<QPushButton>(fixture.page, "btnSave")->isEnabled(), role > 0);
        QCOMPARE(control<QAction>(fixture.page, "calibrationNewRecord")->isEnabled(), role > 0);
        for (const char *name : {"btnCheck", "btnReCalculate", "btnShowChart", "btnCalculateIDSetting", "btnApply", "btn_use_curve_2"})
            QVERIFY2(!control<QPushButton>(fixture.page, name)->isEnabled(), name);
        QCOMPARE(control<QPushButton>(fixture.page, "btnCalculateIDSetting")->isVisible(), role >= 4);
    }
    fixture.session(QStringLiteral("offlineEngineer"), false);
    QTRY_VERIFY(!fixture.window->accountSession().actionReady);
    QVERIFY(!control<QPushButton>(fixture.page, "btnSave")->isEnabled());
    fixture.session(QStringLiteral("offlineEngineer"), true);
    QTRY_VERIFY(fixture.window->accountSession().actionReady);
    QVERIFY(fixture.newDraft(false, true));
    QVERIFY(fixture.newDraft(true, true));
    QVERIFY(fixture.fillPoints());
    answers.next(QMessageBox::Cancel);
    QVERIFY(!fixture.window->selectMainPage(4));
    QCOMPARE(fixture.window->currentMainPage(), 3);
    answers.next(QMessageBox::Cancel); QVERIFY(!fixture.window->requestLogin());
    answers.next(QMessageBox::Cancel); QVERIFY(!fixture.window->requestLogout());
    answers.next(QMessageBox::Cancel); QVERIFY(!fixture.window->close());
    QVERIFY(fixture.page->hasUnsavedChanges());
    answers.next(QMessageBox::Discard);
    QVERIFY(fixture.window->selectMainPage(4));
    QVERIFY(!fixture.page->hasUnsavedChanges());
    QVERIFY(fixture.window->selectMainPage(3));
    QVERIFY(fixture.newDraft()); QVERIFY(fixture.fillPoints());
    // Exercise a hidden draft as a lifecycle state without bypassing the
    // real shell's login/logout/close guards.
    stack->setCurrentIndex(4);
    answers.next(QMessageBox::Cancel); QVERIFY(!fixture.window->requestLogout());
    answers.next(QMessageBox::Discard); QVERIFY(fixture.window->requestLogout());
    QTRY_VERIFY(!fixture.window->accountSession().authenticated && !fixture.page->hasUnsavedChanges());
    Database db(fixture.localPath);
    QCOMPARE(db.scalar("SELECT count(*) FROM calibration_records").toInt(), 0);
    QVERIFY(fixture.window->selectMainPage(3));
    QVERIFY(fixture.host.grab().save(QDir(evidence).filePath("calibration_shell_1600_top.png")));
    fixture.host.resize(1920, 1080); QTest::qWait(50);
    QVERIFY(fixture.host.grab().save(QDir(evidence).filePath("calibration_shell_1920.png")));
    qInfo() << "CALIBRATION_SHELL_ROLES" << "Guest read-only; authenticated Send/Test/Check/Engineer/Admin local editing; no clinical action";
}

void CalibrationShellTest::rawSaveCopyHideReferencesAndReopen()
{
    QTemporaryDir directory(runtime + QStringLiteral("/calibration_persist_XXXXXX"));
    qint64 savedId = 0, copyId = 0, projectId = 0, batchId = 0;
    {
        CalibrationShellFixture fixture(directory.path()); DialogAnswers answers;
        QVERIFY2(fixture.initialize(), qPrintable(fixture.lastError));
        QVERIFY(fixture.newDraft()); QVERIFY(fixture.fillPoints()); QVERIFY(fixture.save(answers));
        savedId = fixture.page->currentRecord().id; projectId = fixture.projectId; batchId = fixture.batchId;
        const CalibrationRecord saved = fixture.page->currentRecord();
        QCOMPARE(saved.projectId, projectId); QCOMPARE(saved.batchId, batchId);
        QCOMPARE(saved.projectName, QStringLiteral("ANON_CAL"));
        QCOMPARE(saved.batchCode, QStringLiteral("ANON_BATCH"));
        QCOMPARE(saved.batchGroup, QStringLiteral("ANON_R1|ANON_R2"));
        QCOMPARE(saved.resultUnit, QStringLiteral("s")); QCOMPARE(saved.concentrationUnit, QStringLiteral("a.u."));
        QCOMPARE(saved.unitRatio, 1.0);
        QCOMPARE(saved.points.first().measurements, QList<double>({12.25, 12.75}));
        Database local(fixture.localPath), catalog(fixture.catalogPath());
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(), 3);
        QCOMPARE(local.scalar("SELECT sum(is_checked+is_applied) FROM calibration_records").toInt(), 0);
        QVERIFY(catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='calibration' AND project_id=?", {projectId}).toInt() >= 1);
        QVERIFY(catalog.scalar("SELECT count(*) FROM catalog_batch_reference WHERE owner_module='calibration' AND batch_id=?", {batchId}).toInt() >= 1);
        auto *chart = control<CalibrationChart>(fixture.page, "cuplotCalibration");
        QCOMPARE(chart->rawPoints().first(), QPointF(100.125, 12.5));
        answers.next(QMessageBox::Cancel); QVERIFY(click(fixture.page, "btnCopy"));
        QCOMPARE(fixture.page->records().size(), 1);
        answers.next(QMessageBox::Yes); QVERIFY(click(fixture.page, "btnCopy"));
        QTRY_VERIFY(!fixture.page->isBusy() && fixture.page->records().size() == 2);
        copyId = fixture.page->currentRecord().id;
        QVERIFY(copyId != savedId); QCOMPARE(fixture.page->currentRecord().sourceId, savedId);
        answers.next(QMessageBox::Yes); QVERIFY(click(fixture.page, "btnDelLine"));
        QTRY_VERIFY(!fixture.page->isBusy() && fixture.page->records().size() == 1);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 2);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records WHERE visible=0").toInt(), 1);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(), 6);
        QCOMPARE(local.scalar("SELECT sum(is_checked+is_applied) FROM calibration_records").toInt(), 0);
        QVERIFY(catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='calibration' AND project_id=?", {projectId}).toInt() >= 1);
        QVERIFY(catalog.scalar("SELECT count(*) FROM catalog_batch_reference WHERE owner_module='calibration' AND batch_id=?", {batchId}).toInt() >= 1);
        ProjectRecord renamed;
        for (const ProjectRecord &project : fixture.projectPage->projects()) if (project.id == projectId) renamed = project;
        renamed.name = QStringLiteral("MUST_NOT_RENAME");
        QSignalSpy projectErrors(fixture.projects, &ProjectsStore::error);
        QMetaObject::invokeMethod(fixture.projects, "save", Qt::QueuedConnection,
            Q_ARG(qint64, qint64(882001)), Q_ARG(ProjectRecord, renamed));
        QTRY_VERIFY(!projectErrors.isEmpty());
        QCOMPARE(catalog.scalar("SELECT project_en_name FROM project_base WHERE id=?", {projectId}).toString(), QStringLiteral("ANON_CAL"));
        projectErrors.clear();
        QMetaObject::invokeMethod(fixture.projects, "remove", Qt::QueuedConnection,
            Q_ARG(qint64, qint64(882002)), Q_ARG(qint64, projectId));
        QTRY_VERIFY(!projectErrors.isEmpty());
        QSignalSpy batchErrors(fixture.reagents, &ReagentStore::error);
        QMetaObject::invokeMethod(fixture.reagents, "removeBatch", Qt::QueuedConnection, Q_ARG(qint64, batchId));
        QTRY_VERIFY(!batchErrors.isEmpty());
        QCOMPARE(catalog.scalar("SELECT count(*) FROM reagent_batch_group WHERE id=?", {batchId}).toInt(), 1);
        QCOMPARE(fixture.page->currentRecord().points.first().measurements.first(), 12.25);
        QVERIFY(fixture.window->close());
    }
    CalibrationShellFixture reopened(directory.path());
    QVERIFY2(reopened.initialize(), qPrintable(reopened.lastError));
    QCOMPARE(reopened.page->records().size(), 1);
    QCOMPARE(reopened.page->currentRecord().id, savedId);
    QCOMPARE(reopened.page->currentRecord().projectId, projectId);
    QCOMPARE(reopened.page->currentRecord().batchId, batchId);
    QCOMPARE(reopened.page->currentRecord().points.at(1).measurements.first(), 18.5);
    Database local(reopened.localPath), catalog(reopened.catalogPath());
    QCOMPARE(local.scalar("SELECT visible FROM calibration_records WHERE id=?", {copyId}).toInt(), 0);
    QVERIFY(catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='calibration' AND project_id=?", {projectId}).toInt() >= 1);
    QVERIFY(catalog.scalar("SELECT count(*) FROM catalog_batch_reference WHERE owner_module='calibration' AND batch_id=?", {batchId}).toInt() >= 1);
    qInfo() << "CALIBRATION_SHELL_STABLE_REFERENCES" << projectId << batchId << savedId << copyId
            << "saved/raw/copy/soft-hidden/reopened; held references retained; no audit or application";
}

void CalibrationShellTest::catalogAndLocalLockFailuresRetainDraft()
{
    QTemporaryDir directory(runtime + QStringLiteral("/calibration_locks_XXXXXX"));
    CalibrationShellFixture fixture(directory.path()); DialogAnswers answers;
    QVERIFY2(fixture.initialize(), qPrintable(fixture.lastError));
    QVERIFY(fixture.newDraft()); QVERIFY(fixture.fillPoints());
    Database local(fixture.localPath), catalog(fixture.catalogPath());
    QVERIFY(catalog.execute("BEGIN IMMEDIATE"));
    QSignalSpy operations(fixture.page, &CalibrationPage::operationFinished);
    answers.next(QMessageBox::Save); QVERIFY(click(fixture.page, "btnSave"));
    QVERIFY(fixture.page->isBusy());
    QVERIFY(!fixture.window->selectMainPage(4)); QVERIFY(!fixture.window->selectUserPage(1));
    QVERIFY(!fixture.window->requestLogin()); QVERIFY(!fixture.window->requestLogout()); QVERIFY(!fixture.window->close());
    QTRY_VERIFY(!fixture.page->isBusy() && !operations.isEmpty());
    QCOMPARE(operations.last().at(1).toBool(), false);
    QVERIFY(fixture.page->hasUnsavedChanges());
    QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 0);
    QCOMPARE(fixture.points()->item(0, 3)->text(), QStringLiteral("12.25"));
    QVERIFY(catalog.execute("ROLLBACK"));
    QVERIFY(fixture.save(answers));
    const qint64 revision = fixture.page->currentRecord().revision;
    QVERIFY(editCell(fixture.points(), 0, 3, QStringLiteral("40")));
    QVERIFY(local.execute("BEGIN IMMEDIATE"));
    operations.clear(); answers.next(QMessageBox::Save); QVERIFY(click(fixture.page, "btnSave"));
    QVERIFY(fixture.page->isBusy());
    QVERIFY(!fixture.window->selectMainPage(4)); QVERIFY(!fixture.window->requestLogout());
    QTRY_VERIFY(!fixture.page->isBusy() && !operations.isEmpty());
    QCOMPARE(operations.last().at(1).toBool(), false);
    QVERIFY(fixture.page->hasUnsavedChanges());
    QCOMPARE(fixture.points()->item(0, 3)->text(), QStringLiteral("40"));
    QCOMPARE(local.scalar("SELECT revision FROM calibration_records WHERE visible=1").toLongLong(), revision);
    const QJsonArray old = QJsonDocument::fromJson(local.scalar("SELECT measurements FROM calibration_points WHERE point_index=1").toByteArray()).array();
    QCOMPARE(old.first().toDouble(), 12.25);
    QVERIFY(local.execute("ROLLBACK")); QVERIFY(fixture.save(answers));
    QCOMPARE(fixture.page->currentRecord().revision, revision + 1);
    QCOMPARE(fixture.page->currentRecord().points.first().measurements.first(), 40.0);
    qInfo() << "CALIBRATION_SHELL_LOCK_RECOVERY" << "catalog reservation failed before first local commit; local write failed retaining draft40; explicit retries committed";
}

void CalibrationShellTest::actualProductFullLegacyInvalidAndDependency()
{
    for (const QString &mode : {QStringLiteral("full"), QStringLiteral("legacy"), QStringLiteral("invalid"), QStringLiteral("dependency")})
    {
        QTemporaryDir directory(runtime + QStringLiteral("/calibration_product_") + mode + QStringLiteral("_XXXXXX"));
        QVERIFY(directory.isValid());
        QTemporaryFile secret(directory.filePath(QStringLiteral(".anonymous-bootstrap-XXXXXX")));
        QVERIFY(secret.open());
        const QByteArray password = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
        QCOMPARE(secret.write(password), qint64(password.size())); QVERIFY(secret.flush());
        QStringList args = {"--offline", "--accounts", "--seed-anonymous", "--test-secret-file", secret.fileName(),
                            "--data-dir", directory.path(), "--viewport", "1600x900"};
        if (mode != QStringLiteral("dependency")) args << "--catalog" << "--seed-catalog-demo" << "--quality-control";
        if (mode != QStringLiteral("legacy")) args << "--calibration";
        if (mode == QStringLiteral("full"))
            args << "--history" << "--samples" << "--instrument" << "--password-login" << "--printer-settings" << "--lis-settings";
        const QByteArray invalidBytes("anonymous-invalid-calibration-database\n");
        if (mode == QStringLiteral("invalid"))
        {
            QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("calibration"))));
            QFile invalid(directory.filePath(QStringLiteral("calibration/calibration_local.db")));
            QVERIFY(invalid.open(QIODevice::WriteOnly)); QCOMPARE(invalid.write(invalidBytes), qint64(invalidBytes.size()));
        }
        QProcess product; product.setProcessChannelMode(QProcess::MergedChannels); product.start(application, args);
        QTRY_VERIFY(product.state() == QProcess::Running || product.state() == QProcess::NotRunning);
        QByteArray output; QElapsedTimer clock; clock.start();
        while (clock.elapsed() < 20000 && !output.contains("SHELL_READY") && product.state() != QProcess::NotRunning)
        { QTest::qWait(20); output += product.readAll(); }
        if (mode == QStringLiteral("full") || mode == QStringLiteral("legacy"))
        { QVERIFY2(output.contains("SHELL_READY"), output.constData()); product.terminate(); }
        QTRY_COMPARE(product.state(), QProcess::NotRunning); output += product.readAll();
        QCOMPARE(product.exitStatus(), QProcess::NormalExit);
        const int expected = mode == QStringLiteral("dependency") ? 2 : mode == QStringLiteral("invalid") ? 3 : 0;
        QCOMPARE(product.exitCode(), expected);
        if (mode == QStringLiteral("full"))
        {
            QVERIFY(output.contains("CALIBRATION_READY")); QVERIFY(output.contains("QUALITY_CONTROL_READY"));
            QCOMPARE(output.count("CALIBRATION_STORE_CLOSED"), 1); QCOMPARE(output.count("QUALITY_CONTROL_STORE_CLOSED"), 1);
            QVERIFY(output.contains("THREADS_STOPPED true"));
            for (const QByteArray &store : {"HOSPITAL", "ACCOUNT", "HISTORY", "PROJECT", "REAGENT", "SAMPLE", "MAINTENANCE"})
                QCOMPARE(output.count(store + "_STORE_CLOSED"), 1);
        }
        if (mode == QStringLiteral("legacy")) QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("calibration"))));
        if (mode == QStringLiteral("dependency"))
        { QVERIFY(!output.contains("SHELL_READY")); QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("calibration")))); }
        if (mode == QStringLiteral("invalid"))
        {
            QVERIFY(!output.contains("SHELL_READY")); QFile invalid(directory.filePath(QStringLiteral("calibration/calibration_local.db")));
            QVERIFY(invalid.open(QIODevice::ReadOnly)); QCOMPARE(invalid.readAll(), invalidBytes);
        }
        QFile log(QDir(evidence).filePath(mode + QStringLiteral("_product.log")));
        QVERIFY(log.open(QIODevice::WriteOnly)); QCOMPARE(log.write(output), qint64(output.size()));
        QFile receipt(QDir(evidence).filePath(mode + QStringLiteral("_exit.txt")));
        QVERIFY(receipt.open(QIODevice::WriteOnly)); receipt.write(QByteArray::number(product.exitCode()) + '\n');
    }
}

QTEST_MAIN(CalibrationShellTest)
#include "calibration_shell_test.moc"
