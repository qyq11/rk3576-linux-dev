#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QStackedWidget>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QValidator>
#include <QtMath>
#include <algorithm>
#include <ctime>
#include <functional>
#include "data/account_store.h"
#include "ui/frmaccountsetting.h"
#include "ui/cusmessagebox.h"

namespace
{
bool waitFor(const std::function<bool()> &predicate, int timeout = 10000)
{
    QElapsedTimer clock;
    clock.start();
    while (!predicate() && clock.elapsed() < timeout)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(1);
    }
    return predicate();
}

bool saveJson(const QString &path, const QJsonObject &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly)
            && file.write(QJsonDocument(value).toJson()) > 0;
}

QJsonObject publicSnapshot(const QString &path)
{
    const QString connection = QUuid::createUuid().toString();
    QJsonObject result;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (database.open())
        {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next())
            {
                result[QStringLiteral("integrity_check")] = query.value(0).toString();
            }
            QJsonArray rows;
            if (query.exec(QStringLiteral("SELECT id,name,role,isdefault FROM user_information ORDER BY id")))
            {
                while (query.next())
                {
                    rows.append(QJsonObject{{QStringLiteral("id"), query.value(0).toInt()},
                        {QStringLiteral("name"), query.value(1).toString()},
                        {QStringLiteral("role"), query.value(2).toInt()},
                        {QStringLiteral("isdefault"), query.value(3).toInt()}});
                }
                result[QStringLiteral("rows")] = rows;
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

// Test driver answers actual modal windows by clicking their actual buttons.
// No password text or password-derived digest is included in observations.
class DialogAnswers : public QObject
{
public:
    DialogAnswers()
    {
        timer.setInterval(5);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            auto *box = qobject_cast<CusMessageBox *>(QApplication::activeModalWidget());
            if (!box || !box->isReadyForInput())
            {
                return;
            }
            auto *cancel = box->findChild<QPushButton *>(QStringLiteral("btnCancel"));
            const bool question = cancel && cancel->isVisible();
            const auto answer = question ? nextQuestion : QMessageBox::Ok;
            auto *button = box->findChild<QPushButton *>(answer == QMessageBox::No
                ? QStringLiteral("btnCancel") : QStringLiteral("btnSure"));
            if (button)
            {
                if (!capturePath.isEmpty())
                {
                    box->grab().save(capturePath);
                    capturePath.clear();
                }
                nextQuestion = QMessageBox::Yes;
                QTest::mouseClick(button, Qt::LeftButton);
            }
        });
        timer.start();
    }
    QTimer timer;
    QMessageBox::StandardButton nextQuestion = QMessageBox::Yes;
    QStringList messages;
    QString capturePath;
};

QJsonObject resourceSample()
{
    QJsonObject result;
    for (const QString &name : {QStringLiteral("status"), QStringLiteral("smaps_rollup")})
    {
        QFile file(QStringLiteral("/proc/self/") + name);
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }
        const auto lines = file.readAll().split('\n');
        for (const auto &line : lines)
        {
            const auto parts = line.simplified().split(' ');
            if (parts.size() < 2)
            {
                continue;
            }
            const QString key = QString::fromLatin1(parts[0]);
            if (key == QStringLiteral("VmRSS:") || key == QStringLiteral("VmHWM:")
                    || key == QStringLiteral("Pss:") || key == QStringLiteral("Threads:"))
            {
                result[key] = parts[1].toInt();
            }
        }
    }
    result[QStringLiteral("fd_count")] = QDir(QStringLiteral("/proc/self/fd"))
            .entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
    return result;
}

class Probe : public QObject
{
public:
    Probe()
    {
        clock.start();
        cpuStart = std::clock();
        previous = clock.nsecsElapsed();
        timer.setTimerType(Qt::PreciseTimer);
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            const qint64 now = clock.nsecsElapsed();
            gaps.append(double(now - previous) / 1000000.0);
            previous = now;
        });
        resources.setInterval(200);
        connect(&resources, &QTimer::timeout, this, [this]() { samples.append(resourceSample()); });
        timer.start();
        resources.start();
    }
    void operation(const QString &name, double milliseconds)
    {
        operations.append(QJsonObject{{QStringLiteral("operation"), name},
            {QStringLiteral("elapsed_ms"), milliseconds}});
    }
    QJsonObject result() const
    {
        QList<double> ordered = gaps;
        std::sort(ordered.begin(), ordered.end());
        return {{QStringLiteral("timer_interval_ms"), 10}, {QStringLiteral("timer_samples"), gaps.size()},
            {QStringLiteral("timer_gap_max_ms"), ordered.isEmpty() ? 0.0 : ordered.last()},
            {QStringLiteral("timer_gap_p95_ms"), ordered.isEmpty() ? 0.0 : ordered[qMax(0, int(qCeil(ordered.size() * .95)) - 1)]},
            {QStringLiteral("operations"), operations}, {QStringLiteral("resources_200ms"), samples},
            {QStringLiteral("cpu_percent_one_logical_core"), 100.0 * double(std::clock() - cpuStart) / CLOCKS_PER_SEC / (clock.nsecsElapsed() / 1e9)},
            {QStringLiteral("elapsed_ms"), double(clock.nsecsElapsed()) / 1000000.0}};
    }
    QTimer timer, resources;
    QElapsedTimer clock;
    qint64 previous;
    std::clock_t cpuStart;
    QList<double> gaps;
    QJsonArray operations, samples;
};

class Session
{
public:
    Session(const QString &directory, const QString &bootstrap)
    {
        AccountStore::registerTypes();
        store = new AccountStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        // Register before the page handler opens its modal error dialog, so
        // this sample measures request-to-error delivery without animation.
        QObject::connect(store, &AccountStore::error, &observer, [this](const QString &, const QString &)
        {
            lastErrorMs = writeClock.isValid() ? writeClock.nsecsElapsed() / 1000000.0 : -1;
        });
        window = new frmAccountSetting(store);
        window->resize(1200, 900);
        operations = new QSignalSpy(window, &frmAccountSetting::operationFinished);
        sessions = new QSignalSpy(store, &AccountStore::sessionChanged);
        QObject::connect(window, &frmAccountSetting::requestAdd, window, [this]() { writeClock.restart(); });
        QObject::connect(window, &frmAccountSetting::requestRemove, window, [this]() { writeClock.restart(); });
        QObject::connect(window, &frmAccountSetting::requestUpdate, window, [this]() { writeClock.restart(); });
        QObject::connect(store, &AccountStore::succeeded, window, [this](const QString &, bool)
        {
            lastCommitMs = writeClock.isValid() ? writeClock.nsecsElapsed() / 1000000.0 : -1;
        });
        QObject::connect(window, &frmAccountSetting::statusChanged, window, [this](const QString &, bool)
        {
            if (feedbackMs < 0 && interactionClock.isValid())
            {
                feedbackMs = interactionClock.nsecsElapsed() / 1000000.0;
            }
        });
        QObject::connect(&thread, &QThread::started, store, [this, directory, bootstrap]()
        {
            store->initialize(directory, bootstrap);
        });
        window->show();
        thread.start();
    }
    ~Session()
    {
        delete operations;
        delete sessions;
        delete window;
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit();
        thread.wait();
    }
    bool ready() { return waitFor([this]() { return window->isReady() && !window->isBusy(); }); }
    bool login(const QString &name, bool actionReady = true)
    {
        sessions->clear();
        operations->clear();
        QMetaObject::invokeMethod(store, "setSession", Qt::QueuedConnection,
            Q_ARG(QString, name), Q_ARG(bool, actionReady));
        // A worker spy can observe sessionChanged before the GUI receives it.
        // Wait for the page's completed reload as well as the worker signal.
        return waitFor([this]() { return !sessions->isEmpty() && !operations->isEmpty()
                && operations->last().at(0).toString() == QStringLiteral("session") && !window->isBusy(); });
    }
    QPushButton *button(const char *name) { return window->findChild<QPushButton *>(QString::fromLatin1(name)); }
    QLineEdit *edit(const char *name) { return window->findChild<QLineEdit *>(QString::fromLatin1(name)); }
    QComboBox *combo(const char *name) { return window->findChild<QComboBox *>(QString::fromLatin1(name)); }
    QTableWidget *table() { return window->findChild<QTableWidget *>(QStringLiteral("tbl_Account")); }
    void text(const char *name, const QString &value)
    {
        QLineEdit *field = edit(name);
        field->setFocus();
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
        if (value.isEmpty())
        {
            QTest::keyClick(field, Qt::Key_Backspace);
        }
        else
        {
            if (value.toLatin1() == value.toUtf8())
            {
                QTest::keyClicks(field, value);
            }
            else
            {
                QInputMethodEvent input;
                input.setCommitString(value);
                QApplication::sendEvent(field, &input);
            }
        }
    }
    void menu(const char *name)
    {
        QTest::mouseClick(button(name), Qt::LeftButton);
        QCoreApplication::processEvents();
    }
    bool click(const char *name, bool expected, Probe *probe = nullptr)
    {
        operations->clear();
        QElapsedTimer clock;
        clock.start();
        interactionClock.restart();
        lastCommitMs = -1;
        lastErrorMs = -1;
        feedbackMs = -1;
        if (!button(name) || !button(name)->isEnabled())
        {
            return false;
        }
        QTest::mouseClick(button(name), Qt::LeftButton);
        const bool finished = waitFor([this]() { return !operations->isEmpty() && !window->isBusy(); });
        if (probe)
        {
            probe->operation(QString::fromLatin1(name) + (expected ? QStringLiteral("_success") : QStringLiteral("_rejected")),
                double(clock.nsecsElapsed()) / 1000000.0);
            if (lastCommitMs >= 0)
            {
                probe->operation(QString::fromLatin1(name) + QStringLiteral("_request_to_commit"), lastCommitMs);
            }
            if (feedbackMs >= 0)
            {
                probe->operation(QString::fromLatin1(name) + QStringLiteral("_status_feedback"), feedbackMs);
            }
        }
        return finished && operations->last().at(1).toBool() == expected;
    }
    bool rejects(const char *name, const QString &messagePart)
    {
        return click(name, false) && window->statusText().contains(messagePart);
    }
    void addFields(const QString &name, const QString &password, int role)
    {
        menu("btnMenuAdd");
        text("ldtUserNameAdd", name);
        text("ldtPasswordAdd", password);
        text("ldtPasswordAgainAdd", password);
        combo("cbxTypeSelectAdd")->setCurrentIndex(role - 1);
    }
    void deleteFields(const QString &name, const QString &password)
    {
        menu("btnMenuDel");
        text("ldtUserNameDel", name);
        text("ldtPasswordDel", password);
    }
    void updateFields(const QString &oldPassword, const QString &newPassword, int role)
    {
        menu("btnMenuChange");
        text("ldtUserPasswordUpdate", oldPassword);
        text("ldtUserPasswordUpdate2", newPassword);
        text("ldtUserPasswordUpdate3", newPassword);
        combo("cbxTypeSelectUpdate")->setCurrentIndex(role - 1);
    }
    void clearSecrets()
    {
        for (auto *field : window->findChildren<QLineEdit *>())
        {
            if (field->echoMode() != QLineEdit::Normal)
            {
                field->clear();
            }
        }
    }
    QThread thread;
    QObject observer;
    AccountStore *store;
    frmAccountSetting *window;
    QSignalSpy *operations, *sessions;
    QElapsedTimer writeClock, interactionClock;
    double lastCommitMs = -1, lastErrorMs = -1, feedbackMs = -1;
};
}

class AccountTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void writeExperiment();
    void readExperiment();
private:
    QString directory_, database_;
};

void AccountTest::initTestCase()
{
    directory_ = qEnvironmentVariable("SN100_EVIDENCE_DIR");
    QVERIFY2(QDir::isAbsolutePath(directory_), "Set a new absolute SN100_EVIDENCE_DIR under temp/sn100_host");
    database_ = QDir(directory_).filePath(QStringLiteral("user_Information.db"));
    qApp->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    qApp->setStyle(QStringLiteral("Fusion"));
}

void AccountTest::writeExperiment()
{
    QVERIFY2(!QFileInfo::exists(database_), "writeExperiment requires a NEW directory; existing account data left untouched");
    const QString secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString changedSecret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DialogAnswers dialogs;
    Probe probe;
    Session session(directory_, secret);
    QVERIFY2(session.ready(), qPrintable(session.window->statusText()));
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 4);
    const qint64 startingBytes = QFileInfo(database_).size();
    QJsonArray cases;
    auto checked = [&cases](const QString &name) { cases.append(name); };

    // Available roles and visibility follow the exact original numeric rule.
    const QStringList actors = {QStringLiteral("offlineSend"), QStringLiteral("offlineTest"),
        QStringLiteral("offlineCheck"), QStringLiteral("offlineEngineer"), QStringLiteral("offlineAdmin")};
    for (int index = 0; index < actors.size(); ++index)
    {
        QVERIFY(session.login(actors[index]));
        QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), qMin(index + 1, 4));
        QCOMPARE(session.table()->rowCount(), index == 4 ? 4 : 3);
    }
    checked(QStringLiteral("five roles: exact choices and original table filter"));
    auto hasVisibleName = [&session](const QString &name)
    {
        for (int row = 0; row < session.table()->rowCount(); ++row)
        {
            if (session.table()->item(row, 2)->text() == name)
            {
                return true;
            }
        }
        return false;
    };
    QVERIFY(hasVisibleName(QStringLiteral("offlineEngineer")));
    const QString roleConnection = QStringLiteral("account_role_change");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), roleConnection);
        database.setDatabaseName(database_);
        QVERIFY(database.open());
        QSqlQuery query(database);
        QVERIFY(query.exec(QStringLiteral("UPDATE user_information SET role=1 WHERE name='offlineAdmin'")));
        session.addFields(QStringLiteral("staleDenied"), secret, 4);
        QVERIFY(session.rejects("btnAdd", QStringLiteral("超出当前用户可用选项")));
        QVERIFY(!hasVisibleName(QStringLiteral("offlineEngineer")));
        QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 1);
        QVERIFY(query.exec(QStringLiteral("UPDATE user_information SET role=5 WHERE name='offlineAdmin'")));
        database.close();
    }
    QSqlDatabase::removeDatabase(roleConnection);
    checked(QStringLiteral("role changed in DB during write: forged role rejected and old administrator rows immediately refiltered"));
    QVERIFY(session.login(QString()));
    QVERIFY(!session.table()->isVisible());
    session.addFields(QStringLiteral("guestDenied"), secret, 1);
    QVERIFY2(session.rejects("btnAdd", QStringLiteral("无权限")), qPrintable(session.window->statusText()));
    checked(QStringLiteral("Guest add rejected and account table hidden"));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));

    session.addFields(QString(), secret, 1);
    QVERIFY(session.rejects("btnAdd", QStringLiteral("不能为空")));
    session.addFields(QStringLiteral("trialA"), QString(), 1);
    QVERIFY(session.rejects("btnAdd", QStringLiteral("不能为空")));
    session.addFields(QStringLiteral("trialA"), secret, 1);
    session.text("ldtPasswordAgainAdd", changedSecret);
    QVERIFY(session.rejects("btnAdd", QStringLiteral("不一致")));
    checked(QStringLiteral("empty name/password and mismatched confirmation rejected"));
    session.addFields(QStringLiteral("trialA"), secret, 2);
    QVERIFY2(session.click("btnAdd", true, &probe), qPrintable(session.window->statusText()));
    session.addFields(QStringLiteral("trialA"), secret, 2);
    QVERIFY(session.rejects("btnAdd", QStringLiteral("已存在")));
    checked(QStringLiteral("actual insert and duplicate rejected"));

    QString sixteen = QStringLiteral("A123456789012345");
    QString seventeen = sixteen + QLatin1Char('6');
    int position = sixteen.size();
    QCOMPARE(session.edit("ldtUserNameAdd")->validator()->validate(sixteen, position), QValidator::Intermediate);
    position = seventeen.size();
    QCOMPARE(session.edit("ldtUserNameAdd")->validator()->validate(seventeen, position), QValidator::Acceptable);
    session.menu("btnMenuChange");
    int targetRow = -1;
    for (int row = 0; row < session.table()->rowCount(); ++row)
    {
        if (session.table()->item(row, 2)->text() == QStringLiteral("trialA"))
        {
            targetRow = row;
        }
    }
    QVERIFY(targetRow >= 0);
    auto doubleClickTarget = [&]()
    {
        const QPoint point = session.table()->visualItemRect(session.table()->item(targetRow, 2)).center();
        QTest::mouseClick(session.table()->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(session.table()->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    };
    doubleClickTarget();
    for (const char *name : {"ldtUserNameAdd", "ldtUserNameDel", "ldtUserNameUpdate", "ldtUserNameUpdate2"})
    {
        QCOMPARE(session.edit(name)->text(), QStringLiteral("trialA"));
    }
    QVERIFY(session.edit("ldtUserNameUpdate")->isReadOnly());
    session.text("ldtUserPasswordUpdate", secret);
    session.text("ldtUserPasswordUpdate2", changedSecret);
    session.text("ldtUserPasswordUpdate3", changedSecret);
    session.combo("cbxTypeSelectUpdate")->setCurrentIndex(0);
    QVERIFY(session.click("btnUpdate", true));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    session.menu("btnMenuChange");
    doubleClickTarget();
    session.text("ldtUserPasswordUpdate", changedSecret);
    session.text("ldtUserPasswordUpdate2", secret);
    session.text("ldtUserPasswordUpdate3", secret);
    session.combo("cbxTypeSelectUpdate")->setCurrentIndex(1);
    QVERIFY(session.click("btnUpdate", true));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    checked(QStringLiteral("original 17-character validator and double-click updating another selected account"));

    const QJsonObject beforeCancel = publicSnapshot(database_);
    session.deleteFields(QStringLiteral("trialA"), secret);
    dialogs.nextQuestion = QMessageBox::No;
    QVERIFY(session.click("btnDel", false));
    QCOMPARE(publicSnapshot(database_), beforeCancel);
    session.deleteFields(QStringLiteral("missingUser"), secret);
    QVERIFY(session.rejects("btnDel", QStringLiteral("不存在")));
    session.deleteFields(QStringLiteral("trialA"), changedSecret);
    QVERIFY(session.rejects("btnDel", QStringLiteral("密码错误")));
    QVERIFY(session.login(QStringLiteral("offlineSend")));
    session.deleteFields(QStringLiteral("offlineTest"), secret);
    QVERIFY(session.rejects("btnDel", QStringLiteral("权限低于")));
    QVERIFY(session.login(QStringLiteral("offlineAdmin")));
    session.menu("btnMenuChange");
    QCOMPARE(session.combo("cbxTypeSelectUpdate")->currentIndex(), -1);
    QCOMPARE(session.combo("cbxTypeSelectUpdate")->currentText(), QString());
    session.deleteFields(QStringLiteral("offlineAdmin"), secret);
    QVERIFY(session.rejects("btnDel", QStringLiteral("不允许删除")));
    checked(QStringLiteral("delete cancel/missing/wrong password/higher-role/admin protection"));

    QVERIFY(session.login(QStringLiteral("offlineTest"), false));
    session.deleteFields(QStringLiteral("offlineTest"), secret);
    QVERIFY(session.rejects("btnDel", QStringLiteral("设备正在运行")));
    session.updateFields(secret, changedSecret, 2);
    QVERIFY(session.rejects("btnUpdate", QStringLiteral("不允许修改用户类型")));
    // Preserve the source comparison: changing to a different role is allowed
    // even while actionReady is false. This is a recorded source issue.
    session.updateFields(secret, changedSecret, 1);
    QSignalSpy logout(session.window, &frmAccountSetting::signalLoginOut);
    QVERIFY(session.click("btnUpdate", true));
    QCOMPARE(logout.size(), 1);
    QVERIFY(!session.table()->isVisible());
    QVERIFY(session.login(QStringLiteral("offlineTest")));
    QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 1);
    session.updateFields(changedSecret, secret, 1);
    QVERIFY(session.click("btnUpdate", true));
    checked(QStringLiteral("simulated busy-state original branches; update commits and logs out"));

    QVERIFY(session.login(QStringLiteral("offlineCheck")));
    session.updateFields(changedSecret, secret, 3);
    QVERIFY(session.rejects("btnUpdate", QStringLiteral("密码错误")));
    session.updateFields(secret, changedSecret, 3);
    session.text("ldtUserPasswordUpdate3", secret);
    QVERIFY(session.rejects("btnUpdate", QStringLiteral("不一致")));
    session.updateFields(secret, QString(), 3);
    QVERIFY(session.click("btnUpdate", true));
    QVERIFY(session.login(QStringLiteral("offlineCheck")));
    session.updateFields(QString(), secret, 3);
    QVERIFY(session.click("btnUpdate", true));
    checked(QStringLiteral("wrong old password/mismatch rejected; original empty new password accepted and reversible"));

    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    session.deleteFields(QStringLiteral("trialA"), secret);
    QVERIFY(session.click("btnDel", true));
    session.addFields(QStringLiteral("selfDelete"), secret, 1);
    QVERIFY(session.click("btnAdd", true));
    QVERIFY(session.login(QStringLiteral("selfDelete")));
    session.deleteFields(QStringLiteral("selfDelete"), secret);
    logout.clear();
    QVERIFY(session.click("btnDel", true));
    QCOMPARE(logout.size(), 1);
    checked(QStringLiteral("actual delete; deleting current account commits and logs out"));

    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    const QJsonObject beforeLock = publicSnapshot(database_);
    const QString connection = QStringLiteral("account_test_lock");
    QJsonObject lockMetrics;
    {
        QSqlDatabase lock = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        lock.setDatabaseName(database_);
        QVERIFY(lock.open());
        QSqlQuery query(lock);
        QVERIFY(query.exec(QStringLiteral("BEGIN IMMEDIATE")));
        session.addFields(QStringLiteral("lockRetry"), secret, 1);
        const int firstSample = probe.gaps.size();
        bool observedBusy = false;
        bool acceptedClose = true;
        QTimer::singleShot(15, session.window, [&]()
        {
            observedBusy = session.window->isBusy();
            acceptedClose = session.window->close();
        });
        dialogs.capturePath = QDir(directory_).filePath(QStringLiteral("locked_dialog.png"));
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(session.click("btnAdd", false));
        QVERIFY(observedBusy);
        QVERIFY(!acceptedClose);
        const double duration = elapsed.nsecsElapsed() / 1000000.0;
        double maxGap = 0;
        for (int index = firstSample; index < probe.gaps.size(); ++index)
        {
            maxGap = qMax(maxGap, probe.gaps[index]);
        }
        QCOMPARE(publicSnapshot(database_), beforeLock);
        QVERIFY(probe.gaps.size() - firstSample >= 5);
        QVERIFY(maxGap < 150);
        lockMetrics = {{QStringLiteral("elapsed_ms"), duration},
            {QStringLiteral("transaction_request_to_error_ms"), session.lastErrorMs},
            {QStringLiteral("heartbeat_count"), probe.gaps.size() - firstSample},
            {QStringLiteral("close_rejected_while_busy"), true},
            {QStringLiteral("max_timer_gap_ms"), maxGap}, {QStringLiteral("partial_commit"), false}};
        session.clearSecrets();
        QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("locked_error.png"))));
        QVERIFY(query.exec(QStringLiteral("ROLLBACK")));
        lock.close();
    }
    QSqlDatabase::removeDatabase(connection);
    session.addFields(QStringLiteral("lockRetry"), secret, 1);
    QVERIFY(session.click("btnAdd", true));
    checked(QStringLiteral("locked database bounded failure/no partial commit; unlock retry succeeds"));

    // Repeat actual writes; 50 additions + 50 deletions, with 100 tab clicks.
    for (int iteration = 0; iteration < 50; ++iteration)
    {
        const QString name = QStringLiteral("repeat%1").arg(iteration);
        session.addFields(name, secret, 1);
        QVERIFY(session.click("btnAdd", true, &probe));
        session.deleteFields(name, secret);
        QVERIFY(session.click("btnDel", true, &probe));
    }
    session.addFields(QStringLiteral("A留存1"), secret, 2);
    QVERIFY2(session.click("btnAdd", true), qPrintable(session.window->statusText()));
    session.clearSecrets();
    session.menu("btnMenuAdd");
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("add_saved.png"))));
    session.menu("btnMenuDel");
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("delete_page.png"))));
    session.menu("btnMenuChange");
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("update_page.png"))));
    QVERIFY(session.login(QString()));
    session.clearSecrets();
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("guest.png"))));
    const QJsonObject actual = publicSnapshot(database_);
    QCOMPARE(actual.value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
    QCOMPARE(actual.value(QStringLiteral("rows")).toArray().size(), 7);
    QVERIFY(probe.gaps.size() > 20);
    QVERIFY2(probe.result().value(QStringLiteral("timer_gap_max_ms")).toDouble() < 500,
        "Measured GUI heartbeat gap exceeds candidate observation threshold");
    const auto *screen = QGuiApplication::primaryScreen();
    QJsonObject result{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("window_width"), session.window->width()}, {QStringLiteral("window_height"), session.window->height()},
        {QStringLiteral("logical_dpi"), screen->logicalDotsPerInch()}, {QStringLiteral("device_pixel_ratio"), screen->devicePixelRatio()},
        {QStringLiteral("cases"), cases}, {QStringLiteral("actual_without_credentials"), actual},
        {QStringLiteral("response"), probe.result()}, {QStringLiteral("locked_write"), lockMetrics},
        {QStringLiteral("repeat_writes"), 100}, {QStringLiteral("repeat_tab_clicks"), 100},
        {QStringLiteral("database_bytes_before"), double(startingBytes)},
        {QStringLiteral("database_bytes_after"), double(QFileInfo(database_).size())}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("write_result.json")), result));
    QVERIFY(session.window->close());
    qInfo().noquote() << "ACCOUNT_WRITE_ACTUAL_WITHOUT_CREDENTIALS" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
}

void AccountTest::readExperiment()
{
    QVERIFY(QFileInfo::exists(database_));
    QFile expectedFile(QDir(directory_).filePath(QStringLiteral("write_result.json")));
    QVERIFY(expectedFile.open(QIODevice::ReadOnly));
    const auto writeResult = QJsonDocument::fromJson(expectedFile.readAll()).object();
    const auto expected = writeResult.value(QStringLiteral("actual_without_credentials")).toObject();
    QVERIFY(writeResult.value(QStringLiteral("process_id")).toDouble() != double(QCoreApplication::applicationPid()));
    DialogAnswers dialogs;
    Session session(directory_, QString());
    QVERIFY2(session.ready(), qPrintable(session.window->statusText()));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QCOMPARE(session.table()->rowCount(), 5);
    bool retained = false;
    for (int row = 0; row < session.table()->rowCount(); ++row)
    {
        retained |= session.table()->item(row, 2)->text() == QStringLiteral("A留存1");
    }
    QVERIFY(retained);
    const auto actual = publicSnapshot(database_);
    QCOMPARE(actual, expected);
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("reopened.png"))));
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("read_result.json")),
        {{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
         {QStringLiteral("actual_without_credentials"), actual}, {QStringLiteral("new_process_readback"), true}}));
    QVERIFY(session.window->close());
}

QTEST_MAIN(AccountTest)
#include "account_test.moc"

