#include "project_page.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDebug>
#include <QScrollArea>
#include <QThread>
#include <QTimer>

class ProjectsHost : public QScrollArea
{
  public:
    explicit ProjectsHost(ProjectPage *page) : m_page(page)
    {
        setWidget(page);
    }

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_page->prepareToLeave())
        {
            event->ignore();
        }
        else
        {
            QScrollArea::closeEvent(event);
        }
    }

  private:
    ProjectPage *m_page;
};
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption(
        {QStringLiteral("data-dir"), QStringLiteral("隔离目录绝对路径"), QStringLiteral("directory")});
    parser.addOption(
        {QStringLiteral("seed-demo"), QStringLiteral("仅新库：显式匿名演示项目/单位，不含生产参数")});
    parser.addOption(
        {QStringLiteral("test-engineer"), QStringLiteral("显式离线工程师模拟身份，不代表生产登录")});
    parser.addOption({QStringLiteral("exit-after-ms"), QStringLiteral("退出时间，默认0"),
                      QStringLiteral("milliseconds"), QStringLiteral("0")});
    parser.process(app);
    if (!parser.isSet(QStringLiteral("data-dir")))
    {
        qCritical() << "--data-dir required";
        return 2;
    }
    ProjectsStore::registerTypes();
    QThread thread;
    auto *store = new ProjectsStore;
    store->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new ProjectPage(store);
    ProjectsHost host(page);
    host.setWidgetResizable(false);
    host.resize(1600, 900);
    host.setWindowTitle(QStringLiteral("SN100 项目/单位与测试方案 · 本地脱机"));
    QObject::connect(page, &ProjectPage::statusChanged, &host,
                     [&host](const QString &message)
                     {
                         host.setWindowTitle(message);
                         qInfo().noquote() << message;
                     });
    AccountSession session;
    if (parser.isSet(QStringLiteral("test-engineer")))
    {
        session.name = QStringLiteral("offlineEngineer");
        session.authenticated = true;
        session.role = 4;
    }
    page->setSession(session);
    bool failed = false;
    QObject::connect(store, &ProjectsStore::error, &app,
                     [&app, &failed](qint64 id, const QString &, const QString &message)
                     {
                         if (id == 0)
                         {
                             failed = true;
                             qCritical().noquote() << message;
                             app.exit(3);
                         }
                     });
    QObject::connect(&thread, &QThread::started, store,
                     [store, &parser]()
                     {
                         store->initialize(parser.value(QStringLiteral("data-dir")),
                                           parser.isSet(QStringLiteral("seed-demo")));
                     });
    thread.start();
    host.show();
    bool valid = false;
    const int duration = parser.value(QStringLiteral("exit-after-ms")).toInt(&valid);
    if (!valid || duration < 0)
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
        return 2;
    }
    if (duration > 0)
    {
        QTimer::singleShot(duration, &host, &QWidget::close);
    }
    const int result = app.exec();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();
    return failed ? 3 : result;
}
