#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFont>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QDebug>

#include "data/hospital_store.h"
#include "data/account_store.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Seerain"));
    QCoreApplication::setApplicationName(QStringLiteral("sn100_host_offline"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    app.setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SN100 脱机移植实验：O01 医院信息 / O02-1 账户设置"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("offline"), QStringLiteral("仅本地实验（默认；本构建没有仪器通信模块）")});
    parser.addOption({QStringLiteral("module"), QStringLiteral("hospital 或 account"), QStringLiteral("name"), QStringLiteral("hospital")});
    parser.addOption({QStringLiteral("test-session"), QStringLiteral("显式模拟匿名操作者名称；不代表生产登录认证"), QStringLiteral("name")});
    parser.addOption({QStringLiteral("device-state"), QStringLiteral("明确模拟设备 ready 或 running"), QStringLiteral("state"), QStringLiteral("ready")});
    parser.addOption({QStringLiteral("seed-anonymous"), QStringLiteral("仅新账户库：创建五个匿名角色测试账号")});
    parser.addOption({QStringLiteral("test-secret-file"), QStringLiteral("仅首次匿名初始化读取的临时密码文件，不记录内容"), QStringLiteral("path")});
    parser.addOption({QStringLiteral("data-dir"), QStringLiteral("新的脱机数据库及兼容 INI 的绝对目录"), QStringLiteral("directory"),
                      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)});
    parser.process(app);
    const QString dataDir = QDir::cleanPath(parser.value(QStringLiteral("data-dir")));
    if (!QDir::isAbsolutePath(dataDir))
    {
        qCritical().noquote() << "--data-dir must be an absolute path";
        return 2;
    }
    const QString module = parser.value(QStringLiteral("module"));
    if (module == QStringLiteral("account"))
    {
        const QString state = parser.value(QStringLiteral("device-state"));
        if (state != QStringLiteral("ready") && state != QStringLiteral("running"))
        {
            qCritical() << "--device-state must be ready or running (simulated)";
            return 2;
        }
        QString bootstrapPassword;
        if (parser.isSet(QStringLiteral("seed-anonymous")))
        {
            const QFileInfo secretInfo(parser.value(QStringLiteral("test-secret-file")));
            QFile secret(secretInfo.absoluteFilePath());
            if (!secretInfo.isAbsolute() || !secretInfo.isFile() || secretInfo.isSymLink()
                    || secretInfo.size() > 256 || !secret.open(QIODevice::ReadOnly))
            {
                qCritical() << "Seeding requires an absolute, regular temporary --test-secret-file (max 256 bytes)";
                return 2;
            }
            bootstrapPassword = QString::fromUtf8(secret.readAll()).trimmed();
            if (bootstrapPassword.isEmpty())
            {
                qCritical() << "Anonymous bootstrap secret must not be empty";
                return 2;
            }
        }
        AccountStore::registerTypes();
        QThread databaseThread;
        databaseThread.setObjectName(QStringLiteral("AccountDatabase"));
        AccountStore *store = new AccountStore;
        store->moveToThread(&databaseThread);
        QObject::connect(&databaseThread, &QThread::finished, store, &QObject::deleteLater);
        frmAccountSetting window(store);
        window.resize(1200, 900);
        window.setMinimumSize(1061, 861);
        window.setWindowTitle(QStringLiteral("SN100 · O02-1 账户设置 · 匿名脱机 / 模拟设备状态"));
        QObject::connect(&databaseThread, &QThread::started, store, [store, dataDir, bootstrapPassword]()
        {
            store->initialize(dataDir, bootstrapPassword);
        });
        const QString testActor = parser.value(QStringLiteral("test-session"));
        QObject::connect(store, &AccountStore::ready, store, [store, testActor, state]()
        {
            store->setSession(testActor, state == QStringLiteral("ready"));
        });
        QObject::connect(store, &AccountStore::error, &app, [](const QString &operation, const QString &message)
        {
            qWarning().noquote() << "ACCOUNT_ERROR" << operation << message;
        });
        QObject::connect(&window, &frmAccountSetting::signalLoginOut, &app, []()
        {
            qInfo() << "OFFLINE_SESSION_LOGGED_OUT";
        });
        window.show();
        databaseThread.start();
        qInfo().noquote() << "START module=account mode=anonymous-offline Qt=" << qVersion()
                          << "platform=" << QGuiApplication::platformName() << "data_dir=" << dataDir
                          << "simulated_device_state=" << state;
        const int result = app.exec();
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        databaseThread.quit();
        databaseThread.wait();
        qInfo() << "EXIT code=" << result;
        return result;
    }
    if (module != QStringLiteral("hospital"))
    {
        qCritical() << "--module must be hospital or account";
        return 2;
    }
    HospitalStore::registerTypes();
    QThread databaseThread;
    databaseThread.setObjectName(QStringLiteral("HospitalDatabase"));
    HospitalStore *store = new HospitalStore;
    store->moveToThread(&databaseThread);
    QObject::connect(&databaseThread, &QThread::finished, store, &QObject::deleteLater);
    frmHospInfoSetting window(store);
    window.setDataDirectory(dataDir);
    window.setWindowTitle(QStringLiteral("SN100 上位机 · 脱机移植实验 O01 · 医院信息设置"));
    window.resize(1200, 760);
    window.setMinimumSize(900, 600);
    QObject::connect(&databaseThread, &QThread::started, store, [store, dataDir]()
    {
        store->initialize(QDir(dataDir).filePath(QStringLiteral("hospital_Information.db")),
                          QDir(dataDir).filePath(QStringLiteral("config.ini")));
    });
    QObject::connect(store, &HospitalStore::warning, &app, [](const QString &message)
    {
        qWarning().noquote() << "STORAGE_WARNING" << message;
    });
    QObject::connect(store, &HospitalStore::error, &app, [](const QString &operation, const QString &message)
    {
        qWarning().noquote() << "STORAGE_ERROR" << operation << message;
    });
    window.show();
    databaseThread.start();
    qInfo().noquote() << "START mode=offline Qt=" << qVersion()
                      << "platform=" << QGuiApplication::platformName() << "data_dir=" << dataDir;
    const int result = app.exec();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    databaseThread.quit();
    databaseThread.wait();
    qInfo() << "EXIT code=" << result;
    return result;
}

