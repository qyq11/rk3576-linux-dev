#include <QtTest>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <memory>

#include "data/hospital_store.h"
#include "ui/frmhospinfosetting.h"

namespace
{
const QString kInitialHospital = QStringLiteral("WSL脱机验证医院");
const QString kFinalHospital = QStringLiteral("WSL脱机复测医院");
const QStringList kTypes = {QStringLiteral("keshi"), QStringLiteral("bingren"),
    QStringLiteral("songjian"), QStringLiteral("jianyan"), QStringLiteral("shenhe")};
const QStringList kButtons = {QStringLiteral("btnKeShiInfo"), QStringLiteral("btnBingRenInfo"),
    QStringLiteral("btnSongJianInfo"), QStringLiteral("btnJianYanInfo"), QStringLiteral("btnShenHeInfo")};
const QStringList kNames = {QStringLiteral("血液科"), QStringLiteral("门诊"),
    QStringLiteral("送检员"), QStringLiteral("检验员"), QStringLiteral("审核员")};
const QStringList kMnemonics = {QStringLiteral("XYK"), QStringLiteral("MZ"),
    QStringLiteral("SJY"), QStringLiteral("JYY"), QStringLiteral("SHY")};
const QStringList kThirdColumns = {QStringLiteral("初始 O'Brien 中文"), QStringLiteral("门诊 O'Brien"),
    QStringLiteral("SJ001"), QStringLiteral("JY001"), QStringLiteral("SH001")};
const QString kFinalNote = QStringLiteral("锁库重试 O'Brien 中文");

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

struct Answer
{
    bool seen = false;
    QString objectName;
};

std::shared_ptr<Answer> answerNextDialog(QMessageBox::StandardButton answer)
{
    auto state = std::make_shared<Answer>();
    QTimer *timer = new QTimer(qApp);
    timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, state, answer]()
    {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (dialog && dialog->button(answer))
        {
            state->seen = true;
            state->objectName = dialog->objectName();
            timer->stop();
            QTest::mouseClick(dialog->button(answer), Qt::LeftButton);
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

    QJsonObject result() const
    {
        QList<double> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        const double maximum = sorted.isEmpty() ? 0 : sorted.last();
        const double p95 = sorted.isEmpty() ? 0 : sorted.at(qMin(sorted.size() - 1, int(sorted.size() * 0.95)));
        return {{QStringLiteral("timer_interval_ms"), 10},
                {QStringLiteral("timer_samples"), gaps.size()},
                {QStringLiteral("timer_gap_max_ms"), maximum},
                {QStringLiteral("timer_gap_p95_ms"), p95},
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
    explicit Session(const QString &directory) : directory(directory)
    {
        HospitalStore::registerTypes();
        store = new HospitalStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        window = new frmHospInfoSetting(store);
        window->setDataDirectory(directory);
        window->resize(1200, 760);
        operations = new QSignalSpy(window, &frmHospInfoSetting::operationFinished);
        QObject::connect(&thread, &QThread::started, store, [this]()
        {
            store->initialize(QDir(this->directory).filePath(QStringLiteral("hospital_Information.db")),
                              QDir(this->directory).filePath(QStringLiteral("config.ini")));
        });
        window->show();
        thread.start();
    }

    ~Session()
    {
        // Failed assertions must also release the worker and SQLite connection.
        // Deletion is the fallback only; passing tests explicitly close the UI.
        delete operations;
        delete window;
        if (thread.isRunning())
        {
            QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
        }
    }

    QPushButton *button(const QString &name) const
    {
        return window->findChild<QPushButton *>(name);
    }

    QTableWidget *table() const
    {
        return window->findChild<QTableWidget *>(QStringLiteral("tableInfo"));
    }

    QLineEdit *cell(int row, int column) const
    {
        return qobject_cast<QLineEdit *>(table()->cellWidget(row, column));
    }

    QLineEdit *hospital() const
    {
        return window->findChild<QLineEdit *>(QStringLiteral("ldtHospName"));
    }

    QLabel *status() const
    {
        return window->findChild<QLabel *>(QStringLiteral("statusLabel"));
    }

    bool ready()
    {
        return waitUntil([this]() { return window->isReady() && !window->isBusy(); });
    }

    bool clickAndWait(const QString &name, ResponseProbe *probe = nullptr)
    {
        operations->clear();
        QElapsedTimer elapsed;
        elapsed.start();
        auto *control = button(name);
        if (!control || !control->isEnabled())
        {
            return false;
        }
        QTest::mouseClick(control, Qt::LeftButton);
        const bool completed = waitUntil([this]() { return !operations->isEmpty() && !window->isBusy(); });
        if (probe)
        {
            probe->operation(name, double(elapsed.nsecsElapsed()) / 1000000.0);
        }
        return completed && operations->last().at(1).toBool();
    }

    bool category(int index, ResponseProbe *probe = nullptr)
    {
        if (button(kButtons.at(index))->isChecked())
        {
            return !window->isBusy();
        }
        return clickAndWait(kButtons.at(index), probe);
    }

    void selectRow(int row)
    {
        const QModelIndex index = table()->model()->index(row, 0);
        table()->scrollTo(index);
        QCoreApplication::processEvents();
        QTest::mouseClick(cell(row, 0), Qt::LeftButton);
    }

    QString directory;
    QThread thread;
    HospitalStore *store = nullptr;
    frmHospInfoSetting *window = nullptr;
    QSignalSpy *operations = nullptr;
};

bool paste(QLineEdit *edit, const QString &text)
{
    if (!edit || !edit->isEnabled() || edit->isReadOnly())
    {
        return false;
    }
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
    return waitUntil([edit, text]() { return edit->text() == text; }, 1000);
}

bool snapshot(const QString &databasePath, QJsonObject *result, QString *error)
{
    const QString connection = QStringLiteral("evidence-") + QUuid::createUuid().toString();
    bool success = false;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(databasePath);
        if (!database.open())
        {
            *error = database.lastError().text();
        }
        else
        {
            QSqlQuery query(database);
            success = query.exec(QStringLiteral("SELECT value FROM app_settings WHERE key='hospital/name'")) && query.next();
            if (success)
            {
                result->insert(QStringLiteral("hospital_name"), query.value(0).toString());
                query.finish();
                success = query.exec(QStringLiteral("SELECT id,column_one,column_two,column_three,type FROM hospital_information ORDER BY type,id"));
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
            {QStringLiteral("column_one"), index == 0 ? QStringLiteral("血液科2") : kNames[index]},
            {QStringLiteral("column_two"), index == 0 ? QStringLiteral("XYK2") : kMnemonics[index]},
            {QStringLiteral("column_three"), index == 0 ? kFinalNote : kThirdColumns[index]}});
    }
    return rows;
}

bool matchesExpected(const QJsonObject &actual)
{
    if (actual.value(QStringLiteral("hospital_name")).toString() != kFinalHospital)
    {
        return false;
    }
    const QJsonArray rows = actual.value(QStringLiteral("rows")).toArray();
    if (rows.size() != 5)
    {
        return false;
    }
    for (const QJsonValue &expected : expectedRows())
    {
        bool found = false;
        for (const QJsonValue &value : rows)
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

bool saveJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            && file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) > 0;
}
}

class OfflineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void writeExperiment();
    void readExperiment();

private:
    QString directory_;
    QString databasePath_;
};

void OfflineTest::initTestCase()
{
    directory_ = QString::fromUtf8(qgetenv("SN100_EVIDENCE_DIR"));
    QVERIFY2(!directory_.isEmpty() && QDir::isAbsolutePath(directory_),
             "SN100_EVIDENCE_DIR must name a new absolute experiment directory");
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"),
             "This integration test requires a visible WSLg XCB GUI, not an offscreen backend");
    QVERIFY(QDir().mkpath(directory_));
    databasePath_ = QDir(directory_).filePath(QStringLiteral("hospital_Information.db"));
    qInfo().noquote() << "EVIDENCE_DIR" << directory_ << "PID" << QCoreApplication::applicationPid();
}

void OfflineTest::writeExperiment()
{
    QVERIFY2(!QFileInfo::exists(databasePath_), "Refusing to overwrite a previous experiment database");
    QVERIFY2(!QFileInfo::exists(QDir(directory_).filePath(QStringLiteral("config.ini"))),
             "Refusing to overwrite a previous experiment INI");
    Session session(directory_);
    QVERIFY2(session.ready(), qPrintable(session.status()->text()));
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    ResponseProbe response;
    QCOMPARE(session.table()->rowCount(), 0);
    QVERIFY(paste(session.hospital(), kInitialHospital));

    for (int index = 0; index < kTypes.size(); ++index)
    {
        QVERIFY2(session.category(index, &response), qPrintable(session.status()->text()));
        QCOMPARE(session.table()->rowCount(), 0);
        QTest::mouseClick(session.button(QStringLiteral("btnAdd")), Qt::LeftButton);
        QCOMPARE(session.table()->rowCount(), 1);
        QVERIFY(paste(session.cell(0, 0), kNames[index]));
        QVERIFY(paste(session.cell(0, 2), kThirdColumns[index]));
        QCOMPARE(session.cell(0, 1)->text(), kMnemonics[index]);
        QVERIFY(session.cell(0, 1)->isReadOnly());
        QVERIFY2(session.clickAndWait(QStringLiteral("btnSave"), &response), qPrintable(session.status()->text()));
        QCOMPARE(session.table()->rowCount(), 1);
        QCOMPARE(session.cell(0, 0)->text(), kNames[index]);
        QCOMPARE(session.cell(0, 2)->text(), kThirdColumns[index]);
    }

    // Validation must reject an incomplete row without opening a modal dialog.
    QTest::mouseClick(session.button(QStringLiteral("btnAdd")), Qt::LeftButton);
    QTest::mouseClick(session.button(QStringLiteral("btnSave")), Qt::LeftButton);
    QVERIFY(session.status()->property("isError").toBool());
    QVERIFY(session.status()->text().contains(QStringLiteral("不能为空")));
    QVERIFY(!session.window->isBusy());
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("error.png"))));
    auto cancelledInvalid = answerNextDialog(QMessageBox::Yes);
    QVERIFY(session.clickAndWait(QStringLiteral("btnCancel"), &response));
    QVERIFY(cancelledInvalid->seen);
    QCOMPARE(session.table()->rowCount(), 1);

    QVERIFY(session.category(0, &response));
    QVERIFY(paste(session.cell(0, 0), QStringLiteral("血液科2")));
    QVERIFY(paste(session.cell(0, 2), QStringLiteral("复测 O'Brien 中文")));
    QVERIFY(session.clickAndWait(QStringLiteral("btnSave"), &response));
    QCOMPARE(session.cell(0, 1)->text(), QStringLiteral("XYK2"));

    QVERIFY(paste(session.cell(0, 0), QStringLiteral("取消测试")));
    auto keep = answerNextDialog(QMessageBox::No);
    QTest::mouseClick(session.button(QStringLiteral("btnCancel")), Qt::LeftButton);
    QVERIFY(keep->seen);
    QVERIFY(session.window->hasUnsavedChanges());
    QCOMPARE(session.cell(0, 0)->text(), QStringLiteral("取消测试"));
    auto discard = answerNextDialog(QMessageBox::Yes);
    QVERIFY(session.clickAndWait(QStringLiteral("btnCancel"), &response));
    QVERIFY(discard->seen);
    QCOMPARE(session.cell(0, 0)->text(), QStringLiteral("血液科2"));

    // Category changes must also respect the user's choice about dirty edits.
    QVERIFY(paste(session.cell(0, 0), QStringLiteral("切换测试")));
    auto keepCategory = answerNextDialog(QMessageBox::No);
    QTest::mouseClick(session.button(kButtons[1]), Qt::LeftButton);
    QVERIFY(keepCategory->seen);
    QVERIFY(session.button(kButtons[0])->isChecked());
    QCOMPARE(session.cell(0, 0)->text(), QStringLiteral("切换测试"));
    auto discardCategory = answerNextDialog(QMessageBox::Yes);
    QVERIFY(session.category(1, &response));
    QVERIFY(discardCategory->seen);
    QCOMPARE(session.cell(0, 0)->text(), kNames[1]);
    QVERIFY(session.category(0, &response));
    QCOMPARE(session.cell(0, 0)->text(), QStringLiteral("血液科2"));

    // Persist an extra row, decline deletion, then confirm deletion.
    QTest::mouseClick(session.button(QStringLiteral("btnAdd")), Qt::LeftButton);
    QVERIFY(paste(session.cell(1, 0), QStringLiteral("临时科")));
    QVERIFY(paste(session.cell(1, 2), QStringLiteral("应被确认删除")));
    QVERIFY(session.clickAndWait(QStringLiteral("btnSave"), &response));
    session.selectRow(1);
    QCOMPARE(session.table()->currentRow(), 1);
    auto noDelete = answerNextDialog(QMessageBox::No);
    QTest::mouseClick(session.button(QStringLiteral("btnDel")), Qt::LeftButton);
    QVERIFY(noDelete->seen);
    QCOMPARE(session.table()->rowCount(), 2);
    auto yesDelete = answerNextDialog(QMessageBox::Yes);
    QVERIFY(session.clickAndWait(QStringLiteral("btnDel"), &response));
    QVERIFY(yesDelete->seen);
    QCOMPARE(session.table()->rowCount(), 1);
    QCOMPARE(session.cell(0, 0)->text(), QStringLiteral("血液科2"));

    // Hold an independent writer lock. Storage must fail in the worker while
    // the real GUI's 10 ms timer and window-close guard remain responsive.
    QJsonObject beforeLock;
    QString error;
    QVERIFY2(snapshot(databasePath_, &beforeLock, &error), qPrintable(error));
    const QString lockConnection = QStringLiteral("external-lock");
    QJsonObject lockMetrics;
    {
        QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnection);
        locker.setDatabaseName(databasePath_);
        QVERIFY(locker.open());
        QSqlQuery lockQuery(locker);
        QVERIFY(lockQuery.exec(QStringLiteral("BEGIN IMMEDIATE")));
        QVERIFY(paste(session.hospital(), kFinalHospital));
        QVERIFY(paste(session.cell(0, 2), kFinalNote));
        session.operations->clear();
        const int timerStart = response.gaps.size();
        QElapsedTimer lockElapsed;
        lockElapsed.start();
        QTest::mouseClick(session.button(QStringLiteral("btnSave")), Qt::LeftButton);
        QVERIFY(session.window->isBusy());
        QVERIFY(!session.button(QStringLiteral("btnSave"))->isEnabled());
        QVERIFY(!session.window->close());
        QVERIFY(session.window->isVisible());
        QVERIFY(waitUntil([&session]() { return !session.operations->isEmpty() && !session.window->isBusy(); }));
        QVERIFY(!session.operations->last().at(1).toBool());
        const double lockedMilliseconds = double(lockElapsed.nsecsElapsed()) / 1000000.0;
        response.operation(QStringLiteral("locked_save_failed"), lockedMilliseconds);
        const int lockSamples = response.gaps.size() - timerStart;
        qInfo() << "LOCK_OBSERVED elapsed_ms=" << lockedMilliseconds
                << "timer_samples=" << lockSamples << "status=" << session.status()->text();
        QVERIFY2(lockSamples >= 5, "GUI timer did not continue during the database lock wait");
        double lockMaxGap = 0;
        for (int index = timerStart; index < response.gaps.size(); ++index)
        {
            lockMaxGap = qMax(lockMaxGap, response.gaps[index]);
        }
        QVERIFY2(lockMaxGap < 100.0, qPrintable(QStringLiteral("GUI timer gap while locked: %1 ms").arg(lockMaxGap)));
        QVERIFY(lockedMilliseconds >= 150.0 && lockedMilliseconds < 2000.0);
        QVERIFY(session.status()->property("isError").toBool());
        QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("locked_error.png"))));
        QJsonObject failedSnapshot;
        QVERIFY2(snapshot(databasePath_, &failedSnapshot, &error), qPrintable(error));
        QCOMPARE(failedSnapshot, beforeLock);
        QVERIFY(lockQuery.exec(QStringLiteral("ROLLBACK")));
        locker.close();
        lockMetrics = {{QStringLiteral("save_failed_elapsed_ms"), lockedMilliseconds},
            {QStringLiteral("gui_timer_samples"), lockSamples},
            {QStringLiteral("gui_timer_gap_max_ms"), lockMaxGap},
            {QStringLiteral("close_rejected_while_busy"), true},
            {QStringLiteral("partial_commit_detected"), false}};
        qInfo() << "LOCK_TEST elapsed_ms=" << lockedMilliseconds
                << "timer_samples=" << lockSamples << "max_gap_ms=" << lockMaxGap;
    }
    QSqlDatabase::removeDatabase(lockConnection);
    QVERIFY2(session.clickAndWait(QStringLiteral("btnSave"), &response), qPrintable(session.status()->text()));
    QCOMPARE(session.hospital()->text(), kFinalHospital);
    QCOMPARE(session.cell(0, 2)->text(), kFinalNote);

    // 100 category changes plus 100 FULL-synchronous saves. No artificial
    // qWait duration is added to the measured operation completion times.
    for (int iteration = 0; iteration < 100; ++iteration)
    {
        const int index = (iteration + 1) % 5;
        QVERIFY2(session.category(index, &response), qPrintable(session.status()->text()));
        QCOMPARE(session.table()->rowCount(), 1);
        QCOMPARE(session.cell(0, 0)->text(), index == 0 ? QStringLiteral("血液科2") : kNames[index]);
        QVERIFY2(session.clickAndWait(QStringLiteral("btnSave"), &response), qPrintable(session.status()->text()));
    }
    QVERIFY(session.category(0, &response));
    QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("saved.png"))));

    QJsonObject actual;
    QVERIFY2(snapshot(databasePath_, &actual, &error), qPrintable(error));
    QVERIFY2(matchesExpected(actual), qPrintable(QString::fromUtf8(QJsonDocument(actual).toJson())));
    QSettings ini(QDir(directory_).filePath(QStringLiteral("config.ini")), QSettings::IniFormat);
    ini.setIniCodec("UTF-8");
    QCOMPARE(ini.value(QStringLiteral("hospital/name")).toString(), kFinalHospital);
    const QJsonObject metrics = response.result();
    QVERIFY(metrics.value(QStringLiteral("timer_samples")).toInt() >= 5);
    QVERIFY2(metrics.value(QStringLiteral("timer_gap_max_ms")).toDouble() < 150.0,
             qPrintable(QString::fromUtf8(QJsonDocument(metrics).toJson())));
    const QJsonObject evidence = {{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("mode"), QStringLiteral("WSLg offline hospital module")},
        {QStringLiteral("expected_hospital_name"), kFinalHospital},
        {QStringLiteral("expected_rows"), expectedRows()},
        {QStringLiteral("actual"), actual}, {QStringLiteral("response"), metrics},
        {QStringLiteral("locked_save"), lockMetrics},
        {QStringLiteral("lock_failed_without_partial_commit"), true},
        {QStringLiteral("repeat_operations"), 200}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("write_result.json")), evidence));
    QVERIFY(!session.window->hasUnsavedChanges());
    QVERIFY(session.window->close());
    qInfo().noquote() << "WRITE_ACTUAL" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "RESPONSE" << QJsonDocument(metrics).toJson(QJsonDocument::Compact);
}

void OfflineTest::readExperiment()
{
    QVERIFY2(QFileInfo::exists(databasePath_), "Run writeExperiment in a separate process first");
    Session session(directory_);
    QVERIFY2(session.ready(), qPrintable(session.status()->text()));
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QCOMPARE(session.hospital()->text(), kFinalHospital);
    for (int index = 0; index < kTypes.size(); ++index)
    {
        QVERIFY2(session.category(index), qPrintable(session.status()->text()));
        QCOMPARE(session.table()->rowCount(), 1);
        QCOMPARE(session.cell(0, 0)->text(), index == 0 ? QStringLiteral("血液科2") : kNames[index]);
        QCOMPARE(session.cell(0, 1)->text(), index == 0 ? QStringLiteral("XYK2") : kMnemonics[index]);
        QCOMPARE(session.cell(0, 2)->text(), index == 0 ? kFinalNote : kThirdColumns[index]);
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
    const QJsonObject evidence = {{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("expected_rows"), expectedRows()},
        {QStringLiteral("actual"), actual}, {QStringLiteral("new_session_readback"), true}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("read_result.json")), evidence));
    QVERIFY(session.window->close());
    qInfo().noquote() << "REOPENED_ACTUAL" << QJsonDocument(actual).toJson(QJsonDocument::Compact);
}

QTEST_MAIN(OfflineTest)
#include "offline_test.moc"
