#include <QtTest>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTableView>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <functional>
#include <memory>

#include "history/history_store.h"
#include "history/history_page.h"

namespace
{
const QString kSavedName = QStringLiteral("NO'Brien中");
const QString kSavedNote = QStringLiteral("中文备注,\"引号\" O'Brien");

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

QJsonObject readJson(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

void reveal(QWidget *widget)
{
    if (!widget)
    {
        return;
    }
    for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
    {
        if (auto *scroll = qobject_cast<QScrollArea *>(parent))
        {
            scroll->ensureWidgetVisible(widget, 10, 10);
        }
        if (parent->isWindow())
        {
            break;
        }
    }
    QCoreApplication::processEvents();
}

bool paste(QLineEdit *edit, const QString &text)
{
    if (edit && edit->text() == text)
    {
        return true;
    }
    if (!edit || !edit->isEnabled() || edit->isReadOnly())
    {
        return false;
    }
    reveal(edit);
    QApplication::clipboard()->setText(text);
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
    return waitUntil([edit, text]() { return edit->text() == text; }, 1000);
}

template <typename T>
T *control(QObject *parent, const char *name)
{
    auto *widget = parent->findChild<T *>(QString::fromLatin1(name));
    reveal(widget);
    return widget;
}

bool chooseDate(QDialog *queryDialog, const char *editName, const char *buttonName,
                const QString &text, bool forceCalendar)
{
    auto *edit = control<QLineEdit>(queryDialog, editName);
    if (!edit)
    {
        return false;
    }
    if (!forceCalendar && edit->text() == text)
    {
        return true;
    }
    const QDate date = QDate::fromString(text, QStringLiteral("yyyy/MM/dd"));
    auto *button = control<QPushButton>(queryDialog, buttonName);
    if (!date.isValid() || !button)
    {
        return false;
    }
    QTest::mouseClick(button, Qt::LeftButton);
    QCalendarWidget *calendar = nullptr;
    if (!waitUntil([queryDialog, &calendar]()
    {
        for (auto *candidate : queryDialog->findChildren<QCalendarWidget *>())
        {
            if (candidate->isVisible())
            {
                calendar = candidate;
                return true;
            }
        }
        return false;
    }))
    {
        return false;
    }
    // Prepare the month view, without assigning the selected date or changing
    // the read-only edit. The real date-grid mouse click must emit clicked()
    // and drive the same page callback that an operator uses.
    calendar->setCurrentPage(date.year(), date.month());
    QCoreApplication::processEvents();
    auto *grid = calendar->findChild<QTableView *>(QStringLiteral("qt_calendar_calendarview"));
    if (!grid)
    {
        return false;
    }
    QList<QModelIndex> candidates;
    for (int row = 1; row < grid->model()->rowCount(); ++row)
    {
        for (int column = 0; column < grid->model()->columnCount(); ++column)
        {
            const QModelIndex index = grid->model()->index(row, column);
            if (index.data().toString() == QString::number(date.day())
                    && (index.flags() & Qt::ItemIsSelectable))
            {
                candidates.append(index);
            }
        }
    }
    // The fixed fixture day is the 23rd, which has one cell in October's grid.
    // Refuse an ambiguous grid instead of accidentally clicking a neighbour
    // month's same-number date.
    if (candidates.size() != 1)
    {
        return false;
    }
    QSignalSpy clicked(calendar, &QCalendarWidget::clicked);
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier,
                      grid->visualRect(candidates.first()).center());
    return !clicked.isEmpty() && clicked.last().at(0).toDate() == date
            && waitUntil([edit, text]() { return edit->text() == text; });
}

// Read the file that the module actually produced. This CSV reader follows
// field quoting rather than splitting lines: patient remarks may contain a
// comma, a quote or an embedded newline.
bool readCsv(const QString &path, QList<QStringList> *rows, QByteArray *bytes, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = file.errorString();
        return false;
    }
    *bytes = file.readAll();
    QTextCodec *codec = QTextCodec::codecForName("GBK");
    if (!codec)
    {
        *error = QStringLiteral("GBK codec is unavailable");
        return false;
    }
    QTextCodec::ConverterState state;
    const QString csv = codec->toUnicode(bytes->constData(), bytes->size(), &state);
    if (state.invalidChars)
    {
        *error = QStringLiteral("CSV is not valid GBK");
        return false;
    }
    QStringList row;
    QString field;
    bool quoted = false;
    bool closedQuote = false;
    for (int index = 0; index < csv.size(); ++index)
    {
        const QChar current = csv.at(index);
        if (quoted)
        {
            if (current == QLatin1Char('"'))
            {
                if (index + 1 < csv.size() && csv.at(index + 1) == QLatin1Char('"'))
                {
                    field += current;
                    ++index;
                }
                else
                {
                    quoted = false;
                    closedQuote = true;
                }
            }
            else
            {
                field += current;
            }
        }
        else if (current == QLatin1Char('"') && field.isEmpty() && !closedQuote)
        {
            quoted = true;
        }
        else if (current == QLatin1Char(','))
        {
            row.append(field);
            field.clear();
            closedQuote = false;
        }
        else if (current == QLatin1Char('\r') || current == QLatin1Char('\n'))
        {
            row.append(field);
            rows->append(row);
            row.clear();
            field.clear();
            closedQuote = false;
            if (current == QLatin1Char('\r') && index + 1 < csv.size()
                    && csv.at(index + 1) == QLatin1Char('\n'))
            {
                ++index;
            }
        }
        else if (closedQuote)
        {
            *error = QStringLiteral("Unexpected character after a closing CSV quote");
            return false;
        }
        else
        {
            field += current;
        }
    }
    if (quoted)
    {
        *error = QStringLiteral("Unterminated CSV field");
        return false;
    }
    if (!field.isEmpty() || !row.isEmpty() || closedQuote)
    {
        row.append(field);
        rows->append(row);
    }
    return !rows->isEmpty();
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
        QJsonObject result{{QStringLiteral("timer_interval_ms"), 10},
            {QStringLiteral("timer_samples"), gaps.size()},
            {QStringLiteral("timer_gap_max_ms"), sorted.isEmpty() ? 0 : sorted.last()},
            {QStringLiteral("operations"), operations}};
        for (const int percentile : {50, 95, 99})
        {
            result.insert(QStringLiteral("timer_gap_p%1_ms").arg(percentile),
                sorted.isEmpty() ? 0 : sorted.at(qMin(sorted.size() - 1, int(sorted.size() * percentile / 100.0))));
        }
        return result;
    }

    QTimer timer;
    QElapsedTimer clock;
    qint64 previous = 0;
    QList<double> gaps;
    QJsonArray operations;
};

struct DialogAction
{
    bool seen = false;
    bool success = false;
};

std::shared_ptr<DialogAction> actOnDialog(const QString &name,
                                        const std::function<bool(QDialog *)> &action)
{
    auto result = std::make_shared<DialogAction>();
    auto *timer = new QTimer(qApp);
    timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, name, action, result]()
    {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            auto *dialog = qobject_cast<QDialog *>(widget);
            if (dialog && dialog->isVisible() && dialog->objectName() == name)
            {
                timer->stop();
                result->seen = true;
                result->success = action(dialog);
                timer->deleteLater();
                return;
            }
        }
    });
    QTimer::singleShot(10000, timer, &QObject::deleteLater);
    timer->start();
    return result;
}

void setChecked(QCheckBox *box, bool checked)
{
    if (box && box->isChecked() != checked)
    {
        QTest::mouseClick(box, Qt::LeftButton);
    }
}

// This is an independent read connection, created and removed in this test's
// thread. It records actual persisted fields and detects any partial writes.
bool databaseSnapshot(const QString &path, QJsonObject *snapshot, QString *error)
{
    const QString connection = QStringLiteral("history-evidence-") + QUuid::createUuid().toString();
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
            success = query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name"));
            QStringList tables;
            while (success && query.next())
            {
                tables.append(query.value(0).toString());
            }
            query.finish();
            for (const QString &table : tables)
            {
                const QString escaped = QString(table).replace(QLatin1Char('"'), QStringLiteral("\"\""));
                success = query.exec(QStringLiteral("SELECT * FROM \"%1\" ORDER BY rowid").arg(escaped));
                if (!success)
                {
                    break;
                }
                QJsonArray rows;
                const QSqlRecord record = query.record();
                while (query.next())
                {
                    QJsonObject row;
                    for (int index = 0; index < record.count(); ++index)
                    {
                        row.insert(record.fieldName(index), QJsonValue::fromVariant(query.value(index)));
                    }
                    rows.append(row);
                }
                snapshot->insert(table, rows);
                query.finish();
            }
            success = success && query.exec(QStringLiteral("PRAGMA integrity_check")) && query.next()
                    && query.value(0).toString() == QStringLiteral("ok");
            snapshot->insert(QStringLiteral("integrity_check"), success ? QStringLiteral("ok") : query.value(0).toString());
            if (!success)
            {
                *error = query.lastError().text();
            }
            query.finish();
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return success;
}

class Session
{
public:
    explicit Session(const QString &dataDirectory) : directory(dataDirectory)
    {
        HistoryStore::registerTypes();
        store = new HistoryStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        page = new HistoryPage(store);
        page->setDataDirectory(directory);
        host.setWidget(page);
        host.resize(1600, 900);
        operations = new QSignalSpy(page, &HistoryPage::operationFinished);
        errors = new QSignalSpy(store, &HistoryStore::error);
        queryResults = new QSignalSpy(store, &HistoryStore::queryReady);
        details = new QSignalSpy(store, &HistoryStore::detailReady);
        exports = new QSignalSpy(store, &HistoryStore::csvExported);
        QObject::connect(page, &HistoryPage::statusChanged, page, [this](const QString &value) { status = value; });
        QObject::connect(&thread, &QThread::started, store, [this]() { store->initialize(directory); });
        host.show();
        thread.start();
    }

    ~Session()
    {
        delete operations;
        delete errors;
        delete queryResults;
        delete details;
        delete exports;
        host.takeWidget();
        delete page;
        if (thread.isRunning())
        {
            QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
        }
    }

    bool ready()
    {
        return waitUntil([this]() { return page->isReady() && !page->isBusy(); });
    }

    QTableWidget *table() const { return control<QTableWidget>(page, "tableResultReportOne"); }
    QPushButton *button(const char *name)
    {
        auto *button = control<QPushButton>(page, name);
        if (button)
        {
            // The preserved 1751 x 968 page scrolls inside a 1600 x 900 host.
            // A mouse event sent directly to a clipped control would not prove
            // that the operator can reach it through the actual viewport.
            host.ensureWidgetVisible(button, 10, 10);
            QCoreApplication::processEvents();
        }
        return button;
    }

    bool finished(const QString &operation, bool success = true) const
    {
        if (page->isBusy())
        {
            return false;
        }
        for (const QList<QVariant> &values : *operations)
        {
            if (values.at(0).toString() == operation && values.at(1).toBool() == success)
            {
                return true;
            }
        }
        return false;
    }

    bool query(const QString &start, const QString &end, const QString &sample = {},
               const QString &barcode = {}, ResponseProbe *probe = nullptr, bool forceCalendar = false)
    {
        operations->clear();
        QElapsedTimer elapsed;
        auto action = actOnDialog(QStringLiteral("MaskDlgQueryReport"), [=, &elapsed](QDialog *dialog)
        {
            setChecked(control<QCheckBox>(dialog, "ckxByDate"), true);
            setChecked(control<QCheckBox>(dialog, "ckxBySampleCode"), false);
            const bool typed = chooseDate(dialog, "ldtStartDate", "btnCalendarStart", start, forceCalendar)
                    && chooseDate(dialog, "ldtEndDate", "btnCalendarEnd", end, forceCalendar)
                    && paste(control<QLineEdit>(dialog, "ldtSampleID"), sample)
                    && paste(control<QLineEdit>(dialog, "ldtSampleCode"), barcode);
            setChecked(control<QCheckBox>(dialog, "ckxBySampleCode"), !barcode.isEmpty());
            elapsed.start();
            QTest::mouseClick(control<QPushButton>(dialog, "btnSave"), Qt::LeftButton);
            return typed;
        });
        elapsed.start();
        if (!button("btnQuery") || !button("btnQuery")->isEnabled())
        {
            return false;
        }
        QTest::mouseClick(button("btnQuery"), Qt::LeftButton);
        const bool done = waitUntil([this, action]() { return action->seen && finished(QStringLiteral("query")); });
        if (probe)
        {
            probe->operation(QStringLiteral("query"), double(elapsed.nsecsElapsed()) / 1000000.0);
        }
        return done && action->success;
    }

    bool selectSample(int sampleNumber)
    {
        for (int row = 0; row < table()->rowCount(); ++row)
        {
            if (table()->item(row, 0) && table()->item(row, 0)->text().toInt() == sampleNumber)
            {
                const QModelIndex index = table()->model()->index(row, 1);
                table()->scrollTo(index);
                const QPoint position = table()->viewport()->mapTo(page, table()->visualRect(index).center());
                host.ensureVisible(position.x(), position.y(), 20, 20);
                QCoreApplication::processEvents();
                QTest::mouseClick(table()->viewport(), Qt::LeftButton, Qt::NoModifier,
                                  table()->visualRect(index).center());
                return table()->currentRow() == row;
            }
        }
        return false;
    }

    bool detail(const std::function<bool(QDialog *)> &action)
    {
        auto dialog = actOnDialog(QStringLiteral("MaskDlgReportDetail"), [action](QDialog *widget)
        {
            const bool result = action(widget);
            if (result)
            {
                widget->close();
            }
            else if (auto *cancel = control<QPushButton>(widget, "btnCancel"))
            {
                QTest::mouseClick(cancel, Qt::LeftButton);
            }
            return result;
        });
        QTest::mouseClick(button("btnDetail"), Qt::LeftButton);
        return waitUntil([dialog]() { return dialog->seen && dialog->success; });
    }

    bool exportAll(const QString &path, bool all = true)
    {
        operations->clear();
        auto range = actOnDialog(QStringLiteral("MaskDlgExport"), [all](QDialog *dialog)
        {
            auto *choice = control<QRadioButton>(dialog, all ? "ckxByAll" : "ckxBySelect");
            if (!choice)
            {
                return false;
            }
            QTest::mouseClick(choice, Qt::LeftButton);
            QTest::mouseClick(control<QPushButton>(dialog, "btnSave"), Qt::LeftButton);
            return true;
        });
        auto file = actOnDialog(QStringLiteral("historyCsvFileDialog"), [path](QDialog *dialog)
        {
            auto *chooser = qobject_cast<QFileDialog *>(dialog);
            if (!chooser)
            {
                return false;
            }
            chooser->selectFile(path);
            return QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
        });
        QTest::mouseClick(button("btnExport"), Qt::LeftButton);
        return waitUntil([this, range, file]()
        {
            return range->seen && range->success && file->seen && file->success
                    && finished(QStringLiteral("exportCsv"));
        });
    }

    QString directory;
    QString status;
    QThread thread;
    QScrollArea host;
    HistoryStore *store = nullptr;
    HistoryPage *page = nullptr;
    QSignalSpy *operations = nullptr;
    QSignalSpy *errors = nullptr;
    QSignalSpy *queryResults = nullptr;
    QSignalSpy *details = nullptr;
    QSignalSpy *exports = nullptr;
};

bool requestQuery(Session &session, qint64 requestId, const HistoryFilter &filter,
                  HistoryReports *reports, qint64 *total)
{
    session.queryResults->clear();
    const bool queued = QMetaObject::invokeMethod(session.store, "query", Qt::QueuedConnection,
        Q_ARG(qint64, requestId), Q_ARG(HistoryFilter, filter));
    if (!queued || !waitUntil([&session, requestId]()
    {
        for (const QList<QVariant> &values : *session.queryResults)
        {
            if (values.at(0).toLongLong() == requestId)
            {
                return true;
            }
        }
        return false;
    }))
    {
        return false;
    }
    for (const QList<QVariant> &values : *session.queryResults)
    {
        if (values.at(0).toLongLong() == requestId)
        {
            *reports = qvariant_cast<HistoryReports>(values.at(2));
            *total = values.at(3).toLongLong();
            return true;
        }
    }
    return false;
}

bool requestDetail(Session &session, qint64 requestId, qint64 reportId, HistoryReport *report)
{
    session.details->clear();
    if (!QMetaObject::invokeMethod(session.store, "detail", Qt::QueuedConnection,
            Q_ARG(qint64, requestId), Q_ARG(qint64, reportId)))
    {
        return false;
    }
    return waitUntil([&session, requestId, report]()
    {
        for (const QList<QVariant> &values : *session.details)
        {
            if (values.at(0).toLongLong() == requestId)
            {
                *report = qvariant_cast<HistoryReport>(values.at(1));
                return true;
            }
        }
        return false;
    });
}

bool hasError(const Session &session, qint64 requestId)
{
    for (const QList<QVariant> &values : *session.errors)
    {
        if (values.at(0).toLongLong() == requestId)
        {
            return true;
        }
    }
    return false;
}

QJsonObject patientJson(const HistoryPatient &patient)
{
    return {{QStringLiteral("report_id"), double(patient.reportId)},
        {QStringLiteral("name"), patient.name}, {QStringLiteral("sex"), patient.sex},
        {QStringLiteral("age"), patient.age}, {QStringLiteral("department"), patient.department},
        {QStringLiteral("patient_type"), patient.patientType},
        {QStringLiteral("application_serial"), patient.applicationSerial},
        {QStringLiteral("bed_number"), patient.bedNumber},
        {QStringLiteral("examiner"), patient.examiner}, {QStringLiteral("sender"), patient.sender},
        {QStringLiteral("auditor"), patient.auditor}, {QStringLiteral("remark"), patient.remark}};
}

HistoryPatient expectedPatient()
{
    HistoryPatient patient;
    patient.reportId = 3;
    patient.name = kSavedName;
    patient.sex = 1;
    patient.age = 42;
    patient.department = QStringLiteral("DO'Brien科");
    patient.patientType = QStringLiteral("T匿名复测类型01");
    patient.applicationSerial = QStringLiteral("T-'003-UP");
    patient.bedNumber = QStringLiteral("B床,42测试甲乙");
    patient.examiner = QStringLiteral("E检验'员测试甲乙");
    patient.sender = QStringLiteral("S送检\"员测试甲乙");
    patient.auditor = QStringLiteral("A审核员测试甲乙0");
    patient.remark = kSavedNote;
    return patient;
}

bool editPatient(QDialog *dialog)
{
    auto *info = control<QPushButton>(dialog, "btnMenuInfo");
    if (!info)
    {
        return false;
    }
    QTest::mouseClick(info, Qt::LeftButton);
    const HistoryPatient expected = expectedPatient();
    const QList<QPair<const char *, QString>> fields = {
        {"ldtName", expected.name}, {"ldtAge", QString::number(expected.age)},
        {"ldtKeShi", expected.department}, {"ldtBingRenLeiXin", expected.patientType},
        {"ldtApplyNumber", expected.applicationSerial}, {"ldtChuangHao", expected.bedNumber},
        {"ldtJianYan", expected.examiner}, {"ldtSongJian", expected.sender},
        {"ldtShenHe", expected.auditor}, {"ldtBeiZhu", expected.remark}};
    for (const auto &field : fields)
    {
        if (!paste(control<QLineEdit>(dialog, field.first), field.second))
        {
            return false;
        }
    }
    auto *sex = control<QComboBox>(dialog, "cbxSex");
    if (!sex || sex->findText(QStringLiteral("女")) < 0)
    {
        return false;
    }
    sex->setFocus();
    QTest::keyClick(sex, Qt::Key_End);
    return sex->currentText() == QStringLiteral("女");
}
}

class HistoryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void writeExperiment();
    void readExperiment();
    void paginationExperiment();

private:
    QString directory_;
    QString dataDirectory_;
    QString databasePath_;
};

void HistoryTest::initTestCase()
{
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    directory_ = QString::fromUtf8(qgetenv("SN100_HISTORY_EVIDENCE_DIR"));
    QVERIFY2(!directory_.isEmpty() && QDir::isAbsolutePath(directory_),
        "SN100_HISTORY_EVIDENCE_DIR must name a new absolute experiment directory");
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"),
        "History integration testing requires the actual WSLg XCB GUI");
    QVERIFY(QDir().mkpath(directory_));
    dataDirectory_ = QDir(directory_).filePath(QStringLiteral("data"));
    databasePath_ = QDir(dataDirectory_).filePath(QStringLiteral("history.sqlite"));
    qInfo().noquote() << "HISTORY_EVIDENCE_DIR" << directory_
        << "PID" << QCoreApplication::applicationPid();
}

void HistoryTest::writeExperiment()
{
    QVERIFY2(!QFileInfo::exists(dataDirectory_), "Refusing to overwrite a previous experiment data directory");
    Session session(dataDirectory_);
    QVERIFY2(session.ready(), qPrintable(session.status));
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(session.table());
    ResponseProbe response;

    QVERIFY2(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23"), {}, {}, &response, true), qPrintable(session.status));
    QCOMPARE(session.table()->rowCount(), 3);
    QList<int> daySamples;
    for (int row = 0; row < session.table()->rowCount(); ++row)
    {
        daySamples.append(session.table()->item(row, 0)->text().toInt());
    }
    std::sort(daySamples.begin(), daySamples.end());
    QCOMPARE(daySamples, QList<int>({2, 3, 4}));
    QVERIFY(session.host.grab().save(QDir(directory_).filePath(QStringLiteral("query_day.png"))));
    QVERIFY(session.page->grab().save(QDir(directory_).filePath(QStringLiteral("query_day_full_page.png"))));

    // Preserve the source screen's actual date SQL semantics, including its
    // effective date range when the date checkbox is not selected.
    HistoryFilter filter;
    filter.start = QDateTime(QDate(2026, 10, 23), QTime(0, 0));
    filter.end = QDateTime(QDate(2026, 10, 23), QTime(23, 59, 59));
    filter.byDate = false;
    HistoryReports reports;
    qint64 total = 0;
    QVERIFY(requestQuery(session, 10001, filter, &reports, &total));
    QCOMPARE(total, qint64(3));
    QCOMPARE(reports.size(), 3);
    filter.byDate = true;
    filter.sampleNumber = QStringLiteral("3");
    filter.barcode = QStringLiteral("ANON,\"003");
    QVERIFY(requestQuery(session, 10002, filter, &reports, &total));
    QCOMPARE(total, qint64(1));
    QCOMPARE(reports.first().id, qint64(3));
    QCOMPARE(reports.first().results.size(), 6);
    filter.sampleNumber.clear();
    filter.barcode = QStringLiteral("' OR 1=1 --");
    QVERIFY(requestQuery(session, 10003, filter, &reports, &total));
    QCOMPARE(total, qint64(0));
    QVERIFY(reports.isEmpty());
    filter.barcode.clear();
    filter.sampleNumber = QStringLiteral("3 OR 1=1");
    QVERIFY(QMetaObject::invokeMethod(session.store, "query", Qt::QueuedConnection,
        Q_ARG(qint64, 10004), Q_ARG(HistoryFilter, filter)));
    QVERIFY(waitUntil([&session]() { return hasError(session, 10004); }));
    filter.sampleNumber.clear();
    filter.start = QDateTime(QDate(2026, 10, 22), QTime(0, 0));
    filter.end = QDateTime(QDate(2026, 10, 24), QTime(23, 59, 59));
    filter.limit = 2;
    QVERIFY(requestQuery(session, 10005, filter, &reports, &total));
    QCOMPARE(total, qint64(5));
    QCOMPARE(reports.size(), 2);
    filter.offset = 4;
    QVERIFY(requestQuery(session, 10006, filter, &reports, &total));
    QCOMPARE(total, qint64(5));
    QCOMPARE(reports.size(), 1);
    HistoryReport before;
    QVERIFY(requestDetail(session, 10007, 3, &before));
    QVERIFY(QMetaObject::invokeMethod(session.store, "detail", Qt::QueuedConnection,
        Q_ARG(qint64, 10008), Q_ARG(qint64, 6)));
    QVERIFY(waitUntil([&session]() { return hasError(session, 10008); }));
    const QDateTime correctStart = filter.start;
    filter.start = filter.end.addSecs(1);
    QVERIFY(QMetaObject::invokeMethod(session.store, "query", Qt::QueuedConnection,
        Q_ARG(qint64, 10010), Q_ARG(HistoryFilter, filter)));
    QVERIFY(waitUntil([&session]() { return hasError(session, 10010); }));
    filter.start = correctStart;

    // Drive both real export dialogs and inspect the output bytes using an
    // independent CSV parser. Expected precision comes from fixed anonymous
    // inputs, never from HistoryStore::displayValue().
    const QString initialCsv = QDir(dataDirectory_).filePath(QStringLiteral("exports/day_initial.csv"));
    QVERIFY2(session.exportAll(initialCsv), qPrintable(session.status));
    QList<QStringList> csv;
    QByteArray csvBytes;
    QString error;
    QVERIFY2(readCsv(initialCsv, &csv, &csvBytes, &error), qPrintable(error));
    QCOMPARE(csv.size(), 5);
    QCOMPARE(csv.first(), QStringList({QStringLiteral("样本号"), QStringLiteral("样本条码"), QStringLiteral("报告日期"),
        QStringLiteral("姓名"), QStringLiteral("年龄"), QStringLiteral("性别"), QStringLiteral("PT s"),
        QStringLiteral("INR"), QStringLiteral("APTT s"), QStringLiteral("FIB g/L"), QStringLiteral("D-D ng/mL"), QStringLiteral("RAWX")}));
    for (const QStringList &row : csv)
    {
        QCOMPARE(row.size(), 12);
    }
    QStringList flattened;
    for (const QStringList &row : csv)
    {
        flattened.append(row);
    }
    for (const QString &expected : {QStringLiteral("12.3"), QStringLiteral("13.5"), QStringLiteral("1.24"),
         QStringLiteral("1.11"), QStringLiteral("35.68"), QStringLiteral("2.35"), QStringLiteral("123.456"),
         QStringLiteral("****"), QStringLiteral("0.00"), QStringLiteral("待确认,\"raw\"\nnext"), QStringLiteral("匿名,\"三号\"\n换行")})
    {
        QVERIFY2(flattened.contains(expected), qPrintable(QStringLiteral("CSV missing expected field: %1").arg(expected)));
    }
    QVERIFY(!csvBytes.startsWith("\xEF\xBB\xBF"));
    QVERIFY(csvBytes.contains(QTextCodec::codecForName("GBK")->fromUnicode(QStringLiteral("样本条码"))));

    QVERIFY(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23"), QStringLiteral("3"), QStringLiteral("ANON,\"003"), &response));
    QCOMPARE(session.table()->rowCount(), 1);
    QVERIFY(session.selectSample(3));
    // A selected-row export must honor the check box and include only that
    // report's repeated tasks. Space toggles the real table item's check state.
    session.table()->setCurrentCell(0, 0);
    QTest::keyClick(session.table(), Qt::Key_Space);
    QCOMPARE(session.table()->item(0, 0)->checkState(), Qt::Checked);
    const QString selectedCsv = QDir(dataDirectory_).filePath(QStringLiteral("exports/selected_3.csv"));
    QVERIFY(session.exportAll(selectedCsv, false));
    QList<QStringList> selectedRows;
    QByteArray selectedBytes;
    QVERIFY2(readCsv(selectedCsv, &selectedRows, &selectedBytes, &error), qPrintable(error));
    QCOMPARE(selectedRows.size(), 3);
    QCOMPARE(selectedRows.at(1).at(0), QStringLiteral("3"));
    QCOMPARE(selectedRows.at(2).at(0), QStringLiteral("3"));
    // Changing one field must not truncate legacy text in untouched controls
    // or turn the fixture's unknown sex into the combo's displayed default.
    QVERIFY(session.detail([&](QDialog *dialog)
    {
        auto *content = dialog->findChild<QWidget *>(QStringLiteral("historyDetailContent"));
        if (!(content ? content : dialog)->grab().save(QDir(directory_).filePath(QStringLiteral("detail_results.png"))))
        {
            return false;
        }
        QTest::mouseClick(control<QPushButton>(dialog, "btnMenuInfo"), Qt::LeftButton);
        if (!paste(control<QLineEdit>(dialog, "ldtAge"), QStringLiteral("1")))
        {
            return false;
        }
        session.operations->clear();
        QTest::mouseClick(control<QPushButton>(dialog, "btnSave"), Qt::LeftButton);
        if (!waitUntil([&session]() { return session.finished(QStringLiteral("savePatient")); })
                || !paste(control<QLineEdit>(dialog, "ldtName"), QStringLiteral("C取消样本测试甲乙")))
        {
            return false;
        }
        QTest::mouseClick(control<QPushButton>(dialog, "btnCancel"), Qt::LeftButton);
        return !dialog->isVisible();
    }));
    HistoryReport ageOnly;
    QVERIFY(requestDetail(session, 10009, 3, &ageOnly));
    HistoryPatient unchanged = before.patient;
    unchanged.age = 1;
    QCOMPARE(patientJson(ageOnly.patient), patientJson(unchanged));
    QJsonObject beforeLock;
    QVERIFY2(databaseSnapshot(databasePath_, &beforeLock, &error), qPrintable(error));
    QJsonObject lockMetrics;
    QString detailFailure;
    const QString lockConnection = QStringLiteral("history-external-writer");
    {
        QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockConnection);
        locker.setDatabaseName(databasePath_);
        QVERIFY(locker.open());
        QSqlQuery lockQuery(locker);
        QVERIFY(lockQuery.exec(QStringLiteral("BEGIN IMMEDIATE")));
        const bool detailPassed = session.detail([&](QDialog *dialog)
        {
            auto fail = [&detailFailure](const QString &message) { detailFailure = message; return false; };
            if (!editPatient(dialog))
            {
                return fail(QStringLiteral("Patient inputs were not editable"));
            }
            auto *save = control<QPushButton>(dialog, "btnSave");
            session.operations->clear();
            const int firstSample = response.gaps.size();
            QElapsedTimer elapsed;
            elapsed.start();
            reveal(save);
            QTest::mouseClick(save, Qt::LeftButton);
            if (!session.page->isBusy() || save->isEnabled())
            {
                return fail(QStringLiteral("Save did not enter the visible busy state"));
            }
            const bool closeRejected = !dialog->close() && !session.page->close();
            if (!closeRejected || !waitUntil([&session]() { return session.finished(QStringLiteral("savePatient"), false); }))
            {
                return fail(QStringLiteral("Locked save/close guard did not finish with a recoverable error"));
            }
            const double duration = elapsed.nsecsElapsed() / 1000000.0;
            double maxGap = 0;
            for (int index = firstSample; index < response.gaps.size(); ++index)
            {
                maxGap = qMax(maxGap, response.gaps.at(index));
            }
            QJsonObject afterFailure;
            if (response.gaps.size() - firstSample < 5 || !databaseSnapshot(databasePath_, &afterFailure, &error)
                    || afterFailure != beforeLock)
            {
                return fail(QStringLiteral("Writer lock blocked the GUI or partially changed persistent data"));
            }
            if (!dialog->grab().save(QDir(directory_).filePath(QStringLiteral("locked_patient_save.png"))))
            {
                return fail(QStringLiteral("Could not retain the actual error screenshot"));
            }
            lockMetrics = {{QStringLiteral("failed_save_ms"), duration},
                {QStringLiteral("gui_heartbeat_samples"), response.gaps.size() - firstSample},
                {QStringLiteral("gui_heartbeat_max_gap_ms"), maxGap},
                {QStringLiteral("close_rejected_while_busy"), closeRejected},
                {QStringLiteral("partial_commit_detected"), false}};
            response.operation(QStringLiteral("locked_save_failed"), duration);
            if (!lockQuery.exec(QStringLiteral("ROLLBACK")))
            {
                return fail(QStringLiteral("Could not release the independent writer lock"));
            }
            session.operations->clear();
            reveal(save);
            QTest::mouseClick(save, Qt::LeftButton);
            if (!waitUntil([&session]() { return session.finished(QStringLiteral("savePatient")); }))
            {
                return fail(QStringLiteral("Save could not be retried after releasing the lock"));
            }
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                const QString remark = iteration == 99 ? kSavedNote : QStringLiteral("压力保存 %1 O'Brien").arg(iteration);
                if (!paste(control<QLineEdit>(dialog, "ldtBeiZhu"), remark))
                {
                    return fail(QStringLiteral("Repeated save input was not editable"));
                }
                session.operations->clear();
                reveal(save);
                elapsed.restart();
                QTest::mouseClick(save, Qt::LeftButton);
                if (!waitUntil([&session]() { return session.finished(QStringLiteral("savePatient")); }))
                {
                    return fail(QStringLiteral("Repeated save failed at iteration %1").arg(iteration));
                }
                response.operation(QStringLiteral("savePatient"), elapsed.nsecsElapsed() / 1000000.0);
            }
            reveal(save);
            auto *content = dialog->findChild<QWidget *>(QStringLiteral("historyDetailContent"));
            return dialog->grab().save(QDir(directory_).filePath(QStringLiteral("patient_saved.png")))
                    && (content ? content : dialog)->grab().save(QDir(directory_).filePath(QStringLiteral("patient_saved_full_content.png")));
        });
        QVERIFY2(detailPassed, qPrintable(detailFailure.isEmpty() ? session.status : detailFailure));
        locker.close();
    }
    QSqlDatabase::removeDatabase(lockConnection);
    HistoryReport after;
    QVERIFY(requestDetail(session, 11001, 3, &after));
    QCOMPARE(patientJson(after.patient), patientJson(expectedPatient()));
    QCOMPARE(after.results.size(), before.results.size());
    for (int index = 0; index < after.results.size(); ++index)
    {
        QCOMPARE(after.results.at(index).taskId, before.results.at(index).taskId);
        QCOMPARE(after.results.at(index).values.size(), before.results.at(index).values.size());
        for (int value = 0; value < after.results.at(index).values.size(); ++value)
        {
            QCOMPARE(after.results.at(index).values.at(value).rawValue, before.results.at(index).values.at(value).rawValue);
        }
    }

    for (int iteration = 0; iteration < 100; ++iteration)
    {
        QVERIFY2(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23"),
            iteration % 2 ? QStringLiteral("3") : QString(), {}, &response), qPrintable(session.status));
        QCOMPARE(session.table()->rowCount(), iteration % 2 ? 1 : 3);
    }
    QVERIFY(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23"), QStringLiteral("9999")));
    QCOMPARE(session.table()->rowCount(), 0);
    const QString emptyCsv = QDir(dataDirectory_).filePath(QStringLiteral("exports/empty.csv"));
    session.operations->clear();
    session.page->exportCsv(emptyCsv, true);
    QVERIFY(waitUntil([&session]() { return session.finished(QStringLiteral("exportCsv"), false); }));
    QVERIFY(!QFileInfo::exists(emptyCsv));
    QVERIFY(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23")));
    const QString finalCsv = QDir(dataDirectory_).filePath(QStringLiteral("exports/day_saved.csv"));
    QVERIFY(session.exportAll(finalCsv));
    csv.clear();
    QVERIFY2(readCsv(finalCsv, &csv, &csvBytes, &error), qPrintable(error));
    QCOMPARE(csv.size(), 5);
    flattened.clear();
    for (const QStringList &row : csv)
    {
        flattened.append(row);
    }
    QVERIFY(flattened.contains(kSavedName));
    QVERIFY(session.host.grab().save(QDir(directory_).filePath(QStringLiteral("history_saved.png"))));
    QVERIFY(session.page->grab().save(QDir(directory_).filePath(QStringLiteral("history_saved_full_page.png"))));
    QJsonObject actual;
    QVERIFY2(databaseSnapshot(databasePath_, &actual, &error), qPrintable(error));
    const QJsonObject evidence{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("mode"), QStringLiteral("WSLg anonymous history module")},
        {QStringLiteral("calendar_dates_selected_via_real_grid"), true},
        {QStringLiteral("data_directory"), dataDirectory_},
        {QStringLiteral("expected_patient"), patientJson(expectedPatient())},
        {QStringLiteral("actual_patient"), patientJson(after.patient)},
        {QStringLiteral("actual_database"), actual},
        {QStringLiteral("response"), response.result()}, {QStringLiteral("locked_save"), lockMetrics},
        {QStringLiteral("display"), QJsonObject{
            {QStringLiteral("screen_width"), QGuiApplication::primaryScreen()->geometry().width()},
            {QStringLiteral("screen_height"), QGuiApplication::primaryScreen()->geometry().height()},
            {QStringLiteral("logical_dpi"), QGuiApplication::primaryScreen()->logicalDotsPerInch()},
            {QStringLiteral("device_pixel_ratio"), QGuiApplication::primaryScreen()->devicePixelRatio()},
            {QStringLiteral("host_width"), session.host.width()},
            {QStringLiteral("host_height"), session.host.height()},
            {QStringLiteral("font"), QApplication::font().family()},
            {QStringLiteral("style"), QStringLiteral("Fusion")}}},
        {QStringLiteral("repeated_queries"), 100}, {QStringLiteral("repeated_saves"), 100},
        {QStringLiteral("day_reports"), 3}, {QStringLiteral("all_visible_reports"), 5},
        {QStringLiteral("csv_day_rows"), 4}, {QStringLiteral("csv_columns"), 12},
        {QStringLiteral("csv_gbk_and_quoted_multiline_verified"), true},
        {QStringLiteral("selected_export_only_checked_report"), true},
        {QStringLiteral("empty_export_refused"), true}, {QStringLiteral("hidden_detail_refused"), true},
        {QStringLiteral("sql_quotes_and_invalid_sample_verified"), true},
        {QStringLiteral("untouched_multiline_fields_and_unknown_sex_preserved"), true},
        {QStringLiteral("patient_draft_cancelled_without_commit"), true},
        {QStringLiteral("results_unchanged_after_patient_save"), true}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("write_result.json")), evidence));
    QVERIFY(!session.page->isBusy());
    QVERIFY(session.page->close());
    qInfo().noquote() << "HISTORY_PATIENT_ACTUAL" << QJsonDocument(patientJson(after.patient)).toJson(QJsonDocument::Compact);
    qInfo().noquote() << "HISTORY_RESPONSE" << QJsonDocument(response.result()).toJson(QJsonDocument::Compact);
}

void HistoryTest::readExperiment()
{
    QVERIFY2(QFileInfo::exists(databasePath_), "Run writeExperiment in a separate process first");
    const QJsonObject written = readJson(QDir(directory_).filePath(QStringLiteral("write_result.json")));
    QVERIFY(!written.isEmpty());
    QVERIFY2(written.value(QStringLiteral("process_id")).toDouble() != double(QCoreApplication::applicationPid()),
        "Readback must execute in a different process");
    Session session(dataDirectory_);
    QVERIFY2(session.ready(), qPrintable(session.status));
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23"), QStringLiteral("3"), QStringLiteral("ANON,\"003")));
    QCOMPARE(session.table()->rowCount(), 1);
    QVERIFY(session.selectSample(3));
    bool fieldsMatched = false;
    QVERIFY(session.detail([&](QDialog *dialog)
    {
        QTest::mouseClick(control<QPushButton>(dialog, "btnMenuInfo"), Qt::LeftButton);
        fieldsMatched = control<QLineEdit>(dialog, "ldtName")->text() == kSavedName
                && control<QLineEdit>(dialog, "ldtAge")->text() == QStringLiteral("42")
                && control<QLineEdit>(dialog, "ldtBeiZhu")->text() == kSavedNote
                && control<QComboBox>(dialog, "cbxSex")->currentText() == QStringLiteral("女");
        return fieldsMatched && dialog->grab().save(QDir(directory_).filePath(QStringLiteral("patient_reopened.png")));
    }));
    QVERIFY(fieldsMatched);
    HistoryReport actualReport;
    QVERIFY(requestDetail(session, 12001, 3, &actualReport));
    QCOMPARE(patientJson(actualReport.patient), patientJson(expectedPatient()));
    QJsonObject actual;
    QString error;
    QVERIFY2(databaseSnapshot(databasePath_, &actual, &error), qPrintable(error));
    QCOMPARE(actual, written.value(QStringLiteral("actual_database")).toObject());
    QVERIFY(session.host.grab().save(QDir(directory_).filePath(QStringLiteral("history_reopened.png"))));
    const QJsonObject evidence{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("writer_process_id"), written.value(QStringLiteral("process_id"))},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("new_process_readback"), true},
        {QStringLiteral("actual_patient"), patientJson(actualReport.patient)},
        {QStringLiteral("actual_database"), actual}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("read_result.json")), evidence));
    QVERIFY(session.page->close());
    qInfo().noquote() << "HISTORY_REOPENED_ACTUAL" << QJsonDocument(patientJson(actualReport.patient)).toJson(QJsonDocument::Compact);
}

void HistoryTest::paginationExperiment()
{
    // A separate marked anonymous database is enlarged only after its worker
    // has closed. The principal save/readback experiment is never modified.
    const QString paginationDirectory = QDir(directory_).filePath(QStringLiteral("pagination_data"));
    QVERIFY2(!QFileInfo::exists(paginationDirectory), "Refusing to overwrite the pagination experiment");
    {
        Session seed(paginationDirectory);
        QVERIFY2(seed.ready(), qPrintable(seed.status));
        QVERIFY(seed.page->close());
    }
    const QString path = QDir(paginationDirectory).filePath(QStringLiteral("history.sqlite"));
    const QString connection = QStringLiteral("history-pagination-fixture");
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        QVERIFY(database.open());
        QVERIFY(database.transaction());
        QSqlQuery query(database);
        // Bring the two outside-day synthetic reports into the fixed day; the
        // hidden report stays hidden. Forty new APTT reports give 45 visible
        // reports, each identified by a distinct id/row_id/sample/barcode.
        QVERIFY(query.prepare(QStringLiteral("UPDATE report_data SET data_time=? WHERE id IN(1,5)")));
        query.addBindValue(QStringLiteral("20261023120000"));
        QVERIFY(query.exec());
        for (int index = 0; index < 40; ++index)
        {
            const int id = index + 7;
            const int rowId = index + 1000;
            const int taskId = index + 2000;
            QVERIFY(query.prepare(QStringLiteral("INSERT INTO report_data SELECT ?,?,?,?,data_time,visible,state,name,sex,age,department,patient_type,application_serial,bed_number,jianyan,checker,auditor,remark FROM report_data WHERE id=2")));
            query.addBindValue(id);
            query.addBindValue(rowId);
            query.addBindValue(id);
            query.addBindValue(QStringLiteral("ANON-PAGE-%1").arg(id));
            QVERIFY(query.exec());
            QVERIFY(query.prepare(QStringLiteral("INSERT INTO report_results SELECT ?,?,project_name,date_time,abnormal_type FROM report_results WHERE task_id=1002")));
            query.addBindValue(taskId);
            query.addBindValue(rowId);
            QVERIFY(query.exec());
            QVERIFY(query.prepare(QStringLiteral("INSERT INTO report_values SELECT ?,result_type,raw_value FROM report_values WHERE task_id=1002")));
            query.addBindValue(taskId);
            QVERIFY(query.exec());
        }
        query.finish();
        QVERIFY(database.commit());
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    Session session(paginationDirectory);
    QVERIFY2(session.ready(), qPrintable(session.status));
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(session.query(QStringLiteral("2026/10/23"), QStringLiteral("2026/10/23")));
    QCOMPARE(session.table()->rowCount(), 20);
    auto *currentPage = control<QLabel>(session.page, "ldtCurPage");
    auto *totalPages = control<QLabel>(session.page, "lblTotalPage");
    QVERIFY(currentPage);
    QVERIFY(totalPages);
    QCOMPARE(currentPage->text(), QStringLiteral("1"));
    QCOMPARE(totalPages->text(), QStringLiteral("3"));
    QList<int> observed;
    QJsonArray pages;
    auto rememberPage = [&]()
    {
        QJsonArray ids;
        for (int row = 0; row < session.table()->rowCount(); ++row)
        {
            const int sample = session.table()->item(row, 0)->text().toInt();
            observed.append(sample);
            ids.append(sample);
        }
        pages.append(QJsonObject{{QStringLiteral("page"), currentPage->text().toInt()},
            {QStringLiteral("row_count"), session.table()->rowCount()}, {QStringLiteral("sample_numbers"), ids}});
    };
    auto pageButton = [&](const char *name)
    {
        session.operations->clear();
        QTest::mouseClick(session.button(name), Qt::LeftButton);
        return waitUntil([&session]() { return session.finished(QStringLiteral("query")); });
    };
    rememberPage();
    QVERIFY(pageButton("btnNextPage"));
    QCOMPARE(currentPage->text(), QStringLiteral("2"));
    QCOMPARE(session.table()->rowCount(), 20);
    rememberPage();
    QVERIFY(pageButton("btnLastPage"));
    QCOMPARE(currentPage->text(), QStringLiteral("3"));
    QCOMPARE(session.table()->rowCount(), 5);
    rememberPage();
    std::sort(observed.begin(), observed.end());
    QList<int> expected{1, 2, 3, 4, 5};
    for (int id = 7; id <= 46; ++id)
    {
        expected.append(id);
    }
    QCOMPARE(observed, expected);
    QVERIFY(pageButton("btnPrvPage"));
    QCOMPARE(currentPage->text(), QStringLiteral("2"));
    QVERIFY(pageButton("btnFirstPage"));
    QCOMPARE(currentPage->text(), QStringLiteral("1"));
    session.host.horizontalScrollBar()->setValue(session.host.horizontalScrollBar()->maximum());
    session.host.verticalScrollBar()->setValue(session.host.verticalScrollBar()->maximum());
    QCoreApplication::processEvents();
    QVERIFY(session.host.grab().save(QDir(directory_).filePath(QStringLiteral("pagination_45_reports.png"))));
    QVERIFY(session.page->grab().save(QDir(directory_).filePath(QStringLiteral("pagination_full_page.png"))));

    // Export all from page 1 must cover every page. The fixture's second PT
    // task produces one extra row; base identities still total 45 distinct
    // reports, with only sample 3 repeated.
    const QString csvPath = QDir(paginationDirectory).filePath(QStringLiteral("exports/all_45.csv"));
    QVERIFY(session.exportAll(csvPath));
    QList<QStringList> csv;
    QByteArray bytes;
    QString error;
    QVERIFY2(readCsv(csvPath, &csv, &bytes, &error), qPrintable(error));
    QCOMPARE(csv.size(), 47);
    QList<int> exported;
    for (int row = 1; row < csv.size(); ++row)
    {
        QCOMPARE(csv.at(row).size(), 12);
        exported.append(csv.at(row).at(0).toInt());
    }
    QCOMPARE(exported.count(3), 2);
    exported.removeOne(3);
    std::sort(exported.begin(), exported.end());
    QCOMPARE(exported, expected);
    const QJsonObject evidence{{QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("anonymous_visible_reports"), 45}, {QStringLiteral("page_size"), 20},
        {QStringLiteral("pages"), pages}, {QStringLiteral("csv_data_rows"), 46},
        {QStringLiteral("csv_distinct_reports"), 45}, {QStringLiteral("all_pages_without_missing_or_duplicate_reports"), true},
        {QStringLiteral("first_previous_next_last_buttons_verified"), true},
        {QStringLiteral("csv_sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())}};
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("pagination_result.json")), evidence));
    QVERIFY(session.page->close());
}

QTEST_MAIN(HistoryTest)
#include "history_test.moc"
