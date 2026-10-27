#include "sample_page.h"
#include "cusdialogmask.h"
#include "qtmaterialradiobutton.h"
#include "quality_control/qc_store.h"
#include "shell/sample_qc_shell.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
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
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <QWindow>
#include <QtTest>
#include <functional>

namespace
{
// 有界等待真实条件；不插入worker延迟、不手动发业务成功信号。
bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < 3000)
    {
        QTest::qWait(5);
    }
    return condition();
}

class Database
{
  public:
    explicit Database(const QString &path)
    {
        // 只打开真实owner已建好的匿名文件；读错路径不能创建一个空库冒充成功。
        if (!QFileInfo(path).isFile())
        {
            return;
        }
        name_ = QUuid::createUuid().toString();
        database_ = QSqlDatabase::addDatabase("QSQLITE", name_);
        database_.setDatabaseName(path);
        database_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");
        if (database_.open())
        {
            exec("PRAGMA foreign_keys=ON");
        }
    }
    ~Database()
    {
        if (name_.isEmpty()) return;
        database_.close();
        database_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(name_);
    }
    bool exec(const QString &sql, const QVariantList &values = {})
    {
        if (!database_.isOpen()) return false;
        QSqlQuery query(database_);
        if (!query.prepare(sql)) return false;
        for (const auto &value : values) query.addBindValue(value);
        const bool ok = query.exec();
        if (!ok) qWarning() << "ANONYMOUS_FIXTURE_SQL" << query.lastError().text();
        return ok;
    }
    QVariant scalar(const QString &sql, const QVariantList &values = {})
    {
        if (!database_.isOpen()) return QVariant();
        QSqlQuery query(database_);
        if (!query.prepare(sql)) return QVariant();
        for (const auto &value : values) query.addBindValue(value);
        if (!query.exec() || !query.next()) return QVariant();
        return query.value(0);
    }
    QByteArray image(const QStringList &queries)
    {
        if (!database_.isOpen()) return QByteArray();
        QJsonArray tables;
        for (const auto &sql : queries)
        {
            QSqlQuery query(database_);
            if (!query.exec(sql)) return QByteArray();
            QJsonArray rows;
            while (query.next())
            {
                QJsonArray row;
                for (int column = 0; column < query.record().count(); ++column)
                    row.append(QJsonValue::fromVariant(query.value(column)));
                rows.append(row);
            }
            if (query.lastError().isValid()) return QByteArray();
            tables.append(rows);
        }
        return QJsonDocument(tables).toJson(QJsonDocument::Compact);
    }
  private:
    QString name_;
    QSqlDatabase database_;
};

QList<qint64> taskIds(const SampleHole &hole)
{
    QList<qint64> result;
    for (const auto &task : hole.tasks) result.append(task.projectId);
    return result;
}

class Harness
{
  public:
    Harness() : directory(QDir(qEnvironmentVariable("SN100_SAMPLE_QC_TEST_ROOT")).filePath("workflow-XXXXXX"))
    {
        directory.setAutoRemove(false);
        ProjectsStore::registerTypes();
        SampleStore::registerTypes();
        QcStore::registerTypes();
        projects = new ProjectsStore;
        samples = new SampleStore;
        quality = new QcStore;
        projects->moveToThread(&projectThread);
        samples->moveToThread(&sampleThread);
        quality->moveToThread(&qualityThread);
        QObject::connect(&projectThread, &QThread::finished, projects, &QObject::deleteLater);
        QObject::connect(&sampleThread, &QThread::finished, samples, &QObject::deleteLater);
        QObject::connect(&qualityThread, &QThread::finished, quality, &QObject::deleteLater);
        // 每个容器只由主测试线程修改/读取；worker只投递完整值。
        QObject::connect(projects, &ProjectsStore::ready, &receiver,
                         [this]() { projectReady = true; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::snapshotReady, &receiver,
                         [this](qint64 id, ProjectRecords value, ProjectUnits units)
                         { projectSnapshots[id] = value; projectUnits = units; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::succeeded, &receiver,
                         [this](qint64 id, QString, qint64 value) { projectSaved[id] = value; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::error, &receiver,
                         [this](qint64 id, QString, QString value) { projectErrors[id] = value; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::closed, &receiver,
                         [this]() { projectClosed = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::ready, &receiver,
                         [this]() { sampleReady = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::snapshotReady, &receiver,
                         [this](qint64 id, SampleSnapshot value) { sampleSnapshots[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::succeeded, &receiver,
                         [this](qint64 id, QString, qint64 value) { sampleSaved[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::error, &receiver,
                         [this](qint64 id, QString, QString value) { sampleErrors[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::qualityProtectionReady, &receiver,
                         [this](bool value) { protectionReady = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::closed, &receiver,
                         [this]() { sampleClosed = true; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::ready, &receiver,
                         [this](QString path) { qualityPath = path; qualityReady = true; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::catalogBound, &receiver,
                         [this]() { catalogBound = true; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::catalogReady, &receiver,
                         [this](qint64 id, QcProjectRefs) { catalogs[id] = true; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::snapshotReady, &receiver,
                         [this](qint64 id, QcControls value) { qualitySnapshots[id] = value; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::succeeded, &receiver,
                         [this](qint64 id, QString, qint64 value) { qualitySaved[id] = value; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::error, &receiver,
                         [this](qint64 id, QString, QString value) { qualityErrors[id] = value; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::sampleChoicesReady, &receiver,
                         [this](qint64, SampleQcPreset::Snapshot value) { choices = value; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::sampleChangeReserved, &receiver,
                         [this](SampleQcReservationReply value) { reservations[value.changeToken] = value; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::sampleChangeFinished, &receiver,
                         [this](SampleQcFinishReply value) { finishes[value.changeToken] = value; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::sampleOwnerReconciled, &receiver,
                         [this](QString, QString, bool ready, QString message)
                         { ++reconcileCount; reconciled = ready; reconcileMessage = message; }, Qt::QueuedConnection);
        QObject::connect(quality, &QcStore::closed, &receiver,
                         [this]() { qualityClosed = true; }, Qt::QueuedConnection);
        page = new SamplePage(samples);
        QObject::connect(page, &SamplePage::snapshotChanged, &receiver,
                         [this](SampleSnapshot) { ++pageLoads; });
        QObject::connect(page, &SamplePage::operationFinished, &receiver,
                         [this](QString operation, bool success) { pageOperations.append(qMakePair(operation, success)); });
        // 唯一业务接线。与production main同一实现，不能在测试中抄一套假桥。
        bridgeConnected = connectSampleQuality(samples, quality, page, &receiver);
        host.setWidget(page);
        host.setWidgetResizable(false);
        host.resize(1600, 900);
        projectThread.start();
        sampleThread.start();
        qualityThread.start();
    }
    ~Harness()
    {
        if (!stopped && !shutdown()) qFatal("Sample/QC workflow workers failed to close and join");
    }
    QString catalogDirectory() const { return directory.filePath("catalog"); }
    QString catalogPath() const { return QDir(catalogDirectory()).filePath("catalog.db"); }
    QString qualityDirectory() const { return directory.filePath("quality"); }
    bool fail(const QString &message) { failure = message; qWarning().noquote() << message; return false; }
    bool loadProjects()
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(projects, [this, id]() { projects->load(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectSnapshots.contains(id) || projectErrors.contains(id); }) || projectErrors.contains(id))
            return fail("ProjectsStore load: " + projectErrors.value(id));
        records = projectSnapshots.value(id);
        return true;
    }
    bool projectWrite(const std::function<void(qint64)> &action)
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(projects, [action, id]() { action(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectSaved.contains(id) || projectErrors.contains(id); }) || projectErrors.contains(id))
            return fail("ProjectsStore write: " + projectErrors.value(id));
        return true;
    }
    bool qualityWrite(const std::function<void(qint64)> &action, bool expectSuccess = true)
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(quality, [action, id]() { action(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return qualitySaved.contains(id) || qualityErrors.contains(id); }))
            return fail("QcStore write reply missing");
        lastQualityError = qualityErrors.value(id);
        lastId = qualitySaved.value(id);
        const bool success = qualitySaved.contains(id) && !qualityErrors.contains(id);
        return success == expectSuccess || fail("QcStore unexpected outcome: " + lastQualityError);
    }
    bool loadSamples(SampleSnapshot *value)
    {
        const qint64 id = ++sequence;
        QMetaObject::invokeMethod(samples, [this, id]() { samples->load(id); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return sampleSnapshots.contains(id) || sampleErrors.contains(id); }) || sampleErrors.contains(id))
            return fail("SampleStore load: " + sampleErrors.value(id));
        *value = sampleSnapshots.value(id);
        return true;
    }
    SampleRack savedRack() const
    {
        for (const auto &rack : page->snapshot().racks) if (rack.name == "S01") return rack;
        return SampleRack();
    }
    bool start()
    {
        if (!directory.isValid() || !bridgeConnected) return fail("New anonymous directory or production bridge unavailable");
        QMetaObject::invokeMethod(projects, [this]() { projects->initialize(catalogDirectory(), true); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return projectReady || projectErrors.contains(0); }) || !projectReady || !loadProjects() || records.size() != 1)
            return fail("Real project owner initialization failed");
        const qint64 original = records.first().id;
        actor.name = "anonymous-local-engineer";
        actor.role = 4;
        actor.authenticated = true;
        actor.source = AccountSessionSource::Simulated;
        actor.actionReady = false; // 沿用本地编辑权限；不要求也不伪造设备ready。
        AccountSession projectEditor = actor;
        projectEditor.actionReady = true; // Anonymous project setup uses its existing ready-only permission.
        QMetaObject::invokeMethod(projects, [this, projectEditor]() { projects->setSession(projectEditor); }, Qt::QueuedConnection);
        for (const auto &name : QStringList{"QC_PROJECT_B", "QC_DISABLED_C", "UNASSOCIATED_D"})
            if (!projectWrite([this, original, name](qint64 id) { projects->copy(id, original, name); })) return false;
        if (!loadProjects() || records.size() != 4) return fail("Anonymous project copies missing");
        for (auto record : records)
        {
            if (record.name != "QC_DISABLED_C") continue;
            record.enabled = false;
            if (!projectWrite([this, record](qint64 id) { projects->save(id, record); })) return false;
        }
        if (!loadProjects()) return false;
        for (const auto &record : records)
        {
            QcProjectRef ref;
            ref.id = record.id;
            ref.name = record.name;
            for (const auto &result : record.results)
            {
                if (result.type != "Calibration") continue; // 不猜Time或未启用的临床类型。
                for (const auto &unit : projectUnits)
                {
                    if (unit.name != result.unit) continue;
                    QcResultRef value;
                    value.type = 2; value.name = result.name; value.unit = result.unit;
                    value.decimals = result.decimals; value.ratio = unit.ratio;
                    ref.results.append(value);
                }
            }
            if (ref.results.size() != 1) return fail("Anonymous Calibration result/unit prerequisite changed");
            refs.append(ref);
            if (record.name == "UNASSOCIATED_D") ordinaryProjectId = record.id;
            else linkedRefs.append(ref);
            if (record.enabled && record.name != "UNASSOCIATED_D") expectedProjects.append(record.id);
        }
        if (expectedProjects.size() != 2 || linkedRefs.size() != 3 || !ordinaryProjectId)
            return fail("Enabled/disabled/unassociated fixture identities invalid");
        page->setProjects(records);
        page->setSession(actor);
        const qint64 catalogRequest = ++sequence;
        QMetaObject::invokeMethod(quality, [this, catalogRequest]()
        {
            quality->initialize(qualityDirectory());
            quality->bindCatalog(catalogDirectory());
            quality->setCatalog(catalogRequest, refs);
        }, Qt::QueuedConnection);
        if (!waitFor([&]() { return qualityReady && catalogBound && catalogs.contains(catalogRequest); }))
            return fail("QC real catalog binding failed: " + qualityErrors.value(catalogRequest));
        for (int index = 0; index < 2; ++index)
        {
            QcControl control;
            control.name = index ? "QC_EMPTY" : "QC_LINKED";
            control.manufacturer = "anonymous manufacturer";
            if (!qualityWrite([this, control](qint64 id) { quality->saveControl(id, control); })) return false;
            const qint64 controlId = lastId;
            if (index) emptyControlId = controlId;
            else
            {
                linkedControlId = controlId;
                if (!qualityWrite([this, controlId](qint64 id) { quality->setProjects(id, controlId, linkedRefs); })) return false;
            }
            qint64 current = 0;
            for (const auto &suffix : QStringList{"REF", "PREV", "CURRENT"})
            {
                QcBatch batch;
                batch.controlId = controlId;
                batch.name = QString(index ? "EMPTY_" : "LINKED_") + suffix;
                batch.expirationDate = "2027/01/31";
                if (!qualityWrite([this, batch](qint64 id) { quality->saveBatch(id, batch); })) return false;
                current = lastId;
                if (suffix == "REF")
                {
                    if (index) emptyBatchId = lastId; else linkedBatchId = lastId;
                }
                // REF最终既非当前也非前批，删除拒绝不能被原批次标志规则冒充。
                if (!qualityWrite([this, controlId, current](qint64 id) { quality->switchBatch(id, controlId, current); })) return false;
            }
        }
        QMetaObject::invokeMethod(samples, [this]() { samples->initialize(catalogDirectory()); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return sampleReady && protectionReady && page->isReady() && !page->isBusy(); }))
            return fail("Sample/QC real owner reconciliation failed");
        SampleRack rack;
        rack.name = "S01";
        for (int number = 1; number <= 8; ++number) { SampleHole hole; hole.number = number; rack.holes.append(hole); }
        rack.holes[0].sampleId = "A001";
        SampleTask task;
        task.projectId = ordinaryProjectId;
        task.repeats = 1;
        for (const auto &record : records) if (record.id == ordinaryProjectId)
            for (const auto &rate : record.sampleRates) if (rate.defaultOption && rate.sampleOption) task.rate = rate.sampleRate;
        if (task.rate.isEmpty()) return fail("Anonymous ordinary rate prerequisite missing");
        rack.holes[0].tasks.append(task);
        const qint64 request = ++sequence;
        QMetaObject::invokeMethod(samples, [this, request, rack]() { samples->saveRack(request, rack); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return sampleSaved.contains(request) || sampleErrors.contains(request); }) || sampleErrors.contains(request))
            return fail("Anonymous ordinary rack seed failed: " + sampleErrors.value(request));
        const int before = pageLoads;
        page->reload();
        if (!waitFor([&]() { return pageLoads > before && !page->isBusy(); })) return fail("Page real reload missing");
        host.show();
        if (!prepareHost()) return false;
        if (table()->rowCount() == 3 && !click(page->findChild<QPushButton *>("btnNextRack"))) return false;
        if (table()->rowCount() != 8) return fail("Ordinary rack not selected");
        qInfo() << "SAMPLE_QC_ANONYMOUS_FIXTURE" << directory.path() << "projects" << records.size();
        return true;
    }
    bool prepareHost()
    {
        if (!QTest::qWaitForWindowExposed(&host, 3000) || !host.windowHandle()) return fail("Host not exposed");
        QScreen *selected = nullptr;
        const auto screens = QGuiApplication::screens();
        for (auto *screen : screens)
        {
            qInfo() << "SAMPLE_QC_SCREEN" << screen->name() << screen->geometry() << screen->availableGeometry()
                    << screen->devicePixelRatio();
            const QRect available = screen->availableGeometry();
            if (!selected && available.width() >= host.frameGeometry().width() && available.height() >= host.frameGeometry().height())
                selected = screen;
        }
        if (!selected) return fail("Actual screen cannot contain unchanged 1600x900 host plus frame");
        const QRect available = selected->availableGeometry();
        const QRect before = host.frameGeometry();
        host.windowHandle()->setFramePosition(available.topLeft());
        host.raise();
        host.activateWindow();
        host.windowHandle()->requestActivate();
        const bool ready = waitFor([&]() { return host.isActiveWindow() && available.contains(host.frameGeometry()); });
        qInfo() << "SAMPLE_QC_HOST" << "before" << before << "frame" << host.frameGeometry()
                << "client" << QRect(host.mapToGlobal(QPoint()), host.size()) << "available" << available
                << "active" << host.isActiveWindow() << "windowDpr" << host.windowHandle()->devicePixelRatio();
        return ready || fail("Actual host frame/activation input prerequisite unmet");
    }
    QTableWidget *table() const { return page->findChild<QTableWidget *>("tblSetting"); }
    bool click(QWidget *widget)
    {
        if (!widget) return fail("Input widget missing");
        if (widget->window() == &host) host.ensureWidgetVisible(widget, 0, 0);
        if (!waitFor([&]() { return widget->isVisible() && widget->isEnabled() && widget->visibleRegion().contains(widget->rect()); }))
            return fail("Input widget not fully visible/enabled: " + widget->objectName());
        const QPoint point = widget->rect().center();
        QWidget *hit = QApplication::widgetAt(widget->mapToGlobal(point));
        if (!hit || (hit != widget && !widget->isAncestorOf(hit)))
            return fail("Global hit prerequisite failed: " + widget->objectName());
        QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, point);
        return true;
    }
    QDialog *detail() const { return page->findChild<QDialog *>("sampleDetailDialog"); }
    bool openDetail()
    {
        if (!click(page->findChild<QPushButton *>("btnHoleDetail"))) return false;
        return waitFor([&]()
        {
            auto *dialog = detail();
            auto *mask = dialog ? dialog->findChild<CusDialogMask *>("mask") : nullptr;
            auto *control = dialog ? dialog->findChild<QComboBox *>("cbxControName") : nullptr;
            return dialog && dialog->isVisible() && mask && mask->isReadyForInput() && control && control->count() == 2;
        }) || fail("Original detail mask/real QC choices not ready");
    }
    bool choose(QComboBox *combo, qint64 id)
    {
        if (!combo) return fail("Original QC combobox missing");
        const int index = combo->findData(id);
        if (index < 0 || !click(combo)) return fail("Stable QC choice missing or not clickable");
        auto *view = combo->view();
        if (!waitFor([&]() { return view->isVisible(); })) return fail("Original QC popup not visible");
        QTest::keyClick(view, Qt::Key_Home);
        for (int row = 0; row < index; ++row) QTest::keyClick(view, Qt::Key_Down);
        QTest::keyClick(view, Qt::Key_Return);
        return combo->currentData().toLongLong() == id || fail("QC keyboard choice did not activate requested stable ID");
    }
    bool selectQuality(bool empty = false)
    {
        auto *dialog = detail();
        auto *radio = dialog ? dialog->findChild<QtMaterialRadioButton *>("radioBoxQC") : nullptr;
        if (!radio || (!radio->isChecked() && !click(radio))) return fail("Original QC radio unavailable");
        if (!choose(dialog->findChild<QComboBox *>("cbxControName"), empty ? emptyControlId : linkedControlId)) return false;
        if (!choose(dialog->findChild<QComboBox *>("cbxControlBatch"), empty ? emptyBatchId : linkedBatchId)) return false;
        return radio->isChecked();
    }
    bool finishDetail(bool save)
    {
        QPointer<QDialog> dialog = detail();
        if (!dialog || !click(dialog->findChild<QPushButton *>(save ? "btnSave" : "btnCancle"))) return false;
        return waitFor([&]() { return dialog.isNull(); }) || fail("Original detail did not close/delete");
    }
    bool savePage(bool expectSuccess = true)
    {
        const int before = pageOperations.size();
        if (!click(page->findChild<QPushButton *>("btnMenuSave"))) return false;
        bool observed = false, success = false;
        const bool done = waitFor([&]()
        {
            for (int i = before; i < pageOperations.size(); ++i)
                if (pageOperations[i].first == "saveRack") { observed = true; success = pageOperations[i].second; }
            return observed && !page->isBusy() && (!expectSuccess || !page->hasUnsavedChanges());
        });
        return (done && success == expectSuccess) || fail("Page rack save outcome: " + page->property("lastStatus").toString());
    }
    bool makeQualityDraft(bool empty = false) { return openDetail() && selectQuality(empty) && finishDetail(true); }
    bool makeOrdinaryDraft(const QString &id)
    {
        if (!openDetail()) return false;
        auto *dialog = detail();
        auto *radio = dialog->findChild<QtMaterialRadioButton *>("radioBoxSM");
        if (!click(radio)) return false;
        auto *input = dialog->findChild<QLineEdit *>("ldtSampleID");
        if (!input || input->isReadOnly() || input->maxLength() != 4 || !click(input)) return false;
        QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(input, id);
        if (input->text() != id || !input->hasAcceptableInput()) return fail("Original ordinary 4-character input contract changed");
        return finishDetail(true);
    }
    bool leave(QMessageBox::StandardButton answer)
    {
        confirmationClicked = false;
        QTimer timer;
        timer.setInterval(5);
        QObject::connect(&timer, &QTimer::timeout, &receiver, [&]()
        {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box->objectName() != "sampleDiscardDialog") return;
            auto *button = box->button(answer);
            if (button && click(button)) { confirmationClicked = true; timer.stop(); }
        });
        // 只用于失败清场，绝不算用户确认成功；正常路径必须上面的实际mouseClick。
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&guard, &QTimer::timeout, &receiver, []()
        { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->reject(); });
        timer.start(); guard.start(3000);
        const bool result = page->prepareToLeave();
        timer.stop(); guard.stop();
        return result;
    }
    bool reopen()
    {
        sampleClosed = false;
        QMetaObject::invokeMethod(samples, "close", Qt::QueuedConnection);
        if (!waitFor([&]() { return sampleClosed && !page->isReady(); })) return fail("Sample close missing");
        qualityClosed = false;
        QMetaObject::invokeMethod(quality, "close", Qt::QueuedConnection);
        if (!waitFor([&]() { return qualityClosed; })) return fail("QC close missing");
        sampleReady = false; qualityReady = false; catalogBound = false; protectionReady = false;
        QMetaObject::invokeMethod(quality, [this]() { quality->initialize(qualityDirectory()); quality->bindCatalog(catalogDirectory()); }, Qt::QueuedConnection);
        if (!waitFor([&]() { return qualityReady && catalogBound; })) return fail("QC reopen/bind missing");
        QMetaObject::invokeMethod(samples, [this]() { samples->initialize(catalogDirectory()); }, Qt::QueuedConnection);
        return waitFor([&]() { return sampleReady && protectionReady && page->isReady() && !page->isBusy(); }) || fail("Real reopened sample/QC reconciliation missing");
    }
    QByteArray sampleImage()
    {
        Database db(catalogPath());
        return db.image({"SELECT * FROM sample_local_rack ORDER BY id", "SELECT * FROM sample_local_hole ORDER BY rack_id,number",
            "SELECT * FROM sample_local_task ORDER BY rack_id,hole_number,project_id",
            "SELECT * FROM sample_local_hole_state ORDER BY rack_id,hole_number",
            "SELECT * FROM sample_local_task_state ORDER BY rack_id,hole_number,project_id",
            "SELECT * FROM sample_local_quality_hole ORDER BY rack_id,hole_number",
            "SELECT * FROM sample_local_quality_project ORDER BY rack_id,hole_number,position",
            "SELECT * FROM sample_local_quality_change ORDER BY change_token",
            "SELECT key,value FROM catalog_metadata WHERE key IN('sample_dataset_uuid','sample_qc_revision','sample_qc_schema') ORDER BY key"});
    }
    QByteArray holdImage(bool includeChanges = false)
    {
        Database db(qualityPath);
        QStringList queries{"SELECT * FROM qc_sample_hold ORDER BY slot_token", "SELECT * FROM qc_sample_hold_project ORDER BY slot_token,position"};
        if (includeChanges) queries.append("SELECT * FROM qc_sample_change ORDER BY change_token");
        return db.image(queries);
    }
    bool shutdown()
    {
        if (stopped) return true;
        host.takeWidget(); delete page; page = nullptr;
        sampleClosed = false;
        QMetaObject::invokeMethod(samples, "close", Qt::QueuedConnection);
        const bool sampleDone = waitFor([&]() { return sampleClosed; });
        qualityClosed = false; projectClosed = false;
        QMetaObject::invokeMethod(quality, "close", Qt::QueuedConnection);
        QMetaObject::invokeMethod(projects, "close", Qt::QueuedConnection);
        const bool otherDone = waitFor([&]() { return qualityClosed && projectClosed; });
        sampleThread.quit(); qualityThread.quit(); projectThread.quit();
        const bool sampleJoined = sampleThread.wait(5000);
        const bool qualityJoined = qualityThread.wait(5000);
        const bool projectJoined = projectThread.wait(5000);
        stopped = sampleJoined && qualityJoined && projectJoined;
        qInfo() << "SAMPLE_QC_STORES_CLOSED" << sampleDone << otherDone << "THREADS_JOINED"
                << sampleJoined << qualityJoined << projectJoined;
        return sampleDone && otherDone && stopped;
    }

    QObject receiver;
    QTemporaryDir directory;
    QThread projectThread, sampleThread, qualityThread;
    ProjectsStore *projects = nullptr;
    SampleStore *samples = nullptr;
    QcStore *quality = nullptr;
    SamplePage *page = nullptr;
    QScrollArea host;
    AccountSession actor;
    ProjectRecords records;
    ProjectUnits projectUnits;
    QcProjectRefs refs, linkedRefs;
    QList<qint64> expectedProjects;
    SampleQcPreset::Snapshot choices;
    QMap<qint64, ProjectRecords> projectSnapshots;
    QMap<qint64, SampleSnapshot> sampleSnapshots;
    QMap<qint64, QcControls> qualitySnapshots;
    QMap<qint64, qint64> projectSaved, sampleSaved, qualitySaved;
    QMap<qint64, QString> projectErrors, sampleErrors, qualityErrors;
    QMap<qint64, bool> catalogs;
    QMap<QString, SampleQcReservationReply> reservations;
    QMap<QString, SampleQcFinishReply> finishes;
    QList<QPair<QString, bool>> pageOperations;
    QString qualityPath, failure, lastQualityError, reconcileMessage;
    qint64 sequence = 1000000, lastId = 0, linkedControlId = 0, linkedBatchId = 0,
           emptyControlId = 0, emptyBatchId = 0, ordinaryProjectId = 0;
    int pageLoads = 0, reconcileCount = 0;
    bool bridgeConnected = false, projectReady = false, sampleReady = false, qualityReady = false,
         catalogBound = false, protectionReady = false, sampleClosed = false, qualityClosed = false,
         projectClosed = false, stopped = false, reconciled = false, confirmationClicked = false;
};
}

class SampleQcWorkflowTest : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        const QString root = QDir::fromNativeSeparators(qEnvironmentVariable("SN100_SAMPLE_QC_TEST_ROOT"));
        QVERIFY2(QDir::isAbsolutePath(root), "Explicit fresh SN100_SAMPLE_QC_TEST_ROOT is required");
        QVERIFY(root.toLower().contains("/temp/sn100_host/single_slot_qc_20261026/"));
        QVERIFY(!root.contains("sn100_host_reference", Qt::CaseInsensitive));
        for (QFileInfo current(root); !current.filePath().isEmpty(); current.setFile(current.absolutePath()))
        {
            QVERIFY(!current.isSymLink());
            if (current.absoluteFilePath() == current.absolutePath()) break;
        }
        QVERIFY(QDir().mkpath(root));
        QVERIFY2(QDir(root).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(), "Data root must be new/empty; old runtime is never reused");
    }

    // 名称/批次只是显示；是否自动选项目须同时区分enabled关联、disabled关联和未关联项目。
    void selectionCancelAndDetailSaveAreDraftOnly()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        const QByteArray before = h.sampleImage(), holds = h.holdImage(true);
        QVERIFY(!before.isEmpty()); QVERIFY(!holds.isEmpty());
        QVERIFY2(h.openDetail() && h.selectQuality(), qPrintable(h.failure));
        auto *dialog = h.detail();
        auto *table = dialog->findChild<QTableWidget *>("tblSetting");
        auto *input = dialog->findChild<QLineEdit *>("ldtSampleID");
        QVERIFY(table && input); QCOMPARE(table->rowCount(), 2);
        QCOMPARE(input->text(), QString("QC_LINKED-LINKED_REF"));
        QVERIFY(input->isReadOnly()); QCOMPARE(input->maxLength(), 32767);
        for (const auto &record : h.records)
        {
            auto *button = dialog->findChild<QPushButton *>("detailProject_" + QString::number(record.id));
            // 原详情只生成已启用项目按钮；disabled关联应不成为任务。
            if (!record.enabled) { QVERIFY(!button); continue; }
            QVERIFY(button); QVERIFY(!button->isEnabled());
            QCOMPARE(button->isChecked(), h.expectedProjects.contains(record.id));
        }
        QVERIFY2(h.finishDetail(false), qPrintable(h.failure));
        QVERIFY(!h.page->hasUnsavedChanges());
        QCOMPARE(h.table()->item(0, 1)->text(), QString("A001"));
        QCOMPARE(h.sampleImage(), before); QCOMPARE(h.holdImage(true), holds);
        QVERIFY2(h.makeQualityDraft(), qPrintable(h.failure));
        QVERIFY(h.page->hasUnsavedChanges());
        QCOMPARE(h.table()->item(0, 1)->text(), QString("QC_LINKED-LINKED_REF"));
        QCOMPARE(h.sampleImage(), before); QCOMPARE(h.holdImage(true), holds);
        QVERIFY(!h.leave(QMessageBox::Cancel)); QVERIFY(h.confirmationClicked);
        QVERIFY(h.page->hasUnsavedChanges());
        QCOMPARE(h.sampleImage(), before); QCOMPARE(h.holdImage(true), holds);
        QVERIFY(h.leave(QMessageBox::Discard)); QVERIFY(h.confirmationClicked);
        QVERIFY(!h.page->hasUnsavedChanges());
        QCOMPARE(h.table()->item(0, 1)->text(), QString("A001"));
        QCOMPARE(h.sampleImage(), before); QCOMPARE(h.holdImage(true), holds);
        QVERIFY(h.shutdown());
    }

    // 必须经过整架保存与两个Store真实重开，不能把详情Save或显示名当持久身份。
    void rackSaveAndReopenPreserveStableQualityIdentity()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        QVERIFY2(h.makeQualityDraft() && h.savePage(), qPrintable(h.failure));
        const SampleRack rack = h.savedRack(); QVERIFY(rack.id > 0); QCOMPARE(rack.holes.size(), 8);
        const SampleHole saved = rack.holes.first(); QCOMPARE(saved.sampleType, 2);
        QCOMPARE(saved.quality.selected.controlId, h.linkedControlId);
        QCOMPARE(saved.quality.selected.batchId, h.linkedBatchId);
        QCOMPARE(taskIds(saved), h.expectedProjects);
        QVERIFY(!QUuid(saved.quality.selected.datasetId).isNull());
        QVERIFY(!QUuid(saved.quality.slotToken).isNull()); QVERIFY(!QUuid(saved.quality.changeToken).isNull());
        QCOMPARE(saved.quality.selected.projects.size(), 2);
        {
            Database qc(h.qualityPath), catalog(h.catalogPath());
            QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 1);
            QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 2);
            QCOMPARE(qc.scalar("SELECT outcome FROM qc_sample_change WHERE change_token=?", {saved.quality.changeToken}).toString(), QString("committed"));
            QCOMPARE(catalog.scalar("SELECT COUNT(*) FROM sample_local_quality_change").toInt(), 0);
            for (int i = 0; i < saved.quality.selected.projects.size(); ++i)
            {
                const auto project = saved.quality.selected.projects[i];
                QCOMPARE(project.projectId, h.expectedProjects[i]);
                QCOMPARE(qc.scalar("SELECT association_id FROM qc_sample_hold_project WHERE slot_token=? AND position=?",
                                   {saved.quality.slotToken, i}).toLongLong(), project.associationId);
                QCOMPARE(catalog.scalar("SELECT association_id FROM sample_local_quality_project WHERE rack_id=? AND hole_number=1 AND position=?",
                                        {rack.id, i}).toLongLong(), project.associationId);
            }
        }
        const QByteArray rows = h.sampleImage(), holds = h.holdImage(true);
        QVERIFY(!rows.isEmpty() && !holds.isEmpty());
        QVERIFY2(h.reopen(), qPrintable(h.failure));
        SampleSnapshot reopened; QVERIFY(h.loadSamples(&reopened));
        SampleHole restored; bool found = false;
        for (const auto &value : reopened.racks) if (value.id == rack.id) { restored = value.holes.first(); found = true; }
        QVERIFY(found); QCOMPARE(restored.sampleType, 2);
        QVERIFY(sameSampleQcSelection(saved.quality.selected, restored.quality.selected));
        QCOMPARE(restored.quality.slotToken, saved.quality.slotToken);
        QCOMPARE(restored.quality.changeToken, saved.quality.changeToken);
        QCOMPARE(taskIds(restored), h.expectedProjects);
        QCOMPARE(h.sampleImage(), rows); QCOMPARE(h.holdImage(true), holds);
        QVERIFY(h.openDetail());
        QVERIFY(h.detail()->findChild<QtMaterialRadioButton *>("radioBoxQC")->isChecked());
        QCOMPARE(h.detail()->findChild<QComboBox *>("cbxControName")->currentData().toLongLong(), h.linkedControlId);
        QCOMPARE(h.detail()->findChild<QComboBox *>("cbxControlBatch")->currentData().toLongLong(), h.linkedBatchId);
        QVERIFY(h.finishDetail(false)); QVERIFY(h.page->prepareToLeave());
        QVERIFY(h.shutdown());
    }

    // 转普通须清身份并释放已证实无用的hold；再转QC仍须原稳定资料、新提交token。
    void qualityAndOrdinaryConversionReleaseAndReacquireProtection()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        QVERIFY(h.makeQualityDraft() && h.savePage());
        const SampleQcIdentity first = h.savedRack().holes.first().quality;
        const QByteArray qualityRows = h.sampleImage();
        QVERIFY(h.makeOrdinaryDraft("B002"));
        QCOMPARE(h.sampleImage(), qualityRows);
        QVERIFY(h.savePage());
        const SampleHole ordinary = h.savedRack().holes.first(); QCOMPARE(ordinary.sampleType, 0);
        QCOMPARE(ordinary.sampleId, QString("B002"));
        QVERIFY(ordinary.quality.selected.datasetId.isEmpty());
        QCOMPARE(ordinary.quality.selected.controlId, qint64(0)); QCOMPARE(ordinary.quality.selected.batchId, qint64(0));
        QVERIFY(ordinary.quality.selected.projects.isEmpty()); QVERIFY(ordinary.quality.slotToken.isEmpty());
        QVERIFY(ordinary.quality.changeToken.isEmpty());
        {
            Database qc(h.qualityPath), catalog(h.catalogPath());
            QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 0);
            QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 0);
            QCOMPARE(catalog.scalar("SELECT COUNT(*) FROM sample_local_quality_hole").toInt(), 0);
            QCOMPARE(catalog.scalar("SELECT COUNT(*) FROM sample_local_quality_project").toInt(), 0);
        }
        QVERIFY(h.reopen()); QCOMPARE(h.savedRack().holes.first().sampleType, 0);
        QVERIFY(h.makeQualityDraft() && h.savePage());
        const SampleQcIdentity again = h.savedRack().holes.first().quality;
        QVERIFY(sameSampleQcSelection(first.selected, again.selected));
        QVERIFY(first.slotToken != again.slotToken); QVERIFY(first.changeToken != again.changeToken);
        Database qc(h.qualityPath);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 1);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 2);
        QVERIFY(h.shutdown());
    }

    // 非当前/前一批次也须被样本引用保护，避免测到旧批次标志限制却误称新保护有效。
    void referencedQualityNamesAndAssociationsAreProtected()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        QVERIFY(h.makeQualityDraft() && h.savePage());
        const QByteArray samples = h.sampleImage(), holds = h.holdImage(true);
        Database qc(h.qualityPath);
        QCOMPARE(qc.scalar("SELECT cur_batch+prv_batch FROM quality_control_batch WHERE id=?", {h.linkedBatchId}).toInt(), 0);
        const auto denied = [&](const std::function<void(qint64)> &action)
        {
            return h.qualityWrite(action, false) && h.lastQualityError.contains(QStringLiteral("样本预设或待定预约引用"))
                && h.holdImage(true) == holds && h.sampleImage() == samples;
        };
        QcControl renamed; renamed.id = h.linkedControlId; renamed.name = "SHOULD_NOT_RENAME"; renamed.manufacturer = "anonymous manufacturer";
        QVERIFY(denied([&](qint64 id) { h.quality->saveControl(id, renamed); }));
        QVERIFY(denied([&](qint64 id) { h.quality->removeControl(id, h.linkedControlId); }));
        QcBatch renamedBatch; renamedBatch.id = h.linkedBatchId; renamedBatch.controlId = h.linkedControlId;
        renamedBatch.name = "SHOULD_NOT_RENAME_BATCH"; renamedBatch.expirationDate = "2027/01/31";
        QVERIFY(denied([&](qint64 id) { h.quality->saveBatch(id, renamedBatch); }));
        QVERIFY(denied([&](qint64 id) { h.quality->removeBatch(id, h.linkedBatchId); }));
        QcProjectRefs reduced;
        for (const auto &ref : h.linkedRefs) if (ref.id != h.expectedProjects.first()) reduced.append(ref);
        QVERIFY(denied([&](qint64 id) { h.quality->setProjects(id, h.linkedControlId, reduced); }));
        QCOMPARE(qc.scalar("SELECT item_name FROM quality_control_list WHERE id=?", {h.linkedControlId}).toString(), QString("QC_LINKED"));
        QCOMPARE(qc.scalar("SELECT batch_name FROM quality_control_batch WHERE id=?", {h.linkedBatchId}).toString(), QString("LINKED_REF"));
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM quality_item_proj WHERE item_id=?", {h.linkedControlId}).toInt(), 3);
        // 非身份字段不应被额外发明的全面禁改规则挡住。
        renamed.name = "QC_LINKED"; renamed.manufacturer = "anonymous updated manufacturer";
        QVERIFY(h.qualityWrite([&](qint64 id) { h.quality->saveControl(id, renamed); }));
        renamedBatch.name = "LINKED_REF"; renamedBatch.expirationDate = "2027/02/28";
        QVERIFY(h.qualityWrite([&](qint64 id) { h.quality->saveBatch(id, renamedBatch); }));
        QCOMPARE(h.holdImage(true), holds); QCOMPARE(h.sampleImage(), samples);
        QVERIFY(h.shutdown());
    }

    // 原版允许空项目QC预设；没有子项目hold也不能让控制品/批次被删改。
    void zeroProjectPresetStillProtectsControlAndBatch()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        QVERIFY(h.makeQualityDraft(true) && h.savePage());
        const SampleHole hole = h.savedRack().holes.first(); QCOMPARE(hole.sampleType, 2);
        QCOMPARE(hole.quality.selected.controlId, h.emptyControlId);
        QVERIFY(hole.tasks.isEmpty()); QVERIFY(hole.quality.selected.projects.isEmpty());
        Database qc(h.qualityPath);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 1);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 0);
        QCOMPARE(qc.scalar("SELECT cur_batch+prv_batch FROM quality_control_batch WHERE id=?", {h.emptyBatchId}).toInt(), 0);
        const QByteArray holds = h.holdImage(true), rows = h.sampleImage();
        QcControl control; control.id = h.emptyControlId; control.name = "NO_EMPTY_RENAME"; control.manufacturer = "anonymous manufacturer";
        QcBatch batch; batch.id = h.emptyBatchId; batch.controlId = h.emptyControlId; batch.name = "NO_EMPTY_BATCH_RENAME"; batch.expirationDate = "2027/01/31";
        for (const auto &action : QList<std::function<void(qint64)>>{
                 [&](qint64 id) { h.quality->saveControl(id, control); },
                 [&](qint64 id) { h.quality->removeControl(id, h.emptyControlId); },
                 [&](qint64 id) { h.quality->saveBatch(id, batch); },
                 [&](qint64 id) { h.quality->removeBatch(id, h.emptyBatchId); }})
        {
            QVERIFY(h.qualityWrite(action, false));
            QVERIFY(h.lastQualityError.contains(QStringLiteral("样本预设或待定预约引用")));
            QCOMPARE(h.holdImage(true), holds); QCOMPARE(h.sampleImage(), rows);
        }
        QVERIFY(h.reopen()); QCOMPARE(h.savedRack().holes.first().sampleType, 2);
        QVERIFY(h.makeOrdinaryDraft("C003") && h.savePage());
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 0);
        QVERIFY(h.qualityWrite([&](qint64 id) { h.quality->removeBatch(id, h.emptyBatchId); }));
        QVERIFY(h.qualityWrite([&](qint64 id) { h.quality->removeControl(id, h.emptyControlId); }));
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM quality_control_list WHERE id=?", {h.emptyControlId}).toInt(), 0);
        QVERIFY(h.shutdown());
    }

    // RAISE(ROLLBACK)发生在删除旧孔/写新孔之后：失败不能丢旧记录或先回收旧保护。
    void rollbackPreservesCommittedRackAndHoldsThenRetries()
    {
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        QVERIFY(h.makeQualityDraft() && h.savePage());
        const SampleHole original = h.savedRack().holes.first();
        const QByteArray rows = h.sampleImage(), holds = h.holdImage();
        QVERIFY(!rows.isEmpty() && !holds.isEmpty());
        Database catalog(h.catalogPath()), qc(h.qualityPath);
        const int abortedBefore = qc.scalar("SELECT COUNT(*) FROM qc_sample_change WHERE outcome='aborted'").toInt();
        QVERIFY(h.makeQualityDraft(true));
        QVERIFY(catalog.exec("CREATE TRIGGER anonymous_qc_rollback BEFORE INSERT ON sample_local_quality_hole "
                             "BEGIN SELECT RAISE(ROLLBACK,'anonymous forced QC hole rollback'); END"));
        QVERIFY2(h.savePage(false), qPrintable(h.failure));
        QVERIFY(h.page->hasUnsavedChanges());
        QVERIFY(h.page->property("lastStatus").toString().contains("anonymous forced QC hole rollback"));
        QCOMPARE(h.sampleImage(), rows); QCOMPARE(h.holdImage(), holds);
        QCOMPARE(qc.scalar("SELECT outcome FROM qc_sample_change WHERE change_token=?", {original.quality.changeToken}).toString(), QString("committed"));
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_change WHERE outcome='pending'").toInt(), 0);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_change WHERE outcome='aborted'").toInt(), abortedBefore + 1);
        QCOMPARE(catalog.scalar("SELECT COUNT(*) FROM sample_local_quality_change").toInt(), 0);
        QVERIFY(catalog.exec("DROP TRIGGER anonymous_qc_rollback"));
        QVERIFY2(h.savePage(), qPrintable(h.failure));
        const SampleHole retried = h.savedRack().holes.first(); QCOMPARE(retried.sampleType, 2);
        QCOMPARE(retried.quality.selected.controlId, h.emptyControlId); QVERIFY(retried.tasks.isEmpty());
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 1);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold WHERE slot_token=?", {original.quality.slotToken}).toInt(), 0);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 0);
        QVERIFY(h.reopen()); QCOMPARE(h.savedRack().holes.first().quality.slotToken, retried.quality.slotToken);
        QVERIFY(h.shutdown());
    }

    void regressedRevisionRejectsFinishAndReconcile_data()
    {
        QTest::addColumn<QString>("sourceOutcome");
        QTest::newRow("pending-source") << QString("pending");
        QTest::newRow("aborted-source") << QString("aborted");
    }

    // 合法匿名恢复状态夹具：不是自然UI故障复现，也不用伪造worker回执。
    void regressedRevisionRejectsFinishAndReconcile()
    {
        QFETCH(QString, sourceOutcome);
        Harness h; QVERIFY2(h.start(), qPrintable(h.failure));
        QVERIFY(h.makeQualityDraft() && h.savePage());
        const SampleRack rack = h.savedRack(); const SampleHole original = rack.holes.first();
        // 用另一普通架的真实提交推进revision；倒退1后旧QC的committed修订仍合法，
        // 拒绝必须来自新增pending/aborted预约门槛，不能被旧committed门槛冒充。
        SampleRack ordinary;
        ordinary.name = "S02";
        for (int number = 1; number <= 8; ++number)
        {
            SampleHole hole; hole.number = number; ordinary.holes.append(hole);
        }
        const qint64 advanceRequest = ++h.sequence;
        QMetaObject::invokeMethod(h.samples, [&h, advanceRequest, ordinary]()
        { h.samples->saveRack(advanceRequest, ordinary); }, Qt::QueuedConnection);
        QVERIFY(waitFor([&]() { return h.sampleSaved.contains(advanceRequest) || h.sampleErrors.contains(advanceRequest); }));
        QVERIFY2(h.sampleSaved.contains(advanceRequest), qPrintable(h.sampleErrors.value(advanceRequest)));
        Database catalog(h.catalogPath()), qc(h.qualityPath);
        const QVariant observed = catalog.scalar("SELECT value FROM catalog_metadata WHERE key='sample_qc_revision'");
        QVERIFY(observed.isValid()); const qint64 revision = observed.toLongLong(); QVERIFY(revision > 0);
        QCOMPARE(qc.scalar("SELECT final_revision FROM qc_sample_change WHERE change_token=?",
                           {original.quality.changeToken}).toLongLong(), revision - 1);
        SampleQcChange change;
        change.changeToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
        change.ownerDatasetId = catalog.scalar("SELECT value FROM catalog_metadata WHERE key='sample_dataset_uuid'").toString();
        change.qualityDatasetId = original.quality.selected.datasetId;
        change.catalogCanonicalPath = QFileInfo(h.catalogPath()).canonicalFilePath();
        change.expectedRevision = revision;
        change.claims.append({QUuid::createUuid().toString(QUuid::WithoutBraces), original.quality.selected});
        QVERIFY(catalog.exec("INSERT INTO sample_local_quality_change VALUES(?,?,?,?,'pending',NULL,'saveRack',?)",
                             {change.changeToken, change.ownerDatasetId, change.qualityDatasetId, revision, rack.id}));
        QMetaObject::invokeMethod(h.quality, [&h, change]() { h.quality->reserveSampleChange(change); }, Qt::QueuedConnection);
        QVERIFY(waitFor([&]() { return h.reservations.contains(change.changeToken); }));
        QVERIFY2(h.reservations.value(change.changeToken).reserved, qPrintable(h.reservations.value(change.changeToken).message));
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 2);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 4);
        QVERIFY(catalog.exec("BEGIN IMMEDIATE"));
        QVERIFY(catalog.exec("UPDATE sample_local_quality_change SET outcome=? WHERE change_token=?", {sourceOutcome, change.changeToken}));
        QVERIFY(catalog.exec("UPDATE catalog_metadata SET value=? WHERE key='sample_qc_revision'", {QString::number(revision - 1)}));
        QVERIFY(catalog.exec("COMMIT"));
        const QByteArray protectedRows = h.holdImage(true), sourceRows = h.sampleImage();
        QVERIFY(!protectedRows.isEmpty() && !sourceRows.isEmpty());
        QMetaObject::invokeMethod(h.quality, [&h, change]() { h.quality->finishSampleChange(change.changeToken, false, -1); }, Qt::QueuedConnection);
        QVERIFY(waitFor([&]() { return h.finishes.contains(change.changeToken); }));
        const auto finish = h.finishes.value(change.changeToken); QVERIFY(!finish.reconciled);
        if (sourceOutcome == "aborted")
            QVERIFY(finish.message.contains(QStringLiteral("修订落后")) || finish.message.contains(QStringLiteral("修订超过")));
        else QVERIFY(finish.message.contains(QStringLiteral("真实源结局仍待定")));
        QCOMPARE(h.holdImage(true), protectedRows); QCOMPARE(h.sampleImage(), sourceRows);
        const int before = h.reconcileCount;
        QMetaObject::invokeMethod(h.quality, [&h, change]() { h.quality->reconcileSampleOwner(change.ownerDatasetId, change.catalogCanonicalPath); }, Qt::QueuedConnection);
        QVERIFY(waitFor([&]() { return h.reconcileCount > before; })); QVERIFY(!h.reconciled);
        QVERIFY(h.reconcileMessage.contains(QStringLiteral("修订落后")) || h.reconcileMessage.contains(QStringLiteral("修订超过")));
        QCOMPARE(h.holdImage(true), protectedRows); QCOMPARE(h.sampleImage(), sourceRows);
        // 精确恢复本匿名夹具的正确修订并明确ABORTED，真实对账才可回收额外预约。
        QVERIFY(catalog.exec("BEGIN IMMEDIATE"));
        QVERIFY(catalog.exec("UPDATE catalog_metadata SET value=? WHERE key='sample_qc_revision'", {QString::number(revision)}));
        QVERIFY(catalog.exec("UPDATE sample_local_quality_change SET outcome='aborted' WHERE change_token=?", {change.changeToken}));
        QVERIFY(catalog.exec("COMMIT"));
        h.finishes.remove(change.changeToken);
        QMetaObject::invokeMethod(h.quality, [&h, change]() { h.quality->finishSampleChange(change.changeToken, false, -1); }, Qt::QueuedConnection);
        QVERIFY(waitFor([&]() { return h.finishes.contains(change.changeToken); }));
        QVERIFY2(h.finishes.value(change.changeToken).reconciled, qPrintable(h.finishes.value(change.changeToken).message));
        QVERIFY(waitFor([&]() { return catalog.scalar("SELECT COUNT(*) FROM sample_local_quality_change WHERE change_token=?", {change.changeToken}).toInt() == 0; }));
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold").toInt(), 1);
        QCOMPARE(qc.scalar("SELECT COUNT(*) FROM qc_sample_hold_project").toInt(), 2);
        QCOMPARE(qc.scalar("SELECT slot_token FROM qc_sample_hold").toString(), original.quality.slotToken);
        QCOMPARE(qc.scalar("SELECT outcome FROM qc_sample_change WHERE change_token=?", {change.changeToken}).toString(), QString("aborted"));
        const int recovery = h.reconcileCount;
        QMetaObject::invokeMethod(h.quality, [&h, change]() { h.quality->reconcileSampleOwner(change.ownerDatasetId, change.catalogCanonicalPath); }, Qt::QueuedConnection);
        QVERIFY(waitFor([&]() { return h.reconcileCount > recovery; })); QVERIFY(h.reconciled);
        QVERIFY(h.shutdown());
    }
};

QTEST_MAIN(SampleQcWorkflowTest)
#include "sample_qc_workflow_test.moc"
