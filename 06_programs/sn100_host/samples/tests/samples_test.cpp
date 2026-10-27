#include "cuscheckbox.h"
#include "cusdialogmask.h"
#include "sample_page.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtTest>
#include <algorithm>
#include <cmath>

static QString rootPath()
{
    const QString root = qEnvironmentVariable("SN100_SAMPLE_TEST_ROOT");
    QDir().mkpath(root);
    return root;
}
static QVariant sql(const QString &path, const QString &statement, bool *ok = nullptr)
{
    const QString name = QUuid::createUuid().toString();
    QVariant result;
    bool success = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery q(db);
            success = q.exec(statement);
            if (success && q.next())
            {
                result = q.value(0);
            }
            q.finish();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (ok)
    {
        *ok = success;
    }
    return result;
}
struct Harness
{
    QTemporaryDir directory{rootPath() + "/samples_XXXXXX"};
    QThread projectsThread, sampleThread;
    ProjectsStore *projects = new ProjectsStore;
    SampleStore *samples = new SampleStore;
    SamplePage *page = nullptr;
    ProjectRecords records;
    QString database() const
    {
        return directory.path() + "/catalog.db";
    }
    Harness()
    {
        projects->moveToThread(&projectsThread);
        samples->moveToThread(&sampleThread);
        QObject::connect(&projectsThread, &QThread::finished, projects, &QObject::deleteLater);
        QObject::connect(&sampleThread, &QThread::finished, samples, &QObject::deleteLater);
        page = new SamplePage(samples);
        page->show();
        projectsThread.start();
        sampleThread.start();
    }
    ~Harness()
    {
        delete page;
        QMetaObject::invokeMethod(samples, "close", Qt::BlockingQueuedConnection);
        sampleThread.quit();
        sampleThread.wait();
        QMetaObject::invokeMethod(projects, "close", Qt::BlockingQueuedConnection);
        projectsThread.quit();
        projectsThread.wait();
    }
    bool start()
    {
        QSignalSpy ready(projects, &ProjectsStore::ready);
        QMetaObject::invokeMethod(projects, "initialize", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, directory.path()), Q_ARG(bool, true));
        if (ready.count() != 1)
        {
            return false;
        }
        QSignalSpy values(projects, &ProjectsStore::snapshotReady);
        QMetaObject::invokeMethod(projects, "load", Qt::BlockingQueuedConnection, Q_ARG(qint64, 1));
        if (values.count() != 1)
        {
            return false;
        }
        records = qvariant_cast<ProjectRecords>(values.first()[1]);
        page->setProjects(records);
        QSignalSpy sampleReady(samples, &SampleStore::ready);
        QMetaObject::invokeMethod(samples, "initialize", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, directory.path()));
        return sampleReady.count() == 1;
    }
    void actor(bool authenticate = true)
    {
        AccountSession session;
        session.name = "offlineEngineer";
        session.authenticated = authenticate;
        session.role = authenticate ? 4 : -1;
        page->setSession(session);
        QMetaObject::invokeMethod(samples, "setSession", Qt::BlockingQueuedConnection,
                                  Q_ARG(AccountSession, session));
        QMetaObject::invokeMethod(projects, "setSession", Qt::BlockingQueuedConnection,
                                  Q_ARG(AccountSession, session));
    }
    SampleSnapshot snapshot()
    {
        QSignalSpy values(samples, &SampleStore::snapshotReady);
        QMetaObject::invokeMethod(samples, "load", Qt::BlockingQueuedConnection, Q_ARG(qint64, 999));
        if (values.isEmpty())
        {
            return {};
        }
        return qvariant_cast<SampleSnapshot>(values.first()[1]);
    }
    SampleRack ordinary(const QString &name = "S01") const
    {
        SampleRack rack;
        rack.name = name;
        for (int i = 1; i <= 8; ++i)
        {
            SampleHole hole;
            hole.number = i;
            rack.holes.append(hole);
        }
        return rack;
    }
    bool save(SampleRack rack)
    {
        QSignalSpy success(samples, &SampleStore::succeeded);
        QMetaObject::invokeMethod(samples, "saveRack", Qt::BlockingQueuedConnection, Q_ARG(qint64, 1000),
                                  Q_ARG(SampleRack, rack));
        return success.count() == 1;
    }
};
static void answer(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0,
                       [button]()
                       {
                           for (QWidget *widget : QApplication::topLevelWidgets())
                           {
                               if (auto *dialog = qobject_cast<QMessageBox *>(widget))
                               {
                                   if (dialog->isVisible())
                                   {
                                       dialog->done(button);
                                       return;
                                   }
                               }
                           }
                       });
}
static void input(QLineEdit *edit, const QString &value)
{
    edit->setFocus();
    edit->selectAll();
    QTest::keyClicks(edit, value);
}
static void capture(QWidget *widget, const QString &name)
{
    const QString directory = qEnvironmentVariable("SN100_SAMPLE_CAPTURE_DIR");
    if (directory.isEmpty())
    {
        return;
    }
    QDir().mkpath(directory);
    if (auto *mask = widget->findChild<CusDialogMask *>("mask"))
    {
        QTRY_VERIFY_WITH_TIMEOUT(mask->isReadyForInput(), 2000);
    }
    QTest::qWait(30);
    widget->grab().save(directory + "/" + name + ".png");
}
class SamplesTest : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        ProjectsStore::registerTypes();
        SampleStore::registerTypes();
        QVERIFY(!rootPath().isEmpty());
        qInfo() << "environment" << qVersion() << QGuiApplication::platformName();
    }
    void directoryAndSchema()
    {
        Harness h;
        QVERIFY(h.directory.isValid());
        QFile sentinel(h.directory.path() + "/sentinel");
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("untouched");
        sentinel.close();
        QSignalSpy errors(h.samples, &SampleStore::error);
        QMetaObject::invokeMethod(h.samples, "initialize", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, h.directory.path()));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!QFile::exists(h.database()));
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.readAll(), QByteArray("untouched"));
        sentinel.close();
        QVERIFY(QFile::remove(sentinel.fileName()));
        QVERIFY(h.start());
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        QCOMPARE(h.snapshot().racks.size(), 1);
        QCOMPARE(h.snapshot().racks.first().holes.size(), 3);
        QMetaObject::invokeMethod(h.samples, "close", Qt::BlockingQueuedConnection);
        bool ok = false;
        sql(h.database(), "DELETE FROM catalog_metadata WHERE key='sample_schema'", &ok);
        QVERIFY(ok);
        const int rows = sql(h.database(), "SELECT COUNT(*) FROM sample_local_rack").toInt();
        errors.clear();
        QMetaObject::invokeMethod(h.samples, "initialize", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, h.directory.path()));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(sql(h.database(), "SELECT COUNT(*) FROM sample_local_rack").toInt(), rows);
        QVERIFY(!sql(h.database(), "SELECT value FROM catalog_metadata WHERE key='sample_schema'").isValid());
    }
    void guestAndRoundtrip()
    {
        Harness h;
        QVERIFY(h.start());
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        SampleRack rack = h.ordinary();
        QVERIFY(!h.save(rack));
        h.actor();
        QVERIFY(h.save(rack));
        auto snapshot = h.snapshot();
        QCOMPARE(snapshot.racks.size(), 2);
        rack = snapshot.racks[1];
        QCOMPARE(rack.holes[0].sampleId, QString(""));
        rack.holes[0].sampleId = "1001";
        rack.holes[0].container = 0;
        rack.holes[0].tasks = {{h.records.first().id, "OFFLINE_RATE_B", 99, true}};
        rack.holes[7].tasks = {{h.records.first().id, "OFFLINE_RATE_A", 0, false}};
        QVERIFY(h.save(rack));
        snapshot = h.snapshot();
        QCOMPARE(snapshot.racks[1].holes[0].tasks.first().repeats, 99);
        QCOMPARE(snapshot.racks[1].holes[7].tasks.first().repeats, 0);
        QMetaObject::invokeMethod(h.samples, "close", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(h.samples, "initialize", Qt::BlockingQueuedConnection,
                                  Q_ARG(QString, h.directory.path()));
        snapshot = h.snapshot();
        QCOMPARE(snapshot.racks[1].holes[0].tasks.first().rate, QString("OFFLINE_RATE_B"));
        QCOMPARE(snapshot.racks[1].holes[0].sampleId, QString("1001"));
        qInfo() << "roundtrip" << snapshot.racks[1].name << snapshot.racks[1].holes[0].sampleId
                << snapshot.racks[1].holes[0].tasks.first().repeats;
    }
    void localPresetWhileInstrumentBusy()
    {
        Harness h;
        QVERIFY(h.start());
        AccountSession session;
        session.authenticated = true;
        session.role = 1;
        session.name = "offlineSend";
        session.actionReady = false;
        h.page->setSession(session);
        QMetaObject::invokeMethod(h.samples, "setSession", Qt::BlockingQueuedConnection,
                                  Q_ARG(AccountSession, session));
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        QVERIFY(h.page->findChild<QPushButton *>("btnMenuSave")->isEnabled());
        SampleRack rack = h.ordinary();
        rack.holes[0].tasks = {{h.records.first().id, "OFFLINE_RATE_A", 1, false}};
        QVERIFY(h.save(rack));
        QCOMPARE(h.snapshot().racks[1].holes[0].tasks.first().repeats, 1);
        QCOMPARE(sql(h.database(), "SELECT COUNT(*) FROM sqlite_master WHERE name "
                                   "IN('report_data','report_result','reagent_setting')")
                     .toInt(),
                 0);
    }
    void validationAndRollback()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        SampleRack rack = h.ordinary();
        rack.holes[0].tasks = {{h.records.first().id, "OFFLINE_RATE_A", 1, false}};
        QVERIFY(h.save(rack));
        rack = h.snapshot().racks[1];
        const QString before = rack.name;
        rack.name = "S02";
        rack.holes[7].tasks = {{h.records.first().id, "UNKNOWN", 1, false}};
        QVERIFY(!h.save(rack));
        QCOMPARE(h.snapshot().racks[1].name, before);
        QCOMPARE(h.snapshot().racks[1].holes[0].tasks.first().repeats, 1);
        rack.holes[7].tasks = {{h.records.first().id, "OFFLINE_RATE_A", 100, false}};
        QVERIFY(!h.save(rack));
        rack.holes[7].tasks.clear();
        rack.holes[7].sampleId = "bad space";
        QVERIFY(!h.save(rack));
        rack.holes[7].sampleId = "";
        rack.holes[7].number = 1;
        QVERIFY(!h.save(rack));
        bool ok = false;
        sql(h.database(), "UPDATE project_base SET is_used=0", &ok);
        QVERIFY(ok);
        rack = h.snapshot().racks[1];
        QVERIFY(!h.save(rack));
        sql(h.database(), "UPDATE project_base SET is_used=1");
        QVERIFY(h.save(rack));
        QCOMPARE(sql(h.database(), "SELECT COUNT(*) FROM sqlite_master WHERE name "
                                   "IN('report_data','report_result','reagent_setting')")
                     .toInt(),
                 0);
    }
    void combinationsAndForeignKeys()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        QSignalSpy copied(h.projects, &ProjectsStore::succeeded);
        QMetaObject::invokeMethod(h.projects, "copy", Qt::BlockingQueuedConnection, Q_ARG(qint64, 5),
                                  Q_ARG(qint64, h.records.first().id), Q_ARG(QString, QString("REFERENCED")));
        QCOMPARE(copied.count(), 1);
        const qint64 id = copied.first()[2].toLongLong();
        SampleCombination combination;
        combination.name = "O'Brien 匿名";
        combination.orderedProjectIds = {id, id, h.records.first().id};
        QSignalSpy saved(h.samples, &SampleStore::succeeded);
        QMetaObject::invokeMethod(h.samples, "saveCombination", Qt::BlockingQueuedConnection,
                                  Q_ARG(qint64, 6), Q_ARG(SampleCombination, combination));
        QCOMPARE(saved.count(), 1);
        auto snapshot = h.snapshot();
        QCOMPARE(snapshot.combinations.first().orderedProjectIds, combination.orderedProjectIds);
        QSignalSpy refused(h.projects, &ProjectsStore::error);
        QMetaObject::invokeMethod(h.projects, "remove", Qt::BlockingQueuedConnection, Q_ARG(qint64, 7),
                                  Q_ARG(qint64, id));
        QCOMPARE(refused.count(), 1);
        SampleCombination empty;
        empty.name = "Empty";
        saved.clear();
        QMetaObject::invokeMethod(h.samples, "saveCombination", Qt::BlockingQueuedConnection,
                                  Q_ARG(qint64, 8), Q_ARG(SampleCombination, empty));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(h.snapshot().combinations.size(), 2);
        QCOMPARE(h.snapshot().combinations.last().orderedProjectIds.size(), 0);
        empty.name = "";
        saved.clear();
        QMetaObject::invokeMethod(h.samples, "saveCombination", Qt::BlockingQueuedConnection,
                                  Q_ARG(qint64, 9), Q_ARG(SampleCombination, empty));
        QCOMPARE(saved.count(), 0);
        const qint64 combinationId = snapshot.combinations.first().id;
        QMetaObject::invokeMethod(h.samples, "removeCombination", Qt::BlockingQueuedConnection,
                                  Q_ARG(qint64, 10), Q_ARG(qint64, combinationId));
        copied.clear();
        QMetaObject::invokeMethod(h.projects, "remove", Qt::BlockingQueuedConnection, Q_ARG(qint64, 11),
                                  Q_ARG(qint64, id));
        QCOMPARE(copied.count(), 1);
    }
    void rackReferencesAndEmergency()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        SampleRack emergency = h.snapshot().racks.first();
        emergency.holes[0].tasks = {{h.records.first().id, "OFFLINE_RATE_A", 1, false}};
        QVERIFY(h.save(emergency));
        QSignalSpy refused(h.samples, &SampleStore::error);
        QMetaObject::invokeMethod(h.samples, "removeRack", Qt::BlockingQueuedConnection, Q_ARG(qint64, 1),
                                  Q_ARG(qint64, emergency.id));
        QCOMPARE(refused.count(), 1);
        QVERIFY(h.save(h.ordinary()));
        QVERIFY(h.save(h.ordinary("S02")));
        QSignalSpy success(h.samples, &SampleStore::succeeded);
        QMetaObject::invokeMethod(h.samples, "clearOrdinaryRacks", Qt::BlockingQueuedConnection,
                                  Q_ARG(qint64, 2));
        QCOMPARE(success.count(), 1);
        QCOMPARE(h.snapshot().racks.size(), 1);
        QCOMPARE(h.snapshot().racks.first().holes.first().tasks.size(), 1);
    }
    void lockFailureAndRecovery()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        SampleRack rack = h.ordinary();
        QVERIFY(h.save(rack));
        rack = h.snapshot().racks[1];
        rack.name = "S02";
        const QString connection = QUuid::createUuid().toString();
        {
            QSqlDatabase lock = QSqlDatabase::addDatabase("QSQLITE", connection);
            lock.setDatabaseName(h.database());
            QVERIFY(lock.open());
            QSqlQuery query(lock);
            QVERIFY(query.exec("BEGIN IMMEDIATE"));
            QVERIFY(!h.save(rack));
            QVERIFY(query.exec("ROLLBACK"));
            lock.close();
        }
        QSqlDatabase::removeDatabase(connection);
        QVERIFY(h.save(rack));
        QCOMPARE(h.snapshot().racks[1].name, QString("S02"));
    }
    void pageDraftAndDependencyRefresh()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        auto *table = h.page->findChild<QTableWidget *>("tblSetting");
        QVERIFY(table);
        table->item(0, 1)->setText("1001");
        QVERIFY(h.page->hasUnsavedChanges());
        h.page->setProjects(h.records);
        ReagentSnapshot reagent;
        h.page->setReagents(reagent);
        h.page->reload();
        QCOMPARE(table->item(0, 1)->text(), QString("1001"));
        QVERIFY(h.page->hasUnsavedChanges());
        answer(QMessageBox::Cancel);
        QVERIFY(!h.page->prepareToLeave());
        QCOMPARE(table->item(0, 1)->text(), QString("1001"));
        answer(QMessageBox::Discard);
        QVERIFY(h.page->prepareToLeave());
        QVERIFY(!h.page->hasUnsavedChanges());
        QCOMPARE(table->item(0, 1)->text(), QString(""));
    }
    void pageDetailAndBatchBoundary()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        h.page->findChild<QPushButton *>("btnHoleDetail")->click();
        QTRY_VERIFY(h.page->findChild<QDialog *>("sampleDetailDialog"));
        auto *detail = h.page->findChild<QDialog *>("sampleDetailDialog");
        QTRY_VERIFY(detail->findChild<CusDialogMask *>("mask")->isReadyForInput());
        auto *project =
            detail->findChild<QPushButton *>("detailProject_" + QString::number(h.records.first().id));
        QVERIFY(project);
        project->click();
        auto *count = detail->findChild<QComboBox *>("detailCount_" + QString::number(h.records.first().id));
        QVERIFY(count);
        count->setCurrentText("0");
        auto *sample = detail->findChild<QLineEdit *>("ldtSampleID");
        input(sample, "1001");
        answer(QMessageBox::Cancel);
        QTest::keyClick(detail, Qt::Key_Escape);
        QVERIFY(detail->isVisible());
        QVERIFY(h.page->hasUnsavedChanges());
        count->setCurrentText("99");
        capture(detail, "sample_detail_99");
        detail->findChild<QPushButton *>("btnSave")->click();
        QTRY_VERIFY(!detail->isVisible());
        h.page->findChild<QPushButton *>("btnMenuSave")->click();
        QTRY_VERIFY(!h.page->isBusy());
        QCOMPARE(h.page->snapshot().racks.first().holes.first().tasks.first().repeats, 99);
        auto *projects = h.page->findChild<QListWidget *>("projButtonList");
        QWidget *active = projects->itemWidget(projects->item(0));
        QVERIFY(active);
        QVERIFY(active->findChild<QPushButton *>("sampleProject_" + QString::number(h.records.first().id))
                    ->isChecked());
        QCOMPARE(active->findChild<QComboBox *>("sampleCount_" + QString::number(h.records.first().id))
                     ->currentText(),
                 QString("99"));
        capture(h.page, "sample_emergency_preset");
        // Last emergency hole, requested three copies: no row 4 access.
        auto *table = h.page->findChild<QTableWidget *>("tblSetting");
        table->selectRow(2);
        table->item(2, 1)->setText("2000");
        h.page->findChild<QPushButton *>("btnHoleDetail")->click();
        QTRY_VERIFY(h.page->findChild<QDialog *>("sampleDetailDialog"));
        detail = h.page->findChild<QDialog *>("sampleDetailDialog");
        QTRY_VERIFY(detail->findChild<CusDialogMask *>("mask")->isReadyForInput());
        detail->findChild<QPushButton *>("detailProject_" + QString::number(h.records.first().id))->click();
        detail->findChild<QPushButton *>("btnSave")->click();
        QTRY_VERIFY(!detail->isVisible());
        h.page->findChild<QPushButton *>("btnRepeatHoleTask")->click();
        QTRY_VERIFY(h.page->findChild<QDialog *>("sampleRepeatDialog"));
        auto *repeat = h.page->findChild<QDialog *>("sampleRepeatDialog");
        QTRY_VERIFY(repeat->findChild<CusDialogMask *>("mask")->isReadyForInput());
        repeat->findChild<QPushButton *>("btnNum_3")->click();
        repeat->findChild<QPushButton *>("btnSave")->click();
        QTRY_VERIFY(!repeat->isVisible());
        h.page->findChild<QPushButton *>("btnMenuSave")->click();
        QTRY_VERIFY(!h.page->isBusy());
        QCOMPARE(h.page->snapshot().racks.first().holes.size(), 3);
        QCOMPARE(h.page->snapshot().racks.first().holes[2].sampleId, QString("2000"));
    }
    void pageCombinationSaveAndFailure()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        h.page->findChild<QPushButton *>("btnCombSetting")->click();
        auto *dialog = h.page->findChild<QDialog *>("sampleCombinationDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->findChild<CusDialogMask *>("mask")->isReadyForInput());
        input(dialog->findChild<QLineEdit *>("ldtCombaName"), "O'Brien");
        auto *count =
            dialog->findChild<QComboBox *>("combinationCount_" + QString::number(h.records.first().id));
        QVERIFY(count);
        count->setCurrentText("2");
        capture(dialog, "sample_combination_edit");
        const QString connection = QUuid::createUuid().toString();
        {
            QSqlDatabase lock = QSqlDatabase::addDatabase("QSQLITE", connection);
            lock.setDatabaseName(h.database());
            QVERIFY(lock.open());
            QSqlQuery query(lock);
            QVERIFY(query.exec("BEGIN IMMEDIATE"));
            dialog->findChild<QPushButton *>("btnSave")->click();
            QTRY_VERIFY(!h.page->isBusy());
            QVERIFY(dialog->isVisible());
            QVERIFY(h.page->hasUnsavedChanges());
            QCOMPARE(dialog->findChild<QLineEdit *>("ldtCombaName")->text(), QString("O'Brien"));
            QVERIFY(query.exec("ROLLBACK"));
            lock.close();
        }
        QSqlDatabase::removeDatabase(connection);
        dialog->findChild<QPushButton *>("btnSave")->click();
        QTRY_VERIFY(!h.page->isBusy());
        QCOMPARE(h.page->snapshot().combinations.size(), 1);
        QCOMPARE(h.page->snapshot().combinations.first().name, QString("O'Brien"));
        QCOMPARE(h.page->snapshot().combinations.first().orderedProjectIds.size(), 2);
    }
    void pagePermissionAndBusyClose()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        h.page->findChild<QPushButton *>("btnHoleDetail")->click();
        auto *dialog = h.page->findChild<QDialog *>("sampleDetailDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->findChild<CusDialogMask *>("mask")->isReadyForInput());
        h.actor(false);
        QVERIFY(!dialog->findChild<QPushButton *>("btnSave")->isEnabled());
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(!h.page->hasUnsavedChanges());
        h.actor();
        h.page->findChild<QTableWidget *>("tblSetting")->item(0, 1)->setText("3000");
        h.page->findChild<QPushButton *>("btnMenuSave")->click();
        QVERIFY(h.page->isBusy());
        QVERIFY(!h.page->prepareToLeave());
        QTRY_VERIFY(!h.page->isBusy());
        QCOMPARE(h.snapshot().racks.first().holes.first().sampleId, QString("3000"));
        QMetaObject::invokeMethod(h.samples, "close", Qt::BlockingQueuedConnection);
        QTRY_VERIFY(!h.page->isReady());
        QVERIFY(!h.page->findChild<QPushButton *>("btnMenuSave")->isEnabled());
    }
    void mixedLoad()
    {
        Harness h;
        QVERIFY(h.start());
        h.actor();
        QTRY_VERIFY(h.page->isReady() && !h.page->isBusy());
        const int seconds = qMax(1, qEnvironmentVariableIntValue("SN100_SAMPLE_MEASURE_SECONDS"));
        QVector<double> heartbeats, operations;
        QElapsedTimer clock;
        clock.start();
        qint64 last = clock.nsecsElapsed();
        QTimer heartbeat;
        heartbeat.setInterval(10);
        heartbeat.setTimerType(Qt::PreciseTimer);
        connect(&heartbeat, &QTimer::timeout, this,
                [&]()
                {
                    const qint64 now = clock.nsecsElapsed();
                    heartbeats.append((now - last) / 1e6);
                    last = now;
                });
        heartbeat.start();
        auto *table = h.page->findChild<QTableWidget *>("tblSetting");
        int completed = 0;
        while (clock.elapsed() < seconds * 1000)
        {
            table->item(0, 1)->setText(QString::number(1000 + (completed % 8000)));
            const qint64 start = clock.nsecsElapsed();
            h.page->findChild<QPushButton *>("btnMenuSave")->click();
            QTRY_VERIFY_WITH_TIMEOUT(!h.page->isBusy(), 3000);
            operations.append((clock.nsecsElapsed() - start) / 1e6);
            ++completed;
            QTest::qWait(10);
        }
        heartbeat.stop();
        QVERIFY(!heartbeats.isEmpty());
        auto percentile = [](QVector<double> values, double fraction)
        {
            std::sort(values.begin(), values.end());
            return values[qBound(0, int(std::ceil(values.size() * fraction)) - 1, values.size() - 1)];
        };
        QJsonObject result{{"environment", "WSL x86_64 Release Qt5"},
                           {"seconds", clock.elapsed() / 1000.0},
                           {"local_commits", completed},
                           {"failures", 0},
                           {"save_to_reload_p95_ms", percentile(operations, .95)},
                           {"save_to_reload_max_ms", percentile(operations, 1)},
                           {"heartbeat_samples", heartbeats.size()},
                           {"heartbeat_p95_ms", percentile(heartbeats, .95)},
                           {"heartbeat_max_ms", percentile(heartbeats, 1)},
                           {"not_instrument_or_pixel_latency", true}};
        const QString output = qEnvironmentVariable("SN100_SAMPLE_MEASURE_JSON");
        if (!output.isEmpty())
        {
            QFile file(output);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write(QJsonDocument(result).toJson()) > 0);
        }
        qInfo().noquote() << QJsonDocument(result).toJson(QJsonDocument::Compact);
        QCOMPARE(h.page->snapshot().racks.first().holes.first().sampleId,
                 QString::number(1000 + ((completed - 1) % 8000)));
    }
};
QTEST_MAIN(SamplesTest)
#include "samples_test.moc"
