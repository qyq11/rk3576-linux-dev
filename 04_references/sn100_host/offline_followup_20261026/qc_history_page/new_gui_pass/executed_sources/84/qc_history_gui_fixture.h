#ifndef SN100_QC_HISTORY_GUI_FIXTURE_H
#define SN100_QC_HISTORY_GUI_FIXTURE_H
// 本夹具只创建显式新匿名目录；项目由真实owner生成，历史行是测试输入，
// 不构成产品手工录点/导入入口。所有跨worker采集均Queued到主测试线程。
#include "projects/projects_store.h"
#include "quality_control/quality_control_page.h"
#include <QDir>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtTest>
#include <functional>

namespace QcHistoryGui
{
inline bool waitFor(const std::function<bool()> &condition)
{
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<10000)QTest::qWait(5);
    return condition();
}

class Database
{
public:
    explicit Database(const QString &path)
    {
        name=QStringLiteral("QcHistoryGui-%1").arg(QUuid::createUuid().toString());
        db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=100");db.open();
    }
    ~Database(){db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(name);}
    bool exec(const QString &sql,const QVariantList &values={})
    {
        QSqlQuery query(db);if(!query.prepare(sql))return false;
        for(const auto &value:values)query.addBindValue(value);
        const bool ok=query.exec();if(!ok)qWarning()<<query.lastError();return ok;
    }
    QVariant scalar(const QString &sql)
    {QSqlQuery query(db);return query.exec(sql)&&query.next()?query.value(0):QVariant();}
    QSqlDatabase db;QString name;
};

class Session
{
public:
    explicit Session(const QString &root):directory(QDir(root).filePath("session-XXXXXX")),
        projects(new ProjectsStore),store(new QcStore)
    {
        directory.setAutoRemove(false);ProjectsStore::registerTypes();QcStore::registerTypes();
        projects->moveToThread(&worker);store->moveToThread(&worker);
        QObject::connect(&worker,&QThread::finished,projects,&QObject::deleteLater);
        QObject::connect(&worker,&QThread::finished,store,&QObject::deleteLater);
        QObject::connect(projects,&ProjectsStore::ready,&context,[this](QString){projectReady=true;},Qt::QueuedConnection);
        QObject::connect(projects,&ProjectsStore::snapshotReady,&context,[this](quint64 request,ProjectRecords records,ProjectUnits units)
        {projectSnapshots[request]=records;projectUnits=units;},Qt::QueuedConnection);
        QObject::connect(projects,&ProjectsStore::succeeded,&context,[this](quint64 request,QString,qint64 id)
        {projectSaved[request]=id;},Qt::QueuedConnection);
        QObject::connect(projects,&ProjectsStore::error,&context,[this](quint64 request,QString operation,QString message)
        {projectErrors[request]=operation+":"+message;},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::ready,&context,[this](QString path){qcPath=path;qcReady=true;},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::catalogBound,&context,[this](QString){bound=true;},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::succeeded,&context,[this](qint64 request,QString,qint64 id)
        {saved[request]=id;},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::error,&context,[this](qint64 request,QString operation,QString message)
        {errors[request]=operation+":"+message;},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::snapshotReady,&context,[this](qint64 request,QcControls controls)
        {snapshots[request]=controls;},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::historyReady,&context,[this](qint64 request,QcHistoryReply reply)
        {history.append(qMakePair(request,reply));},Qt::QueuedConnection);
        QObject::connect(store,&QcStore::closed,&context,[this](){qcClosed=true;},Qt::QueuedConnection);
        QObject::connect(projects,&ProjectsStore::closed,&context,[this](){projectsClosed=true;},Qt::QueuedConnection);
        worker.start();
    }
    ~Session(){if(!stopped&&!shutdown())qFatal("QC history GUI fixture threads failed to stop");}
    bool write(const std::function<void(qint64)> &action)
    {
        const qint64 request=++sequence;
        if(!QMetaObject::invokeMethod(store,[=](){action(request);},Qt::QueuedConnection))return false;
        if(!waitFor([&](){return saved.contains(request)||errors.contains(request);})||errors.contains(request))return false;
        lastId=saved.value(request);return true;
    }
    bool initialize(bool invalidDatesForB=false)
    {
        if(!directory.isValid())return false;
        catalog=directory.filePath("catalog");qcDirectory=directory.filePath("qc");
        QMetaObject::invokeMethod(projects,[this](){projects->initialize(catalog,true);projects->load(101);},Qt::QueuedConnection);
        if(!waitFor([&](){return projectReady&&projectSnapshots.contains(101);}))return false;
        const auto initial=projectSnapshots.value(101);if(initial.isEmpty())return false;
        const qint64 sourceId=initial.first().id;
        AccountSession actor;actor.name="anonymous-history-fixture";actor.role=4;
        actor.authenticated=true;actor.actionReady=true;actor.source=AccountSessionSource::Simulated;
        QMetaObject::invokeMethod(projects,[this,actor,sourceId](){projects->setSession(actor);projects->copy(102,sourceId,"HIST_PROJECT_B");},Qt::QueuedConnection);
        if(!waitFor([&](){return projectSaved.contains(102)||projectErrors.contains(102);})||projectErrors.contains(102))return false;
        QMetaObject::invokeMethod(projects,[this](){projects->load(103);},Qt::QueuedConnection);
        if(!waitFor([&](){return projectSnapshots.contains(103);}))return false;
        for(const auto &record:projectSnapshots.value(103))
        {
            QcProjectRef ref;ref.id=record.id;ref.name=record.name;
            for(const auto &result:record.results)if(result.type=="Calibration")
            {
                QcResultRef value;value.type=2;value.name=result.name;value.unit=result.unit;value.decimals=result.decimals;
                bool found=false;for(const auto &unit:projectUnits)if(unit.name==value.unit){value.ratio=unit.ratio;found=true;}
                if(found)ref.results.append(value);
            }
            if(!ref.results.isEmpty())refs.append(ref);
        }
        if(refs.size()!=2)return false;
        QMetaObject::invokeMethod(store,[this](){store->initialize(qcDirectory);store->bindCatalog(catalog);store->setCatalog(100000001,refs);},Qt::QueuedConnection);
        if(!waitFor([&](){return qcReady&&bound;}))return false;
        QcControl control;control.name="QC_HISTORY_GUI";control.manufacturer="匿名厂家";
        if(!write([this,control](qint64 request){store->saveControl(request,control);}))return false;
        controlId=lastId;
        if(!write([this](qint64 request){store->setProjects(request,controlId,refs);}))return false;
        const qint64 loaded=++sequence;
        QMetaObject::invokeMethod(store,[this,loaded](){store->load(loaded);},Qt::QueuedConnection);
        if(!waitFor([&](){return snapshots.contains(loaded);}))return false;
        const auto controls=snapshots.value(loaded);if(controls.size()!=1)return false;
        for(const auto &association:controls.first().projects)
            if(association.projectName=="DEMO_A")associationA=association.id;else if(association.projectName=="HIST_PROJECT_B")associationB=association.id;
        if(!associationA||!associationB)return false;
        QcBatch batch;batch.controlId=controlId;batch.name="GUI_PREV";batch.expirationDate="2027/01/31";
        if(!write([this,batch](qint64 request){store->saveBatch(request,batch);}))return false;
        batch.name="GUI_CURRENT";
        if(!write([this,batch](qint64 request){store->saveBatch(request,batch);}))return false;
        const qint64 current=lastId;
        if(!write([this,current](qint64 request){store->switchBatch(request,controlId,current);}))return false;
        Database database(qcPath);
        for(int project=0;project<2;++project)
            for(int index=0;index<3;++index)
            {
                const QString name=project?QString("HIST_PROJECT_B"):QString("DEMO_A");
                const int value=project?100+20*index:10+2*index;
                const QString encoded="["+name+"|0|=Calibration-"+QString::number(value)+":1/1-101]";
                if(!database.exec("INSERT INTO quality_data(control_name,project_name,control_batch,result_val,date,time) VALUES(?,?,?,?,?,?)",
                    {control.name,name,index?QString("GUI_CURRENT"):QString("GUI_PREV"),encoded,
                     project&&invalidDatesForB?QString("date-unknown"):QString("2026/10/%1").arg(23+index),"10:00:00"}))return false;
            }
        beforeHistory=database.scalar(historyImageQuery()).toString();
        // 真正关闭/重开owner，使新页面接到真实ready/snapshot，而不是伪造成功信号。
        qcClosed=false;QMetaObject::invokeMethod(store,"close",Qt::QueuedConnection);
        if(!waitFor([&](){return qcClosed;}))return false;
        qcClosed=false;qcReady=false;bound=false;
        page=new QualityControlPage(store);host.setWidget(page);host.setWidgetResizable(false);host.resize(1920,1080);host.show();
        QMetaObject::invokeMethod(store,[this](){store->initialize(qcDirectory);store->bindCatalog(catalog);},Qt::QueuedConnection);
        page->setProjects(refs);
        return waitFor([&](){return qcReady&&bound&&page->isReady()&&!page->isBusy()&&!history.isEmpty();});
    }
    bool unchangedHistory()
    {Database database(qcPath);return database.scalar(historyImageQuery()).toString()==beforeHistory;}
    QString historyImageQuery()const
    {return "SELECT group_concat(image,';') FROM (SELECT id||'|'||control_name||'|'||project_name||'|'||control_batch||'|'||date||'|'||time||'|'||result_val AS image FROM quality_data ORDER BY id)";}
    bool shutdown()
    {
        if(stopped)return true;
        if(page){host.takeWidget();delete page;page=nullptr;}
        qcClosed=false;projectsClosed=false;
        QMetaObject::invokeMethod(store,"close",Qt::QueuedConnection);
        QMetaObject::invokeMethod(projects,"close",Qt::QueuedConnection);
        const bool closed=waitFor([&](){return qcClosed&&projectsClosed;});
        worker.quit();const bool joined=worker.wait(5000);stopped=joined;
        qInfo()<<"QC_HISTORY_GUI_STORES_CLOSED"<<closed<<"THREAD_STOPPED"<<joined;
        return closed&&joined;
    }
    QObject context;
    QTemporaryDir directory;
    QThread worker;
    ProjectsStore *projects;QcStore *store;QualityControlPage *page=nullptr;QScrollArea host;
    QString catalog,qcDirectory,qcPath,beforeHistory;
    QMap<quint64,ProjectRecords> projectSnapshots;ProjectUnits projectUnits;
    QMap<quint64,qint64> projectSaved;QMap<quint64,QString> projectErrors;
    QMap<qint64,qint64> saved;QMap<qint64,QString> errors;QMap<qint64,QcControls> snapshots;
    QList<QPair<qint64,QcHistoryReply>> history;QcProjectRefs refs;
    qint64 sequence=100000100,lastId=0,controlId=0,associationA=0,associationB=0;
    bool projectReady=false,qcReady=false,bound=false,qcClosed=false,projectsClosed=false,stopped=false;
};
}
#endif
