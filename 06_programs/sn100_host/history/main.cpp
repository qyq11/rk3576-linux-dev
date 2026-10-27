#include <QApplication>
#include <QCommandLineParser>
#include <QCloseEvent>
#include <QDebug>
#include <QDir>
#include <QFont>
#include <QScreen>
#include <QScrollArea>
#include <QThread>
#include <QTimer>

#include "history_store.h"
#include "history_page.h"
#include "report_controller.h"

class HistoryHost : public QScrollArea
{
public:
    explicit HistoryHost(HistoryPage *page) : m_page(page)
    {
        setWidget(page);
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!m_page->close()) event->ignore();
        else QScrollArea::closeEvent(event);
    }

private:
    HistoryPage *m_page;
};

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sn100_history_offline"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    app.setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SN100 O04-2 匿名历史、均值/CV、原模板报告预览/PDF；无仪器通信"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("offline"), QStringLiteral("独立匿名脱机运行")});
    parser.addOption({QStringLiteral("simulate-role"), QStringLiteral("显式匿名本地角色0至5；0为Guest只读，非生产认证"), QStringLiteral("role"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("专用匿名实验数据绝对目录（必填）"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("exit-after-ms"), QStringLiteral("到期后等待操作完成再安全关闭，未保存草稿保留；0为持续运行"), QStringLiteral("milliseconds"), QStringLiteral("0")});
    parser.process(app);
    const QString directory = QDir::cleanPath(parser.value(QStringLiteral("data-dir")));
    bool validDuration = false;
    const int exitAfter = parser.value(QStringLiteral("exit-after-ms")).toInt(&validDuration);
    bool validRole = false;
    const int role = parser.value(QStringLiteral("simulate-role")).toInt(&validRole);
    if (!parser.isSet(QStringLiteral("data-dir")) || !QDir::isAbsolutePath(directory) ||
        !validDuration || exitAfter < 0 || !validRole || role < 0 || role > 5)
    {
        qCritical() << "An absolute --data-dir, nonnegative --exit-after-ms and --simulate-role in 0..5 are required.";
        return 2;
    }

    HistoryStore::registerTypes();
    QThread databaseThread;
    databaseThread.setObjectName(QStringLiteral("HistoryDatabase"));
    auto *store = new HistoryStore;
    store->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new HistoryPage(store);
    AccountSession session;
    session.name = role == 0 ? QStringLiteral("Guest") : QStringLiteral("Anonymous report test");
    session.role = role;
    session.authenticated = role != 0;
    session.source = role == 0 ? AccountSessionSource::Guest : AccountSessionSource::Simulated;
    page->setSession(session);
    page->setDataDirectory(directory);
    HistoryHost window(page);
    HistoryReportController reports(&window);
    reports.setDataDirectory(directory);
    page->setReportPreviewEnabled(true);
    QObject::connect(page, &HistoryPage::reportPreviewRequested, &reports, &HistoryReportController::preview);
    QObject::connect(&reports, &HistoryReportController::busyChanged, page, &HistoryPage::setReportOperationBusy);
    QObject::connect(&reports, &HistoryReportController::failed, page,
                     [page](const QString &operation, const QString &message) {
        page->setProperty("lastStatus", operation + QStringLiteral(": ") + message);
        emit page->statusChanged(operation + QStringLiteral(": ") + message);
        qWarning().noquote() << "REPORT_ERROR" << operation << message;
    });
    QObject::connect(&reports, &HistoryReportController::pdfExported, page,
                     [page](const QString &path, int rows) {
        const QString message = QStringLiteral("PDF已导出：%1行，%2").arg(rows).arg(path);
        page->setProperty("lastStatus", message);
        emit page->statusChanged(message);
        qInfo().noquote() << "REPORT_PDF" << rows << path;
    });
    QObject::connect(&reports, &HistoryReportController::htmlExported, page,
                     [page](const QString &path, int rows) {
        const QString message = QStringLiteral("HTML已导出：%1行，%2").arg(rows).arg(path);
        page->setProperty("lastStatus", message);
        emit page->statusChanged(message);
        qInfo().noquote() << "REPORT_HTML" << rows << path;
    });
    // Retain the original 1751x968 business coordinates. Scrolling keeps every
    // control reachable on smaller WSLg displays without distorting the page.
    window.setWidgetResizable(false);
    window.setWindowTitle(QStringLiteral("SN100 O04-2 · 匿名历史数据 · 脱机"));
    QObject::connect(page, &HistoryPage::statusChanged, &window, [&window](const QString &message)
    {
        window.setWindowTitle(QStringLiteral("SN100 O04-2 · 脱机 · ") + message);
    });
    const QSize available = app.primaryScreen()->availableGeometry().size() - QSize(40, 60);
    window.resize(QSize(1780, 1020).boundedTo(available));
    QObject::connect(&databaseThread, &QThread::started, store, [store, directory]()
    {
        store->initialize(directory);
    });
    bool startupFailed = false;
    QObject::connect(store, &HistoryStore::error, &app,
                     [&app, &startupFailed](qint64 requestId, const QString &operation, const QString &message)
    {
        qWarning().noquote() << "HISTORY_ERROR" << requestId << operation << message;
        if (requestId == 0)
        {
            startupFailed = true;
            app.exit(3);
        }
    });
    window.show();
    databaseThread.start();
    QTimer exitTimer;
    if (exitAfter > 0)
    {
        exitTimer.setInterval(100);
        QObject::connect(&exitTimer, &QTimer::timeout, &app, [&app, page]()
        {
            if (!page->isBusy() && !page->hasUnsavedChanges()) app.quit();
        });
        QTimer::singleShot(exitAfter, &exitTimer, [&exitTimer]() { exitTimer.start(); });
    }
    qInfo().noquote() << "START mode=anonymous-offline Qt=" << qVersion()
                      << "platform=" << QGuiApplication::platformName() << "data_dir=" << directory;
    const int result = app.exec();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    databaseThread.quit();
    databaseThread.wait();
    qInfo() << "EXIT code=" << (startupFailed ? 3 : result);
    return startupFailed ? 3 : result;
}
