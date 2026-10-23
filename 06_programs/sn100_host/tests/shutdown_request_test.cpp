#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <QWidget>

#include "shell/shutdown_request.h"

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace
{
volatile sig_atomic_t restoredSignals = 0;
void previousHandler(int) { ++restoredSignals; }

class GuardedWindow : public QWidget
{
public:
    bool dirty = false;
    bool busy = false;
    bool repeatInsideClose = false;
    int closeAttempts = 0;
    int depth = 0;
    int maxDepth = 0;

    void closeEvent(QCloseEvent *event) override
    {
        ++closeAttempts;
        ++depth;
        maxDepth = qMax(maxDepth, depth);
        if (repeatInsideClose)
        {
            // 代表原草稿确认框的嵌套事件循环：重复信号不能再次进入close。
            repeatInsideClose = false;
            ::raise(SIGTERM);
            ::raise(SIGINT);
            QTest::qWait(20);
        }
        if (dirty || busy) event->ignore();
        else event->accept();
        --depth;
    }
};

QStringList openDescriptors()
{
    const QStringList entries = QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
    QStringList descriptors;
    for (const QString &entry : entries)
    {
        if (::fcntl(entry.toInt(), F_GETFD) >= 0) descriptors.append(entry);
    }
    return descriptors;
}
}
#endif

class ShutdownRequestTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QApplication::setQuitOnLastWindowClosed(false);
    }

    void guardedSignals()
    {
#ifndef Q_OS_UNIX
        QSKIP("POSIX shutdown requests are disabled on Windows");
#else
        GuardedWindow window;
        window.show();
        ShutdownRequest requests;
        requests.setWindow(&window);
        QString error;
        QVERIFY2(requests.initialize(&error), qPrintable(error));

        window.dirty = true;
        window.repeatInsideClose = true;
        ::raise(SIGTERM);
        QTRY_COMPARE(window.closeAttempts, 1);
        QTest::qWait(30);
        QCOMPARE(window.closeAttempts, 1);
        QCOMPARE(window.maxDepth, 1);
        QVERIFY(window.isVisible());
        QVERIFY(window.dirty);

        window.dirty = false;
        window.busy = true;
        ::raise(SIGINT);
        QTRY_COMPARE(window.closeAttempts, 2);
        QVERIFY(window.isVisible());
        QVERIFY(window.busy);

        // 大量同步请求填满管道也不能阻塞handler或绕过拒绝守卫。
        for (int i = 0; i < 10000; ++i) ::raise(SIGTERM);
        QTRY_COMPARE(window.closeAttempts, 3);
        QTest::qWait(20);
        QCOMPARE(window.closeAttempts, 3);
        QVERIFY(window.isVisible());
        window.busy = false;
        ::raise(SIGTERM);
        QTRY_COMPARE(window.closeAttempts, 4);
        QVERIFY(!window.isVisible());
#endif
    }

    void restoresHandlersAndDescriptors()
    {
#ifndef Q_OS_UNIX
        QSKIP("POSIX shutdown requests are disabled on Windows");
#else
        QCoreApplication::processEvents();
        const QStringList before = openDescriptors();
        struct sigaction savedTerm = {}, savedInt = {}, action = {};
        action.sa_handler = previousHandler;
        sigemptyset(&action.sa_mask);
        QVERIFY(::sigaction(SIGTERM, &action, &savedTerm) == 0);
        QVERIFY(::sigaction(SIGINT, &action, &savedInt) == 0);
        bool initialized = true;
        bool attributes = true;
        bool duplicateRejected = true;
        for (int cycle = 0; cycle < 20; ++cycle)
        {
            ShutdownRequest requests;
            QString error;
            if (!requests.initialize(&error)) { initialized = false; break; }
            const QStringList during = openDescriptors();
            int added = 0;
            for (const QString &name : during)
            {
                if (before.contains(name)) continue;
                const int descriptor = name.toInt();
                // entryList短暂打开目录自身，目录FD已关闭不纳入桥接FD。
                if (::fcntl(descriptor, F_GETFD) < 0) continue;
                ++added;
                attributes = attributes && (::fcntl(descriptor, F_GETFD) & FD_CLOEXEC)
                    && (::fcntl(descriptor, F_GETFL) & O_NONBLOCK);
            }
            attributes = attributes && added == 2;
            ShutdownRequest duplicate;
            duplicateRejected = duplicateRejected && !duplicate.initialize(&error);
        }
        struct sigaction actualTerm = {}, actualInt = {};
        ::sigaction(SIGTERM, nullptr, &actualTerm);
        ::sigaction(SIGINT, nullptr, &actualInt);
        const bool restored = actualTerm.sa_handler == previousHandler && actualInt.sa_handler == previousHandler;
        restoredSignals = 0;
        ::raise(SIGTERM);
        ::raise(SIGINT);
        const int delivered = restoredSignals;
        ::sigaction(SIGTERM, &savedTerm, nullptr);
        ::sigaction(SIGINT, &savedInt, nullptr);
        QVERIFY(initialized);
        QVERIFY(attributes);
        QVERIFY(duplicateRejected);
        QVERIFY(restored);
        QCOMPARE(delivered, 2);
        QCOMPARE(openDescriptors(), before);
#endif
    }

    void applicationSignalCleanup()
    {
#ifndef Q_OS_UNIX
        QSKIP("POSIX shutdown requests are disabled on Windows");
#else
        const QString executable = qEnvironmentVariable("SN100_SHUTDOWN_APP");
        QVERIFY2(!executable.isEmpty(), "SN100_SHUTDOWN_APP must name the matching native shell build");
        const QString work = qEnvironmentVariable("SN100_SHUTDOWN_WORK_DIR");
        QVERIFY(QDir::isAbsolutePath(work));
        QTemporaryDir experiment(QDir(work).filePath(QStringLiteral("signal_cleanup_XXXXXX")));
        QVERIFY(experiment.isValid());
        const QString secretPath = QDir(experiment.path()).filePath(QStringLiteral("bootstrap.secret"));
        QFile secret(secretPath);
        QVERIFY(secret.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QVERIFY(secret.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(secret.write("local-signal-test") > 0);
        secret.close();
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        process.setProcessEnvironment(environment);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(executable, {QStringLiteral("--offline"), QStringLiteral("--accounts"), QStringLiteral("--history"),
            QStringLiteral("--data-dir"), QDir(experiment.path()).filePath(QStringLiteral("data")),
            QStringLiteral("--seed-anonymous"), QStringLiteral("--test-secret-file"), secretPath});
        QVERIFY(process.waitForStarted(10000));
        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 15000 && !output.contains("SHELL_READY") && process.state() != QProcess::NotRunning)
        {
            process.waitForReadyRead(100);
            output += process.readAll();
        }
        QVERIFY2(output.contains("SHELL_READY"), output.constData());
        QVERIFY(QFile::remove(secretPath));
        QVERIFY(::kill(static_cast<pid_t>(process.processId()), SIGTERM) == 0);
        QVERIFY2(process.waitForFinished(10000), "Signal did not complete normal window/database cleanup");
        output += process.readAll();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY2(output.contains("SHUTDOWN_REQUEST"), output.constData());
        QVERIFY(output.contains("SHUTDOWN_ACCEPTED"));
        QVERIFY(output.contains("HOSPITAL_STORE_CLOSED"));
        QVERIFY(output.contains("ACCOUNT_STORE_CLOSED"));
        QVERIFY(output.contains("HISTORY_STORE_CLOSED"));
        QVERIFY(output.contains("THREADS_STOPPED true"));
        QVERIFY(output.contains("EXIT code= 0"));
        QFile log(QDir(work).filePath(QStringLiteral("application_signal.log")));
        QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(log.write(output), static_cast<qint64>(output.size()));
#endif
    }
};

QTEST_MAIN(ShutdownRequestTest)
#include "shutdown_request_test.moc"
