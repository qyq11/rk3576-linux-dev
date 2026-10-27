#include "calibration_page.h"
#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QScrollArea>
#include <QThread>
#include <QTimer>

class CalibrationHost : public QScrollArea
{
public:
    explicit CalibrationHost(CalibrationPage *page) : m_page(page)
    { setWidget(page); setWidgetResizable(false); }
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (m_page->prepareToLeave()) QScrollArea::closeEvent(event);
        else event->ignore();
    }
private:
    CalibrationPage *m_page;
};
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion")); app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"data-dir", "校准专属隔离目录（绝对路径）", "directory"});
    parser.addOption({"catalog-db", "显式已保存的匿名catalog.db（仅只读）", "file"});
    parser.addOption({"test-engineer", "显式离线工程师模拟身份，不代表正式登录"});
    parser.addOption({"exit-after-ms", "正常关闭时间；有草稿或busy时仍执行关闭守卫", "milliseconds", "0"});
    parser.process(app);
    bool ok = false; const int duration = parser.value("exit-after-ms").toInt(&ok);
    if (!ok || duration < 0 || !QFileInfo(parser.value("data-dir")).isAbsolute()
        || !QFileInfo(parser.value("catalog-db")).isAbsolute())
    { qCritical() << "Explicit absolute --data-dir and --catalog-db required; duration >= 0"; return 2; }
    CalibrationStore::registerTypes();
    QThread thread;
    auto *store = new CalibrationStore;
    store->moveToThread(&thread);
    QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new CalibrationPage(store);
    CalibrationHost host(page); host.resize(1600, 900);
    host.setWindowTitle(QStringLiteral("SN100 校准本地记录 · 脱机／未审核／未应用"));
    QObject::connect(page, &CalibrationPage::statusChanged, &host, [&host](const QString &message)
    { host.setWindowTitle(message); qInfo().noquote() << message; });
    AccountSession session;
    if (parser.isSet("test-engineer"))
    {
        session.name = QStringLiteral("offlineEngineer"); session.role = 4;
        session.authenticated = true; session.source = AccountSessionSource::Simulated;
    }
    page->setSession(session);
    bool failed = false;
    QObject::connect(store, &CalibrationStore::error, &app,
        [&](qint64 request, const QString &, const QString &message)
    {
        if (request == 0) { failed = true; qCritical().noquote() << message; app.exit(3); }
    });
    QObject::connect(&thread, &QThread::started, store, [&parser, store]()
    { store->initialize(parser.value("data-dir"), parser.value("catalog-db")); });
    thread.start(); host.show();
    if (duration > 0) QTimer::singleShot(duration, &host, &QWidget::close);
    const int result = app.exec();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    thread.quit(); thread.wait();
    qInfo() << "calibration normal worker/database shutdown, exit" << (failed ? 3 : result);
    return failed ? 3 : result;
}
