#include "sample_page.h"
#include "sample_widgets.h"
#include "cuscheckbox.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <QtTest>
#include <functional>

namespace
{
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
QVariant sql(const QString &path, const QString &statement, bool *success = nullptr)
{
    const QString connection = QUuid::createUuid().toString();
    QVariant result;
    bool ok = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(path);
        if (database.open())
        {
            QSqlQuery query(database);
            ok = query.exec(statement);
            if (ok && query.next())
            {
                result = query.value(0);
            }
            if (!ok)
            {
                qWarning().noquote() << query.lastError().text();
            }
            query.finish();
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    if (success)
    {
        *success = ok;
    }
    return result;
}
struct Harness
{
    QTemporaryDir directory;
    QObject receiver;
    QThread projectThread, sampleThread;
    ProjectsStore *projects = new ProjectsStore;
    SampleStore *samples = new SampleStore;
    SamplePage *page = nullptr;
    ProjectRecords records;
    QMap<qint64, SampleSnapshot> snapshots;
    QMap<qint64, QString> errors;
    QMap<qint64, bool> saves;
    bool projectReady = false, sampleReady = false, projectLoaded = false, sampleClosed = false;
    qint64 request = 10000;
    explicit Harness() : directory(qEnvironmentVariable("SN100_SAMPLE_RESET_ROOT") + "/workflow-XXXXXX")
    {
        projects->moveToThread(&projectThread);
        samples->moveToThread(&sampleThread);
        QObject::connect(&projectThread, &QThread::finished, projects, &QObject::deleteLater);
        QObject::connect(&sampleThread, &QThread::finished, samples, &QObject::deleteLater);
        // 全部观察结果queued回测试主线程，不让worker修改主线程QSignalSpy容器。
        QObject::connect(projects, &ProjectsStore::ready, &receiver,
                         [this]() { projectReady = true; }, Qt::QueuedConnection);
        QObject::connect(projects, &ProjectsStore::snapshotReady, &receiver,
                         [this](qint64, ProjectRecords value, ProjectUnits)
                         { records = value; projectLoaded = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::ready, &receiver,
                         [this]() { sampleReady = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::closed, &receiver,
                         [this]() { sampleClosed = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::snapshotReady, &receiver,
                         [this](qint64 id, SampleSnapshot value) { snapshots[id] = value; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::succeeded, &receiver,
                         [this](qint64 id, QString, qint64) { saves[id] = true; }, Qt::QueuedConnection);
        QObject::connect(samples, &SampleStore::error, &receiver,
                         [this](qint64 id, QString, QString message) { errors[id] = message; }, Qt::QueuedConnection);
        projectThread.start();
        sampleThread.start();
        page = new SamplePage(samples);
        page->show();
    }
    ~Harness()
    {
        delete page;
        QMetaObject::invokeMethod(samples, "close", Qt::BlockingQueuedConnection);
        sampleThread.quit();
        sampleThread.wait();
        QMetaObject::invokeMethod(projects, "close", Qt::BlockingQueuedConnection);
        projectThread.quit();
        projectThread.wait();
    }
    QString database() const { return directory.path() + "/catalog.db"; }
    bool start()
    {
        if (!directory.isValid())
        {
            return false;
        }
        QMetaObject::invokeMethod(projects, "initialize", Qt::QueuedConnection,
                                  Q_ARG(QString, directory.path()), Q_ARG(bool, true));
        if (!waitFor([this]() { return projectReady; }))
        {
            return false;
        }
        QMetaObject::invokeMethod(projects, "load", Qt::QueuedConnection, Q_ARG(qint64, ++request));
        if (!waitFor([this]() { return projectLoaded && !records.isEmpty(); }))
        {
            return false;
        }
        page->setProjects(records);
        actor(true);
        QMetaObject::invokeMethod(samples, "initialize", Qt::QueuedConnection, Q_ARG(QString, directory.path()));
        return waitFor([this]() { return sampleReady && page->isReady() && !page->isBusy(); });
    }
    void actor(bool allowed)
    {
        AccountSession session;
        session.name = "offlineEngineer";
        session.authenticated = allowed;
        session.role = allowed ? 4 : -1;
        session.source = allowed ? AccountSessionSource::Simulated : AccountSessionSource::Guest;
        session.actionReady = false; // 本地编辑不要求设备ready。
        page->setSession(session);
    }
    bool save(SampleRack rack)
    {
        const qint64 id = ++request;
        QMetaObject::invokeMethod(samples, "saveRack", Qt::QueuedConnection,
                                  Q_ARG(qint64, id), Q_ARG(SampleRack, rack));
        return waitFor([this, id]() { return saves.contains(id) || errors.contains(id); }) && saves.contains(id);
    }
    bool load(SampleSnapshot *snapshot)
    {
        const qint64 id = ++request;
        QMetaObject::invokeMethod(samples, "load", Qt::QueuedConnection, Q_ARG(qint64, id));
        if (!waitFor([this, id]() { return snapshots.contains(id) || errors.contains(id); }) || errors.contains(id))
        {
            return false;
        }
        *snapshot = snapshots[id];
        return true;
    }
    SampleRack ordinary(const QList<SampleTaskState> &states) const
    {
        SampleRack rack;
        rack.name = "S01";
        for (int number = 1; number <= 8; ++number)
        {
            SampleHole hole;
            hole.number = number;
            rack.holes.append(hole);
        }
        rack.holes[0].sampleId = "1001";
        rack.holes[0].container = 0;
        rack.holes[0].revise = false;
        SampleTask task{records.first().id, "OFFLINE_RATE_B", states.size(), true};
        task.states = states;
        rack.holes[0].tasks.append(task);
        return rack;
    }
    QTableWidget *table() const { return page->findChild<QTableWidget *>("tblSetting"); }
    QList<SampleTaskState> viewStates(int row = 0) const
    {
        auto *view = dynamic_cast<SampleTaskView *>(table()->cellWidget(row, 4));
        return view ? view->states() : QList<SampleTaskState>();
    }
    bool openOrdinary()
    {
        QSignalSpy loaded(page, &SamplePage::snapshotChanged);
        page->reload();
        if (!waitFor([&]() { return !loaded.isEmpty() && !page->isBusy(); }))
        {
            return false;
        }
        auto *next = page->findChild<QPushButton *>("btnNextRack");
        if (table()->rowCount() == 3 && next)
        {
            next->click();
        }
        return table()->rowCount() == 8;
    }
    void click(const char *name)
    {
        auto *button = page->findChild<QPushButton *>(name);
        if (button)
        {
            QTest::mouseClick(button, Qt::LeftButton);
        }
    }
    bool closeStore()
    {
        sampleClosed = false;
        QMetaObject::invokeMethod(samples, "close", Qt::QueuedConnection);
        return waitFor([this]() { return sampleClosed && !page->isReady(); });
    }
    bool reopen(bool expectSuccess = true)
    {
        if (!closeStore())
        {
            return false;
        }
        sampleReady = false;
        errors.remove(0);
        QMetaObject::invokeMethod(samples, "initialize", Qt::QueuedConnection, Q_ARG(QString, directory.path()));
        if (expectSuccess)
        {
            return waitFor([this]() { return sampleReady && page->isReady() && !page->isBusy(); });
        }
        return waitFor([this]() { return errors.contains(0); }) && !sampleReady;
    }
};
void answer(QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, [button]()
    {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            auto *dialog = qobject_cast<QMessageBox *>(widget);
            if (dialog && dialog->isVisible() && dialog->objectName() == "sampleDiscardDialog")
            {
                dialog->done(button);
                return;
            }
        }
    });
}
}

class SampleResetWorkflowTest : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        const QString root = qEnvironmentVariable("SN100_SAMPLE_RESET_ROOT");
        QVERIFY(QDir::isAbsolutePath(root));
        QVERIFY(QDir().mkpath(root));
    }
    void resetErrorsOnlyChangesDraftThenSavesAndReopens()
    {
        Harness h;
        QVERIFY(h.start());
        QVERIFY(h.save(h.ordinary({SampleTaskState::Error, SampleTaskState::Finished,
                                  SampleTaskState::Reset, SampleTaskState::Idle})));
        QVERIFY(h.openOrdinary());
        h.click("btnResetErr");
        QVERIFY(h.page->hasUnsavedChanges());
        const QList<SampleTaskState> expected{SampleTaskState::Idle, SampleTaskState::Finished,
                                             SampleTaskState::Idle, SampleTaskState::Idle};
        QVERIFY(h.viewStates() == expected);
        SampleSnapshot before;
        QVERIFY(h.load(&before));
        QVERIFY(before.racks[1].holes[0].tasks[0].states.first() == SampleTaskState::Error);
        QVERIFY(!before.racks[1].holes[0].revise);
        h.click("btnMenuSave");
        QVERIFY(waitFor([&]() { return !h.page->isBusy() && !h.page->hasUnsavedChanges(); }));
        QVERIFY(h.reopen());
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        const SampleHole hole = saved.racks[1].holes[0];
        QVERIFY(hole.revise && hole.tasks[0].states == expected);
        QCOMPARE(hole.sampleId, QString("1001"));
        QCOMPARE(hole.container, 0);
        QCOMPARE(hole.tasks[0].rate, QString("OFFLINE_RATE_B"));
        QVERIFY(hole.tasks[0].prolonged);
        QCOMPARE(hole.tasks[0].repeats, 4);
    }
    void retestCancelDiscardSaveAndReopen()
    {
        Harness h;
        QVERIFY(h.start());
        const QList<SampleTaskState> original{SampleTaskState::Finished, SampleTaskState::Error, SampleTaskState::Reset};
        QVERIFY(h.save(h.ordinary(original)));
        QVERIFY(h.openOrdinary());
        bool sqlRead = false;
        const QString statesSql = QStringLiteral("SELECT states FROM sample_local_task_state");
        const QString reviseSql = QStringLiteral("SELECT revise FROM sample_local_hole_state WHERE hole_number=1 "
                                                 "AND rack_id=(SELECT id FROM sample_local_rack WHERE name='S01')");
        const QVariant savedStates = sql(h.database(), statesSql, &sqlRead);
        QVERIFY(sqlRead);
        QCOMPARE(savedStates.toString(), QString("[3,4,1]"));
        const QVariant savedRevise = sql(h.database(), reviseSql, &sqlRead);
        QVERIFY(sqlRead);
        QCOMPARE(savedRevise.toInt(), 0);
        h.click("btnResetFin");
        QVERIFY(h.page->hasUnsavedChanges());
        QVERIFY(h.viewStates() == QList<SampleTaskState>({SampleTaskState::Idle, SampleTaskState::Idle, SampleTaskState::Idle}));
        answer(QMessageBox::Cancel);
        QVERIFY(!h.page->prepareToLeave());
        QVERIFY(h.page->hasUnsavedChanges());
        QSignalSpy status(h.page, &SamplePage::statusChanged);
        QVERIFY(status.isValid());
        answer(QMessageBox::Discard);
        QVERIFY(h.page->prepareToLeave());
        QVERIFY(!h.page->hasUnsavedChanges());
        QVERIFY(h.viewStates() == original);
        const QString discarded = QStringLiteral("已放弃未保存更改，已恢复本地已保存设置；未发送设备命令。");
        QCOMPARE(h.page->property("lastStatus").toString(), discarded);
        QCOMPARE(status.count(), 1);
        QCOMPARE(status.first().first().toString(), discarded);
        const SampleHole committed = h.page->snapshot().racks[1].holes[0];
        QVERIFY(!committed.revise);
        QVERIFY(committed.tasks[0].states == original);
        QCOMPARE(sql(h.database(), statesSql, &sqlRead), savedStates);
        QVERIFY(sqlRead);
        QCOMPARE(sql(h.database(), reviseSql, &sqlRead), savedRevise);
        QVERIFY(sqlRead);
        h.click("btnResetFin");
        h.click("btnMenuSave");
        QVERIFY(waitFor([&]() { return !h.page->isBusy() && !h.page->hasUnsavedChanges(); }));
        QVERIFY(h.reopen());
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        QVERIFY(saved.racks[1].holes[0].revise);
        QVERIFY(saved.racks[1].holes[0].tasks[0].states ==
                QList<SampleTaskState>({SampleTaskState::Idle, SampleTaskState::Idle, SampleTaskState::Idle}));
    }
    void selectedTestingRefusesBothActionsWithoutPartialDraft()
    {
        for (const char *button : {"btnResetErr", "btnResetFin"})
        {
            Harness h;
            QVERIFY(h.start());
            auto rack = h.ordinary({SampleTaskState::Error});
            SampleTask testing{h.records[0].id, "OFFLINE_RATE_A", 1, false};
            testing.states = {SampleTaskState::Testing};
            rack.holes[1].tasks.append(testing);
            rack.holes[1].revise = false;
            QVERIFY(h.save(rack));
            QVERIFY(h.openOrdinary());
            auto *checked = qobject_cast<CusCheckBox *>(h.table()->cellWidget(1, 0));
            QVERIFY(checked);
            checked->setCheckState(Qt::Checked);
            h.table()->setCurrentCell(0, 1);
            h.click(button);
            QVERIFY(!h.page->hasUnsavedChanges());
            QVERIFY(h.viewStates(0) == QList<SampleTaskState>({SampleTaskState::Error}));
            QVERIFY(h.viewStates(1) == QList<SampleTaskState>({SampleTaskState::Testing}));
            QVERIFY(h.page->property("lastStatus").toString().contains(QStringLiteral("测试状态")));
        }
    }
    void checkedNonTestingHolesKeepIdentityAndBecomeRevisable()
    {
        Harness h;
        QVERIFY(h.start());
        auto rack = h.ordinary({SampleTaskState::Error});
        SampleTask finished{h.records[0].id, "OFFLINE_RATE_A", 1, false};
        finished.states = {SampleTaskState::Finished};
        rack.holes[1].tasks.append(finished);
        rack.holes[1].sampleId = "1002";
        rack.holes[1].revise = false;
        QVERIFY(h.save(rack));
        QVERIFY(h.openOrdinary());
        auto *check = qobject_cast<CusCheckBox *>(h.table()->cellWidget(1, 0));
        QVERIFY(check);
        check->setCheckState(Qt::Checked);
        h.table()->setCurrentCell(0, 1);
        h.click("btnResetErr");
        QVERIFY(h.page->hasUnsavedChanges());
        QVERIFY(h.viewStates(1) == finished.states);
        h.click("btnMenuSave");
        QVERIFY(waitFor([&]() { return !h.page->isBusy() && !h.page->hasUnsavedChanges(); }));
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        QVERIFY(saved.racks[1].holes[0].revise && saved.racks[1].holes[1].revise);
        QCOMPARE(saved.racks[1].holes[1].sampleId, QString("1002"));
        QVERIFY(saved.racks[1].holes[1].tasks[0].states == finished.states);
    }
    void writeFailureRetainsCommittedStateAndDraftForRetry()
    {
        Harness h;
        QVERIFY(h.start());
        QVERIFY(h.save(h.ordinary({SampleTaskState::Error, SampleTaskState::Finished})));
        QVERIFY(h.openOrdinary());
        h.click("btnResetErr");
        bool ok = false;
        sql(h.database(), "CREATE TRIGGER reject_state BEFORE INSERT ON sample_local_task_state "
                          "BEGIN SELECT RAISE(ABORT,'anonymous forced state write failure'); END", &ok);
        QVERIFY(ok);
        QSignalSpy finished(h.page, &SamplePage::operationFinished);
        h.click("btnMenuSave");
        QVERIFY(waitFor([&]() { return !finished.isEmpty() && !h.page->isBusy(); }));
        QVERIFY(h.page->hasUnsavedChanges());
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[4,3]"));
        sql(h.database(), "DROP TRIGGER reject_state", &ok);
        QVERIFY(ok);
        h.click("btnMenuSave");
        QVERIFY(waitFor([&]() { return !h.page->isBusy() && !h.page->hasUnsavedChanges(); }));
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[0,3]"));
    }
    void staleDraftAndRevokedIdentityCannotOverwriteTesting()
    {
        Harness h;
        QVERIFY(h.start());
        QVERIFY(h.save(h.ordinary({SampleTaskState::Error, SampleTaskState::Finished})));
        QVERIFY(h.openOrdinary());
        h.click("btnResetErr");
        bool ok = false;
        sql(h.database(), "UPDATE sample_local_task_state SET states='[2,3]'", &ok);
        QVERIFY(ok);
        QSignalSpy finished(h.page, &SamplePage::operationFinished);
        h.click("btnMenuSave");
        QVERIFY(waitFor([&]() { return !finished.isEmpty() && !h.page->isBusy(); }));
        QVERIFY(h.page->hasUnsavedChanges());
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[2,3]"));
        h.actor(false);
        QVERIFY(!h.page->findChild<QPushButton *>("btnResetErr")->isEnabled());
        QVERIFY(!h.page->findChild<QPushButton *>("btnResetFin")->isEnabled());
        SampleSnapshot snapshot;
        QVERIFY(h.load(&snapshot));
        snapshot.racks[1].holes[0].tasks[0].states = {SampleTaskState::Idle, SampleTaskState::Idle};
        QVERIFY(!h.save(snapshot.racks[1]));
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[2,3]"));
    }
    void oldV1UpgradePreservesLegacyFieldsAndDefaultIdle()
    {
        Harness h;
        QVERIFY(h.start());
        auto rack = h.ordinary({SampleTaskState::Idle, SampleTaskState::Idle});
        rack.holes[0].tasks[0].states.clear();
        rack.holes[0].revise = true;
        QVERIFY(h.save(rack));
        QVERIFY(h.closeStore());
        bool ok = false;
        // 基础v1表由真实owner创建，移除本轮空扩展得到原有表示，不手造项目/样本schema。
        sql(h.database(), "DROP TABLE sample_local_task_state", &ok); QVERIFY(ok);
        sql(h.database(), "DROP TABLE sample_local_hole_state", &ok); QVERIFY(ok);
        sql(h.database(), "DELETE FROM catalog_metadata WHERE key='sample_task_state_schema'", &ok); QVERIFY(ok);
        h.sampleReady = false;
        QMetaObject::invokeMethod(h.samples, "initialize", Qt::QueuedConnection, Q_ARG(QString, h.directory.path()));
        QVERIFY(waitFor([&]() { return h.sampleReady && h.page->isReady() && !h.page->isBusy(); }));
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        const SampleHole hole = saved.racks[1].holes[0];
        QVERIFY(hole.revise);
        QCOMPARE(hole.sampleId, QString("1001"));
        QCOMPARE(hole.container, 0);
        QCOMPARE(hole.tasks[0].repeats, 2);
        QVERIFY(hole.tasks[0].prolonged && hole.tasks[0].states.isEmpty());
        QVERIFY(sampleTaskStates(hole.tasks[0]) == QList<SampleTaskState>({SampleTaskState::Idle, SampleTaskState::Idle}));
    }
    void malformedStateAndUnknownVersionArePreserved()
    {
        Harness h;
        QVERIFY(h.start());
        auto rack = h.ordinary({SampleTaskState::Error, SampleTaskState::Finished});
        QVERIFY(h.save(rack));
        bool ok = false;
        sql(h.database(), "UPDATE sample_local_task_state SET states='[4,99]'", &ok);
        QVERIFY(ok);
        SampleSnapshot saved;
        QVERIFY(!h.load(&saved));
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[4,99]"));
        sql(h.database(), "UPDATE catalog_metadata SET value='future' WHERE key='sample_task_state_schema'", &ok);
        QVERIFY(ok);
        QVERIFY(h.reopen(false));
        QCOMPARE(sql(h.database(), "SELECT value FROM catalog_metadata WHERE key='sample_task_state_schema'").toString(), QString("future"));
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[4,99]"));
    }
    void emergencyAndZeroNinetyNineRepetitionsRoundTrip()
    {
        Harness h;
        QVERIFY(h.start());
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        auto emergency = saved.racks.first();
        SampleTask task{h.records[0].id, "OFFLINE_RATE_A", 99, false};
        for (int i = 0; i < 99; ++i)
        {
            task.states.append(i == 98 ? SampleTaskState::Error : SampleTaskState::Finished);
        }
        emergency.holes[0].tasks.append(task);
        emergency.holes[0].revise = false;
        emergency.holes[1].tasks.append({h.records[0].id, "OFFLINE_RATE_B", 0, true});
        QVERIFY(h.save(emergency));
        QVERIFY(h.reopen());
        QVERIFY(h.load(&saved));
        QCOMPARE(saved.racks[0].holes[0].tasks[0].states.size(), 99);
        QVERIFY(saved.racks[0].holes[0].tasks[0].states.last() == SampleTaskState::Error);
        QCOMPARE(saved.racks[0].holes[1].tasks[0].repeats, 0);
        QVERIFY(saved.racks[0].holes[1].tasks[0].states.isEmpty());
    }
    void invalidDraftCannotReplaceCommittedStates()
    {
        Harness h;
        QVERIFY(h.start());
        QVERIFY(h.save(h.ordinary({SampleTaskState::Error, SampleTaskState::Finished})));
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        auto invalid = saved.racks[1];
        invalid.holes[0].tasks[0].states = {SampleTaskState::Error};
        QVERIFY(!h.save(invalid));
        invalid = saved.racks[1];
        invalid.holes[0].tasks[0].states[0] = static_cast<SampleTaskState>(99);
        QVERIFY(!h.save(invalid));
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[4,3]"));
    }
    void currentTestingAndDestructiveActionsAreGuarded()
    {
        Harness h;
        QVERIFY(h.start());
        QVERIFY(h.save(h.ordinary({SampleTaskState::Testing})));
        QVERIFY(h.openOrdinary());
        for (const char *button : {"btnResetErr", "btnResetFin"})
        {
            h.click(button);
            QVERIFY(!h.page->hasUnsavedChanges());
            QVERIFY(h.viewStates() == QList<SampleTaskState>({SampleTaskState::Testing}));
            QVERIFY(h.page->property("lastStatus").toString().contains(QStringLiteral("测试状态")));
        }
        QVERIFY(!h.page->findChild<QPushButton *>("btnHoleDetail")->isEnabled());
        QVERIFY(!(h.table()->item(0, 1)->flags() & Qt::ItemIsEditable));
        SampleSnapshot saved;
        QVERIFY(h.load(&saved));
        auto otherHoleOnly = saved.racks[1];
        otherHoleOnly.holes[1].sampleId = "2002";
        QVERIFY(h.save(otherHoleOnly)); // 未改Testing孔时仍可准备其他孔位。
        const qint64 removeId = ++h.request;
        QMetaObject::invokeMethod(h.samples, "removeRack", Qt::QueuedConnection,
                                  Q_ARG(qint64, removeId), Q_ARG(qint64, otherHoleOnly.id));
        QVERIFY(waitFor([&]() { return h.errors.contains(removeId); }));
        const qint64 clearId = ++h.request;
        QMetaObject::invokeMethod(h.samples, "clearOrdinaryRacks", Qt::QueuedConnection,
                                  Q_ARG(qint64, clearId));
        QVERIFY(waitFor([&]() { return h.errors.contains(clearId); }));
        QCOMPARE(sql(h.database(), "SELECT COUNT(*) FROM sample_local_rack WHERE emergency=0").toInt(), 1);
        QCOMPARE(sql(h.database(), "SELECT states FROM sample_local_task_state").toString(), QString("[2]"));
    }
    void incompleteExtensionIsNotSilentlyRecreated()
    {
        Harness h;
        QVERIFY(h.start());
        QVERIFY(h.save(h.ordinary({SampleTaskState::Error})));
        QVERIFY(h.closeStore());
        bool ok = false;
        sql(h.database(), "DROP TABLE sample_local_task_state", &ok);
        QVERIFY(ok);
        h.sampleReady = false;
        h.errors.remove(0);
        QMetaObject::invokeMethod(h.samples, "initialize", Qt::QueuedConnection, Q_ARG(QString, h.directory.path()));
        QVERIFY(waitFor([&]() { return h.errors.contains(0); }));
        QVERIFY(!h.sampleReady);
        QCOMPARE(sql(h.database(), "SELECT COUNT(*) FROM sqlite_master WHERE name='sample_local_task_state'").toInt(), 0);
        QCOMPARE(sql(h.database(), "SELECT value FROM catalog_metadata WHERE key='sample_task_state_schema'").toString(),
                 QString("sn100-sample-task-state-v1"));
    }
};
QTEST_MAIN(SampleResetWorkflowTest)
#include "sample_reset_workflow_test.moc"
