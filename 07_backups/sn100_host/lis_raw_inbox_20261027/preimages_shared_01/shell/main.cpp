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
#include "sample_qc_shell.h"
#include "printer_report_shell.h"
#include "shutdown_request.h"
#include "data/hospital_store.h"
#include "data/account_store.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"
#include "history/history_page.h"
#include "history/history_store.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include "protocol/status_replay.h"
#include "protocol/status_loopback.h"
#include "instrument/instrument_page.h"
#include "instrument/maintenance_store.h"
#include "data/printer_settings_store.h"
#include "ui/frmprintersetting.h"
#include "data/lis_settings_store.h"
#include "ui/frmlissetting.h"
#include "quality_control/quality_control_page.h"
#include "quality_control/qc_store.h"
#include "calibration/calibration_page.h"
#include "ui/language_setting_page.h"
#include "alarm_history/alarm_history_host.h"

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
    app.setApplicationVersion(QStringLiteral("0.5.0"));
    app.setOrganizationName(QStringLiteral("Seerain"));
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("统一脱机主框架；可选本机模拟只读状态，无真实仪器连接"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("offline"), QStringLiteral("脱机模式（本目标始终脱机）")});
    parser.addOption({QStringLiteral("accounts"), QStringLiteral("O03-2：接入O02账户页，默认Guest；使用独立子目录")});
    parser.addOption({QStringLiteral("password-login"), QStringLiteral("O02-2：启用原版本地密码登录卡；需要--accounts，默认仍为Guest")});
    parser.addOption({QStringLiteral("printer-settings"), QStringLiteral("原打印机本地设置；已提交设备用于原报告单次确认打印，不改系统默认设备")});
    parser.addOption({QStringLiteral("lis-settings"), QStringLiteral("O10-2：原LIS本地设置；只保存意图，不连接或打开串口")});
    parser.addOption({QStringLiteral("quality-control"), QStringLiteral("原质控品/项目/批次本地页面；需要--catalog，不执行检测或失控判定")});
    parser.addOption({QStringLiteral("calibration"), QStringLiteral("原校准记录/原始点本地页面；需要--catalog，不拟合、审核或应用曲线")});
    parser.addOption({QStringLiteral("alarm-history"), QStringLiteral("仅启用本地既有告警历史入口；实时告警和设备动作未知")});
    parser.addOption({QStringLiteral("alarm-history-db"), QStringLiteral("显式已批准匿名/迁移只读历史文件绝对路径；不创建文件"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("history"), QStringLiteral("O03-3：接入匿名历史查询；使用独立history子目录")});
    parser.addOption({QStringLiteral("local-results"), QStringLiteral("启用空白本地原值结果库；需要--history和--catalog，未接检测调度、单位映射或文件输出")});
    parser.addOption({QStringLiteral("catalog"), QStringLiteral("O07：接入本地项目/单位及试剂基础；使用catalog子目录，需要--accounts")});
    parser.addOption({QStringLiteral("samples"), QStringLiteral("O08：接入本地样本预设/任务；需要--catalog，无真实投递或执行")});
    parser.addOption({QStringLiteral("instrument"), QStringLiteral("仪器操作原版页面及本地维护记录；需要--accounts，设备动作禁用")});
    parser.addOption({QStringLiteral("status-replay"), QStringLiteral("O09-1：显式匿名状态文件回放；无网络或设备动作"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("status-loopback"), QStringLiteral("O09-2：自建127.0.0.1模拟端的后台只读Qj；不接受外部端点，不执行仪器动作")});
    parser.addOption({QStringLiteral("seed-catalog-demo"), QStringLiteral("仅新catalog库：显式匿名项目/单位/比例候选，非生产配置")});
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
    const bool withPasswordLogin = parser.isSet(QStringLiteral("password-login"));
    const bool withPrinterSettings = parser.isSet(QStringLiteral("printer-settings"));
    const bool withLisSettings = parser.isSet(QStringLiteral("lis-settings"));
    const bool withQualityControl = parser.isSet(QStringLiteral("quality-control"));
    const bool withCalibration = parser.isSet(QStringLiteral("calibration"));
    const bool withAlarmHistory = parser.isSet(QStringLiteral("alarm-history"));
    const QString alarmHistoryPath = parser.isSet(QStringLiteral("alarm-history-db"))
        ? parser.value(QStringLiteral("alarm-history-db"))
        : QDir(dataDir).filePath(QStringLiteral("alarm_history/abnormal_log.db"));
    if ((parser.isSet(QStringLiteral("alarm-history-db")) && !withAlarmHistory)
        || (withAlarmHistory && (!QDir::isAbsolutePath(alarmHistoryPath) || alarmHistoryPath.contains(QChar::Null))))
    {
        qCritical() << "--alarm-history-db requires --alarm-history and an absolute explicit path";
        return 2;
    }
    // Do not mkdir, discover, open or validate a history schema at startup.
    // The explicit badge/view request uses Store::openExisting read-only.

    const bool withHistory = parser.isSet(QStringLiteral("history"));
    const bool withCatalog = parser.isSet(QStringLiteral("catalog"));
    const bool withLocalResults = parser.isSet(QStringLiteral("local-results"));
    if (withLocalResults && (!withHistory || !withCatalog))
    {
        qCritical() << "--local-results requires --history and --catalog";
        return 2;
    }
    const bool withSamples = parser.isSet(QStringLiteral("samples"));
    const bool withInstrument = parser.isSet(QStringLiteral("instrument"));
    const bool withStatusReplay = parser.isSet(QStringLiteral("status-replay"));
    const bool withStatusLoopback = parser.isSet(QStringLiteral("status-loopback"));
    if (withStatusReplay && withStatusLoopback)
    {
        qCritical() << "--status-replay and --status-loopback are mutually exclusive";
        return 2;
    }
    Sn100Protocol::StatusReplay statusReplay;
    QTimer statusReplayTimer;
    Sn100Protocol::StatusLoopbackController statusLoopback;
    if (withStatusReplay)
    {
        QString error;
        // 先验证整个输入，再创建数据库/窗口；坏回放不留下部分初始化数据。
        if (!statusReplay.loadFile(parser.value(QStringLiteral("status-replay")), &error))
        {
            qCritical().noquote() << "STATUS_REPLAY_INPUT_FAILED" << error;
            return 2;
        }
        qInfo() << "STATUS_REPLAY_LOADED" << statusReplay.current().eventCount
                << "events; recorded virtual time; no network or instrument";
    }
    if (((withCatalog || withInstrument || withPasswordLogin) && !withAccounts) || (withSamples && !withCatalog)
        || ((parser.isSet(QStringLiteral("seed-catalog-demo")) || withQualityControl || withCalibration) && !withCatalog))
    {
        qCritical() << "--catalog, --instrument and --password-login require --accounts; --samples, --quality-control, --calibration and --seed-catalog-demo require --catalog";
        return 2;
    }
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
    QThread projectThread, reagentThread;
    projectThread.setObjectName(QStringLiteral("O07ProjectDatabase"));
    reagentThread.setObjectName(QStringLiteral("O07ReagentDatabase"));
    ProjectsStore *projects = withCatalog ? new ProjectsStore : nullptr;
    ReagentStore *reagents = withCatalog ? new ReagentStore : nullptr;
    if (withCatalog)
    {
        ProjectsStore::registerTypes();ReagentStore::registerTypes();
        projects->moveToThread(&projectThread);reagents->moveToThread(&reagentThread);
        QObject::connect(&projectThread,&QThread::finished,projects,&QObject::deleteLater);
        QObject::connect(&reagentThread,&QThread::finished,reagents,&QObject::deleteLater);
    }
    QThread sampleThread;
    sampleThread.setObjectName(QStringLiteral("O08SampleDatabase"));
    SampleStore *samples=withSamples ? new SampleStore : nullptr;
    if (samples)
    {
        SampleStore::registerTypes();
        samples->moveToThread(&sampleThread);
        QObject::connect(&sampleThread,&QThread::finished,samples,&QObject::deleteLater);
    }
    QThread maintenanceThread;
    maintenanceThread.setObjectName(QStringLiteral("InstrumentMaintenanceDatabase"));
    MaintenanceStore *maintenance = withInstrument ? new MaintenanceStore : nullptr;
    if (maintenance)
    {
        MaintenanceStore::registerTypes();
        maintenance->moveToThread(&maintenanceThread);
        QObject::connect(&maintenanceThread,&QThread::finished,maintenance,&QObject::deleteLater);
    }
    QThread printerThread;
    printerThread.setObjectName(QStringLiteral("O10PrinterSettings"));
    auto *printer = withPrinterSettings ? new PrinterSettingsStore : nullptr;
    if (printer)
    {
        printer->moveToThread(&printerThread);
        QObject::connect(&printerThread, &QThread::finished, printer, &QObject::deleteLater);
    }
    QThread lisThread;
    lisThread.setObjectName(QStringLiteral("O10LisSettings"));
    auto *lis = withLisSettings ? new LisSettingsStore : nullptr;
    if (lis)
    {
        lis->moveToThread(&lisThread);
        QObject::connect(&lisThread, &QThread::finished, lis, &QObject::deleteLater);
    }
    auto *shell = new frmMain(store, nullptr, accounts);
    shell->attachLanguagePage(new LanguageSettingPage);
    QThread alarmHistoryThread;
    alarmHistoryThread.setObjectName(QStringLiteral("AlarmHistoryReadOnlyDatabase"));
    auto *alarmHistory = withAlarmHistory ? new AlarmHistory::Store : nullptr;
    if (alarmHistory)
    {
        AlarmHistory::Store::registerTypes();
        alarmHistory->moveToThread(&alarmHistoryThread);
        QObject::connect(&alarmHistoryThread, &QThread::finished, alarmHistory, &QObject::deleteLater);
        shell->enableAlarmHistory(alarmHistory, alarmHistoryPath);
        QObject::connect(alarmHistory, &AlarmHistory::Store::closed, alarmHistory, []()
        { qInfo() << "ALARM_HISTORY_SOURCE_CLOSED"; });
        // closed() also means recoverable source release. Never connect it to quit().
    }
    QThread qualityThread;
    qualityThread.setObjectName(QStringLiteral("QualityControlDatabase"));
    auto *quality = withQualityControl ? new QcStore : nullptr;
    if (quality)
    {
        QcStore::registerTypes();
        quality->moveToThread(&qualityThread);
        QObject::connect(&qualityThread, &QThread::finished, quality, &QObject::deleteLater);
    }
    QThread calibrationThread;
    calibrationThread.setObjectName(QStringLiteral("CalibrationDatabase"));
    auto *calibration = withCalibration ? new CalibrationStore : nullptr;
    if (calibration)
    {
        CalibrationStore::registerTypes();
        calibration->moveToThread(&calibrationThread);
        QObject::connect(&calibrationThread, &QThread::finished, calibration, &QObject::deleteLater);
    }
    QObject::connect(&statusLoopback, &Sn100Protocol::StatusLoopbackController::snapshotChanged,
                     shell, &frmMain::setLoopbackStatusState);
    QObject::connect(&statusLoopback, &Sn100Protocol::StatusLoopbackController::wireObserved,
                     &app, [](const Sn100Protocol::LoopbackWireEvent &event)
    {
        qInfo().noquote() << "STATUS_LOOPBACK_" + event.direction << event.elapsedMs
            << "run=" << event.runId << "generation=" << event.connectionGeneration
            << "sequence=" << event.sequence << event.bytes.toHex(' ').toUpper();
    });
    QObject::connect(&statusLoopback, &Sn100Protocol::StatusLoopbackController::error,
                     &app, [](const QString &message) { qWarning().noquote() << "STATUS_LOOPBACK_ERROR" << message; });
    QObject::connect(&statusLoopback, &Sn100Protocol::StatusLoopbackController::started,
                     &app, []() { qInfo() << "STATUS_LOOPBACK_STARTED self_owned_peer=127.0.0.1 instrument=false"; });
    const QString hospitalDir = (withAccounts || withHistory)
        ? QDir(dataDir).filePath(QStringLiteral("hospital")) : dataDir;
    shell->setDataDirectory(hospitalDir);
    bool hospitalReady = false;
    bool accountReady = !withAccounts;
    bool historyReady = !withHistory;
    bool projectReady = !withCatalog;
    bool reagentReady = !withCatalog;
    bool sampleReady = !withSamples;
    bool instrumentReady = !withInstrument;
    bool printerReady = !withPrinterSettings;
    bool lisReady = !withLisSettings;
    bool qualityReady = !withQualityControl;
    bool qualityBound = !withQualityControl;
    bool calibrationReady = !withCalibration;
    bool sessionObserved = !withAccounts;
    bool readyReported = false;
    bool reportSetupFailed = false;
    const auto reportReady = [&]()
    {
        if (!reportSetupFailed && hospitalReady && accountReady && historyReady && projectReady && reagentReady && sampleReady
            && instrumentReady && printerReady && lisReady && qualityReady && qualityBound && calibrationReady && sessionObserved && !readyReported)
        {
            readyReported = true;
            if (withPasswordLogin && !shell->enablePasswordLogin())
            {
                qCritical() << "PASSWORD_LOGIN_SETUP_FAILED";
                app.exit(3);
                return;
            }
            qInfo() << "SHELL_READY";
            if (withLocalResults) qInfo() << "LOCAL_RESULTS_READY empty_profile_allowed=true scheduler=false unit_mapping=false file_output=false";
            if (alarmHistory)
            {
                shell->setAlarmHistoryAvailable(alarmHistoryThread.isRunning());
                qInfo() << "ALARM_HISTORY_ENABLED local_history_only=true source_opened=false live_alarms_unknown=true";
            }
            if (withPrinterSettings) qInfo() << "PRINTER_SETTINGS_READY local_only=true print_job=false";
            if (withLisSettings) qInfo() << "LIS_SETTINGS_READY local_only=true communication=false";
            if (withQualityControl) qInfo() << "QUALITY_CONTROL_READY local_only=true clinical_rules=false";
            if (withCalibration) qInfo() << "CALIBRATION_READY local_only=true fitting=false applied=false";
            if (withPasswordLogin) qInfo() << "PASSWORD_LOGIN_ENABLED; default Guest; isolated local accounts only";
            if (withStatusReplay)
            {
                shell->selectMainPage(7);
                shell->setStatusReplayState(statusReplay.current());
                statusReplayTimer.start(250);
            }
            if (withStatusLoopback)
            {
                shell->selectMainPage(7);
                statusLoopback.start();
            }
        }
    };
    if (alarmHistory)
    {
        QObject::connect(&alarmHistoryThread, &QThread::started, shell, [&, shell]()
        { if (readyReported) shell->setAlarmHistoryAvailable(true); }, Qt::QueuedConnection);
    }
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
        QObject::connect(shell, &frmMain::sessionChanged, &app, [&,actor](AccountSession session)
        {
            // initialize() emits Guest before the requested simulated actor is
            // selected. That reset must not acknowledge a missing/invalid actor.
            const bool identityMatches = actor.isEmpty() ? !session.authenticated
                : session.authenticated && session.name==actor;
            if (identityMatches && session.actionReady==(state==QStringLiteral("ready"))) sessionObserved=true;
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
            if (operation == QStringLiteral("initialize")
                || (!readyReported && (operation==QStringLiteral("session") || !accountReady))) app.exit(3);
        });
        QObject::connect(accounts, &AccountStore::closed, accounts, []() { qInfo() << "ACCOUNT_STORE_CLOSED"; });
        QObject::connect(shell, &frmMain::loggedOut, &app, []() { qInfo() << "OFFLINE_SESSION_LOGGED_OUT"; });
    }
    if (history)
    {
        auto *page = new HistoryPage(history);
        const QString historyDir = QDir(dataDir).filePath(QStringLiteral("history"));
        page->setDataDirectory(historyDir);
        page->setLocalResultsMode(withLocalResults);
        shell->attachHistoryPage(page);
        page->setHospitalDictionaryAvailable(false);
        QObject::connect(page, &HistoryPage::requestHospitalDictionary,
                         store, &HospitalStore::queryCommittedDictionary, Qt::QueuedConnection);
        QObject::connect(store, &HospitalStore::dictionaryQueryCompleted,
                         page, &HistoryPage::receiveHospitalDictionary, Qt::QueuedConnection);
        QObject::connect(store, &HospitalStore::ready, page, [page]()
        { page->setHospitalDictionaryAvailable(true); }, Qt::QueuedConnection);
        QObject::connect(store, &HospitalStore::closed, page, [page]()
        { page->setHospitalDictionaryAvailable(false); }, Qt::QueuedConnection);
        if (!withLocalResults && !shell->enableHistoryReports(historyDir))
        {
            qCritical() << "REPORT_SETUP_FAILED";
            reportSetupFailed = true;
            // Enter the normal event loop/three-worker teardown path even if
            // embedded report resources are missing during initialization.
            QTimer::singleShot(0, &app, [&app]() { app.exit(3); });
        }
        if (withLocalResults)
        {
            const QString catalogDir = QDir(dataDir).filePath(QStringLiteral("catalog"));
            // Catalog schema writers finish before this worker opens its own
            // protected project-reference connection. The mode never seeds reports.
            const auto initializeResults = [history, historyDir, catalogDir]()
            { history->initializeLocalResults(historyDir, catalogDir); };
            if (samples)
                QObject::connect(samples, &SampleStore::ready, history, initializeResults, Qt::QueuedConnection);
            else
                QObject::connect(reagents, &ReagentStore::ready, history, initializeResults, Qt::QueuedConnection);
        }
        else
        {
            QObject::connect(&historyThread, &QThread::started, history,
                             [history, historyDir]() { history->initialize(historyDir); });
        }
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
    if (withCatalog)
    {
        auto *projectPage = new ProjectPage(projects);
        auto *reagentPage = new ReagentsPage(reagents);
        shell->attachCatalogPages(projectPage,reagentPage);
        const QString catalogDir = QDir(dataDir).filePath(QStringLiteral("catalog"));
        if (quality)
        {
            auto *page = new QualityControlPage(quality);
            shell->attachQualityControlPage(page);
            const QString qualityDir = QDir(dataDir).filePath(QStringLiteral("quality_control"));
            QObject::connect(projects, &ProjectsStore::ready, quality,
                             [quality, qualityDir, catalogDir]()
            {
                quality->initialize(qualityDir);
                quality->bindCatalog(catalogDir);
            }, Qt::QueuedConnection);
            QObject::connect(quality, &QcStore::catalogBound, &app, [&](QString)
            { qualityBound = true; reportReady(); });
            QObject::connect(page, &QualityControlPage::operationFinished, &app,
                             [&, page](const QString &, bool success)
            {
                if (!qualityReady && success && page->isReady() && !page->isBusy())
                { qualityReady = true; reportReady(); }
            });
            QObject::connect(quality, &QcStore::error, &app,
                             [&](qint64, const QString &operation, const QString &message)
            {
                qWarning().noquote() << "QUALITY_CONTROL_ERROR" << operation << message;
                if (!qualityReady || !qualityBound) app.exit(3);
            });
            QObject::connect(quality, &QcStore::warning, &app, [](const QString &message)
            { qWarning().noquote() << "QUALITY_CONTROL_REFERENCE_WARNING" << message; });
            QObject::connect(quality, &QcStore::closed, quality,
                             []() { qInfo() << "QUALITY_CONTROL_STORE_CLOSED"; });
        }
        if (samples)
        {
            auto *samplePage=new SamplePage(samples);
            shell->attachSamplePage(samplePage);
            // Schema changes on the shared catalog are serialized: project,
            // reagent, then sample. Each store owns a separate SQL connection.
            QObject::connect(reagents,&ReagentStore::ready,samples,
                             [samples,catalogDir]() { samples->initialize(catalogDir); },Qt::QueuedConnection);
            QObject::connect(samplePage,&SamplePage::operationFinished,&app,
                             [&,samplePage](const QString &operation,bool success)
            {
                if (operation==QStringLiteral("load") && success && samplePage->isReady() && !samplePage->isBusy())
                { sampleReady=true;reportReady(); }
            });
            QObject::connect(samples,&SampleStore::error,&app,[&](qint64,const QString &operation,const QString &message)
            {
                qWarning().noquote()<<"SAMPLE_ERROR"<<operation<<message;
                if (!sampleReady) app.exit(3);
            });
            QObject::connect(samples,&SampleStore::closed,samples,[]() { qInfo()<<"SAMPLE_STORE_CLOSED"; });
            if (quality && !connectSampleQuality(samples, quality, samplePage, &app))
            {
                qCritical() << "SAMPLE_QC_BRIDGE_SETUP_FAILED";
                QTimer::singleShot(0, &app, [&app]() { app.exit(3); });
            }
        }
        if (calibration)
        {
            auto *page = new CalibrationPage(calibration);
            shell->attachCalibrationPage(page);
            const QString calibrationDir = QDir(dataDir).filePath(QStringLiteral("calibration"));
            const QString catalogDatabase = QDir(catalogDir).filePath(QStringLiteral("catalog.db"));
            const auto initializeCalibration = [calibration, calibrationDir, catalogDatabase]()
            { calibration->initialize(calibrationDir, catalogDatabase); };
            // 所有共用catalog建表提交后再打开校准引用保护连接，避免并行schema升级。
            if (samples) QObject::connect(samples, &SampleStore::ready, calibration, initializeCalibration, Qt::QueuedConnection);
            else QObject::connect(reagents, &ReagentStore::ready, calibration, initializeCalibration, Qt::QueuedConnection);
            QObject::connect(page, &CalibrationPage::operationFinished, &app,
                             [&, page](const QString &operation, bool success)
            {
                if (operation == QStringLiteral("load") && success && page->isReady() && !page->isBusy())
                { calibrationReady = true; reportReady(); }
                else if (!success && !calibrationReady) app.exit(3);
            });
            QObject::connect(calibration, &CalibrationStore::error, &app,
                             [&](qint64, const QString &operation, const QString &message)
            {
                qWarning().noquote() << "CALIBRATION_ERROR" << operation << message;
                if (!calibrationReady) app.exit(3);
            });
            QObject::connect(calibration, &CalibrationStore::closed, calibration,
                             []() { qInfo() << "CALIBRATION_STORE_CLOSED"; });
        }
        const bool seedDemo = parser.isSet(QStringLiteral("seed-catalog-demo"));
        QObject::connect(&projectThread,&QThread::started,projects,
                         [projects,catalogDir,seedDemo]() { projects->initialize(catalogDir,seedDemo); });
        // All threads run before initialization. The catalog creator commits its
        // schema/marker first, then the reagent worker may open its own connection.
        QObject::connect(projects,&ProjectsStore::ready,reagents,
                         [reagents,catalogDir](const QString &) { reagents->initialize(catalogDir); },Qt::QueuedConnection);
        QObject::connect(projectPage,&ProjectPage::operationFinished,&app,[&,projectPage](const QString &operation,bool success)
        {
            if (operation==QStringLiteral("load") && success && !projectPage->isBusy())
            { projectReady=true;reportReady(); }
        });
        QObject::connect(reagentPage,&ReagentsPage::readyChanged,&app,[&](bool ready)
        { if (ready) { reagentReady=true;reportReady(); } });
        QObject::connect(projects,&ProjectsStore::error,&app,[&](qint64,const QString &operation,const QString &message)
        {
            qWarning().noquote()<<"PROJECT_ERROR"<<operation<<message;
            if (operation==QStringLiteral("initialize") || !projectReady) app.exit(3);
        });
        QObject::connect(reagents,&ReagentStore::error,&app,[&](const QString &operation,const QString &message)
        {
            qWarning().noquote()<<"REAGENT_ERROR"<<operation<<message;
            if (!reagentReady) app.exit(3);
        });
        QObject::connect(projects,&ProjectsStore::closed,projects,[]() { qInfo()<<"PROJECT_STORE_CLOSED"; });
        QObject::connect(reagents,&ReagentStore::closed,reagents,[]() { qInfo()<<"REAGENT_STORE_CLOSED"; });
    }
    if (maintenance)
    {
        auto *page = new InstrumentPage(maintenance);
        shell->attachInstrumentPage(page);
        const QString instrumentDir = QDir(dataDir).filePath(QStringLiteral("instrument"));
        QObject::connect(&maintenanceThread,&QThread::started,maintenance,
                         [maintenance,instrumentDir]() { maintenance->initialize(instrumentDir); });
        QObject::connect(page,&InstrumentPage::operationFinished,&app,
                         [&](const QString &operation,bool success)
        {
            if (operation == QStringLiteral("load") && success)
            {
                if (!instrumentReady)
                {
                    instrumentReady = true;
                    qInfo() << "INSTRUMENT_INITIAL_MONTH_READY";
                }
                reportReady();
            }
            else if (!success && !instrumentReady) app.exit(3);
        });
        QObject::connect(maintenance,&MaintenanceStore::error,&app,
                         [&](const QString &operation,const QString &message)
        {
            qWarning().noquote() << "MAINTENANCE_ERROR" << operation << message;
            if (!instrumentReady) app.exit(3);
        });
        QObject::connect(maintenance,&MaintenanceStore::closed,maintenance,
                         []() { qInfo() << "MAINTENANCE_STORE_CLOSED"; });
    }
    if (printer)
    {
        auto *page = new frmPrinterSetting(printer);
        shell->attachPrinterPage(page);
        const QString settingsDir = QDir(dataDir).filePath(QStringLiteral("settings"));
        QObject::connect(&printerThread, &QThread::started, printer,
                         [printer, settingsDir]() { printer->initialize(settingsDir); });
        QObject::connect(page, &frmPrinterSetting::operationFinished, &app,
                         [&](const QString &, bool success)
        {
            if (!printerReady && success)
            {
                printerReady = true;
                reportReady();
            }
            else if (!printerReady) app.exit(3);
        });
        QObject::connect(printer, &PrinterSettingsStore::closed, printer,
                         []() { qInfo() << "PRINTER_SETTINGS_CLOSED"; });
    }
    if (printer && shell->historyReports())
    {
        // Bind only committed Store values; draft settings never select a job device.
        auto *printerReports = new PrinterReportShell(printer, &app);
        printerReports->setReportController(shell->historyReports());
        QObject::connect(printerReports, &PrinterReportShell::applicationError, &app,
                         [](const QString &message)
        { qWarning().noquote() << "PRINTER_REPORT_BINDING_ERROR" << message; });
        qInfo() << "PRINTER_REPORT_BINDING_ENABLED committed_config_only=true automatic_submit=false";
    }
    if (lis)
    {
        auto *page = new frmLisSetting(lis);
        shell->attachLisPage(page);
        const QString settingsDir = QDir(dataDir).filePath(QStringLiteral("settings"));
        QObject::connect(&lisThread, &QThread::started, lis,
                         [lis, settingsDir]() { lis->initialize(settingsDir); });
        QObject::connect(page, &frmLisSetting::operationFinished, &app,
                         [&](const QString &, bool success)
        {
            if (!lisReady && success) { lisReady = true; reportReady(); }
            else if (!lisReady) app.exit(3);
        });
        QObject::connect(lis, &LisSettingsStore::closed, lis,
                         []() { qInfo() << "LIS_SETTINGS_CLOSED"; });
    }
    ShellViewport viewport(shell, viewportSize, withAccounts, withHistory);
    if (withCatalog) viewport.setWindowTitle(QStringLiteral("SN100 · 统一脱机 · 项目/单位、试剂、医院、账户、历史/报告"));
    if (withSamples) viewport.setWindowTitle(QStringLiteral("SN100 · 统一脱机 · 样本/任务、项目/单位、试剂、医院、账户、历史/报告"));
    if (withStatusReplay) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 离线状态回放"));
    if (withInstrument) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 仪器操作/本地维护"));
    if (withPasswordLogin) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 本地密码登录"));
    if (withPrinterSettings) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 本地打印设置"));
    if (withLisSettings) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · LIS本地设置"));
    if (withQualityControl) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 质控本地设置"));
    if (withCalibration) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 校准本地原始点"));
    if (withLocalResults) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 本地原值结果库"));
    QObject::connect(&statusReplayTimer, &QTimer::timeout, &app, [&]()
    {
        // 250ms只是回放展示节奏；所有协议时间严格取文件记录的at_ms。
        const bool stepped = statusReplay.step();
        const auto &state = statusReplay.current();
        for (const QByteArray &frame : state.transmitted)
            qInfo().noquote() << "STATUS_REPLAY_PLANNED_TX" << state.sourceTimeMs << frame.toHex(' ').toUpper();
        if (!state.received.isEmpty())
            qInfo().noquote() << "STATUS_REPLAY_FILE_RX" << state.sourceTimeMs << state.received.toHex(' ').toUpper();
        for (const QString &notice : state.notices)
            qInfo().noquote() << "STATUS_REPLAY_NOTICE" << state.sourceTimeMs << notice;
        shell->setStatusReplayState(state);
        qInfo() << "STATUS_REPLAY_STEP" << state.eventIndex << state.action << state.linkState
                << "fresh=" << state.fresh << "finished=" << state.finished;
        if (!stepped || state.finished) statusReplayTimer.stop();
    });
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
    if (alarmHistory) alarmHistoryThread.start();
    if (accounts) accountThread.start();
    if (history) historyThread.start();
    if (samples) sampleThread.start();
    if (maintenance) maintenanceThread.start();
    if (printer) printerThread.start();
    if (lis) lisThread.start();
    if (quality) qualityThread.start();
    if (calibration) calibrationThread.start();
    if (withCatalog) { reagentThread.start();projectThread.start(); }
    qInfo().noquote() << "START mode=offline module=" << ((withCalibration || withQualityControl) ? "LOCAL_BUSINESS_20261025" : withSamples ? "O08" : withCatalog ? "O07" : withHistory ? "O03-4" : withAccounts ? "O03-2" : "O03-1") << "Qt=" << qVersion()
                      << "platform=" << QGuiApplication::platformName()
                      << "logical_canvas=1920x1080 viewport=" << viewportSize << "data_dir=" << dataDir;
    int result = app.exec();
    statusReplayTimer.stop();
    if (withStatusReplay)
    {
        statusReplay.stop();
        qInfo() << "STATUS_REPLAY_STOPPED" << !statusReplayTimer.isActive();
    }
    if (withStatusLoopback)
    {
        const bool stopped = statusLoopback.shutdownAndWait();
        qInfo() << "STATUS_LOOPBACK_STOPPED" << stopped;
        if (!stopped) result = 3;
    }
    // Normal shell shutdown is accepted only after AlarmHistoryHost::viewClosed.
    // At boot all init-failure exits happen before the entry is enabled.
    if (alarmHistory) QMetaObject::invokeMethod(alarmHistory,"close",Qt::BlockingQueuedConnection);
    if (calibration) QMetaObject::invokeMethod(calibration,"close",Qt::BlockingQueuedConnection);
    if (quality) QMetaObject::invokeMethod(quality,"close",Qt::BlockingQueuedConnection);
    if (lis) QMetaObject::invokeMethod(lis,"close",Qt::BlockingQueuedConnection);
    if (printer) QMetaObject::invokeMethod(printer,"close",Qt::BlockingQueuedConnection);
    if (maintenance) QMetaObject::invokeMethod(maintenance,"close",Qt::BlockingQueuedConnection);
    if (samples) QMetaObject::invokeMethod(samples,"close",Qt::BlockingQueuedConnection);
    if (reagents) QMetaObject::invokeMethod(reagents,"close",Qt::BlockingQueuedConnection);
    if (projects) QMetaObject::invokeMethod(projects,"close",Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    if (accounts) QMetaObject::invokeMethod(accounts, "close", Qt::BlockingQueuedConnection);
    if (history) QMetaObject::invokeMethod(history, "close", Qt::BlockingQueuedConnection);
    databaseThread.quit();
    if (alarmHistory) alarmHistoryThread.quit();
    if (accounts) accountThread.quit();
    if (history) historyThread.quit();
    if (samples) sampleThread.quit();
    if (maintenance) maintenanceThread.quit();
    if (printer) printerThread.quit();
    if (lis) lisThread.quit();
    if (quality) qualityThread.quit();
    if (calibration) calibrationThread.quit();
    if (withCatalog) { reagentThread.quit();projectThread.quit(); }
    databaseThread.wait();
    if (alarmHistory)
    {
        alarmHistoryThread.wait();
        qInfo() << "ALARM_HISTORY_WORKER_STOPPED" << !alarmHistoryThread.isRunning();
    }
    if (accounts) accountThread.wait();
    if (history) historyThread.wait();
    if (samples) sampleThread.wait();
    if (maintenance) maintenanceThread.wait();
    if (printer) printerThread.wait();
    if (lis) lisThread.wait();
    if (quality) qualityThread.wait();
    if (calibration) calibrationThread.wait();
    if (withCatalog) { reagentThread.wait();projectThread.wait(); }
    if (accounts || history) qInfo() << "THREADS_STOPPED" << (!databaseThread.isRunning() && !accountThread.isRunning() && !historyThread.isRunning()
        && !projectThread.isRunning() && !reagentThread.isRunning() && !sampleThread.isRunning() && !maintenanceThread.isRunning() && !printerThread.isRunning() && !lisThread.isRunning() && !qualityThread.isRunning() && !calibrationThread.isRunning() && !statusLoopback.isRunning() && !alarmHistoryThread.isRunning());
    else qInfo() << "THREAD_STOPPED" << !databaseThread.isRunning();
    qInfo() << "EXIT code=" << result;
    return result;
}
