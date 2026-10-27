#include <QtTest>
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <functional>
#include "projects/projects_store.h"
#include "quality_control/quality_control_page.h"
#include "ui/cusdialogmask.h"

namespace
{
bool until(const std::function<bool()> &condition,int timeoutMs=10000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed()<timeoutMs) QTest::qWait(1);
    return condition();
}
template<typename T> T *widget(QObject *parent,const char *name)
{
    return parent?parent->findChild<T*>(QString::fromLatin1(name)):nullptr;
}
bool readyDialog(QObject *dialog)
{
    auto *mask=widget<CusDialogMask>(dialog,"mask");
    return mask && until([mask](){return mask->isReadyForInput();});
}
bool click(QObject *dialog,const char *name)
{
    auto *button=widget<QPushButton>(dialog,name);
    if (!button || !readyDialog(dialog) || !button->isVisible() || !button->isEnabled()) return false;
    QTest::mouseClick(button,Qt::LeftButton);
    return true;
}
void type(QLineEdit *edit,const QString &text)
{
    QTest::mouseClick(edit,Qt::LeftButton);
    QTest::keyClick(edit,Qt::Key_A,Qt::ControlModifier);
    QTest::keyClicks(edit,text);
}
double percentile(QList<double> values,double fraction)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(),values.end());
    return values[qBound(0,int(std::ceil(fraction*values.size()))-1,values.size()-1)];
}
QJsonObject summary(const QList<double> &values)
{
    QJsonObject result;
    result.insert("count",values.size());
    result.insert("p95_ms",percentile(values,.95));
    result.insert("p99_ms",percentile(values,.99));
    result.insert("max_ms",percentile(values,1));
    return result;
}
QString executableSha()
{
    QFile executable(QCoreApplication::applicationFilePath());
    if (!executable.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!executable.atEnd()) hash.addData(executable.read(64*1024));
    return QString::fromLatin1(hash.result().toHex());
}
class Fixture
{
public:
    QTemporaryDir runtime;
    QThread projectThread,qcThread;
    ProjectsStore *projects=nullptr;
    QcStore *store=nullptr;
    QualityControlPage *page=nullptr;
    QScrollArea host;
    QString failure;
    bool stopped=false;
    bool normalLeave=false;
    bool projectClosed=false,qcClosed=false;

    Fixture() : runtime(QDir(QCoreApplication::applicationDirPath()).filePath("qc-response-runtime-XXXXXX"))
    {
        if (!runtime.isValid()) {failure="Cannot create isolated anonymous runtime beside test executable";return;}
        ProjectsStore::registerTypes();
        QcStore::registerTypes();
        projects=new ProjectsStore;
        store=new QcStore;
        projects->moveToThread(&projectThread);
        store->moveToThread(&qcThread);
        QObject::connect(&projectThread,&QThread::finished,projects,&QObject::deleteLater);
        QObject::connect(&qcThread,&QThread::finished,store,&QObject::deleteLater);
        page=new QualityControlPage(store);
        host.setWidget(page);
        host.setWidgetResizable(false);
        host.resize(1600,900);
        host.show();
        QObject::connect(projects,&ProjectsStore::error,&host,[this](qint64,QString operation,QString message){failure=operation+": "+message;});
        QObject::connect(store,&QcStore::error,&host,[this](qint64,QString operation,QString message){failure=operation+": "+message;});
        QObject::connect(projects,&ProjectsStore::closed,&host,[this](){projectClosed=true;});
        QObject::connect(store,&QcStore::closed,&host,[this](){qcClosed=true;});
        const QString catalog=QDir(runtime.path()).filePath("catalog");
        const QString qc=QDir(runtime.path()).filePath("quality_control");
        QObject::connect(&projectThread,&QThread::started,projects,[this,catalog](){projects->initialize(catalog,true);});
        QObject::connect(projects,&ProjectsStore::ready,store,[this,qc](QString){store->initialize(qc);},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::ready,store,[this,catalog](QString){store->bindCatalog(catalog);});
        projectThread.start();
        qcThread.start();
        if (!until([this](){return !failure.isEmpty() || (page->isReady()&&!page->isBusy());})) failure="QC page initialization timeout";
    }
    bool stop()
    {
        if (stopped) return !projectThread.isRunning()&&!qcThread.isRunning();
        stopped=true;
        if (page) normalLeave=page->requestLeave(QStringLiteral("测量后正常退出"));
        host.close();
        // 正常路径先经页面草稿/busy守卫；失败路径销毁页面停止接收过期回复。
        delete host.takeWidget();
        page=nullptr;
        if (qcThread.isRunning()) QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection);
        if (projectThread.isRunning()) QMetaObject::invokeMethod(projects,"close",Qt::BlockingQueuedConnection);
        until([this](){return qcClosed&&projectClosed;},2000);
        qcThread.quit();
        projectThread.quit();
        const bool qcStopped=qcThread.wait(5000);
        const bool projectsStopped=projectThread.wait(5000);
        return qcStopped&&projectsStopped;
    }
    ~Fixture() {stop();}
};
}

class QcLocalResponseTest : public QObject
{
    Q_OBJECT
private slots:
    void measureFortyOriginalSaveActions();
};

void QcLocalResponseTest::measureFortyOriginalSaveActions()
{
    const QString evidence=qEnvironmentVariable("SN100_QC_EVIDENCE");
    QVERIFY2(QDir::isAbsolutePath(evidence),"Set SN100_QC_EVIDENCE to an absolute evidence output directory");
    QVERIFY(QDir().mkpath(evidence));
    Fixture fixture;
    QVERIFY2(fixture.failure.isEmpty(),qPrintable(fixture.failure));
    QVERIFY(fixture.page && fixture.page->isReady()&&!fixture.page->isBusy());
    fixture.page->openManager();
    auto *manager=widget<QDialog>(fixture.page,"QcControlManager");
    QVERIFY(manager);
    QVERIFY(click(manager,"btn_add_control_item"));
    QPointer<QDialog> add=widget<QDialog>(fixture.page,"QcControlAdd");
    QVERIFY(add);
    QVERIFY(readyDialog(add));
    type(widget<QLineEdit>(add,"ldt_control_name"),QStringLiteral("QC_RESPONSE"));
    type(widget<QLineEdit>(add,"ldt_factor"),QStringLiteral("FACTOR_BASE"));
    QVERIFY(click(add,"btnSave"));
    QVERIFY(until([&](){return fixture.page->controls().size()==1&&!fixture.page->isBusy()&&add.isNull();}));
    const qint64 controlId=fixture.page->controls().first().id;
    QVERIFY(controlId>0);
    QVERIFY(readyDialog(manager));
    auto *name=widget<QLineEdit>(manager,"ldtControlName");
    auto *manufacturer=widget<QLineEdit>(manager,"ldtFactor");
    auto *save=widget<QPushButton>(manager,"btn_save");
    QVERIFY(name&&manufacturer&&save&&save->isVisible()&&save->isEnabled());

    QElapsedTimer monotonic;
    monotonic.start();
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(5);
    qint64 lastHeartbeatNs=monotonic.nsecsElapsed();
    QJsonArray heartbeats,samples;
    QList<double> busyMs,finishedMs,visibleMs,heartbeatDelayMs,heartbeatIntervalMs;
    QObject::connect(&heartbeat,&QTimer::timeout,this,[&]()
    {
        const qint64 now=monotonic.nsecsElapsed();
        const qint64 planned=lastHeartbeatNs+5000000;
        const qint64 late=qMax<qint64>(0,now-planned);
        heartbeats.append(QJsonObject{{"planned_ns",double(planned)},{"observed_ns",double(now)},
            {"delay_ns",double(late)},{"interval_ns",double(now-lastHeartbeatNs)}});
        heartbeatDelayMs.append(late/1000000.0);
        heartbeatIntervalMs.append((now-lastHeartbeatNs)/1000000.0);
        lastHeartbeatNs=now;
    });
    heartbeat.start();
    bool active=false,finished=false,success=false;
    qint64 busyAt=-1,finishedAt=-1;
    QObject::connect(fixture.page,&QualityControlPage::busyChanged,this,[&](bool busy)
    {if(active&&busy&&busyAt<0)busyAt=monotonic.nsecsElapsed();});
    QObject::connect(fixture.page,&QualityControlPage::operationFinished,this,[&](QString operation,bool ok)
    {
        if(active&&operation==QStringLiteral("saveControl"))
        {finishedAt=monotonic.nsecsElapsed();success=ok;finished=true;}
    });

    for (int index=0;index<40;++index)
    {
        const QString expectedName=QStringLiteral("QC_RESPONSE_%1").arg(index,2,10,QLatin1Char('0'));
        const QString expectedManufacturer=QStringLiteral("FACTOR_%1").arg(index,2,10,QLatin1Char('0'));
        type(name,expectedName);
        type(manufacturer,expectedManufacturer);
        QVERIFY(fixture.page->isDirty());
        busyAt=-1;finishedAt=-1;finished=false;success=false;active=true;
        qint64 mouseAt=-1;
        const qint64 queuedAt=monotonic.nsecsElapsed();
        QTimer::singleShot(0,save,[&]()
        {
            mouseAt=monotonic.nsecsElapsed();
            QTest::mouseClick(save,Qt::LeftButton);
        });
        QVERIFY2(until([&](){return finished;}),"Original Save action did not finish within 10 seconds");
        active=false;
        QVERIFY2(success,qPrintable(fixture.page->statusText()));
        QVERIFY(busyAt>=queuedAt&&mouseAt>=queuedAt&&finishedAt>=busyAt);
        QVERIFY(until([&]()
        {
            const QcControls controls=fixture.page->controls();
            return !fixture.page->isBusy()&&controls.size()==1&&controls.first().id==controlId&&
                controls.first().name==expectedName&&controls.first().manufacturer==expectedManufacturer&&
                name->text()==expectedName&&manufacturer->text()==expectedManufacturer;
        }));
        const qint64 visibleAt=monotonic.nsecsElapsed();
        const double feedback=(busyAt-queuedAt)/1000000.0;
        const double completion=(finishedAt-queuedAt)/1000000.0;
        const double dataVisible=(visibleAt-queuedAt)/1000000.0;
        busyMs.append(feedback);finishedMs.append(completion);visibleMs.append(dataVisible);
        samples.append(QJsonObject{{"sample",index+1},{"control_id",double(controlId)},
            {"input_name",expectedName},{"input_manufacturer",expectedManufacturer},
            {"page_name",fixture.page->controls().first().name},
            {"page_manufacturer",fixture.page->controls().first().manufacturer},
            {"queued_ns",double(queuedAt)},{"mouse_dispatched_ns",double(mouseAt)},
            {"first_busy_ns",double(busyAt)},{"operation_finished_ns",double(finishedAt)},
            {"committed_page_observed_ns",double(visibleAt)},
            {"queued_to_busy_ms",feedback},{"queued_to_finished_ms",completion},
            {"queued_to_committed_page_ms",dataVisible},{"success",success}});
        QVERIFY(!fixture.page->isDirty());
    }
    heartbeat.stop();
    const qint64 durationNs=monotonic.nsecsElapsed();
    QVERIFY(!heartbeats.isEmpty());
    QVERIFY(fixture.stop());
    QVERIFY(fixture.normalLeave&&fixture.qcClosed&&fixture.projectClosed);
    QJsonObject environment{{"os",QSysInfo::prettyProductName()},
        {"architecture",QSysInfo::currentCpuArchitecture()},{"qt",QString::fromLatin1(qVersion())},
        {"qpa",QGuiApplication::platformName()},{"font",QApplication::font().family()},
        {"host_width",1600},{"host_height",900},{"page_width",1751},{"page_height",968},
        {"display",qEnvironmentVariable("DISPLAY")},{"wayland_display",qEnvironmentVariable("WAYLAND_DISPLAY")},
        {"anonymous_control_count",1},{"operation_count",40},{"heartbeat_interval_ms",5},
        {"build","Release"},{"worker_delay_injected",false}};
    if (QScreen *screen=QGuiApplication::primaryScreen())
    {
        environment.insert("screen_width",screen->geometry().width());
        environment.insert("screen_height",screen->geometry().height());
        environment.insert("logical_dpi",screen->logicalDotsPerInch());
        environment.insert("device_pixel_ratio",screen->devicePixelRatio());
    }
    QJsonObject metrics{{"schema","sn100-qc-local-response-v1"},{"environment",environment},
        {"method","Queued Qt mouse click on original btn_save; first busy signal, operationFinished and committed page snapshot observed. Heartbeat records nonnegative interval lateness above 5 ms, with previous actual dispatch as next baseline."},
        {"duration_ns",double(durationNs)},{"samples",samples},{"heartbeats",heartbeats},
        {"queued_to_busy",summary(busyMs)},{"queued_to_finished",summary(finishedMs)},
        {"queued_to_committed_page",summary(visibleMs)},
        {"heartbeat_delay",summary(heartbeatDelayMs)},{"heartbeat_interval",summary(heartbeatIntervalMs)},
        {"executable_sha256",executableSha()},{"normal_page_leave",fixture.normalLeave},
        {"qc_worker_closed",fixture.qcClosed},{"catalog_worker_closed",fixture.projectClosed},
        {"test_function_passed",true},{"process_exit_code",QJsonValue(QJsonValue::Null)},
        {"acceptance","Environment-specific short baseline only; user budget, original Windows behavior, RK, instrument, clinical rules and long stability remain unverified. Actual process exit is the external runner receipt."}};
    QSaveFile output(QDir(evidence).filePath(QStringLiteral("metrics.json")));
    QVERIFY(output.open(QIODevice::WriteOnly));
    const QByteArray bytes=QJsonDocument(metrics).toJson(QJsonDocument::Indented);
    QCOMPARE(output.write(bytes),qint64(bytes.size()));
    QVERIFY(output.commit());
    qInfo().noquote()<<"QC_RESPONSE samples=40 heartbeat="<<heartbeats.size()<<"normal_leave="<<fixture.normalLeave
        <<"qc_closed="<<fixture.qcClosed<<"catalog_closed="<<fixture.projectClosed;
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    app.setStyle(QStringLiteral("Fusion"));
#ifdef Q_OS_LINUX
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"),11));
#endif
    QcLocalResponseTest test;
    return QTest::qExec(&test,argc,argv);
}
#include "qc_local_response_test.moc"
