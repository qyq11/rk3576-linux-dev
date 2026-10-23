// O03-2: exercise the real shell, O01 and O02 Widgets using only isolated,
// anonymous data. Evidence explicitly selects public fields, never pwd.
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibrary>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <ctime>
#include <functional>

#include "data/account_store.h"
#include "data/hospital_store.h"
#include "shell/frmmain.h"
#include "ui/cusmessagebox.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <unistd.h>
#endif

namespace
{
const QString kHospital = QStringLiteral("O03-2保存重开医院 O'Brien");
const QString kDepartment = QStringLiteral("血液科");
const QString kNote = QStringLiteral("O03-2匿名科室 O'Brien");
const QStringList kActors = {QString(), QStringLiteral("offlineSend"),
    QStringLiteral("offlineTest"), QStringLiteral("offlineCheck"),
    QStringLiteral("offlineEngineer"), QStringLiteral("offlineAdmin")};
const QStringList kPasswordFields = {QStringLiteral("ldtPasswordAdd"),
    QStringLiteral("ldtPasswordAgainAdd"), QStringLiteral("ldtPasswordDel"),
    QStringLiteral("ldtUserPasswordUpdate"), QStringLiteral("ldtUserPasswordUpdate2"),
    QStringLiteral("ldtUserPasswordUpdate3")};
const QStringList kIdentityFields = {QStringLiteral("ldtUserNameAdd"),
    QStringLiteral("ldtUserNameDel"), QStringLiteral("ldtUserNameUpdate"),
    QStringLiteral("ldtUserNameUpdate2")};

bool waitUntil(const std::function<bool()> &predicate, int timeout = 10000)
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

bool saveJson(const QString &path, const QJsonObject &value)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        && file.write(QJsonDocument(value).toJson(QJsonDocument::Indented)) > 0;
}

QString quoted(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool savePublicCsv(const QString &path, const QJsonObject &value)
{
    QString csv = QStringLiteral("kind,id,name,role,isdefault,type,column_one,column_two,column_three\r\n");
    for (const QJsonValue &item : value.value(QStringLiteral("accounts")).toObject()
         .value(QStringLiteral("rows")).toArray())
    {
        const QJsonObject row = item.toObject();
        csv += QStringLiteral("account,%1,%2,%3,%4,,,,\r\n")
            .arg(row.value(QStringLiteral("id")).toInt())
            .arg(quoted(row.value(QStringLiteral("name")).toString()))
            .arg(row.value(QStringLiteral("role")).toInt())
            .arg(row.value(QStringLiteral("isdefault")).toInt());
    }
    const QJsonObject hospital = value.value(QStringLiteral("hospital")).toObject();
    csv += QStringLiteral("hospital,,%1,,,,,,\r\n")
        .arg(quoted(hospital.value(QStringLiteral("hospital_name")).toString()));
    for (const QJsonValue &item : hospital.value(QStringLiteral("rows")).toArray())
    {
        const QJsonObject row = item.toObject();
        csv += QStringLiteral("dictionary,%1,,,,%2,%3,%4,%5\r\n")
            .arg(row.value(QStringLiteral("id")).toInt())
            .arg(quoted(row.value(QStringLiteral("type")).toString()))
            .arg(quoted(row.value(QStringLiteral("column_one")).toString()))
            .arg(quoted(row.value(QStringLiteral("column_two")).toString()))
            .arg(quoted(row.value(QStringLiteral("column_three")).toString()));
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(csv.toUtf8()) > 0;
}

QJsonObject publicSnapshot(const QString &path, bool accounts)
{
    const QString connection = QStringLiteral("shell-public-") + QUuid::createUuid().toString();
    QJsonObject result;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open())
        {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next())
            {
                result.insert(QStringLiteral("integrity_check"), query.value(0).toString());
            }
            if (!accounts && query.exec(QStringLiteral("SELECT value FROM app_settings WHERE key='hospital/name'"))
                && query.next())
            {
                result.insert(QStringLiteral("hospital_name"), query.value(0).toString());
            }
            const QString sql = accounts
                ? QStringLiteral("SELECT id,name,role,isdefault FROM user_information ORDER BY id")
                : QStringLiteral("SELECT id,type,column_one,column_two,column_three FROM hospital_information ORDER BY id");
            if (query.exec(sql))
            {
                QJsonArray rows;
                while (query.next())
                {
                    QJsonObject row{{QStringLiteral("id"), query.value(0).toInt()}};
                    if (accounts)
                    {
                        row.insert(QStringLiteral("name"), query.value(1).toString());
                        row.insert(QStringLiteral("role"), query.value(2).toInt());
                        row.insert(QStringLiteral("isdefault"), query.value(3).toInt());
                    }
                    else
                    {
                        row.insert(QStringLiteral("type"), query.value(1).toString());
                        row.insert(QStringLiteral("column_one"), query.value(2).toString());
                        row.insert(QStringLiteral("column_two"), query.value(3).toString());
                        row.insert(QStringLiteral("column_three"), query.value(4).toString());
                    }
                    rows.append(row);
                }
                result.insert(QStringLiteral("rows"), rows);
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return result;
}

QJsonObject resourceSample(qint64 pid)
{
    QJsonObject result;
    const QString prefix = QStringLiteral("/proc/%1/").arg(pid);
    for (const QString &name : {QStringLiteral("status"), QStringLiteral("smaps_rollup")})
    {
        QFile file(prefix + name);
        if (!file.open(QIODevice::ReadOnly))
        {
            continue;
        }
        for (const QByteArray &line : file.readAll().split('\n'))
        {
            const QList<QByteArray> parts = line.simplified().split(' ');
            const QString key = QString::fromLatin1(parts.value(0));
            if (parts.size() > 1 && (key == QStringLiteral("VmRSS:")
                || key == QStringLiteral("VmHWM:") || key == QStringLiteral("Pss:")
                || key == QStringLiteral("Threads:")))
            {
                result.insert(key, parts[1].toInt());
            }
        }
    }
    QFile stat(prefix + QStringLiteral("stat"));
    if (stat.open(QIODevice::ReadOnly))
    {
        const QByteArray raw = stat.readAll();
        const QList<QByteArray> parts = raw.mid(raw.lastIndexOf(')') + 2).simplified().split(' ');
        if (parts.size() > 12)
        {
            result.insert(QStringLiteral("cpu_ticks"), double(parts[11].toLongLong() + parts[12].toLongLong()));
        }
    }
    const QDir descriptors(prefix + QStringLiteral("fd"));
    if (descriptors.exists())
    {
        result.insert(QStringLiteral("fd_count"), descriptors.entryList(
            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden).size());
    }
    return result;
}

// Simulate a public identity/role edit by another actor. This connection never
// selects or changes a password; refreshSession must recheck these fields.
bool changePublicAccount(const QString &path, const QString &oldName,
                         const QString &newName, int role)
{
    const QString connection = QStringLiteral("shell-public-edit-") + QUuid::createUuid().toString();
    bool changed = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(path);
        if (db.open())
        {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("UPDATE user_information SET name=:newName,role=:role WHERE name=:oldName"));
            query.bindValue(QStringLiteral(":newName"), newName);
            query.bindValue(QStringLiteral(":role"), role);
            query.bindValue(QStringLiteral(":oldName"), oldName);
            changed = query.exec() && query.numRowsAffected() == 1;
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return changed;
}

class Probe : public QObject
{
public:
    Probe()
    {
        clock.start();
        cpuStart = std::clock();
        previous = clock.nsecsElapsed();
        heartbeat.setTimerType(Qt::PreciseTimer);
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [this]()
        {
            const qint64 now = clock.nsecsElapsed();
            gaps.append(double(now - previous) / 1000000.0);
            previous = now;
        });
        resources.setInterval(200);
        connect(&resources, &QTimer::timeout, this, [this]()
        {
            QJsonObject sample = resourceSample(QCoreApplication::applicationPid());
            sample.insert(QStringLiteral("elapsed_ms"), clock.nsecsElapsed() / 1000000.0);
            samples.append(sample);
        });
        heartbeat.start();
        resources.start();
    }

    static QJsonObject summary(QList<double> values)
    {
        std::sort(values.begin(), values.end());
        return {{QStringLiteral("samples"), values.size()},
            {QStringLiteral("p95_ms"), values.isEmpty() ? 0 : values.at(qMax(0, int(qCeil(values.size() * .95)) - 1))},
            {QStringLiteral("max_ms"), values.isEmpty() ? 0 : values.last()}};
    }

    QJsonObject result() const
    {
        QJsonArray heartbeatGaps;
        for (double value : gaps)
        {
            heartbeatGaps.append(value);
        }
        QList<double> navigationTimes;
        for (const QJsonValue &value : navigation)
        {
            navigationTimes.append(value.toObject().value(QStringLiteral("elapsed_ms")).toDouble());
        }
        return {{QStringLiteral("timer_interval_ms"), 10}, {QStringLiteral("heartbeat"), summary(gaps)},
            {QStringLiteral("heartbeat_gap_ms"), heartbeatGaps},
            {QStringLiteral("navigation"), summary(navigationTimes)},
            {QStringLiteral("navigation_samples"), navigation},
            {QStringLiteral("navigation_measurement"), QStringLiteral("QTest mouse injection to navigationChanged; excludes compositor/pixel presentation")},
            {QStringLiteral("resources_200ms"), samples},
            {QStringLiteral("resource_condition"), QStringLiteral("QtTest integrated business/real-dialog/PNG workload, two SQL workers; formal product idle measured separately")},
            {QStringLiteral("cpu_percent_one_core"), 100.0 * double(std::clock() - cpuStart) / CLOCKS_PER_SEC / (clock.nsecsElapsed() / 1e9)},
            {QStringLiteral("elapsed_ms"), clock.nsecsElapsed() / 1000000.0}};
    }

    QElapsedTimer clock;
    QTimer heartbeat, resources;
    std::clock_t cpuStart;
    qint64 previous = 0;
    QList<double> gaps;
    QJsonArray navigation, samples;
};

// Preserve the original CusMessageBox animation. Only click after its own
// input-ready state; the O01 standard question is answered independently.
class DialogAnswers : public QObject
{
public:
    DialogAnswers()
    {
        timer.setInterval(5);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            QWidget *modal = QApplication::activeModalWidget();
            QAbstractButton *button = nullptr;
            if (auto *box = qobject_cast<CusMessageBox *>(modal))
            {
                if (!box->isReadyForInput())
                {
                    return;
                }
                auto *cancel = box->findChild<QPushButton *>(QStringLiteral("btnCancel"));
                const bool question = cancel && cancel->isVisible();
                button = box->findChild<QPushButton *>(question && nextCustom == QMessageBox::No
                    ? QStringLiteral("btnCancel") : QStringLiteral("btnSure"));
                nextCustom = QMessageBox::Yes;
            }
            else if (auto *box = qobject_cast<QMessageBox *>(modal))
            {
                button = box->button(nextStandard);
                nextStandard = QMessageBox::Yes;
            }
            if (!button || !button->isVisible() || !button->isEnabled())
            {
                return;
            }
            if (!capturePath.isEmpty())
            {
                captured = modal->grab().save(capturePath);
                capturePath.clear();
            }
            ++answered;
            QTest::mouseClick(button, Qt::LeftButton);
        });
        timer.start();
    }

    QTimer timer;
    QMessageBox::StandardButton nextStandard = QMessageBox::Yes;
    QMessageBox::StandardButton nextCustom = QMessageBox::Yes;
    QString capturePath;
    bool captured = false;
    int answered = 0;
};

bool paste(QLineEdit *field, const QString &text)
{
    if (!field || !waitUntil([field]()
        { return field->isVisible() && field->isEnabled() && !field->isReadOnly(); }, 1000))
    {
        return false;
    }
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(field, Qt::LeftButton);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier);
    const bool entered = waitUntil([field, text]() { return field->text() == text; }, 1000);
    QApplication::clipboard()->clear();
    return entered;
}

class Session
{
public:
    Session(const QString &directory, const QString &fixtureSecret)
    {
        HospitalStore::registerTypes();
        AccountStore::registerTypes();
        hospitalDirectory = QDir(directory).filePath(QStringLiteral("hospital"));
        accountDirectory = QDir(directory).filePath(QStringLiteral("account"));
        hospitalStore = new HospitalStore;
        accounts = new AccountStore;
        hospitalStore->moveToThread(&hospitalThread);
        accounts->moveToThread(&accountThread);
        QObject::connect(&hospitalThread, &QThread::finished, hospitalStore, &QObject::deleteLater);
        QObject::connect(&accountThread, &QThread::finished, accounts, &QObject::deleteLater);
        QObject::connect(hospitalStore, &HospitalStore::closed, &observer, [this]() { hospitalClosed = true; });
        QObject::connect(accounts, &AccountStore::closed, &observer, [this]() { accountClosed = true; });
        QObject::connect(hospitalStore, &HospitalStore::ready, &observer, [this]() { ++hospitalReadyCount; });
        QObject::connect(accounts, &AccountStore::ready, &observer, [this]() { ++accountReadyCount; });
        window = new frmMain(hospitalStore, nullptr, accounts);
        window->setDataDirectory(hospitalDirectory);
        accountPage = window->accountPage();
        hospitalPage = window->hospitalPage();
        operations = new QSignalSpy(accountPage, &frmAccountSetting::operationFinished);
        hospitalOperations = new QSignalSpy(hospitalPage, &frmHospInfoSetting::operationFinished);
        navigation = new QSignalSpy(window, &frmMain::navigationChanged);
        rejected = new QSignalSpy(window, &frmMain::navigationRejected);
        logout = new QSignalSpy(window, &frmMain::loggedOut);
        QObject::connect(&hospitalThread, &QThread::started, hospitalStore, [this]()
        {
            hospitalStore->initialize(QDir(hospitalDirectory).filePath(QStringLiteral("hospital_Information.db")),
                                     QDir(hospitalDirectory).filePath(QStringLiteral("config.ini")));
        });
        QObject::connect(&accountThread, &QThread::started, accounts, [this, fixtureSecret]()
        {
            accounts->initialize(accountDirectory, fixtureSecret);
        });
        window->show();
        hospitalThread.start();
        accountThread.start();
    }

    ~Session()
    {
        delete operations;
        delete hospitalOperations;
        delete navigation;
        delete rejected;
        delete logout;
        delete window;
        stopWorkers();
    }

    bool stopWorkers()
    {
        if (hospitalThread.isRunning())
        {
            QMetaObject::invokeMethod(hospitalStore, "close", Qt::BlockingQueuedConnection);
            hospitalThread.quit();
            hospitalThread.wait();
        }
        if (accountThread.isRunning())
        {
            QMetaObject::invokeMethod(accounts, "close", Qt::BlockingQueuedConnection);
            accountThread.quit();
            accountThread.wait();
        }
        return waitUntil([this]() { return hospitalClosed && accountClosed; }, 1000)
            && !hospitalThread.isRunning() && !accountThread.isRunning();
    }

    bool ready() const
    {
        return waitUntil([this]() { return hospitalPage && accountPage
            && hospitalPage->isReady() && !hospitalPage->isBusy()
            && accountPage->isReady() && !accountPage->isBusy(); });
    }

    bool login(const QString &name, bool actionReady = true)
    {
        operations->clear();
        QMetaObject::invokeMethod(accounts, "setSession", Qt::QueuedConnection,
            Q_ARG(QString, name), Q_ARG(bool, actionReady));
        return waitUntil([this, name, actionReady]()
        {
            const AccountSession identity = window->accountSession();
            return !operations->isEmpty() && !accountPage->isBusy()
                && identity.name == name && identity.actionReady == actionReady
                && identity.authenticated == !name.isEmpty();
        });
    }

    QAbstractButton *button(const char *name) const
    {
        return window->findChild<QAbstractButton *>(QString::fromLatin1(name));
    }

    QLineEdit *edit(const char *name) const
    {
        return accountPage->findChild<QLineEdit *>(QString::fromLatin1(name));
    }

    QComboBox *combo(const char *name) const
    {
        return accountPage->findChild<QComboBox *>(QString::fromLatin1(name));
    }

    QTableWidget *table() const
    {
        return accountPage->findChild<QTableWidget *>(QStringLiteral("tbl_Account"));
    }

    QLineEdit *hospitalName() const
    {
        return hospitalPage->findChild<QLineEdit *>(QStringLiteral("ldtHospName"));
    }

    bool navigationClick(const char *name, int expectedMain, int expectedUser, Probe *probe = nullptr)
    {
        QAbstractButton *control = button(name);
        if (!control || !control->isVisible() || !control->isEnabled())
        {
            return false;
        }
        navigation->clear();
        QElapsedTimer elapsed;
        elapsed.start();
        double signalMs = -1;
        const auto connection = QObject::connect(window, &frmMain::navigationChanged, window,
            [&elapsed, &signalMs](int, int)
        {
            if (signalMs < 0)
            {
                signalMs = elapsed.nsecsElapsed() / 1000000.0;
            }
        });
        QTest::mouseClick(control, Qt::LeftButton);
        const bool accepted = waitUntil([this, expectedMain, expectedUser]()
        {
            return !navigation->isEmpty() && window->currentMainPage() == expectedMain
                && window->currentUserPage() == expectedUser;
        });
        QObject::disconnect(connection);
        if (probe && signalMs >= 0)
        {
            probe->navigation.append(QJsonObject{{QStringLiteral("button"), QString::fromLatin1(name)},
                {QStringLiteral("main_page"), expectedMain}, {QStringLiteral("user_page"), expectedUser},
                {QStringLiteral("elapsed_ms"), signalMs}});
        }
        return accepted;
    }

    bool clickAccount(const char *name, bool expected)
    {
        QAbstractButton *control = accountPage->findChild<QAbstractButton *>(QString::fromLatin1(name));
        if (!control || !control->isVisible() || !control->isEnabled())
        {
            return false;
        }
        operations->clear();
        QTest::mouseClick(control, Qt::LeftButton);
        return waitUntil([this]() { return !operations->isEmpty() && !accountPage->isBusy(); })
            && operations->last().at(1).toBool() == expected;
    }

    void menu(const char *name)
    {
        QTest::mouseClick(accountPage->findChild<QAbstractButton *>(QString::fromLatin1(name)), Qt::LeftButton);
    }

    bool addFields(const QString &name, const QString &secret, int role)
    {
        menu("btnMenuAdd");
        const bool entered = paste(edit("ldtUserNameAdd"), name)
            && paste(edit("ldtPasswordAdd"), secret) && paste(edit("ldtPasswordAgainAdd"), secret);
        combo("cbxTypeSelectAdd")->setCurrentIndex(combo("cbxTypeSelectAdd")->findData(role));
        return entered;
    }

    bool deleteFields(const QString &name, const QString &secret)
    {
        menu("btnMenuDel");
        return paste(edit("ldtUserNameDel"), name) && paste(edit("ldtPasswordDel"), secret);
    }

    bool updateFields(const QString &oldSecret, const QString &newSecret, int role)
    {
        menu("btnMenuChange");
        const bool entered = paste(edit("ldtUserPasswordUpdate"), oldSecret)
            && paste(edit("ldtUserPasswordUpdate2"), newSecret)
            && paste(edit("ldtUserPasswordUpdate3"), newSecret);
        combo("cbxTypeSelectUpdate")->setCurrentIndex(combo("cbxTypeSelectUpdate")->findData(role));
        return entered;
    }

    bool passwordsEmpty() const
    {
        for (const QString &name : kPasswordFields)
        {
            if (!accountPage->findChild<QLineEdit *>(name)->text().isEmpty())
            {
                return false;
            }
        }
        return true;
    }

    void clearPasswords()
    {
        for (const QString &name : kPasswordFields)
        {
            accountPage->findChild<QLineEdit *>(name)->clear();
        }
    }

    bool guestClean() const
    {
        const AccountSession identity = window->accountSession();
        if (identity.authenticated || identity.role != 0 || !identity.name.isEmpty()
            || !passwordsEmpty() || table()->isVisible() || table()->rowCount() != 0
            || combo("cbxTypeSelectAdd")->count() != 0 || combo("cbxTypeSelectUpdate")->count() != 0)
        {
            return false;
        }
        for (const QString &name : kIdentityFields)
        {
            if (!accountPage->findChild<QLineEdit *>(name)->text().isEmpty())
            {
                return false;
            }
        }
        return window->findChild<QLabel *>(QStringLiteral("lblUserName"))->text().contains(QStringLiteral("Guest"));
    }

    QJsonObject snapshot() const
    {
        return {{QStringLiteral("accounts"), publicSnapshot(QDir(accountDirectory).filePath(QStringLiteral("user_Information.db")), true)},
            {QStringLiteral("hospital"), publicSnapshot(QDir(hospitalDirectory).filePath(QStringLiteral("hospital_Information.db")), false)}};
    }

    QObject observer;
    QThread hospitalThread, accountThread;
    HospitalStore *hospitalStore = nullptr;
    AccountStore *accounts = nullptr;
    frmMain *window = nullptr;
    frmHospInfoSetting *hospitalPage = nullptr;
    frmAccountSetting *accountPage = nullptr;
    QSignalSpy *operations = nullptr, *hospitalOperations = nullptr;
    QSignalSpy *navigation = nullptr, *rejected = nullptr, *logout = nullptr;
    QString hospitalDirectory, accountDirectory;
    bool hospitalClosed = false, accountClosed = false;
    int hospitalReadyCount = 0, accountReadyCount = 0;
};

#ifdef Q_OS_LINUX
bool nativeClose(qulonglong windowId)
{
    QLibrary library(QStringLiteral("libX11.so.6"));
    if (!library.load())
    {
        return false;
    }
    const auto openDisplay = reinterpret_cast<Display *(*)(const char *)>(library.resolve("XOpenDisplay"));
    const auto internAtom = reinterpret_cast<Atom (*)(Display *, const char *, Bool)>(library.resolve("XInternAtom"));
    const auto sendEvent = reinterpret_cast<Status (*)(Display *, Window, Bool, long, XEvent *)>(library.resolve("XSendEvent"));
    const auto flush = reinterpret_cast<int (*)(Display *)>(library.resolve("XFlush"));
    const auto closeDisplay = reinterpret_cast<int (*)(Display *)>(library.resolve("XCloseDisplay"));
    if (!openDisplay || !internAtom || !sendEvent || !flush || !closeDisplay)
    {
        return false;
    }
    Display *display = openDisplay(nullptr);
    if (!display)
    {
        return false;
    }
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = Window(windowId);
    event.xclient.message_type = internAtom(display, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = long(internAtom(display, "WM_DELETE_WINDOW", False));
    event.xclient.data.l[1] = CurrentTime;
    const bool sent = sendEvent(display, Window(windowId), False, NoEventMask, &event) != 0;
    flush(display);
    closeDisplay(display);
    return sent;
}
#endif
}

class ShellAccountTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void writeExperiment();
    void readExperiment();
    void navigationResponsivenessExperiment();
    void applicationExitExperiment();
private:
    QString directory_;
};

void ShellAccountTest::initTestCase()
{
    directory_ = qEnvironmentVariable("SN100_SHELL_ACCOUNT_EVIDENCE_DIR");
    QVERIFY2(QDir::isAbsolutePath(directory_), "Set an absolute new SN100_SHELL_ACCOUNT_EVIDENCE_DIR under temp/sn100_host/o03_2");
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(directory_));
    QVERIFY2(normalized.contains(QStringLiteral("/temp/sn100_host/o03_2/")), "O03-2 evidence must remain in its independent temp tree");
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"), "Requires visible WSLg/XCB, never offscreen");
    QVERIFY(QDir().mkpath(directory_));
    qApp->setQuitOnLastWindowClosed(false);
    qApp->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    qApp->setStyle(QStringLiteral("Fusion"));
}

void ShellAccountTest::writeExperiment()
{
    QVERIFY2(!QFileInfo::exists(QDir(directory_).filePath(QStringLiteral("account/user_Information.db"))), "Existing account data is never reset");
    QVERIFY2(!QFileInfo::exists(QDir(directory_).filePath(QStringLiteral("hospital/hospital_Information.db"))), "Existing O01 data is never reset");
    const QString fixtureSecret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString changedSecret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DialogAnswers dialogs;
    Session session(directory_, fixtureSecret);
    QVERIFY2(session.ready(), "Two pages/workers failed to become ready");
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    Probe probe;
    QJsonArray cases;
    const auto checked = [&cases](const QString &name, const QJsonValue &input,
                                  const QJsonValue &expected, const QJsonValue &actual)
    {
        cases.append(QJsonObject{{QStringLiteral("case"), name}, {QStringLiteral("input"), input},
            {QStringLiteral("expected"), expected}, {QStringLiteral("actual"), actual},
            {QStringLiteral("passed"), true}});
    };

    QCOMPARE(session.window->size(), QSize(1920, 1080));
    QCOMPARE(session.window->currentMainPage(), 6);
    QCOMPARE(session.window->currentUserPage(), 0);
    QVERIFY(session.guestClean());
    QWidget *loginIcon = session.window->findChild<QWidget *>(QStringLiteral("btnUser"));
    QVERIFY(loginIcon);
    QVERIFY(!loginIcon->isEnabled());
    QVERIFY(!session.button("btnLoginOut")->isEnabled());
    QCOMPARE(session.table()->pos(), QPoint(10, 30));
    QCOMPARE(session.accountPage->findChild<QWidget *>(QStringLiteral("widget"))->pos(), QPoint(410, 30));
    checked(QStringLiteral("default identity"), QStringLiteral("fresh isolated stores"), QStringLiteral("Guest, production login disabled"), QStringLiteral("Guest, production login disabled"));

    // Persist O01 through its real controls before testing discard/navigation.
    QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
    QVERIFY(paste(session.hospitalName(), kHospital));
    auto *hospitalAdd = session.hospitalPage->findChild<QAbstractButton *>(QStringLiteral("btnAdd"));
    auto *hospitalSave = session.hospitalPage->findChild<QAbstractButton *>(QStringLiteral("btnSave"));
    auto *hospitalTable = session.hospitalPage->findChild<QTableWidget *>(QStringLiteral("tableInfo"));
    QTest::mouseClick(hospitalAdd, Qt::LeftButton);
    QCOMPARE(hospitalTable->rowCount(), 1);
    QVERIFY(paste(qobject_cast<QLineEdit *>(hospitalTable->cellWidget(0, 0)), kDepartment));
    QVERIFY(paste(qobject_cast<QLineEdit *>(hospitalTable->cellWidget(0, 2)), kNote));
    session.hospitalOperations->clear();
    QTest::mouseClick(hospitalSave, Qt::LeftButton);
    QVERIFY(waitUntil([&session]() { return !session.hospitalOperations->isEmpty() && !session.hospitalPage->isBusy(); }));
    QVERIFY(session.hospitalOperations->last().at(1).toBool());
    const QJsonObject persistedHospital = session.snapshot().value(QStringLiteral("hospital")).toObject();
    QCOMPARE(persistedHospital.value(QStringLiteral("hospital_name")).toString(), kHospital);
    QCOMPARE(persistedHospital.value(QStringLiteral("rows")).toArray().size(), 1);
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("hospital_saved.png"))));

    // A failed switch must retain both the page and the exact hospital draft.
    QVERIFY(paste(session.hospitalName(), QStringLiteral("切账户前未保存草稿")));
    dialogs.nextStandard = QMessageBox::No;
    dialogs.capturePath = QDir(directory_).filePath(QStringLiteral("unsaved_navigation.png"));
    const int answeredBeforeSwitch = dialogs.answered;
    QTest::mouseClick(session.button("btnAccount"), Qt::LeftButton);
    QVERIFY(dialogs.answered > answeredBeforeSwitch && dialogs.captured);
    QCOMPARE(session.window->currentUserPage(), 1);
    QVERIFY(session.hospitalPage->hasUnsavedChanges());
    QCOMPARE(session.hospitalName()->text(), QStringLiteral("切账户前未保存草稿"));
    dialogs.nextStandard = QMessageBox::Yes;
    QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));
    QVERIFY(!session.hospitalPage->hasUnsavedChanges());
    QCOMPARE(session.snapshot().value(QStringLiteral("hospital")).toObject(), persistedHospital);
    checked(QStringLiteral("O01 unsaved navigation guard"), QStringLiteral("edit hospital, cancel then discard switching to account"), QStringLiteral("cancel retains draft; discard zero DB writes"), QStringLiteral("cancel retains draft; discard zero DB writes"));

    // Guest and each source role: assert option values and original unusual
    // list filter. No general user_powers ACL is inferred from role numbering.
    QJsonArray roleResults;
    for (int role = 0; role <= 5; ++role)
    {
        QVERIFY(session.login(kActors.at(role)));
        QCOMPARE(session.window->accountSession().role, role);
        const int expectedOptions = qMin(role, 4);
        QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), expectedOptions);
        QCOMPARE(session.combo("cbxTypeSelectUpdate")->count(), expectedOptions);
        for (int index = 0; index < expectedOptions; ++index)
        {
            QCOMPARE(session.combo("cbxTypeSelectAdd")->itemData(index).toInt(), index + 1);
            QCOMPARE(session.combo("cbxTypeSelectAdd")->itemText(index), AccountStore::roleName(index + 1));
        }
        const int expectedRows = role == 0 ? 0 : (role == 5 ? 4 : 3);
        QCOMPARE(session.table()->rowCount(), expectedRows);
        QCOMPARE(session.table()->isVisible(), role > 0);
        const QString displayedName = role == 0 ? QStringLiteral("Guest") : kActors.at(role);
        QVERIFY(session.window->findChild<QLabel *>(QStringLiteral("lblUserName"))->toolTip().contains(displayedName));
        session.menu("btnMenuChange");
        QCOMPARE(session.combo("cbxTypeSelectUpdate")->currentIndex(), role == 0 || role == 5 ? -1 : role - 1);
        session.menu("btnMenuAdd");
        roleResults.append(QJsonObject{{QStringLiteral("actor"), displayedName}, {QStringLiteral("role"), role},
            {QStringLiteral("expected_options"), expectedOptions}, {QStringLiteral("actual_options"), session.combo("cbxTypeSelectAdd")->count()},
            {QStringLiteral("expected_rows"), expectedRows}, {QStringLiteral("actual_rows"), session.table()->rowCount()}});
    }
    checked(QStringLiteral("role options, row filter, shell identity"), QStringLiteral("Guest and roles 1..5"), QStringLiteral("exact O02 rules including administrator empty update selection"), roleResults);
    QVERIFY(session.login(QString()));
    const QJsonObject beforeGuest = session.snapshot();
    QVERIFY(session.addFields(QStringLiteral("O03GuestDenied"), fixtureSecret, 1));
    QVERIFY(session.clickAccount("btnAdd", false));
    QVERIFY(session.accountPage->statusText().contains(QStringLiteral("无权限")));
    QVERIFY(session.deleteFields(QStringLiteral("offlineSend"), fixtureSecret));
    QVERIFY(session.clickAccount("btnDel", false));
    QCOMPARE(session.snapshot(), beforeGuest);
    session.clearPasswords();
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("guest.png"))));
    checked(QStringLiteral("Guest writes denied"), QStringLiteral("real Add/Delete buttons"), QStringLiteral("rejected, DB unchanged"), QStringLiteral("rejected, DB unchanged"));

    QVERIFY(session.login(QStringLiteral("offlineSend")));
    QVERIFY(session.addFields(QStringLiteral("O03HigherDenied"), fixtureSecret, 1));
    session.combo("cbxTypeSelectAdd")->addItem(QStringLiteral("测试越权输入"), 4);
    session.combo("cbxTypeSelectAdd")->setCurrentIndex(1);
    const QJsonObject beforeForged = session.snapshot();
    QVERIFY(session.clickAccount("btnAdd", false));
    QVERIFY(session.accountPage->statusText().contains(QStringLiteral("超出当前用户可用选项")));
    QCOMPARE(session.snapshot(), beforeForged);
    QVERIFY(session.login(QStringLiteral("offlineSend")));
    QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 1);
    checked(QStringLiteral("forged higher role denied by worker"), QStringLiteral("role 1 actor submits role 4 through modified combo data"), QStringLiteral("transaction refused, no row inserted"), QStringLiteral("transaction refused, no row inserted"));

    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    const QString accountPath = QDir(session.accountDirectory).filePath(QStringLiteral("user_Information.db"));
    const QJsonObject beforeRoleEdit = session.snapshot();
    QVERIFY(changePublicAccount(accountPath, QStringLiteral("offlineEngineer"), QStringLiteral("offlineEngineer"), 1));
    QVERIFY(session.addFields(QStringLiteral("O03RoleDenied"), fixtureSecret, 4));
    QVERIFY(session.clickAccount("btnAdd", false));
    QCOMPARE(session.window->accountSession().role, 1);
    QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 1);
    QVERIFY(session.window->findChild<QLabel *>(QStringLiteral("lblUserName"))->toolTip()
        .contains(AccountStore::roleName(1)));
    QVERIFY(changePublicAccount(accountPath, QStringLiteral("offlineEngineer"), QStringLiteral("offlineEngineer"), 4));
    QCOMPARE(session.snapshot(), beforeRoleEdit);
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QVERIFY(changePublicAccount(accountPath, QStringLiteral("offlineEngineer"), QStringLiteral("O03InvalidUser"), 4));
    QVERIFY(session.addFields(QStringLiteral("O03NameDenied"), fixtureSecret, 1));
    session.logout->clear();
    QVERIFY(session.clickAccount("btnAdd", false));
    QVERIFY(waitUntil([&session]() { return session.guestClean(); }));
    QCOMPARE(session.logout->size(), 1);
    QVERIFY(changePublicAccount(accountPath, QStringLiteral("O03InvalidUser"), QStringLiteral("offlineEngineer"), 4));
    QCOMPARE(session.snapshot(), beforeRoleEdit);
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    checked(QStringLiteral("role and identity rechecked inside transaction"), QStringLiteral("public role downgrade then public identity rename while old UI session remains"), QStringLiteral("downgrade refreshed before refusal; invalid identity becomes Guest; no added rows"), QStringLiteral("downgrade refreshed before refusal; invalid identity becomes Guest; no added rows"));
    QVERIFY(session.addFields(QStringLiteral("O03Retained"), fixtureSecret, 2));
    QVERIFY2(session.clickAccount("btnAdd", true), qPrintable(session.accountPage->statusText()));
    session.clearPasswords();
    session.menu("btnMenuAdd");
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("account_engineer.png"))));

    // Page instances, worker objects and their initialization counts stay
    // fixed across 100 actual account/hospital transitions.
    frmAccountSetting *const initialAccount = session.accountPage;
    frmHospInfoSetting *const initialHospital = session.hospitalPage;
    AccountStore *const initialAccountStore = session.accounts;
    HospitalStore *const initialHospitalStore = session.hospitalStore;
    for (int iteration = 0; iteration < 50; ++iteration)
    {
        QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
        // QWidget QTest injection and navigationChanged are synchronous.
        // Let queued paints, retained animations and heartbeat timers run
        // between user actions instead of manufacturing one long GUI burst.
        QTest::qWait(10);
        QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));
        QTest::qWait(10);
        QCOMPARE(session.window->accountPage(), initialAccount);
        QCOMPARE(session.window->hospitalPage(), initialHospital);
        QCOMPARE(session.accounts, initialAccountStore);
        QCOMPARE(session.hospitalStore, initialHospitalStore);
        QCOMPARE(session.window->findChildren<frmAccountSetting *>().size(), 1);
        QCOMPARE(session.window->findChildren<frmHospInfoSetting *>().size(), 1);
        QVERIFY(!session.hospitalPage->hasUnsavedChanges());
    }
    QCOMPARE(session.accountReadyCount, 1);
    QCOMPARE(session.hospitalReadyCount, 1);
    checked(QStringLiteral("100 page transitions"), 100, QStringLiteral("one instance and one initialization per page/store"), QStringLiteral("100 accepted, unchanged page/store instances, initialization counts 1/1"));

    QVERIFY(session.addFields(QStringLiteral("O03DraftName"), fixtureSecret, 1));
    QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
    QVERIFY(session.passwordsEmpty());
    // Session updates reach the hidden account page as well as the shell.
    QVERIFY(session.login(QStringLiteral("offlineSend")));
    QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 1);
    QCOMPARE(session.table()->rowCount(), 4); // three original lower roles + retained role 2
    QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));
    QCOMPARE(session.combo("cbxTypeSelectAdd")->count(), 1);
    QVERIFY(session.passwordsEmpty());
    checked(QStringLiteral("hidden account permissions and password cleanup"), QStringLiteral("leave populated account fields, switch session while hidden"), QStringLiteral("six password fields empty, hidden role filter refreshed"), QStringLiteral("six password fields empty, hidden role filter refreshed"));

    // Preserve the source running-state comparison exactly. Same-role update
    // and current-user deletion are refused; different-role update commits
    // and logs out, as recorded in O02 rather than silently repaired here.
    QVERIFY(session.login(QStringLiteral("offlineTest"), false));
    QVERIFY(session.deleteFields(QStringLiteral("offlineTest"), fixtureSecret));
    QVERIFY(session.clickAccount("btnDel", false));
    QVERIFY(session.accountPage->statusText().contains(QStringLiteral("设备正在运行")));
    QVERIFY(session.updateFields(fixtureSecret, changedSecret, 2));
    QVERIFY(session.clickAccount("btnUpdate", false));
    QVERIFY(session.accountPage->statusText().contains(QStringLiteral("不允许修改用户类型")));
    session.logout->clear();
    QVERIFY(session.updateFields(fixtureSecret, changedSecret, 1));
    QVERIFY(session.clickAccount("btnUpdate", true));
    QVERIFY(waitUntil([&session]() { return session.guestClean(); }));
    QCOMPARE(session.logout->size(), 1);
    QVERIFY(session.login(QStringLiteral("offlineTest")));
    QCOMPARE(session.window->accountSession().role, 1);
    // Role 1 cannot restore role 2. An engineer may select this visible row
    // using the original double-click behavior, then restore its public role.
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    session.menu("btnMenuChange");
    int targetRow = -1;
    for (int row = 0; row < session.table()->rowCount(); ++row)
    {
        if (session.table()->item(row, 2)->text() == QStringLiteral("offlineTest"))
        {
            targetRow = row;
        }
    }
    QVERIFY(targetRow >= 0);
    const QPoint target = session.table()->visualItemRect(session.table()->item(targetRow, 2)).center();
    QTest::mouseClick(session.table()->viewport(), Qt::LeftButton, Qt::NoModifier, target);
    QTest::mouseDClick(session.table()->viewport(), Qt::LeftButton, Qt::NoModifier, target);
    QCOMPARE(session.edit("ldtUserNameUpdate")->text(), QStringLiteral("offlineTest"));
    QVERIFY(paste(session.edit("ldtUserPasswordUpdate"), changedSecret));
    QVERIFY(paste(session.edit("ldtUserPasswordUpdate2"), fixtureSecret));
    QVERIFY(paste(session.edit("ldtUserPasswordUpdate3"), fixtureSecret));
    session.combo("cbxTypeSelectUpdate")->setCurrentIndex(1);
    QVERIFY(session.clickAccount("btnUpdate", true));
    QVERIFY(waitUntil([&session]() { return session.guestClean(); }));
    checked(QStringLiteral("running-state branches and automatic update logout"), QStringLiteral("running offlineTest: delete, same role update, changed role update"), QStringLiteral("deny, deny, commit+Guest; engineer restores role 2 then Guest"), QStringLiteral("deny, deny, commit+Guest; engineer restores role 2 then Guest"));

    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QVERIFY(session.addFields(QStringLiteral("O03SelfDelete"), fixtureSecret, 1));
    QVERIFY(session.clickAccount("btnAdd", true));
    QVERIFY(session.login(QStringLiteral("O03SelfDelete")));
    QVERIFY(session.deleteFields(QStringLiteral("O03SelfDelete"), fixtureSecret));
    session.logout->clear();
    QVERIFY(session.clickAccount("btnDel", true));
    QVERIFY(waitUntil([&session]() { return session.guestClean(); }));
    QCOMPARE(session.logout->size(), 1);
    checked(QStringLiteral("automatic current-account deletion logout"), QStringLiteral("real Delete button for O03SelfDelete"), QStringLiteral("row deleted; Guest; no cached names/passwords/table"), QStringLiteral("row deleted; Guest; no cached names/passwords/table"));

    // The original top logout button must pass O01's unsaved guard before
    // asking the account worker to clear the only authoritative identity.
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
    QVERIFY(paste(session.hospitalName(), QStringLiteral("注销前未保存草稿")));
    session.logout->clear();
    dialogs.nextStandard = QMessageBox::No;
    QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
    QCOMPARE(session.window->accountSession().name, QStringLiteral("offlineEngineer"));
    QVERIFY(session.hospitalPage->hasUnsavedChanges());
    QCOMPARE(session.hospitalName()->text(), QStringLiteral("注销前未保存草稿"));
    QCOMPARE(session.logout->size(), 0);
    dialogs.nextStandard = QMessageBox::Yes;
    QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
    QVERIFY(waitUntil([&session]() { return !session.accountPage->isBusy() && session.guestClean(); }));
    QCOMPARE(session.logout->size(), 1);
    QVERIFY(!session.hospitalPage->hasUnsavedChanges());
    QCOMPARE(session.hospitalName()->text(), kHospital);
    QCOMPARE(session.snapshot().value(QStringLiteral("hospital")).toObject(), persistedHospital);
    session.logout->clear();
    QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
    QVERIFY(!session.window->requestLogout());
    QVERIFY(waitUntil([&session]() { return !session.accountPage->isBusy(); }));
    QVERIFY(session.guestClean());
    QCOMPARE(session.logout->size(), 0);
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("logout_guest.png"))));
    checked(QStringLiteral("manual logout and repeat"), QStringLiteral("dirty O01: cancel, accept, logout again"), QStringLiteral("cancel identity/draft retained; accept Guest and zero draft writes; repeat no extra logout"), QStringLiteral("cancel identity/draft retained; accept Guest and zero draft writes; repeat no extra logout"));

    // Lock only this experiment's account DB. During its bounded SQL wait,
    // attempt real navigation/logout buttons and close the shell itself.
    QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    const QJsonObject beforeLock = session.snapshot();
    const QString connection = QStringLiteral("shell-account-lock-") + QUuid::createUuid().toString();
    QJsonObject lockResult;
    {
        QSqlDatabase lock = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        lock.setDatabaseName(QDir(session.accountDirectory).filePath(QStringLiteral("user_Information.db")));
        QVERIFY(lock.open());
        QSqlQuery query(lock);
        QVERIFY(query.exec(QStringLiteral("BEGIN IMMEDIATE")));
        QVERIFY(session.addFields(QStringLiteral("O03LockRetry"), fixtureSecret, 1));
        bool observedBusy = false;
        bool userStayed = false;
        bool mainStayed = false;
        bool identityStayed = false;
        bool logoutQueued = true;
        bool logoutDisabled = false;
        bool closeAccepted = true;
        const int firstHeartbeat = probe.gaps.size();
        session.rejected->clear();
        QTimer::singleShot(15, session.window, [&]()
        {
            observedBusy = session.accountPage->isBusy();
            QTest::mouseClick(session.button("btnHospInfo"), Qt::LeftButton);
            userStayed = session.window->currentUserPage() == 0;
            QTest::mouseClick(session.button("btnReportAdptive"), Qt::LeftButton);
            mainStayed = session.window->currentMainPage() == 6;
            QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
            logoutDisabled = !session.button("btnLoginOut")->isEnabled();
            logoutQueued = session.window->requestLogout();
            identityStayed = session.window->accountSession().name == QStringLiteral("offlineEngineer");
            closeAccepted = session.window->close();
        });
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(session.clickAccount("btnAdd", false));
        QVERIFY(observedBusy && userStayed && mainStayed && identityStayed && !closeAccepted
            && logoutDisabled && !logoutQueued);
        QVERIFY(session.rejected->size() >= 4);
        QCOMPARE(session.snapshot(), beforeLock);
        const int heartbeatCount = probe.gaps.size() - firstHeartbeat;
        QVERIFY(heartbeatCount >= 5);
        double maxGap = 0;
        for (int index = firstHeartbeat; index < probe.gaps.size(); ++index)
        {
            maxGap = qMax(maxGap, probe.gaps.at(index));
        }
        lockResult = {{QStringLiteral("operation"), QStringLiteral("account Add under BEGIN IMMEDIATE")},
            {QStringLiteral("elapsed_with_original_dialog_ms"), elapsed.nsecsElapsed() / 1000000.0},
            {QStringLiteral("heartbeat_count"), heartbeatCount}, {QStringLiteral("max_gap_ms"), maxGap},
            {QStringLiteral("navigation_rejections"), session.rejected->size()},
            {QStringLiteral("user_navigation_rejected"), userStayed}, {QStringLiteral("main_navigation_rejected"), mainStayed},
            {QStringLiteral("logout_rejected"), identityStayed}, {QStringLiteral("shell_close_rejected"), !closeAccepted},
            {QStringLiteral("logout_button_disabled"), logoutDisabled}, {QStringLiteral("logout_request_queued"), logoutQueued},
            {QStringLiteral("partial_commit"), false}};
        QVERIFY(query.exec(QStringLiteral("ROLLBACK")));
        lock.close();
    }
    QSqlDatabase::removeDatabase(connection);
    QVERIFY(session.addFields(QStringLiteral("O03LockRetry"), fixtureSecret, 1));
    QVERIFY(session.clickAccount("btnAdd", true));
    session.clearPasswords();
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("lock_recovered.png"))));
    checked(QStringLiteral("account busy shell guards and recovery"), QStringLiteral("locked account write; hospital/main/logout/close attempts"), QStringLiteral("all four refused; no partial write; retry succeeds"), lockResult);

    const QJsonObject actual = session.snapshot();
    QCOMPARE(actual.value(QStringLiteral("accounts")).toObject().value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
    QCOMPARE(actual.value(QStringLiteral("hospital")).toObject().value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
    QCOMPARE(actual.value(QStringLiteral("accounts")).toObject().value(QStringLiteral("rows")).toArray().size(), 7);
    QCOMPARE(actual.value(QStringLiteral("hospital")).toObject(), persistedHospital);
    QVERIFY(session.passwordsEmpty());
    QVERIFY(session.window->close());
    QVERIFY(session.stopWorkers());
    const QJsonObject response = probe.result();
    QVERIFY(response.value(QStringLiteral("navigation")).toObject().value(QStringLiteral("samples")).toInt() >= 100);
    QVERIFY(probe.gaps.size() > 20);
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QJsonObject result{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("window_size"), QStringLiteral("1920x1080")},
        {QStringLiteral("logical_dpi"), screen ? screen->logicalDotsPerInch() : 0},
        {QStringLiteral("device_pixel_ratio"), screen ? screen->devicePixelRatio() : 0},
        {QStringLiteral("mode"), QStringLiteral("O03-2 offline shell with O01 and anonymous O02; no production login or user_powers ACL claims")},
        {QStringLiteral("cases"), cases}, {QStringLiteral("response"), response},
        {QStringLiteral("actual_public"), actual}, {QStringLiteral("repeat_page_transitions"), 100},
        {QStringLiteral("page_instances"), QJsonObject{{QStringLiteral("hospital"), 1}, {QStringLiteral("account"), 1}}},
        {QStringLiteral("worker_initializations"), QJsonObject{{QStringLiteral("hospital"), session.hospitalReadyCount}, {QStringLiteral("account"), session.accountReadyCount}}},
        {QStringLiteral("both_workers_closed_and_stopped"), true}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("write_result.json")), result));
    QVERIFY(savePublicCsv(QDir(directory_).filePath(QStringLiteral("public_rows.csv")), actual));
    qInfo().noquote() << "SHELL_ACCOUNT_PUBLIC_ACTUAL" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
}

void ShellAccountTest::readExperiment()
{
    QFile file(QDir(directory_).filePath(QStringLiteral("write_result.json")));
    QVERIFY2(file.open(QIODevice::ReadOnly), "Run writeExperiment in another process first");
    const QJsonObject writeResult = QJsonDocument::fromJson(file.readAll()).object();
    QVERIFY(writeResult.value(QStringLiteral("process_id")).toDouble() != double(QCoreApplication::applicationPid()));
    DialogAnswers dialogs;
    Session session(directory_, QString());
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.guestClean());
    QCOMPARE(session.window->currentUserPage(), 0);
    QVERIFY(session.navigationClick("btnHospInfo", 6, 1));
    QCOMPARE(session.hospitalName()->text(), kHospital);
    auto *table = session.hospitalPage->findChild<QTableWidget *>(QStringLiteral("tableInfo"));
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(qobject_cast<QLineEdit *>(table->cellWidget(0, 0))->text(), kDepartment);
    QCOMPARE(qobject_cast<QLineEdit *>(table->cellWidget(0, 2))->text(), kNote);
    QVERIFY(session.navigationClick("btnAccount", 6, 0));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QCOMPARE(session.table()->rowCount(), 5);
    QStringList displayed;
    for (int row = 0; row < session.table()->rowCount(); ++row)
    {
        displayed.append(session.table()->item(row, 2)->text());
    }
    QVERIFY(displayed.contains(QStringLiteral("O03Retained")));
    QVERIFY(displayed.contains(QStringLiteral("O03LockRetry")));
    QVERIFY(!displayed.contains(QStringLiteral("O03SelfDelete")));
    const QJsonObject actual = session.snapshot();
    QCOMPARE(actual, writeResult.value(QStringLiteral("actual_public")).toObject());
    QVERIFY(session.passwordsEmpty());
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("reopened.png"))));
    QVERIFY(session.window->close());
    QVERIFY(session.stopWorkers());
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("read_result.json")),
        {{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
         {QStringLiteral("writer_process_id"), writeResult.value(QStringLiteral("process_id"))},
         {QStringLiteral("new_process_readback"), true}, {QStringLiteral("actual_public"), actual},
         {QStringLiteral("both_workers_closed_and_stopped"), true}}));
    qInfo().noquote() << "SHELL_ACCOUNT_REOPENED_PUBLIC_ACTUAL" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
}

void ShellAccountTest::navigationResponsivenessExperiment()
{
    const QString runtime = QDir(directory_).filePath(QStringLiteral("focus_runtime"));
    QVERIFY2(!QFileInfo::exists(runtime), "Focused navigation runtime must be new");
    const QString fixtureSecret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Session session(runtime, fixtureSecret);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QCOMPARE(session.window->currentMainPage(), 6);
    QCOMPARE(session.window->currentUserPage(), 0);

    QJsonObject response;
    {
        // Separate the measured navigation workload from fixture startup,
        // session reload, screenshots, edits, external SQL and modal dialogs.
        // This measures the observed distribution; it sets no new threshold
        // and does not attribute the broader experiment's earlier long gap.
        Probe probe;
        for (int iteration = 0; iteration < 50; ++iteration)
        {
            QVERIFY(session.navigationClick("btnHospInfo", 6, 1, &probe));
            QTest::qWait(10);
            QVERIFY(session.navigationClick("btnAccount", 6, 0, &probe));
            QTest::qWait(10);
            QVERIFY(!session.accountPage->isBusy());
            QVERIFY(!session.hospitalPage->isBusy());
        }
        response = probe.result();
    }
    QCOMPARE(response.value(QStringLiteral("navigation")).toObject()
        .value(QStringLiteral("samples")).toInt(), 100);
    QVERIFY(response.value(QStringLiteral("heartbeat")).toObject()
        .value(QStringLiteral("samples")).toInt() > 0);
    response.insert(QStringLiteral("resource_condition"),
        QStringLiteral("Focused QtTest 100 real mouse navigation transitions after ready/login, 10 ms event-loop interval after each transition to allow drawing/timers; no PNG capture, external SQL, editing or dialogs during probe"));
    response.insert(QStringLiteral("event_loop_interval_after_each_navigation_ms"), 10);

    // The source shell permits manual logout while the simulated instrument
    // state is running. Exercise the original button after timing is finished.
    QVERIFY(session.login(QStringLiteral("offlineEngineer"), false));
    QVERIFY(!session.window->accountSession().actionReady);
    QVERIFY(session.button("btnLoginOut")->isEnabled());
    session.logout->clear();
    QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
    QVERIFY(waitUntil([&session]()
        { return !session.accountPage->isBusy() && session.guestClean(); }));
    QCOMPARE(session.logout->size(), 1);
    QVERIFY(!session.window->accountSession().actionReady);
    QVERIFY(session.window->close());
    QVERIFY(session.stopWorkers());
    const QJsonObject evidence{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("window_size"), QStringLiteral("1920x1080")},
        {QStringLiteral("mode"), QStringLiteral("O03-2 focused navigation timing, followed by separate running-state manual logout")},
        {QStringLiteral("repeat_page_transitions"), 100}, {QStringLiteral("response"), response},
        {QStringLiteral("navigation_measurement_boundary"),
            QStringLiteral("QTest mouse injection to navigationChanged; not compositor/pixel presentation; no new acceptance threshold or attribution of previous long gap")},
        {QStringLiteral("running_manual_logout_input"),
            QStringLiteral("offlineEngineer, simulated actionReady=false, original btnLoginOut mouse click")},
        {QStringLiteral("running_manual_logout_expected"),
            QStringLiteral("Guest role 0; cleared usernames/passwords/table; preserve simulated running state")},
        {QStringLiteral("running_manual_logout_passed"), true},
        {QStringLiteral("running_manual_logout_signal_count"), session.logout->size()},
        {QStringLiteral("both_workers_closed_and_stopped"), true}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("navigation_focus.json")), evidence));
    qInfo().noquote() << "SHELL_ACCOUNT_FOCUSED_NAVIGATION" << QJsonDocument(response).toJson(QJsonDocument::Compact);
}

void ShellAccountTest::applicationExitExperiment()
{
#ifndef Q_OS_LINUX
    QSKIP("Formal PID-targeted native close and /proc observation require Linux/XCB");
#else
    const QString application = qEnvironmentVariable("SN100_SHELL_APP");
    QVERIFY2(QFileInfo(application).isAbsolute() && QFileInfo(application).isExecutable(), "SN100_SHELL_APP must name the freshly built product");
    const QString finder = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    QVERIFY2(!finder.isEmpty(), "xdotool is required for PID-limited window discovery");
    const QString runtime = QDir(directory_).filePath(QStringLiteral("formal_runtime"));
    QVERIFY2(!QFileInfo::exists(runtime), "Formal runtime must be new");
    struct TemporaryFixtureInput
    {
        QString path;
        ~TemporaryFixtureInput() { QFile::remove(path); }
    } fixtureInput{QDir(directory_).filePath(QStringLiteral("formal_fixture_input.txt"))};
    QFile fixtureFile(fixtureInput.path);
    QVERIFY(fixtureFile.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    QVERIFY(fixtureFile.write(QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8()) > 0);
    fixtureFile.close();
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(application, {QStringLiteral("--offline"), QStringLiteral("--accounts"),
        QStringLiteral("--data-dir"), runtime, QStringLiteral("--viewport"), QStringLiteral("1600x900"),
        QStringLiteral("--seed-anonymous"), QStringLiteral("--test-secret-file"), fixtureInput.path});
    QVERIFY(process.waitForStarted(10000));
    const qint64 pid = process.processId();
    QByteArray output;
    QVERIFY(waitUntil([&]()
    {
        output += process.readAll();
        return output.contains("SHELL_READY") || process.state() == QProcess::NotRunning;
    }));
    QVERIFY2(process.state() != QProcess::NotRunning, output.constData());
    QVERIFY(QFile::remove(fixtureInput.path));
    QByteArray windowId;
    QVERIFY(waitUntil([&]()
    {
        QProcess search;
        search.start(finder, {QStringLiteral("search"), QStringLiteral("--onlyvisible"), QStringLiteral("--pid"), QString::number(pid)});
        if (!search.waitForFinished(1000))
        {
            return false;
        }
        windowId = search.readAllStandardOutput().trimmed().split('\n').value(0);
        return search.exitCode() == 0 && !windowId.isEmpty();
    }));
    QElapsedTimer elapsed;
    elapsed.start();
    QJsonArray resources;
    for (int index = 0; index <= 10; ++index)
    {
        QVERIFY(process.state() != QProcess::NotRunning);
        QJsonObject sample = resourceSample(pid);
        sample.insert(QStringLiteral("elapsed_ms"), elapsed.nsecsElapsed() / 1000000.0);
        resources.append(sample);
        if (index < 10)
        {
            QTest::qWait(500);
        }
    }
    bool validId = false;
    const qulonglong target = windowId.toULongLong(&validId);
    QVERIFY(validId && target > 0);
    QVERIFY(nativeClose(target));
    QVERIFY(waitUntil([&]() { return process.state() == QProcess::NotRunning; }));
    output += process.readAll();
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY2(output.contains("HOSPITAL_STORE_CLOSED"), output.constData());
    QVERIFY2(output.contains("ACCOUNT_STORE_CLOSED"), output.constData());
    QVERIFY2(output.contains("THREADS_STOPPED true"), output.constData());
    QVERIFY2(QRegularExpression(QStringLiteral("EXIT code=\\s*0(?:\\s|$)"))
        .match(QString::fromUtf8(output)).hasMatch(), output.constData());
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    QVERIFY(ticksPerSecond > 0);
    const QJsonObject first = resources.first().toObject();
    const QJsonObject last = resources.last().toObject();
    const double seconds = (last.value(QStringLiteral("elapsed_ms")).toDouble()
        - first.value(QStringLiteral("elapsed_ms")).toDouble()) / 1000.0;
    QVERIFY(seconds >= 5);
    QVERIFY(first.contains(QStringLiteral("cpu_ticks")) && last.contains(QStringLiteral("cpu_ticks")));
    const double cpuSeconds = (last.value(QStringLiteral("cpu_ticks")).toDouble()
        - first.value(QStringLiteral("cpu_ticks")).toDouble()) / ticksPerSecond;
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("formal_app_exit.json")),
        {{QStringLiteral("process_id"), double(pid)}, {QStringLiteral("test_process_id"), double(QCoreApplication::applicationPid())},
         {QStringLiteral("application"), application}, {QStringLiteral("mode"), QStringLiteral("--offline --accounts, default Guest, independent anonymous account store; transient fixture input removed")},
         {QStringLiteral("requested_viewport"), QStringLiteral("1600x900")},
         {QStringLiteral("close_injection"), QStringLiteral("X11 WM_DELETE_WINDOW exact PID-matched visible window")},
         {QStringLiteral("exit_code"), 0}, {QStringLiteral("both_workers_closed_and_stopped"), true},
         {QStringLiteral("capture_scope"), QStringLiteral("No cross-process screenshot acceptance; direct integrated QWidget captures reviewed separately")},
         {QStringLiteral("sampled_seconds"), seconds}, {QStringLiteral("sampling_interval_ms"), 500},
         {QStringLiteral("cpu_percent_one_core"), cpuSeconds / seconds * 100.0},
         {QStringLiteral("resources"), resources}, {QStringLiteral("actual_output"), QString::fromUtf8(output)}}));
    qInfo().noquote() << "FORMAL_SHELL_ACCOUNT_OUTPUT" << QString::fromUtf8(output);
#endif
}

QTEST_MAIN(ShellAccountTest)
#include "shell_account_test.moc"
