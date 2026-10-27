#include "projects/projects_store.h"
#include "reagents_page.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QThread>
#include <QTimer>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setFont(QFont("Noto Sans CJK SC", 12));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({ "data-dir", "Explicit isolated catalog directory", "path" });
    parser.addOption({ "seed-demo", "Create anonymous projects on a new catalog" });
    parser.addOption(
        { "test-engineer", "Explicit anonymous offline Engineer identity; not production login" });
    parser.addOption(
        { "quit-after-ms", "Request guarded close after this many milliseconds", "milliseconds" });
    parser.addOption({ "screenshot", "Save original logical 1751x968 page PNG", "path" });
    parser.process(app);
    const QString directory = parser.value("data-dir");
    if (!QDir::isAbsolutePath(directory))
    {
        qCritical() << "--data-dir absolute path is required";
        return 2;
    }
    ProjectsStore::registerTypes();
    ReagentStore::registerTypes();
    QThread projectThread, reagentThread;
    auto* projects = new ProjectsStore;
    auto* reagents = new ReagentStore;
    projects->moveToThread(&projectThread);
    reagents->moveToThread(&reagentThread);
    QObject::connect(&projectThread, &QThread::finished, projects, &QObject::deleteLater);
    QObject::connect(&reagentThread, &QThread::finished, reagents, &QObject::deleteLater);
    ReagentsPage page(reagents);
    page.setWindowTitle(QStringLiteral("试剂基础 — 离线隔离目录"));
    page.setFixedSize(1751, 968);
    AccountSession session;
    if (parser.isSet("test-engineer"))
    {
        session.name = "offline_engineer";
        session.authenticated = true;
        session.role = 4;
    }
    page.setSession(session);
    QObject::connect(&page, &ReagentsPage::operationMessage, &app,
        [](QString op, QString message) { qInfo().noquote() << op << message; });
    QObject::connect(
        projects, &ProjectsStore::ready, reagents,
        [reagents, directory, session]()
        {
            reagents->setSession(session);
            reagents->initialize(directory);
        },
        Qt::QueuedConnection);
    QObject::connect(projects, &ProjectsStore::ready, projects, [projects]() { projects->load(1); });
    QObject::connect(projects, &ProjectsStore::snapshotReady, &page,
        [&page](qint64, ProjectRecords records, ProjectUnits)
        {
            ReagentProjects result;
            for (const auto& p : records)
                result.append({ p.id, p.name, p.results.isEmpty() ? QString() : p.results.first().unit });
            page.setProjects(result);
        });
    bool started = false;
    QObject::connect(reagents, &ReagentStore::ready, &app, [&started]() { started = true; });
    QObject::connect(projects, &ProjectsStore::error, &app,
        [&app](qint64, QString op, QString message)
        {
            qCritical().noquote() << op << message;
            app.exit(3);
        });
    QObject::connect(reagents, &ReagentStore::error, &app,
        [&app, &started](QString op, QString message)
        {
            qCritical().noquote() << op << message;
            if (!started)
                app.exit(3);
        });
    projectThread.start();
    reagentThread.start();
    QMetaObject::invokeMethod(projects, "initialize", Qt::QueuedConnection, Q_ARG(QString, directory),
        Q_ARG(bool, parser.isSet("seed-demo")));
    const QString screenshot = parser.value("screenshot");
    if (!screenshot.isEmpty())
        QObject::connect(&page, &ReagentsPage::readyChanged, &page,
            [&page, screenshot](bool ready)
            {
                if (ready)
                    QTimer::singleShot(300, &page, [&page, screenshot]() { page.grab().save(screenshot); });
            });
    if (parser.isSet("quit-after-ms"))
        QTimer::singleShot(parser.value("quit-after-ms").toInt(), &page, &QWidget::close);
    page.show();
    const int result = app.exec();
    QMetaObject::invokeMethod(reagents, "close", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(projects, "close", Qt::BlockingQueuedConnection);
    reagentThread.quit();
    projectThread.quit();
    reagentThread.wait();
    projectThread.wait();
    return result;
}
