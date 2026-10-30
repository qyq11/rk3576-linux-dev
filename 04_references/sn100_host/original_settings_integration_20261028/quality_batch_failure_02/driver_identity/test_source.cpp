// 在RK用正式主窗、页面和工作线程检查任务链；TCP对端为本进程匿名模拟，未连接仪器。
#include "original_task_flow_fixture.h"
#include "shell/frmmain.h"
#include "data/hospital_store.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include "instrument/device_control.h"
#include "ui/cusmessagebox.h"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QScrollArea>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <functional>
#include <memory>

using namespace Sn100Protocol;
class OriginalMainTaskChainRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    OriginalTaskFlowFixture fixture;
    QThread worker;
    HospitalStore *hospital=nullptr;
    AccountStore *accounts=nullptr;
    ProjectsStore *projects=nullptr;
    ReagentStore *reagents=nullptr;
    SampleStore *samples=nullptr;
    std::unique_ptr<QScrollArea> host;
    frmMain *shell=nullptr;
    QTcpServer server;
    QPointer<QTcpSocket> peer;
    FrameDecoder decoder{Direction::HostToDevice};
    QElapsedTimer clock;
    QList<QByteArray> received;
    int mainStatus=2;
    bool linuxNew=true;
    QElapsedTimer measurementClock;
    QString measurementScene;
    QMap<QString,qint64> measurementTimes;

    struct InitFailureCleanup
    {
        std::function<void()> close;
        ~InitFailureCleanup() { if(QTest::currentTestFailed()) close(); }
    };

    void beginMeasurement(const QString &scene)
    {
        measurementScene=scene; measurementTimes.clear(); measurementClock.start();
    }
    void markMeasurement(const QString &name)
    {
        if (measurementScene.isEmpty() || measurementTimes.contains(name)) return;
        const qint64 elapsed=measurementClock.elapsed(); measurementTimes.insert(name,elapsed);
        qInfo().noquote()<<"RK_CHAIN_TIMING scene="+measurementScene<<"event="+name<<"elapsed_ms="<<elapsed;
    }
    void saveEvidence(const QString &name, const QString &scope)
    {
        QJsonObject times;
        for(auto i=measurementTimes.cbegin();i!=measurementTimes.cend();++i) times.insert(i.key(),i.value());
        QJsonObject result{{"scene",measurementScene},{"scope",scope},{"elapsed_ms",times},
            {"sample_count",1},{"real_instrument",false},{"physical_input",false}};
        qInfo().noquote()<<"RK_CHAIN_MEASUREMENT"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
        const QString folder=qEnvironmentVariable("SN100_GUI_EVIDENCE_DIR");
        if (!QDir::isAbsolutePath(folder)) return;
        QVERIFY(QDir().mkpath(folder));
        // 抓取原1920×1080逻辑主窗，不把它冒充1600×900屏幕合成或物理点击证据。
        QVERIFY(shell->grab().save(QDir(folder).filePath(name+".png")));
        QFile output(QDir(folder).filePath(name+"_measurement.json"));
        QVERIFY(output.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        const QByteArray bytes=QJsonDocument(result).toJson(QJsonDocument::Indented);
        QCOMPARE(output.write(bytes),qint64(bytes.size()));
    }

    QByteArray status() const
    {
        QByteArray result="Qj[20][RK_TEST]["+QByteArray::number(mainStatus)+"][1][0][][1][0][1][ANON][ANON][0][0][0][20250905016][0][0][0][0][0]["+(linuxNew ? "LinuxNew" : "Legacy")+"][6]";
        for (const auto &block : QList<QPair<int,int>>{{16,14},{6,3},{17,4},{3,7},{14,24},{12,8}})
        {
            result+='['+QByteArray::number(block.first)+"]["+QByteArray::number(block.second)+']';
            for (int i=0;i<block.second;++i) result+=block.first==16 && i==13 ? "[0]" : "[1]";
        }
        return result;
    }
    void reply(const QByteArray &payload)
    {
        QVERIFY(peer);
        qInfo().noquote()<<"ANON_DEVICE_TX"<<payload;
        for (const auto &frame : encodeMessage(payload,Direction::DeviceToHost)) peer->write(frame);
        peer->flush();
    }
    QByteArray allReagents() const
    {
        return "Ur[4][r_1][r_reagent][R1][Default][B1][0][100][2000][1][1][2026/10/27 12:00:00][2031/01/01][500][5000][2000]"
               "[r_2][cleaner][Cleaner][Default][B1][0][100][2000][1][1][2026/10/27 12:00:00][2031/01/01][500][5000][2000]"
               "[s_1][s_ibs][IBS][Default][B1][0][100][2000][1][1][2026/10/27 12:00:00][2031/01/01][500][5000][2000]"
               "[s_2][cleaner][Cleaner][Default][B1][0][100][2000][1][1][2026/10/27 12:00:00][2031/01/01][500][5000][2000][0]";
    }
    void receive(const QByteArray &payload)
    {
        received.append(payload); qInfo().noquote()<<"ANON_DEVICE_RX"<<payload;
        if (payload=="Qj") reply(status());
        else if(payload=="Ur") reply(allReagents());
        else if(payload=="UP[PT]") reply(fixture.reply());
        else if(payload=="UP[OTHER]")
        {
            QByteArray other=fixture.reply();
            // OTHER从PT匿名库复制，项目和结果的LIS代码仍为PT，只替换设备项目标识。
            other.replace("UP[t][PT][9]","UP[t][OTHER][10]");
            other.replace("[R1]","[Cleaner]"); reply(other);
        }
        else if(payload=="Ts") { markMeasurement("start_command_observed"); reply("Ts[1]"); mainStatus=5; reply(status()); }
        else if(payload=="Tp[0]") { mainStatus=6; reply(status()); }
        else if(payload.startsWith("Us["))
        {
            markMeasurement("single_hole_command_observed");
            // 模拟设备按Us保存后以原Ur的不同字段次序回读，不把TX入队当执行完成。
            QList<QByteArray> values;
            for (const auto &field : payload.mid(3,payload.size()-4).split(']')) values.append(field.startsWith('[') ? field.mid(1) : field);
            QCOMPARE(values.size(),15);
            QByteArray response="Ur[1]";
            for (int index : {0,1,2,3,4}) response+='['+values[index]+']';
            response+="[0]["+values[11]+"]["+values[5]+"]["+values[6]+"]["+values[7]+"]["+values[8]+"]["+values[9]+"]["+values[10]+"]["+values[12]+"]["+values[13]+"][1]";
            reply(response);
        }
        else if(payload.startsWith("Tt[") && payload!="Tt[S01][0]") markMeasurement("rack_tasks_observed");
    }
    void clickMain(const char *name)
    {
        auto *button=shell->findChild<QPushButton*>(QString::fromLatin1(name)); QVERIFY(button);
        QElapsedTimer deadline; deadline.start();
        while(!button->isEnabled() && deadline.elapsed()<15000) QTest::qWait(50);
        if(!button->isEnabled())
        {
            int tasks=0;
            for(const auto &rack : shell->samplePage()->snapshot().racks)
                for(const auto &hole : rack.holes) tasks+=hole.tasks.size();
            qInfo().noquote()<<"MAIN_BUTTON_BLOCKED"<<name<<"reason="<<button->toolTip()
                <<"authenticated="<<shell->accountSession().authenticated<<"role="<<shell->accountSession().role
                <<"sample_ready_busy_dirty="<<shell->samplePage()->isReady()<<shell->samplePage()->isBusy()<<shell->samplePage()->hasUnsavedChanges()
                <<"project_ready_busy_dirty="<<shell->projectPage()->isReady()<<shell->projectPage()->isBusy()<<shell->projectPage()->hasUnsavedChanges()
                <<"reagent_ready_busy_dirty="<<shell->reagentPage()->isReady()<<shell->reagentPage()->isBusy()<<shell->reagentPage()->hasUnsavedChanges()
                <<"task_count="<<tasks<<"configuration="<<shell->property("originalConfigurationReady")
                <<"device_connected_fresh_status="<<shell->deviceControl()->state().connected<<shell->deviceControl()->state().fresh<<shell->deviceControl()->state().status.mainStatus
                <<"button_visible_geometry="<<button->isVisible()<<button->geometry()<<button->mapToGlobal(QPoint())<<"host_geometry="<<host->geometry();
            const QString folder=qEnvironmentVariable("SN100_GUI_EVIDENCE_DIR");
            if(QDir::isAbsolutePath(folder))
            { QVERIFY(QDir().mkpath(folder)); QVERIFY(shell->grab().save(QDir(folder).filePath(QString::fromLatin1(name)+"_blocked.png"))); }
        }
        QVERIFY2(button->isEnabled(),qPrintable(button->toolTip()));
        QSignalSpy clicks(button,&QPushButton::clicked);
        QTimer answer;
        answer.setInterval(25);
        connect(&answer,&QTimer::timeout,this,[]()
        {
            auto *box=qobject_cast<CusMessageBox*>(QApplication::activeModalWidget());
            if (box && box->isReadyForInput())
                if (auto *sure=box->findChild<QPushButton*>("btnSure")) QTest::mouseClick(sure,Qt::LeftButton);
        });
        answer.start(); QTest::mouseClick(button,Qt::LeftButton); answer.stop();
        QCOMPARE(clicks.size(),1);
    }
    void savedSample()
    {
        // 沿正式页面录入：添加普通架、编辑样本号、选择原项目，再更新设置。
        // 不调用Store预置样本，以便发现页面未接业务或项目编号传错。
        QVERIFY(shell->selectMainPage(1));
        auto *page=shell->samplePage();
        auto *add=page->findChild<QWidget*>("btnAdd");
        QVERIFY(add);
        const auto logEntryState=[&](const char *stage)
        {
            qInfo().noquote()<<"SAMPLE_ENTRY_STATE"<<stage<<"main_page="<<shell->currentMainPage()
                <<"page_visible_ready_busy_dirty="<<page->isVisible()<<page->isReady()<<page->isBusy()<<page->hasUnsavedChanges()
                <<"authenticated_role="<<shell->accountSession().authenticated<<shell->accountSession().role
                <<"configuration="<<shell->property("originalConfigurationReady");
            for(auto *candidate:page->findChildren<QWidget*>("btnAdd"))
            {
                qInfo().noquote()<<"SAMPLE_ENTRY_ADD"<<candidate->metaObject()->className()
                    <<"visible_hidden_enabled="<<candidate->isVisible()<<candidate->isHidden()<<candidate->isEnabled();
                for(auto *parent=candidate->parentWidget();parent;parent=parent->parentWidget())
                    qInfo().noquote()<<"SAMPLE_ENTRY_PARENT"<<parent->metaObject()->className()<<parent->objectName()
                        <<"visible_hidden_enabled="<<parent->isVisible()<<parent->isHidden()<<parent->isEnabled();
            }
        };
        logEntryState("before_wait");
        // 项目回读成功后，页面仍可能排队重读样本快照；等其正常完成，不改控件状态。
        QElapsedTimer readyDeadline; readyDeadline.start();
        while((!page->isReady() || page->isBusy() || !add->isVisible() || !add->isEnabled()) && readyDeadline.elapsed()<15000)
            QTest::qWait(50);
        if(!page->isReady() || page->isBusy() || !add->isVisible() || !add->isEnabled())
        {
            logEntryState("wait_timeout");
            const QString folder=qEnvironmentVariable("SN100_GUI_EVIDENCE_DIR");
            if(QDir::isAbsolutePath(folder))
            { QVERIFY(QDir().mkpath(folder)); QVERIFY(shell->grab().save(QDir(folder).filePath("sample_add_blocked.png"))); }
        }
        else qInfo()<<"SAMPLE_ENTRY_READY_AFTER_MS"<<readyDeadline.elapsed();
        QVERIFY(page->isReady()); QVERIFY(!page->isBusy()); QVERIFY(add->isVisible()); QVERIFY(add->isEnabled());
        QTest::mouseClick(add,Qt::LeftButton);
        auto *table=page->findChild<QTableWidget*>("tblSetting"); QVERIFY(table);
        QTRY_VERIFY(table->isVisible()); QCOMPARE(table->rowCount(),8);
        auto *item=table->item(0,1); QVERIFY(item);
        table->scrollToItem(item);
        QTest::mouseClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualItemRect(item).center());
        QTest::mouseDClick(table->viewport(),Qt::LeftButton,Qt::NoModifier,table->visualItemRect(item).center());
        QLineEdit *editor=nullptr;
        QTRY_VERIFY(([&](){ for(auto *candidate:table->findChildren<QLineEdit*>())
            if(candidate->isVisible()) { editor=candidate; return true; } return false; })());
        QTest::keyClick(editor,Qt::Key_A,Qt::ControlModifier); QTest::keyClicks(editor,"17");
        QTest::keyClick(editor,Qt::Key_Return); QTRY_COMPARE(table->item(0,1)->text(),QStringLiteral("17"));
        QPushButton *project=nullptr;
        QTRY_VERIFY(([&](){ for(auto *candidate:page->findChildren<QPushButton*>("sampleProject_7"))
            if(candidate->isVisible() && candidate->isEnabled()) { project=candidate; return true; } return false; })());
        QTest::mouseClick(project,Qt::LeftButton); QVERIFY(project->isChecked());
        auto *save=page->findChild<QPushButton*>("btnMenuSave"); QVERIFY(save && save->isEnabled());
        QSignalSpy saved(page,&SamplePage::operationFinished);
        QTest::mouseClick(save,Qt::LeftButton);
        QTRY_VERIFY(([&](){ for(const auto &event:saved)
            if(event[0].toString()=="saveRack" && event[1].toBool()) return true; return false; })());
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges());
        bool found=false;
        for(const auto &rack:page->snapshot().racks) if(rack.name=="S01")
        {
            found=true; QCOMPARE(rack.holes.size(),8); QCOMPARE(rack.holes.first().sampleId,QStringLiteral("17"));
            QCOMPARE(rack.holes.first().tasks.size(),1); QCOMPARE(rack.holes.first().tasks.first().projectId,qint64(7));
            QCOMPARE(rack.holes.first().tasks.first().rate,QStringLiteral("1/2"));
            QCOMPARE(rack.holes.first().tasks.first().repeats,1);
        }
        QVERIFY(found); QVERIFY(!received.contains("Ts"));
        QCOMPARE(fixture.value("result/results.db","SELECT COUNT(*) FROM report_data").toInt(),0);
        // 原普通架是本次进程的内存预设；不声称重启后仍保留，也不创建不存在的样本设置库。
        auto *back=page->findChild<QPushButton*>("btnMenuReturn"); QVERIFY(back && back->isEnabled());
        QTest::mouseClick(back,Qt::LeftButton); QTRY_VERIFY(!back->isVisible());
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("正式主窗模拟任务链只在RK ARM64执行。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
        HospitalStore::registerTypes(); AccountStore::registerTypes(); ProjectsStore::registerTypes();
        SampleStore::registerTypes(); ReagentStore::registerTypes(); clock.start();
        // EGLFS在一个QApplication内保留首个原生顶层窗口的显示上下文。
        // 各用例只更换其中的业务主窗；实际程序重开另由独立进程场景核对。
        host.reset(new QScrollArea); host->setWidgetResizable(false); host->resize(1600,900);
    }
    void init()
    {
        // 初始化中的QVERIFY/QTRY提前返回时，也要释放本例监听、窗口和工作线程。
        const InitFailureCleanup failureCleanup{[this](){ cleanup(); }};
        mainStatus=2; linuxNew=true; received.clear(); decoder.reset(); measurementScene.clear(); measurementTimes.clear();
        directory.reset(new QTemporaryDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")+"/main_chain_XXXXXX"));
        QVERIFY(directory->isValid()); fixture.create(directory->path()); QVERIFY(!QTest::currentTestFailed());
        QVERIFY(fixture.database("db/userSet/reagent_Information.db",{
            "INSERT INTO reagent_batch_group VALUES(1,'PT','Default')"}));
        if(QString::fromLatin1(QTest::currentTestFunction())==QStringLiteral("formalHoleBatchChoicesFollowOriginalProjects"))
        {
            // 第二项目只使用Cleaner；不能把它的批号混到R1，也不能把条码当批组选项。
            QVERIFY(QFile::copy(fixture.root+"/project/PT.db",fixture.root+"/project/OTHER.db"));
            QVERIFY(fixture.database("project/OTHER.db",{
                "UPDATE project_base SET value='8' WHERE name='xuhao'",
                "UPDATE project_base SET value='10' WHERE name='project_id'",
                "UPDATE project_base SET value='OTHER' WHERE name IN('project_en_name','project_cn_name')",
                "UPDATE project_reagent SET reagent_name='Cleaner'"}));
            QVERIFY(fixture.database("db/userSet/reagent_Information.db",{
                "INSERT INTO reagent_batch_group VALUES(2,'PT','LOT-A|LOT-B'),(3,'PT','LOT-A'),(4,'PT','PTFbg'),(5,'OTHER','UNRELATED')",
                "INSERT INTO reagent_barcode VALUES(1,'R1','BARCODE-ONLY','2032/01/01','B1','1')"}));
        }
        QVERIFY(fixture.database("db/systemInfo/user.db",{
            "CREATE TABLE user_role(id INTEGER PRIMARY KEY,role TEXT)","INSERT INTO user_role VALUES(0,'匿名配置角色')",
            "CREATE TABLE user_powers(id INTEGER PRIMARY KEY,describe TEXT,can_role_id TEXT)"}));
        QVERIFY(fixture.database("db/userSet/user_Information.db",{
            "CREATE TABLE user_information(id INTEGER PRIMARY KEY,name VARCHAR,role INTEGER,pwd VARCHAR,isdefault INTEGER)"}));
        QVERIFY(fixture.database("db/userSet/hospital_Information.db",{
            "CREATE TABLE hospital_information(id INTEGER PRIMARY KEY,column_one VARCHAR,column_two VARCHAR,column_three VARCHAR,type VARCHAR)"}));
        { QFile debug(fixture.root+"/config_Debug.ini"); QVERIFY(debug.open(QIODevice::Append));
          QVERIFY(debug.write("\n[login]\nopen=false\ndefaultPermission=0\n")>0); }
        hospital=new HospitalStore; accounts=new AccountStore; projects=new ProjectsStore;
        reagents=new ReagentStore; samples=new SampleStore;
        for (QObject *object : {static_cast<QObject*>(hospital),static_cast<QObject*>(accounts),static_cast<QObject*>(projects),static_cast<QObject*>(reagents),static_cast<QObject*>(samples)})
        { object->moveToThread(&worker); connect(&worker,&QThread::finished,object,&QObject::deleteLater); }
        shell=new frmMain(hospital,nullptr,accounts); shell->enableDeviceControl(false);
        shell->attachCatalogPages(new ProjectPage(projects),new ReagentsPage(reagents));
        shell->attachSamplePage(new SamplePage(samples)); shell->attachDeviceTaskStore();
        connect(reagents,&ReagentStore::succeeded,this,[this](const QString &operation)
        { if(operation==QStringLiteral("清除孔位配置")) markMeasurement("local_hole_commit_reported"); });
        connect(samples,&SampleStore::devicePacketProcessed,this,
            [this](quint64,quint64,const QByteArray &command,bool success,const QString &)
        {
            if(!success) return;
            if(command=="Ur") markMeasurement("device_reagent_readback_saved");
            if(command=="Tr") markMeasurement("raw_curve_saved");
        });
        host->setWidget(shell); host->show();
        worker.start();
        QSignalSpy accountReady(accounts,&AccountStore::ready);
        for (QObject *object : {static_cast<QObject*>(hospital),static_cast<QObject*>(accounts),static_cast<QObject*>(reagents),static_cast<QObject*>(samples)})
            QVERIFY(QMetaObject::invokeMethod(object,"initializeOriginal",Qt::QueuedConnection,Q_ARG(QString,fixture.root)));
        QVERIFY(QMetaObject::invokeMethod(projects,"initializeOriginal",Qt::QueuedConnection,Q_ARG(QString,fixture.root),Q_ARG(QString,QString())));
        QTRY_VERIFY(!accountReady.isEmpty());
        QMetaObject::invokeMethod(accounts,"applyOriginalConfiguredSession",Qt::QueuedConnection,Q_ARG(quint64,1));
        QTRY_VERIFY(shell->accountSession().authenticated);
        QCOMPARE(shell->accountSession().role,0); QCOMPARE(shell->accountSession().source,AccountSessionSource::Configured);
        QTRY_VERIFY(shell->projectPage()->isReady() && shell->reagentPage()->isReady() && shell->samplePage()->isReady());
        connect(&server,&QTcpServer::newConnection,this,[this]()
        {
            peer=server.nextPendingConnection();
            connect(peer,&QTcpSocket::readyRead,this,[this]()
            {
                const auto batch=decoder.feed(peer->readAll(),clock.elapsed()); QVERIFY(batch.errors.isEmpty());
                for (const auto &payload : batch.messages) receive(payload);
            });
        });
        QVERIFY(server.listen(QHostAddress::LocalHost,0));
        QVERIFY(shell->deviceControl()->open("127.0.0.1",server.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(shell->property("originalConfigurationReady").toBool(),15000);
    }
    void cleanup()
    {
        if (shell) shell->deviceControl()->close();
        if(peer) { peer->abort(); delete peer; peer=nullptr; }
        server.close(); disconnect(&server,nullptr,this,nullptr);
        if(host) delete host->takeWidget(); shell=nullptr;
        for (QObject *object : {static_cast<QObject*>(hospital),static_cast<QObject*>(accounts),static_cast<QObject*>(projects),static_cast<QObject*>(reagents),static_cast<QObject*>(samples)})
            if(object) QMetaObject::invokeMethod(object,"close",Qt::BlockingQueuedConnection);
        worker.quit(); QVERIFY(worker.wait(5000));
        hospital=nullptr;accounts=nullptr;projects=nullptr;reagents=nullptr;samples=nullptr;directory.reset();
    }
    void cleanupTestCase()
    {
        host.reset();
    }
    void formalHolePageSaveWaitsForMatchingDeviceReadback()
    {
        QVERIFY(shell->selectMainPage(0));
        auto *page=shell->reagentPage(); auto *open=page->findChild<QPushButton*>("btnReagentInfoSet");
        QVERIFY(open && open->isEnabled()); QTest::mouseClick(open,Qt::LeftButton);
        QDialog *dialog=nullptr;
        QTRY_VERIFY(([&](){ for(auto *candidate : page->findChildren<QDialog*>()) if(candidate->isVisible()) {dialog=candidate;return true;} return false;})());
        auto *name=dialog->findChild<QComboBox*>("cbxName"); QVERIFY(name); name->setCurrentIndex(0);
        auto *save=dialog->findChild<QPushButton*>("btnSave"); QVERIFY(save);
        // 从测试发出正式保存按钮事件起，到本地提交、模拟器收到Us及Ur实际保存回执。
        beginMeasurement(QStringLiteral("single_hole_clear")); QTest::mouseClick(save,Qt::LeftButton);
        const QByteArray clear="Us[r_1][r_reagent][none][][][0][99][0][][][0][0][0][0][1]";
        QTRY_VERIFY(received.contains(clear));
        QTRY_VERIFY(shell->property("originalReagentUpdateMessage").toString().startsWith(QStringLiteral("已收到")));
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT priority FROM reagent_setting WHERE id=1").toInt(),99);
        QTRY_VERIFY(!page->isBusy());
        QCOMPARE(page->snapshot().holes.first().reagentId,qint64(0));
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        QVERIFY(measurementTimes.contains("local_hole_commit_reported"));
        QVERIFY(measurementTimes.contains("single_hole_command_observed"));
        QVERIFY(measurementTimes.contains("device_reagent_readback_saved"));
        saveEvidence(QStringLiteral("reagent_hole_after_device_readback"),QStringLiteral("1个r_1孔清除；从正式保存按钮事件到原库提交、Us被本进程模拟器接收、对应Ur由后台成功保存。含GUI/worker排队；不含人工输入、物理显示延迟或真实仪器。"));
    }
    void formalStartPauseResumeTasksAndRawResult()
    {
        savedSample(); if(QTest::currentTestFailed()) return;
        QVERIFY(shell->selectMainPage(1));
        // 总时长含测试自动确认的原对话框动画，以及中间暂停/恢复；不是纯算法耗时。
        beginMeasurement(QStringLiteral("one_sample_task")); clickMain("btnStart"); QVERIFY(!QTest::currentTestFailed());
        QTRY_VERIFY(received.contains("Ts"));
        QTRY_COMPARE(shell->deviceControl()->state().status.mainStatus,5);
        reply("Tt[S01][1][1][BAR99][0]");
        const QByteArray tasks="Tt[S01][1][SAM][41][1][cup][BAR99][PT][0][0][1/2][Default][s_1][1][0][]";
        QTRY_VERIFY(received.contains(tasks));
        clickMain("btnPause"); QVERIFY(!QTest::currentTestFailed()); QTRY_VERIFY(received.contains("Tp[0]"));
        QTRY_COMPARE(shell->deviceControl()->state().status.mainStatus,6);
        clickMain("btnStart"); QVERIFY(!QTest::currentTestFailed()); QTRY_COMPARE(received.count("Ts"),2);
        QTRY_COMPARE(shell->deviceControl()->state().status.mainStatus,5);
        reply("TD[S01][1][41]");
        markMeasurement("raw_curve_reply_sent");
        reply("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][1][1][37.1][r_1][1/2][100][1][s_1][][][][405nm-1,2,3,4]");
        QTRY_COMPARE(fixture.value("result/results.db","SELECT state FROM report_data WHERE row_id=21").toString(),QStringLiteral("已收到曲线，待计算"));
        QCOMPARE(fixture.value("result/results.db","SELECT original_value FROM report_results WHERE task_id=41").toString(),QString());
        QCOMPARE(fixture.value("result/results.db","SELECT total_count FROM total_count WHERE id=1").toInt(),42);
        reply("Tt[S01][1][1][BAR99][0]"); QTRY_VERIFY(received.contains("Tt[S01][0]"));
        QVERIFY(shell->property("deviceTaskError").toString().isEmpty());
        QVERIFY(measurementTimes.contains("start_command_observed"));
        QVERIFY(measurementTimes.contains("rack_tasks_observed"));
        QVERIFY(measurementTimes.contains("raw_curve_saved"));
        saveEvidence(QStringLiteral("sample_after_raw_curve_saved"),QStringLiteral("先从正式页面添加S01架、录入17、选择原项目7/PT并更新设置；1架1孔1任务41。计时从正式开始按钮前到原始曲线和原报告状态保存回执，含自动确认、暂停/恢复与本机TCP模拟；没有算法计算、人工输入或真实仪器时延。raw_curve_reply_sent至raw_curve_saved可看本次接收保存间隔。"));
    }
    void formalNormalHoleSaveCancelAndReopen()
    {
        QVERIFY(shell->selectMainPage(0));
        auto *page=shell->reagentPage();
        auto *open=page->findChild<QPushButton*>("btnReagentInfoSet"); QVERIFY(open && open->isEnabled());
        auto visibleDialog=[&]() -> QDialog*
        { for(auto *candidate:page->findChildren<QDialog*>()) if(candidate->isVisible()) return candidate; return nullptr; };
        const QString rowSql="SELECT reagent_name||'|'||reagent_batch||'|'||bottle_type||'|'||amount_uL||'|'||priority||'|'||datetime||'|'||out_date FROM reagent_setting WHERE id=1";
        const QString before=fixture.value("db/userSet/reagent_Setting.db",rowSql).toString();
        QVERIFY(!before.isEmpty());
        QSignalSpy saves(page,&ReagentsPage::requestSaveHole);
        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog());
        auto *dialog=visibleDialog(); auto *batch=dialog->findChild<QComboBox*>("cbxBatch"); QVERIFY(batch);
        auto *expiry=dialog->findChild<QLineEdit*>("ldtOutDate"); QVERIFY(expiry);
        const QString previousExpiry=expiry->text();
        QTest::keyClick(expiry,Qt::Key_A,Qt::ControlModifier); QTest::keyClicks(expiry,"2030-01-02");
        QTRY_VERIFY(page->hasUnsavedChanges());
        auto *cancel=dialog->findChild<QPushButton*>("btnCancel"); QVERIFY(cancel);
        QTest::mouseClick(cancel,Qt::LeftButton); QTRY_VERIFY(!visibleDialog());
        QVERIFY(!page->hasUnsavedChanges()); QCOMPARE(saves.size(),0);
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db",rowSql).toString(),before);
        for(const auto &packet:received) QVERIFY(!packet.startsWith("Us["));

        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog()); dialog=visibleDialog();
        batch=dialog->findChild<QComboBox*>("cbxBatch"); QVERIFY(batch);
        QCOMPARE(batch->currentText(),QStringLiteral("Default"));
        expiry=dialog->findChild<QLineEdit*>("ldtOutDate"); QVERIFY(expiry); QCOMPARE(expiry->text(),previousExpiry);
        QTest::keyClick(expiry,Qt::Key_A,Qt::ControlModifier); QTest::keyClicks(expiry,"2032-02-03");
        auto *save=dialog->findChild<QPushButton*>("btnSave"); QVERIFY(save && save->isEnabled());
        beginMeasurement(QStringLiteral("normal_hole_save")); QTest::mouseClick(save,Qt::LeftButton);
        QTRY_COMPARE(saves.size(),1);
        QTRY_VERIFY(measurementTimes.contains("device_reagent_readback_saved"));
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges() && !visibleDialog());
        const QString stamp=fixture.value("db/userSet/reagent_Setting.db","SELECT datetime FROM reagent_setting WHERE id=1").toString();
        QVERIFY(QDateTime::fromString(stamp,"yyyy/MM/dd HH:mm:ss").isValid());
        const QByteArray expected="Us[r_1][r_reagent][R1][Default][B1][5000][99][1]["+stamp.toUtf8()+"][2032-02-03][500][100][5000][5000][1]";
        QTRY_VERIFY(received.contains(expected));
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT used_ul FROM reagent_setting WHERE id=1").toDouble(),5000.0);
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT COUNT(*) FROM reagent_setting").toInt(),4);
        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog()); dialog=visibleDialog();
        batch=dialog->findChild<QComboBox*>("cbxBatch"); expiry=dialog->findChild<QLineEdit*>("ldtOutDate");
        cancel=dialog->findChild<QPushButton*>("btnCancel"); QVERIFY(batch && expiry && cancel);
        QCOMPARE(batch->currentText(),QStringLiteral("Default"));
        QCOMPARE(expiry->text(),QStringLiteral("2032-02-03"));
        QTest::mouseClick(cancel,Qt::LeftButton); QTRY_VERIFY(!visibleDialog());
        saveEvidence(QStringLiteral("normal_reagent_after_save_cancel_reopen"),QStringLiteral("正式R_1孔页改有效期后取消不提交、不发Us；保留Default批组正常保存新有效期，按原瓶容量5000和优先级99提交，精确Us十五字段和对应Ur保存，关闭重开编辑器读回。不覆盖批组候选/选择，非程序重启、物理输入或真实仪器。"));
    }
    void formalHoleBatchChoicesFollowOriginalProjects()
    {
        QVERIFY(shell->selectMainPage(0)); auto *page=shell->reagentPage();
        auto *open=page->findChild<QPushButton*>("btnReagentInfoSet"); QVERIFY(open && open->isEnabled());
        auto visibleDialog=[&]() -> QDialog*
        { for(auto *candidate:page->findChildren<QDialog*>()) if(candidate->isVisible()) return candidate; return nullptr; };
        auto choices=[](QComboBox *box)
        { QSet<QString> result; for(int i=0;i<box->count();++i) result.insert(box->itemText(i)); return result; };
        const QSet<QString> expected{QStringLiteral("Default"),QStringLiteral("LOT-A"),QStringLiteral("LOT-B")};
        const QString query="SELECT reagent_batch FROM reagent_setting WHERE id=1";
        const QString before=fixture.value("db/userSet/reagent_Setting.db",query).toString();
        QSignalSpy saves(page,&ReagentsPage::requestSaveHole);
        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog()); auto *dialog=visibleDialog();
        auto *batch=dialog->findChild<QComboBox*>("cbxBatch"); QVERIFY(batch);
        QVERIFY(!batch->isEditable()); QVERIFY(!batch->lineEdit());
        QCOMPARE(choices(batch),expected); QCOMPARE(batch->count(),3); QCOMPARE(batch->currentText(),before);
        const auto selectText=[](QComboBox *box,const QString &text)
        {
            const int index=box->findText(text); QVERIFY(index>=0); box->setFocus();
            QTest::keyClick(box,Qt::Key_Home);
            for(int i=0;i<index;++i) QTest::keyClick(box,Qt::Key_Down);
            QCOMPARE(box->currentText(),text);
        };
        selectText(batch,QStringLiteral("LOT-B")); if(QTest::currentTestFailed()) return;
        QTRY_VERIFY(page->hasUnsavedChanges());
        auto *cancel=dialog->findChild<QPushButton*>("btnCancel"); QVERIFY(cancel);
        QTest::mouseClick(cancel,Qt::LeftButton); QTRY_VERIFY(!visibleDialog());
        QCOMPARE(saves.size(),0); QCOMPARE(fixture.value("db/userSet/reagent_Setting.db",query).toString(),before);
        for(const auto &packet:received) QVERIFY(!packet.startsWith("Us["));

        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog()); dialog=visibleDialog();
        batch=dialog->findChild<QComboBox*>("cbxBatch"); QVERIFY(batch); QCOMPARE(batch->currentText(),before);
        selectText(batch,QStringLiteral("LOT-B")); if(QTest::currentTestFailed()) return;
        auto *save=dialog->findChild<QPushButton*>("btnSave"); QVERIFY(save && save->isEnabled());
        beginMeasurement(QStringLiteral("original_batch_choice")); QTest::mouseClick(save,Qt::LeftButton);
        QTRY_COMPARE(saves.size(),1); QTRY_VERIFY(measurementTimes.contains("device_reagent_readback_saved"));
        QTRY_VERIFY(!page->isBusy() && !page->hasUnsavedChanges() && !visibleDialog());
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db",query).toString(),QStringLiteral("LOT-B"));
        const QByteArray stamp=fixture.value("db/userSet/reagent_Setting.db","SELECT datetime FROM reagent_setting WHERE id=1").toString().toUtf8();
        const QByteArray expectedPacket="Us[r_1][r_reagent][R1][LOT-B][B1][5000][99][1]["+stamp+"][2031/01/01][500][100][5000][5000][1]";
        QTRY_VERIFY(received.contains(expectedPacket));
        QTest::mouseClick(open,Qt::LeftButton); QTRY_VERIFY(visibleDialog()); dialog=visibleDialog();
        batch=dialog->findChild<QComboBox*>("cbxBatch"); auto *name=dialog->findChild<QComboBox*>("cbxName");
        auto *bottle=dialog->findChild<QComboBox*>("cbxBottleName"); QVERIFY(batch && name && bottle);
        QCOMPARE(batch->currentText(),QStringLiteral("LOT-B"));
        selectText(name,QStringLiteral("Cleaner")); if(QTest::currentTestFailed()) return;
        const QSet<QString> cleanerBatch{QStringLiteral("UNRELATED")}; QCOMPARE(choices(batch),cleanerBatch);
        selectText(name,QStringLiteral("none")); if(QTest::currentTestFailed()) return;
        QCOMPARE(batch->count(),0); QCOMPARE(bottle->count(),0);
        cancel=dialog->findChild<QPushButton*>("btnCancel"); QVERIFY(cancel);
        QTest::mouseClick(cancel,Qt::LeftButton); QTRY_VERIFY(!visibleDialog()); QCOMPARE(saves.size(),1);
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db",query).toString(),QStringLiteral("LOT-B"));
        QCOMPARE(fixture.value("db/userSet/reagent_Setting.db","SELECT unknown_vendor_column FROM reagent_setting WHERE id=1").toString(),QStringLiteral("KEEP"));
        saveEvidence(QStringLiteral("original_batch_choice_saved"),QStringLiteral("正式孔页按实际项目试剂关联给出只读批组，拆|去重并排PTFbg/其他试剂项目/条码；键盘选择LOT-B后取消不写/不发，再保存精确Us并等Ur落盘；重开和切换Cleaner/none刷新候选后取消。软件事件和TCP模拟，未接仪器。"));
    }
    void actualMainSelectsOriginalSampleAndReagentStoresAndRestarts()
    {
        const QString binary=qEnvironmentVariable("SN100_FORMAL_MAIN_BINARY");
        QVERIFY(QFileInfo(binary).isExecutable());
        // 另建原结构匿名根，避免父进程页面的DB连接影响子程序启动和关闭。
        QTemporaryDir childDirectory(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")+"/entry_chain_XXXXXX");
        QVERIFY(childDirectory.isValid());
        OriginalTaskFlowFixture child; child.create(childDirectory.path()); QVERIFY(!QTest::currentTestFailed());
        QVERIFY(child.database("db/systemInfo/user.db",{
            "CREATE TABLE user_role(id INTEGER PRIMARY KEY,role TEXT)","INSERT INTO user_role VALUES(0,'匿名配置角色')",
            "CREATE TABLE user_powers(id INTEGER PRIMARY KEY,describe TEXT,can_role_id TEXT)"}));
        QVERIFY(child.database("db/userSet/user_Information.db",{
            "CREATE TABLE user_information(id INTEGER PRIMARY KEY,name VARCHAR,role INTEGER,pwd VARCHAR,isdefault INTEGER)"}));
        QVERIFY(child.database("db/userSet/hospital_Information.db",{
            "CREATE TABLE hospital_information(id INTEGER PRIMARY KEY,column_one VARCHAR,column_two VARCHAR,column_three VARCHAR,type VARCHAR)"}));
        { QFile debug(child.root+"/config_Debug.ini"); QVERIFY(debug.open(QIODevice::Append));
          QVERIFY(debug.write("\n[login]\nopen=false\ndefaultPermission=0\n")>0); }
        for (int restart=0;restart<2;++restart)
        {
            QProcess process; process.setProcessChannelMode(QProcess::MergedChannels); process.setWorkingDirectory(child.root);
            process.start(binary,{"--data-dir",child.root,"--original-db-root",child.root,"--accounts","--catalog","--samples","--viewport","1600x900"});
            QVERIFY(process.waitForStarted(5000));
            QByteArray output; QElapsedTimer deadline; deadline.start();
            while(deadline.elapsed()<20000 && !output.contains("SHELL_READY") && process.state()!=QProcess::NotRunning)
            { QTest::qWait(50); output+=process.readAll(); }
            const bool ready=output.contains("SHELL_READY");
            process.terminate(); const bool closed=process.waitForFinished(10000); output+=process.readAll();
            if(!closed) { process.kill(); process.waitForFinished(); }
            qInfo().noquote()<<"ACTUAL_MAIN_ORIGINAL_SAMPLE_RESTART"<<restart<<output;
            QVERIFY2(ready,output.constData()); QVERIFY(closed);
            QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),0);
            QVERIFY(output.contains("SAMPLE_STORE_CLOSED")); QVERIFY(output.contains("THREADS_STOPPED true"));
        }
    }
};
QTEST_MAIN(OriginalMainTaskChainRkTest)
#include "original_main_task_chain_rk_test.moc"
