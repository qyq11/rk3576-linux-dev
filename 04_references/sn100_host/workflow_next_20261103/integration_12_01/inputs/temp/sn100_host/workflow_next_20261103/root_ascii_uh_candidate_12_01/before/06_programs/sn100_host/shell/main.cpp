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
#include <QToolTip>
#include <QVBoxLayout>
#include <memory>
#include <limits>
#include <QMessageBox>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "frmmain.h"
#include "sample_qc_shell.h"
#include "printer_report_shell.h"
#include "shutdown_request.h"
#include "startup_splash.h"
#include "data/hospital_store.h"
#include "data/account_store.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"
#include "history/history_page.h"
#include "history/history_store.h"
#include "projects/project_page.h"
#include "../original_files/original_ini.h"
#include "../original_files/theme_configuration.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include "protocol/status_replay.h"
#include "protocol/status_loopback.h"
#include "instrument/instrument_page.h"
#include "instrument/device_control.h"
#include "instrument/maintenance_store.h"
#include "instrument/original_channel_receiver.h"
#include "data/printer_settings_store.h"
#include "ui/frmprintersetting.h"
#include "data/lis_settings_store.h"
#include "lis_transport/lis_connection_controller.h"
#include "ui/frmlissetting.h"
#include "quality_control/quality_control_page.h"
#include "quality_control/qc_store.h"
#include "calibration/calibration_page.h"
#include "ui/language_setting_page.h"
#include "alarm_history/alarm_history_host.h"
#include "alarm_history/original_alarm_receiver.h"

namespace
{
// 原checkOnly登记固定名称；首个实例持有到退出，允许多开也先尝试登记。
class OriginalSingleInstance
{
public:
    enum class Result { Registered, AlreadyRegistered, EnvironmentError };
    OriginalSingleInstance() = default;
    OriginalSingleInstance(const OriginalSingleInstance &) = delete;
    OriginalSingleInstance &operator=(const OriginalSingleInstance &) = delete;
    ~OriginalSingleInstance()
    {
#if defined(Q_OS_WIN)
        if (handle_) CloseHandle(handle_);
#elif defined(Q_OS_LINUX)
        if (descriptor_>=0) ::close(descriptor_);
#endif
    }

    Result registerProcess(QString *message)
    {
#if defined(Q_OS_WIN)
        HANDLE created=CreateMutexW(nullptr,FALSE,L"fortest_abc123");
        const DWORD error=GetLastError();
        if (!created)
        {
            if (message) *message=QStringLiteral("无法建立单实例登记，Windows错误码：%1").arg(error);
            return Result::EnvironmentError;
        }
        if (error==ERROR_ALREADY_EXISTS)
        {
            CloseHandle(created);
            return Result::AlreadyRegistered;
        }
        handle_=created;
        return Result::Registered;
#elif defined(Q_OS_LINUX)
        // 固定同一Linux有效用户编号（UID）及同一/tmp文件系统；不随程序、配置或XDG_RUNTIME_DIR变化。
        // 这比Windows按登录会话隔离更严格，不宣称跨用户或多图形会话完全等价。
        const uid_t user=::geteuid();
        const QByteArray directory=QByteArrayLiteral("/tmp/sn100-single-instance-")+QByteArray::number(qulonglong(user));
        if (::mkdir(directory.constData(),0700)<0 && errno!=EEXIST)
        {
            const int error=errno;
            return systemError(message,QStringLiteral("无法建立单实例登记目录"),error);
        }
        const int directoryDescriptor=::open(directory.constData(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if (directoryDescriptor<0)
        {
            const int error=errno;
            return systemError(message,QStringLiteral("无法打开单实例登记目录"),error);
        }
        struct stat directoryInfo;
        if (::fstat(directoryDescriptor,&directoryInfo)<0)
        {
            const int error=errno; ::close(directoryDescriptor);
            return systemError(message,QStringLiteral("无法核对单实例登记目录"),error);
        }
        if (!S_ISDIR(directoryInfo.st_mode) || directoryInfo.st_uid!=user || (directoryInfo.st_mode&0777)!=0700)
        {
            ::close(directoryDescriptor);
            if (message) *message=QStringLiteral("单实例登记目录的类型、所有者或0700权限不符，未接管或清理该目录。");
            return Result::EnvironmentError;
        }
        const char *name="fortest_abc123.lock";
        const int created=::openat(directoryDescriptor,name,O_CREAT|O_RDWR|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC,0600);
        if (created<0)
        {
            const int error=errno; ::close(directoryDescriptor);
            return systemError(message,QStringLiteral("无法打开单实例登记文件"),error);
        }
        struct stat info;
        if (::fstat(created,&info)<0)
        {
            const int error=errno; ::close(created); ::close(directoryDescriptor);
            return systemError(message,QStringLiteral("无法核对单实例登记文件"),error);
        }
        if (!S_ISREG(info.st_mode) || info.st_uid!=user || info.st_nlink!=1 || (info.st_mode&0777)!=0600)
        {
            ::close(created); ::close(directoryDescriptor);
            if (message) *message=QStringLiteral("单实例登记文件的类型、所有者、链接数或0600权限不符，未接管或清理旧文件。");
            return Result::EnvironmentError;
        }
        int locked;
        do { locked=::flock(created,LOCK_EX|LOCK_NB); } while (locked<0 && errno==EINTR);
        const int lockError=locked<0 ? errno : 0;
        struct stat namedInfo;
        const int namedStatus=::fstatat(directoryDescriptor,name,&namedInfo,AT_SYMLINK_NOFOLLOW);
        const int namedError=namedStatus<0 ? errno : 0;
        const bool sameFile=namedStatus==0 &&
            S_ISREG(namedInfo.st_mode) && namedInfo.st_dev==info.st_dev && namedInfo.st_ino==info.st_ino &&
            namedInfo.st_uid==user && namedInfo.st_nlink==1 && (namedInfo.st_mode&0777)==0600;
        ::close(directoryDescriptor);
        if (namedStatus<0)
        {
            ::close(created);
            return systemError(message,QStringLiteral("无法复核单实例登记名称"),namedError);
        }
        if (!sameFile)
        {
            ::close(created);
            if (message) *message=QStringLiteral("单实例登记文件在检查期间已变化，未把其他文件当作当前登记，也未清理文件。");
            return Result::EnvironmentError;
        }
        if (locked==0)
        {
            descriptor_=created; // 退出时仅关闭自己的描述符；不删除文件，崩溃由内核释放锁。
            return Result::Registered;
        }
        ::close(created);
        if (lockError==EWOULDBLOCK || lockError==EAGAIN) return Result::AlreadyRegistered;
        return systemError(message,QStringLiteral("无法取得单实例登记锁"),lockError);
#else
        if (message) *message=QStringLiteral("当前平台没有已接入的单实例登记实现，未按已登记或正常首次启动放行。");
        return Result::EnvironmentError;
#endif
    }

private:
#if defined(Q_OS_WIN)
    HANDLE handle_=nullptr;
#elif defined(Q_OS_LINUX)
    int descriptor_=-1;
    static Result systemError(QString *message,const QString &operation,int error)
    {
        if (message) *message=operation+QStringLiteral("：")+QString::fromLocal8Bit(std::strerror(error));
        return Result::EnvironmentError;
    }
#endif
};
}

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

    void setOriginalChannelReceiver(OriginalChannelReceiver *receiver) { originalChannelReceiver_ = receiver; }
    void finishOriginalChannelStop()
    {
        if (!originalChannelClosePending_) return;
        originalChannelClosePending_ = false;
        close();
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
        // 原草稿/忙态守卫已接受退出，才撤销Uh；等待回执期间仍处理GUI事件。
        if (originalChannelReceiver_ && !originalChannelReceiver_->isStopped())
        {
            originalChannelClosePending_ = true;
            originalChannelReceiver_->requestStop();
            event->ignore();
            return;
        }
        event->accept();
    }

private:
    frmMain *shell_;
    QPointer<OriginalChannelReceiver> originalChannelReceiver_;
    bool originalChannelClosePending_ = false;
};

int main(int argc, char *argv[])
{
    // 先声明空登记对象，使其晚于QApplication析构；实际取锁仍在配置读取后。
    OriginalSingleInstance singleInstance;
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
    parser.addOption({QStringLiteral("lis-settings"), QStringLiteral("原LIS设置；原资料按原启动/保存规则打开TCP，串口待接入")});
    parser.addOption({QStringLiteral("lis-raw-inbox"), QStringLiteral("离线原报文导入查看；需要--accounts和--lis-settings，未映射、不应用检测任务")});
    parser.addOption({QStringLiteral("quality-control"), QStringLiteral("原质控品/项目/批次本地页面；需要--catalog，不执行检测或失控判定")});
    parser.addOption({QStringLiteral("calibration"), QStringLiteral("原校准记录/原始点本地页面；需要--catalog，不拟合、审核或应用曲线")});
    parser.addOption({QStringLiteral("alarm-history"), QStringLiteral("仅启用本地既有告警历史入口；实时告警和设备动作未知")});
    parser.addOption({QStringLiteral("alarm-history-db"), QStringLiteral("显式已批准匿名/迁移只读历史文件绝对路径；不创建文件"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("history"), QStringLiteral("O03-3：接入匿名历史查询；使用独立history子目录")});
    parser.addOption({QStringLiteral("local-results"), QStringLiteral("启用空白本地原值结果库；需要--history和--catalog，未接检测调度、单位映射或文件输出")});
    parser.addOption({QStringLiteral("catalog"), QStringLiteral("O07：接入本地项目/单位及试剂基础；使用catalog子目录，需要--accounts")});
    parser.addOption({QStringLiteral("original-db-root"), QStringLiteral("直接使用原bin分类的工作副本根；项目、医院、账户、历史、样本/试剂、维护和已启用设置沿用原资料，需要--catalog"), QStringLiteral("directory")});
    parser.addOption({QStringLiteral("start-project-page"), QStringLiteral("本次启动就绪后显示分析项目页；需要--catalog")});
    parser.addOption({QStringLiteral("samples"), QStringLiteral("O08：接入本地样本预设/任务；需要--catalog，无真实投递或执行")});
    parser.addOption({QStringLiteral("instrument"), QStringLiteral("仪器操作原版页面及本地维护记录；需要--accounts，设备动作禁用")});
    parser.addOption({QStringLiteral("instrument-control"), QStringLiteral("启用现有主控连接；需要--instrument，并指定原资料根或--instrument-config")});
    parser.addOption({QStringLiteral("instrument-config"), QStringLiteral("显式主控连接配置的绝对路径；读取tcpmain地址/端口和theme/DriverModel，保留原任务与状态检查"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("theme-config"), QStringLiteral("原theme_config.ini的绝对路径；应用机型、孔位数量、报告方式及原颜色"), QStringLiteral("path")});
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
    bool withPasswordLogin = parser.isSet(QStringLiteral("password-login"));
    const bool withPrinterSettings = parser.isSet(QStringLiteral("printer-settings"));
    const bool withLisSettings = parser.isSet(QStringLiteral("lis-settings"));
    const bool withLisRawInbox = parser.isSet(QStringLiteral("lis-raw-inbox"));
    if (withLisRawInbox && (!withAccounts || !withLisSettings))
    {
        qCritical() << "--lis-raw-inbox requires --accounts and --lis-settings";
        return 2;
    }
    const bool withQualityControl = parser.isSet(QStringLiteral("quality-control"));
    const bool withCalibration = parser.isSet(QStringLiteral("calibration"));
    const bool withAlarmHistory = parser.isSet(QStringLiteral("alarm-history"));
    QString alarmHistoryPath = parser.isSet(QStringLiteral("alarm-history-db"))
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
    const bool withOriginalProjects = parser.isSet(QStringLiteral("original-db-root"));
    const QString originalDbRoot = withOriginalProjects
        ? QDir::cleanPath(parser.value(QStringLiteral("original-db-root"))) : QString();
    const bool sharedOriginalRoot = withOriginalProjects && originalDbRoot == dataDir;
    if (withOriginalProjects && !parser.isSet(QStringLiteral("alarm-history-db")))
        alarmHistoryPath = QDir(originalDbRoot).filePath(QStringLiteral("db/userSet/abnormal_Log.db"));
    if (withOriginalProjects && withAccounts) withPasswordLogin = true;
    if (withOriginalProjects && (parser.isSet(QStringLiteral("test-session")) ||
        parser.isSet(QStringLiteral("seed-anonymous")) || parser.isSet(QStringLiteral("test-secret-file"))))
    {
        qCritical() << "Original accounts use original configuration or password login; simulated sessions and account seeding are unavailable";
        return 2;
    }
    if ((withOriginalProjects && (!withCatalog || !QDir::isAbsolutePath(originalDbRoot) ||
         originalDbRoot.contains(QChar::Null) || originalDbRoot.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive) ||
         (sharedOriginalRoot && !withAccounts && !withHistory) ||
         (!sharedOriginalRoot && (originalDbRoot.startsWith(dataDir + QLatin1Char('/')) ||
          dataDir.startsWith(originalDbRoot + QLatin1Char('/')))) || parser.isSet(QStringLiteral("seed-catalog-demo")))) ||
        (parser.isSet(QStringLiteral("start-project-page")) && !withCatalog))
    {
        qCritical() << "Original project root must be absolute and use --catalog without seed-catalog-demo; "
                       "a shared data-dir requires --accounts or --history, separate roots cannot contain each other";
        return 2;
    }
    const bool withLocalResults = parser.isSet(QStringLiteral("local-results"));
    if (withLocalResults && (withOriginalProjects || !withHistory || !withCatalog))
    {
        qCritical() << "--local-results requires --history and --catalog without --original-db-root";
        return 2;
    }
    const bool withSamples = parser.isSet(QStringLiteral("samples"));
    const bool withInstrument = parser.isSet(QStringLiteral("instrument"));
    const bool withDeviceControl = parser.isSet(QStringLiteral("instrument-control"));
    OriginalRuntime::RuntimeSettings originalRuntimeConfiguration;
    if (withOriginalProjects)
    {
        QString message;
        if (!OriginalRuntime::loadRuntimeSettings(originalDbRoot,&originalRuntimeConfiguration,&message))
        {
            qCritical().noquote()<<"ORIGINAL_RUNTIME_CONFIGURATION_INVALID"<<message;
            return 2;
        }
    }
    // 原main先读取多开开关，再调用checkOnly；登记必须早于业务线程及设备连接。
    QString singleInstanceError;
    const auto instanceResult=singleInstance.registerProcess(&singleInstanceError);
    if (instanceResult==OriginalSingleInstance::Result::EnvironmentError)
    {
        qCritical().noquote()<<"SINGLE_INSTANCE_ENVIRONMENT_ERROR"<<singleInstanceError;
        QMessageBox::critical(nullptr,QStringLiteral("启动失败"),singleInstanceError);
        return 2;
    }
    if (instanceResult==OriginalSingleInstance::Result::AlreadyRegistered &&
        !originalRuntimeConfiguration.debug.allowMultipleInstances)
    {
        qWarning()<<"SINGLE_INSTANCE_ALREADY_RUNNING";
        QMessageBox::critical(nullptr,QStringLiteral("错误"),QStringLiteral("程序已经在运行!"));
        return 0; // 保留原已有实例时的退出值，不结束、唤醒或聚焦旧进程。
    }
    qInfo()<<"SINGLE_INSTANCE_CHECKED registered="<<(instanceResult==OriginalSingleInstance::Result::Registered)
           <<"multiple_allowed="<<originalRuntimeConfiguration.debug.allowMultipleInstances;

    OriginalRuntime::SystemProfile originalAlarmProfile;
    if (withOriginalProjects && withDeviceControl)
    {
        QString message;
        if (!OriginalRuntime::loadSystemProfile(originalDbRoot, &originalAlarmProfile, &message))
        {
            qCritical().noquote() << "ORIGINAL_ALARM_PROFILE_INVALID" << message;
            return 2;
        }
    }
    const bool withThemeConfiguration = parser.isSet(QStringLiteral("theme-config"));
    OriginalRuntime::ThemeOptions themeConfiguration;
    if (withThemeConfiguration)
    {
        QString message;
        if (!loadThemeConfiguration(parser.value(QStringLiteral("theme-config")), &themeConfiguration, &message))
        {
            qCritical().noquote() << "THEME_CONFIGURATION_INVALID" << message;
            return 2;
        }
        if (themeConfiguration.reportTableType!=1 && !withOriginalProjects)
        {
            qCritical() << "THEME_CONFIGURATION_INVALID current_local_report_requires_ReportTableType_1=true";
            return 2;
        }
        qInfo().noquote() << "THEME_CONFIGURATION_LOADED model=" << themeConfiguration.driverModel
            << "reagents=" << themeConfiguration.reagentPositions << "sample_holes=" << themeConfiguration.sampleHoles
            << "emergency_holes=" << themeConfiguration.emergencyHoles << "channels=" << themeConfiguration.testChannels
            << "report_table_type=" << themeConfiguration.reportTableType;
    }
    const QString deviceConfigurationFile = parser.isSet(QStringLiteral("instrument-config"))
        ? QDir::cleanPath(parser.value(QStringLiteral("instrument-config"))) : QString();
    QString configuredDeviceModel;
    if (parser.isSet(QStringLiteral("instrument-config")))
    {
        OriginalIni::Settings settings;
        QString message;
        const QFileInfo file(deviceConfigurationFile);
        if (!withDeviceControl || !file.isAbsolute() || !file.isFile() || file.isSymLink() ||
            !OriginalIni::read(deviceConfigurationFile, &settings, &message))
        {
            qCritical() << "INSTRUMENT_CONFIGURATION_INVALID absolute_regular_ini_required=true";
            return 2;
        }
        configuredDeviceModel = settings.values.value(QStringLiteral("theme/DriverModel")).toString().trimmed();
        if (withThemeConfiguration) configuredDeviceModel=themeConfiguration.driverModel;
        if (configuredDeviceModel.isEmpty() || configuredDeviceModel.size() > 64 ||
            configuredDeviceModel.contains(QChar::LineFeed) || configuredDeviceModel.contains(QChar::CarriageReturn))
        {
            qCritical() << "INSTRUMENT_CONFIGURATION_INVALID theme_DriverModel_required=true";
            return 2;
        }
    }
    if (withDeviceControl && (!withInstrument || (!withOriginalProjects && deviceConfigurationFile.isEmpty()) ||
        parser.isSet(QStringLiteral("status-replay")) || parser.isSet(QStringLiteral("status-loopback"))))
    {
        qCritical() << "--instrument-control requires --instrument and an original root or explicit --instrument-config without replay/loopback";
        return 2;
    }
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
    if (reagents && withThemeConfiguration) reagents->configureLayout(themeConfiguration.reagentPositions);
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
    if (samples && withThemeConfiguration) samples->configureLayout(themeConfiguration.sampleHoles,themeConfiguration.emergencyHoles);
    if (samples && withOriginalProjects &&
        !samples->configureOriginalDefaultContainer(originalRuntimeConfiguration.user.containerIsTube))
    {
        qCritical() << "原样本新孔默认容器未能在移入工作线程前配置。";
        return 3;
    }
    if (samples && reagents && withOriginalProjects &&
        !samples->configureOriginalReagentMetadata(reagents->originalPageMetadata()))
    {
        qCritical() << "原样本未能绑定真实试剂盘面批号来源。";
        return 3;
    }
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
    // Uh使用独立后台，只有下方本片接线能退出它，不能借维护线程的closed信号。
    QThread originalChannelThread;
    originalChannelThread.setObjectName(QStringLiteral("OriginalChannelDatabase"));
    QPointer<OriginalChannelStore> originalChannelStore;
    OriginalChannelReceiver *originalChannelReceiver = nullptr;
    bool originalChannelFailure = false;
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
    Sn100Lis::LisConnectionController *lisConnection = nullptr;
    if (lis)
    {
        lis->moveToThread(&lisThread);
        QObject::connect(&lisThread, &QThread::finished, lis, &QObject::deleteLater);
    }
    auto *shell = new frmMain(store, nullptr, accounts);
    shell->setProperty("deviceConfigurationFile", deviceConfigurationFile);
    shell->setProperty("configuredDeviceModel", configuredDeviceModel);
    if (withOriginalProjects) shell->setProperty("originalDataRoot", originalDbRoot);
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
    // 原模式的医院、打印和LIS共用原根config.ini；存储层只修改各自拥有的键。
    const QString hospitalDir = withOriginalProjects ? originalDbRoot
        : (withAccounts || withHistory || sharedOriginalRoot)
            ? QDir(dataDir).filePath(QStringLiteral("hospital")) : dataDir;
    shell->setDataDirectory(hospitalDir);
    if (withDeviceControl)
    {
        shell->enableDeviceControl(true);
        if (auto *control=shell->deviceControl())
        {
            // 与原tcpmain共用调试配置；该配置不创建第二条主控连接。
            if (withOriginalProjects)
                control->setReconnectOptions(originalRuntimeConfiguration.debug.reconnect,
                                             originalRuntimeConfiguration.debug.reconnectMilliseconds);
            QObject::connect(&app,&QCoreApplication::aboutToQuit,control,&DeviceControl::close);
        }
    }
    QThread originalAlarmThread;
    originalAlarmThread.setObjectName(QStringLiteral("OriginalAlarmWriter"));
    auto *originalAlarmWriter = withOriginalProjects && withDeviceControl ? new AlarmHistory::Store : nullptr;
    AlarmHistory::OriginalAlarmReceiver *originalAlarmReceiver = nullptr;
    if (originalAlarmWriter)
    {
        originalAlarmWriter->moveToThread(&originalAlarmThread);
        QObject::connect(&originalAlarmThread, &QThread::finished, originalAlarmWriter, &QObject::deleteLater);
        originalAlarmReceiver = new AlarmHistory::OriginalAlarmReceiver(originalAlarmWriter, originalAlarmProfile.alarms, originalDbRoot, shell);
        shell->enableOriginalAlarmReceiver(originalAlarmReceiver);
        if (alarmHistory && !shell->enableOriginalCurrentAlarms(originalAlarmProfile))
        {
            qCritical() << "ORIGINAL_CURRENT_ALARM_ENABLE_FAILED";
            return 3;
        }
        QObject::connect(&originalAlarmThread, &QThread::started, originalAlarmReceiver,
            [originalAlarmReceiver, originalDbRoot]() { originalAlarmReceiver->openOriginal(originalDbRoot); }, Qt::QueuedConnection);
    }
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
    bool currentFormatRefreshPending = false; // Consumed after the writer Page reloads its actual committed source.
    StartupSplash *startupSplash = nullptr; // 由实际显示宿主持有，数据就绪后停止动画。
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
            if (startupSplash) startupSplash->finish();
            qInfo() << "SHELL_READY";
            if (withOriginalProjects)
            {
                qInfo() << "ORIGINAL_PROJECTS_READY direct_original_files=true instrument=" << withDeviceControl;
                qInfo().noquote() << "ORIGINAL_PROJECTS_SCOPE 已启用页面读取原项目、医院、账户、历史、样本/试剂、维护、质控/校准和设置；质控/校准任务、算法及其余配置继续逐项接入。";
                qInfo() << "ORIGINAL_SETTINGS_READY hospital=true printer=" << withPrinterSettings
                        << "lis=" << withLisSettings << "network=" << withDeviceControl;
            }
            if (withLocalResults) qInfo() << "LOCAL_RESULTS_READY empty_profile_allowed=true scheduler=false current_config_format="
                << !withOriginalProjects << "measurement_unit_snapshot=false file_output=false";
            if (parser.isSet(QStringLiteral("start-project-page"))) shell->selectMainPage(5);
            if (alarmHistory)
            {
                shell->setAlarmHistoryAvailable(alarmHistoryThread.isRunning());
                qInfo() << "ALARM_HISTORY_ENABLED local_history_only=true source_opened=false live_alarms_unknown=true";
            }
            if (withPrinterSettings) qInfo() << "PRINTER_SETTINGS_READY local_only=true print_job=false";
            if (withLisSettings) qInfo() << "LIS_SETTINGS_READY"
                << "original_connection_policy=" << withOriginalProjects
                << "business_download_upload=false serial=false";
            if (withLisRawInbox)
            {
                // 所有启动失败分支结束后才启用异步辅助窗口；不在aboutToQuit里关线程。
                if (!shell->enableLisRawInbox(QDir(dataDir).filePath(QStringLiteral("lis_raw_inbox"))))
                {
                    qCritical() << "LIS_RAW_INBOX_ENTRY_SETUP_FAILED";
                    app.exit(3);
                    return;
                }
                QTimer::singleShot(0, shell, &frmMain::openLisRawInbox);
                qInfo() << "LIS_RAW_INBOX_ENABLED local_only=true network=false task_application=false";
            }
            if (withQualityControl) qInfo() << "QUALITY_CONTROL_READY local_only=true clinical_rules=false";
            if (withCalibration) qInfo() << "CALIBRATION_READY local_only=true fitting=false applied=false";
            if (withPasswordLogin) qInfo() << "PASSWORD_LOGIN_ENABLED original_profile=" << withOriginalProjects
                << "configured_or_password_session=true";
            // 原版开启登录时在启动后自动弹登录卡；不能只启用头像按钮。
            if (withOriginalProjects && withPasswordLogin && originalRuntimeConfiguration.debug.loginOpen)
                shell->requestStartupLogin();
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
                         [accounts, accountDir, bootstrapPassword, withOriginalProjects, originalDbRoot]()
        {
            if (withOriginalProjects) accounts->initializeOriginal(originalDbRoot);
            else accounts->initialize(accountDir, bootstrapPassword);
        });
        QObject::connect(accounts, &AccountStore::ready, accounts, [&, accounts, actor, state, withOriginalProjects, originalDbRoot]()
        {
            if (!withOriginalProjects) { accounts->setSession(actor, state == QStringLiteral("ready")); return; }
            OriginalIni::Settings debug; QString message;
            if (!OriginalIni::read(QDir(originalDbRoot).filePath(QStringLiteral("config_Debug.ini")), &debug, &message))
            {
                qWarning().noquote() << "ORIGINAL_LOGIN_CONFIGURATION_ERROR" << message;
                QTimer::singleShot(0, &app, [&app]() { app.exit(3); }); return;
            }
            if (!debug.values.value(QStringLiteral("login/open"), true).toBool())
                accounts->applyOriginalConfiguredSession(0); // 0只用于启动配置身份，弹窗请求号从1开始。
            else
                QMetaObject::invokeMethod(&app, [&]() { sessionObserved = true; reportReady(); }, Qt::QueuedConnection);
        });
        QObject::connect(accounts, &AccountStore::loginFinished, &app,
                         [&](quint64 request, bool accepted, bool, const QString &error, const QString &)
        {
            if (!withOriginalProjects || request != 0) return;
            if (!accepted) { qWarning().noquote() << "ORIGINAL_CONFIGURED_SESSION_ERROR" << error; app.exit(3); return; }
            sessionObserved = true; reportReady();
        });
        QObject::connect(shell, &frmMain::sessionChanged, &app, [&,actor](AccountSession session)
        {
            if (withOriginalProjects) { reportReady(); return; }
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
            if (operation == QStringLiteral("initialize") || operation == QStringLiteral("initializeOriginal")
                || (!readyReported && (operation==QStringLiteral("session") || !accountReady))) app.exit(3);
        });
        QObject::connect(accounts, &AccountStore::closed, accounts, []() { qInfo() << "ACCOUNT_STORE_CLOSED"; });
        QObject::connect(shell, &frmMain::loggedOut, &app, []() { qInfo() << "OFFLINE_SESSION_LOGGED_OUT"; });
    }
    if (history)
    {
        auto *page = new HistoryPage(history);
        const QString historyDir = withOriginalProjects ? QDir(originalDbRoot).filePath(QStringLiteral("result"))
            : QDir(dataDir).filePath(QStringLiteral("history"));
        page->setDataDirectory(historyDir);
        page->setLocalResultsMode(withLocalResults);
        // 原开关只控制竖向结果表的均值/CV显示，明细与打印仍保留各次任务结果。
        if (withOriginalProjects) page->setAverageEnabled(originalRuntimeConfiguration.user.averageEnabled);
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
        if (withOriginalProjects)
        {
            QObject::connect(&historyThread, &QThread::started, history,
                             [history, originalDbRoot]() { history->initializeOriginal(originalDbRoot); });
        }
        else if (withLocalResults)
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
        if (samples) QObject::connect(samples,&SampleStore::originalReagentPageMetadataChanged,
            reagentPage,&ReagentsPage::refreshOriginalPageMetadata,Qt::QueuedConnection);
        shell->attachCatalogPages(projectPage,reagentPage);
        if (withLocalResults && !withOriginalProjects)
        {
            auto *resultPage = shell->historyPage();
            auto *displayState = resultPage ? resultPage->currentDisplayState() : nullptr;
            if (!resultPage || !projectPage->bindCurrentDisplayState(displayState))
            {
                reportSetupFailed = true;
                qCritical() << "CURRENT_RESULT_FORMAT_BINDING_FAILED";
                QTimer::singleShot(0, &app, [&app]() { app.exit(3); });
            }
            else
            {
                // The actual Page owns the QObject validator. No bool substitutes for its source receipt.
                QObject::connect(projectPage, &ProjectPage::currentFormatInvalidated,
                                 resultPage, &HistoryPage::invalidateCurrentResultFormat);
                const auto writesFormat = [](const QString &operation)
                {
                    return operation == QStringLiteral("save") || operation == QStringLiteral("copy")
                        || operation == QStringLiteral("remove")
                        || operation == QStringLiteral("replaceCurrentResultFormat");
                };
                QObject::connect(projects, &ProjectsStore::succeeded, resultPage,
                    [resultPage, writesFormat, &currentFormatRefreshPending](qint64, const QString &operation, qint64)
                {
                    if (!writesFormat(operation)) return;
                    currentFormatRefreshPending = true;
                    resultPage->invalidateCurrentResultFormat(QObject::tr("项目配置已提交，当前结果显示等待重新读取。"));
                }, Qt::QueuedConnection);
                QObject::connect(projects, &ProjectsStore::error, resultPage,
                    [resultPage, writesFormat, &currentFormatRefreshPending](qint64, const QString &operation, const QString &message)
                {
                    if (!writesFormat(operation)) return;
                    currentFormatRefreshPending = true;
                    // Unknown/failed writes are not called cancelled. A later real reload or manual query reads current SQL.
                    resultPage->invalidateCurrentResultFormat(message);
                }, Qt::QueuedConnection);
                QObject::connect(projectPage, &ProjectPage::operationFinished, resultPage,
                    [resultPage, &currentFormatRefreshPending](const QString &operation, bool success)
                {
                    // A commit callback precedes the editor's close/reload. Waiting for actual load avoids
                    // a second writer-Page invalidation cancelling a newly started history query.
                    if (success && operation == QStringLiteral("load") && currentFormatRefreshPending)
                    {
                        currentFormatRefreshPending = false;
                        resultPage->refresh(); // Page itself defers safely while patient drafts/real writes are active.
                    }
                });
                QObject::connect(projects, &ProjectsStore::closed, resultPage,
                    [resultPage, &currentFormatRefreshPending]()
                {
                    currentFormatRefreshPending = false;
                    resultPage->invalidateCurrentResultFormat(QObject::tr("项目资料源已关闭。"));
                }, Qt::QueuedConnection);
                QObject::connect(&app, &QCoreApplication::aboutToQuit, resultPage,
                    [resultPage, &currentFormatRefreshPending]()
                {
                    currentFormatRefreshPending = false;
                    // GUI value invalidation only; no SQL, refresh request or thread wait in this callback.
                    resultPage->invalidateCurrentResultFormat(QObject::tr("应用正在退出。"));
                });
            }
        }
        const QString catalogDir = QDir(dataDir).filePath(QStringLiteral("catalog"));
        if (quality)
        {
            auto *page = new QualityControlPage(quality);
            shell->attachQualityControlPage(page);
            QObject::connect(shell, &frmMain::sessionChanged, quality, &QcStore::setSession, Qt::QueuedConnection);
            QObject::connect(shell, &frmMain::deviceActionStateChanged, quality, &QcStore::setSession, Qt::QueuedConnection);
            const QString qualityDir = QDir(dataDir).filePath(QStringLiteral("quality_control"));
            QObject::connect(projects, &ProjectsStore::ready, quality,
                             [quality, qualityDir, catalogDir, withOriginalProjects, originalDbRoot, initialized = false]() mutable
            {
                if (initialized) return;
                initialized = true;
                if (withOriginalProjects) quality->initializeOriginal(originalDbRoot);
                else
                {
                    quality->initialize(qualityDir);
                    quality->bindCatalog(catalogDir);
                }
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
            if (withThemeConfiguration) samplePage->configureLayout(themeConfiguration.sampleHoles,themeConfiguration.emergencyHoles);
            if (withOriginalProjects &&
                !samplePage->configureOriginalDefaultContainer(originalRuntimeConfiguration.user.containerIsTube))
            {
                delete samplePage;
                qCritical() << "原样本页面的新孔默认容器未能在页面所属线程配置。";
                return 3;
            }
            shell->attachSamplePage(samplePage);
            // Schema changes on the shared catalog are serialized: project,
            // reagent, then sample. Each store owns a separate SQL connection.
            QObject::connect(reagents,&ReagentStore::ready,samples,
                             [samples,catalogDir,withOriginalProjects,originalDbRoot]()
            {
                if (withOriginalProjects) samples->initializeOriginal(originalDbRoot);
                else samples->initialize(catalogDir);
            },Qt::QueuedConnection);
            shell->attachDeviceTaskStore();
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
            if (quality && !withOriginalProjects && !connectSampleQuality(samples, quality, samplePage, &app))
            {
                qCritical() << "SAMPLE_QC_BRIDGE_SETUP_FAILED";
                QTimer::singleShot(0, &app, [&app]() { app.exit(3); });
            }
        }
        if (calibration)
        {
            auto *page = new CalibrationPage(calibration);
            shell->attachCalibrationPage(page);
            if (withOriginalProjects && samples)
            {
                QObject::connect(samples, &SampleStore::originalCalibrationRecordsChanged, page, [page]()
                {
                    if (page->isReady() && !page->isBusy() && !page->hasUnsavedChanges()) page->reload();
                });
            }
            const QString calibrationDir = QDir(dataDir).filePath(QStringLiteral("calibration"));
            const QString catalogDatabase = QDir(catalogDir).filePath(QStringLiteral("catalog.db"));
            if (withOriginalProjects)
            {
                // 校准以原项目编号和单位读历史。先等两个页面取得已提交快照，
                // 再排队打开原库；不能以Store刚打开的信号代替资料已读完。
                auto initialized = std::make_shared<bool>(false);
                const auto synchronize = [calibration, page, projectPage, reagentPage, originalDbRoot, initialized]()
                {
                    if (!projectPage->isReady() || projectPage->isBusy() ||
                        !reagentPage->isReady() || reagentPage->isBusy()) return;
                    const auto records = projectPage->projects();
                    const auto units = projectPage->units();
                    const auto snapshot = reagentPage->snapshot();
                    const bool first = !*initialized;
                    *initialized = true;
                    QMetaObject::invokeMethod(calibration, [calibration, records, units, snapshot, first, originalDbRoot]()
                    {
                        if (first) calibration->initializeOriginal(originalDbRoot);
                        // initialize会释放上次资料；同一工作线程返回事件循环前
                        // 设置新快照，保证ready引起的首次load不会读到空项目。
                        calibration->setOriginalCatalog(records, units, snapshot);
                    }, Qt::QueuedConnection);
                    if (!first && page->isReady() && !page->isBusy() && !page->hasUnsavedChanges()) page->reload();
                };
                QObject::connect(projectPage, &ProjectPage::snapshotChanged, page, synchronize);
                QObject::connect(reagentPage, &ReagentsPage::snapshotChanged, page, synchronize);
            }
            else
            {
                const auto initializeCalibration = [calibration, calibrationDir, catalogDatabase]()
                { calibration->initialize(calibrationDir, catalogDatabase); };
                // 独立模式仍等共用catalog建表提交后再打开引用保护连接。
                if (samples) QObject::connect(samples, &SampleStore::ready, calibration, initializeCalibration, Qt::QueuedConnection);
                else QObject::connect(reagents, &ReagentStore::ready, calibration, initializeCalibration, Qt::QueuedConnection);
            }
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
                         [projects,catalogDir,seedDemo,withOriginalProjects,originalDbRoot,&app]()
        {
            if (withOriginalProjects)
            {
                // 工作线程只核对配置能否完整读取，不启用原文件里的仪器或网络连接。
                const QStringList iniFiles{QStringLiteral("config.ini"), QStringLiteral("config_Debug.ini"),
                    QStringLiteral("theme_config.ini"), QStringLiteral("calibrationFile/ReagentAmountCalibration.ini")};
                int fileCount = 0, keyCount = 0, commentCount = 0;
                for (const QString &relative : iniFiles)
                {
                    const QString path = QDir(originalDbRoot).filePath(relative);
                    if (!QFileInfo::exists(path) && !QFileInfo(path).isSymLink()) continue;
                    OriginalIni::Settings settings;
                    QString message;
                    if (!OriginalIni::read(path, &settings, &message))
                    {
                        qCritical().noquote() << "ORIGINAL_INI_ERROR" << relative << message;
                        QTimer::singleShot(0, &app, [&app]() { app.exit(3); });
                        return; // 读取不完整时不能继续报告原配置就绪。
                    }
                    ++fileCount;
                    keyCount += settings.values.size();
                    commentCount += settings.commentLines;
                }
                qInfo() << "ORIGINAL_INI_READ_OK files=" << fileCount << "keys=" << keyCount
                        << "compatible_comment_lines=" << commentCount << "connection_settings_applied=false";
                projects->initializeOriginal(originalDbRoot, catalogDir);
            }
            else projects->initialize(catalogDir,seedDemo);
        });
        // All threads run before initialization. The catalog creator commits its
        // schema/marker first, then the reagent worker may open its own connection.
        QObject::connect(projects,&ProjectsStore::ready,reagents,
                         [reagents,catalogDir,withOriginalProjects,originalDbRoot,initialized = false](const QString &) mutable
        {
            if (initialized) return;
            initialized = true;
            if (withOriginalProjects) reagents->initializeOriginal(originalDbRoot);
            else reagents->initialize(catalogDir);
        },Qt::QueuedConnection);
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
            if (operation==QStringLiteral("initialize") || operation==QStringLiteral("initializeOriginal") || !projectReady) app.exit(3);
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
        if (withThemeConfiguration)
        {
            OriginalRuntime::RuntimeSettings runtime=originalRuntimeConfiguration;
            runtime.theme=themeConfiguration;
            if (!withOriginalProjects) runtime.debug.showCommandsInitially=true;
            page->applyOriginalRuntime(runtime,nullptr);
        }
        shell->attachInstrumentPage(page);
        if (withOriginalProjects && withDeviceControl)
        {
            // 告警初始化已经只读加载同一原系统资料；复用通道值快照，不重复打开数据库。
            const auto &theme = withThemeConfiguration ? themeConfiguration : originalRuntimeConfiguration.theme;
            QString message;
            if (!page->configureOriginalAdChannels(originalAlarmProfile.channels,theme.testChannels,&message))
            {
                qCritical().noquote() << "ORIGINAL_AD_CHANNELS_INVALID" << message;
                return 2;
            }
            auto *control = shell->deviceControl();
            if (!control || control->thread() != page->thread())
            {
                qCritical() << "ORIGINAL_AD_ROUTE_INVALID device_and_page_must_share_gui_thread=true";
                return 2;
            }
            qInfo() << "ORIGINAL_AD_CHANNELS_READY configured_count=" << theme.testChannels
                    << "source_rows=" << originalAlarmProfile.channels.test.size();
            originalChannelStore = new OriginalChannelStore;
            originalChannelStore->moveToThread(&originalChannelThread);
            QObject::connect(&originalChannelThread,&QThread::finished,originalChannelStore.data(),&QObject::deleteLater);
            originalChannelReceiver = new OriginalChannelReceiver(originalChannelStore.data(),control,originalDbRoot,
                [target=QPointer<InstrumentPage>(page)](const OriginalRuntime::Channels &channels,quint64 generation,QString *message)
            {
                if (!target)
                {
                    if (message) *message=QStringLiteral("仪器状态页面已关闭，库已保存但未刷新页面。");
                    return false;
                }
                return target->applyCommittedOriginalChannels(channels,generation,message);
            },&app);
            QObject::connect(&originalChannelThread,&QThread::started,originalChannelReceiver,
                [originalChannelReceiver,&originalChannelFailure]()
            {
                if (!originalChannelReceiver->start())
                {
                    originalChannelFailure=true;
                    originalChannelReceiver->requestStop();
                }
            },Qt::QueuedConnection);
            // 连设备时statusChanged不会改主状态区；沿用已有非模态浮动提示，不覆盖主控状态灯。
            const auto presentOriginalChannelResult = [page,host=QPointer<frmMain>(shell)](
                quint64 request,quint64 generation,const QString &outcome,bool displayed,const QString &text)
            {
                page->setProperty("originalChannelResult",text);
                page->setProperty("originalChannelResultRequest",QVariant::fromValue(request));
                page->setProperty("originalChannelResultGeneration",QVariant::fromValue(generation));
                page->setProperty("originalChannelResultOutcome",outcome);
                page->setProperty("originalChannelResultDisplayed",displayed);
                page->setToolTip(text);
                emit page->statusChanged(text);
                // 通道页可能隐藏或滚出可见画布；固定原状态区作锚点，不切页、不抢焦点。
                if (!host || !host->isVisible() || !host->window()->isActiveWindow()) return;
                auto *anchor=host->findChild<QWidget*>(QStringLiteral("statusArea"));
                if (!anchor || !anchor->isVisible() || anchor->visibleRegion().isEmpty()) return;
                const QString tooltip=QStringLiteral("<qt>%1</qt>").arg(
                    text.toHtmlEscaped().replace(QLatin1Char('\n'),QStringLiteral("<br/>")));
                QToolTip::showText(anchor->mapToGlobal(anchor->visibleRegion().boundingRect().center()),
                                   tooltip,anchor,QRect(),12000);
            };
            QObject::connect(originalChannelReceiver,&OriginalChannelReceiver::error,page,
                [presentOriginalChannelResult,&originalChannelFailure](const QString &message)
            {
                originalChannelFailure=true;
                qWarning().noquote() << "ORIGINAL_CHANNEL_ERROR" << message;
                presentOriginalChannelResult(0,0,QStringLiteral("ReceiverError"),false,message);
            });
            QObject::connect(originalChannelReceiver,&OriginalChannelReceiver::batchFinished,page,
                [presentOriginalChannelResult,source=QPointer<DeviceControl>(control),&originalChannelFailure](
                    quint64 request,quint64 generation,const QString &outcome,bool displayed,
                    const QString &message,int changed,int unknown)
            {
                qInfo().noquote() << "ORIGINAL_CHANNEL_RESULT request=" << request << "generation=" << generation
                    << "outcome=" << outcome << "displayed=" << displayed << "changed=" << changed
                    << "unknown=" << unknown << message;
                if (outcome==QStringLiteral("Failed") || outcome==QStringLiteral("CommittedReloadFailed")
                    || outcome==QStringLiteral("Unconfirmed")) originalChannelFailure=true;
                const bool current=source && source->state().connected && source->connectionGeneration()==generation;
                const bool refreshed=displayed && current;
                QString result;
                if (outcome==QStringLiteral("Committed"))
                    result=refreshed ? QStringLiteral("通道资料已保存，当前通道表已刷新并清除旧AD；等待主控上报新读数。")
                        : current ? QStringLiteral("通道资料已保存，但当前页面未刷新。")
                                  : QStringLiteral("旧连接的通道资料已保存，未刷新当前页面。");
                else if (outcome==QStringLiteral("CommittedReloadFailed"))
                    result=QStringLiteral("通道资料已保存，但重新读取失败，页面尚未刷新；需重新读取核对。");
                else if (outcome==QStringLiteral("Cancelled"))
                    result=QStringLiteral("本批通道回读已取消，未保存。");
                else if (outcome==QStringLiteral("Failed"))
                    result=QStringLiteral("通道回读未保存，请核对以下原因。");
                else
                    result=QStringLiteral("通道回读的保存结果待核，未确认完成，不会自动重放。");
                if (!message.isEmpty()) result+=QStringLiteral("\n原因或详情：")+message;
                presentOriginalChannelResult(request,generation,outcome,refreshed,result);
            });
            QObject::connect(originalChannelReceiver,&OriginalChannelReceiver::snapshotRecovered,page,
                [presentOriginalChannelResult,source=QPointer<DeviceControl>(control)](
                    quint64 request,quint64 generation,bool displayed,const QString &message)
            {
                qInfo().noquote() << "ORIGINAL_CHANNEL_SNAPSHOT_RECOVERED request=" << request
                    << "generation=" << generation << "displayed=" << displayed << message;
                const bool refreshed=displayed && source && source->state().connected
                    && source->connectionGeneration()==generation;
                const QString result=(refreshed
                    ? QStringLiteral("已保存的通道资料重新读取成功，当前通道表已刷新并清除旧AD。")
                    : QStringLiteral("已保存的通道资料重新读取成功，未刷新当前页面。"))
                    +(displayed && !refreshed
                        ? QStringLiteral("\n连接在显示结果确认期间已变化，以当前连接页面为准。")
                        : QStringLiteral("\n详情：")+message);
                presentOriginalChannelResult(request,generation,QStringLiteral("SnapshotRecovered"),refreshed,result);
            });
            // 正常停止由GUI消费closed后触发；异常退出在app.exec返回后由main显式quit/join。
            QObject::connect(originalChannelReceiver,&OriginalChannelReceiver::stopped,&originalChannelThread,&QThread::quit);
            QObject::connect(&app,&QCoreApplication::aboutToQuit,originalChannelReceiver,
                             &OriginalChannelReceiver::requestStop,Qt::DirectConnection);
            QObject::connect(control,&DeviceControl::payloadReceived,page,
                [page,control,reagentPositions=theme.reagentPositions](const QByteArray &payload) mutable
            {
                // 本信号与页面同在GUI线程并直接执行，接包时取代次，不排队后重标旧包。
                const quint64 connection = control->connectionGeneration();
                QString message;
                if (payload.startsWith("Qq"))
                {
                    if (!page->receiveOriginalAd(payload,connection,&message))
                        qWarning().noquote() << "ORIGINAL_AD_REJECTED" << message;
                    else
                        page->setProperty("originalAdMessageCount",page->property("originalAdMessageCount").toULongLong()+1);
                    return;
                }
                if (!payload.startsWith("Qj") || !control->state().connected || !control->state().fresh) return;
                // DeviceControl已完整校验并安装本条Qj；20/21为原试剂位/通道数字段。
                // 旧协议缺这两项时，原pcQueryStatus明确使用20/18，不根据机型名称推断。
                const auto &fields = control->state().status.generalFields;
                bool positionsValid = true, channelsValid = true;
                const int positions = fields.size()>20 ? fields.at(20).toInt(&positionsValid) : 20;
                const int channels = fields.size()>21 ? fields.at(21).toInt(&channelsValid) : 18;
                if (!positionsValid || !channelsValid || positions<0 || channels<0 || channels>1024)
                {
                    qWarning() << "ORIGINAL_AD_RELOAD_REJECTED invalid_reported_channel_counts=true";
                    return;
                }
                if (positions == reagentPositions) return;
                // 原signalReceiveDriverModel仅在试剂位数量改变时重建表头和通道行。
                if (!page->reloadOriginalAdChannels(channels,&message))
                    qWarning().noquote() << "ORIGINAL_AD_RELOAD_REJECTED" << message;
                else
                {
                    reagentPositions = positions;
                    qInfo() << "ORIGINAL_AD_CHANNELS_RELOADED reagent_positions=" << positions
                            << "reported_channels=" << channels;
                }
            },Qt::DirectConnection);
        }
        const QString instrumentDir = QDir(dataDir).filePath(QStringLiteral("instrument"));
        QObject::connect(&maintenanceThread,&QThread::started,maintenance,
                         [maintenance,instrumentDir,withOriginalProjects,originalDbRoot]()
        {
            if (withOriginalProjects) maintenance->initializeOriginal(originalDbRoot);
            else maintenance->initialize(instrumentDir);
        });
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
                         [printer, settingsDir, withOriginalProjects, originalDbRoot]()
        {
            if (withOriginalProjects) printer->initializeOriginal(originalDbRoot);
            else printer->initialize(settingsDir);
        });
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
        lisConnection = new Sn100Lis::LisConnectionController(lis,
            originalRuntimeConfiguration.debug.reconnect,
            originalRuntimeConfiguration.debug.reconnectMilliseconds, &app);
        lisConnection->setObjectName(QStringLiteral("originalLisConnectionController"));
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::stateChanged,
                         shell, &frmMain::setLisConnectionState);
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::stateChanged, &app,
                         [](const Sn100Lis::ConnectionState &state)
        {
            qInfo() << "LIS_CONNECTION_STATE" << state.state << "revision=" << state.configuration.revision
                    << "generation=" << state.generation << "peers=" << state.connectedPeers;
        });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::error, &app,
                         [shell, lisConnection](const Sn100Lis::ConfigurationIdentity &configuration,
                         const QString &operation, quint64 generation, quint64 peer, const QString &detail)
        {
            qWarning().noquote() << "LIS_CONNECTION_ERROR" << operation << "revision=" << configuration.revision
                << "generation=" << generation << "peer=" << peer << detail;
            auto state = lisConnection->state();
            if (state.configuration.revision == configuration.revision && state.generation == generation)
            { state.detail = detail; shell->setLisConnectionState(state); }
        });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::messageReceived, &app,
                         [shell, lisConnection](const Sn100Lis::ReceivedMessage &message)
        {
            // 这里真实接到完整正文和值身份。任务/上传消费者下一片接，不据此回成功ACK。
            // 默认日志仅记来源与长度；正文可能含患者资料，不自动写入公共app.log。
            qWarning() << "LIS_MESSAGE_NOT_APPLIED" << "revision=" << message.configuration.revision
                << "generation=" << message.generation << "peer=" << message.peer
                << "body_bytes=" << message.body.size() << "ack_sent=false task_saved=false";
            auto state = lisConnection->state();
            state.detail = QObject::tr("已收到完整LIS消息（连接代次%1、对端%2）；业务保存与确认尚未接入。")
                .arg(message.generation).arg(message.peer);
            shell->setLisConnectionState(state);
        });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::sendFinished, &app,
                         [](const Sn100Lis::SendOutcome &result)
        {
            qInfo() << "LIS_SEND_OUTCOME" << result.request << "revision=" << result.configuration.revision
                    << "generation=" << result.generation << "peer=" << result.peer << result.outcome
                    << "business_acknowledged=false";
        });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::messageDiscarded, &app,
                         [](const Sn100Lis::ConfigurationIdentity &configuration,
                         quint64 generation, quint64 peer, int count, const QString &reason)
        { qWarning().noquote() << "LIS_MESSAGE_DISCARDED" << configuration.revision << generation << peer << count << reason; });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::wireBytes, shell,
                         [shell](const Sn100Lis::ConfigurationIdentity &, const QString &direction,
                         quint64, quint64, const QByteArray &bytes)
        {
            // 完整字节由有身份的wireBytes接口供获准诊断使用，此处只做有界计数。
            const char *property = direction == QStringLiteral("RX") ? "lisReceivedBytes" : "lisSubmittedBytes";
            const qulonglong previous = shell->property(property).toULongLong();
            const qulonglong maximum = std::numeric_limits<qulonglong>::max();
            shell->setProperty(property, QVariant::fromValue(previous > maximum - qulonglong(bytes.size())
                ? maximum : previous + qulonglong(bytes.size())));
        });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::businessTimeoutTick, shell,
                         [shell](const Sn100Lis::ConfigurationIdentity &, quint64, qint64 elapsed)
        {
            // 仅交接原第三拍时间。当前没有查询在途身份，不补造超时业务或重发。
            shell->setProperty("lisLastBusinessTick", elapsed);
        });
        QObject::connect(lisConnection, &Sn100Lis::LisConnectionController::stopped, &app,
                         []() { qInfo() << "LIS_CONNECTION_STOPPED"; });
        QObject::connect(&app, &QCoreApplication::aboutToQuit, lisConnection,
                         &Sn100Lis::LisConnectionController::shutdown);
        shell->setLisConnectionState(lisConnection->state());
        const QString settingsDir = QDir(dataDir).filePath(QStringLiteral("settings"));
        QObject::connect(&lisThread, &QThread::started, lis,
                         [lis, settingsDir, withOriginalProjects, originalDbRoot]()
        {
            if (withOriginalProjects) lis->initializeOriginal(originalDbRoot);
            else lis->initialize(settingsDir);
        });
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
    viewport.setOriginalChannelReceiver(originalChannelReceiver);
    if (originalChannelReceiver)
        QObject::connect(originalChannelReceiver,&OriginalChannelReceiver::stopped,&viewport,
                         [&viewport]() { viewport.finishOriginalChannelStop(); },Qt::QueuedConnection);
    if (withThemeConfiguration) shell->applyThemeConfiguration(themeConfiguration);
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
    if (withLisRawInbox) viewport.setWindowTitle(viewport.windowTitle() + QStringLiteral(" · 离线原报文"));
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
    QObject::connect(&databaseThread, &QThread::started, store, [store, hospitalDir, withOriginalProjects]()
    {
        if (withOriginalProjects) store->initializeOriginal(hospitalDir);
        else store->initialize(QDir(hospitalDir).filePath(QStringLiteral("hospital_Information.db")),
                               QDir(hospitalDir).filePath(QStringLiteral("config.ini")));
    });
    QObject::connect(store, &HospitalStore::error, &app, [&](const QString &operation, const QString &message)
    {
        qWarning().noquote() << "STORAGE_ERROR" << operation << message;
        if (operation == QStringLiteral("initialize") || operation == QStringLiteral("initializeOriginal")) app.exit(3);
    });
    QObject::connect(store, &HospitalStore::warning, &app, [](const QString &message)
    {
        qWarning().noquote() << "STORAGE_WARNING" << message;
    });
    QObject::connect(store, &HospitalStore::closed, store, [withAccounts]()
    {
        qInfo() << (withAccounts ? "HOSPITAL_STORE_CLOSED" : "STORE_CLOSED");
    });
    startupSplash = new StartupSplash(&viewport);
    viewport.show();
    QString splashError;
    if (!startupSplash->start(&splashError))
        qWarning().noquote() << "STARTUP_SPLASH_UNAVAILABLE" << splashError;
    databaseThread.start();
    if (alarmHistory) alarmHistoryThread.start();
    if (originalAlarmWriter) originalAlarmThread.start();
    if (accounts) accountThread.start();
    if (history) historyThread.start();
    if (samples) sampleThread.start();
    if (maintenance) maintenanceThread.start();
    if (originalChannelStore) originalChannelThread.start();
    if (printer) printerThread.start();
    if (lis) lisThread.start();
    if (quality) qualityThread.start();
    if (calibration) calibrationThread.start();
    if (withCatalog) { reagentThread.start();projectThread.start(); }
    qInfo().noquote() << "START mode=offline module=" << ((withCalibration || withQualityControl) ? "LOCAL_BUSINESS_20261025" : withSamples ? "O08" : withCatalog ? "O07" : withHistory ? "O03-4" : withAccounts ? "O03-2" : "O03-1") << "Qt=" << qVersion()
                      << "platform=" << QGuiApplication::platformName()
                      << "logical_canvas=1920x1080 viewport=" << viewportSize << "data_dir=" << dataDir;
    int result = app.exec();
    if (lisConnection)
    {
        lisConnection->shutdown(); // 先停新收发，再关闭原Store线程。
        if (!lisConnection->isStopped())
        {
            qCritical() << "LIS_CONNECTION_SHUTDOWN_UNCONFIRMED";
            result = 3;
        }
    }
    if (originalChannelReceiver)
    {
        if (!originalChannelReceiver->isStopped())
        {
            originalChannelReceiver->requestStop();
            // 本线程仅由Receiver.stopped或此处quit；未stopped时仍能处理最后的close。
            // GUI循环已结束，不把排队但未消费的applied当取消，也不自动重放。
            if (originalChannelStore && originalChannelThread.isRunning())
                QMetaObject::invokeMethod(originalChannelStore.data(),"close",Qt::BlockingQueuedConnection);
            qCritical() << "ORIGINAL_CHANNEL_SHUTDOWN_UNCONFIRMED pending_batches="
                        << originalChannelReceiver->pendingBatchCount();
            result=3;
        }
        originalChannelThread.quit();
        originalChannelThread.wait();
        originalChannelReceiver->finishAfterWorkerStopped();
        qInfo() << "ORIGINAL_CHANNEL_WORKER_STOPPED" << !originalChannelThread.isRunning();
        if (originalChannelFailure) result=3;
    }
    if (originalAlarmReceiver && !shell->originalAlarmShutdownComplete())
    {
        // 启动失败或强制退出可能绕过closeEvent；直接close不代表在途批次已排空。
        // 此时GUI事件循环已经停止，先原子保留完整原行及接收账户/时间，再直接清理SQL。
        QString savedFile, saveError;
        const QString reason = QStringLiteral("GUI事件循环在告警接收器正常停止前退出，写入结果待核；禁止自动重放。");
        if (!originalAlarmReceiver->savePendingRecovery(reason, &savedFile, &saveError))
            qCritical().noquote() << "ORIGINAL_ALARM_RECOVERY_SAVE_FAILED" << saveError;
        else if (!savedFile.isEmpty())
            qCritical().noquote() << "ORIGINAL_ALARM_RECOVERY_SAVED" << savedFile << "automatic_replay=false";
        qCritical() << "ORIGINAL_ALARM_SHUTDOWN_UNCONFIRMED pending_batches="
                    << originalAlarmReceiver->pendingBatchCount()
                    << "pending_recovery_batches=" << originalAlarmReceiver->pendingRecoveryBatchCount();
        result = 3;
    }
    if (originalAlarmReceiver && originalAlarmReceiver->hasStorageErrors()) result = 3;
    if (withLisRawInbox)
    {
        const bool closed = shell->lisRawInboxShutdownComplete();
        qInfo() << "LIS_RAW_INBOX_STOPPED" << closed;
        if (!closed) result = 3;
    }
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
    if (originalAlarmWriter) QMetaObject::invokeMethod(originalAlarmWriter,"close",Qt::BlockingQueuedConnection);
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
    if (originalAlarmWriter) originalAlarmThread.quit();
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
    if (originalAlarmWriter)
    {
        originalAlarmThread.wait();
        qInfo() << "ORIGINAL_ALARM_WORKER_STOPPED" << !originalAlarmThread.isRunning();
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
        && !projectThread.isRunning() && !reagentThread.isRunning() && !sampleThread.isRunning() && !maintenanceThread.isRunning() && !printerThread.isRunning() && !lisThread.isRunning() && !qualityThread.isRunning() && !calibrationThread.isRunning() && !statusLoopback.isRunning() && !alarmHistoryThread.isRunning() && !originalAlarmThread.isRunning());
    else qInfo() << "THREAD_STOPPED" << !databaseThread.isRunning();
    qInfo() << "EXIT code=" << result;
    return result;
}
