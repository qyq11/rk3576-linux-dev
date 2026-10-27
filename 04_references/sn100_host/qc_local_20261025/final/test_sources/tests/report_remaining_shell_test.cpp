#include <QtTest>
#include <QApplication>
#include <QAction>
#include <QCalendarWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QPrintPreviewDialog>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <functional>
#include "data/account_store.h"
#include "data/hospital_store.h"
#include "history/history_page.h"
#include "history/report_controller.h"
#include "history/report_remaining_display_dialog.h"
#include "history/report_remaining_html.h"
#include "shell/frmmain.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

namespace
{
bool until(const std::function<bool()> &condition, int timeout = 15000)
{
    QElapsedTimer clock; clock.start();
    while (!condition() && clock.elapsed() < timeout) QTest::qWait(10);
    return condition();
}
template<class T> T *control(QObject *root, const char *name)
{ return root ? root->findChild<T *>(QString::fromLatin1(name)) : nullptr; }
void reveal(QWidget *widget)
{
    for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) scroll->ensureWidgetVisible(widget, 12, 12);
}
bool click(QObject *root, const char *name)
{
    auto *button = control<QPushButton>(root, name);
    if (!button || !button->isEnabled()) return false;
    reveal(button);
    if (!button->isVisible()) return false;
    QTest::mouseClick(button, Qt::LeftButton);
    return true;
}
bool activateForInput(QWidget *widget, bool focus = false)
{
    if (!widget || !widget->isVisible()) return false;
    QWidget *top = widget->window();
    QWidget *modal = QApplication::activeModalWidget();
    if (modal && modal->isVisible() && modal != top && !modal->isAncestorOf(widget)) return false;
    top->activateWindow();
    QApplication::setActiveWindow(top);
    if (focus) widget->setFocus(Qt::MouseFocusReason);
    return QApplication::activeWindow() == top;
}
bool clickPreviewAction(QPrintPreviewDialog *preview, QAction *action)
{
    if (!activateForInput(preview) || !action || !action->isEnabled())
    {
        qWarning() << "REPORT_HTML_INPUT_UNAVAILABLE" << preview << action << QApplication::activeModalWidget();
        return false;
    }
    QToolBar *toolbar = nullptr;
    QToolButton *button = nullptr;
    QToolButton *extension = nullptr;
    const auto directlyVisible = [](QWidget *widget, QToolBar *bar)
    {
        return widget && widget->isVisible() && widget->isEnabled()
            && widget->visibleRegion().contains(widget->rect().center())
            && bar->rect().contains(widget->mapTo(bar, widget->rect().center()));
    };
    // previewReady follows open(), before the toolbar necessarily has a layout.
    if (!until([&]()
    {
        for (auto *candidate : preview->findChildren<QToolBar *>())
            if (candidate->actions().contains(action)) { toolbar = candidate; break; }
        if (!toolbar) return false;
        button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
        extension = toolbar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
        return directlyVisible(button, toolbar) || directlyVisible(extension, toolbar);
    }, 5000))
    {
        qWarning() << "REPORT_HTML_LAYOUT_UNAVAILABLE" << toolbar << button << extension;
        if (toolbar) qWarning() << "REPORT_HTML_TOOLBAR" << toolbar->geometry() << toolbar->isVisible();
        if (button) qWarning() << "REPORT_HTML_BUTTON" << button->geometry() << button->isVisible() << button->isEnabled();
        if (extension) qWarning() << "REPORT_HTML_EXTENSION" << extension->geometry() << extension->isVisible() << extension->isEnabled();
        preview->grab().save(QDir(qEnvironmentVariable("SN100_REPORT_REMAINING_EVIDENCE"))
                            .filePath(QStringLiteral("html_toolbar_layout_failure.png")));
        return false;
    }
    QSignalSpy triggered(action, &QAction::triggered);
    if (directlyVisible(button, toolbar))
    {
        qInfo() << "REPORT_HTML_TOOLBAR_MOUSE direct";
        QTest::mouseClick(button, Qt::LeftButton);
        return !triggered.isEmpty();
    }
    // The extension may enter QMenu::exec during mousePress. Queue the real
    // menu click first so both popup implementations retain the mouse workflow.
    QElapsedTimer elapsed; elapsed.start();
    QPointer<QMenu> popup;
    QTimer menuClick;
    menuClick.setInterval(10);
    QObject::connect(&menuClick, &QTimer::timeout, &menuClick, [&]()
    {
        // A docked Qt5 toolbar may expand into another row instead of using
        // QMenu. It still exposes the original action's real tool button.
        if (directlyVisible(button, toolbar))
        {
            menuClick.stop();
            qInfo() << "REPORT_HTML_TOOLBAR_MOUSE expanded";
            QTest::mouseClick(button, Qt::LeftButton);
            return;
        }
        for (auto *candidate : QApplication::allWidgets())
            if (auto *menu = qobject_cast<QMenu *>(candidate))
                if (menu->isVisible() && menu->actions().contains(action)) { popup = menu; break; }
        if (popup && action->isEnabled())
        {
            const QRect area = popup->actionGeometry(action);
            if (area.isValid() && popup->rect().contains(area.center()))
            {
                menuClick.stop();
                qInfo() << "REPORT_HTML_TOOLBAR_MOUSE overflow";
                QTest::mouseMove(popup, area.center());
                QTest::mouseClick(popup, Qt::LeftButton, Qt::NoModifier, area.center());
                return;
            }
        }
        if (elapsed.elapsed() >= 5000)
        {
            menuClick.stop();
            if (popup) popup->close();
        }
    });
    menuClick.start();
    QTest::mouseClick(extension, Qt::LeftButton);
    const bool clicked = until([&]() { return !triggered.isEmpty() || elapsed.elapsed() >= 5000; }, 5000)
        && !triggered.isEmpty();
    menuClick.stop();
    if (!clicked)
    {
        qWarning() << "REPORT_HTML_OVERFLOW_UNAVAILABLE" << QApplication::activePopupWidget()
                   << extension->geometry() << extension->isVisible() << button->geometry() << button->isVisible();
        preview->grab().save(QDir(qEnvironmentVariable("SN100_REPORT_REMAINING_EVIDENCE"))
                            .filePath(QStringLiteral("html_toolbar_overflow_failure.png")));
        if (popup) popup->close();
    }
    return clicked;
}
void answer(QMessageBox::StandardButton response)
{
    QTimer::singleShot(20, qApp, [response]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (auto *button = box->button(response)) QTest::mouseClick(button, Qt::LeftButton);
    });
}
bool chooseFixtureDate(QDialog *dialog, const char *buttonName, const char *editName)
{
    auto *edit = control<QLineEdit>(dialog, editName);
    if (!edit) return false;
    if (edit->text() == QStringLiteral("2026/10/23")) return true;
    if (!click(dialog, buttonName)) return false;
    QCalendarWidget *calendar = nullptr;
    if (!until([&]()
    {
        for (auto *candidate : dialog->findChildren<QCalendarWidget *>())
            if (candidate->isVisible()) { calendar = candidate; return true; }
        return false;
    })) return false;
    calendar->setCurrentPage(2026, 10);
    auto *grid = control<QTableView>(calendar, "qt_calendar_calendarview");
    if (!grid) return false;
    for (int row = 1; row < grid->model()->rowCount(); ++row)
        for (int column = 0; column < grid->model()->columnCount(); ++column)
        {
            const QModelIndex index = grid->model()->index(row, column);
            if (index.data().toString() == QStringLiteral("23") && (index.flags() & Qt::ItemIsSelectable))
            {
                QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, grid->visualRect(index).center());
                return until([edit]() { return edit->text() == QStringLiteral("2026/10/23"); });
            }
        }
    return false;
}
class Database
{
public:
    explicit Database(const QString &path)
    {
        name = QStringLiteral("ReportShellInspection-%1").arg(QUuid::createUuid().toString());
        value = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        value.setDatabaseName(path);
        if (!value.open()) qFatal("Anonymous history inspection could not open");
    }
    ~Database() { value.close(); value = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(value);
        if (!query.exec(sql) || !query.next()) return {};
        return query.value(0);
    }
    QString name;
    QSqlDatabase value;
};
class Session
{
public:
    explicit Session(const QString &root, const QString &renderer)
        : hospital(new HospitalStore), accounts(new AccountStore), history(new HistoryStore), root(root)
    {
        HospitalStore::registerTypes(); AccountStore::registerTypes(); HistoryStore::registerTypes();
        workers = {{hospital, &hospitalThread}, {accounts, &accountThread}, {history, &historyThread}};
        for (const auto &worker : workers)
        {
            worker.first->moveToThread(worker.second);
            QObject::connect(worker.second, &QThread::finished, worker.first, &QObject::deleteLater);
        }
        window = new frmMain(hospital, nullptr, accounts);
        const QString hospitalRoot = QDir(root).filePath("hospital");
        const QString accountRoot = QDir(root).filePath("account");
        window->setDataDirectory(hospitalRoot);
        page = new HistoryPage(history); page->setDataDirectory(historyDirectory());
        window->attachHistoryPage(page);
        reportEnabled = window->enableHistoryReports(historyDirectory());
        if (reportEnabled) window->historyReports()->setRendererPath(renderer);
        window->enablePasswordLogin();
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        { hospital->initialize(QDir(hospitalRoot).filePath("hospital_Information.db"), QDir(hospitalRoot).filePath("config.ini")); });
        const QString bootstrap = QFileInfo::exists(QDir(accountRoot).filePath("user_Information.db"))
            ? QString() : QUuid::createUuid().toString(QUuid::WithoutBraces);
        QObject::connect(&accountThread, &QThread::started, accounts, [=]() { accounts->initialize(accountRoot, bootstrap); });
        QObject::connect(&historyThread, &QThread::started, history, [this]() { history->initialize(historyDirectory()); });
        host.setWidget(window); host.setWidgetResizable(false); host.resize(1600, 900); host.show();
        for (const auto &worker : workers) worker.second->start();
    }
    ~Session()
    {
        host.takeWidget(); delete window;
        for (const auto &worker : workers)
        {
            QMetaObject::invokeMethod(worker.first, "close", Qt::BlockingQueuedConnection);
            worker.second->quit();
            if (!worker.second->wait(5000)) qFatal("Report shell worker failed normal shutdown");
        }
        qInfo() << "REPORT_REMAINING_SHELL_THREADS_STOPPED";
    }
    bool ready()
    {
        return reportEnabled && until([this]() { return page->isReady() && !page->isBusy()
            && window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
            && window->accountPage()->isReady() && !window->accountPage()->isBusy(); });
    }
    void identity(const QString &actor, bool actionReady = true)
    { QMetaObject::invokeMethod(accounts, "setSession", Qt::QueuedConnection, Q_ARG(QString, actor), Q_ARG(bool, actionReady)); }
    bool queryFixture()
    {
        if (!window->selectMainPage(2) || !click(page, "btnQuery")) return false;
        auto *dialog = control<QDialog>(page, "MaskDlgQueryReport");
        if (!dialog || !chooseFixtureDate(dialog, "btnCalendarStart", "ldtStartDate")
            || !chooseFixtureDate(dialog, "btnCalendarEnd", "ldtEndDate") || !click(dialog, "btnSave")) return false;
        return until([this]() { return !page->isBusy() && control<QTableWidget>(page, "tableResultReportOne")->rowCount() > 0; });
    }
    ReportRemainingDisplayDialog *display() const { return page->findChild<ReportRemainingDisplayDialog *>(); }
    bool openDisplay()
    { return click(page, "btnDisplay") && until([this]() { return display() && display()->isVisible() && !page->isBusy(); }); }
    bool saveHospital()
    {
        if (!window->selectMainPage(6) || !window->selectUserPage(1)) return false;
        auto *edit = control<QLineEdit>(window->hospitalPage(), "ldtHospName");
        reveal(edit); QTest::mouseClick(edit, Qt::LeftButton); QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(edit, "ANON_REPORT_HOSPITAL");
        QSignalSpy operations(window->hospitalPage(), &frmHospInfoSetting::operationFinished);
        if (!click(window->hospitalPage(), "btnSave")) return false;
        return until([&]() { return !window->hospitalPage()->isBusy() && !operations.isEmpty(); });
    }
    bool report(HistoryReport *result)
    {
        QSignalSpy detail(history, &HistoryStore::detailReady);
        QMetaObject::invokeMethod(history, "detail", Qt::QueuedConnection, Q_ARG(qint64, qint64(990001)), Q_ARG(qint64, qint64(2)));
        if (!until([&detail]() { return !detail.isEmpty(); })) return false;
        *result = qvariant_cast<HistoryReport>(detail.last().at(1));
        return result->id == 2 && !result->results.isEmpty();
    }
    QString historyDirectory() const { return QDir(root).filePath("history"); }
    QThread hospitalThread, accountThread, historyThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    HistoryStore *history;
    frmMain *window;
    HistoryPage *page;
    QScrollArea host;
    QList<QPair<QObject *, QThread *>> workers;
    QString root;
    bool reportEnabled = false;
};
}

class ReportRemainingShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void sessionPermissionsAndDisplayDraft();
    void committedDisplayHideAndReopen();
    void htmlBusyFromReportAndBackgroundPage();
private:
    QString runtime, evidence, application, renderer;
};

void ReportRemainingShellTest::initTestCase()
{
    qRegisterMetaType<QPrintPreviewDialog *>("QPrintPreviewDialog*");
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
    runtime = qEnvironmentVariable("SN100_REPORT_REMAINING_TEST_ROOT");
    evidence = qEnvironmentVariable("SN100_REPORT_REMAINING_EVIDENCE");
    application = qEnvironmentVariable("SN100_REPORT_REMAINING_APP");
    renderer = QFileInfo(application).absoluteDir().filePath(QStringLiteral("sn100_report_renderer"));
    QVERIFY(QDir::isAbsolutePath(runtime)); QVERIFY(QDir::isAbsolutePath(evidence));
    QVERIFY(QDir().mkpath(runtime)); QVERIFY(QDir().mkpath(evidence));
    QVERIFY(QFileInfo(application).isExecutable()); QVERIFY(QFileInfo(renderer).isExecutable());
}

void ReportRemainingShellTest::sessionPermissionsAndDisplayDraft()
{
    QTemporaryDir directory(runtime + QStringLiteral("/roles_XXXXXX"));
    Session session(directory.path(), renderer);
    QVERIFY(session.ready()); QVERIFY(session.window->selectMainPage(2));
    const QStringList actors = {QString(), "offlineSend", "offlineTest", "offlineCheck", "offlineEngineer", "offlineAdmin"};
    for (int role = 0; role < actors.size(); ++role)
    {
        session.identity(actors.at(role), false);
        QTRY_COMPARE(session.window->accountSession().role, role);
        // Guest is also the initial role, so role==0 alone cannot acknowledge
        // the queued actionReady change from the actual account worker.
        QTRY_VERIFY(!session.window->accountSession().actionReady);
        QCOMPARE(session.window->accountSession().authenticated,role>0);
        QCOMPARE(session.window->accountSession().source,role>0 ? AccountSessionSource::Simulated : AccountSessionSource::Guest);
        QTRY_VERIFY(!session.window->accountPage()->isBusy());
        QCOMPARE(control<QPushButton>(session.page, "btnDel")->isEnabled(), role > 0);
        QCOMPARE(control<QPushButton>(session.page, "btnDisplay")->isEnabled(), role > 0);
        QVERIFY(!session.window->accountSession().actionReady);
        QSignalSpy denied(session.history, &HistoryStore::error), loaded(session.history, &HistoryStore::displayLoaded);
        if (role == 0)
        {
            QMetaObject::invokeMethod(session.history, "hideReports", Qt::QueuedConnection,
                Q_ARG(qint64, qint64(990002)), Q_ARG(HistoryIds, HistoryIds({2})));
            QTRY_VERIFY(!denied.isEmpty());
            QVERIFY(denied.last().last().toString().contains(QStringLiteral("Guest")));
        }
        else
        {
            QVERIFY(session.openDisplay());
            QVERIFY(click(session.display(), "btnCancel"));
            QTRY_VERIFY(!session.display());
        }
    }
    QVERIFY(session.openDisplay());
    auto *display = session.display();
    QVERIFY(click(display, "displayColumn_patient:age"));
    QVERIFY(session.page->hasUnsavedChanges());
    answer(QMessageBox::Cancel); QVERIFY(!session.window->selectMainPage(0));
    QCOMPARE(session.window->currentMainPage(), 2);
    answer(QMessageBox::Cancel); QVERIFY(!session.window->requestLogin());
    answer(QMessageBox::Cancel); QVERIFY(!session.window->requestLogout());
    answer(QMessageBox::Cancel); QVERIFY(!session.window->close());
    QVERIFY(session.page->hasUnsavedChanges());
    answer(QMessageBox::Discard); QVERIFY(session.window->requestLogout());
    QTRY_VERIFY(!session.window->accountSession().authenticated && !session.page->hasUnsavedChanges());
    QVERIFY(!control<QPushButton>(session.page, "btnDisplay")->isEnabled());
    Database db(QDir(session.historyDirectory()).filePath("history.sqlite"));
    QCOMPARE(db.scalar("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='report_view_preferences'").toInt(), 0);
    QCOMPARE(db.scalar("SELECT count(*) FROM report_data WHERE visible=1").toInt(), 5);
    qInfo() << "REPORT_SHELL_SESSION" << "Guest denied; five authenticated roles allowed with actionReady=false; Cancel preserved, Discard logout wrote no preferences";
}

void ReportRemainingShellTest::committedDisplayHideAndReopen()
{
    QTemporaryDir directory(runtime + QStringLiteral("/persist_XXXXXX"));
    {
        Session session(directory.path(), renderer);
        QVERIFY(session.ready()); session.identity(QStringLiteral("offlineEngineer"));
        QTRY_COMPARE(session.window->accountSession().role, 4);
        QVERIFY(session.queryFixture()); QVERIFY(session.openDisplay());
        QVERIFY(click(session.display(), "displayColumn_patient:age"));
        QPointer<ReportRemainingDisplayDialog> display = session.display();
        QVERIFY(click(display, "btnSave"));
        QTRY_VERIFY(!session.page->isBusy() && display.isNull());
        QVERIFY(control<QTableWidget>(session.page, "tablePatientInfo")->isColumnHidden(1));
        QVERIFY(!session.page->hasUnsavedChanges());
        Database db(QDir(session.historyDirectory()).filePath("history.sqlite"));
        QCOMPARE(db.scalar("SELECT visible FROM report_view_preferences WHERE column_key='patient:age'").toInt(), 0);
        const int results = db.scalar("SELECT count(*) FROM report_results").toInt();
        const int values = db.scalar("SELECT count(*) FROM report_values").toInt();
        const int before = db.scalar("SELECT count(*) FROM report_data WHERE visible=1").toInt();
        answer(QMessageBox::No); QVERIFY(click(session.page, "btnDel"));
        QCOMPARE(db.scalar("SELECT count(*) FROM report_data WHERE visible=1").toInt(), before);
        answer(QMessageBox::Yes); QVERIFY(click(session.page, "btnDel"));
        QTRY_VERIFY(!session.page->isBusy());
        QCOMPARE(db.scalar("SELECT count(*) FROM report_data WHERE visible=1").toInt(), before - 1);
        QCOMPARE(db.scalar("SELECT count(*) FROM report_results").toInt(), results);
        QCOMPARE(db.scalar("SELECT count(*) FROM report_values").toInt(), values);
        QVERIFY(session.window->close());
    }
    Session reopened(directory.path(), renderer);
    QVERIFY(reopened.ready()); QVERIFY(reopened.window->selectMainPage(2));
    QVERIFY(control<QTableWidget>(reopened.page, "tablePatientInfo")->isColumnHidden(1));
    QVERIFY(!control<QPushButton>(reopened.page, "btnDisplay")->isEnabled());
    Database db(QDir(reopened.historyDirectory()).filePath("history.sqlite"));
    QCOMPARE(db.scalar("SELECT count(*) FROM report_data WHERE visible=1").toInt(), 4);
    QVERIFY(reopened.host.grab().save(QDir(evidence).filePath("report_remaining_shell_1600.png")));
}

void ReportRemainingShellTest::htmlBusyFromReportAndBackgroundPage()
{
    QTemporaryDir directory(runtime + QStringLiteral("/html_XXXXXX"));
    Session session(directory.path(), renderer);
    QVERIFY(session.ready()); session.identity(QStringLiteral("offlineEngineer"));
    QTRY_COMPARE(session.window->accountSession().role, 4);
    QVERIFY(session.saveHospital());
    HistoryReport input; QVERIFY(session.report(&input));
    HistoryPrintOptions expectedOptions;
    HistoryPrintDocument expectedDocument;
    QString expectedError;
    QVERIFY(HistoryReportDocument::anonymousOptions(&expectedOptions,&expectedError));
    expectedOptions.hospitalName=QStringLiteral("ANON_REPORT_HOSPITAL");
    QVERIFY(HistoryReportDocument::build(input,expectedOptions,&expectedDocument,&expectedError));
    QCOMPARE(expectedDocument.rows.size(),1);
    QCOMPARE(expectedDocument.rows.first().value("project"),QStringLiteral("APTT"));
    QCOMPARE(expectedDocument.rows.first().value("unit"),QStringLiteral("s"));
    QVERIFY(expectedDocument.rows.first().value("project_result").startsWith(QStringLiteral("0.00")));
    auto *controller = session.window->historyReports(); QVERIFY(controller);
    for (bool background : {false, true})
    {
        QVERIFY(session.window->selectMainPage(background ? 6 : 2));
        if (background) QVERIFY(session.window->selectUserPage(1));
        const int originalMain = session.window->currentMainPage();
        const int originalUser = session.window->currentUserPage();
        const QString name = background ? QStringLiteral("background.html") : QStringLiteral("foreground.html");
        const QString output = QDir(session.historyDirectory()).filePath(QStringLiteral("exports/") + name);
        QSignalSpy complete(controller, &HistoryReportController::htmlExported), errors(controller, &HistoryReportController::failed);
        QPointer<QPrintPreviewDialog> preview;
        if (background)
        {
            // Public controller path also has to guard navigation globally.
            controller->exportHtml(input, output);
        }
        else
        {
            QSignalSpy opened(controller,&HistoryReportController::previewReady);
            controller->preview(input);
            QTRY_COMPARE_WITH_TIMEOUT(opened.size(),1,30000);
            preview=qvariant_cast<QPrintPreviewDialog *>(opened.first().first());
            QVERIFY(preview && preview->isVisible());
            auto *action=preview->findChild<QAction *>(QStringLiteral("actExpToHtml"));
            QVERIFY(action);
            QVERIFY(clickPreviewAction(preview,action));
            QPointer<QFileDialog> chooser=preview->findChild<QFileDialog *>(QStringLiteral("historyHtmlFileDialog"));
            QVERIFY(chooser && chooser->isVisible());
            QVERIFY(!session.window->selectMainPage(0));
            auto *buttons=chooser->findChild<QDialogButtonBox *>();
            QVERIFY(buttons && buttons->button(QDialogButtonBox::Cancel));
            QTest::mouseClick(buttons->button(QDialogButtonBox::Cancel),Qt::LeftButton);
            QTRY_VERIFY(chooser.isNull());
            QCOMPARE(complete.size(),0);
            QVERIFY(clickPreviewAction(preview,action));
            chooser=preview->findChild<QFileDialog *>(QStringLiteral("historyHtmlFileDialog"));
            QVERIFY(chooser && chooser->isVisible());
            auto *filename=chooser->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
            QVERIFY(filename);
            QVERIFY(activateForInput(filename,true));
            QTest::mouseClick(filename,Qt::LeftButton);
            QTest::keyClick(filename,Qt::Key_A,Qt::ControlModifier);
            QTest::keyClicks(filename,name);
            QCOMPARE(filename->text(),name);
            QSignalSpy selected(chooser,&QFileDialog::fileSelected);
            buttons=chooser->findChild<QDialogButtonBox *>();
            QVERIFY(buttons && buttons->button(QDialogButtonBox::Save));
            QTRY_VERIFY(buttons->button(QDialogButtonBox::Save)->isEnabled());
            QTest::mouseClick(buttons->button(QDialogButtonBox::Save),Qt::LeftButton);
            QTRY_COMPARE(selected.size(),1);
            QCOMPARE(selected.first().first().toString(),output);
            QTRY_VERIFY(chooser.isNull());
            // Closing the actual preview must leave the helper's busy guard on.
            QVERIFY(preview->close());
        }
        QVERIFY(session.page->isBusy());
        QVERIFY(!session.window->selectMainPage(0)); QVERIFY(!session.window->selectMainPage(2));
        QVERIFY(!session.window->selectUserPage(0));
        QVERIFY(!session.window->requestLogin()); QVERIFY(!session.window->requestLogout()); QVERIFY(!session.window->close());
        QCOMPARE(session.window->currentMainPage(), originalMain); QCOMPARE(session.window->currentUserPage(), originalUser);
        QTRY_VERIFY_WITH_TIMEOUT(!complete.isEmpty() || !errors.isEmpty(), 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.last().last().toString()));
        QCOMPARE(complete.size(), 1); QVERIFY(!session.page->isBusy());
        QCOMPARE(complete.first().first().toString(), output);
        QCOMPARE(complete.first().at(1).toInt(),expectedDocument.rows.size());
        QVERIFY(session.page->property("lastStatus").toString().contains(QStringLiteral("HTML已导出")));
        QVERIFY(session.page->property("lastStatus").toString().contains(output));
        QVERIFY(session.window->selectMainPage(2));
        QVERIFY(control<QLabel>(session.window, "lblDivState")->toolTip().contains(output));
        QFile html(output); QVERIFY(html.open(QIODevice::ReadOnly)); const QByteArray bytes = html.readAll();
        QString validation;
        QVERIFY2(validateHistoryHtml(bytes, &expectedDocument, &validation), qPrintable(validation));
        QTextDocument parsed; parsed.setHtml(QString::fromUtf8(bytes));
        QVERIFY(parsed.toPlainText().contains(QStringLiteral("ANON_REPORT_HOSPITAL")));
        QVERIFY(parsed.toPlainText().contains(input.patient.name));
        QVERIFY(parsed.toPlainText().contains(QStringLiteral("APTT")));
        QVERIFY(parsed.toPlainText().contains(QStringLiteral("0.00")));
        QVERIFY(parsed.toPlainText().contains(QStringLiteral("2026-10-23 00:00:00")));
        QVERIFY(QFile::copy(output, QDir(evidence).filePath(name)));
        qInfo().noquote() << "REPORT_SHELL_HTML_VERIFIED" << (background ? "background" : "foreground")
                         << complete.first().at(1).toInt() << output;
    }
    QVERIFY(session.window->close());
}

QTEST_MAIN(ReportRemainingShellTest)
#include "report_remaining_shell_test.moc"
