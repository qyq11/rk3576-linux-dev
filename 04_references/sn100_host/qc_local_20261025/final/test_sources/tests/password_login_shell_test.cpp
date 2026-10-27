#include "data/account_store.h"
#include "data/hospital_store.h"
#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"
#include "ui/maskdlglogin.h"
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QWindow>
#include <QtTest>
#include <functional>

namespace
{
QStringList privateSecrets; // 仅运行内存，用于检查自有证据无泄漏；不序列化。
QString newSecret()
{
    const QString value = QUuid::createUuid().toString(QUuid::WithoutBraces);
    privateSecrets.append(value);
    return value;
}
bool until(const std::function<bool()> &predicate, int timeout = 15000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout)
    {
        QTest::qWait(10);
    }
    return predicate();
}
QByteArray hashFile(const QString &path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray();
}
bool writeEvidence(const QString &path, const QJsonObject &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) &&
           file.write(QJsonDocument(value).toJson()) > 0;
}
template <class T> T *child(QObject *root, const char *name)
{
    return root->findChild<T *>(QString::fromLatin1(name));
}
bool enter(QLineEdit *edit, const QString &text)
{
    if (!edit)
    {
        return false;
    }
    edit->setFocus();
    edit->selectAll();
    QTest::keyClick(edit, Qt::Key_Backspace);
    QTest::keyClicks(edit, text); // ASCII运行时输入；不使用系统剪贴板。
    return edit->text() == text;  // 不用QCOMPARE，失败诊断不能打印密码。
}
MaskDlgLogin *visibleLogin(frmMain *window)
{
    for (auto *dialog : window->findChildren<MaskDlgLogin *>())
    {
        if (dialog->isVisible())
        {
            return dialog;
        }
    }
    return nullptr;
}
bool loginReady(MaskDlgLogin *dialog)
{
    auto *mask = dialog ? child<CusDialogMask>(dialog, "mask") : nullptr;
    return dialog && dialog->isVisible() && !dialog->isBusy() && mask && mask->isReadyForInput();
}
bool acknowledgeLoginWarning(MaskDlgLogin *dialog)
{
    CusMessageBox *warning = nullptr;
    if (!until(
            [&]()
            {
                warning = child<CusMessageBox>(dialog, "loginWarningDialog");
                return warning && warning->isVisible() && warning->isReadyForInput();
            }))
    {
        return false;
    }
    auto *sure = child<QPushButton>(warning, "btnSure");
    if (!sure || !sure->isEnabled())
    {
        return false;
    }
    QTest::mouseClick(sure, Qt::LeftButton);
    return until([&]() { return loginReady(dialog); });
}
// 原医院草稿是标准QMessageBox；不对登录卡的原CusMessageBox动画强制done。
void answerHospitalDraft(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(30,
                       [answer]()
                       {
                           if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                           {
                               if (auto *button = dialog->button(answer))
                               {
                                   QTest::mouseClick(button, Qt::LeftButton);
                               }
                           }
                       });
}

class Session
{
  public:
    explicit Session(const QString &root)
        : hospital(new HospitalStore), accounts(new AccountStore), secret(newSecret())
    {
        HospitalStore::registerTypes();
        AccountStore::registerTypes();
        hospital->moveToThread(&hospitalThread);
        accounts->moveToThread(&accountThread);
        QObject::connect(&hospitalThread, &QThread::finished, hospital, &QObject::deleteLater);
        QObject::connect(&accountThread, &QThread::finished, accounts, &QObject::deleteLater);
        window = new frmMain(hospital, nullptr, accounts);
        const QString hospitalRoot = QDir(root).filePath("hospital");
        const QString accountRoot = QDir(root).filePath("account");
        window->setDataDirectory(hospitalRoot);
        host.setWidget(window);
        host.resize(1600, 900);
        host.show();
        QObject::connect(&hospitalThread, &QThread::started, hospital,
                         [=]()
                         {
                             hospital->initialize(QDir(hospitalRoot).filePath("hospital_Information.db"),
                                                  QDir(hospitalRoot).filePath("config.ini"));
                         });
        const QString bootstrap = secret;
        QObject::connect(&accountThread, &QThread::started, accounts,
                         [=]() { accounts->initialize(accountRoot, bootstrap); });
        hospitalThread.start();
        accountThread.start();
    }
    ~Session()
    {
        for (auto *dialog : window->findChildren<MaskDlgLogin *>())
        {
            if (dialog->isVisible() && !dialog->isBusy())
            {
                dialog->close();
            }
        }
        QMetaObject::invokeMethod(accounts, "close", Qt::BlockingQueuedConnection);
        QMetaObject::invokeMethod(hospital, "close", Qt::BlockingQueuedConnection);
        accountThread.quit();
        hospitalThread.quit();
        accountThread.wait();
        hospitalThread.wait();
        host.takeWidget();
        delete window;
        secret.clear();
    }
    bool ready()
    {
        return until(
            [&]()
            {
                return window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy() &&
                       window->accountPage()->isReady() && !window->accountPage()->isBusy();
            });
    }
    bool prepareHostForInput(const QSize &size, QWidget *target)
    {
        if (!target || visibleLogin(window))
        {
            return false;
        }
        host.resize(size);
        host.show();
        host.raise();
        host.activateWindow();
        host.ensureWidgetVisible(target);
        QList<QRect> previousGeometry;
        QPoint previousScroll;
        int stableTurns = 0;
        // XCB的resize/激活/滚动请求先于WM实际处理；每轮同步窗口系统并核实
        // 宿主输入条件，不能用登录卡的包含结果作为“等到通过”为止的条件。
        const bool prepared = until(
            [&]()
            {
                QApplication::sync();
                const QRect targetRect(target->mapTo(host.viewport(), QPoint()), target->size());
                const QList<QRect> geometry = {
                    QRect(host.mapToGlobal(QPoint()), host.size()), host.viewport()->geometry(),
                    window->geometry(), targetRect};
                const QPoint scroll(host.horizontalScrollBar()->value(), host.verticalScrollBar()->value());
                const bool inputReady = host.isVisible() && host.isActiveWindow() && host.windowHandle() &&
                    host.windowHandle()->isExposed() && host.size() == size && target->isVisible() &&
                    target->isEnabled() && host.viewport()->rect().contains(targetRect) &&
                    target->visibleRegion().contains(target->rect());
                stableTurns = inputReady && geometry == previousGeometry && scroll == previousScroll
                    ? stableTurns + 1 : 0;
                previousGeometry = geometry;
                previousScroll = scroll;
                return inputReady && stableTurns >= 2;
            });
        qInfo() << "LOGIN_HOST_INPUT_PREPARED" << prepared << "requested" << size
                << "host" << QRect(host.mapToGlobal(QPoint()), host.size())
                << "viewport" << host.viewport()->geometry() << "target" << previousGeometry.value(3)
                << "active" << host.isActiveWindow() << "scroll" << previousScroll
                << "stable_event_turns" << stableTurns;
        return prepared;
    }
    MaskDlgLogin *open()
    {
        if (!window->requestLogin())
        {
            return nullptr;
        }
        if (!until([&]() { return loginReady(visibleLogin(window)); }))
        {
            return nullptr;
        }
        return visibleLogin(window);
    }
    bool closed(MaskDlgLogin *dialog)
    {
        QPointer<MaskDlgLogin> guard(dialog);
        return until([&]() { return (!guard || !guard->isVisible()) && !window->accountPage()->isBusy(); });
    }
    QString defaultName()
    {
        QSignalSpy result(accounts, &AccountStore::defaultLoginLoaded);
        const quint64 request = 9000001;
        QMetaObject::invokeMethod(accounts, "loadLoginDefault", Qt::QueuedConnection,
                                  Q_ARG(quint64, request));
        if (!until([&]() { return !result.isEmpty(); }))
        {
            return {};
        }
        return result.last().at(1).toString();
    }
    QThread hospitalThread, accountThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    QString secret;
    frmMain *window;
    QScrollArea host;
};

class Product : public QProcess
{
  public:
    ~Product()
    {
        if (state() == QProcess::NotRunning)
        {
            return;
        }
        terminate();
        if (!until([&]() { return state() == QProcess::NotRunning; }, 10000))
        {
            kill();
            waitForFinished(5000);
        }
    }
};
} // namespace

class PasswordLoginShellTest : public QObject
{
    Q_OBJECT
  private slots:
    void initTestCase();
    void legacyOptInAndGuestLogin();
    void passwordSessionAndGuards();
    void fullProductAndInvalidMode();
    void cleanupTestCase();

  private:
    QString evidence_, runtime_, application_;
    QByteArray applicationHash_;
    bool evidenceCreated_ = false;
};

void PasswordLoginShellTest::initTestCase()
{
    QApplication::setStyle("Fusion");
    QApplication::setFont(QFont("Noto Sans CJK SC", 11));
    QApplication::setQuitOnLastWindowClosed(false);
    evidence_ = qEnvironmentVariable("SN100_LOGIN_EVIDENCE_DIR");
    runtime_ = qEnvironmentVariable("SN100_LOGIN_RUNTIME_DIR");
    application_ = qEnvironmentVariable("SN100_SHELL_APP");
    applicationHash_ = qgetenv("SN100_SHELL_SHA256");
    QVERIFY(QDir::isAbsolutePath(evidence_) && QDir::isAbsolutePath(runtime_));
    QVERIFY(!QFileInfo::exists(evidence_) && !QFileInfo::exists(runtime_));
    QVERIFY(QDir().mkpath(evidence_) && QDir().mkpath(runtime_));
    evidenceCreated_ = true;
    QVERIFY(QFileInfo(application_).isAbsolute() && QFileInfo(application_).isExecutable());
    QCOMPARE(applicationHash_.size(), 64);
    QCOMPARE(hashFile(application_), applicationHash_);
}

void PasswordLoginShellTest::legacyOptInAndGuestLogin()
{
    Session session(QDir(runtime_).filePath("legacy_guest"));
    QVERIFY(session.ready());
    auto *user = child<CusBadgeButton>(session.window, "btnUser");
    QVERIFY(user && !user->isEnabled());
    QVERIFY(!session.window->requestLogin());
    QVERIFY(!visibleLogin(session.window));
    QVERIFY(!session.window->accountSession().authenticated);
    QCOMPARE(session.window->accountSession().source, AccountSessionSource::Guest);
    QMetaObject::invokeMethod(session.accounts, "setSession", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("offlineCheck")), Q_ARG(bool, true));
    QVERIFY(until(
        [&]()
        {
            return session.window->accountSession().source == AccountSessionSource::Simulated &&
                   !session.window->accountPage()->isBusy();
        }));
    QCOMPARE(session.window->accountSession().role, 3);
    QVERIFY(!user->isEnabled());
    QMetaObject::invokeMethod(session.accounts, "setSession", Qt::QueuedConnection, Q_ARG(QString, QString()),
                              Q_ARG(bool, true));
    QVERIFY(until(
        [&]()
        {
            return session.window->accountSession().source == AccountSessionSource::Guest &&
                   !session.window->accountPage()->isBusy();
        }));
    QVERIFY(session.window->enablePasswordLogin());
    QTRY_VERIFY(user->isEnabled());
    const QString defaultName = session.defaultName();
    QVERIFY(!defaultName.isEmpty());
    for (const QSize size : {QSize(1600, 900), QSize(1920, 1080)})
    {
        QVERIFY(session.prepareHostForInput(size, user));
        QTest::mouseClick(user, Qt::LeftButton);
        QVERIFY(until([&]() { return loginReady(visibleLogin(session.window)); }));
        auto *dialog = visibleLogin(session.window);
        auto *password = child<QLineEdit>(dialog, "ldtPassword");
        auto *card = child<QFrame>(dialog, "view_frame");
        QVERIFY(password && card);
        QCOMPARE(password->echoMode(), QLineEdit::Password);
        QVERIFY(password->text().isEmpty());
        QCOMPARE(card->size(), QSize(450, 571));
        const QRect available(session.host.mapToGlobal(QPoint(0, 0)), session.host.size());
        const QRect cardRect(card->mapToGlobal(QPoint(0, 0)), card->size());
        qInfo() << "LOGIN_CARD_GEOMETRY" << "host" << available << "card" << cardRect
                << "dialog" << dialog->geometry() << "active" << dialog->isActiveWindow();
        QVERIFY(available.contains(cardRect));
        QVERIFY(dialog->grab().save(QDir(evidence_).filePath(QString("login_%1.png").arg(size.width()))));
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTest::qWait(40);
        QVERIFY(dialog->isVisible());
        dialog->close();
        QVERIFY(session.closed(dialog));

        dialog = session.open();
        QVERIFY(dialog);
        QVERIFY(enter(child<QLineEdit>(dialog, "ldtUserName"), "offlineEngineer"));
        child<QCheckBox>(dialog, "cbx_isdefault")->setChecked(true);
        QTest::mouseClick(child<QPushButton>(dialog, "btnLogin"), Qt::LeftButton);
        QVERIFY(session.closed(dialog));
        QVERIFY(!session.window->accountSession().authenticated);
        QCOMPARE(session.window->accountSession().source, AccountSessionSource::Guest);
        QCOMPARE(session.defaultName(), defaultName);
    }
    QVERIFY(writeEvidence(QDir(evidence_).filePath("legacy_guest.json"),
                          {{"legacy_button_disabled", true},
                           {"legacy_simulated_session_preserved", true},
                           {"opt_in_button_enabled", true},
                           {"card_size", QStringLiteral("450x571")},
                           {"viewports", QStringLiteral("1600x900,1920x1080")},
                           {"escape_does_not_close", true},
                           {"idle_window_close_animated", true},
                           {"empty_password_guest_without_default_reset", true}}));
}

void PasswordLoginShellTest::passwordSessionAndGuards()
{
    Session session(QDir(runtime_).filePath("password_guards"));
    QVERIFY(session.ready());
    QVERIFY(session.window->enablePasswordLogin());
    QMetaObject::invokeMethod(session.accounts, "setSession", Qt::QueuedConnection, Q_ARG(QString, QString()),
                              Q_ARG(bool, false));
    QVERIFY(until(
        [&]()
        {
            return !session.window->accountSession().actionReady && !session.window->accountPage()->isBusy();
        }));
    auto *hospitalName = child<QLineEdit>(session.window->hospitalPage(), "ldtHospName");
    QVERIFY(session.window->selectMainPage(6));
    QVERIFY(session.window->selectUserPage(1));
    hospitalName->setText("Saved hospital before login");
    QMetaObject::invokeMethod(session.hospital, []() { QThread::msleep(200); }, Qt::QueuedConnection);
    auto *hospitalSave = child<QPushButton>(session.window->hospitalPage(), "btnSave");
    session.host.ensureWidgetVisible(hospitalSave);
    QTest::mouseClick(hospitalSave, Qt::LeftButton);
    QVERIFY(session.window->hospitalPage()->isBusy());
    QVERIFY(!session.window->requestLogin());
    QVERIFY(until([&]() { return !session.window->hospitalPage()->isBusy(); }));
    QVERIFY(session.window->selectMainPage(0));
    const QString committed = hospitalName->text();
    hospitalName->setText("Hidden login draft");
    QVERIFY(session.window->hospitalPage()->hasUnsavedChanges());
    answerHospitalDraft(QMessageBox::No);
    QVERIFY(!session.window->requestLogin());
    QCOMPARE(hospitalName->text(), QString("Hidden login draft"));
    answerHospitalDraft(QMessageBox::Yes);
    MaskDlgLogin *dialog = session.open();
    QVERIFY(dialog);
    QCOMPARE(hospitalName->text(), committed);
    QVERIFY(!session.window->hospitalPage()->hasUnsavedChanges());

    auto *crudName = child<QLineEdit>(session.window->accountPage(), "ldtUserNameAdd");
    crudName->setText("PendingAccountAlias");
    QSignalSpy accountErrors(session.accounts, &AccountStore::error);
    QSignalSpy accountOutcomes(session.window->accountPage(), &frmAccountSetting::operationFinished);
    QSignalSpy loginResults(session.accounts, &AccountStore::loginFinished);
    const QString wrong = newSecret();
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtUserName"), "offlineEngineer"));
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtPassword"), wrong));
    QTest::mouseClick(child<QPushButton>(dialog, "btnLogin"), Qt::LeftButton);
    QVERIFY(until([&]() { return loginResults.size() == 1; }));
    QVERIFY(!loginResults.first().at(1).toBool());
    QVERIFY(!session.window->accountSession().authenticated);
    QVERIFY(acknowledgeLoginWarning(dialog));
    QCOMPARE(accountErrors.size(), 0);
    for (const auto &outcome : accountOutcomes)
    {
        QVERIFY(outcome.at(1).toBool());
    }
    QVERIFY(!session.window->accountPage()->isBusy());
    QCOMPARE(crudName->text(), QString("PendingAccountAlias"));
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtPassword"), session.secret));
    QTest::mouseClick(child<QPushButton>(dialog, "btnLogin"), Qt::LeftButton);
    QVERIFY(session.closed(dialog));
    QVERIFY(session.window->accountSession().authenticated);
    QCOMPARE(session.window->accountSession().name, QString("offlineEngineer"));
    QCOMPARE(session.window->accountSession().role, 4);
    QCOMPARE(session.window->accountSession().source, AccountSessionSource::Password);
    QVERIFY(!session.window->accountSession().actionReady);

    dialog = session.open();
    QVERIFY(dialog);
    const int beforeRejectedRelogin = loginResults.size();
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtUserName"), "offlineTest"));
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtPassword"), wrong));
    QTest::mouseClick(child<QPushButton>(dialog, "btnLogin"), Qt::LeftButton);
    QVERIFY(until([&]() { return loginResults.size() == beforeRejectedRelogin + 1; }));
    QVERIFY(!loginResults.last().at(1).toBool());
    QVERIFY(acknowledgeLoginWarning(dialog));
    QCOMPARE(session.window->accountSession().name, QString("offlineEngineer"));
    QCOMPARE(session.window->accountSession().role, 4);
    QCOMPARE(session.window->accountSession().source, AccountSessionSource::Password);
    QVERIFY(!session.window->accountSession().actionReady);
    // 原卡仍空闲时requestLogin只raise已有卡；不得再创建第二个登录窗口。
    QVERIFY(session.window->requestLogin());
    QCOMPARE(visibleLogin(session.window), dialog);
    QVERIFY(!session.window->selectMainPage(2));
    QVERIFY(!session.window->selectUserPage(1));
    QVERIFY(!session.window->requestLogout());
    QVERIFY(!session.window->close());
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtUserName"), "offlineEngineer"));
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtPassword"), session.secret));
    const int before = loginResults.size();
    QMetaObject::invokeMethod(session.accounts, []() { QThread::msleep(200); }, Qt::QueuedConnection);
    auto *submit = child<QPushButton>(dialog, "btnLogin");
    QTest::mouseClick(submit, Qt::LeftButton);
    QVERIFY(dialog->isBusy() && !submit->isEnabled());
    QTest::mouseClick(submit, Qt::LeftButton);
    QVERIFY(!session.window->requestLogin());
    QVERIFY(!session.window->selectMainPage(2));
    QVERIFY(!session.window->selectUserPage(1));
    QVERIFY(!session.window->requestLogout());
    QVERIFY(!session.window->close());
    QVERIFY(session.closed(dialog));
    QCOMPARE(loginResults.size(), before + 1);
    QVERIFY(session.window->accountSession().authenticated);
    QVERIFY(!session.window->accountSession().actionReady);
    QVERIFY(session.window->requestLogout());
    QVERIFY(until(
        [&]()
        {
            return !session.window->accountSession().authenticated &&
                   !session.window->accountPage()->isBusy();
        }));
    QCOMPARE(session.window->accountSession().source, AccountSessionSource::Guest);
    dialog = session.open();
    QVERIFY(dialog);
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtUserName"), "offlineTest"));
    QVERIFY(enter(child<QLineEdit>(dialog, "ldtPassword"), session.secret));
    QTest::mouseClick(child<QPushButton>(dialog, "btnLogin"), Qt::LeftButton);
    QVERIFY(session.closed(dialog));
    QCOMPARE(session.window->accountSession().role, 2);
    QCOMPARE(session.window->accountSession().source, AccountSessionSource::Password);
    QVERIFY(!session.window->accountSession().actionReady);
    QVERIFY(writeEvidence(QDir(evidence_).filePath("session_guards.json"),
                          {{"hidden_hospital_draft_cancel_and_discard", true},
                           {"database_busy_rejects_open", true},
                           {"wrong_password_stays_guest", true},
                           {"wrong_relogin_preserves_authenticated_identity", true},
                           {"hidden_account_crud_not_polluted", true},
                           {"password_session_role4_then_role2", true},
                           {"source", QStringLiteral("Password")},
                           {"action_ready_preserved", false},
                           {"active_and_busy_navigation_logout_close_guarded", true},
                           {"duplicate_submit_rejected", true},
                           {"logout_then_password_login", true}}));
}

void PasswordLoginShellTest::fullProductAndInvalidMode()
{
    QTemporaryFile secret(QDir(runtime_).filePath("bootstrap_XXXXXX.secret"));
    QVERIFY(secret.open());
    const QByteArray value = newSecret().toUtf8();
    QCOMPARE(secret.write(value), qint64(value.size()));
    QVERIFY(secret.flush());
    secret.close();
    Product product;
    product.setProcessChannelMode(QProcess::MergedChannels);
    product.start(application_, {"--offline", "--accounts", "--history", "--catalog", "--samples",
                                 "--instrument", "--status-loopback", "--password-login", "--seed-anonymous",
                                 "--seed-catalog-demo", "--test-secret-file", secret.fileName(), "--data-dir",
                                 QDir(runtime_).filePath("full_product"), "--viewport", "1600x900"});
    QVERIFY(product.waitForStarted(10000));
    QByteArray output;
    const auto collect = [&]()
    {
        output += product.readAll();
        return output.contains("SHELL_READY") && output.contains("STATUS_LOOPBACK_RX");
    };
    const bool ready = until(collect, 25000);
    QVERIFY(secret.remove());
    product.terminate();
    const bool stopped = until(
        [&]()
        {
            collect();
            return product.state() == QProcess::NotRunning;
        },
        15000);
    if (!stopped)
    {
        product.kill();
        product.waitForFinished(5000);
    }
    collect();
    const bool leaked = output.contains(value);
    output.replace(value, "<redacted-transient-secret>");
    QFile log(QDir(evidence_).filePath("product.log"));
    QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QCOMPARE(log.write(output), qint64(output.size()));
    log.close();
    QVERIFY2(ready && stopped, output.constData());
    QCOMPARE(product.exitStatus(), QProcess::NormalExit);
    QCOMPARE(product.exitCode(), 0);
    QVERIFY(!leaked);
    for (const char *marker : {"STATUS_LOOPBACK_STOPPED true", "SAMPLE_STORE_CLOSED", "PROJECT_STORE_CLOSED",
                               "REAGENT_STORE_CLOSED", "HISTORY_STORE_CLOSED", "ACCOUNT_STORE_CLOSED",
                               "HOSPITAL_STORE_CLOSED", "MAINTENANCE_STORE_CLOSED", "THREADS_STOPPED true"})
    {
        QVERIFY2(output.contains(marker), marker);
    }
    QCOMPARE(hashFile(application_), applicationHash_);
    const QString rejectedRoot = QDir(runtime_).filePath("must_not_initialize");
    Product invalid;
    invalid.setProcessChannelMode(QProcess::MergedChannels);
    invalid.start(application_, {"--offline", "--password-login", "--data-dir", rejectedRoot});
    QVERIFY(invalid.waitForStarted(10000));
    QVERIFY(until([&]() { return invalid.state() == QProcess::NotRunning; }));
    QCOMPARE(invalid.exitStatus(), QProcess::NormalExit);
    QCOMPARE(invalid.exitCode(), 2);
    QVERIFY(!QFileInfo::exists(rejectedRoot));
    QJsonObject result{{"application_sha256", QString::fromLatin1(applicationHash_)},
                       {"normal_exit", 0},
                       {"stores_closed", 7},
                       {"loopback_stopped", true},
                       {"transient_secret_removed_and_not_logged", true},
                       {"requires_accounts_exit", 2},
                       {"rejected_directory_created", false}};
    QFile record(QDir(evidence_).filePath("product_lifecycle.json"));
    QVERIFY(record.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QVERIFY(record.write(QJsonDocument(result).toJson()) > 0);
}

void PasswordLoginShellTest::cleanupTestCase()
{
    if (!evidenceCreated_)
    {
        privateSecrets.clear();
        return;
    }
    QDirIterator files(evidence_, {"*.log", "*.json", "*.txt"}, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        QFile file(files.next());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        for (const QString &secret : privateSecrets)
        {
            QVERIFY(!bytes.contains(secret.toUtf8()));
        }
    }
    privateSecrets.clear();
}

QTEST_MAIN(PasswordLoginShellTest)
#include "password_login_shell_test.moc"
