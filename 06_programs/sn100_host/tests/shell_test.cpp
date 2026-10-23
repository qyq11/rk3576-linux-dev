// O03-1 integration evidence. All interaction targets are real Widgets and
// all persistence is confined to a caller-supplied new anonymous directory.
#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibrary>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QScreen>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <memory>

#include "data/hospital_store.h"
#include "shell/frmmain.h"
#include "ui/frmhospinfosetting.h"

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#include <unistd.h>
#endif

namespace
{
const QString kHospital = QStringLiteral("O03脱机验证医院");
const QString kFinalHospital = QStringLiteral("O03保存重开医院 O'Brien");
const QString kFinalNote = QStringLiteral("锁库恢复后保存 O'Brien 中文");
const QStringList kTypes = {QStringLiteral("keshi"), QStringLiteral("bingren"),
    QStringLiteral("songjian"), QStringLiteral("jianyan"), QStringLiteral("shenhe")};
const QStringList kCategories = {QStringLiteral("btnKeShiInfo"), QStringLiteral("btnBingRenInfo"),
    QStringLiteral("btnSongJianInfo"), QStringLiteral("btnJianYanInfo"), QStringLiteral("btnShenHeInfo")};
const QStringList kNames = {QStringLiteral("血液科"), QStringLiteral("门诊"),
    QStringLiteral("送检员"), QStringLiteral("检验员"), QStringLiteral("审核员")};
const QStringList kMnemonics = {QStringLiteral("XYK"), QStringLiteral("MZ"),
    QStringLiteral("SJY"), QStringLiteral("JYY"), QStringLiteral("SHY")};
const QStringList kDetails = {QStringLiteral("匿名科室 O'Brien"), QStringLiteral("匿名门诊"),
    QStringLiteral("SJ003"), QStringLiteral("JY003"), QStringLiteral("SH003")};
const QStringList kMainButtons = {QStringLiteral("btnReagentAdptive"), QStringLiteral("btnSampleAdptive"),
    QStringLiteral("btnReportAdptive"), QStringLiteral("btnCalibrateAdptive"),
    QStringLiteral("btnQualityAdptive"), QStringLiteral("btnSystemAdptive"),
    QStringLiteral("btnUserSettingAdptive"), QStringLiteral("btnDivStatusAdptive")};
const QStringList kUserButtons = {QStringLiteral("btnAccount"), QStringLiteral("btnHospInfo"),
    QStringLiteral("btnLanguage"), QStringLiteral("btnPrinter"), QStringLiteral("btnLis")};

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

bool saveJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            && file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) > 0;
}

#ifdef Q_OS_LINUX
bool sendNativeWindowClose(qulonglong windowId, QString *error)
{
    // A WM_DELETE_WINDOW message exercises the real closeEvent through XCB.
    // Resolving Xlib here keeps the ordinary test and Windows link inputs
    // independent of the Linux-only native close-injection helper.
    QLibrary library(QStringLiteral("libX11.so.6"));
    if (!library.load())
    {
        *error = library.errorString();
        return false;
    }
    using OpenDisplay = Display *(*)(const char *);
    using InternAtom = Atom (*)(Display *, const char *, Bool);
    using SendEvent = Status (*)(Display *, Window, Bool, long, XEvent *);
    using Flush = int (*)(Display *);
    using CloseDisplay = int (*)(Display *);
    const auto openDisplay = reinterpret_cast<OpenDisplay>(library.resolve("XOpenDisplay"));
    const auto internAtom = reinterpret_cast<InternAtom>(library.resolve("XInternAtom"));
    const auto sendEvent = reinterpret_cast<SendEvent>(library.resolve("XSendEvent"));
    const auto flush = reinterpret_cast<Flush>(library.resolve("XFlush"));
    const auto closeDisplay = reinterpret_cast<CloseDisplay>(library.resolve("XCloseDisplay"));
    if (!openDisplay || !internAtom || !sendEvent || !flush || !closeDisplay)
    {
        *error = QStringLiteral("A required libX11 native-close symbol is unavailable: ") + library.errorString();
        return false;
    }
    Display *display = openDisplay(nullptr);
    if (!display)
    {
        *error = QStringLiteral("XOpenDisplay could not open the current DISPLAY");
        return false;
    }
    XEvent event = {};
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = static_cast<Window>(windowId);
    event.xclient.message_type = internAtom(display, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = static_cast<long>(internAtom(display, "WM_DELETE_WINDOW", False));
    event.xclient.data.l[1] = CurrentTime;
    const bool sent = sendEvent(display, static_cast<Window>(windowId), False, NoEventMask, &event) != 0;
    flush(display);
    closeDisplay(display);
    if (!sent)
    {
        *error = QStringLiteral("XSendEvent rejected WM_DELETE_WINDOW for the PID-matched target window");
    }
    return sent;
}

QByteArray readProcFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QJsonValue procNumber(const QString &text, const QString &key)
{
    const QRegularExpression expression(QStringLiteral("^%1:\\s+(\\d+)").arg(QRegularExpression::escape(key)),
                                        QRegularExpression::MultilineOption);
    const auto match = expression.match(text);
    return match.hasMatch() ? QJsonValue(match.captured(1).toDouble()) : QJsonValue(QJsonValue::Null);
}

QJsonObject sampleFormalProcess(qint64 pid, double elapsedMilliseconds)
{
    const QString prefix = QStringLiteral("/proc/%1/").arg(pid);
    const QString status = QString::fromUtf8(readProcFile(prefix + QStringLiteral("status")));
    const QString smaps = QString::fromUtf8(readProcFile(prefix + QStringLiteral("smaps_rollup")));
    const QByteArray stat = readProcFile(prefix + QStringLiteral("stat"));
    // The command field is parenthesized and can contain spaces; fields after
    // its final ')' start at field 3. utime/stime are fields 14 and 15.
    const int commandEnd = stat.lastIndexOf(')');
    const QList<QByteArray> fields = commandEnd < 0 ? QList<QByteArray>()
        : stat.mid(commandEnd + 2).simplified().split(' ');
    bool userValid = false;
    bool systemValid = false;
    const qint64 userTicks = fields.value(11).toLongLong(&userValid);
    const qint64 systemTicks = fields.value(12).toLongLong(&systemValid);
    const QDir descriptors(prefix + QStringLiteral("fd"));
    const QJsonValue fdCount = descriptors.exists()
        ? QJsonValue(descriptors.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden).size())
        : QJsonValue(QJsonValue::Null);
    return {{QStringLiteral("elapsed_ms"), elapsedMilliseconds},
        {QStringLiteral("rss_kib"), procNumber(status, QStringLiteral("VmRSS"))},
        {QStringLiteral("peak_rss_kib"), procNumber(status, QStringLiteral("VmHWM"))},
        {QStringLiteral("pss_kib"), procNumber(smaps, QStringLiteral("Pss"))},
        {QStringLiteral("threads"), procNumber(status, QStringLiteral("Threads"))},
        {QStringLiteral("fd_count"), fdCount},
        {QStringLiteral("cpu_ticks"), userValid && systemValid
            ? QJsonValue(double(userTicks + systemTicks)) : QJsonValue(QJsonValue::Null)},
        {QStringLiteral("raw_status"), status},
        {QStringLiteral("raw_smaps_rollup"), smaps},
        {QStringLiteral("raw_stat"), QString::fromUtf8(stat)}};
}
#endif

QString quotedCsv(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

bool saveCsv(const QString &path, const QJsonObject &actual)
{
    QString csv = QStringLiteral("hospital_name,id,type,column_one,column_two,column_three\r\n");
    for (const QJsonValue &value : actual.value(QStringLiteral("rows")).toArray())
    {
        const QJsonObject row = value.toObject();
        csv += quotedCsv(actual.value(QStringLiteral("hospital_name")).toString()) + QLatin1Char(',')
                + QString::number(qint64(row.value(QStringLiteral("id")).toDouble())) + QLatin1Char(',')
                + quotedCsv(row.value(QStringLiteral("type")).toString()) + QLatin1Char(',')
                + quotedCsv(row.value(QStringLiteral("column_one")).toString()) + QLatin1Char(',')
                + quotedCsv(row.value(QStringLiteral("column_two")).toString()) + QLatin1Char(',')
                + quotedCsv(row.value(QStringLiteral("column_three")).toString()) + QStringLiteral("\r\n");
    }
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(csv.toUtf8()) > 0;
}

// Independent read-only verification uses its own connection in the test
// thread, never a worker-owned SQL connection or reference/production data.
bool snapshot(const QString &path, QJsonObject *result, QString *error)
{
    const QString connection = QStringLiteral("shell-evidence-") + QUuid::createUuid().toString();
    bool success = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(path);
        if (!database.open())
        {
            *error = database.lastError().text();
        }
        else
        {
            QSqlQuery query(database);
            success = query.exec(QStringLiteral("SELECT value FROM app_settings WHERE key='hospital/name'"))
                    && query.next();
            if (success)
            {
                result->insert(QStringLiteral("hospital_name"), query.value(0).toString());
                query.finish();
                success = query.exec(QStringLiteral("SELECT id,column_one,column_two,column_three,type "
                                                   "FROM hospital_information ORDER BY type,id"));
            }
            QJsonArray rows;
            while (success && query.next())
            {
                rows.append(QJsonObject{{QStringLiteral("id"), query.value(0).toDouble()},
                    {QStringLiteral("column_one"), query.value(1).toString()},
                    {QStringLiteral("column_two"), query.value(2).toString()},
                    {QStringLiteral("column_three"), query.value(3).toString()},
                    {QStringLiteral("type"), query.value(4).toString()}});
            }
            result->insert(QStringLiteral("rows"), rows);
            query.finish();
            success = success && query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next();
            if (success)
            {
                result->insert(QStringLiteral("integrity_check"), query.value(0).toString());
                success = query.value(0).toString() == QStringLiteral("ok");
            }
            if (!success)
            {
                *error = query.lastError().text();
            }
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return success;
}

QJsonArray expectedRows()
{
    QJsonArray rows;
    for (int index = 0; index < kTypes.size(); ++index)
    {
        rows.append(QJsonObject{{QStringLiteral("type"), kTypes[index]},
            {QStringLiteral("column_one"), kNames[index]},
            {QStringLiteral("column_two"), kMnemonics[index]},
            {QStringLiteral("column_three"), index == 0 ? kFinalNote : kDetails[index]}});
    }
    return rows;
}

bool matchesExpected(const QJsonObject &actual)
{
    if (actual.value(QStringLiteral("hospital_name")).toString() != kFinalHospital
            || actual.value(QStringLiteral("rows")).toArray().size() != kTypes.size()
            || actual.value(QStringLiteral("integrity_check")).toString() != QStringLiteral("ok"))
    {
        return false;
    }
    for (const QJsonValue &expected : expectedRows())
    {
        bool found = false;
        for (const QJsonValue &value : actual.value(QStringLiteral("rows")).toArray())
        {
            QJsonObject row = value.toObject();
            if (row.take(QStringLiteral("id")).toDouble() <= 0)
            {
                return false;
            }
            found = found || row == expected.toObject();
        }
        if (!found)
        {
            return false;
        }
    }
    return true;
}

bool paste(QLineEdit *edit, const QString &text)
{
    if (!edit || !edit->isEnabled() || edit->isReadOnly() || !edit->isVisible())
    {
        return false;
    }
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
    return waitUntil([edit, text]() { return edit->text() == text; }, 1000);
}

struct DialogAnswer
{
    bool seen = false;
    bool screenshotSaved = false;
    QString objectName;
    QString text;
};

std::shared_ptr<DialogAnswer> answerNextDialog(QMessageBox::StandardButton answer,
                                             const QString &screenshotPath = QString())
{
    auto state = std::make_shared<DialogAnswer>();
    QTimer *timer = new QTimer(qApp);
    timer->setInterval(5);
    QElapsedTimer deadline;
    deadline.start();
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, state, answer, screenshotPath, deadline]()
    {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (dialog && dialog->button(answer))
        {
            state->seen = true;
            state->objectName = dialog->objectName();
            state->text = dialog->text();
            if (!screenshotPath.isEmpty())
            {
                state->screenshotSaved = dialog->grab().save(screenshotPath);
            }
            timer->stop();
            QTest::mouseClick(dialog->button(answer), Qt::LeftButton);
            timer->deleteLater();
        }
        else if (deadline.elapsed() > 10000)
        {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
    return state;
}

class ResponseProbe : public QObject
{
public:
    ResponseProbe()
    {
        clock.start();
        previous = clock.nsecsElapsed();
        timer.setTimerType(Qt::PreciseTimer);
        timer.setInterval(10);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            const qint64 now = clock.nsecsElapsed();
            gaps.append(double(now - previous) / 1000000.0);
            previous = now;
        });
        timer.start();
    }

    void operation(const QString &name, double milliseconds)
    {
        operations.append(QJsonObject{{QStringLiteral("operation"), name},
                                      {QStringLiteral("elapsed_ms"), milliseconds}});
    }

    static QJsonObject summary(QList<double> values)
    {
        std::sort(values.begin(), values.end());
        return {{QStringLiteral("samples"), values.size()},
            {QStringLiteral("p95_ms"), values.isEmpty() ? 0 : values.at(qMin(values.size() - 1, int(values.size() * 0.95)))},
            {QStringLiteral("max_ms"), values.isEmpty() ? 0 : values.last()}};
    }

    QJsonObject result() const
    {
        QJsonArray heartbeat;
        for (double gap : gaps)
        {
            heartbeat.append(gap);
        }
        QList<double> navigation;
        for (const QJsonValue &value : operations)
        {
            const QJsonObject operation = value.toObject();
            if (operation.value(QStringLiteral("operation")).toString().startsWith(QStringLiteral("navigation_")))
            {
                navigation.append(operation.value(QStringLiteral("elapsed_ms")).toDouble());
            }
        }
        return {{QStringLiteral("timer_interval_ms"), 10},
            {QStringLiteral("heartbeat"), summary(gaps)},
            {QStringLiteral("heartbeat_gap_ms"), heartbeat},
            {QStringLiteral("navigation"), summary(navigation)},
            {QStringLiteral("navigation_measurement"), QStringLiteral("QTest mouse injection to navigationChanged after page selection; excludes compositor/pixel presentation")},
            {QStringLiteral("operations"), operations}};
    }

    QTimer timer;
    QElapsedTimer clock;
    qint64 previous = 0;
    QList<double> gaps;
    QJsonArray operations;
};

class Session
{
public:
    explicit Session(const QString &directory)
    {
        HospitalStore::registerTypes();
        store = new HospitalStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        window = new frmMain(store);
        window->setDataDirectory(directory);
        page = window->hospitalPage();
        operations = new QSignalSpy(page, &frmHospInfoSetting::operationFinished);
        navigation = new QSignalSpy(window, &frmMain::navigationChanged);
        rejected = new QSignalSpy(window, &frmMain::navigationRejected);
        QObject::connect(&thread, &QThread::started, store, [this, directory]()
        {
            store->initialize(QDir(directory).filePath(QStringLiteral("hospital_Information.db")),
                              QDir(directory).filePath(QStringLiteral("config.ini")));
        });
        window->show();
        thread.start();
    }

    ~Session()
    {
        delete operations;
        delete navigation;
        delete rejected;
        delete window;
        if (thread.isRunning())
        {
            QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
        }
    }

    QAbstractButton *button(const QString &name) const
    {
        return window->findChild<QAbstractButton *>(name);
    }

    QTableWidget *table() const
    {
        return page->findChild<QTableWidget *>(QStringLiteral("tableInfo"));
    }

    QLineEdit *cell(int row, int column) const
    {
        return qobject_cast<QLineEdit *>(table()->cellWidget(row, column));
    }

    QLineEdit *hospital() const
    {
        return page->findChild<QLineEdit *>(QStringLiteral("ldtHospName"));
    }

    QLabel *status() const
    {
        return page->findChild<QLabel *>(QStringLiteral("statusLabel"));
    }

    QLabel *visibleStatus() const
    {
        return window->findChild<QLabel *>(QStringLiteral("lblDivState"));
    }

    bool ready() const
    {
        return waitUntil([this]() { return page->isReady() && !page->isBusy(); });
    }

    bool clickOperation(const QString &name, ResponseProbe *response = nullptr)
    {
        QAbstractButton *control = button(name);
        if (!control || !control->isVisible() || !control->isEnabled())
        {
            return false;
        }
        operations->clear();
        QElapsedTimer elapsed;
        elapsed.start();
        QTest::mouseClick(control, Qt::LeftButton);
        const bool completed = waitUntil([this]() { return !operations->isEmpty() && !page->isBusy(); });
        if (response)
        {
            response->operation(name, double(elapsed.nsecsElapsed()) / 1000000.0);
        }
        return completed && operations->last().at(1).toBool();
    }

    bool category(int index, ResponseProbe *response = nullptr)
    {
        QAbstractButton *control = button(kCategories.at(index));
        return control && (control->isChecked() ? !page->isBusy() : clickOperation(kCategories.at(index), response));
    }

    bool clickNavigation(const QString &name, int expectedMain, int expectedUser,
                         ResponseProbe *response = nullptr)
    {
        QAbstractButton *control = button(name);
        if (!control || !control->isVisible() || !control->isEnabled())
        {
            return false;
        }
        navigation->clear();
        QElapsedTimer elapsed;
        elapsed.start();
        double signalMilliseconds = -1;
        const QMetaObject::Connection measurement = QObject::connect(window, &frmMain::navigationChanged,
            window, [&elapsed, &signalMilliseconds](int, int)
        {
            if (signalMilliseconds < 0)
            {
                signalMilliseconds = double(elapsed.nsecsElapsed()) / 1000000.0;
            }
        });
        QTest::mouseClick(control, Qt::LeftButton);
        const bool completed = waitUntil([this, expectedMain, expectedUser]()
        {
            return !navigation->isEmpty() && window->currentMainPage() == expectedMain
                    && window->currentUserPage() == expectedUser;
        });
        QObject::disconnect(measurement);
        if (response && signalMilliseconds >= 0)
        {
            response->operation(QStringLiteral("navigation_") + name, signalMilliseconds);
        }
        // Original user-setting Material buttons are intentionally not
        // checkable; their selected page is represented by the stack index.
        return completed && (kUserButtons.contains(name)
                             ? !control->isCheckable() : control->isChecked());
    }

    bool visiblePlaceholder() const
    {
        for (QLabel *label : window->findChildren<QLabel *>(QStringLiteral("offlinePlaceholderLabel")))
        {
            if (label->isVisible() && label->text().contains(QStringLiteral("尚未接入（脱机）")))
            {
                return true;
            }
        }
        return false;
    }

    QThread thread;
    HospitalStore *store = nullptr;
    frmMain *window = nullptr;
    frmHospInfoSetting *page = nullptr;
    QSignalSpy *operations = nullptr;
    QSignalSpy *navigation = nullptr;
    QSignalSpy *rejected = nullptr;
};
}

class ShellTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void writeExperiment();
    void readExperiment();
    void applicationExitExperiment();

private:
    QString directory_;
    QString databasePath_;
};

void ShellTest::initTestCase()
{
    directory_ = QString::fromUtf8(qgetenv("SN100_SHELL_EVIDENCE_DIR"));
    QVERIFY2(!directory_.isEmpty() && QDir::isAbsolutePath(directory_),
             "SN100_SHELL_EVIDENCE_DIR must name an absolute isolated anonymous experiment directory");
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"),
             "This integration test requires visible WSLg/XCB, not offscreen rendering");
    QVERIFY(QDir().mkpath(directory_));
    databasePath_ = QDir(directory_).filePath(QStringLiteral("hospital_Information.db"));
    qApp->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    qApp->setStyle(QStringLiteral("Fusion"));
    qInfo().noquote() << "SHELL_EVIDENCE_DIR" << directory_ << "PID" << QCoreApplication::applicationPid();
}

void ShellTest::writeExperiment()
{
    QVERIFY2(!QFileInfo::exists(databasePath_), "Refusing to overwrite an existing experiment database");
    QVERIFY2(!QFileInfo::exists(QDir(directory_).filePath(QStringLiteral("config.ini"))),
             "Refusing to overwrite an existing experiment INI");
    Session session(directory_);
    QVERIFY2(session.page && session.ready(), "O01 did not become ready in the shell");
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    ResponseProbe response;
    QJsonArray assertions;
    const auto checked = [&assertions](const QString &name)
    {
        assertions.append(QJsonObject{{QStringLiteral("assertion"), name}, {QStringLiteral("passed"), true}});
    };
    QCOMPARE(session.window->currentMainPage(), 6);
    QCOMPARE(session.window->currentUserPage(), 1);
    for (const QString &name : kUserButtons)
    {
        QVERIFY(session.button(name));
        QVERIFY(!session.button(name)->isCheckable());
    }
    QVERIFY(session.page->isVisible());
    QCOMPARE(session.window->findChildren<frmHospInfoSetting *>().size(), 1);
    QCOMPARE(session.table()->rowCount(), 0);
    QVERIFY(paste(session.hospital(), kHospital));
    for (int index = 0; index < kTypes.size(); ++index)
    {
        QVERIFY2(session.category(index, &response), qPrintable(session.status()->text()));
        QCOMPARE(session.table()->rowCount(), 0);
        QTest::mouseClick(session.button(QStringLiteral("btnAdd")), Qt::LeftButton);
        QCOMPARE(session.table()->rowCount(), 1);
        QVERIFY(paste(session.cell(0, 0), kNames[index]));
        QVERIFY(paste(session.cell(0, 2), kDetails[index]));
        QCOMPARE(session.cell(0, 1)->text(), kMnemonics[index]);
        QVERIFY(session.cell(0, 1)->isReadOnly());
        QVERIFY2(session.clickOperation(QStringLiteral("btnSave"), &response), qPrintable(session.status()->text()));
        QCOMPARE(session.cell(0, 0)->text(), kNames[index]);
        QCOMPARE(session.cell(0, 2)->text(), kDetails[index]);
    }
    QVERIFY(session.category(0, &response));
    checked(QStringLiteral("five_anonymous_dictionary_types_saved_through_embedded_O01"));
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("hospital_saved.png"))));

    // Main navigation must offer the real O01 dirty guard, not merely hide it.
    QJsonObject beforeDiscard;
    QString error;
    QVERIFY2(snapshot(databasePath_, &beforeDiscard, &error), qPrintable(error));
    QVERIFY(paste(session.hospital(), QStringLiteral("主菜单未保存医院")));
    QVERIFY(paste(session.cell(0, 2), QStringLiteral("主菜单未保存草稿")));
    session.rejected->clear();
    auto retainMain = answerNextDialog(QMessageBox::No,
        QDir(directory_).filePath(QStringLiteral("unsaved_dialog.png")));
    QTest::mouseClick(session.button(kMainButtons[2]), Qt::LeftButton);
    QVERIFY(retainMain->seen && retainMain->screenshotSaved);
    QCOMPARE(retainMain->objectName, QStringLiteral("discardChangesDialog"));
    QCOMPARE(session.window->currentMainPage(), 6);
    QCOMPARE(session.window->currentUserPage(), 1);
    QVERIFY(!session.rejected->isEmpty());
    QVERIFY(session.page->hasUnsavedChanges());
    QCOMPARE(session.hospital()->text(), QStringLiteral("主菜单未保存医院"));
    QCOMPARE(session.cell(0, 2)->text(), QStringLiteral("主菜单未保存草稿"));
    QVERIFY(session.button(kMainButtons[6])->isChecked());
    checked(QStringLiteral("main_navigation_cancel_keeps_page_selection_and_draft"));

    auto discardMain = answerNextDialog(QMessageBox::Yes);
    QVERIFY(session.clickNavigation(kMainButtons[2], 2, 1, &response));
    QVERIFY(discardMain->seen);
    QVERIFY(!session.page->hasUnsavedChanges());
    QVERIFY(session.visiblePlaceholder());
    QVERIFY(!session.page->isVisible());
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("navigation_placeholder.png"))));
    QVERIFY(session.clickNavigation(kMainButtons[6], 6, 1, &response));
    QCOMPARE(session.hospital()->text(), kHospital);
    QCOMPARE(session.cell(0, 2)->text(), kDetails[0]);
    QJsonObject afterDiscard;
    QVERIFY2(snapshot(databasePath_, &afterDiscard, &error), qPrintable(error));
    QCOMPARE(afterDiscard, beforeDiscard);
    checked(QStringLiteral("main_navigation_discard_restores_committed_draft_without_database_write"));

    // User-setting subpages use the same guard and retain original names.
    QVERIFY(paste(session.hospital(), QStringLiteral("子菜单未保存医院")));
    session.rejected->clear();
    auto retainUser = answerNextDialog(QMessageBox::No);
    QTest::mouseClick(session.button(kUserButtons[0]), Qt::LeftButton);
    QVERIFY(retainUser->seen);
    QCOMPARE(session.window->currentUserPage(), 1);
    QCOMPARE(session.hospital()->text(), QStringLiteral("子菜单未保存医院"));
    QVERIFY(session.page->hasUnsavedChanges());
    QVERIFY(!session.rejected->isEmpty());
    QVERIFY(!session.button(kUserButtons[1])->isCheckable());
    QCOMPARE(session.window->currentUserPage(), 1);
    auto discardUser = answerNextDialog(QMessageBox::Yes);
    QVERIFY(session.clickNavigation(kUserButtons[2], 6, 2, &response));
    QVERIFY(discardUser->seen);
    QVERIFY(session.visiblePlaceholder());
    QVERIFY(session.clickNavigation(kUserButtons[1], 6, 1, &response));
    QCOMPARE(session.hospital()->text(), kHospital);
    QVERIFY(!session.page->hasUnsavedChanges());
    checked(QStringLiteral("user_navigation_cancel_and_discard_use_O01_guard"));

    // Exercise every original navigation name via actual mouse injection.
    for (int index = 0; index < kMainButtons.size(); ++index)
    {
        if (index == 6)
        {
            continue;
        }
        QVERIFY(session.clickNavigation(kMainButtons[index], index, 1, &response));
        QVERIFY(session.visiblePlaceholder());
        QVERIFY(session.clickNavigation(kMainButtons[6], 6, 1, &response));
    }
    for (int index = 0; index < kUserButtons.size(); ++index)
    {
        if (index == 1)
        {
            continue;
        }
        QVERIFY(session.clickNavigation(kUserButtons[index], 6, index, &response));
        QVERIFY(session.visiblePlaceholder());
        QVERIFY(session.clickNavigation(kUserButtons[1], 6, 1, &response));
    }
    checked(QStringLiteral("all_eight_main_and_five_user_navigation_names_clicked"));

    // The same page object and its prior editor focus survive 100 transitions.
    // Native modal close can leave the WSLg top-level inactive. Activate only
    // this test window before checking application focus; retain the actual
    // editor-focus and all subsequent restoration assertions.
    session.window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(session.window));
    QTest::mouseClick(session.hospital(), Qt::LeftButton);
    QVERIFY(waitUntil([&session]() { return QApplication::focusWidget() == session.hospital(); }));
    frmHospInfoSetting *const originalPage = session.page;
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        const int target = iteration % 2 == 0 ? 2 : 6;
        QVERIFY(session.clickNavigation(kMainButtons[target], target, 1, &response));
        QCOMPARE(session.window->hospitalPage(), originalPage);
        QCOMPARE(session.window->findChildren<frmHospInfoSetting *>().size(), 1);
        if (target == 6)
        {
            QVERIFY(waitUntil([&session]() { return QApplication::focusWidget() == session.hospital(); }));
            QCOMPARE(session.hospital()->text(), kHospital);
            QCOMPARE(session.cell(0, 2)->text(), kDetails[0]);
        }
    }
    checked(QStringLiteral("100_mouse_navigation_transitions_reuse_one_O01_and_restore_editor_focus"));

    // A real SQLite writer lock leaves the GUI responsive while the storage
    // worker waits; no test changes any private busy flag or worker state.
    QJsonObject beforeLock;
    QVERIFY2(snapshot(databasePath_, &beforeLock, &error), qPrintable(error));
    QJsonObject lockMetrics;
    const QString lockConnection = QStringLiteral("shell-writer-lock");
    {
        QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnection);
        locker.setDatabaseName(databasePath_);
        QVERIFY(locker.open());
        QSqlQuery query(locker);
        QVERIFY(query.exec(QStringLiteral("BEGIN IMMEDIATE")));
        QVERIFY(paste(session.hospital(), kFinalHospital));
        QVERIFY(paste(session.cell(0, 2), kFinalNote));
        session.operations->clear();
        session.rejected->clear();
        const int timerStart = response.gaps.size();
        QElapsedTimer elapsed;
        elapsed.start();
        QTest::mouseClick(session.button(QStringLiteral("btnSave")), Qt::LeftButton);
        QVERIFY(session.page->isBusy());
        QTest::mouseClick(session.button(kMainButtons[2]), Qt::LeftButton);
        QCOMPARE(session.window->currentMainPage(), 6);
        QCOMPARE(session.window->currentUserPage(), 1);
        QVERIFY(!session.rejected->isEmpty());
        QVERIFY(session.button(kMainButtons[6])->isChecked());
        QTest::mouseClick(session.button(kUserButtons[0]), Qt::LeftButton);
        QCOMPARE(session.window->currentUserPage(), 1);
        QVERIFY(!session.button(kUserButtons[1])->isCheckable());
        QCOMPARE(session.window->currentUserPage(), 1);
        QVERIFY(!session.window->close());
        QVERIFY(session.window->isVisible());
        QVERIFY(waitUntil([&session]() { return !session.operations->isEmpty() && !session.page->isBusy(); }));
        QVERIFY(!session.operations->last().at(1).toBool());
        const double failedSaveMilliseconds = double(elapsed.nsecsElapsed()) / 1000000.0;
        response.operation(QStringLiteral("locked_save_failure"), failedSaveMilliseconds);
        const int timerSamples = response.gaps.size() - timerStart;
        QVERIFY2(timerSamples >= 5, "10 ms GUI heartbeat did not progress during the real worker lock wait");
        double maximumGap = 0;
        for (int index = timerStart; index < response.gaps.size(); ++index)
        {
            maximumGap = qMax(maximumGap, response.gaps[index]);
        }
        QVERIFY(session.status()->property("isError").toBool());
        QVERIFY(session.visibleStatus());
        QVERIFY(session.visibleStatus()->isVisible());
        QVERIFY(session.visibleStatus()->text().startsWith(QStringLiteral("脱机")));
        QVERIFY(session.visibleStatus()->toolTip().contains(session.status()->text()));
        QVERIFY(session.visibleStatus()->styleSheet().contains(QStringLiteral("E83323"), Qt::CaseInsensitive));
        QVERIFY(session.page->hasUnsavedChanges());
        QCOMPARE(session.hospital()->text(), kFinalHospital);
        QCOMPARE(session.cell(0, 2)->text(), kFinalNote);
        QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("locked_error.png"))));
        QJsonObject failedActual;
        QVERIFY2(snapshot(databasePath_, &failedActual, &error), qPrintable(error));
        QCOMPARE(failedActual, beforeLock);
        QVERIFY(query.exec(QStringLiteral("ROLLBACK")));
        locker.close();
        lockMetrics = {{QStringLiteral("failed_save_ms"), failedSaveMilliseconds},
            {QStringLiteral("heartbeat_samples"), timerSamples},
            {QStringLiteral("heartbeat_max_gap_ms"), maximumGap},
            {QStringLiteral("main_navigation_rejected"), true},
            {QStringLiteral("user_navigation_rejected"), true},
            {QStringLiteral("close_rejected"), true},
            {QStringLiteral("draft_preserved"), true},
            {QStringLiteral("partial_commit"), false},
            {QStringLiteral("actual_error"), session.status()->text()},
            {QStringLiteral("visible_error_text"), session.visibleStatus()->text()},
            {QStringLiteral("visible_error_tooltip"), session.visibleStatus()->toolTip()},
            {QStringLiteral("visible_error_color_checked"), true}};
    }
    QSqlDatabase::removeDatabase(lockConnection);
    QVERIFY2(session.clickOperation(QStringLiteral("btnSave"), &response), qPrintable(session.status()->text()));
    QVERIFY(session.visibleStatus()->isVisible());
    QVERIFY(!session.visibleStatus()->styleSheet().contains(QStringLiteral("E83323"), Qt::CaseInsensitive));
    QVERIFY(session.visibleStatus()->toolTip().contains(session.status()->text()));
    QVERIFY(!session.page->hasUnsavedChanges());
    QVERIFY(session.clickNavigation(kMainButtons[2], 2, 1, &response));
    QVERIFY(session.clickNavigation(kMainButtons[6], 6, 1, &response));
    QCOMPARE(session.hospital()->text(), kFinalHospital);
    QCOMPARE(session.cell(0, 2)->text(), kFinalNote);
    checked(QStringLiteral("real_locked_save_rejects_navigation_and_close_then_recovers_without_partial_commit"));
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("hospital_final.png"))));

    QJsonObject actual;
    QVERIFY2(snapshot(databasePath_, &actual, &error), qPrintable(error));
    QVERIFY2(matchesExpected(actual), qPrintable(QString::fromUtf8(QJsonDocument(actual).toJson())));
    QSettings ini(QDir(directory_).filePath(QStringLiteral("config.ini")), QSettings::IniFormat);
    ini.setIniCodec("UTF-8");
    QCOMPARE(ini.value(QStringLiteral("hospital/name")).toString(), kFinalHospital);
    QVERIFY(saveCsv(QDir(directory_).filePath(QStringLiteral("actual_data.csv")), actual));

    // Closing the whole shell uses the same dirty guard and never writes a
    // draft merely because the user accepted exit.
    QVERIFY(paste(session.hospital(), QStringLiteral("退出未保存医院")));
    auto retainExit = answerNextDialog(QMessageBox::No);
    QVERIFY(!session.window->close());
    QVERIFY(retainExit->seen);
    QVERIFY(session.window->isVisible());
    QVERIFY(session.page->hasUnsavedChanges());
    QCOMPARE(session.hospital()->text(), QStringLiteral("退出未保存医院"));
    auto discardExit = answerNextDialog(QMessageBox::Yes);
    session.window->close();
    QVERIFY(discardExit->seen);
    QVERIFY(waitUntil([&session]() { return !session.window->isVisible(); }));
    QVERIFY(!session.page->hasUnsavedChanges());
    QJsonObject afterExit;
    QVERIFY2(snapshot(databasePath_, &afterExit, &error), qPrintable(error));
    QCOMPARE(afterExit, actual);
    checked(QStringLiteral("dirty_shell_exit_cancel_keeps_draft_confirm_exits_without_persisting_draft"));

    const QJsonObject metrics = response.result();
    QVERIFY(metrics.value(QStringLiteral("heartbeat")).toObject().value(QStringLiteral("samples")).toInt() >= 5);
    const QJsonObject evidence = {{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("window_size"), QStringLiteral("%1x%2").arg(session.window->width()).arg(session.window->height())},
        {QStringLiteral("mode"), QStringLiteral("O03-1 offline shell with O01 only")},
        {QStringLiteral("expected_hospital_name"), kFinalHospital},
        {QStringLiteral("expected_rows"), expectedRows()}, {QStringLiteral("actual"), actual},
        {QStringLiteral("assertions"), assertions}, {QStringLiteral("response"), metrics},
        {QStringLiteral("locked_save"), lockMetrics}, {QStringLiteral("repeat_navigation_transitions"), 100},
        {QStringLiteral("discard_dialog_text"), retainMain->text}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("write_result.json")), evidence));
    qInfo().noquote() << "SHELL_WRITE_ACTUAL" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "SHELL_RESPONSE" << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
}

void ShellTest::readExperiment()
{
    QVERIFY2(QFileInfo::exists(databasePath_), "Run writeExperiment in a separate process first");
    Session session(directory_);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QCOMPARE(session.window->currentMainPage(), 6);
    QCOMPARE(session.window->currentUserPage(), 1);
    QCOMPARE(session.hospital()->text(), kFinalHospital);
    for (int index = 0; index < kTypes.size(); ++index)
    {
        QVERIFY2(session.category(index), qPrintable(session.status()->text()));
        QCOMPARE(session.table()->rowCount(), 1);
        QCOMPARE(session.cell(0, 0)->text(), kNames[index]);
        QCOMPARE(session.cell(0, 1)->text(), kMnemonics[index]);
        QCOMPARE(session.cell(0, 2)->text(), index == 0 ? kFinalNote : kDetails[index]);
    }
    QVERIFY(session.category(0));
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("reopened.png"))));
    QJsonObject actual;
    QString error;
    QVERIFY2(snapshot(databasePath_, &actual, &error), qPrintable(error));
    QVERIFY(matchesExpected(actual));
    QSettings ini(QDir(directory_).filePath(QStringLiteral("config.ini")), QSettings::IniFormat);
    ini.setIniCodec("UTF-8");
    QCOMPARE(ini.value(QStringLiteral("hospital/name")).toString(), kFinalHospital);
    QVERIFY(!session.page->hasUnsavedChanges());
    QVERIFY(session.window->close());
    const QJsonObject evidence = {{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("expected_rows"), expectedRows()}, {QStringLiteral("actual"), actual},
        {QStringLiteral("new_process_readback"), true}, {QStringLiteral("clean_shell_close_accepted"), true}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("read_result.json")), evidence));
    qInfo().noquote() << "SHELL_REOPENED_ACTUAL" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
}

void ShellTest::applicationExitExperiment()
{
#ifndef Q_OS_LINUX
    QSKIP("The formal native-window close and /proc resource evidence tool requires Linux/X11");
#else
    const QString application = QString::fromUtf8(qgetenv("SN100_SHELL_APP"));
    QVERIFY2(QFileInfo(application).isAbsolute() && QFileInfo(application).isExecutable(),
             "SN100_SHELL_APP must name the freshly built formal shell executable");
    const QString xdotool = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    QVERIFY2(!xdotool.isEmpty(), "xdotool is required to discover the formal shell window by its PID");
    const QString runtime = QDir(directory_).filePath(QStringLiteral("formal_app_runtime"));
    QVERIFY2(!QFileInfo::exists(runtime), "Refusing to reuse formal-app smoke runtime data");
    QVERIFY(QDir().mkpath(runtime));
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(application, {QStringLiteral("--offline"), QStringLiteral("--data-dir"), runtime,
                               QStringLiteral("--viewport"), QStringLiteral("1600x900")});
    QVERIFY(process.waitForStarted(10000));
    const qint64 pid = process.processId();
    QByteArray output;
    QVERIFY(waitUntil([&process, &output]()
    {
        output += process.readAll();
        return output.contains("START") || process.state() == QProcess::NotRunning;
    }));
    QVERIFY2(process.state() != QProcess::NotRunning, output.constData());

    // Search only this process. The helper never activates or closes another
    // parallel window and sends the native close message to this window only.
    QByteArray windowId;
    QVERIFY(waitUntil([&xdotool, pid, &windowId]()
    {
        QProcess search;
        search.start(xdotool, {QStringLiteral("search"), QStringLiteral("--onlyvisible"),
                              QStringLiteral("--pid"), QString::number(pid)});
        if (!search.waitForFinished(1000))
        {
            return false;
        }
        const QList<QByteArray> identifiers = search.readAllStandardOutput().trimmed().split('\n');
        if (search.exitCode() == 0 && !identifiers.isEmpty() && !identifiers.first().isEmpty())
        {
            windowId = identifiers.first();
            return true;
        }
        return false;
    }));
    QVERIFY(!windowId.isEmpty());
    // Initialization is asynchronous. Wait for its explicit ready line before
    // requesting close, otherwise the correct busy guard would reject exit.
    QVERIFY(waitUntil([&process, &output]()
    {
        output += process.readAll();
        return output.contains("SHELL_READY") || process.state() == QProcess::NotRunning;
    }));
    QVERIFY2(process.state() != QProcess::NotRunning, output.constData());

    // Measure the real, ready product process while it is idle. These samples
    // exclude the QtTest writer/PNG overhead and are never the parent's /proc.
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    QVERIFY(ticksPerSecond > 0);
    QJsonArray resourceSamples;
    QElapsedTimer resourceClock;
    resourceClock.start();
    for (int index = 0; index <= 10; ++index)
    {
        QVERIFY2(process.state() != QProcess::NotRunning, "Formal shell exited during ready idle sampling");
        resourceSamples.append(sampleFormalProcess(pid, double(resourceClock.nsecsElapsed()) / 1000000.0));
        if (index < 10)
        {
            QTest::qWait(500);
        }
    }
    const QJsonObject firstSample = resourceSamples.first().toObject();
    const QJsonObject lastSample = resourceSamples.last().toObject();
    QVERIFY(!firstSample.value(QStringLiteral("cpu_ticks")).isNull());
    QVERIFY(!lastSample.value(QStringLiteral("cpu_ticks")).isNull());
    const double sampledSeconds = (lastSample.value(QStringLiteral("elapsed_ms")).toDouble()
            - firstSample.value(QStringLiteral("elapsed_ms")).toDouble()) / 1000.0;
    QVERIFY(sampledSeconds >= 5.0);
    const double cpuSeconds = (lastSample.value(QStringLiteral("cpu_ticks")).toDouble()
            - firstSample.value(QStringLiteral("cpu_ticks")).toDouble()) / double(ticksPerSecond);
    const QJsonObject resources = {{QStringLiteral("application_process_id"), double(pid)},
        {QStringLiteral("condition"), QStringLiteral("Formal shell ready, 1600x900 viewport, idle; separate from QtTest business/PNG workload")},
        {QStringLiteral("sampling_interval_ms"), 500},
        {QStringLiteral("sampled_seconds"), sampledSeconds},
        {QStringLiteral("clock_ticks_per_second"), double(ticksPerSecond)},
        {QStringLiteral("cpu_seconds"), cpuSeconds},
        {QStringLiteral("cpu_percent_one_core"), cpuSeconds / sampledSeconds * 100.0},
        {QStringLiteral("samples"), resourceSamples}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("formal_app_resources.json")), resources));

    bool validWindowId = false;
    const qulonglong nativeWindowId = windowId.toULongLong(&validWindowId);
    QVERIFY(validWindowId && nativeWindowId > 0);
    QScreen *screen = QGuiApplication::primaryScreen();
    const QPixmap viewportImage = screen
        ? screen->grabWindow(static_cast<WId>(nativeWindowId)) : QPixmap();
    const bool viewportImageSaved = !viewportImage.isNull()
        && viewportImage.save(QDir(directory_).filePath(QStringLiteral("formal_app_viewport.png")));
    int pixelSamples = 0;
    int blackOrTransparentSamples = 0;
    if (!viewportImage.isNull())
    {
        const QImage pixels = viewportImage.toImage().convertToFormat(QImage::Format_ARGB32);
        const int xStep = qMax(1, pixels.width() / 64);
        const int yStep = qMax(1, pixels.height() / 64);
        for (int y = 0; y < pixels.height(); y += yStep)
        {
            for (int x = 0; x < pixels.width(); x += xStep)
            {
                const QRgb pixel = pixels.pixel(x, y);
                ++pixelSamples;
                if (qAlpha(pixel) < 16 || (qRed(pixel) <= 8 && qGreen(pixel) <= 8 && qBlue(pixel) <= 8))
                {
                    ++blackOrTransparentSamples;
                }
            }
        }
    }
    const double blackOrTransparentRatio = pixelSamples > 0
        ? double(blackOrTransparentSamples) / double(pixelSamples) : 1.0;
    const bool nativeCaptureUsable = viewportImageSaved && pixelSamples > 0 && blackOrTransparentRatio < 0.99;
    // WSLg may report a valid cross-process window size but return black
    // pixels. Preserve that limitation as metadata; it is independent of the
    // correctly exercised product closeEvent and must not fail its test.
    const QString captureReason = !viewportImageSaved
        ? QStringLiteral("Native cross-process capture unavailable; this slot proves ready/requested viewport/exit only, not viewport visual acceptance")
        : nativeCaptureUsable
            ? QStringLiteral("Native capture contains non-black content; independent visual review is still required")
            : QStringLiteral("WSLg native cross-process capture is nearly all black/transparent and is unusable as visual evidence; do not infer viewport visual acceptance");
    QString closeError;
    QVERIFY2(sendNativeWindowClose(nativeWindowId, &closeError), qPrintable(closeError));
    QVERIFY2(waitUntil([&process]() { return process.state() == QProcess::NotRunning; }),
             "Formal shell did not exit after PID-targeted native WM_DELETE_WINDOW");
    output += process.readAll();
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY2(output.contains("STORE_CLOSED"), output.constData());
    QVERIFY2(output.contains("THREAD_STOPPED true"), output.constData());
    QVERIFY2(QRegularExpression(QStringLiteral("EXIT code=\\s*0(?:\\s|$)"))
             .match(QString::fromUtf8(output)).hasMatch(), output.constData());
    const QJsonObject evidence = {{QStringLiteral("test_process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("application_process_id"), double(pid)},
        {QStringLiteral("application"), application}, {QStringLiteral("data_directory"), runtime},
        {QStringLiteral("requested_viewport"), QStringLiteral("1600x900")},
        {QStringLiteral("target_window_id"), QString::fromLatin1(windowId)},
        {QStringLiteral("close_injection"), QStringLiteral("X11 WM_PROTOCOLS/WM_DELETE_WINDOW ClientMessage via libX11.so.6, exact PID-matched window ID")},
        {QStringLiteral("captured_viewport_width"), viewportImage.width()},
        {QStringLiteral("captured_viewport_height"), viewportImage.height()},
        {QStringLiteral("native_viewport_capture_saved"), viewportImageSaved},
        {QStringLiteral("native_viewport_capture_usable"), nativeCaptureUsable},
        {QStringLiteral("native_viewport_capture_reason"), captureReason},
        {QStringLiteral("native_viewport_pixel_samples"), pixelSamples},
        {QStringLiteral("native_viewport_black_or_transparent_ratio"), blackOrTransparentRatio},
        {QStringLiteral("native_viewport_visual_acceptance"), false},
        {QStringLiteral("exit_code"), process.exitCode()}, {QStringLiteral("normal_exit"), true},
        {QStringLiteral("actual_output"), QString::fromUtf8(output)}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("formal_app_exit.json")), evidence));
    qInfo().noquote() << "FORMAL_SHELL_OUTPUT" << QString::fromUtf8(output);
#endif
}

QTEST_MAIN(ShellTest)
#include "shell_test.moc"
