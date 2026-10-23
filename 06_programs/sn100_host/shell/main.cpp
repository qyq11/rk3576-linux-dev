#include <QApplication>
#include <QCloseEvent>
#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include "frmmain.h"
#include "shutdown_request.h"
#include "data/hospital_store.h"
#include "data/account_store.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"
#include "history/history_page.h"
#include "history/history_store.h"

// Keep the original 1920x1080 logical canvas. A smaller WSL desktop scrolls
// outside that canvas instead of stretching or rearranging business controls.
class ShellViewport : public QWidget
{
public:
    ShellViewport(frmMain *shell, const QSize &viewportSize, bool accounts, bool history) : shell_(shell)
    {
        setObjectName(QStringLiteral("shellViewport"));
        setWindowTitle(history ? QStringLiteral("SN100 · 统一脱机主界面 · 报告预览/PDF · 模拟会话")
                       : accounts ? QStringLiteral("SN100 · O03-2 脱机主框架 · 模拟会话")
                                : QStringLiteral("SN100 · O03-1 脱机主框架"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("shellScrollArea"));
        scroll->setWidgetResizable(false);
        shell_->setFixedSize(1920, 1080);
        scroll->setWidget(shell_);
        layout->addWidget(scroll);
        resize(viewportSize);
        setMinimumSize(900, 600);
    }

protected:
    void closeEvent(QCloseEvent *event) override
    {
        // QWidget::close() returns false when the page guard rejects it.
        if (!shell_->close())
        {
            event->ignore();
            return;
        }
        event->accept();
    }

private:
    frmMain *shell_;
};

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sn100_shell"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));
    app.setOrganizationName(QStringLiteral("Seerain"));
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("脱机主框架：医院信息 / 账户权限 / 历史查询；无设备通信"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("offline"), QStringLiteral("脱机模式（本目标始终脱机）")});
    parser.addOption({QStringLiteral("accounts"), QStringLiteral("O03-2：接入O02账户页，默认Guest；使用独立子目录")});
    parser.addOption({QStringLiteral("history"), QStringLiteral("O03-3：接入匿名历史查询；使用独立history子目录")});
    parser.addOption({QStringLiteral("test-session"), QStringLiteral("显式模拟匿名操作者；不代表生产登录认证"), QStringLiteral("name")});
    parser.addOption({QStringLiteral("device-state"), QStringLiteral("仅模拟账户操作条件 ready 或 running"), QStringLiteral("state"), QStringLiteral("ready")});
    parser.addOption({QStringLiteral("seed-anonymous"), QStringLiteral("仅新账户库：创建五个匿名角色账号")});
    parser.addOption({QStringLiteral("test-secret-file"), QStringLiteral("仅首次匿名初始化读取的临时密码文件，不记录内容"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("独立匿名医院数据的绝对目录"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("viewport"), QStringLiteral("外部滚动宿主尺寸，如1600x900；业务画布保持1920x1080"), QStringLiteral("WxH")});
    parser.process(app);
    const QString dataDir = QDir::cleanPath(parser.value(QStringLiteral("data-dir")));
    if (!parser.isSet(QStringLiteral("data-dir")) || !QDir::isAbsolutePath(dataDir))
    {
        qCritical() << "--data-dir must specify an absolute isolated offline directory";
        return 2;
    }
    const bool withAccounts = parser.isSet(QStringLiteral("accounts"));
    const bool withHistory = parser.isSet(QStringLiteral("history"));
    const QString state = parser.value(QStringLiteral("device-state"));
    if ((state != QStringLiteral("ready") && state != QStringLiteral("running"))
        || (!withAccounts && (parser.isSet(QStringLiteral("test-session"))
            || parser.isSet(QStringLiteral("seed-anonymous"))
            || parser.isSet(QStringLiteral("test-secret-file"))
            || parser.isSet(QStringLiteral("device-state")))))
    {
        qCritical() << "Account options require --accounts; --device-state must be ready or running (simulated)";
        return 2;
    }
    QString bootstrapPassword;
    if (parser.isSet(QStringLiteral("seed-anonymous")))
    {
        const QFileInfo info(parser.value(QStringLiteral("test-secret-file")));
        QFile secret(info.absoluteFilePath());
        if (!info.isAbsolute() || !info.isFile() || info.isSymLink()
            || info.size() > 256 || !secret.open(QIODevice::ReadOnly))
        {
            qCritical() << "Seeding requires an absolute regular --test-secret-file (max 256 bytes)";
            return 2;
        }
        bootstrapPassword = QString::fromUtf8(secret.readAll()).trimmed();
        if (bootstrapPassword.isEmpty())
        {
            qCritical() << "Anonymous bootstrap secret must not be empty";
            return 2;
        }
    }
    else if (parser.isSet(QStringLiteral("test-secret-file")))
    {
        qCritical() << "--test-secret-file requires --seed-anonymous";
        return 2;
    }
    QSize viewportSize(1920, 1080);
    if (QGuiApplication::primaryScreen())
    {
        const QSize available = QGuiApplication::primaryScreen()->availableGeometry().size();
        viewportSize = QSize(qMin(1920, available.width() - 32), qMin(1080, available.height() - 64));
        viewportSize = viewportSize.expandedTo(QSize(900, 600));
    }
    if (parser.isSet(QStringLiteral("viewport")))
    {
        const QRegularExpression expression(QStringLiteral("^([0-9]{3,4})x([0-9]{3,4})$"));
        const auto match = expression.match(parser.value(QStringLiteral("viewport")));
        if (!match.hasMatch() || match.captured(1).toInt() < 900 || match.captured(2).toInt() < 600
                || match.captured(1).toInt() > 3840 || match.captured(2).toInt() > 2160)
        {
            qCritical() << "--viewport must be between 900x600 and 3840x2160";
            return 2;
        }
        viewportSize = QSize(match.captured(1).toInt(), match.captured(2).toInt());
    }

    HospitalStore::registerTypes();
    AccountStore::registerTypes();
    HistoryStore::registerTypes();
    ShutdownRequest shutdown;
    QString shutdownError;
    if (!shutdown.initialize(&shutdownError))
    {
        qCritical().noquote() << "SHUTDOWN_SETUP_FAILED" << shutdownError;
        return 2;
    }
    QThread databaseThread;
    databaseThread.setObjectName(QStringLiteral("O03HospitalDatabase"));
    auto *store = new HospitalStore;
    store->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished, store, &QObject::deleteLater);
    QThread accountThread;
    accountThread.setObjectName(QStringLiteral("O03AccountDatabase"));
    AccountStore *accounts = withAccounts ? new AccountStore : nullptr;
    if (accounts)
    {
        accounts->moveToThread(&accountThread);
        QObject::connect(&accountThread, &QThread::finished, accounts, &QObject::deleteLater);
    }
    QThread historyThread;
    historyThread.setObjectName(QStringLiteral("O03HistoryDatabase"));
    HistoryStore *history = withHistory ? new HistoryStore : nullptr;
    if (history)
    {
        history->moveToThread(&historyThread);
        QObject::connect(&historyThread, &QThread::finished, history, &QObject::deleteLater);
    }
    auto *shell = new frmMain(store, nullptr, accounts);
    const QString hospitalDir = (withAccounts || withHistory)
        ? QDir(dataDir).filePath(QStringLiteral("hospital")) : dataDir;
    shell->setDataDirectory(hospitalDir);
    bool hospitalReady = false;
    bool accountReady = !withAccounts;
    bool historyReady = !withHistory;
    bool sessionObserved = !withAccounts;
    bool readyReported = false;
    bool reportSetupFailed = false;
    const auto reportReady = [&]()
    {
        if (!reportSetupFailed && hospitalReady && accountReady && historyReady && sessionObserved && !readyReported)
        {
            readyReported = true;
            qInfo() << "SHELL_READY";
        }
    };
    QObject::connect(shell->hospitalPage(), &frmHospInfoSetting::operationFinished,
                     &app, [&](const QString &operation, bool success)
    {
        if (operation == QStringLiteral("initialize") && success)
        {
            hospitalReady = true;
            reportReady();
        }
        else if (operation == QStringLiteral("initialize")) app.exit(3);
    });
    if (accounts)
    {
        const QString accountDir = QDir(dataDir).filePath(QStringLiteral("account"));
        const QString actor = parser.value(QStringLiteral("test-session"));
        QObject::connect(&accountThread, &QThread::started, accounts,
                         [accounts, accountDir, bootstrapPassword]()
        {
            accounts->initialize(accountDir, bootstrapPassword);
        });
        QObject::connect(accounts, &AccountStore::ready, accounts, [accounts, actor, state]()
        {
            accounts->setSession(actor, state == QStringLiteral("ready"));
        });
        QObject::connect(shell, &frmMain::sessionChanged, &app, [&](AccountSession)
        {
            sessionObserved = true;
            reportReady();
        });
        QObject::connect(shell->accountPage(), &frmAccountSetting::operationFinished,
                         &app, [&](const QString &, bool success)
        {
            if (success && shell->accountPage()->isReady() && !shell->accountPage()->isBusy())
            {
                accountReady = true;
                reportReady();
            }
        });
        QObject::connect(accounts, &AccountStore::error, &app, [&](const QString &operation, const QString &message)
        {
            qWarning().noquote() << "ACCOUNT_ERROR" << operation << message;
            if (operation == QStringLiteral("initialize") || (!sessionObserved && operation == QStringLiteral("session"))) app.exit(3);
        });
        QObject::connect(accounts, &AccountStore::closed, accounts, []() { qInfo() << "ACCOUNT_STORE_CLOSED"; });
        QObject::connect(shell, &frmMain::loggedOut, &app, []() { qInfo() << "OFFLINE_SESSION_LOGGED_OUT"; });
    }
    if (history)
    {
        auto *page = new HistoryPage(history);
        const QString historyDir = QDir(dataDir).filePath(QStringLiteral("history"));
        page->setDataDirectory(historyDir);
        shell->attachHistoryPage(page);
        if (!shell->enableHistoryReports(historyDir))
        {
            qCritical() << "REPORT_SETUP_FAILED";
            reportSetupFailed = true;
            // Enter the normal event loop/three-worker teardown path even if
            // embedded report resources are missing during initialization.
            QTimer::singleShot(0, &app, [&app]() { app.exit(3); });
        }
        QObject::connect(&historyThread, &QThread::started, history,
                         [history, historyDir]() { history->initialize(historyDir); });
        QObject::connect(page, &HistoryPage::operationFinished, &app,
                         [&](const QString &operation, bool success)
        {
            // ready()会立即启动首轮查询；查询返回后才允许主入口报就绪。
            if (operation == QStringLiteral("query") && success
                && shell->historyPage()->isReady() && !shell->historyPage()->isBusy())
            {
                if (!historyReady) qInfo() << "HISTORY_INITIAL_QUERY_READY";
                historyReady = true;
                reportReady();
            }
            else if (!success && (operation == QStringLiteral("initialize") || !historyReady)) app.exit(3);
        });
        QObject::connect(history, &HistoryStore::error, &app,
                         [](qint64, const QString &operation, const QString &message)
        {
            qWarning().noquote() << "HISTORY_ERROR" << operation << message;
        });
        QObject::connect(history, &HistoryStore::closed, history, []() { qInfo() << "HISTORY_STORE_CLOSED"; });
    }
    ShellViewport viewport(shell, viewportSize, withAccounts, withHistory);
    shutdown.setWindow(&viewport);
    QObject::connect(&databaseThread, &QThread::started, store, [store, hospitalDir]()
    {
        store->initialize(QDir(hospitalDir).filePath(QStringLiteral("hospital_Information.db")),
                          QDir(hospitalDir).filePath(QStringLiteral("config.ini")));
    });
    QObject::connect(store, &HospitalStore::error, &app, [&](const QString &operation, const QString &message)
    {
        qWarning().noquote() << "STORAGE_ERROR" << operation << message;
        if (operation == QStringLiteral("initialize")) app.exit(3);
    });
    QObject::connect(store, &HospitalStore::warning, &app, [](const QString &message)
    {
        qWarning().noquote() << "STORAGE_WARNING" << message;
    });
    QObject::connect(store, &HospitalStore::closed, store, [withAccounts]()
    {
        qInfo() << (withAccounts ? "HOSPITAL_STORE_CLOSED" : "STORE_CLOSED");
    });
    viewport.show();
    databaseThread.start();
    if (accounts) accountThread.start();
    if (history) historyThread.start();
    qInfo().noquote() << "START mode=offline module=" << (withHistory ? "O03-4" : withAccounts ? "O03-2" : "O03-1") << "Qt=" << qVersion()
                      << "platform=" << QGuiApplication::platformName()
                      << "logical_canvas=1920x1080 viewport=" << viewportSize << "data_dir=" << dataDir;
    const int result = app.exec();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    if (accounts) QMetaObject::invokeMethod(accounts, "close", Qt::BlockingQueuedConnection);
    if (history) QMetaObject::invokeMethod(history, "close", Qt::BlockingQueuedConnection);
    databaseThread.quit();
    if (accounts) accountThread.quit();
    if (history) historyThread.quit();
    databaseThread.wait();
    if (accounts) accountThread.wait();
    if (history) historyThread.wait();
    if (accounts || history) qInfo() << "THREADS_STOPPED" << (!databaseThread.isRunning() && !accountThread.isRunning() && !historyThread.isRunning());
    else qInfo() << "THREAD_STOPPED" << !databaseThread.isRunning();
    qInfo() << "EXIT code=" << result;
    return result;
}
