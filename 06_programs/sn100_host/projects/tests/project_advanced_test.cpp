#include "project_page.h"
#include <QComboBox>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QScrollArea>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <memory>

class ProjectAdvancedTest : public QObject
{
    Q_OBJECT
    QThread worker;
    ProjectsStore *store = nullptr;
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<ProjectPage> page;
    std::unique_ptr<QSignalSpy> ready, snapshots, success, errors, held, holdErrors, references;
    qint64 request = 100;
    AccountSession engineer() const
    {
        AccountSession value;
        value.name = QStringLiteral("offlineEngineer");
        value.role = 4;
        value.authenticated = true;
        return value;
    }
    void session(AccountSession value)
    {
        QMetaObject::invokeMethod(store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, value));
    }
    void load()
    {
        snapshots->clear();
        QMetaObject::invokeMethod(store, "load", Qt::QueuedConnection, Q_ARG(qint64, ++request));
        QTRY_COMPARE_WITH_TIMEOUT(snapshots->count(), 1, 3000);
    }
    ProjectRecords projects() const { return qvariant_cast<ProjectRecords>(snapshots->last().at(1)); }
    void save(ProjectRecord value)
    {
        QMetaObject::invokeMethod(store, "save", Qt::QueuedConnection, Q_ARG(qint64, ++request), Q_ARG(ProjectRecord, value));
    }
    void hold(QString owner, QString key, qint64 id)
    {
        QMetaObject::invokeMethod(store, "holdProjectReference", Qt::QueuedConnection,
            Q_ARG(qint64, ++request), Q_ARG(QString, owner), Q_ARG(QString, key), Q_ARG(qint64, id));
    }
    void release(QString owner, QString key)
    {
        QMetaObject::invokeMethod(store, "releaseProjectReference", Qt::QueuedConnection,
            Q_ARG(qint64, ++request), Q_ARG(QString, owner), Q_ARG(QString, key));
    }
    ProjectAdvanced fixture() const
    {
        ProjectAdvanced value;
        value.configured = true;
        value.fields = {{"min_test_time", "5"}, {"stopTime", "20"}, {"max_test_time", "40"},
                        {"sample_change_clean", "8"}, {"first_light", "405nm"}, {"second_light", "570nm"}};
        return value;
    }
    void reopen()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        ready->clear();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory->path()), Q_ARG(bool, false));
        QTRY_COMPARE_WITH_TIMEOUT(ready->count(), 1, 3000);
        session(engineer());
        load();
    }
  private slots:
    void initTestCase() { ProjectsStore::registerTypes(); }
    void init()
    {
        directory.reset(new QTemporaryDir(QCoreApplication::applicationDirPath() + "/advanced-runtime-XXXXXX"));
        QVERIFY(directory->isValid());
        store = new ProjectsStore;
        store->moveToThread(&worker);
        connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        ready.reset(new QSignalSpy(store, &ProjectsStore::ready));
        snapshots.reset(new QSignalSpy(store, &ProjectsStore::snapshotReady));
        success.reset(new QSignalSpy(store, &ProjectsStore::succeeded));
        errors.reset(new QSignalSpy(store, &ProjectsStore::error));
        held.reset(new QSignalSpy(store, &ProjectsStore::projectReferenceSucceeded));
        holdErrors.reset(new QSignalSpy(store, &ProjectsStore::projectReferenceError));
        references.reset(new QSignalSpy(store, &ProjectsStore::projectReferencesReady));
        worker.start();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory->path()), Q_ARG(bool, true));
        QTRY_COMPARE_WITH_TIMEOUT(ready->count(), 1, 3000);
        session(engineer());
        load();
        QCOMPARE(errors->count(), 0);
    }
    void cleanup()
    {
        page.reset();
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        QVERIFY(worker.wait(3000));
    }
    void scalarSaveCopyReopenPreservesExistingContract()
    {
        auto value = projects().first();
        QVERIFY(!value.advanced.configured);
        value.advanced = fixture();
        save(value);
        QTRY_COMPARE(success->count(), 1);
        reopen();
        const auto reopened = projects().first();
        QCOMPARE(reopened.id, value.id);
        QCOMPARE(reopened.instrumentProjectId, value.instrumentProjectId);
        QCOMPARE(reopened.results[0].unit, value.results[0].unit);
        QCOMPARE(reopened.results[1].decimals, value.results[1].decimals);
        QCOMPARE(reopened.sampleRates[0].sampleRate, value.sampleRates[0].sampleRate);
        QCOMPARE(reopened.advanced.fields, value.advanced.fields);
        QMetaObject::invokeMethod(store, "copy", Qt::QueuedConnection, Q_ARG(qint64, ++request),
            Q_ARG(qint64, value.id), Q_ARG(QString, QStringLiteral("COPY_LOCAL")));
        QTRY_COMPARE(success->count(), 2);
        load();
        QCOMPARE(projects().last().advanced.fields, value.advanced.fields);
        qInfo() << "ADVANCED_SAVE_REOPEN" << reopened.id << reopened.advanced.fields
                << "result_unit" << reopened.results[0].unit << "sample_rate" << reopened.sampleRates[0].sampleRate;
    }
    void emptyIllegalAndUnknownInputsAreRejected()
    {
        auto value = projects().first();
        value.advanced = fixture();
        value.advanced.fields["min_test_time"] = "";
        save(value); QTRY_COMPARE(errors->count(), 1);
        value.advanced = fixture();
        value.advanced.fields["stopTime"] = "NaN";
        save(value); QTRY_COMPARE(errors->count(), 2);
        value.advanced = fixture();
        value.advanced.fields["max_test_time"] = "39";
        save(value); QTRY_COMPARE(errors->count(), 3);
        value.advanced = fixture();
        value.advanced.fields["send_device_command"] = "1";
        save(value); QTRY_COMPARE(errors->count(), 4);
        value.advanced = fixture(); value.advanced.fields["sample_change_clean"] = "8\n";
        save(value); QTRY_COMPARE(errors->count(), 5);
        load();
        QVERIFY(!projects().first().advanced.configured);
        QCOMPARE(success->count(), 0);
    }
    void actionRowsSaveInOrderValidateAndProtectReagentReferences()
    {
        auto value = projects().first(); value.advanced = fixture();
        for (const auto &rate : value.sampleRates)
        {
            value.advanced.samples.append({{"sample_rate", rate.sampleRate}, {"sample_add_mode", "none"},
                {"IBS_amount_ul", "0"}, {"sample_amount_ul", "5"}, {"mix_amount_ul", "0"},
                {"second_IBS_amount_ul", "0"}, {"action_amount_ul", "5"}, {"blender_type", "none"},
                {"blender_time_sec", "0"}, {"calibration_option", "0"}, {"before_clean_frequency", "0"},
                {"before_clean_amount_ul", "0"}, {"before_clean_name", "none"}, {"after_clean_frequency", "0"}});
        }
        ProjectActionFields step = {{"reagent_name", "ANON_R"}, {"reagent_warmup_length", "3.5"},
            {"reagent_amount_ul", "15.5"}, {"needle_select", "1"}, {"blender_type", "none"},
            {"blender_time_sec", "0"}, {"before_clean_frequency", "0"}, {"before_clean_amount_ul", "0"},
            {"before_clean_name", "none"}, {"after_clean_frequency", "0"}};
        value.advanced.reagents = {step, step}; // Repeated reagent steps are legal and ordered.
        save(value); QTRY_COMPARE(errors->count(), 1); // Unknown reagent cannot be guessed.
        const QString connection = QStringLiteral("advanced-action-catalog");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE reagent_list(id INTEGER PRIMARY KEY,reagent_name TEXT UNIQUE,reagent_type TEXT,valid INTEGER)"));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO reagent_list VALUES(1,'ANON_R','试剂',1)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        save(value); QTRY_COMPARE(success->count(), 1);
        reopen();
        const auto reopened = projects().first();
        QCOMPARE(reopened.advanced.samples.size(), 2);
        QCOMPARE(reopened.advanced.reagents.size(), 2);
        QCOMPARE(reopened.advanced.reagents[1].value("reagent_amount_ul"), QStringLiteral("15.5"));
        QCOMPARE(reopened.sampleRates[0].sampleOption, true);
        QCOMPARE(reopened.advanced.samples[0].value("sample_option"), QStringLiteral("1"));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("PRAGMA foreign_keys=ON"));
            QVERIFY(!query.exec("DELETE FROM reagent_list WHERE id=1"));
            QVERIFY(query.exec("SELECT count(*) FROM project_action_reagent_reference")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 1); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        auto bad = value; bad.advanced.samples[0]["action_amount_ul"] = "0";
        save(bad); QTRY_COMPARE(errors->count(), 2);
        bad = value; bad.advanced.samples[0]["sample_rate"] = "RENAMED";
        save(bad); QTRY_COMPARE(errors->count(), 3);
        bad = value; bad.advanced.reagents[0]["reagent_amount_ul"] = "";
        save(bad); QTRY_COMPARE(errors->count(), 4);
        page.reset(new ProjectPage(store)); page->setSession(engineer());
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory->path()), Q_ARG(bool, false));
        QTRY_VERIFY(page->isReady() && !page->isBusy());
        auto *sampleTable = page->findChild<QTableWidget *>("sampleRateTable");
        auto *reagentTable = page->findChild<QTableWidget *>("reagentAddingTable");
        QCOMPARE(sampleTable->rowCount(), 2); QCOMPARE(reagentTable->rowCount(), 2);
        auto *sampleVolume = qobject_cast<QLineEdit *>(sampleTable->cellWidget(0, 3));
        auto *actionVolume = qobject_cast<QLineEdit *>(sampleTable->cellWidget(0, 6));
        auto *reagentVolume = qobject_cast<QLineEdit *>(reagentTable->cellWidget(1, 2));
        QVERIFY(sampleVolume && actionVolume && reagentVolume);
        QCOMPARE(sampleVolume->text(), QStringLiteral("5"));
        QCOMPARE(reagentVolume->text(), QStringLiteral("15.5"));
        sampleVolume->setText(QStringLiteral("6")); actionVolume->setText(QStringLiteral("6"));
        reagentVolume->setText(QStringLiteral("16.5"));
        QVERIFY(page->hasUnsavedChanges());
        QTest::mouseClick(page->findChild<QPushButton *>("btnSave"), Qt::LeftButton);
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges());
        QCOMPARE(page->projects().first().advanced.samples[0].value("sample_amount_ul"), QStringLiteral("6"));
        QCOMPARE(page->projects().first().advanced.reagents[1].value("reagent_amount_ul"), QStringLiteral("16.5"));
        if (qEnvironmentVariableIsSet("SN100_PROJECT_ADVANCED_SCREENSHOT"))
        {
            page->show();
            QTest::mouseClick(page->findChild<QPushButton *>("btnSampleAdd"), Qt::LeftButton);
            QTest::qWait(100);
            QVERIFY(page->grab().save(qEnvironmentVariable("SN100_PROJECT_ADVANCED_SCREENSHOT") + QStringLiteral(".populated.png")));
            QTest::mouseClick(page->findChild<QPushButton *>("btnReagentAdd"), Qt::LeftButton);
            QTest::qWait(100);
            QVERIFY(page->grab().save(qEnvironmentVariable("SN100_PROJECT_ADVANCED_SCREENSHOT") + QStringLiteral(".populated-actions.png")));
        }
        page.reset(); reopen();
        QCOMPARE(projects().first().advanced.samples[0].value("sample_amount_ul"), QStringLiteral("6"));
        QCOMPARE(projects().first().advanced.reagents[1].value("reagent_amount_ul"), QStringLiteral("16.5"));
        qInfo() << "ACTION_REOPEN" << reopened.advanced.samples.size() << reopened.advanced.reagents.size()
                << "ordered steps; 15.5uL; GUI sample6uL/reagent16.5uL persisted/reopened; unknown reagent/empty/action-volume mismatch refused; reagent FK delete blocked";
    }
    void durableReferencesProtectSeparateDatabaseRecords()
    {
        QMetaObject::invokeMethod(store, "copy", Qt::QueuedConnection, Q_ARG(qint64, ++request),
            Q_ARG(qint64, qint64(1)), Q_ARG(QString, QStringLiteral("EXTERNAL")));
        QTRY_COMPARE(success->count(), 1);
        load();
        auto value = projects().last();
        hold("calibration", "anonymous-record-1", value.id);
        QTRY_COMPARE(held->count(), 1);
        // Simulate the separate database commit only after the catalog hold.
        const QString connection = QStringLiteral("advanced-external");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("anonymous-external.db"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TABLE records(id TEXT PRIMARY KEY,project_id INTEGER,stored_unit TEXT,stored_value TEXT)"));
            query.prepare("INSERT INTO records VALUES('anonymous-record-1',?,'old-unit','12.345')");
            query.addBindValue(value.id); QVERIFY(query.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        hold("calibration", "anonymous-record-1", value.id);
        QTRY_COMPARE(held->count(), 2); // retry is idempotent
        hold("calibration", "anonymous-record-1", 1);
        QTRY_COMPARE(holdErrors->count(), 1); // cannot rebind a stable external key
        reopen();
        auto unitChange = projects().last();
        unitChange.results[1].unit = QStringLiteral("a.u.x10");
        save(unitChange); QTRY_COMPARE(success->count(), 2);
        value.name = "BLOCKED";
        save(value); QTRY_COMPARE(errors->count(), 1);
        QMetaObject::invokeMethod(store, "remove", Qt::QueuedConnection, Q_ARG(qint64, ++request), Q_ARG(qint64, value.id));
        QTRY_COMPARE(errors->count(), 2);
        QMetaObject::invokeMethod(store, "checkProjectReferences", Qt::QueuedConnection, Q_ARG(qint64, ++request), Q_ARG(qint64, value.id));
        QTRY_COMPARE(references->count(), 1);
        QCOMPARE(references->last().at(2).toBool(), true);
        QCOMPARE(references->last().at(3).toStringList(), QStringList({"calibration:anonymous-record-1"}));
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("anonymous-external.db")); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("SELECT stored_unit,stored_value FROM records")); QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QString("old-unit")); QCOMPARE(query.value(1).toString(), QString("12.345"));
            QVERIFY(query.exec("DELETE FROM records")); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        release("calibration", "anonymous-record-1"); QTRY_COMPARE(held->count(), 3);
        save(value); QTRY_COMPARE(success->count(), 3);
        qInfo() << "CROSS_DB_REFERENCE" << value.id << "hold persisted; rename/delete refused; external old-unit/12.345 preserved; explicit release permits rename";
    }
    void referencePermissionMissingProjectAndClosedStoreFailClosed()
    {
        hold("quality", "missing", 99999); QTRY_COMPARE(holdErrors->count(), 1);
        session(AccountSession());
        hold("quality", "guest", 1); QTRY_COMPARE(holdErrors->count(), 2);
        auto value = projects().first(); value.advanced = fixture(); save(value); QTRY_COMPARE(errors->count(), 1);
        auto operatorSession = engineer(); operatorSession.role = 1; session(operatorSession);
        hold("quality", "operator-record", 1); QTRY_COMPARE(held->count(), 1);
        release("quality", "operator-record"); QTRY_COMPARE(held->count(), 2);
        save(value); QTRY_COMPARE(errors->count(), 2); // Reference bookkeeping never grants project-edit permission.
        hold("quality\n", "newline-module", 1); QTRY_COMPARE(holdErrors->count(), 3);
        session(engineer());
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(store, "checkProjectReferences", Qt::QueuedConnection,
            Q_ARG(qint64, ++request), Q_ARG(qint64, qint64(1)));
        QTRY_COMPARE(holdErrors->count(), 4);
        QCOMPARE(references->count(), 0);
    }
    void lockFailureAndRetryKeepCommittedData()
    {
        auto value = projects().first(); value.advanced = fixture();
        const QString connection = QStringLiteral("advanced-lock");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("BEGIN EXCLUSIVE"));
            int ticks = 0; QTimer timer; timer.setInterval(10);
            connect(&timer, &QTimer::timeout, this, [&ticks]() { ++ticks; }); timer.start();
            save(value); QTRY_COMPARE_WITH_TIMEOUT(errors->count(), 1, 3000);
            QVERIFY(ticks > 20); timer.stop();
            QVERIFY(query.exec("ROLLBACK")); db.close();
            qInfo() << "ADVANCED_LOCK_RECOVERY_GUI_TICKS" << ticks;
        }
        QSqlDatabase::removeDatabase(connection);
        load(); QVERIFY(!projects().first().advanced.configured);
        save(value); QTRY_COMPARE(success->count(), 1);
        reopen(); QCOMPARE(projects().first().advanced.fields, value.advanced.fields);
    }
    void oldCatalogUpgradeAndWriteFailureRollback()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        const QString connection = QStringLiteral("advanced-upgrade");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("DROP TABLE project_advanced"));
            QVERIFY(query.exec("DROP TABLE catalog_project_reference"));
            QVERIFY(query.exec("DELETE FROM catalog_metadata WHERE key='project_advanced_schema'"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        reopen();
        QCOMPARE(projects().first().id, qint64(1));
        QCOMPARE(projects().first().results[0].unit, QStringLiteral("s"));
        QVERIFY(!projects().first().advanced.configured);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER reject_advanced BEFORE INSERT ON project_advanced BEGIN SELECT RAISE(ABORT,'injected write failure'); END"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        auto value = projects().first(); value.advanced = fixture(); value.fullName = QStringLiteral("必须回滚");
        save(value); QTRY_COMPARE(errors->count(), 1);
        load(); QCOMPARE(projects().first().fullName, QStringLiteral("离线项目A"));
        QVERIFY(!projects().first().advanced.configured);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("DROP TRIGGER reject_advanced")); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        save(value); QTRY_COMPARE(success->count(), 1);
        reopen(); QCOMPARE(projects().first().fullName, value.fullName);
        QCOMPARE(projects().first().advanced.fields, value.advanced.fields);
        qInfo() << "OLD_CATALOG_UPGRADE baseline IDs/results preserved; injected write failure rolled back all fields; retry/reopen succeeded";
    }
    void guiCancelDraftBusyAndLocalSave()
    {
        page.reset(new ProjectPage(store)); page->setSession(engineer());
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory->path()), Q_ARG(bool, false));
        QTRY_VERIFY(page->isReady() && !page->isBusy());
        auto setText = [this](const char *name, const QString &text)
        {
            auto *edit = page->findChild<QLineEdit *>(name); QVERIFY(edit); edit->setText(text);
        };
        setText("min_test_time", "5"); setText("stopTime", "20"); setText("max_test_time", "40");
        setText("sample_change_clean", "8");
        QVERIFY(page->hasUnsavedChanges());
        QTimer::singleShot(0, []() { auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); QVERIFY(dialog); dialog->done(QMessageBox::Cancel); });
        QVERIFY(!page->prepareToLeave());
        QTest::mouseClick(page->findChild<QPushButton *>("btnCancel"), Qt::LeftButton);
        QVERIFY(!page->hasUnsavedChanges());
        QVERIFY(!page->projects().first().advanced.configured);
        setText("min_test_time", "5"); setText("stopTime", "20"); setText("max_test_time", "40");
        setText("sample_change_clean", "8");
        page->findChild<QComboBox *>("first_light")->setCurrentText(QStringLiteral("405nm"));
        page->findChild<QComboBox *>("second_light")->setCurrentText(QStringLiteral("570nm"));
        QTest::mouseClick(page->findChild<QPushButton *>("btnSave"), Qt::LeftButton);
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges());
        QVERIFY(page->projects().first().advanced.configured);
        QCOMPARE(page->projects().first().advanced.fields.value("max_test_time"), QStringLiteral("40"));
        const QString lockConnection = QStringLiteral("advanced-page-lock");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", lockConnection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery lock(db); QVERIFY(lock.exec("BEGIN EXCLUSIVE"));
            setText("max_test_time", "41");
            QTest::mouseClick(page->findChild<QPushButton *>("btnSave"), Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(!page->isBusy(), 3000);
            QVERIFY(page->hasUnsavedChanges());
            QCOMPARE(page->findChild<QLineEdit *>("max_test_time")->text(), QStringLiteral("41"));
            QCOMPARE(page->projects().first().advanced.fields.value("max_test_time"), QStringLiteral("40"));
            QVERIFY(lock.exec("ROLLBACK")); db.close();
        }
        QSqlDatabase::removeDatabase(lockConnection);
        QTest::mouseClick(page->findChild<QPushButton *>("btnSave"), Qt::LeftButton);
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges());
        QCOMPARE(page->projects().first().advanced.fields.value("max_test_time"), QStringLiteral("41"));
        page->reload(); QVERIFY(page->isBusy()); QVERIFY(!page->prepareToLeave());
        QTRY_VERIFY(!page->isBusy());
        AccountSession guest; page->setSession(guest);
        QVERIFY(page->findChild<QLineEdit *>("min_test_time")->isReadOnly());
        QVERIFY(!page->findChild<QPushButton *>("btnSave")->isEnabled());
        if (qEnvironmentVariableIsSet("SN100_PROJECT_ADVANCED_SCREENSHOT"))
        {
            page->show();
            QTest::mouseClick(page->findChild<QPushButton *>("btnSampleAdd"), Qt::LeftButton);
            QTest::qWait(100);
            QVERIFY(page->grab().save(qEnvironmentVariable("SN100_PROJECT_ADVANCED_SCREENSHOT")));
            QTest::mouseClick(page->findChild<QPushButton *>("btnReagentAdd"), Qt::LeftButton);
            QTest::qWait(100);
            QVERIFY(page->grab().save(qEnvironmentVariable("SN100_PROJECT_ADVANCED_SCREENSHOT") + QStringLiteral(".sample-actions.png")));
            QScrollArea viewport;
            viewport.setWidgetResizable(false);
            viewport.setWidget(page.get());
            viewport.resize(1600, 900);
            viewport.show();
            QTest::qWait(100);
            QVERIFY(viewport.grab().save(qEnvironmentVariable("SN100_PROJECT_ADVANCED_SCREENSHOT") + QStringLiteral(".1600.png")));
            viewport.takeWidget();
            page->setParent(nullptr);
        }
        QVERIFY(page->close());
    }
    void corruptStoredPayloadFailsWithoutDroppingUnknownData()
    {
        auto value = projects().first(); value.advanced = fixture(); save(value);
        QTRY_COMPARE(success->count(), 1);
        const QString connection = QStringLiteral("advanced-corrupt");
        QString original;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("SELECT payload FROM project_advanced WHERE project_id=1")); QVERIFY(query.next());
            original = query.value(0).toString(); query.finish();
            query.prepare("UPDATE project_advanced SET payload=? WHERE project_id=1");
            QString corrupt = original; corrupt.chop(1); corrupt += ",\"unknown_future_field\":1}";
            query.addBindValue(corrupt); QVERIFY(query.exec()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        snapshots->clear();
        QMetaObject::invokeMethod(store, "load", Qt::QueuedConnection, Q_ARG(qint64, ++request));
        QTRY_COMPARE(errors->count(), 1);
        QCOMPARE(snapshots->count(), 0);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(directory->filePath("catalog.db")); QVERIFY(db.open());
            QSqlQuery query(db); QVERIFY(query.exec("SELECT payload FROM project_advanced WHERE project_id=1")); QVERIFY(query.next());
            QVERIFY(query.value(0).toString().contains("unknown_future_field")); query.finish();
            query.prepare("UPDATE project_advanced SET payload=? WHERE project_id=1");
            query.addBindValue(original); QVERIFY(query.exec()); db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        load(); QCOMPARE(projects().first().advanced.fields, value.advanced.fields);
    }
};
QTEST_MAIN(ProjectAdvancedTest)
#include "project_advanced_test.moc"
