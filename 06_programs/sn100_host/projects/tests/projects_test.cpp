#include "project_page.h"
#include <QComboBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtTest>
#include <memory>

class ProjectsTest : public QObject
{
    Q_OBJECT
  private:
    QThread worker;
    ProjectsStore *store = nullptr;
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<ProjectPage> page;
    std::unique_ptr<QSignalSpy> ready, snapshots, success, errors;
    qint64 request = 10;
    AccountSession engineer() const
    {
        AccountSession session;
        session.name = QStringLiteral("offlineEngineer");
        session.role = 4;
        session.authenticated = true;
        return session;
    }
    void session(AccountSession value)
    {
        QMetaObject::invokeMethod(store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, value));
    }
    void load()
    {
        snapshots->clear();
        QMetaObject::invokeMethod(store, "load", Qt::QueuedConnection, Q_ARG(qint64, ++request));
        QTRY_VERIFY_WITH_TIMEOUT(!snapshots->isEmpty(), 3000);
    }
    ProjectRecords projects() const
    {
        return qvariant_cast<ProjectRecords>(snapshots->last().at(1));
    }
    void save(ProjectRecord project)
    {
        QMetaObject::invokeMethod(store, "save", Qt::QueuedConnection, Q_ARG(qint64, ++request),
                                  Q_ARG(ProjectRecord, project));
    }
    void copy(const QString &name)
    {
        QMetaObject::invokeMethod(store, "copy", Qt::QueuedConnection, Q_ARG(qint64, ++request),
                                  Q_ARG(qint64, qint64(1)), Q_ARG(QString, name));
    }
    void remove(qint64 id)
    {
        QMetaObject::invokeMethod(store, "remove", Qt::QueuedConnection, Q_ARG(qint64, ++request),
                                  Q_ARG(qint64, id));
    }
  private slots:
    void initTestCase()
    {
        ProjectsStore::registerTypes();
    }
    void init()
    {
        directory.reset(
            new QTemporaryDir(QCoreApplication::applicationDirPath() + QStringLiteral("/runtime-XXXXXX")));
        QVERIFY(directory->isValid());
        store = new ProjectsStore;
        store->moveToThread(&worker);
        connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        ready.reset(new QSignalSpy(store, &ProjectsStore::ready));
        snapshots.reset(new QSignalSpy(store, &ProjectsStore::snapshotReady));
        success.reset(new QSignalSpy(store, &ProjectsStore::succeeded));
        errors.reset(new QSignalSpy(store, &ProjectsStore::error));
        worker.start();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
                                  Q_ARG(QString, directory->path()), Q_ARG(bool, true));
        QTRY_COMPARE_WITH_TIMEOUT(ready->count(), 1, 3000);
        QCOMPARE(errors->count(), 0);
        session(engineer());
        load();
        QCOMPARE(projects().size(), 1);
    }
    void cleanup()
    {
        page.reset();
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        QVERIFY(worker.wait(3000));
        ready.reset();
        snapshots.reset();
        success.reset();
        errors.reset();
        directory.reset();
    }
    void copySaveReopenAndDelete()
    {
        copy(QStringLiteral("DEMO_B"));
        QTRY_COMPARE(success->count(), 1);
        load();
        QCOMPARE(projects().size(), 2);
        ProjectRecord copy = projects().last();
        QVERIFY(copy.id != 1);
        QVERIFY(!copy.fixed);
        QCOMPARE(copy.instrumentProjectId, QStringLiteral("OFFLINE-001"));
        QCOMPARE(copy.sampleRates.size(), 2);
        QVERIFY(copy.sampleRates[0].defaultOption);
        QVERIFY(copy.sampleRates[0].sampleOption);
        QCOMPARE(copy.sampleRates[1].sampleRate, QStringLiteral("OFFLINE_RATE_B"));
        QCOMPARE(copy.sampleRates[1].order, 2);
        copy.fullName = QStringLiteral("匿名保存验证");
        copy.lisCode = QStringLiteral("LOCAL8");
        copy.enabled = false;
        copy.results[1].unit = QStringLiteral("a.u.x10");
        copy.results[1].decimals = 6;
        copy.results[0].decimals = 4;
        copy.results[1].normalLow = 1.25;
        save(copy);
        QTRY_COMPARE(success->count(), 2);
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        ready->clear();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
                                  Q_ARG(QString, directory->path()), Q_ARG(bool, true));
        QTRY_COMPARE(ready->count(), 1);
        session(engineer());
        load();
        const ProjectRecord reopened = projects().last();
        QCOMPARE(reopened.id, copy.id);
        QCOMPARE(reopened.fullName, copy.fullName);
        QCOMPARE(reopened.enabled, false);
        QCOMPARE(reopened.lisCode, copy.lisCode);
        QCOMPARE(reopened.results[1].unit, QStringLiteral("a.u.x10"));
        QCOMPARE(reopened.results[1].normalLow, 1.25);
        QCOMPARE(reopened.results[1].decimals, 6);
        QCOMPARE(reopened.results[0].decimals, 4);
        QCOMPARE(reopened.sampleRates.size(), 2);
        QCOMPARE(reopened.sampleRates[0].sampleRate, QStringLiteral("OFFLINE_RATE_A"));
        QVERIFY(reopened.sampleRates[0].defaultOption);
        QVERIFY(!reopened.sampleRates[1].defaultOption);
        const ProjectUnits units = qvariant_cast<ProjectUnits>(snapshots->last().at(2));
        QCOMPARE(units.size(), 3);
        QCOMPARE(units.last().ratio, 10.0);
        remove(copy.id);
        QTRY_COMPARE(success->count(), 3);
        load();
        QCOMPARE(projects().size(), 1);
        QCOMPARE(errors->count(), 0);
    }
    void invalidAndDuplicateAreAtomic()
    {
        ProjectRecord project = projects().first();
        project.name = QStringLiteral("../bad");
        save(project);
        QTRY_COMPARE(errors->count(), 1);
        load();
        QCOMPARE(projects().first().name, QStringLiteral("DEMO_A"));
        project = projects().first();
        project.results[0].testLow = 100;
        project.results[0].testHigh = 1;
        save(project);
        QTRY_COMPARE(errors->count(), 2);
        project = projects().first();
        project.results[1].unit = QStringLiteral("unknown");
        save(project);
        QTRY_COMPARE(errors->count(), 3);
        copy(QStringLiteral("DEMO_A"));
        QTRY_COMPARE(errors->count(), 4);
        load();
        QCOMPARE(projects().size(), 1);
        project = projects().first();
        project.results[0].mainResult = false;
        save(project);
        QTRY_COMPARE(errors->count(), 5);
        QCOMPARE(success->count(), 0);
    }
    void permissionAndFixedProtection()
    {
        const ProjectRecord project = projects().first();
        for (int role : {0, 1, 2, 3, 6})
        {
            AccountSession denied = engineer();
            denied.role = role;
            session(denied);
            save(project);
            QTRY_COMPARE(errors->count(), role < 4 ? role + 1 : 5);
        }
        AccountSession denied = engineer();
        denied.authenticated = false;
        session(denied);
        copy(QStringLiteral("DENIED"));
        QTRY_COMPARE(errors->count(), 6);
        denied = engineer();
        denied.actionReady = false;
        session(denied);
        remove(1);
        QTRY_COMPARE(errors->count(), 7);
        session(engineer());
        remove(1);
        QTRY_COMPARE(errors->count(), 8);
        load();
        QCOMPARE(projects().size(), 1);
    }
    void externalReferenceProtectsDelete()
    {
        copy(QStringLiteral("REFERENCED"));
        QTRY_COMPARE(success->count(), 1);
        load();
        const qint64 id = projects().last().id;
        const QString connection = QStringLiteral("reference-test");
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            database.setDatabaseName(directory->filePath(QStringLiteral("catalog.db")));
            QVERIFY(database.open());
            QSqlQuery query(database);
            QVERIFY(query.exec(QStringLiteral("PRAGMA foreign_keys=ON")));
            QVERIFY(query.exec(
                QStringLiteral("CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY,project_id INTEGER "
                               "NOT NULL REFERENCES project_base(id) ON DELETE RESTRICT)")));
            query.prepare(QStringLiteral("INSERT INTO reagent_batch_group VALUES(1,?)"));
            query.addBindValue(id);
            QVERIFY(query.exec());
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);
        remove(id);
        QTRY_COMPARE(errors->count(), 1);
        load();
        QCOMPARE(projects().size(), 2);
        QCOMPARE(projects().last().results.size(), 2);
    }
    void lockFailureKeepsGuiEventLoopAlive()
    {
        const QString connection = QStringLiteral("lock-test");
        {
            QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            database.setDatabaseName(directory->filePath(QStringLiteral("catalog.db")));
            QVERIFY(database.open());
            QSqlQuery lock(database);
            QVERIFY(lock.exec(QStringLiteral("BEGIN EXCLUSIVE")));
            int ticks = 0;
            QTimer timer;
            timer.setTimerType(Qt::PreciseTimer);
            timer.setInterval(10);
            connect(&timer, &QTimer::timeout, this, [&ticks]() { ++ticks; });
            timer.start();
            QElapsedTimer duration;
            duration.start();
            save(projects().first());
            QTRY_COMPARE_WITH_TIMEOUT(errors->count(), 1, 3000);
            timer.stop();
            QVERIFY(ticks > 30);
            QVERIFY(duration.elapsed() < 2500);
            qInfo() << "PROJECT_LOCK_GUI ticks" << ticks << "elapsed_ms" << duration.elapsed();
            QVERIFY(lock.exec(QStringLiteral("ROLLBACK")));
            database.close();
        }
        QSqlDatabase::removeDatabase(connection);
        save(projects().first());
        QTRY_COMPARE(success->count(), 1);
    }
    void guiDraftCancelPermissionAndUnitSave()
    {
        page.reset(new ProjectPage(store));
        page->setSession(engineer());
        // Page normally subscribes before initialize in either real entry point.
        ready->clear();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
                                  Q_ARG(QString, directory->path()), Q_ARG(bool, false));
        QTRY_VERIFY(page->isReady() && !page->isBusy());
        auto *name = page->findChild<QLineEdit *>(QStringLiteral("project_cn_name"));
        QVERIFY(name);
        name->setFocus();
        name->selectAll();
        QTest::keyClicks(name, "draft");
        QVERIFY(page->hasUnsavedChanges());
        QTimer::singleShot(0,
                           []()
                           {
                               auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->done(QMessageBox::Cancel);
                           });
        QVERIFY(!page->prepareToLeave());
        QCOMPARE(name->text(), QStringLiteral("draft"));
        QTest::mouseClick(page->findChild<QPushButton *>(QStringLiteral("btnCancel")), Qt::LeftButton);
        QVERIFY(!page->hasUnsavedChanges());
        QCOMPARE(name->text(), QStringLiteral("离线项目A"));
        auto *table = page->findChild<QTableWidget *>(QStringLiteral("resultTable"));
        QVERIFY(table);
        auto *unit = qobject_cast<QComboBox *>(table->cellWidget(1, 9));
        QVERIFY(unit);
        unit->setCurrentText(QStringLiteral("a.u.x10"));
        QVERIFY(page->hasUnsavedChanges());
        QTest::mouseClick(page->findChild<QPushButton *>(QStringLiteral("btnSave")), Qt::LeftButton);
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges());
        QCOMPARE(page->projects().first().results[1].unit, QStringLiteral("a.u.x10"));
        AccountSession guest;
        page->setSession(guest);
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("btnSave"))->isEnabled());
        QVERIFY(page->findChild<QLineEdit *>(QStringLiteral("project_en_name"))->isReadOnly());
        QVERIFY(page->findChild<QPushButton *>(QStringLiteral("btnSampleAdd"))->isEnabled());
        QVERIFY(page->findChild<QLineEdit *>(QStringLiteral("min_test_time"))->isReadOnly());
        if (qEnvironmentVariableIsSet("SN100_PROJECT_SCREENSHOT"))
        {
            page->show();
            QTest::qWait(100);
            QVERIFY(page->grab().save(qEnvironmentVariable("SN100_PROJECT_SCREENSHOT")));
        }
    }
    void unmarkedAndUnknownInputsArePreserved()
    {
        const QString unmarked = directory->filePath(QStringLiteral("unmarked"));
        QVERIFY(QDir().mkpath(unmarked));
        QFile userFile(QDir(unmarked).filePath(QStringLiteral("keep.txt")));
        QVERIFY(userFile.open(QIODevice::WriteOnly));
        QCOMPARE(userFile.write("keep unchanged\n"), qint64(15));
        userFile.close();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection, Q_ARG(QString, unmarked),
                                  Q_ARG(bool, true));
        QTRY_COMPARE(errors->count(), 1);
        QVERIFY(!QFileInfo::exists(QDir(unmarked).filePath(QStringLiteral("catalog.db"))));
        QVERIFY(userFile.open(QIODevice::ReadOnly));
        QCOMPARE(userFile.readAll(), QByteArray("keep unchanged\n"));
        userFile.close();

        const QString unknown = directory->filePath(QStringLiteral("unknown"));
        QVERIFY(QDir().mkpath(unknown));
        QFile marker(QDir(unknown).filePath(QStringLiteral(".sn100-catalog-offline-v1")));
        QVERIFY(marker.open(QIODevice::WriteOnly));
        marker.write("sn100-catalog-offline-v1\n");
        marker.close();
        QFile database(QDir(unknown).filePath(QStringLiteral("catalog.db")));
        QVERIFY(database.open(QIODevice::WriteOnly));
        database.write("unrecognized-database\n");
        database.close();
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection, Q_ARG(QString, unknown),
                                  Q_ARG(bool, true));
        QTRY_COMPARE(errors->count(), 2);
        QVERIFY(database.open(QIODevice::ReadOnly));
        QCOMPARE(database.readAll(), QByteArray("unrecognized-database\n"));
        database.close();
        QMetaObject::invokeMethod(
            store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory->filePath(QStringLiteral("sn100_host_reference/forbidden"))),
            Q_ARG(bool, true));
        QTRY_COMPARE(errors->count(), 3);
        QVERIFY(!QFileInfo::exists(directory->filePath(QStringLiteral("sn100_host_reference"))));
    }
};
QTEST_MAIN(ProjectsTest)
#include "projects_test.moc"
