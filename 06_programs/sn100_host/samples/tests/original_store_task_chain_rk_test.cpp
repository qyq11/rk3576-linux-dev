#include "original_task_flow_fixture.h"
#include "samples/sample_store.h"
#include "reagents/reagent_store.h"
#include "original_files/original_ini.h"
#include <QTemporaryDir>
#include <QDate>
#include <memory>

class OriginalStoreTaskChainRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    OriginalTaskFlowFixture fixture;
    std::unique_ptr<SampleStore> samples;
    std::unique_ptr<ReagentStore> reagents;
    AccountSession actor;
    QByteArray reagentReply() const
    {
        QByteArray reply="Ur[4]";
        for (const auto &hole : QList<QPair<QByteArray,QByteArray>>{{"r_1","R1"},{"r_2","Cleaner"},{"s_1","IBS"},{"s_2","Cleaner"}})
        {
            const QByteArray type=hole.second=="Cleaner" ? "cleaner" : hole.second=="IBS" ? "s_ibs" : "r_reagent";
            reply+='['+hole.first+"]["+type+"]["+hole.second+"][Default][B1][0][100][2000][1][1][2026/10/27 12:00:00][2031/01/01][500][5000][2000]";
        }
        return reply+"[0]";
    }
    void synchronize(quint64 connection=7)
    {
        QSignalSpy packets(samples.get(),&SampleStore::originalConfigurationPacket);
        QSignalSpy state(samples.get(),&SampleStore::originalConfigurationState);
        samples->beginOriginalDeviceSync(connection,true);
        QCOMPARE(packets.size(),1); QCOMPARE(packets.last()[0].toByteArray(),QByteArray("Ur"));
        samples->receiveDevicePacket(reagentReply(),connection,1);
        QCOMPARE(packets.size(),3); QVERIFY(packets.at(1)[0].toByteArray().startsWith("Up["));
        QCOMPARE(packets.last()[0].toByteArray(),QByteArray("UP[PT]"));
        samples->receiveDevicePacket(fixture.reply(),connection,2);
        QVERIFY(!state.isEmpty()); QVERIFY(state.last()[1].toBool());
    }
    void saveRack()
    {
        SampleRack rack; rack.name="S01";
        for (int i=1;i<=8;++i)
        {
            SampleHole hole; hole.number=i;
            if (i==1)
            {
                hole.sampleId="17";
                SampleTask task; task.projectId=7; task.rate="1/2"; task.repeats=1;
                hole.tasks.append(task);
            }
            rack.holes.append(hole);
        }
        QSignalSpy saved(samples.get(),&SampleStore::succeeded);
        samples->saveRack(1,rack); QCOMPARE(saved.size(),1);
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("样本后台任务链只在RK ARM64运行。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
    }
    void init()
    {
        directory.reset(new QTemporaryDir(QDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")).filePath("store_chain_XXXXXX")));
        QVERIFY(directory->isValid()); fixture.create(directory->path()); QVERIFY(!QTest::currentTestFailed());
        samples.reset(new SampleStore); reagents.reset(new ReagentStore);
        QSignalSpy errors(samples.get(),&SampleStore::error);
        actor.role=0; actor.authenticated=true; actor.actionReady=true; actor.source=AccountSessionSource::Configured;
        samples->setSession(actor); samples->initializeOriginal(fixture.root);
        reagents->setSession(actor); reagents->initializeOriginal(fixture.root);
        // 正式壳收到Ur提交成功也会重读页面/Editor；更新指纹后才允许再编辑。
        connect(samples.get(),&SampleStore::originalReagentsChanged,reagents.get(),&ReagentStore::load);
        QVERIFY(errors.isEmpty());
    }
    void cleanup()
    {
        samples->close(); reagents->close(); samples.reset(); reagents.reset(); directory.reset();
    }
    void establishedRoleZeroSavesOriginalSampleWithOriginalProjectNumber()
    {
        QVERIFY(SampleStore::canEdit(actor)); saveRack(); QVERIFY(!QTest::currentTestFailed());
        QSignalSpy snapshots(samples.get(),&SampleStore::snapshotReady);
        samples->load(2); QCOMPARE(snapshots.size(),1);
        const auto snapshot=qvariant_cast<SampleSnapshot>(snapshots.last()[1]);
        bool found=false;
        for (const auto &rack : snapshot.racks) if(rack.name=="S01")
        { QCOMPARE(rack.holes[0].tasks[0].projectId,qint64(7)); found=true; }
        QVERIFY(found);
        actor.authenticated=false; QVERIFY(!SampleStore::canEdit(actor));
        actor.authenticated=true; actor.role=6; QVERIFY(!SampleStore::canEdit(actor));
    }
    void singleHoleUsesNewProtocolButtonPriorityAndRejectsStaleConnection()
    {
        synchronize(); QVERIFY(!QTest::currentTestFailed());
        connect(reagents.get(),&ReagentStore::originalHoleSaved,samples.get(),[this](const QString &hole)
        { samples->prepareOriginalReagentUpdate(hole,true,7); });
        QSignalSpy packets(samples.get(),&SampleStore::originalConfigurationPacket);
        QSignalSpy errors(samples.get(),&SampleStore::deviceTaskError);
        reagents->clearHole("R_1"); QCOMPARE(packets.size(),1);
        QCOMPARE(packets.last()[0].toByteArray(),QByteArray("Us[r_1][r_reagent][none][][][0][99][0][][][0][0][0][0][1]"));
        samples->prepareOriginalReagentUpdate("R_1",true,6); QCOMPARE(errors.size(),1); QCOMPARE(packets.size(),1);
        samples->beginOriginalDeviceSync(8,false);
        samples->prepareOriginalReagentUpdate("R_1",true,8); QCOMPARE(packets.size(),1);
    }
    void preparationRackRequestProgressAndCurveKeepTheSameTaskIdentity()
    {
        synchronize(); QVERIFY(!QTest::currentTestFailed()); saveRack(); QVERIFY(!QTest::currentTestFailed());
        QSignalSpy prepared(samples.get(),&SampleStore::deviceTasksPrepared);
        samples->prepareDeviceTasks(11,true); QCOMPARE(prepared.size(),1);
        QVERIFY2(prepared.last()[1].toBool(),qPrintable(prepared.last()[2].toString()));
        QSignalSpy tasks(samples.get(),&SampleStore::deviceRackTasksPrepared);
        QSignalSpy processed(samples.get(),&SampleStore::devicePacketProcessed);
        samples->receiveDevicePacket("Tt[S01][1][1][BAR99][0]",7,3);
        QCOMPARE(tasks.size(),1); QVERIFY2(processed.last()[3].toBool(),qPrintable(processed.last()[4].toString()));
        QCOMPARE(tasks.last()[0].toByteArray(),QByteArray("Tt[S01][1][SAM][41][1][cup][BAR99][PT][0][0][1/2][Default][s_1][1][0][]"));
        samples->receiveDevicePacket("TD[S01][1][41]",7,4); QVERIFY(processed.last()[3].toBool());
        samples->receiveDevicePacket("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][1][1][37.1][r_1][1/2][100][1][s_1][][][][405nm-1,2,3,4]",7,5);
        QVERIFY2(processed.last()[3].toBool(),qPrintable(processed.last()[4].toString()));
        QCOMPARE(fixture.value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),42);
        QCOMPARE(fixture.value("result/results.db","SELECT original_value FROM report_results WHERE task_id=41").toString(),QString());
        OriginalIni::Settings result; QString message;
        QVERIFY(OriginalIni::read(QDir(fixture.root).filePath("result/"+QDate::currentDate().toString("yyyy-MM-dd")+"/41"),&result,&message));
        QCOMPARE(result.values.value("rk/calculation_state").toString(),QStringLiteral("waiting"));
        QCOMPARE(result.values.value("base/task_id").toInt(),41);
        samples->receiveDevicePacket("Tt[S01][1][1][BAR99][0]",7,6);
        QCOMPARE(tasks.last()[0].toByteArray(),QByteArray("Tt[S01][0]"));
        QCOMPARE(fixture.value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),42);
    }
    void staleReagentPacketDoesNotWriteTheOriginalDatabase()
    {
        samples->beginOriginalDeviceSync(8,true);
        QSignalSpy processed(samples.get(),&SampleStore::devicePacketProcessed);
        QByteArray incoming=reagentReply(); incoming.replace("[2000]","[1000]");
        samples->receiveDevicePacket(incoming,7,99);
        QCOMPARE(processed.size(),1); QVERIFY(!processed.last()[3].toBool());
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toDouble(),2000.0);
    }
};
QTEST_GUILESS_MAIN(OriginalStoreTaskChainRkTest)
#include "original_store_task_chain_rk_test.moc"
