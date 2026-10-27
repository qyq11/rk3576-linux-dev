#include "projects/projects_store.h"
#include "reagents_page.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <memory>

class ReagentBatchTest : public QObject
{
    Q_OBJECT
    QThread worker_;
    ReagentStore* store_ = nullptr;
    std::unique_ptr<QTemporaryDir> directory_;
    QString catalog_;
    ReagentSnapshot snapshot_;
    AccountSession engineer_;
    ReagentBatch batch_;
    ProjectRecord project_;
    int saved_ = 0, errors_ = 0, loaded_ = 0, referenceSaved_ = 0, referenceErrors_ = 0;
    QString message_;
    bool wait(int* count, int old)
    {
        QElapsedTimer time;
        time.start();
        while (*count == old && time.elapsed() < 4000)
            QTest::qWait(5);
        return *count > old;
    }
    bool reload()
    {
        const int old = loaded_;
        QMetaObject::invokeMethod(store_, "load", Qt::QueuedConnection);
        return wait(&loaded_, old);
    }
    bool save(ReagentBatch batch, bool expected = true)
    {
        const int old = expected ? saved_ : errors_;
        QMetaObject::invokeMethod(store_, "saveBatch", Qt::QueuedConnection, Q_ARG(ReagentBatch, batch));
        return wait(expected ? &saved_ : &errors_, old);
    }
    bool sql(const QString& statement)
    {
        const QString connection = "batch_test_sql";
        bool success;
        {
            QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connection);
            database.setDatabaseName(catalog_ + "/catalog.db");
            success = database.open();
            if (success)
            {
                QSqlQuery query(database);
                success = query.exec(statement);
                if (!success)
                    qWarning() << query.lastError().text();
            }
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);
        return success;
    }
    bool hold(QString module, QString key, qint64 id, bool expected = true)
    {
        const int old = expected ? referenceSaved_ : referenceErrors_;
        QMetaObject::invokeMethod(store_, "holdBatchReference", Qt::QueuedConnection, Q_ARG(qint64, 101),
            Q_ARG(QString, module), Q_ARG(QString, key), Q_ARG(qint64, id));
        return wait(expected ? &referenceSaved_ : &referenceErrors_, old);
    }
    QString scalar(const QString& statement)
    {
        QString value;
        {
            QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", "batch_scalar");
            database.setDatabaseName(catalog_ + "/catalog.db");
            if (database.open())
            {
                QSqlQuery query(database);
                if (query.exec(statement) && query.next())
                    value = query.value(0).toString();
            }
            database.close();
        }
        QSqlDatabase::removeDatabase("batch_scalar");
        return value;
    }
    void release(QString module, QString key)
    {
        const int old = referenceSaved_;
        QMetaObject::invokeMethod(store_, "releaseBatchReference", Qt::QueuedConnection, Q_ARG(qint64, 102),
            Q_ARG(QString, module), Q_ARG(QString, key));
        QVERIFY(wait(&referenceSaved_, old));
    }
private slots:
    void initTestCase()
    {
        ProjectsStore::registerTypes();
        ReagentStore::registerTypes();
        const QString runtime = qEnvironmentVariable("SN100_BATCH_TEST_ROOT");
        QVERIFY2(QDir::isAbsolutePath(runtime), "SN100_BATCH_TEST_ROOT must explicitly name this test's isolated temp root");
        QVERIFY(QDir().mkpath(runtime));
        directory_.reset(new QTemporaryDir(runtime + "/batch-XXXXXX"));
        QVERIFY(directory_->isValid());
        directory_->setAutoRemove(false);
        catalog_ = directory_->path() + "/catalog";
        qInfo().noquote() << "ANONYMOUS_RUNTIME" << catalog_;
        engineer_.name = "offline_engineer";
        engineer_.role = 4;
        engineer_.authenticated = true;
        ProjectsStore projects;
        projects.initialize(catalog_, true);
        projects.setSession(engineer_);
        QSignalSpy data(&projects, &ProjectsStore::snapshotReady);
        projects.load(1);
        QCOMPARE(data.count(), 1);
        project_ = qvariant_cast<ProjectRecords>(data.takeFirst().at(1)).first();
        project_.name = "PT_ANON";
        QSignalSpy saved(&projects, &ProjectsStore::succeeded);
        projects.save(2, project_);
        QCOMPARE(saved.count(), 1);
        projects.close();
        store_ = new ReagentStore;
        store_->moveToThread(&worker_);
        connect(&worker_, &QThread::finished, store_, &QObject::deleteLater);
        connect(store_, &ReagentStore::loaded, this, [this](ReagentSnapshot snapshot) { snapshot_ = snapshot; ++loaded_; });
        connect(store_, &ReagentStore::succeeded, this, [this](QString) { ++saved_; });
        connect(store_, &ReagentStore::error, this, [this](QString operation, QString message)
        { message_ = message; ++errors_; qInfo().noquote() << "EXPECTED_OR_ACTUAL_ERROR" << operation << message; });
        connect(store_, &ReagentStore::batchReferenceSucceeded, this, [this](qint64, QString) { ++referenceSaved_; });
        connect(store_, &ReagentStore::batchReferenceError, this, [this](qint64, QString, QString message)
        { message_ = message; ++referenceErrors_; qInfo().noquote() << "REFERENCE_ERROR" << message; });
        worker_.start();
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(wait(&loaded_, 0));
        QCOMPARE(errors_, 0);
        QMetaObject::invokeMethod(store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, engineer_));
    }
    void saveReopenAndLegacyCompatibility()
    {
        batch_.projectId = project_.id;
        batch_.code = "LOT_A";
        batch_.batchGroup = "LOT_A|LOT_B";
        batch_.metadataEdited = true;
        batch_.isiValue = "1.2345";
        batch_.normalValue = "12.3400";
        QVERIFY(save(batch_));
        QVERIFY(reload());
        QCOMPARE(snapshot_.batches.size(), 1);
        batch_ = snapshot_.batches.first();
        QVERIFY(batch_.id > 0 && batch_.hasMetadata);
        QCOMPARE(batch_.isiValue, QString("1.2345"));
        QCOMPARE(batch_.normalValue, QString("12.3400"));
        QVERIFY(!batch_.metadataEdited);
        ReagentBatch oldCaller;
        oldCaller.id = batch_.id;
        oldCaller.projectId = batch_.projectId;
        oldCaller.code = batch_.code;
        oldCaller.batchGroup = batch_.batchGroup;
        QVERIFY(save(oldCaller));
        QMetaObject::invokeMethod(store_, "close", Qt::BlockingQueuedConnection);
        const int old = loaded_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(wait(&loaded_, old));
        QCOMPARE(snapshot_.batches.first().normalValue, QString("12.3400"));
        qInfo() << "COMMITTED_BATCH" << batch_.id << batch_.projectId << batch_.batchGroup << batch_.isiValue << batch_.normalValue;
    }
    void emptyIllegalAndDuplicate()
    {
        for (const QString& value : {QString(), QString("-1"), QString("NaN"), QString("1e2"), QString("10000"),
                 QString("9999.0001"), QString("1.23456"), QString("01.2"), QString(" 1"), QString("1 "), QString("1\n")})
        {
            ReagentBatch invalid = batch_;
            invalid.metadataEdited = true;
            invalid.isiValue = value;
            QVERIFY(save(invalid, false));
            qInfo() << "REJECTED_ISI_INPUT" << value;
        }
        ReagentBatch invalid = batch_;
        invalid.metadataEdited = true;
        invalid.normalValue.clear();
        QVERIFY(save(invalid, false));
        invalid = batch_;
        invalid.id = 0;
        QVERIFY(save(invalid, false));
        invalid.code = "ANOTHER";
        QVERIFY(save(invalid, false)); // member already belongs to LOT_A's group
        invalid.batchGroup = "X||Y";
        QVERIFY(save(invalid, false));
        invalid.batchGroup = "X|X";
        QVERIFY(save(invalid, false));
        invalid.batchGroup = "X| Y";
        QVERIFY(save(invalid, false));
        QVERIFY(reload());
        QCOMPARE(snapshot_.batches.size(), 1);
        QCOMPARE(snapshot_.batches.first().isiValue, batch_.isiValue);
    }
    void reservationAndMutationProtection()
    {
        QVERIFY(hold("calibration", "curve/17", batch_.id));
        // Two independent databases: reserve first, then observe a real failed
        // external commit. No SQLite FK/transaction spans these files.
        QSqlDatabase external = QSqlDatabase::addDatabase("QSQLITE", "batch_external_fixture");
        external.setDatabaseName(directory_->path() + "/anonymous_external.db");
        QVERIFY(external.open());
        {
            QSqlQuery query(external);
            QVERIFY(query.exec("CREATE TABLE external_record(id INTEGER PRIMARY KEY,batch_id INTEGER NOT NULL,payload TEXT NOT NULL)"));
            QVERIFY(!query.exec(QString("INSERT INTO external_record(id,batch_id,payload) VALUES(17,%1,NULL)").arg(batch_.id)));
            QVERIFY(query.exec("SELECT COUNT(*) FROM external_record"));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 0);
            qInfo() << "EXTERNAL_WRITE_FAILED hold intentionally retained" << batch_.id;
        }
        QVERIFY(hold("calibration", "curve/17", batch_.id)); // retry is idempotent
        QVERIFY(hold("calibration", "curve/17", batch_.id + 999, false));
        QVERIFY(hold("calibration", "missing", batch_.id + 999, false));
        QVERIFY(hold("wrong module", "curve/17", batch_.id, false));
        QVERIFY(hold("calibration\n", "newline", batch_.id, false));
        QSignalSpy references(store_, &ReagentStore::batchReferencesReady);
        QMetaObject::invokeMethod(store_, "checkBatchReferences", Qt::QueuedConnection, Q_ARG(qint64, 103), Q_ARG(qint64, batch_.id));
        QTRY_COMPARE(references.count(), 1);
        const auto result = references.takeFirst();
        QVERIFY(result.at(2).toBool());
        QCOMPARE(qvariant_cast<ReagentBatchReferences>(result.at(3)).size(), 1);
        ReagentBatch renamed = batch_;
        renamed.code = "RENAMED";
        QVERIFY(save(renamed, false));
        renamed = batch_;
        renamed.batchGroup = "NEW_MEMBER";
        QVERIFY(save(renamed, false));
        int old = errors_;
        QMetaObject::invokeMethod(store_, "removeBatch", Qt::QueuedConnection, Q_ARG(qint64, batch_.id));
        QVERIFY(wait(&errors_, old));
        QMetaObject::invokeMethod(store_, "close", Qt::BlockingQueuedConnection);
        old = loaded_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(wait(&loaded_, old));
        // A failed/absent external save must leave the durable hold after restart.
        renamed = batch_;
        renamed.code = "AFTER_RESTART";
        QVERIFY(save(renamed, false));
        {
            QSqlQuery query(external);
            QVERIFY(query.exec(QString("INSERT INTO external_record(id,batch_id,payload) VALUES(17,%1,'anonymous')").arg(batch_.id)));
            QVERIFY(query.exec("DELETE FROM external_record WHERE id=17"));
        }
        external.close();
        external = QSqlDatabase();
        QSqlDatabase::removeDatabase("batch_external_fixture");
        release("calibration", "curve/17");
        QVERIFY(save(batch_));
    }
    void projectActionReagentProtection()
    {
        ReagentRecord reagent;
        reagent.name = "ACTION_REAGENT";
        int old = saved_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, reagent));
        QVERIFY(wait(&saved_, old));
        QVERIFY(reload());
        reagent = snapshot_.reagents.first();
        QVERIFY(sql(QString("INSERT INTO project_action_reagent_reference(project_id,reagent_id) VALUES(%1,%2)").arg(project_.id).arg(reagent.id)));
        ReagentRecord changed = reagent;
        changed.name = "STALE_NAME";
        old = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, changed));
        QVERIFY(wait(&errors_, old));
        changed = reagent;
        changed.type = QStringLiteral("清洗液");
        old = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, changed));
        QVERIFY(wait(&errors_, old));
        changed = reagent;
        changed.valid = false;
        old = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, changed));
        QVERIFY(wait(&errors_, old));
        old = errors_;
        QMetaObject::invokeMethod(store_, "removeReagent", Qt::QueuedConnection, Q_ARG(qint64, reagent.id));
        QVERIFY(wait(&errors_, old));
        reagent.manufacturer = "anonymous_updated";
        old = saved_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, reagent));
        QVERIFY(wait(&saved_, old));
        QVERIFY(sql(QString("DELETE FROM project_action_reagent_reference WHERE project_id=%1 AND reagent_id=%2").arg(project_.id).arg(reagent.id)));
        old = saved_;
        QMetaObject::invokeMethod(store_, "removeReagent", Qt::QueuedConnection, Q_ARG(qint64, reagent.id));
        QVERIFY(wait(&saved_, old));
    }
    void atomicWriteFailureAndRetry()
    {
        QVERIFY(sql("CREATE TRIGGER reject_batch_metadata BEFORE INSERT ON reagent_batch_metadata BEGIN SELECT RAISE(ABORT,'injected metadata write failure'); END"));
        ReagentBatch failed = batch_;
        failed.id = 0;
        failed.code = failed.batchGroup = "FAIL_ATOMIC";
        failed.metadataEdited = true;
        QVERIFY(save(failed, false));
        QVERIFY(message_.contains("injected metadata write failure"));
        QVERIFY(reload());
        QCOMPARE(snapshot_.batches.size(), 1); // batch+metadata roll back together
        QVERIFY(sql("DROP TRIGGER reject_batch_metadata"));
        QVERIFY(save(failed));
        QVERIFY(reload());
        QCOMPARE(snapshot_.batches.size(), 2);
        qInfo() << "ATOMIC_FAILURE_RETRY" << snapshot_.batches.last().code << snapshot_.batches.last().normalValue;
    }
    void permissionsAndReadOnlyReferenceChecks()
    {
        AccountSession guest;
        QMetaObject::invokeMethod(store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, guest));
        QVERIFY(save(batch_, false));
        QVERIFY(hold("quality", "item/1", batch_.id, false));
        QSignalSpy checked(store_, &ReagentStore::batchReferencesReady);
        QMetaObject::invokeMethod(store_, "checkBatchReferences", Qt::QueuedConnection, Q_ARG(qint64, 104), Q_ARG(qint64, batch_.id));
        QTRY_COMPARE(checked.count(), 1);
        QVERIFY(checked.first().at(2).toBool());
        QMetaObject::invokeMethod(store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, engineer_));
    }
    void pageDraftCancelBusyFailureAndReopen()
    {
        ReagentsPage page(store_);
        page.setFixedSize(1600, 900);
        page.setProjects({{project_.id, "PT_ANON", "s", true}});
        page.setSession(engineer_);
        page.show();
        QVERIFY(reload());
        QVERIFY(page.isReady());
        page.openBatchEditor();
        QTest::qWait(300);
        auto* dialog = page.findChild<QDialog*>("MaskDlgGroupSetting");
        QVERIFY(dialog);
        auto* groups = dialog->findChild<QTableWidget*>("tblGroup");
        auto* isi = dialog->findChild<QLineEdit*>("ldtIsiVal");
        auto* normal = dialog->findChild<QLineEdit*>("ldtNormalVal");
        QVERIFY(groups && isi && normal);
        groups->setCurrentCell(0, 0);
        QCOMPARE(isi->text(), QString("1.2345"));
        QCOMPARE(normal->text(), QString("12.3400"));
        isi->setFocus();
        isi->selectAll();
        QTest::keyClicks(isi, "2.5000");
        QVERIFY(page.hasUnsavedChanges());
        QVERIFY(reload());
        QCOMPARE(isi->text(), QString("2.5000")); // committed snapshots cannot replace a draft
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnCancel"), Qt::LeftButton);
        QVERIFY(!page.hasUnsavedChanges());
        QCOMPARE(isi->text(), QString("1.2345"));
        const int initialCount = groups->rowCount();
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnAdd"), Qt::LeftButton);
        QVERIFY(page.hasUnsavedChanges());
        QCOMPARE(groups->currentRow(), -1);
        groups->setCurrentCell(0, 0);
        QCOMPARE(groups->currentRow(), -1); // new draft cannot switch to another saved batch
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnCancel"), Qt::LeftButton);
        QCOMPARE(groups->rowCount(), initialCount);
        QVERIFY(!page.hasUnsavedChanges());
        groups->setCurrentCell(0, 0);
        isi->setFocus();
        isi->selectAll();
        QTest::keyClicks(isi, "2.5000");
        QSqlDatabase lock = QSqlDatabase::addDatabase("QSQLITE", "batch_ui_lock");
        lock.setDatabaseName(catalog_ + "/catalog.db");
        QVERIFY(lock.open());
        {
            QSqlQuery query(lock);
            QVERIFY(query.exec("BEGIN EXCLUSIVE"));
            const int old = errors_;
            QTest::mouseClick(dialog->findChild<QPushButton*>("btnSave"), Qt::LeftButton);
            QVERIFY(page.isBusy());
            QVERIFY(!page.prepareToLeave());
            QVERIFY(!dialog->isEnabled());
            QVERIFY(wait(&errors_, old));
            QVERIFY(message_.contains("locked"));
            QVERIFY(!page.isBusy());
            QVERIFY(page.hasUnsavedChanges());
            QCOMPARE(isi->text(), QString("2.5000"));
            QVERIFY(query.exec("ROLLBACK"));
        }
        lock.close();
        lock = QSqlDatabase();
        QSqlDatabase::removeDatabase("batch_ui_lock");
        const int old = saved_;
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnSave"), Qt::LeftButton);
        QVERIFY(wait(&saved_, old));
        QVERIFY(reload());
        QVERIFY(!page.hasUnsavedChanges());
        QCOMPARE(snapshot_.batches.first().isiValue, QString("2.5000"));
        const QString evidence = qEnvironmentVariable("SN100_BATCH_EVIDENCE");
        if (!evidence.isEmpty())
        {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(dialog->grab().save(evidence + "/reagent_batch_local_1600.png"));
        }
        QVERIFY(page.prepareToLeave());
        QTest::qWait(40);
        page.openBatchEditor();
        QTest::qWait(300);
        dialog = page.findChild<QDialog*>("MaskDlgGroupSetting");
        QVERIFY(dialog);
        dialog->findChild<QTableWidget*>("tblGroup")->setCurrentCell(0, 0);
        QCOMPARE(dialog->findChild<QLineEdit*>("ldtIsiVal")->text(), QString("2.5000"));
        QVERIFY(page.prepareToLeave());
        QVERIFY(page.close());
    }
    void removalReleasesMetadata()
    {
        int old = saved_;
        QMetaObject::invokeMethod(store_, "removeBatch", Qt::QueuedConnection, Q_ARG(qint64, batch_.id));
        QVERIFY(wait(&saved_, old));
        QVERIFY(reload());
        QCOMPARE(snapshot_.batches.size(), 1);
        QSignalSpy refs(store_, &ReagentStore::batchReferencesReady);
        QMetaObject::invokeMethod(store_, "checkBatchReferences", Qt::QueuedConnection, Q_ARG(qint64, 105), Q_ARG(qint64, batch_.id));
        QTRY_COMPARE(refs.count(), 1);
        QVERIFY(!refs.first().at(2).toBool());
    }
    void unknownVersionAndCorruptionPreserveData()
    {
        const qint64 remainingId = snapshot_.batches.first().id;
        QVERIFY(sql(QString("UPDATE reagent_batch_metadata SET normal_value='NaN' WHERE batch_id=%1").arg(remainingId)));
        int old = errors_;
        const int oldLoads = loaded_;
        QMetaObject::invokeMethod(store_, "load", Qt::QueuedConnection);
        QVERIFY(wait(&errors_, old));
        QVERIFY(message_.contains(QStringLiteral("损坏")));
        QCOMPARE(loaded_, oldLoads);
        QCOMPARE(scalar(QString("SELECT normal_value FROM reagent_batch_metadata WHERE batch_id=%1").arg(remainingId)), QString("NaN"));
        QVERIFY(sql(QString("UPDATE reagent_batch_metadata SET normal_value='12.3400' WHERE batch_id=%1").arg(remainingId)));
        QVERIFY(reload());
        QVERIFY(sql("UPDATE catalog_metadata SET value='future-v2' WHERE key='reagent_batch_metadata'"));
        QMetaObject::invokeMethod(store_, "close", Qt::BlockingQueuedConnection);
        old = errors_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(wait(&errors_, old));
        QVERIFY(message_.contains(QStringLiteral("版本不支持")));
        QCOMPARE(scalar("SELECT value FROM catalog_metadata WHERE key='reagent_batch_metadata'"), QString("future-v2"));
        QVERIFY(sql("UPDATE catalog_metadata SET value='v1' WHERE key='reagent_batch_metadata'"));
        old = loaded_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(wait(&loaded_, old));
        QCOMPARE(snapshot_.batches.size(), 1);
    }
    void cleanupTestCase()
    {
        if (worker_.isRunning())
        {
            QMetaObject::invokeMethod(store_, "close", Qt::BlockingQueuedConnection);
            worker_.quit();
            QVERIFY(worker_.wait(4000));
        }
        qInfo() << "NORMAL_EXIT runtime preserved" << catalog_;
    }
};
QTEST_MAIN(ReagentBatchTest)
#include "reagent_batch_test.moc"
