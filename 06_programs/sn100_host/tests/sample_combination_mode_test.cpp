#include "sample_page.h"
#include "sample_widgets.h"
#include "cuscombox_base.h"
#include "cusdialogmask.h"
#include <QApplication>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionButton>
#include <QCheckBox>
#include <QDataStream>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QVariant>
#include <QWindow>
#include <QtTest>
#include <cstdio>
#include <functional>

namespace
{
bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 3000) QTest::qWait(5);
    return condition();
}

// 所有SQL仅打开真实owner在本次新匿名目录内已创建的文件。
class Sql
{
  public:
    explicit Sql(const QString &path)
    {
        if (!QFileInfo(path).isFile()) return;
        connection_ = QUuid::createUuid().toString();
        database_ = QSqlDatabase::addDatabase("QSQLITE", connection_);
        database_.setDatabaseName(path);
        database_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=200");
        if (database_.open()) exec("PRAGMA foreign_keys=ON");
    }
    ~Sql()
    {
        if (connection_.isEmpty()) return;
        database_.close(); database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_);
    }
    bool exec(const QString &sql)
    {
        QSqlQuery query(database_);
        const bool ok = database_.isOpen() && query.exec(sql);
        if (!ok) qWarning() << "ANONYMOUS_SQL" << query.lastError().text();
        return ok;
    }
    QByteArray image(const QStringList &statements)
    {
        if (!database_.isOpen()) return {};
        QByteArray bytes;
        QDataStream stream(&bytes, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_5_12);
        for (const auto &statement : statements)
        {
            QSqlQuery query(database_);
            if (!query.exec(statement)) return {};
            QList<QVariantList> rows;
            while (query.next())
            {
                QVariantList row;
                for (int column = 0; column < query.record().count(); ++column) row.append(query.value(column));
                rows.append(row);
            }
            if (query.lastError().isValid()) return {};
            stream << rows;
        }
        return stream.status() == QDataStream::Ok ? bytes : QByteArray();
    }
  private:
    QString connection_;
    QSqlDatabase database_;
};

QByteArray rackValue(const SampleRack &rack)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << rack.id << rack.name << rack.emergency << rack.holes.size();
    for (const auto &hole : rack.holes)
    {
        const auto &quality = hole.quality;
        const auto &selection = quality.selected;
        stream << hole.number << hole.sampleId << hole.container << hole.revise << hole.sampleType
               << selection.datasetId << selection.controlId << selection.batchId << selection.controlName
               << selection.batchName << selection.sampleId << quality.slotToken << quality.changeToken
               << selection.projects.size();
        for (const auto &reference : selection.projects) stream << reference.associationId << reference.projectId;
        stream << hole.tasks.size();
        for (const auto &task : hole.tasks)
        {
            stream << task.projectId << task.rate << task.repeats << task.prolonged;
            const auto states = sampleTaskStates(task);
            stream << states.size();
            for (auto state : states) stream << static_cast<int>(state);
        }
    }
    return stream.status() == QDataStream::Ok ? bytes : QByteArray();
}

class Harness
{
  public:
    Harness() : directory(QDir(qEnvironmentVariable("SN100_COMBINATION_TEST_ROOT")).filePath("session-XXXXXX"))
    {
        directory.setAutoRemove(false);
        ProjectsStore::registerTypes(); SampleStore::registerTypes();
        projects = new ProjectsStore; samples = new SampleStore;
        projects->moveToThread(&projectThread); samples->moveToThread(&sampleThread);
        QObject::connect(&projectThread, &QThread::finished, projects, &QObject::deleteLater);
        QObject::connect(&sampleThread, &QThread::finished, samples, &QObject::deleteLater);
        // 跨worker只排队完整值，主测试线程独占这些容器。
        QObject::connect(projects, &ProjectsStore::ready, &receiver, [this]() { projectReady = true; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::snapshotReady, &receiver,
                         [this](qint64 id, ProjectRecords value, ProjectUnits) { projectSnapshots[id] = value; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::succeeded, &receiver,
                         [this](qint64 id, QString, qint64 value) { projectSaves[id] = value; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::error, &receiver,
                         [this](qint64 id, QString, QString value) { projectErrors[id] = value; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::closed, &receiver, [this]() { projectClosed = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::ready, &receiver, [this]() { sampleReady = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::snapshotReady, &receiver,
                         [this](qint64 id, SampleSnapshot value) { snapshots[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::succeeded, &receiver,
                         [this](qint64 id, QString, qint64 value) { saves[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::error, &receiver,
                         [this](qint64 id, QString, QString value) { errors[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::closed, &receiver, [this]() { sampleClosed = true; }, Qt::QueuedConnection);
        page = new SamplePage(samples);
        QObject::connect(page, &SamplePage::snapshotChanged, &receiver, [this](SampleSnapshot) { ++pageLoads; });
        QObject::connect(page, &SamplePage::operationFinished, &receiver,
                         [this](QString operation, bool ok) { operations.append(qMakePair(operation, ok)); });
        host.setWidget(page); host.setWidgetResizable(false); host.resize(1600, 900);
        projectThread.start(); sampleThread.start();
    }
    ~Harness() { if (!stopped && !shutdown()) qFatal("Combination worker close/join failed"); }
    QString database() const { return directory.filePath("catalog.db"); }
    bool fail(const QString &message) { failure = message; qWarning().noquote() << message; return false; }
    bool projectWrite(const std::function<void(qint64)> &action, bool success = true)
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(projects, [action, id]() { action(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectSaves.contains(id) || projectErrors.contains(id); })) return fail("Project reply missing");
        lastProjectError = projectErrors.value(id);
        if (projectSaves.contains(id) == projectErrors.contains(id)) return fail("Project write did not have exactly one outcome");
        return ((projectSaves.contains(id) && !projectErrors.contains(id)) == success) || fail(lastProjectError);
    }
    bool sampleWrite(const std::function<void(qint64)> &action, bool success = true)
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(samples, [action, id]() { action(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return saves.contains(id) || errors.contains(id); })) return fail("Sample reply missing");
        lastError = errors.value(id); lastId = saves.value(id);
        if (saves.contains(id) == errors.contains(id)) return fail("Sample write did not have exactly one outcome");
        return ((saves.contains(id) && !errors.contains(id)) == success) || fail(lastError);
    }
    bool load(SampleSnapshot *value)
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(samples, [this, id]() { samples->load(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return snapshots.contains(id) || errors.contains(id); }) || errors.contains(id)) return fail(errors.value(id));
        *value = snapshots.value(id); return true;
    }
    bool combination(qint64 id, SampleCombination *value)
    {
        SampleSnapshot snapshot;
        if (!load(&snapshot)) return false;
        for (const auto &known : snapshot.combinations) if (known.id == id) { *value = known; return true; }
        return fail("Stable combination ID missing");
    }
    bool save(SampleCombination *value, bool success = true)
    {
        const SampleCombination copy = *value;
        if (!sampleWrite([this, copy](qint64 id) { samples->saveCombination(id, copy); }, success)) return false;
        if (success) value->id = lastId;
        return true;
    }
    bool openSample()
    {
        sampleReady = false; errors.remove(0);
        // close()清空SampleStore身份。重开时通过原Page公开setter恢复同一模拟身份。
        page->setSession(localActor);
        QMetaObject::invokeMethod(samples, [this]() { samples->initialize(directory.path()); }, Qt::QueuedConnection);
        return waitFor([&]() { return sampleReady && page->isReady() && !page->isBusy(); }) || fail("Real SampleStore initialization/reload failed: " + errors.value(0));
    }
    bool closeSample()
    {
        sampleClosed = false;
        QMetaObject::invokeMethod(samples, "close", Qt::QueuedConnection);
        return waitFor([&]() { return sampleClosed && !page->isReady(); }) || fail("Sample close missing");
    }
    bool reopen() { return closeSample() && openSample(); }
    bool start()
    {
        if (!directory.isValid()) return fail("New anonymous fixture directory missing");
        QMetaObject::invokeMethod(projects, [this]() { projects->initialize(directory.path(), true); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectReady; })) return fail("Actual ProjectsStore seed failed");
        const qint64 first = ++sequence;
        QMetaObject::invokeMethod(projects, [this, first]() { projects->load(first); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectSnapshots.contains(first); }) || projectSnapshots.value(first).size() != 1) return fail("Expected one actual anonymous seed project");
        AccountSession engineer;
        engineer.name = "anon-engineer"; engineer.role = 4; engineer.authenticated = true;
        engineer.source = AccountSessionSource::Simulated; engineer.actionReady = true;
        QMetaObject::invokeMethod(projects, [this, engineer]() { projects->setSession(engineer); }, Qt::QueuedConnection);
        const qint64 original = projectSnapshots.value(first).first().id;
        for (const auto &name : QStringList{"COMBO_A", "COMBO_B"})
            if (!projectWrite([this, original, name](qint64 id) { projects->copy(id, original, name); })) return false;
        const qint64 second = ++sequence;
        QMetaObject::invokeMethod(projects, [this, second]() { projects->load(second); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectSnapshots.contains(second); })) return fail("Copied projects not loaded");
        records = projectSnapshots.value(second);
        for (const auto &record : records)
        {
            if (record.name == "COMBO_A") a = record.id;
            if (record.name == "COMBO_B") b = record.id;
        }
        if (records.size() != 3 || !a || !b) return fail("Anonymous flexible project identities missing");
        localActor.name = "anon-local"; localActor.role = 1; localActor.authenticated = true;
        localActor.source = AccountSessionSource::Simulated; localActor.actionReady = false;
        page->setProjects(records);
        return openSample();
    }
    QByteArray combinationImage(bool modes = true)
    {
        Sql sql(database());
        QStringList queries{"SELECT * FROM sample_combination ORDER BY id", "SELECT * FROM sample_combination_member ORDER BY combination_id,position"};
        if (modes) queries.append("SELECT * FROM sample_combination_mode ORDER BY combination_id");
        return sql.image(queries);
    }
    QByteArray rackImage()
    {
        Sql sql(database());
        return sql.image({"SELECT * FROM sample_local_rack ORDER BY id", "SELECT * FROM sample_local_hole ORDER BY rack_id,number",
            "SELECT * FROM sample_local_task ORDER BY rack_id,hole_number,project_id",
            "SELECT * FROM sample_local_hole_state ORDER BY rack_id,hole_number",
            "SELECT * FROM sample_local_task_state ORDER BY rack_id,hole_number,project_id",
            "SELECT * FROM sample_local_quality_hole ORDER BY rack_id,hole_number",
            "SELECT * FROM sample_local_quality_project ORDER BY rack_id,hole_number,position",
            "SELECT value FROM catalog_metadata WHERE key='sample_qc_revision'"});
    }
    QTableWidget *table() const { return page->findChild<QTableWidget *>("tblSetting"); }
    bool prepareHost()
    {
        host.show();
        if (!QTest::qWaitForWindowExposed(&host, 3000) || !host.windowHandle()) return fail("Host not exposed");
        QScreen *selected = nullptr;
        for (auto *screen : QGuiApplication::screens())
        {
            qInfo() << "COMBINATION_SCREEN" << screen->name() << screen->geometry() << screen->availableGeometry() << screen->devicePixelRatio();
            const QRect available = screen->availableGeometry();
            if (!selected && available.width() >= host.frameGeometry().width() && available.height() >= host.frameGeometry().height()) selected = screen;
        }
        if (!selected) return fail("Actual screen cannot contain unchanged1600x900 host/frame");
        const QRect available = selected->availableGeometry(), before = host.frameGeometry();
        host.windowHandle()->setFramePosition(available.topLeft());
        host.raise(); host.activateWindow(); host.windowHandle()->requestActivate();
        const bool ready = waitFor([&]() { return host.isActiveWindow() && available.contains(host.frameGeometry()); });
        qInfo() << "COMBINATION_HOST" << before << host.frameGeometry() << available << host.isActiveWindow();
        return ready || fail("Host frame/activation prerequisite unmet");
    }
    bool click(QWidget *widget)
    {
        if (!widget) return fail("Input widget missing");
        if (widget->window() == &host) host.ensureWidgetVisible(widget, 0, 0);
        if (!waitFor([&]() { return widget->isVisible() && widget->isEnabled() && widget->visibleRegion().contains(widget->rect()); })) return fail("Input not fully visible/enabled: " + widget->objectName());
        const QPoint center = widget->rect().center();
        QWidget *hit = QApplication::widgetAt(widget->mapToGlobal(center));
        if (!hit || (hit != widget && !widget->isAncestorOf(hit))) return fail("Original center global hit failed: " + widget->objectName());
        QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, center);
        return true;
    }
    bool reloadPage()
    {
        const int before = pageLoads; page->reload();
        return waitFor([&]() { return pageLoads > before && !page->isBusy(); }) || fail("Page real reload missing");
    }
    QDialog *editor() const { return page->findChild<QDialog *>("sampleCombinationDialog"); }
    bool openEditor()
    {
        if (!click(page->findChild<QPushButton *>("btnCombSetting"))) return false;
        return waitFor([&]()
        {
            auto *dialog = editor(); auto *mask = dialog ? dialog->findChild<CusDialogMask *>("mask") : nullptr;
            return dialog && dialog->isVisible() && mask && mask->isReadyForInput();
        }) || fail("Original combination editor not ready");
    }
    bool finishEditor(bool save)
    {
        QPointer<QDialog> dialog = editor(); const int before = operations.size();
        if (!dialog || !click(dialog->findChild<QPushButton *>(save ? "btnSave" : "btnCancel"))) return false;
        return waitFor([&]()
        {
            bool committed = !save;
            for (int i = before; i < operations.size(); ++i)
                if (operations[i].first == "saveCombination") committed = operations[i].second;
            return dialog.isNull() && committed && !page->isBusy();
        }) || fail("Original editor cancel/save did not finish");
    }
    QByteArray draftView()
    {
        QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly); stream.setVersion(QDataStream::Qt_5_12);
        stream << table()->rowCount() << page->hasUnsavedChanges();
        for (int row = 0; row < table()->rowCount(); ++row)
        {
            for (int column = 0; column < table()->columnCount(); ++column)
                stream << (table()->item(row, column) ? table()->item(row, column)->text() : QString());
            auto *container = qobject_cast<CusCombox_Base *>(table()->cellWidget(row, 3));
            auto *tasks = dynamic_cast<SampleTaskView *>(table()->cellWidget(row, 4));
            if (!container || !tasks) return {};
            stream << container->getCurrentIndex() << tasks->states().size();
            for (auto state : tasks->states()) stream << static_cast<int>(state);
        }
        return bytes;
    }
    bool shutdown()
    {
        if (stopped) return true;
        host.takeWidget(); delete page; page = nullptr;
        sampleClosed = false; projectClosed = false;
        QMetaObject::invokeMethod(samples, "close", Qt::QueuedConnection);
        QMetaObject::invokeMethod(projects, "close", Qt::QueuedConnection);
        const bool closed = waitFor([&]() { return sampleClosed && projectClosed; });
        sampleThread.quit(); projectThread.quit();
        const bool samplesJoined = sampleThread.wait(5000), projectsJoined = projectThread.wait(5000);
        stopped = samplesJoined && projectsJoined;
        qInfo() << "COMBINATION_STORES_CLOSED" << closed << "THREADS_JOINED" << samplesJoined << projectsJoined;
        return closed && stopped;
    }
    QTemporaryDir directory;
    QObject receiver;
    QThread projectThread, sampleThread;
    ProjectsStore *projects = nullptr;
    SampleStore *samples = nullptr;
    SamplePage *page = nullptr;
    QScrollArea host;
    AccountSession localActor;
    ProjectRecords records;
    QMap<qint64, ProjectRecords> projectSnapshots;
    QMap<qint64, SampleSnapshot> snapshots;
    QMap<qint64, qint64> projectSaves, saves;
    QMap<qint64, QString> projectErrors, errors;
    QList<QPair<QString, bool>> operations;
    qint64 sequence = 1000000, lastId = 0, a = 0, b = 0;
    int pageLoads = 0;
    QString failure, lastError, lastProjectError;
    bool projectReady = false, sampleReady = false, sampleClosed = false, projectClosed = false, stopped = false;
};
}


namespace
{
QStyleOptionButton checkboxOption(QCheckBox *box)
{
    // Only a local option is populated. The original widget/style/layout is unchanged.
    QStyleOptionButton option;
    option.initFrom(box);
    option.text = box->text();
    option.icon = box->icon();
    option.iconSize = box->iconSize();
    if (box->isDown()) option.state |= QStyle::State_Sunken;
    if (box->checkState() == Qt::PartiallyChecked) option.state |= QStyle::State_NoChange;
    else if (box->isChecked()) option.state |= QStyle::State_On;
    else option.state |= QStyle::State_Off;
    return option;
}
QJsonArray rectangle(const QRect &rect)
{
    return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}
QJsonArray actualSignalValues(const QSignalSpy &spy)
{
    QJsonArray values;
    for (const auto &arguments : spy)
        values.append(arguments.isEmpty() ? QJsonValue() : QJsonValue(arguments.first().toBool()));
    return values;
}
void checkboxTrace(const char *phase, QCheckBox *box, const QSignalSpy &clicked,
                   const QSignalSpy &toggled, SamplePage *page)
{
    QJsonObject row{{"phase", QString::fromLatin1(phase)}, {"widget_alive", box != nullptr},
                    {"clicked_count", clicked.count()}, {"toggled_count", toggled.count()},
                    {"clicked_values", actualSignalValues(clicked)}, {"toggled_values", actualSignalValues(toggled)},
                    {"page_busy", page && page->isBusy()}, {"page_dirty", page && page->hasUnsavedChanges()}};
    if (box)
    {
        const auto option = checkboxOption(box);
        const QRect clickRect = box->style()->subElementRect(QStyle::SE_CheckBoxClickRect, &option, box);
        const QRect indicator = box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box);
        const QPoint center = box->rect().center(), global = box->mapToGlobal(center);
        QWidget *hit = QApplication::widgetAt(global);
        const QFont font = box->font();
        row.insert("checked", box->isChecked()); row.insert("check_state", int(box->checkState()));
        row.insert("enabled", box->isEnabled()); row.insert("visible", box->isVisible());
        row.insert("fully_visible", box->visibleRegion().contains(box->rect()));
        row.insert("down", box->isDown()); row.insert("focus", box->hasFocus());
        row.insert("rect", rectangle(box->rect())); row.insert("text", box->text());
        row.insert("click_rect", rectangle(clickRect)); row.insert("indicator_rect", rectangle(indicator));
        row.insert("center", QJsonArray{center.x(), center.y()});
        row.insert("global_center", QJsonArray{global.x(), global.y()});
        row.insert("center_in_click_rect", clickRect.contains(center));
        row.insert("center_widgetAt_is_box_or_child", hit && (hit == box || box->isAncestorOf(hit)));
        row.insert("center_widgetAt_name", hit ? hit->objectName() : QString());
        row.insert("center_widgetAt_class", hit ? QString::fromLatin1(hit->metaObject()->className()) : QString());
        row.insert("style_class", QString::fromLatin1(box->style()->metaObject()->className()));
        row.insert("style_name", box->style()->objectName());
        row.insert("application_style", QString::fromLatin1(QApplication::style()->metaObject()->className()));
        row.insert("font_family", font.family()); row.insert("font_point_size", font.pointSizeF());
        row.insert("font_pixel_size", font.pixelSize()); row.insert("font_weight", font.weight());
        row.insert("text_advance", QFontMetrics(font).horizontalAdvance(box->text()));
        row.insert("option_state", int(option.state));
        row.insert("option_source", "initFrom plus actual public text/icon/check/down properties; protected initStyleOption is not invoked");
        if (page)
        {
            auto *dialog = page->findChild<QDialog *>("sampleCombinationDialog");
            if (dialog)
                for (const QString &name : {QString("btnSave"), QString("btnCancel"), QString("btnAdd")})
                    if (auto *button = dialog->findChild<QPushButton *>(name))
                        row.insert(name + "_enabled", button->isEnabled());
        }
    }
    qInfo().noquote() << "CHECKBOX_DIAGNOSTIC" << QJsonDocument(row).toJson(QJsonDocument::Compact);
}
}

class CombinationCheckboxDiagnosticTest : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        const QString root = QDir::fromNativeSeparators(qEnvironmentVariable("SN100_COMBINATION_TEST_ROOT"));
        QVERIFY(QDir::isAbsolutePath(root));
        QVERIFY(root.contains("/temp/sn100_host/sample_combination_mode_tests/"));
        QVERIFY(!root.contains("sn100_host_reference", Qt::CaseInsensitive));
        for (QFileInfo info(root); !info.filePath().isEmpty(); info.setFile(info.absolutePath()))
        {
            QVERIFY(!info.isSymLink());
            if (info.absoluteFilePath() == info.absolutePath()) break;
        }
        QVERIFY2(!QFileInfo::exists(root), "Each selected business method requires a new explicit anonymous root");
        QVERIFY(QDir().mkpath(root));
        QVERIFY(QSqlDatabase::isDriverAvailable("QSQLITE"));
    }

    void centerAndIndicatorStateTrace()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        SampleCombination combination; combination.name = "UI_COMBO"; combination.orderedProjectIds = {h.a, h.b, h.a}; QVERIFY(h.save(&combination));
        SampleRack rack; rack.name = "S01";
        for (int number = 1; number <= 8; ++number) { SampleHole hole; hole.number = number; rack.holes.append(hole); }
        rack.holes[0].sampleId = "1001"; rack.holes[0].container = 0;
        SampleTask task; task.projectId = h.a; task.rate = "OFFLINE_RATE_A"; task.repeats = 1;
        task.states = {SampleTaskState::Error}; rack.holes[0].tasks.append(task);
        QVERIFY(h.sampleWrite([store = h.samples, rack](qint64 request) { store->saveRack(request, rack); }));
        rack.id = h.lastId;
        // 以真实已提交基线建立后续预期，避免把QString null/empty存储规范化算成草稿变化。
        SampleSnapshot baseline; QVERIFY(h.load(&baseline)); bool baselineFound = false;
        for (const auto &known : baseline.racks) if (known.id == rack.id) { rack = known; baselineFound = true; }
        QVERIFY(baselineFound);
        QVERIFY(h.reloadPage()); QVERIFY2(h.prepareHost(), qPrintable(h.failure));
        QVERIFY(h.click(h.page->findChild<QPushButton *>("btnNextRack"))); QCOMPARE(h.table()->rowCount(), 8);
        const QByteArray storedRacks = h.rackImage(); QVERIFY(!storedRacks.isEmpty());
        QVERIFY(h.click(h.page->findChild<QPushButton *>("btnResetErr"))); QVERIFY(h.page->hasUnsavedChanges());
        const QByteArray draft = h.draftView(), combinations = h.combinationImage(); QVERIFY(!draft.isEmpty() && !combinations.isEmpty());
        QVERIFY(h.openEditor());
        auto *mode = h.editor()->findChild<QCheckBox *>("cbk_multiCtrl"); QVERIFY(mode && !mode->isChecked());
        {
            QSignalSpy clicked(mode, &QCheckBox::clicked), toggled(mode, &QCheckBox::toggled);
            QVERIFY(clicked.isValid() && toggled.isValid());
            checkboxTrace("cancel_original_center_before", mode, clicked, toggled, h.page);
            QVERIFY(h.click(mode)); // Original center input is unchanged.
            checkboxTrace("cancel_original_center_after", mode, clicked, toggled, h.page);
        }
        auto *name = h.editor()->findChild<QLineEdit *>("ldtCombaName"); QVERIFY(name && h.click(name));
        QTest::keyClick(name, Qt::Key_A, Qt::ControlModifier); QTest::keyClicks(name, "CANCELLED");
        QVERIFY(h.finishEditor(false)); QCOMPARE(h.combinationImage(), combinations); QCOMPARE(h.rackImage(), storedRacks); QCOMPARE(h.draftView(), draft);
        QVERIFY(h.openEditor()); mode = h.editor()->findChild<QCheckBox *>("cbk_multiCtrl");
        QVERIFY(mode && !mode->isChecked()); QCOMPARE(h.editor()->findChild<QLineEdit *>("ldtCombaName")->text(), QString("UI_COMBO"));
        QSignalSpy clicked(mode, &QCheckBox::clicked), toggled(mode, &QCheckBox::toggled);
        QVERIFY(clicked.isValid() && toggled.isValid());
        QObject traceReceiver;
        QPointer<QCheckBox> observed = mode;
        QObject::connect(mode, &QCheckBox::clicked, &traceReceiver, [&]()
        { checkboxTrace("actual_clicked_signal", observed, clicked, toggled, h.page); });
        QObject::connect(mode, &QCheckBox::toggled, &traceReceiver, [&]()
        { checkboxTrace("actual_toggled_signal", observed, clicked, toggled, h.page); });
        auto *saveButton = h.editor()->findChild<QPushButton *>("btnSave"); QVERIFY(saveButton);
        QObject::connect(saveButton, &QPushButton::pressed, &traceReceiver, [&]()
        { checkboxTrace("save_button_pressed", observed, clicked, toggled, h.page); });
        QObject::connect(saveButton, &QPushButton::clicked, &traceReceiver, [&]()
        { checkboxTrace("save_button_clicked", observed, clicked, toggled, h.page); });
        checkboxTrace("save_original_center_before", mode, clicked, toggled, h.page);
        QVERIFY(h.click(mode)); // Original center input is unchanged, even if ineffective.
        checkboxTrace("save_original_center_after", mode, clicked, toggled, h.page);
        const bool centerChecked = mode->isChecked();
        const int centerClicked = clicked.count(), centerToggled = toggled.count();
        if (!centerChecked && centerToggled == 0)
        {
            // Independent diagnostic control, never a replacement for the recorded center result.
            QVERIFY(waitFor([&]() { return mode->isVisible() && mode->isEnabled() &&
                mode->visibleRegion().contains(mode->rect()); }));
            const auto option = checkboxOption(mode);
            const QRect indicator = mode->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, mode);
            const QPoint point = indicator.center();
            QVERIFY(indicator.isValid() && mode->rect().contains(point));
            QWidget *hit = QApplication::widgetAt(mode->mapToGlobal(point));
            QVERIFY(hit && (hit == mode || mode->isAncestorOf(hit)));
            qInfo().noquote() << "CHECKBOX_INDICATOR_CONTROL" << QJsonDocument(QJsonObject{
                {"center_checked", centerChecked}, {"center_clicked_count", centerClicked},
                {"center_toggled_count", centerToggled}, {"point", QJsonArray{point.x(), point.y()}},
                {"global_point", QJsonArray{mode->mapToGlobal(point).x(), mode->mapToGlobal(point).y()}},
                {"global_widgetAt_name", hit->objectName()},
                {"global_widgetAt_is_box_or_child", hit && (hit == mode || mode->isAncestorOf(hit))}
            }).toJson(QJsonDocument::Compact);
            checkboxTrace("indicator_control_before", mode, clicked, toggled, h.page);
            QTest::mouseClick(mode, Qt::LeftButton, Qt::NoModifier, point);
            checkboxTrace("indicator_control_after", mode, clicked, toggled, h.page);
        }
        else
        {
            qInfo().noquote() << "CHECKBOX_INDICATOR_CONTROL_NOT_RUN" << centerChecked << centerClicked << centerToggled;
        }
        checkboxTrace("before_original_save", mode, clicked, toggled, h.page);
        QVERIFY(h.finishEditor(true));
        SampleCombination saved; QVERIFY(h.combination(combination.id, &saved));
        {
            Sql direct(h.database());
            const QByteArray bytes = direct.image({QString("SELECT mode FROM sample_combination_mode WHERE combination_id=%1").arg(combination.id)});
            QVERIFY(!bytes.isEmpty());
            QDataStream stream(bytes); stream.setVersion(QDataStream::Qt_5_12);
            QList<QVariantList> rows; stream >> rows;
            QVERIFY(stream.status() == QDataStream::Ok && rows.size() == 1 && rows.first().size() == 1);
            qInfo().noquote() << "CHECKBOX_SAVE_READBACK" << QJsonDocument(QJsonObject{
                {"original_center_checked", centerChecked}, {"original_center_clicked_count", centerClicked},
                {"original_center_toggled_count", centerToggled}, {"snapshot_mode", int(saved.mode)},
                {"actual_SQL_mode", rows.first().first().toString()}, {"combination_id", QString::number(combination.id)},
                {"diagnostic_control_only_not_original_case_pass", true}
            }).toJson(QJsonDocument::Compact);
        }
        QVERIFY(saved.mode == SampleCombinationMode::MultiReagentBottle); QCOMPARE(saved.orderedProjectIds, combination.orderedProjectIds);
        QCOMPARE(h.rackImage(), storedRacks); QCOMPARE(h.draftView(), draft);
        QPushButton *apply = nullptr;
        auto *list = h.page->findChild<QListWidget *>("projButtonList"); QVERIFY(list);
        for (auto *button : list->findChildren<QPushButton *>()) if (button->text() == "UI_COMBO") apply = button;
        QVERIFY(apply); QVERIFY(h.click(apply));
        QVERIFY(h.page->property("lastStatus").toString().contains(QStringLiteral("不能应用")));
        QCOMPARE(h.draftView(), draft); QCOMPARE(h.rackImage(), storedRacks);
        const int beforeSave = h.operations.size(); QVERIFY(h.click(h.page->findChild<QPushButton *>("btnMenuSave")));
        QVERIFY(waitFor([&]()
        {
            bool committed = false;
            for (int i = beforeSave; i < h.operations.size(); ++i) if (h.operations[i].first == "saveRack") committed = h.operations[i].second;
            return committed && !h.page->isBusy() && !h.page->hasUnsavedChanges();
        }));
        // 不只比较屏幕或旧snapshot：最终真实提交全部孔位/任务与原ResetErr预期逐字段对照。
        rack.holes[0].tasks[0].states = {SampleTaskState::Idle};
        SampleSnapshot snapshot; QVERIFY(h.load(&snapshot));
        bool found = false;
        for (const auto &known : snapshot.racks) if (known.id == rack.id) { QCOMPARE(rackValue(known), rackValue(rack)); found = true; }
        QVERIFY(found); QVERIFY(h.shutdown());
    }
};

int main(int argc, char **argv)
{
    int selected = 0;
    for (int index = 1; index < argc; ++index)
        if (QString::fromLocal8Bit(argv[index]) == QStringLiteral("centerAndIndicatorStateTrace")) ++selected;
    if (selected != 1)
    { std::fputs("Select exactly the single checkbox diagnostic; no old business rerun.\n", stderr); return 2; }
    QApplication app(argc, argv);
    CombinationCheckboxDiagnosticTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "sample_combination_mode_test.moc"
