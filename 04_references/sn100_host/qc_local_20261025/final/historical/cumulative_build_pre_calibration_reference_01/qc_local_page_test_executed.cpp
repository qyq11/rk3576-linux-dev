#include <QtTest>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>
#include "projects/projects_store.h"
#include "quality_control/quality_control_page.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"

namespace
{
bool until(const std::function<bool()> &condition, int timeout = 10000)
{
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < timeout) QTest::qWait(10);
    return condition();
}
template<class T> T *control(QObject *parent, const char *name)
{
    return parent ? parent->findChild<T *>(QString::fromLatin1(name)) : nullptr;
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
        if (!until([mask]() { return mask->isReadyForInput(); })) qFatal("QC dialog animation did not become ready");
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(edit, text);
}
QcProjectRefs projectChoices(const ProjectRecords &records, const ProjectUnits &units)
{
    QcProjectRefs choices;
    const QStringList sourceTypes = {"Original", "Calibration", "Rate", "INR"};
    for (const ProjectRecord &record : records)
    {
        QcProjectRef project;
        project.id = record.id;
        project.name = record.name;
        for (const ProjectResult &result : record.results)
        {
            const int index = sourceTypes.indexOf(result.type);
            if (index < 0) continue; // DEMO_A.Time is unknown to the original QC enum; never guess Original.
            QcResultRef value;
            value.type = index + 1;
            value.name = result.name;
            value.unit = result.unit;
            value.decimals = result.decimals;
            for (const ProjectUnit &unit : units) if (unit.name == value.unit) value.ratio = unit.ratio;
            project.results.append(value);
        }
        if (!project.results.isEmpty()) choices.append(project);
    }
    return choices;
}

// Each test connection is created, used and closed in this GUI test thread;
// no QSqlDatabase handle from either production worker crosses a thread.
class SqlConnection
{
public:
    explicit SqlConnection(const QString &path)
    {
        name = QStringLiteral("QcGuiTest-%1").arg(QUuid::createUuid().toString());
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=100"));
        if (!database.open()) qFatal("Anonymous QC test database could not open");
    }
    ~SqlConnection()
    {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(database);
        if (!query.exec(sql) || !query.next())
        {
            qWarning().noquote() << "QC_TEST_SQL_FAILED" << query.lastError().text();
            return QVariant();
        }
        return query.value(0);
    }
    bool execute(const QString &sql)
    {
        QSqlQuery query(database);
        const bool success = query.exec(sql);
        if (!success) qWarning().noquote() << query.lastError().text();
        return success;
    }
    QString name;
    QSqlDatabase database;
};

class PageFixture
{
public:
    explicit PageFixture(const QString &root)
        : projects(new ProjectsStore), store(new QcStore), page(new QualityControlPage(store)), root(root)
    {
        ProjectsStore::registerTypes();
        QcStore::registerTypes();
        projects->moveToThread(&projectThread);
        store->moveToThread(&worker);
        QObject::connect(&projectThread, &QThread::finished, projects, &QObject::deleteLater);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        const QString catalog = QDir(root).filePath(QStringLiteral("catalog"));
        const QString qc = QDir(root).filePath(QStringLiteral("quality_control"));
        QObject::connect(&projectThread, &QThread::started, projects, [=]() { projects->initialize(catalog, true); });
        QObject::connect(projects, &ProjectsStore::ready, projects, [this]() { projects->load(800); });
        QObject::connect(projects, &ProjectsStore::snapshotReady, page,
                         [this, qc](qint64 request, ProjectRecords records, ProjectUnits units)
        {
            if (request != 800) return;
            choices = projectChoices(records, units);
            QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection, Q_ARG(QString, qc));
        });
        QObject::connect(store, &QcStore::ready, store, [this, catalog](QString path)
        { databasePath = path; store->bindCatalog(catalog); });
        QObject::connect(store, &QcStore::catalogBound, page, [this](QString) { bound = true; });
        QObject::connect(store, &QcStore::error, page, [this](qint64, QString operation, QString message)
        { lastError = operation + QStringLiteral(":") + message; });
        page->setFixedSize(1751, 968);
        host.setWidget(page);
        host.setWidgetResizable(false);
        host.resize(1920, 1080);
        host.show();
        worker.start();
        projectThread.start();
    }
    ~PageFixture()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(projects, "close", Qt::BlockingQueuedConnection);
        worker.quit(); projectThread.quit();
        if (!worker.wait(5000) || !projectThread.wait(5000)) qFatal("QC page fixture workers did not stop");
        host.takeWidget();
        delete page;
    }
    bool ready()
    {
        if (!until([this]() { return page->isReady() && !page->isBusy() && bound; })) return false;
        if (!published)
        {
            QSignalSpy accepted(store, &QcStore::catalogReady);
            page->setProjects(choices);
            if (!until([&accepted]() { return !accepted.isEmpty(); })) return false;
            published = true;
        }
        return !choices.isEmpty();
    }
    QDialog *dialog(const char *name) const { return control<QDialog>(page, name); }
    bool openManager()
    {
        return click(page, "btnAddControl") && until([this]() { return dialog("QcControlManager"); });
    }
    bool add(const QString &name, const QString &manufacturer)
    {
        if (!dialog("QcControlManager") && !openManager()) return false;
        if (!click(dialog("QcControlManager"), "btn_add_control_item")) return false;
        if (!until([this]() { return dialog("QcControlAdd"); })) return false;
        auto *form = dialog("QcControlAdd");
        type(control<QLineEdit>(form, "ldt_control_name"), name);
        type(control<QLineEdit>(form, "ldt_factor"), manufacturer);
        if (!click(form, "btnSave")) return false;
        return until([this, name]()
        {
            for (const QcControl &entry : page->controls())
                if (entry.name == name && !page->isBusy() && !dialog("QcControlAdd")) return true;
            return false;
        });
    }
    QThread projectThread, worker;
    ProjectsStore *projects;
    QcStore *store;
    QualityControlPage *page;
    QScrollArea host;
    QString root, databasePath, lastError;
    QcProjectRefs choices;
    bool bound = false, published = false;
};

void answer(QMessageBox::StandardButton button)
{
    QTimer::singleShot(20, qApp, [button]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (auto *target = box->button(button)) QTest::mouseClick(target, Qt::LeftButton);
    });
}
}

class QcLocalPageTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void controlProjectsCancelAndReopen();
    void duplicateLockRecoveryAndDraft();
    void batchesDefaultNoopAndInvalidDate();
    void filteredControlsKeepStableId();
    void referenceCleanupWarningSurvivesReload();
private:
    QString runtime, evidence;
};

void QcLocalPageTest::initTestCase()
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
            qInfo().noquote() << "QC_GUI_WARNING_ACK" << box->getMsg();
            static int warningIndex = 0;
            const QString path = QDir(qEnvironmentVariable("SN100_QC_EVIDENCE")).filePath(
                QStringLiteral("qc_warning_%1.png").arg(++warningIndex, 2, 10, QLatin1Char('0')));
            if (!box->grab().save(path)) qFatal("Could not save actual QC warning view");
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
    QVERIFY(QDir::isAbsolutePath(runtime));
    QVERIFY(QDir::isAbsolutePath(evidence));
    QVERIFY(QDir().mkpath(runtime));
    QVERIFY(QDir().mkpath(evidence));
}

void QcLocalPageTest::controlProjectsCancelAndReopen()
{
    QTemporaryDir directory(runtime + QStringLiteral("/crud_XXXXXX"));
    QVERIFY(directory.isValid());
    qint64 stableControl = 0, stableProject = 0;
    {
        PageFixture fixture(directory.path());
        QVERIFY2(fixture.ready(), qPrintable(fixture.lastError));
        QVERIFY(control<QPushButton>(fixture.page, "btnPrint")->isHidden());
        QCOMPARE(control<QListWidget>(fixture.page, "lstControl")->count(), 0);
        QVERIFY(fixture.openManager());
        auto *manager = fixture.dialog("QcControlManager");
        QVERIFY(click(manager, "btn_add_control_item"));
        QTRY_VERIFY(fixture.dialog("QcControlAdd"));
        auto *add = fixture.dialog("QcControlAdd");
        QSignalSpy results(fixture.page, &QualityControlPage::operationFinished);
        QVERIFY(click(add, "btnSave"));
        QTRY_VERIFY(!results.isEmpty());
        QCOMPARE(results.last().at(1).toBool(), false);
        QCOMPARE(fixture.page->controls().size(), 0);
        type(control<QLineEdit>(add, "ldt_control_name"), "QC_A");
        type(control<QLineEdit>(add, "ldt_factor"), "ANON_FACTOR");
        QVERIFY(add->grab().save(QDir(evidence).filePath("qc_control_add_1920.png")));
        QVERIFY(click(add, "btnSave"));
        QTRY_COMPARE(fixture.page->controls().size(), 1);
        QTRY_VERIFY(!fixture.page->isBusy() && !fixture.dialog("QcControlAdd"));
        stableControl = fixture.page->controls().first().id;
        QVERIFY(stableControl > 0);
        type(control<QLineEdit>(manager, "ldtControlName"), "QC_RENAMED");
        type(control<QLineEdit>(manager, "ldtFactor"), "FACTOR_B");
        QVERIFY(click(manager, "btn_save"));
        QTRY_VERIFY(!fixture.page->isBusy());
        QCOMPARE(fixture.page->controls().first().name, QStringLiteral("QC_RENAMED"));
        SqlConnection db(fixture.databasePath);
        QCOMPARE(db.scalar("SELECT item_name FROM quality_control_list").toString(), QStringLiteral("QC_RENAMED"));
        for (const char *cancel : {"btn_close", "btnClose"})
        {
            QVERIFY(click(manager, "btn_add_control_project"));
            QTRY_VERIFY(fixture.dialog("QcProjectChooser"));
            QPointer<QDialog> chooser = fixture.dialog("QcProjectChooser");
            auto *list = control<QListWidget>(chooser, "listProjectWidget");
            QVERIFY(list && list->count() > 0);
            QTRY_VERIFY(control<CusDialogMask>(chooser, "mask")->isReadyForInput());
            QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
            QVERIFY(list->item(0)->isSelected());
            QVERIFY(click(chooser, cancel));
            QTRY_VERIFY(chooser.isNull());
            QCOMPARE(db.scalar("SELECT count(*) FROM quality_item_proj").toInt(), 0);
        }
        QVERIFY(click(manager, "btn_add_control_project"));
        QTRY_VERIFY(fixture.dialog("QcProjectChooser"));
        auto *chooser = fixture.dialog("QcProjectChooser");
        auto *list = control<QListWidget>(chooser, "listProjectWidget");
        QTRY_VERIFY(control<CusDialogMask>(chooser, "mask")->isReadyForInput());
        stableProject = list->item(0)->data(Qt::UserRole).toLongLong();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
        QVERIFY(click(chooser, "btnSave"));
        QTRY_VERIFY(!fixture.page->isBusy() && !fixture.dialog("QcProjectChooser"));
        QCOMPARE(db.scalar("SELECT project_id FROM quality_item_proj").toLongLong(), stableProject);
        QCOMPARE(db.scalar("SELECT count(*) FROM quality_rules").toInt(), 1);
        QCOMPARE(db.scalar("SELECT type FROM quality_rules").toInt(), 2);
        QCOMPARE(db.scalar("SELECT item_proj_id FROM quality_rules").toLongLong(), fixture.page->controls().first().projects.first().id);
        type(control<QLineEdit>(manager, "ldt_target_value"), "12.5000");
        type(control<QLineEdit>(manager, "ldt_sd_value"), "0.25");
        type(control<QLineEdit>(manager, "ldt_upper_limit"), "14");
        type(control<QLineEdit>(manager, "ldt_loweer_limit"), "11");
        type(control<QLineEdit>(manager, "ldt_upper_bound"), "15");
        type(control<QLineEdit>(manager, "ldt_lower_bound"), "10");
        results.clear();
        QVERIFY(click(manager, "btn_save"));
        QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
        QCOMPARE(results.last().at(1).toBool(), true);
        QCOMPARE(db.scalar("SELECT target FROM quality_rules").toString(), QStringLiteral("12.5000"));
        QCOMPARE(db.scalar("SELECT sd_value FROM quality_rules").toString(), QStringLiteral("0.25"));
        QCOMPARE(db.scalar("SELECT upper_limit||':'||lower_limit||':'||upper_bound||':'||lower_bound FROM quality_rules").toString(), QStringLiteral("14:11:15:10"));
        qInfo() << "QC_GUI_RULE_METADATA" << "12.5000" << "0.25" << "14:11:15:10" << "clinical_rules_disabled";
        QVERIFY(manager->grab().save(QDir(evidence).filePath("qc_manager_1920.png")));
        for (const char *cancel : {"btn_close", "btnClose"})
        {
            QVERIFY(click(manager, "btn_add_control_project"));
            QTRY_VERIFY(fixture.dialog("QcProjectChooser"));
            QPointer<QDialog> existing = fixture.dialog("QcProjectChooser");
            auto *savedList = control<QListWidget>(existing, "listProjectWidget");
            QTRY_VERIFY(control<CusDialogMask>(existing, "mask")->isReadyForInput());
            QVERIFY(savedList->item(0)->isSelected());
            if (QString::fromLatin1(cancel) == QStringLiteral("btn_close"))
                QVERIFY(existing->grab().save(QDir(evidence).filePath("qc_project_chooser_1920.png")));
            QTest::mouseClick(savedList->viewport(), Qt::LeftButton, Qt::NoModifier,
                              savedList->visualItemRect(savedList->item(0)).center());
            QVERIFY(!savedList->item(0)->isSelected());
            QVERIFY(click(existing, cancel));
            QTRY_VERIFY(existing.isNull());
            QCOMPARE(db.scalar("SELECT count(*) FROM quality_item_proj").toInt(), 1);
            QCOMPARE(db.scalar("SELECT count(*) FROM quality_rules").toInt(), 1);
        }
        QVERIFY(click(manager, "btn_cancel"));
        QTRY_VERIFY(!fixture.dialog("QcControlManager"));
        QVERIFY(fixture.page->close());
        qInfo() << "QC_GUI_COMMIT" << stableControl << stableProject << "QC_RENAMED" << "FACTOR_B";
    }
    PageFixture reopened(directory.path());
    QVERIFY2(reopened.ready(), qPrintable(reopened.lastError));
    QCOMPARE(reopened.page->controls().size(), 1);
    const QcControl saved = reopened.page->controls().first();
    QCOMPARE(saved.id, stableControl);
    QCOMPARE(saved.name, QStringLiteral("QC_RENAMED"));
    QCOMPARE(saved.manufacturer, QStringLiteral("FACTOR_B"));
    QCOMPARE(saved.projects.first().projectId, stableProject);
    QVERIFY(reopened.host.grab().save(QDir(evidence).filePath("qc_home_1920.png")));
}

void QcLocalPageTest::duplicateLockRecoveryAndDraft()
{
    QTemporaryDir directory(runtime + QStringLiteral("/failure_XXXXXX"));
    PageFixture fixture(directory.path());
    QVERIFY(fixture.ready());
    QVERIFY(fixture.add("QC_A", "FACTOR_A"));
    auto *manager = fixture.dialog("QcControlManager");
    QVERIFY(click(manager, "btn_add_control_item"));
    QTRY_VERIFY(fixture.dialog("QcControlAdd"));
    auto *add = fixture.dialog("QcControlAdd");
    type(control<QLineEdit>(add, "ldt_control_name"), "QC_A");
    type(control<QLineEdit>(add, "ldt_factor"), "FACTOR_DUP");
    QSignalSpy results(fixture.page, &QualityControlPage::operationFinished);
    QVERIFY(click(add, "btnSave"));
    QTRY_VERIFY(!results.isEmpty() && !fixture.page->isBusy());
    QCOMPARE(results.last().at(1).toBool(), false);
    QCOMPARE(control<QLineEdit>(add, "ldt_control_name")->text(), QStringLiteral("QC_A"));
    type(control<QLineEdit>(add, "ldt_control_name"), "QC_B");
    QVERIFY(click(add, "btnSave"));
    QTRY_COMPARE(fixture.page->controls().size(), 2);
    QTRY_VERIFY(!fixture.page->isBusy() && !fixture.dialog("QcControlAdd"));
    SqlConnection lock(fixture.databasePath);
    QVERIFY(lock.execute("BEGIN IMMEDIATE"));
    type(control<QLineEdit>(manager, "ldtControlName"), "QC_B_RETRY");
    results.clear();
    QVERIFY(click(manager, "btn_save"));
    QVERIFY(fixture.page->isBusy());
    QVERIFY(!fixture.page->requestLeave(QStringLiteral("test-busy")));
    QVERIFY(!fixture.page->close());
    QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
    QCOMPARE(results.last().at(1).toBool(), false);
    QCOMPARE(control<QLineEdit>(manager, "ldtControlName")->text(), QStringLiteral("QC_B_RETRY"));
    QCOMPARE(lock.scalar("SELECT count(*) FROM quality_control_list WHERE item_name='QC_B_RETRY'").toInt(), 0);
    QVERIFY(lock.execute("ROLLBACK"));
    results.clear();
    QVERIFY(click(manager, "btn_save"));
    QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
    QCOMPARE(results.last().at(1).toBool(), true);
    QCOMPARE(lock.scalar("SELECT count(*) FROM quality_control_list WHERE item_name='QC_B_RETRY'").toInt(), 1);
    type(control<QLineEdit>(manager, "ldtControlName"), "UNSAVED");
    QVERIFY(fixture.page->isDirty());
    answer(QMessageBox::Cancel);
    QVERIFY(!fixture.page->requestLeave(QStringLiteral("test-draft")));
    QCOMPARE(control<QLineEdit>(manager, "ldtControlName")->text(), QStringLiteral("UNSAVED"));
    answer(QMessageBox::Discard);
    QVERIFY(fixture.page->requestLeave(QStringLiteral("test-discard")));
    QTRY_VERIFY(!fixture.page->isDirty());
    QCOMPARE(lock.scalar("SELECT count(*) FROM quality_control_list WHERE item_name='UNSAVED'").toInt(), 0);
    qInfo() << "QC_GUI_LOCK_FAILURE_RETRY" << "unchanged_then_committed";
}

void QcLocalPageTest::batchesDefaultNoopAndInvalidDate()
{
    QTemporaryDir directory(runtime + QStringLiteral("/batch_XXXXXX"));
    PageFixture fixture(directory.path());
    QVERIFY(fixture.ready());
    QVERIFY(fixture.add("QC_BATCH", "FACTOR"));
    QVERIFY(click(fixture.dialog("QcControlManager"), "btn_cancel"));
    QTRY_VERIFY(!fixture.dialog("QcControlManager"));
    QVERIFY(click(fixture.page, "btnEditControl"));
    QTRY_VERIFY(fixture.dialog("QcBatchManager"));
    auto *batch = fixture.dialog("QcBatchManager");
    auto *name = control<QLineEdit>(batch, "ldtBatch");
    auto *date = control<QLineEdit>(batch, "ldtOutDate");
    SqlConnection db(fixture.databasePath);
    type(name, "BATCH_A"); type(date, "2026/02/30");
    QSignalSpy results(fixture.page, &QualityControlPage::operationFinished);
    QVERIFY(click(batch, "btn_add_item"));
    QTRY_VERIFY(!results.isEmpty());
    QCOMPARE(results.last().at(1).toBool(), false);
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch").toInt(), 0);
    type(date, "2027/01/31");
    QVERIFY(click(batch, "btn_add_item"));
    QTRY_VERIFY(!fixture.page->isBusy());
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch WHERE batch_name='BATCH_A' AND cur_batch=1 AND prv_batch=0").toInt(), 1);
    type(name, "BATCH_B"); type(date, "2027/02/28");
    QVERIFY(click(batch, "btn_add_item"));
    QTRY_VERIFY(!fixture.page->isBusy());
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch").toInt(), 2);
    auto *combo = control<QComboBox>(batch, "cbx_default_batch");
    QCOMPARE(combo->count(), 2);
    QSignalSpy activated(combo, QOverload<int>::of(&QComboBox::activated));
    QTest::mouseClick(combo, Qt::LeftButton);
    QTRY_VERIFY(combo->view()->isVisible());
    QTest::keyClick(combo->view(), Qt::Key_End);
    QTest::keyClick(combo->view(), Qt::Key_Return);
    QTRY_COMPARE(activated.count(), 1);
    QTRY_VERIFY(!fixture.page->isBusy());
    QTRY_COMPARE(db.scalar("SELECT batch_name FROM quality_control_batch WHERE cur_batch=1").toString(), QStringLiteral("BATCH_B"));
    QCOMPARE(db.scalar("SELECT batch_name FROM quality_control_batch WHERE prv_batch=1").toString(), QStringLiteral("BATCH_A"));
    const QVariant before = db.scalar("SELECT group_concat(id||':'||cur_batch||':'||prv_batch,';') FROM quality_control_batch ORDER BY id");
    results.clear();
    activated.clear();
    QTest::mouseClick(combo, Qt::LeftButton);
    QTRY_VERIFY(combo->view()->isVisible());
    QTest::keyClick(combo->view(), Qt::Key_Return);
    QTRY_COMPARE(activated.count(), 1);
    QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
    QCOMPARE(results.last().at(1).toBool(), true);
    QCOMPARE(db.scalar("SELECT group_concat(id||':'||cur_batch||':'||prv_batch,';') FROM quality_control_batch ORDER BY id"), before);
    auto *table = control<QTableWidget>(batch, "tab_quality_control_sns");
    QVERIFY(table->rowCount() == 2);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualItemRect(table->item(1, 1)).center());
    results.clear();
    QVERIFY(click(batch, "btn_delete"));
    QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
    QCOMPARE(results.last().at(1).toBool(), false); // Current/previous deletion must not trigger implicit default repair.
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch").toInt(), 2);
    // 原编辑框用于完整批次增改删；Ctrl+N是保留原布局的明确新增入口。
    batch->activateWindow();
    QTRY_VERIFY(batch->isActiveWindow());
    table->setFocus();
    QSignalSpy newBatch(control<QAction>(batch, "qcNewBatchAction"), &QAction::triggered);
    QTest::keyClick(table, Qt::Key_N, Qt::ControlModifier);
    QTRY_COMPARE(newBatch.count(), 1);
    QTRY_VERIFY(name->text().isEmpty() && date->text().isEmpty());
    type(name, "BATCH_C"); type(date, "2027/03/31");
    QVERIFY(click(batch, "btn_add_item"));
    QTRY_VERIFY(!fixture.page->isBusy());
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch").toInt(), 3);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualItemRect(table->item(2, 1)).center());
    type(name, "BATCH_C_EDIT"); type(date, "2027/04/30");
    QVERIFY(click(batch, "btn_add_item"));
    QTRY_VERIFY(!fixture.page->isBusy());
    QCOMPARE(db.scalar("SELECT expiration_date FROM quality_control_batch WHERE batch_name='BATCH_C_EDIT'").toString(), QStringLiteral("2027/04/30"));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier,
                      table->visualItemRect(table->item(2, 1)).center());
    QVERIFY(click(batch, "btn_delete"));
    QTRY_VERIFY(!fixture.page->isBusy());
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch").toInt(), 2);
    QVERIFY(batch->grab().save(QDir(evidence).filePath("qc_batch_1920.png")));
    QVERIFY(click(batch, "btnClose"));
    QTRY_VERIFY(!fixture.dialog("QcBatchManager"));
    QVERIFY(fixture.add("QC_OTHER", "FACTOR"));
    QVERIFY(click(fixture.dialog("QcControlManager"), "btn_cancel"));
    QTRY_VERIFY(!fixture.dialog("QcControlManager"));
    QVERIFY(click(fixture.page, "btnEditControl"));
    QTRY_VERIFY(fixture.dialog("QcBatchManager"));
    batch = fixture.dialog("QcBatchManager");
    type(control<QLineEdit>(batch, "ldtBatch"), "BATCH_A");
    type(control<QLineEdit>(batch, "ldtOutDate"), "2027/12/31");
    results.clear();
    QVERIFY(click(batch, "btn_add_item"));
    QTRY_VERIFY(!fixture.page->isBusy() && !results.isEmpty());
    QCOMPARE(results.last().at(1).toBool(), false);
    QCOMPARE(db.scalar("SELECT count(*) FROM quality_control_batch").toInt(), 2);
    QCOMPARE(control<QLineEdit>(batch, "ldtBatch")->text(), QStringLiteral("BATCH_A"));
    answer(QMessageBox::Discard);
    QVERIFY(click(batch, "btnClose"));
    qInfo() << "QC_GUI_BATCH_SWITCH" << "BATCH_A->BATCH_B" << "same_current_noop";
}

void QcLocalPageTest::filteredControlsKeepStableId()
{
    QTemporaryDir directory(runtime + QStringLiteral("/filtered_XXXXXX"));
    qint64 visibleId = 0;
    QString path;
    {
        PageFixture fixture(directory.path());
        QVERIFY(fixture.ready());
        QVERIFY(fixture.add("HIDDEN_FIRST", "FACTOR"));
        QVERIFY(fixture.add("VISIBLE_SECOND", "FACTOR"));
        for (const QcControl &entry : fixture.page->controls())
            if (entry.name == QStringLiteral("VISIBLE_SECOND")) visibleId = entry.id;
        path = fixture.databasePath;
    }
    {
        // Explicit anonymous fixture for an already persisted inactive original row.
        // ckxValid remains a read-only display; this is not a UI persistence claim.
        SqlConnection fixture(path);
        QVERIFY(fixture.execute("UPDATE quality_control_list SET is_used=0 WHERE item_name='HIDDEN_FIRST'"));
    }
    PageFixture reopened(directory.path());
    QVERIFY(reopened.ready());
    auto *list = control<QListWidget>(reopened.page, "lstControl");
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toLongLong(), visibleId);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
    QVERIFY(click(reopened.page, "btnEditControl"));
    QTRY_VERIFY(reopened.dialog("QcBatchManager"));
    QCOMPARE(control<QLineEdit>(reopened.dialog("QcBatchManager"), "ldtControlName")->text(), QStringLiteral("VISIBLE_SECOND"));
    QVERIFY(click(reopened.dialog("QcBatchManager"), "btnClose"));
    reopened.host.resize(1600, 900);
    QTest::qWait(50);
    QVERIFY(reopened.host.grab().save(QDir(evidence).filePath("qc_home_1600_top.png")));
    QVERIFY(reopened.openManager());
    auto *manager = reopened.dialog("QcControlManager");
    QTRY_VERIFY(control<CusDialogMask>(manager, "mask")->isReadyForInput());
    for (const char *name : {"btnClose", "btn_save", "btn_cancel", "btn_add_control_item", "btn_add_control_project"})
    {
        auto *button = control<QPushButton>(manager, name);
        QVERIFY(manager->rect().contains(button->mapTo(manager, button->rect().center())));
    }
    QVERIFY(manager->grab().save(QDir(evidence).filePath("qc_manager_1600.png")));
    QVERIFY(click(manager, "btn_cancel"));
    qInfo() << "QC_GUI_FILTERED_STABLE_ID" << visibleId << "VISIBLE_SECOND";
}

void QcLocalPageTest::referenceCleanupWarningSurvivesReload()
{
    QTemporaryDir directory(runtime + QStringLiteral("/reference_notice_XXXXXX"));
    QVERIFY(directory.isValid());
    PageFixture fixture(directory.path());
    QVERIFY2(fixture.ready(), qPrintable(fixture.lastError));
    QVERIFY(fixture.add("QC_REF_NOTICE", "FACTOR"));
    auto *manager=fixture.dialog("QcControlManager");
    QVERIFY(click(manager,"btn_add_control_project"));
    QTRY_VERIFY(fixture.dialog("QcProjectChooser"));
    auto *chooser=fixture.dialog("QcProjectChooser");
    auto *list=control<QListWidget>(chooser,"listProjectWidget");
    QTRY_VERIFY(control<CusDialogMask>(chooser,"mask")->isReadyForInput());
    QVERIFY(list && list->count()>0);
    QTest::mouseClick(list->viewport(),Qt::LeftButton,Qt::NoModifier,list->visualItemRect(list->item(0)).center());
    QVERIFY(click(chooser,"btnSave"));
    QTRY_VERIFY(!fixture.page->isBusy() && !fixture.dialog("QcProjectChooser"));
    SqlConnection catalog(QDir(directory.path()).filePath("catalog/catalog.db"));
    SqlConnection local(fixture.databasePath);
    QCOMPARE(catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='quality_control'").toInt(),1);
    // A real catalog trigger fails only cleanup after the local delete commits.
    // No emitted success/error signal is simulated by the test.
    QVERIFY(catalog.execute("CREATE TRIGGER qc_notice_cleanup BEFORE DELETE ON catalog_project_reference BEGIN SELECT RAISE(ABORT,'QC_NOTICE_CLEANUP_FAILURE'); END"));
    QSignalSpy finished(fixture.page,&QualityControlPage::operationFinished);
    QSignalSpy warnings(fixture.store,&QcStore::warning);
    QSignalSpy status(fixture.page,&QualityControlPage::statusChanged);
    QVERIFY(click(manager,"btn_delete"));
    QTRY_VERIFY(!fixture.page->isBusy() && !finished.isEmpty() && !warnings.isEmpty());
    QCOMPARE(finished.last().at(1).toBool(),true);
    QCOMPARE(local.scalar("SELECT count(*) FROM quality_control_list").toInt(),0);
    QCOMPARE(catalog.scalar("SELECT count(*) FROM catalog_project_reference WHERE owner_module='quality_control'").toInt(),1);
    QVERIFY(fixture.page->statusText().contains("QC_NOTICE_CLEANUP_FAILURE"));
    QVERIFY(fixture.page->statusText().contains(QStringLiteral("待核对项目引用")));
    QVERIFY(!status.isEmpty() && status.last().at(1).toBool());
    QVERIFY(click(manager,"btn_cancel"));
    qInfo()<<"QC_REFERENCE_NOTICE_RETAINED local_delete_committed=true conservative_hold=1";
}

QTEST_MAIN(QcLocalPageTest)
#include "qc_local_page_test.moc"
