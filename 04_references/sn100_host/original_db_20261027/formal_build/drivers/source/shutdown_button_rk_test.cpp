#include "data/hospital_store.h"
#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "ui/frmhospinfosetting.h"
#include "ui/maskdlgshutdown.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>
#include <functional>
#include <memory>

namespace
{
bool until(const std::function<bool()> &condition, int timeout = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

// 与正式ShellViewport相同的关闭转交：核对关机按钮关闭外层窗，而非只隐藏业务子窗。
class ExitHost : public QWidget
{
public:
    frmMain *shell;
    explicit ExitHost(HospitalStore *store) : shell(new frmMain(store))
    {
        auto *layout = new QVBoxLayout(this);
        auto *scroll = new QScrollArea(this);
        scroll->setWidget(shell);
        layout->addWidget(scroll);
        resize(1600, 900);
    }
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (shell->close()) event->accept();
        else event->ignore();
    }
};
}

class ShutdownButtonRkTest : public QObject
{
    Q_OBJECT
    QThread worker;
    HospitalStore *store = nullptr;
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<ExitHost> host;

    bool initializeData()
    {
        const QString data = directory->path();
        host->shell->setDataDirectory(data);
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, QDir(data).filePath(QStringLiteral("hospital_Information.db"))),
            Q_ARG(QString, QDir(data).filePath(QStringLiteral("config.ini"))));
        return until([this]() { return host->shell->hospitalPage()->isReady() &&
                                      !host->shell->hospitalPage()->isBusy(); });
    }

    MaskDlgShutDown *openConfirmation()
    {
        auto *button = host->shell->findChild<CusBadgeButton *>(QStringLiteral("btnShutDown"));
        if (!button || !button->isEnabled()) return nullptr;
        QTest::mouseClick(button, Qt::LeftButton);
        MaskDlgShutDown *dialog = nullptr;
        if (!until([&]()
            {
                dialog = host->shell->findChild<MaskDlgShutDown *>();
                return dialog && dialog->isVisible() && dialog->isReadyForInput();
            })) return nullptr;
        return dialog;
    }

private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("本驱动只允许交叉编译后在RK ARM64运行。");
#endif
        QApplication::setQuitOnLastWindowClosed(false);
        HospitalStore::registerTypes();
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
    }

    void init()
    {
        directory.reset(new QTemporaryDir(QDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT"))
                                             .filePath(QStringLiteral("exit_button_XXXXXX"))));
        QVERIFY(directory->isValid());
        store = new HospitalStore;
        store->moveToThread(&worker);
        connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        worker.start();
        host.reset(new ExitHost(store));
        host->show();
        QVERIFY(until([this]() { return host->isVisible(); }));
    }

    void cleanup()
    {
        host.reset();
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        QVERIFY(worker.wait(5000));
        store = nullptr;
        directory.reset();
    }

    void cancelAndRepeatedRequestsPreserveWindow()
    {
        QVERIFY(initializeData());
        QPointer<MaskDlgShutDown> dialog = openConfirmation();
        QVERIFY(dialog);
        QVERIFY(!host->shell->requestShutdown());
        QCOMPARE(host->shell->findChildren<MaskDlgShutDown *>().size(), 1);
        QVERIFY(!host->close());
        QVERIFY(host->isVisible());
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("btnClose")), Qt::LeftButton);
        QVERIFY(until([&]() { return dialog.isNull(); }));
        QVERIFY(host->isVisible() && host->shell->isVisible());
    }

    void confirmsOuterWindowClose()
    {
        QVERIFY(initializeData());
        auto *dialog = openConfirmation();
        QVERIFY(dialog);
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("btnShutDown")), Qt::LeftButton);
        QVERIFY(until([this]() { return !host->isVisible(); }));
        QVERIFY(!host->shell->isVisible());
    }

    void pendingDataRejectsExit()
    {
        // 未完成首次读取的真实页面保持busy，关机不能跳过该守卫。
        QVERIFY(host->shell->hospitalPage()->isBusy());
        QPointer<MaskDlgShutDown> dialog = openConfirmation();
        QVERIFY(dialog);
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("btnShutDown")), Qt::LeftButton);
        QVERIFY(until([&]() { return dialog.isNull(); }));
        QVERIFY(host->isVisible() && host->shell->hospitalPage()->isBusy());
    }

    void keepUnsavedDraftRejectsExit()
    {
        QVERIFY(initializeData());
        auto *hospitalName = host->shell->findChild<QLineEdit *>(QStringLiteral("ldtHospName"));
        QVERIFY(hospitalName);
        hospitalName->setText(QStringLiteral("关机前未保存的匿名医院"));
        QVERIFY(host->shell->hospitalPage()->hasUnsavedChanges());
        QPointer<MaskDlgShutDown> dialog = openConfirmation();
        QVERIFY(dialog);
        bool answered = false;
        QTimer answer;
        answer.setInterval(10);
        connect(&answer, &QTimer::timeout, this, [&]()
        {
            auto *question = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (question && question->objectName() == QStringLiteral("discardChangesDialog"))
            {
                answered = true;
                answer.stop();
                question->button(QMessageBox::No)->click();
            }
        });
        answer.start();
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("btnShutDown")), Qt::LeftButton);
        QVERIFY(until([&]() { return answered && dialog.isNull(); }));
        QVERIFY(host->isVisible());
        QVERIFY(host->shell->hospitalPage()->hasUnsavedChanges());
        QCOMPARE(hospitalName->text(), QStringLiteral("关机前未保存的匿名医院"));
    }
};

QTEST_MAIN(ShutdownButtonRkTest)
#include "shutdown_button_rk_test.moc"
