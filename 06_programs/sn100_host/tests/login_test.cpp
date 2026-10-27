#include <QtTest>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <functional>

#include "data/account_store.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"
#include "ui/maskdlglogin.h"

namespace
{
QString transientSecret()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool waitUntil(const std::function<bool()> &predicate, int timeout = 6000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }
    return predicate();
}

bool withDatabase(const QString &directory, const std::function<bool(QSqlDatabase &)> &operation)
{
    const QString connection = QStringLiteral("login-fixture-") + transientSecret();
    bool result = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(QDir(directory).filePath(QStringLiteral("user_Information.db")));
        if (database.open())
        {
            result = operation(database);
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

bool insertAccount(const QString &directory, const QString &name, const QString &password, int role)
{
    return withDatabase(directory, [&](QSqlDatabase &database)
    {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("INSERT INTO user_information(name,pwd,role,isdefault) VALUES(:name,:pwd,:role,0)"));
        query.bindValue(QStringLiteral(":name"), name);
        query.bindValue(QStringLiteral(":pwd"), password);
        query.bindValue(QStringLiteral(":role"), role);
        return query.exec();
    });
}

QStringList defaultNames(const QString &directory)
{
    QStringList names;
    withDatabase(directory, [&](QSqlDatabase &database)
    {
        QSqlQuery query(database);
        if (!query.exec(QStringLiteral("SELECT name FROM user_information WHERE isdefault=1 ORDER BY id")))
        {
            return false;
        }
        while (query.next())
        {
            names.append(query.value(0).toString());
        }
        return true;
    });
    return names;
}

class Worker
{
public:
    Worker() : store(new AccountStore), ready(store, &AccountStore::ready),
        errors(store, &AccountStore::error), successes(store, &AccountStore::succeeded),
        sessions(store, &AccountStore::sessionChanged), defaults(store, &AccountStore::defaultLoginLoaded),
        logins(store, &AccountStore::loginFinished)
    {
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        thread.start();
    }
    ~Worker()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    bool initialize(const QString &directory, const QString &password = QString())
    {
        ready.clear();
        const bool invoked = QMetaObject::invokeMethod(store, "initialize", Qt::BlockingQueuedConnection,
            Q_ARG(QString, directory), Q_ARG(QString, password));
        QCoreApplication::processEvents();
        return invoked && ready.size() == 1;
    }
    bool simulate(const QString &name, bool actionReady)
    {
        const bool invoked = QMetaObject::invokeMethod(store, "setSession", Qt::BlockingQueuedConnection,
            Q_ARG(QString, name), Q_ARG(bool, actionReady));
        QCoreApplication::processEvents();
        return invoked;
    }
    bool authenticate(const QString &name, const QString &password, bool remember = false)
    {
        logins.clear();
        const bool invoked = QMetaObject::invokeMethod(store, "authenticate", Qt::BlockingQueuedConnection,
            Q_ARG(quint64, ++sequence), Q_ARG(QString, name), Q_ARG(QString, password), Q_ARG(bool, remember));
        QCoreApplication::processEvents();
        return invoked && logins.size() == 1;
    }
    AccountSession session() const
    {
        return sessions.isEmpty() ? AccountSession() : qvariant_cast<AccountSession>(sessions.last().at(0));
    }
    bool accepted() const { return logins.last().at(1).toBool(); }
    bool defaultSaved() const { return logins.last().at(2).toBool(); }
    QString error() const { return logins.last().at(3).toString(); }
    QString warning() const { return logins.last().at(4).toString(); }
    bool readDefault()
    {
        defaults.clear();
        const bool invoked = QMetaObject::invokeMethod(store, "loadLoginDefault", Qt::BlockingQueuedConnection,
            Q_ARG(quint64, ++sequence));
        QCoreApplication::processEvents();
        return invoked && defaults.size() == 1;
    }
    QThread thread;
    AccountStore *store;
    QSignalSpy ready, errors, successes, sessions, defaults, logins;
    quint64 sequence = 100000;
};

bool acknowledgeWarning(MaskDlgLogin &login)
{
    QPointer<CusMessageBox> warning;
    if (!waitUntil([&]()
    {
        warning = login.findChild<CusMessageBox *>(QStringLiteral("loginWarningDialog"));
        return warning && warning->isVisible() && warning->isReadyForInput();
    }))
    {
        return false;
    }
    auto *button = warning->findChild<QPushButton *>(QStringLiteral("btnSure"));
    if (!button)
    {
        return false;
    }
    QTest::mouseClick(button, Qt::LeftButton);
    return waitUntil([&]() { return !warning || !warning->isVisible(); });
}

bool interactive(MaskDlgLogin &login)
{
    auto *mask = login.findChild<CusDialogMask *>(QStringLiteral("mask"));
    return mask && waitUntil([&]() { return !login.isBusy() && mask->isReadyForInput(); });
}
}

class LoginTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void credentialsAndSessionSources();
    void failuresPreserveIdentity();
    void matchingCredentialsAndAmbiguity();
    void emptyPasswordGuestIgnoresDefault();
    void defaultNamesReopenAndRoleRules();
    void defaultWriteFailureRollsBack();
    void databaseLocksAndIndependentSignals();
    void dialogFailureBusyEscapeAndReopen();
    void dialogSuccessAndGuest();
    void dialogWarningAndStaleReplies();
private:
    QString fixture() const;
    QString output_;
};

void LoginTest::initTestCase()
{
    output_ = qEnvironmentVariable("SN100_LOGIN_EVIDENCE_DIR");
    QVERIFY(QDir::isAbsolutePath(output_));
    QVERIFY(!QFileInfo::exists(QDir(output_).filePath(QStringLiteral("login_test_started.tag"))));
    QVERIFY(QDir().mkpath(output_));
    QFile tag(QDir(output_).filePath(QStringLiteral("login_test_started.tag")));
    QVERIFY(tag.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QVERIFY(tag.write("anonymous local login tests; no stored authentication material archived\n") > 0);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
}

QString LoginTest::fixture() const
{
    return QDir(output_).filePath(QString::fromLatin1(QTest::currentTestFunction()));
}

void LoginTest::credentialsAndSessionSources()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(!worker.session().authenticated);
    QVERIFY(worker.session().source == AccountSessionSource::Guest);
    QVERIFY(worker.simulate(QStringLiteral("offlineEngineer"), false));
    QVERIFY(worker.session().source == AccountSessionSource::Simulated);
    const QStringList users{QStringLiteral("offlineSend"), QStringLiteral("offlineTest"),
        QStringLiteral("offlineCheck"), QStringLiteral("offlineEngineer"), QStringLiteral("offlineAdmin")};
    for (int index = 0; index < users.size(); ++index)
    {
        QVERIFY(worker.authenticate(users.at(index), password));
        QVERIFY(worker.accepted());
        QCOMPARE(worker.session().role, index + 1);
        QVERIFY(worker.session().authenticated);
        QVERIFY(worker.session().source == AccountSessionSource::Password);
        QVERIFY(!worker.session().actionReady);
    }
    QVERIFY(insertAccount(fixture(), QStringLiteral("roleZero"), password, 0));
    QVERIFY(worker.authenticate(QStringLiteral("roleZero"), password));
    QVERIFY(worker.accepted());
    QCOMPARE(worker.session().role, 0);
    QVERIFY(worker.session().authenticated);
    QVERIFY(worker.session().source == AccountSessionSource::Password);
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
}

void LoginTest::failuresPreserveIdentity()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(worker.simulate(QStringLiteral("offlineEngineer"), false));
    const int sessionEvents = worker.sessions.size();
    for (const auto &input : QList<QPair<QString, QString>>{
        {QStringLiteral("missingUser"), password},
        {QStringLiteral("offlineAdmin"), transientSecret()},
        {QString(), password}, {QString(QChar::Null), password}})
    {
        QVERIFY(worker.authenticate(input.first, input.second));
        QVERIFY(!worker.accepted() && !worker.error().isEmpty());
        QVERIFY(!worker.error().contains(password));
        QCOMPARE(worker.session().name, QStringLiteral("offlineEngineer"));
        QCOMPARE(worker.session().role, 4);
        QVERIFY(!worker.session().actionReady);
        QVERIFY(worker.session().source == AccountSessionSource::Simulated);
        QCOMPARE(worker.sessions.size(), sessionEvents);
    }
    QVERIFY(insertAccount(fixture(), QStringLiteral("invalidRole"), password, 9));
    QVERIFY(worker.authenticate(QStringLiteral("invalidRole"), password));
    QVERIFY(!worker.accepted());
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
}

void LoginTest::matchingCredentialsAndAmbiguity()
{
    const QString password = transientSecret();
    const QString otherPassword = transientSecret();
    const QString quotedPassword = transientSecret() + QLatin1Char('\'');
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(insertAccount(fixture(), QStringLiteral("duplicate"), password, 1));
    QVERIFY(insertAccount(fixture(), QStringLiteral("duplicate"), otherPassword, 3));
    QVERIFY(worker.authenticate(QStringLiteral("duplicate"), otherPassword));
    QVERIFY(worker.accepted());
    QCOMPARE(worker.session().role, 3);
    // 删除另一条同名行后，原CRUD仍能核对认证实际选中的id。
    QVERIFY(withDatabase(fixture(), [](QSqlDatabase &database)
    {
        QSqlQuery query(database);
        return query.exec(QStringLiteral("DELETE FROM user_information WHERE name='duplicate' AND role=1"));
    }));
    QVERIFY(QMetaObject::invokeMethod(worker.store, "add", Qt::BlockingQueuedConnection,
        Q_ARG(QString, QStringLiteral("idVerified")), Q_ARG(QString, password), Q_ARG(QString, password), Q_ARG(int, 1)));
    QCoreApplication::processEvents();
    QCOMPARE(worker.successes.size(), 1);
    QVERIFY(insertAccount(fixture(), QStringLiteral("duplicate"), otherPassword, 5));
    const int sessionEvents = worker.sessions.size();
    QVERIFY(worker.authenticate(QStringLiteral("duplicate"), otherPassword));
    QVERIFY(!worker.accepted() && worker.error().contains(QStringLiteral("歧义")));
    QCOMPARE(worker.sessions.size(), sessionEvents);
    QVERIFY(insertAccount(fixture(), QStringLiteral("quote'name"), quotedPassword, 2));
    QVERIFY(worker.authenticate(QStringLiteral("quote'name"), quotedPassword));
    QVERIFY(worker.accepted());
    QCOMPARE(worker.session().role, 2);
    QVERIFY(worker.authenticate(QStringLiteral("' OR 1=1 --"), quotedPassword));
    QVERIFY(!worker.accepted());
}

void LoginTest::emptyPasswordGuestIgnoresDefault()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(worker.authenticate(QStringLiteral("offlineTest"), password, true));
    QVERIFY(worker.defaultSaved());
    const QStringList defaults = defaultNames(fixture());
    QVERIFY(worker.simulate(QStringLiteral("offlineAdmin"), false));
    QVERIFY(worker.authenticate(QString(QChar::Null), QString(), true));
    QVERIFY(worker.accepted() && !worker.defaultSaved());
    QVERIFY(!worker.session().authenticated && worker.session().role == 0);
    QVERIFY(worker.session().source == AccountSessionSource::Guest);
    QVERIFY(!worker.session().actionReady);
    QCOMPARE(defaultNames(fixture()), defaults);
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
}

void LoginTest::defaultNamesReopenAndRoleRules()
{
    const QString password = transientSecret();
    {
        Worker worker;
        QVERIFY(worker.initialize(fixture(), password));
        QVERIFY(worker.readDefault());
        QCOMPARE(worker.defaults.last().at(1).toString(), QStringLiteral("Guest"));
        QVERIFY(worker.authenticate(QStringLiteral("offlineEngineer"), password, true));
        QVERIFY(worker.accepted() && worker.defaultSaved());
        const QStringList defaults = defaultNames(fixture());
        QVERIFY(worker.authenticate(QStringLiteral("offlineAdmin"), password, true));
        QVERIFY(worker.accepted() && !worker.defaultSaved());
        QCOMPARE(defaultNames(fixture()), defaults);
        QVERIFY(worker.authenticate(QStringLiteral("offlineCheck"), password, false));
        QVERIFY(worker.accepted() && !worker.defaultSaved());
        QCOMPARE(defaultNames(fixture()), defaults);
        QVERIFY(insertAccount(fixture(), QStringLiteral("offlineEngineer"), transientSecret(), 1));
        QVERIFY(worker.authenticate(QStringLiteral("offlineEngineer"), password, true));
        QVERIFY(worker.defaultSaved());
        QCOMPARE(defaultNames(fixture()).size(), 2);
    }
    Worker reopened;
    QVERIFY(reopened.initialize(fixture()));
    QVERIFY(reopened.readDefault());
    QCOMPARE(reopened.defaults.last().at(1).toString(), QStringLiteral("offlineEngineer"));
    QVERIFY(!reopened.session().authenticated);
    QVERIFY(reopened.authenticate(QStringLiteral("offlineSend"), password, true));
    QVERIFY(reopened.accepted() && reopened.defaultSaved());
    QCOMPARE(defaultNames(fixture()), QStringList{QStringLiteral("offlineSend")});
}

void LoginTest::defaultWriteFailureRollsBack()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(worker.authenticate(QStringLiteral("offlineTest"), password, true));
    const QStringList before = defaultNames(fixture());
    QVERIFY(withDatabase(fixture(), [](QSqlDatabase &database)
    {
        QSqlQuery query(database);
        return query.exec(QStringLiteral("CREATE TRIGGER refuse_default BEFORE UPDATE OF isdefault ON user_information "
            "WHEN NEW.isdefault=1 AND NEW.name='offlineEngineer' BEGIN SELECT RAISE(ABORT,'controlled default failure'); END"));
    }));
    QVERIFY(worker.authenticate(QStringLiteral("offlineEngineer"), password, true));
    QVERIFY(worker.accepted() && !worker.defaultSaved());
    QVERIFY(worker.error().isEmpty() && !worker.warning().isEmpty());
    QCOMPARE(defaultNames(fixture()), before);
    QCOMPARE(worker.session().role, 4);
    QVERIFY(worker.session().source == AccountSessionSource::Password);
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
}

void LoginTest::databaseLocksAndIndependentSignals()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(worker.authenticate(QStringLiteral("offlineTest"), password, true));
    const QStringList before = defaultNames(fixture());
    QVERIFY(withDatabase(fixture(), [&](QSqlDatabase &database)
    {
        QSqlQuery lock(database);
        if (!lock.exec(QStringLiteral("BEGIN IMMEDIATE"))) return false;
        const bool checked = worker.authenticate(QStringLiteral("offlineEngineer"), password, true)
            && worker.accepted() && !worker.defaultSaved() && !worker.warning().isEmpty();
        database.rollback();
        return checked;
    }));
    QCOMPARE(defaultNames(fixture()), before);
    QVERIFY(withDatabase(fixture(), [&](QSqlDatabase &database)
    {
        QSqlQuery lock(database);
        if (!lock.exec(QStringLiteral("BEGIN EXCLUSIVE"))) return false;
        const int sessionEvents = worker.sessions.size();
        const bool checked = worker.authenticate(QStringLiteral("offlineAdmin"), password)
            && !worker.accepted() && worker.sessions.size() == sessionEvents && !worker.error().isEmpty()
            && worker.readDefault() && !worker.defaults.last().at(2).toString().isEmpty();
        database.rollback();
        return checked;
    }));
    QVERIFY(worker.authenticate(QStringLiteral("offlineEngineer"), password, true));
    QVERIFY(worker.defaultSaved());
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
    // 错误线程与已关闭服务只发认证通道，旧账户CRUD观察者不受污染。
    worker.store->authenticate(++worker.sequence, QStringLiteral("offlineAdmin"), password, false);
    QVERIFY(!worker.accepted());
    QVERIFY(QMetaObject::invokeMethod(worker.store, "close", Qt::BlockingQueuedConnection));
    QVERIFY(worker.authenticate(QStringLiteral("offlineAdmin"), password));
    QVERIFY(!worker.accepted());
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
}

void LoginTest::dialogFailureBusyEscapeAndReopen()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(worker.simulate(QStringLiteral("offlineEngineer"), false));
    QWidget host;
    host.resize(1600, 900);
    host.show();
    MaskDlgLogin login(worker.store, &host);
    QSignalSpy finished(&login, &QDialog::finished);
    QElapsedTimer openClock;
    openClock.start();
    login.showDialog();
    QVERIFY(openClock.elapsed() < 100);
    QVERIFY(interactive(login));
    QVERIFY(host.grab().save(QDir(output_).filePath(QStringLiteral("login_host_empty.png"))));
    QVERIFY(login.grab().save(QDir(output_).filePath(QStringLiteral("login_card_empty.png"))));
    auto *name = login.findChild<QLineEdit *>(QStringLiteral("ldtUserName"));
    auto *secret = login.findChild<QLineEdit *>(QStringLiteral("ldtPassword"));
    auto *submit = login.findChild<QPushButton *>(QStringLiteral("btnLogin"));
    QVERIFY(name && secret && submit);
    QCOMPARE(secret->echoMode(), QLineEdit::Password);
    QTest::keyClick(&login, Qt::Key_Escape);
    QVERIFY(login.isVisible() && finished.isEmpty());
    name->setText(QStringLiteral("offlineAdmin"));
    QTest::keyClicks(secret, transientSecret());
    const QString incorrect = secret->text();
    QVERIFY(QMetaObject::invokeMethod(worker.store, []() { QThread::msleep(120); }, Qt::QueuedConnection));
    const int replies = worker.logins.size();
    QTest::mouseClick(submit, Qt::LeftButton);
    QVERIFY(login.isBusy());
    QVERIFY(!login.close());
    QVERIFY(login.isVisible());
    QVERIFY(QMetaObject::invokeMethod(&login, "submitLogin", Qt::DirectConnection));
    QVERIFY(acknowledgeWarning(login));
    QVERIFY(interactive(login));
    QCOMPARE(worker.logins.size(), replies + 1);
    QVERIFY(secret->text() == incorrect);
    QCOMPARE(worker.session().name, QStringLiteral("offlineEngineer"));
    login.reject();
    QVERIFY(waitUntil([&]() { return finished.size() == 1; }));
    QVERIFY(!login.isBusy());
    QVERIFY(secret->text().isEmpty());
    MaskDlgLogin reopened(worker.store, &host);
    reopened.showDialog();
    QVERIFY(interactive(reopened));
    QVERIFY(reopened.findChild<QLineEdit *>(QStringLiteral("ldtPassword"))->text().isEmpty());
    reopened.reject();
    QVERIFY(waitUntil([&]() { return !reopened.isVisible(); }));
}

void LoginTest::dialogSuccessAndGuest()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QWidget host;
    host.resize(1920, 1080);
    host.show();
    MaskDlgLogin login(worker.store, &host);
    QSignalSpy finished(&login, &QDialog::finished);
    login.showDialog();
    QVERIFY(interactive(login));
    login.findChild<QLineEdit *>(QStringLiteral("ldtUserName"))->setText(QStringLiteral("offlineEngineer"));
    QTest::keyClicks(login.findChild<QLineEdit *>(QStringLiteral("ldtPassword")), password);
    login.findChild<QCheckBox *>(QStringLiteral("cbx_isdefault"))->setChecked(true);
    QTest::mouseClick(login.findChild<QPushButton *>(QStringLiteral("btnLogin")), Qt::LeftButton);
    QVERIFY(waitUntil([&]() { return finished.size() == 1; }));
    QCOMPARE(finished.last().at(0).toInt(), int(QDialog::Accepted));
    QVERIFY(worker.session().source == AccountSessionSource::Password);
    QVERIFY(login.findChild<QLineEdit *>(QStringLiteral("ldtPassword"))->text().isEmpty());
    QCOMPARE(defaultNames(fixture()), QStringList{QStringLiteral("offlineEngineer")});
    MaskDlgLogin guest(worker.store, &host);
    QSignalSpy guestFinished(&guest, &QDialog::finished);
    guest.showDialog();
    QVERIFY(interactive(guest));
    QCOMPARE(guest.findChild<QLineEdit *>(QStringLiteral("ldtUserName"))->text(), QStringLiteral("offlineEngineer"));
    guest.findChild<QCheckBox *>(QStringLiteral("cbx_isdefault"))->setChecked(true);
    QTest::mouseClick(guest.findChild<QPushButton *>(QStringLiteral("btnLogin")), Qt::LeftButton);
    QVERIFY(waitUntil([&]() { return guestFinished.size() == 1; }));
    QVERIFY(!worker.session().authenticated);
    QVERIFY(worker.session().source == AccountSessionSource::Guest);
    QCOMPARE(defaultNames(fixture()), QStringList{QStringLiteral("offlineEngineer")});
}

void LoginTest::dialogWarningAndStaleReplies()
{
    const QString password = transientSecret();
    Worker worker;
    QVERIFY(worker.initialize(fixture(), password));
    QVERIFY(withDatabase(fixture(), [](QSqlDatabase &database)
    {
        QSqlQuery query(database);
        return query.exec(QStringLiteral("CREATE TRIGGER refuse_default BEFORE UPDATE OF isdefault ON user_information "
            "WHEN NEW.isdefault=1 BEGIN SELECT RAISE(ABORT,'controlled default failure'); END"));
    }));
    QWidget host;
    host.resize(1600, 900);
    host.show();
    MaskDlgLogin login(worker.store, &host);
    QSignalSpy finished(&login, &QDialog::finished);
    login.showDialog();
    QVERIFY(interactive(login));
    auto *name = login.findChild<QLineEdit *>(QStringLiteral("ldtUserName"));
    auto *secret = login.findChild<QLineEdit *>(QStringLiteral("ldtPassword"));
    name->setText(QStringLiteral("offlineEngineer"));
    // 空闲及旧请求号回复不能覆盖已经编辑的用户名，也不能关闭弹窗。
    emit worker.store->defaultLoginLoaded(0, QStringLiteral("oldDefault"), QString());
    emit worker.store->loginFinished(0, true, false, QString(), QString());
    QCoreApplication::processEvents();
    QCOMPARE(name->text(), QStringLiteral("offlineEngineer"));
    QVERIFY(login.isVisible() && finished.isEmpty());
    QTest::keyClicks(secret, password);
    login.findChild<QCheckBox *>(QStringLiteral("cbx_isdefault"))->setChecked(true);
    QTest::mouseClick(login.findChild<QPushButton *>(QStringLiteral("btnLogin")), Qt::LeftButton);
    QVERIFY(waitUntil([&]() { return worker.session().source == AccountSessionSource::Password; }));
    QVERIFY(login.isVisible() && finished.isEmpty());
    QVERIFY(login.isBusy());
    QVERIFY(!login.close());
    QVERIFY(acknowledgeWarning(login));
    QVERIFY(waitUntil([&]() { return finished.size() == 1; }));
    QCOMPARE(finished.last().at(0).toInt(), int(QDialog::Accepted));
    QVERIFY(worker.accepted() && !worker.defaultSaved() && !worker.warning().isEmpty());
    QVERIFY(defaultNames(fixture()).isEmpty());
    QVERIFY(worker.errors.isEmpty() && worker.successes.isEmpty());
}

QTEST_MAIN(LoginTest)
#include "login_test.moc"
