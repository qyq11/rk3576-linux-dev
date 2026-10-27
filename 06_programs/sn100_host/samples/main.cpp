#include "sample_page.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDebug>
#include <QScrollArea>
#include <QThread>
class SamplesHost : public QScrollArea
{
  public:
    explicit SamplesHost(SamplePage *page) : page_(page)
    {
        setWidget(page);
        setWidgetResizable(false);
    }

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (page_->prepareToLeave())
        {
            QScrollArea::closeEvent(event);
        }
        else
        {
            event->ignore();
        }
    }

  private:
    SamplePage *page_;
};
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setFont(QFont("Noto Sans CJK SC", 11));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"catalog-dir", "显式隔离目录绝对路径", "directory"});
    parser.addOption({"seed-demo", "仅新库显式匿名项目/比例标签；不含生产参数"});
    parser.addOption({"test-engineer", "显式匿名工程师模拟身份"});
    parser.process(app);
    if (!parser.isSet("catalog-dir"))
    {
        qCritical() << "--catalog-dir required";
        return 2;
    }
    ProjectsStore::registerTypes();
    SampleStore::registerTypes();
    QThread projectThread, sampleThread;
    auto *projects = new ProjectsStore;
    auto *samples = new SampleStore;
    projects->moveToThread(&projectThread);
    samples->moveToThread(&sampleThread);
    QObject::connect(&projectThread, &QThread::finished, projects, &QObject::deleteLater);
    QObject::connect(&sampleThread, &QThread::finished, samples, &QObject::deleteLater);
    auto *page = new SamplePage(samples);
    SamplesHost host(page);
    host.resize(1600, 900);
    host.setWindowTitle("SN100 样本本地预设（脱机）");
    QObject::connect(page, &SamplePage::statusChanged, &host,
                     [&host](const QString &message)
                     {
                         host.setWindowTitle(message);
                         qInfo().noquote() << message;
                     });
    QObject::connect(projects, &ProjectsStore::ready, &app,
                     [projects, samples, &parser](const QString &)
                     {
                         QMetaObject::invokeMethod(projects, "load", Qt::QueuedConnection, Q_ARG(qint64, 1));
                         QMetaObject::invokeMethod(samples, "initialize", Qt::QueuedConnection,
                                                   Q_ARG(QString, parser.value("catalog-dir")));
                     });
    QObject::connect(projects, &ProjectsStore::snapshotReady, page,
                     [page](qint64, ProjectRecords records, ProjectUnits) { page->setProjects(records); });
    bool failed = false;
    QObject::connect(projects, &ProjectsStore::error, &app,
                     [&](qint64, const QString &, const QString &message)
                     {
                         failed = true;
                         qCritical().noquote() << message;
                         app.exit(3);
                     });
    QObject::connect(samples, &SampleStore::error, &app,
                     [&](qint64 request, const QString &, const QString &message)
                     {
                         if (!request)
                         {
                             failed = true;
                             qCritical().noquote() << message;
                             app.exit(3);
                         }
                     });
    AccountSession session;
    if (parser.isSet("test-engineer"))
    {
        session.authenticated = true;
        session.role = 4;
        session.name = "offlineEngineer";
    }
    page->setSession(session);
    QObject::connect(&projectThread, &QThread::started, projects, [projects, &parser]()
                     { projects->initialize(parser.value("catalog-dir"), parser.isSet("seed-demo")); });
    sampleThread.start();
    projectThread.start();
    host.show();
    const int code = app.exec();
    QMetaObject::invokeMethod(samples, "close", Qt::BlockingQueuedConnection);
    sampleThread.quit();
    sampleThread.wait();
    QMetaObject::invokeMethod(projects, "close", Qt::BlockingQueuedConnection);
    projectThread.quit();
    projectThread.wait();
    return failed ? 3 : code;
}
