#include "quality_control_page.h"
#include "projects/projects_store.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QScrollArea>
#include <QThread>
#include <QTimer>
#include <cmath>

class QcHost : public QScrollArea
{
public:
    explicit QcHost(QualityControlPage *page) : page_(page) {setWidget(page);}
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!page_->requestLeave(QStringLiteral("退出程序"))) event->ignore();
        else QScrollArea::closeEvent(event);
    }
private:
    QualityControlPage *page_;
};
int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    app.setStyle(QStringLiteral("Fusion"));
#ifdef Q_OS_LINUX
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"),11));
#endif
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("data-dir"),QStringLiteral("显式匿名隔离根目录绝对路径"),QStringLiteral("directory")});
    parser.addOption({QStringLiteral("catalog-dir"),QStringLiteral("可选的已有匿名catalog目录，默认data-dir/catalog"),QStringLiteral("directory")});
    parser.addOption({QStringLiteral("seed-demo"),QStringLiteral("仅新匿名catalog创建显式DEMO项目，不含生产参数")});
    parser.addOption({QStringLiteral("exit-after-ms"),QStringLiteral("正常退出的时间，0为手动退出"),QStringLiteral("milliseconds"),QStringLiteral("0")});
    parser.process(app);
    const QString directory=parser.value(QStringLiteral("data-dir"));
    bool valid=false;
    const int duration=parser.value(QStringLiteral("exit-after-ms")).toInt(&valid);
    if (!QDir::isAbsolutePath(directory) || !valid || duration<0)
    {qCritical()<<"absolute --data-dir and nonnegative --exit-after-ms required";return 2;}
    const QString catalog=parser.isSet(QStringLiteral("catalog-dir"))?parser.value(QStringLiteral("catalog-dir")):QDir(directory).filePath(QStringLiteral("catalog"));
    if (!QDir::isAbsolutePath(catalog)) {qCritical()<<"absolute --catalog-dir required";return 2;}
    QcStore::registerTypes();
    ProjectsStore::registerTypes();
    QThread thread;
    auto *store=new QcStore;
    auto *projects=new ProjectsStore;
    store->moveToThread(&thread);
    projects->moveToThread(&thread);
    QObject::connect(&thread,&QThread::finished,store,&QObject::deleteLater);
    QObject::connect(&thread,&QThread::finished,projects,&QObject::deleteLater);
    auto *page=new QualityControlPage(store);
    QcHost host(page);
    host.setWidgetResizable(false);
    host.resize(1600,900);
    host.setWindowTitle(QStringLiteral("SN100质控品/项目/批次 · 本地脱机"));
    QObject::connect(page,&QualityControlPage::statusChanged,&host,[&host](QString message,bool error)
    {host.setWindowTitle(message);if(error) qWarning().noquote()<<message;else qInfo().noquote()<<message;});
    bool failed=false;
    QObject::connect(projects,&ProjectsStore::error,&app,[&](qint64 request,QString operation,QString message)
    {qCritical().noquote()<<operation<<message;if(!request){failed=true;app.exit(3);}});
    QObject::connect(store,&QcStore::error,&app,[&](qint64 request,QString operation,QString message)
    {qWarning().noquote()<<operation<<message;if(!request){failed=true;app.exit(3);}});
    QObject::connect(projects,&ProjectsStore::ready,store,[store,directory](QString){store->initialize(QDir(directory).filePath(QStringLiteral("quality_control")));});
    QObject::connect(store,&QcStore::ready,store,[store,catalog](QString){store->bindCatalog(catalog);});
    QObject::connect(store,&QcStore::catalogBound,projects,[projects](QString){projects->load(101);});
    QObject::connect(projects,&ProjectsStore::snapshotReady,page,[page](qint64 request,ProjectRecords records,ProjectUnits units)
    {
        if (request!=101) return;
        QcProjectRefs refs;
        for (const ProjectRecord &record:records)
        {
            QcProjectRef ref;ref.id=record.id;ref.name=record.name;
            for (const ProjectResult &result:record.results)
            {
                const int type=result.type==QStringLiteral("Original")?1:result.type==QStringLiteral("Calibration")?2:result.type==QStringLiteral("Rate")?3:result.type==QStringLiteral("INR")?4:0;
                if (!type) continue;
                QcResultRef value;value.type=type;value.name=result.name;value.unit=result.unit;value.decimals=result.decimals;
                bool unitFound=false;
                for (const ProjectUnit &unit:units) if(unit.name==result.unit && std::isfinite(unit.ratio) && unit.ratio>0)
                {value.ratio=unit.ratio;unitFound=true;}
                if(!unitFound)continue;
                ref.results.append(value);
            }
            if (!ref.results.isEmpty()) refs.append(ref);
        }
        page->setProjects(refs);
    });
    QObject::connect(&thread,&QThread::started,projects,[projects,catalog,&parser](){projects->initialize(catalog,parser.isSet(QStringLiteral("seed-demo")));});
    thread.start();
    host.show();
    if(duration>0)QTimer::singleShot(duration,&host,&QWidget::close);
    const int result=app.exec();
    QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(projects,"close",Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
    return failed?3:result;
}
