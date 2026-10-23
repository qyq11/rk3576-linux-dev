// O04-2 hand-written, anonymous statistical and real Widgets report checks.
// This target has its own evidence/data directory; history_test stays unchanged.
#include <QtTest>
#include <QApplication>
#include <QCalendarWidget>
#include <QCryptographicHash>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QPrintDialog>
#include <QPrinter>
#include <QPainter>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScreen>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif
#include "history/history_page.h"
#include "history/report_controller.h"
#include "history/report_document.h"
#include "history/result_statistics.h"
#include "qtrpt.h"

namespace
{
bool waitUntil(const std::function<bool()> &predicate, int timeout = 15000)
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

bool closeEnough(double actual, double expected, double tolerance = 1e-10)
{
    return std::isfinite(actual) && std::abs(actual - expected) <= tolerance;
}

void reveal(QWidget *control)
{
    if (!control) return;
    for (QWidget *parent = control->parentWidget(); parent; parent = parent->parentWidget())
    {
        if (auto *scroll = qobject_cast<QScrollArea *>(parent))
            scroll->ensureWidgetVisible(control, 10, 10);
        if (parent->isWindow()) break;
    }
    QCoreApplication::processEvents();
}

HistoryResultValue value(const QString &raw, int type = 1,
                         const QString &label = QStringLiteral("PT"),
                         const QString &unit = QStringLiteral("s"),
                         int decimals = 2, double factor = 1.0)
{
    HistoryResultValue result;
    result.rawValue = raw;
    result.resultType = type;
    result.label = label;
    result.unit = unit;
    result.decimals = decimals;
    result.factor = factor;
    return result;
}

HistoryReport report(const QStringList &numbers, int type = 1, double factor = 1.0)
{
    HistoryReport output;
    output.id = 700;
    output.rowId = 1700;
    output.sampleNumber = 700;
    for (int index = 0; index < numbers.size(); ++index)
    {
        HistoryResult task;
        task.taskId = 7000 + index;
        task.project = QStringLiteral("PT");
        task.testedAt = QDateTime(QDate(2026, 10, 23), QTime(12, 0, index));
        task.values.append(value(numbers.at(index), type, QStringLiteral("PT"),
                                 QStringLiteral("s"), 2, factor));
        output.results.append(task);
    }
    return output;
}

// Serialize every source field used in reporting; an immutable snapshot catches
// accidental replacement of raw results with formatted means or unit scaling.
QJsonArray rawSnapshot(const HistoryReport &report)
{
    QJsonArray output;
    for (const HistoryResult &task : report.results)
    {
        QJsonArray values;
        for (const HistoryResultValue &entry : task.values)
        {
            values.append(QJsonObject{{"type", entry.resultType}, {"label", entry.label},
                {"unit", entry.unit}, {"raw", entry.rawValue}, {"decimals", entry.decimals},
                {"factor", entry.factor}});
        }
        output.append(QJsonObject{{"task_id", double(task.taskId)}, {"project", task.project},
            {"tested_at", task.testedAt.toString(Qt::ISODate)},
            {"abnormal_type", task.abnormalType}, {"values", values}});
    }
    return output;
}

QJsonObject statisticJson(const HistoryStatisticGroup &group)
{
    return {{"project", group.project}, {"result_type", group.resultType},
        {"entries", group.entries.size()}, {"valid_count", group.validCount},
        {"invalid_count", group.invalidCount}, {"summary_row", group.hasAverageRow()},
        {"configuration_consistent", group.configurationConsistent},
        {"mean_available", group.meanAvailable}, {"sd_available", group.sampleSdAvailable},
        {"cv_available", group.cvAvailable},
        {"mean", group.meanAvailable ? QJsonValue(group.mean) : QJsonValue()},
        {"sample_sd", group.sampleSdAvailable ? QJsonValue(group.sampleSd) : QJsonValue()},
        {"cv_percent", group.cvAvailable ? QJsonValue(group.cvPercent) : QJsonValue()},
        {"mean_text", group.meanText}, {"cv_text", group.cvText},
        {"mean_unavailable_reason", group.meanUnavailableReason},
        {"cv_unavailable_reason", group.cvUnavailableReason}};
}

HistoryReport printableCopy(HistoryReport source)
{
    source.patient.name = QStringLiteral("匿名三号");
    source.patient.remark = QStringLiteral("匿名备注含引号\"与逗号,及单引号'");
    return source;
}

HistoryReport sixtyFourTasks(HistoryReport source)
{
    source.patient.name = QStringLiteral("匿名分页");
    source.patient.remark.clear();
    source.results.clear();
    for (int row = 0; row < 64; ++row)
    {
        HistoryResult task;
        task.taskId = 8000 + row;
        task.project = QStringLiteral("PT");
        task.testedAt = QDateTime(QDate(2026, 10, 23), QTime(10, row / 60, row % 60));
        task.values.append(value(QString::number(8000 + row) + QStringLiteral(".1"), 1,
            QStringLiteral("PT"), QStringLiteral("s"), 1));
        source.results.append(task);
    }
    return source;
}

bool saveDocumentSnapshots(const HistoryReport &source, const QString &directory, QString *error)
{
    HistoryPrintOptions options;
    HistoryPrintDocument seven;
    HistoryPrintDocument large;
    if (!HistoryReportDocument::anonymousOptions(&options, error)
        || !HistoryReportDocument::build(printableCopy(source), options, &seven, error)
        || !HistoryReportDocument::build(sixtyFourTasks(source), options, &large, error))
        return false;
    return saveJson(QDir(directory).filePath(QStringLiteral("printable7_snapshot.json")), HistoryReportDocument::toJson(seven))
        && saveJson(QDir(directory).filePath(QStringLiteral("rows64_snapshot.json")), HistoryReportDocument::toJson(large));
}

bool databaseSnapshot(const QString &path, QJsonObject *snapshot, QString *error)
{
    const QString connection = QStringLiteral("report-verification-") + QUuid::createUuid().toString();
    bool success = true;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(path);
        success = database.open();
        if (!success)
            *error = database.lastError().text();
        else
        {
            for (const QString &table : {QStringLiteral("report_data"), QStringLiteral("report_results"),
                 QStringLiteral("report_values"), QStringLiteral("report_columns")})
            {
                QSqlQuery query(database);
                if (!query.exec(QStringLiteral("SELECT * FROM %1 ORDER BY rowid").arg(table)))
                {
                    success = false;
                    *error = query.lastError().text();
                    break;
                }
                const QSqlRecord columns = query.record();
                QJsonArray rows;
                while (query.next())
                {
                    QJsonObject row;
                    for (int index = 0; index < columns.count(); ++index)
                        row.insert(columns.fieldName(index), QJsonValue::fromVariant(query.value(index)));
                    rows.append(row);
                }
                snapshot->insert(table, rows);
            }
            QSqlQuery integrity(database);
            success = success && integrity.exec(QStringLiteral("PRAGMA integrity_check"))
                && integrity.next() && integrity.value(0).toString() == QStringLiteral("ok");
            snapshot->insert(QStringLiteral("integrity_check"), success ? QStringLiteral("ok") : QStringLiteral("failed"));
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return success;
}

// The PDF is reopened by an independent parser, not by the report renderer.
// Set SN100_REPORT_PDF_PYTHON to a Python executable containing pypdf. Windows
// bundled Python can inspect a /mnt/<drive>/ evidence file through its drive path.
bool inspectPdf(const QString &path, QJsonObject *result, QString *error)
{
    const QString python = QString::fromUtf8(qgetenv("SN100_REPORT_PDF_PYTHON"));
    if (python.isEmpty())
    {
        *error = QStringLiteral("SN100_REPORT_PDF_PYTHON must select an independent pypdf runtime");
        return false;
    }
    QString inputPath = path;
#ifdef Q_OS_LINUX
    if (python.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
        && inputPath.startsWith(QStringLiteral("/mnt/")) && inputPath.size() > 7)
        inputPath = inputPath.mid(5, 1).toUpper() + QStringLiteral(":/") + inputPath.mid(7);
#endif
    const QString script = QStringLiteral(
        "import json,sys; from pypdf import PdfReader; r=PdfReader(sys.argv[1]); "
        "print(json.dumps({'page_count':len(r.pages), 'pages':[{'width_pt':float(p.mediabox.width),"
        "'height_pt':float(p.mediabox.height),'text':p.extract_text()} for p in r.pages]},ensure_ascii=True))");
    QProcess process;
    process.start(python, {QStringLiteral("-c"), script, inputPath});
    if (!waitUntil([&process]() { return process.state() == QProcess::NotRunning; }, 20000)
        || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        *error = process.errorString() + QString::fromUtf8(process.readAllStandardError());
        process.kill();
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
    {
        *error = parse.errorString();
        return false;
    }
    *result = document.object();
    QJsonArray pages = result->value(QStringLiteral("pages")).toArray();
    for (int index = 0; index < pages.size(); ++index)
    {
        auto page = pages.at(index).toObject();
        page.insert(QStringLiteral("normalized_text"), page.value(QStringLiteral("text")).toString()
            .normalized(QString::NormalizationForm_KC));
        pages[index] = page;
    }
    result->insert(QStringLiteral("pages"), pages);
    result->insert(QStringLiteral("search_normalization"), QStringLiteral("Unicode NFKC; raw text remains in each page.text"));
    return true;
}

QString pdfText(const QJsonObject &pdf)
{
    QString output;
    for (const auto &page : pdf.value(QStringLiteral("pages")).toArray())
        output += page.toObject().value(QStringLiteral("text")).toString();
    // Qt/Noto's subset CMap can expose compatibility radicals (e.g. U+2F6C
    // for 目) for the same drawn glyph. Normalize only the search copy; JSON
    // keeps the untouched parser text and the PDF itself is never rewritten.
    return output.normalized(QString::NormalizationForm_KC);
}

class EventProbe : public QObject
{
public:
    EventProbe()
    {
        clock.start();
        previous = clock.nsecsElapsed();
        timer.setInterval(10);
        timer.setTimerType(Qt::PreciseTimer);
        connect(&timer, &QTimer::timeout, this, [this]()
        {
            const qint64 now = clock.nsecsElapsed();
            gaps.append(double(now - previous) / 1000000.0);
            previous = now;
            if (gaps.size() % 10 == 0)
                sampleResources();
        });
        timer.start();
    }
    QJsonObject result() const
    {
        QList<double> sorted = gaps;
        std::sort(sorted.begin(), sorted.end());
        QJsonArray actual;
        for (double gap : gaps)
            actual.append(gap);
        QJsonObject output{{"timer_interval_ms", 10}, {"samples_ms", actual},
            {"sample_count", sorted.size()}, {"operations", operations},
            {"resource_samples", resources}, {"clock_ticks_per_second", double(CLOCKS_PER_SEC)},
            {"resource_scope", "test parent process; child PDF renderer not included"},
            {"renderer_resource_samples", rendererResources},
            {"renderer_resource_scope", "separate renderer PIDs sampled every approximately 100 ms; CPU ticks are cumulative observed /proc values"},
            {"response_scope", "mouse dispatch to signal/content readiness; not pixel presentation or real time"}};
        for (int percentile : {50, 95, 99, 100})
        {
            const int index = qMax(0, int(std::ceil(sorted.size() * percentile / 100.0)) - 1);
            output.insert(QStringLiteral("gap_p%1_ms").arg(percentile),
                sorted.isEmpty() ? QJsonValue() : QJsonValue(sorted.at(index)));
        }
        return output;
    }
    void sampleResources()
    {
        QJsonObject sample{{"elapsed_ms", clock.elapsed()}, {"cpu_clock_ticks", double(std::clock())}};
#ifdef Q_OS_LINUX
        const auto readFields = [](const QString &path)
        {
            QMap<QString, qint64> fields;
            QFile file(path);
            if (file.open(QIODevice::ReadOnly))
            {
                for (const QByteArray &line : file.readAll().split('\n'))
                {
                    const QList<QByteArray> parts = line.simplified().split(' ');
                    if (parts.size() >= 2)
                    {
                        bool ok = false;
                        const qint64 number = parts.at(1).toLongLong(&ok);
                        if (ok) fields.insert(QString::fromLatin1(parts.first()), number);
                    }
                }
            }
            return fields;
        };
        const auto status = readFields(QStringLiteral("/proc/self/status"));
        const auto rollup = readFields(QStringLiteral("/proc/self/smaps_rollup"));
        for (const auto &key : {QStringLiteral("VmRSS:"), QStringLiteral("VmHWM:"), QStringLiteral("Threads:")})
            sample.insert(key, status.contains(key) ? QJsonValue(double(status.value(key))) : QJsonValue());
        sample.insert(QStringLiteral("Pss_kib"), rollup.contains(QStringLiteral("Pss:"))
            ? QJsonValue(double(rollup.value(QStringLiteral("Pss:")))) : QJsonValue());
        sample.insert(QStringLiteral("fd_count"), QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
        const qint64 pid = rendererPid ? rendererPid() : 0;
        if (pid > 0)
        {
            const QString prefix = QStringLiteral("/proc/%1").arg(pid);
            const auto childStatus = readFields(prefix + QStringLiteral("/status"));
            const auto childRollup = readFields(prefix + QStringLiteral("/smaps_rollup"));
            QJsonObject child{{"elapsed_ms", clock.elapsed()}, {"pid", double(pid)},
                {"proc_clock_ticks_per_second", double(sysconf(_SC_CLK_TCK))}};
            for (const auto &key : {QStringLiteral("VmRSS:"), QStringLiteral("VmHWM:"), QStringLiteral("Threads:")})
                child.insert(key, childStatus.contains(key) ? QJsonValue(double(childStatus.value(key))) : QJsonValue());
            child.insert(QStringLiteral("Pss_kib"), childRollup.contains(QStringLiteral("Pss:"))
                ? QJsonValue(double(childRollup.value(QStringLiteral("Pss:")))) : QJsonValue());
            child.insert(QStringLiteral("fd_count"), QDir(prefix + QStringLiteral("/fd")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size());
            QFile processStat(prefix + QStringLiteral("/stat"));
            if (processStat.open(QIODevice::ReadOnly))
            {
                const QByteArray record = processStat.readAll();
                const QList<QByteArray> fields = record.mid(record.lastIndexOf(')') + 2).simplified().split(' ');
                if (fields.size() > 12)
                {
                    child.insert(QStringLiteral("user_ticks"), double(fields.at(11).toLongLong()));
                    child.insert(QStringLiteral("system_ticks"), double(fields.at(12).toLongLong()));
                }
            }
            rendererResources.append(child);
        }
#endif
        resources.append(sample);
    }
    QTimer timer;
    QElapsedTimer clock;
    qint64 previous = 0;
    QList<double> gaps;
    QJsonArray operations;
    QJsonArray resources;
    QJsonArray rendererResources;
    std::function<qint64()> rendererPid;
};

class Session
{
public:
    explicit Session(const QString &directory)
    {
        HistoryStore::registerTypes();
        store = new HistoryStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        page = new HistoryPage(store);
        page->setDataDirectory(directory);
        host.setWidget(page);
        host.resize(1600, 900);
        QObject::connect(page, &HistoryPage::statusChanged, page,
            [this](const QString &text) { status = text; });
        QObject::connect(&thread, &QThread::started, store,
            [this, directory]() { store->initialize(directory); });
        host.show();
        thread.start();
    }
    ~Session()
    {
        host.takeWidget();
        delete page;
        if (thread.isRunning())
        {
            QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
            thread.quit();
            thread.wait();
        }
    }
    bool ready() { return waitUntil([this]() { return page->isReady() && !page->isBusy(); }); }
    QPushButton *button(const char *name)
    {
        auto *control = page->findChild<QPushButton *>(QString::fromLatin1(name));
        if (control)
        {
            host.ensureWidgetVisible(control, 10, 10);
            QCoreApplication::processEvents();
        }
        return control;
    }
    bool selectSample(int sample)
    {
        auto *table = page->findChild<QTableWidget *>(QStringLiteral("tableResultReportOne"));
        if (!table)
            return false;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            if (table->item(row, 0)->text().toInt() != sample)
                continue;
            const QModelIndex index = table->model()->index(row, 1);
            table->scrollTo(index);
            const QPoint position = table->viewport()->mapTo(page, table->visualRect(index).center());
            host.ensureVisible(position.x(), position.y(), 20, 20);
            QCoreApplication::processEvents();
            QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(index).center());
            return table->currentRow() == row;
        }
        return false;
    }
    bool queryFixtureDay()
    {
        bool seen = false;
        bool clicked = false;
        QTimer action;
        action.setInterval(5);
        QObject::connect(&action, &QTimer::timeout, page, [&]()
        {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog || (dialog->objectName() != QStringLiteral("MaskDlgQueryReport")
                && dialog->objectName() != QStringLiteral("historyQueryDialog")))
                return;
            action.stop();
            seen = true;
            const auto date = [dialog](const char *buttonName, const char *editName)
            {
                auto *button = dialog->findChild<QPushButton *>(QString::fromLatin1(buttonName));
                auto *edit = dialog->findChild<QLineEdit *>(QString::fromLatin1(editName));
                if (!button || !edit)
                    return false;
                if (edit->text() == QStringLiteral("2026/10/23"))
                    return true;
                QTest::mouseClick(button, Qt::LeftButton);
                QCalendarWidget *calendar = nullptr;
                if (!waitUntil([&]()
                {
                    for (auto *candidate : dialog->findChildren<QCalendarWidget *>())
                        if (candidate->isVisible()) { calendar = candidate; return true; }
                    return false;
                })) return false;
                calendar->setCurrentPage(2026, 10);
                QCoreApplication::processEvents();
                auto *grid = calendar->findChild<QTableView *>(QStringLiteral("qt_calendar_calendarview"));
                if (!grid) return false;
                for (int row = 1; row < grid->model()->rowCount(); ++row)
                    for (int column = 0; column < grid->model()->columnCount(); ++column)
                    {
                        const auto index = grid->model()->index(row, column);
                        if (index.data().toString() == QStringLiteral("23") && (index.flags() & Qt::ItemIsSelectable))
                        {
                            QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier,
                                              grid->visualRect(index).center());
                            return waitUntil([&]() { return edit->text() == QStringLiteral("2026/10/23"); });
                        }
                    }
                return false;
            };
            clicked = date("btnCalendarStart", "ldtStartDate") && date("btnCalendarEnd", "ldtEndDate");
            if (clicked)
                QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("btnSave")), Qt::LeftButton);
            else dialog->reject();
        });
        QSignalSpy operations(page, &HistoryPage::operationFinished);
        action.start();
        auto *queryButton = button("btnQuery");
        if (!queryButton || !queryButton->isEnabled()) return false;
        QTest::mouseClick(queryButton, Qt::LeftButton);
        const bool completed = waitUntil([&]()
        {
            for (const auto &operation : operations)
                if (operation.at(0).toString() == QStringLiteral("query") && operation.at(1).toBool())
                    return !page->isBusy();
            return false;
        });
        return completed && seen && clicked;
    }
    QThread thread;
    HistoryStore *store = nullptr;
    HistoryPage *page = nullptr;
    QScrollArea host;
    QString status;
};
}

class HistoryReportTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void statisticsGolden();
    void statisticsBoundaries();
    void statisticsGrouping();
    void documentContract();
    void rendererOwnership();
    void statisticsUi();
    void previewAndPdf();
    void errorLifecycle();
private:
    QString directory_;
    QJsonArray statistics_;
};

void HistoryReportTest::initTestCase()
{
    qRegisterMetaType<QProcess::ProcessError>("QProcess::ProcessError");
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    directory_ = QString::fromUtf8(qgetenv("SN100_REPORT_EVIDENCE_DIR"));
    QVERIFY2(QDir::isAbsolutePath(directory_) && !directory_.isEmpty(),
        "SN100_REPORT_EVIDENCE_DIR must be a new absolute experiment directory");
    QVERIFY2(!QFileInfo::exists(directory_), "Refusing to overwrite previous report evidence");
    QVERIFY(QDir().mkpath(directory_));
    qInfo().noquote() << "REPORT_EVIDENCE_DIR" << directory_ << "PID" << QCoreApplication::applicationPid();
}

void HistoryReportTest::statisticsGolden()
{
    const HistoryReport input = report({"8", "10", "12"});
    const QJsonArray before = rawSnapshot(input);
    const auto groups = HistoryResultStatistics::groups(input);
    QCOMPARE(groups.size(), 1);
    const auto group = groups.first();
    QCOMPARE(group.validCount, 3);
    QCOMPARE(group.invalidCount, 0);
    QVERIFY(group.hasAverageRow());
    QVERIFY(group.meanAvailable && group.sampleSdAvailable && group.cvAvailable);
    QVERIFY(closeEnough(group.mean, 10));
    QVERIFY(closeEnough(group.sampleSd, 2));
    QVERIFY(closeEnough(group.cvPercent, 20));
    QCOMPARE(group.meanText, QStringLiteral("10.00"));
    QCOMPARE(group.cvText, QStringLiteral("20.00%"));
    QCOMPARE(rawSnapshot(input), before);
    statistics_.append(QJsonObject{{"input", before}, {"actual", statisticJson(group)}});

    const auto precision = HistoryResultStatistics::groups(report({"12.34", "13.46"})).first();
    QVERIFY(closeEnough(precision.mean, 12.9));
    QVERIFY(closeEnough(precision.sampleSd, 0.791959594928933));
    QVERIFY(closeEnough(precision.cvPercent, 6.13922166611576));
    QCOMPARE(precision.meanText, QStringLiteral("12.90"));
    QCOMPARE(precision.cvText, QStringLiteral("6.14%"));
    statistics_.append(QJsonObject{{"case", "raw precision before display rounding"}, {"actual", statisticJson(precision)}});

    const auto scaled = HistoryResultStatistics::groups(report({"0.008", "0.010", "0.012"}, 2, 1000)).first();
    QVERIFY(closeEnough(scaled.mean, 10));
    QVERIFY(closeEnough(scaled.sampleSd, 2));
    QCOMPARE(scaled.meanText, QStringLiteral("10.00"));
    QCOMPARE(scaled.cvText, QStringLiteral("20.00%"));
    const auto unscaled = HistoryResultStatistics::groups(report({"8", "10", "12"}, 1, 1000)).first();
    QVERIFY(closeEnough(unscaled.mean, 10));
    statistics_.append(QJsonObject{{"case", "calibration factor 1000"}, {"actual", statisticJson(scaled)}});
}

void HistoryReportTest::statisticsBoundaries()
{
    QVERIFY(HistoryResultStatistics::groups(HistoryReport()).isEmpty());
    const auto one = HistoryResultStatistics::groups(report({"10"})).first();
    QVERIFY(!one.hasAverageRow());
    QVERIFY(one.meanAvailable && !one.sampleSdAvailable && !one.cvAvailable);
    QVERIFY(one.cvText.isEmpty() && !one.cvUnavailableReason.isEmpty());

    const auto oneValid = HistoryResultStatistics::groups(report({"10", "INVALID", "nan", "inf"})).first();
    QVERIFY(oneValid.hasAverageRow());
    QCOMPARE(oneValid.validCount, 1);
    QCOMPARE(oneValid.invalidCount, 3);
    QCOMPARE(oneValid.meanText, QStringLiteral("10.00"));
    QVERIFY(!oneValid.sampleSdAvailable && !oneValid.cvAvailable && oneValid.cvText.isEmpty());

    const auto invalid = HistoryResultStatistics::groups(report({"****", "待确认"})).first();
    QVERIFY(invalid.hasAverageRow() && !invalid.meanAvailable && !invalid.cvAvailable);
    QVERIFY(invalid.meanText.isEmpty() && invalid.cvText.isEmpty());
    QVERIFY(!invalid.meanUnavailableReason.isEmpty() && !invalid.cvUnavailableReason.isEmpty());

    const auto zero = HistoryResultStatistics::groups(report({"-2", "0", "2"})).first();
    QVERIFY(zero.meanAvailable && zero.sampleSdAvailable && !zero.cvAvailable);
    QVERIFY(closeEnough(zero.mean, 0) && closeEnough(zero.sampleSd, 2));
    QVERIFY(zero.cvText.isEmpty() && !zero.cvUnavailableReason.isEmpty());

    const auto constant = HistoryResultStatistics::groups(report({"10", "10"})).first();
    QVERIFY(constant.cvAvailable && closeEnough(constant.sampleSd, 0));
    QCOMPARE(constant.cvText, QStringLiteral("0"));
    const auto negative = HistoryResultStatistics::groups(report({"-12", "-10", "-8"})).first();
    QVERIFY(closeEnough(negative.cvPercent, -20));
    QCOMPARE(negative.cvText, QStringLiteral("-20.00%"));

    const auto tiny = HistoryResultStatistics::groups(report({"9.999999", "10.000001"})).first();
    QVERIFY(tiny.cvAvailable && tiny.cvPercent > 0 && tiny.cvPercent < 0.0001);
    QCOMPARE(tiny.cvText, QStringLiteral("0"));

    HistoryReport unknown = report({"10", "20"});
    for (auto &task : unknown.results)
        task.values[0].decimals = -1;
    const auto unknownPrecision = HistoryResultStatistics::groups(unknown).first();
    QVERIFY(unknownPrecision.meanAvailable && closeEnough(unknownPrecision.mean, 15));
    QVERIFY(unknownPrecision.meanText.isEmpty() && !unknownPrecision.meanUnavailableReason.isEmpty());

    const auto overflow = HistoryResultStatistics::groups(report({"-1e308", "1e308"})).first();
    QVERIFY(!overflow.cvAvailable);
    QVERIFY(overflow.cvText.isEmpty() && !overflow.cvUnavailableReason.isEmpty());
    for (const auto &group : {one, oneValid, invalid, zero, constant, negative, tiny, unknownPrecision, overflow})
        statistics_.append(QJsonObject{{"case", "boundary"}, {"actual", statisticJson(group)}});
}

void HistoryReportTest::statisticsGrouping()
{
    HistoryReport input = report({"8", "10", "12"});
    input.results[1].abnormalType = QStringLiteral("1");
    for (int index = 0; index < input.results.size(); ++index)
        input.results[index].values.append(value(QString::number(index + 1), 4, QStringLiteral("INR"), QString(), 2));
    HistoryResult aptt;
    aptt.taskId = 7100;
    aptt.project = QStringLiteral("APTT");
    aptt.values.append(value(QStringLiteral("100"), 1, QStringLiteral("APTT"), QStringLiteral("s"), 2));
    input.results.append(aptt);
    const auto before = rawSnapshot(input);
    const auto groups = HistoryResultStatistics::groups(input);
    QCOMPARE(groups.size(), 3);
    QCOMPARE(groups.at(0).project, QStringLiteral("PT"));
    QCOMPARE(groups.at(0).resultType, 1);
    QCOMPARE(groups.at(1).project, QStringLiteral("PT"));
    QCOMPARE(groups.at(1).resultType, 4);
    QCOMPARE(groups.at(2).project, QStringLiteral("APTT"));
    QVERIFY(closeEnough(groups.at(0).mean, 10));
    QVERIFY(closeEnough(groups.at(1).mean, 2));
    QVERIFY(closeEnough(groups.at(1).sampleSd, 1));
    QCOMPARE(groups.at(0).entries.at(1).abnormalType, QStringLiteral("1"));
    QVERIFY(!groups.at(2).hasAverageRow());
    QCOMPARE(rawSnapshot(input), before);

    // A repeat task that lacks a previously seen result type is a missing
    // result, not a vanished repeat and not a numeric zero.
    HistoryReport missing = report({"10", "20"});
    missing.results[0].values.append(value(QStringLiteral("1.2"), 4, QStringLiteral("INR"), QString(), 2));
    const auto missingBefore = rawSnapshot(missing);
    const auto missingGroups = HistoryResultStatistics::groups(missing);
    QCOMPARE(missingGroups.size(), 2);
    const auto missingInr = missingGroups.at(1);
    QCOMPARE(missingInr.entries.size(), 2);
    QCOMPARE(missingInr.validCount, 1);
    QCOMPARE(missingInr.invalidCount, 1);
    QVERIFY(missingInr.hasAverageRow() && missingInr.meanAvailable && !missingInr.cvAvailable);
    QCOMPARE(missingInr.meanText, QStringLiteral("1.20"));
    QVERIFY(missingInr.entries.first().valuePresent && !missingInr.entries.last().valuePresent);
    QVERIFY(missingInr.entries.last().value.rawValue.isEmpty() && missingInr.cvText.isEmpty());
    QCOMPARE(rawSnapshot(missing), missingBefore);
    statistics_.append(QJsonObject{{"case", "repeat missing an existing result type"},
        {"input", missingBefore}, {"actual", statisticJson(missingInr)}});

    for (int field = 0; field < 4; ++field)
    {
        HistoryReport conflict = report({"8", "10"});
        auto &different = conflict.results[1].values[0];
        if (field == 0) different.unit = QStringLiteral("ms");
        if (field == 1) different.decimals = 3;
        if (field == 2) different.factor = 1000;
        if (field == 3) different.label = QStringLiteral("different");
        const QJsonArray beforeConflict = rawSnapshot(conflict);
        const auto group = HistoryResultStatistics::groups(conflict).first();
        QVERIFY(!group.configurationConsistent && !group.meanAvailable && !group.cvAvailable);
        QVERIFY(!group.meanUnavailableReason.isEmpty() && !group.cvUnavailableReason.isEmpty());
        QCOMPARE(group.entries.size(), 2);
        QCOMPARE(rawSnapshot(conflict), beforeConflict);
        statistics_.append(QJsonObject{{"case", QStringLiteral("configuration conflict %1").arg(field)}, {"actual", statisticJson(group)}});
    }
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("statistics.json")),
        {{"source", "hand-written anonymous goldens, sample SD and signed percent CV"}, {"cases", statistics_}}));
}

void HistoryReportTest::statisticsUi()
{
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"),
        "Report GUI verification requires actual WSLg XCB");
    const QString directory = QDir(directory_).filePath(QStringLiteral("statistics_ui_data"));
    Session session(directory);
    QVERIFY2(session.ready(), qPrintable(session.status));
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(session.queryFixtureDay());
    QVERIFY(session.selectSample(3));
    auto *table = session.page->findChild<QTableWidget *>(QStringLiteral("tableResultReport"));
    QVERIFY(table);
    QJsonObject before;
    QString error;
    const QString database = QDir(directory).filePath(QStringLiteral("history.sqlite"));
    QVERIFY2(databaseSnapshot(database, &before, &error), qPrintable(error));
    QCOMPARE(table->columnCount(), 5);
    QCOMPARE(table->rowCount(), 10);
    QStringList averages;
    QJsonArray visibleRows;
    for (int row = 0; row < table->rowCount(); ++row)
    {
        QJsonArray fields;
        for (int column = 0; column < table->columnCount(); ++column)
            fields.append(table->item(row, column) ? table->item(row, column)->text() : QString());
        visibleRows.append(fields);
        if (table->item(row, 0)->text() == QStringLiteral("平均值"))
        {
            QCOMPARE(table->item(row, 2)->text(), QStringLiteral("CV"));
            averages.append(table->item(row, 1)->text() + QStringLiteral("/") + table->item(row, 3)->text());
        }
    }
    QCOMPARE(averages, QStringList({QStringLiteral("12.9/6.14%"), QStringLiteral("1.17/7.53%")}));
    QVERIFY(session.page->grab().save(QDir(directory_).filePath(QStringLiteral("statistics_full_page.png"))));
    session.page->setAverageEnabled(false);
    QCOMPARE(table->rowCount(), 8);
    session.page->setAverageEnabled(true);
    QCOMPARE(table->rowCount(), 10);
    QVERIFY(session.selectSample(4));
    QCOMPARE(table->rowCount(), 2); // Invalid single tasks never get a summary row.
    QVERIFY(session.selectSample(3));
    QJsonObject after;
    QVERIFY2(databaseSnapshot(database, &after, &error), qPrintable(error));
    QCOMPARE(after, before);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("statistics_ui.json")),
        {{"visible_rows", visibleRows}, {"database_before", before}, {"database_after", after},
         {"source_results_unchanged", true}}));
}

void HistoryReportTest::documentContract()
{
    HistoryReport input = report({"7", "10", "13", "INVALID"});
    input.dateTime = QDateTime(QDate(2026, 10, 23), QTime(12, 34, 56));
    input.patient.name = QStringLiteral("匿名金样本");
    input.patient.sex = 1;
    input.patient.age = 30;
    input.patient.department = QStringLiteral("匿名科室");
    input.patient.patientType = QStringLiteral("匿名门诊");
    input.patient.bedNumber = QStringLiteral("测试床1");
    input.patient.sender = QStringLiteral("匿名送检");
    input.patient.examiner = QStringLiteral("匿名检验");
    input.patient.auditor = QStringLiteral("匿名审核");
    input.patient.remark = QStringLiteral("匿名备注");
    const auto before = rawSnapshot(input);
    HistoryPrintOptions options;
    options.hospitalName = QStringLiteral("匿名金样本医院");
    HistoryPrintColumn pt;
    pt.project = QStringLiteral("PT");
    pt.resultType = 1;
    pt.description = QStringLiteral("匿名PT项目");
    pt.hasReference = true;
    pt.lower = 8;
    pt.upper = 12;
    options.columns.append(pt);
    HistoryPrintDocument output;
    QString error;
    QVERIFY2(HistoryReportDocument::build(input, options, &output, &error), qPrintable(error));
    const QMap<QString, QString> expectedHeader = {{"hospital_name", QStringLiteral("匿名金样本医院")},
        {"name", QStringLiteral("匿名金样本")}, {"sex", QStringLiteral("女")}, {"age", "30"},
        {"bed_number", QStringLiteral("测试床1")}, {"department", QStringLiteral("匿名科室")},
        {"sample_provider", QStringLiteral("匿名送检")}, {"sample_sn", "700"},
        {"patient_type", QStringLiteral("匿名门诊")}, {"report_date", "2026-10-23 12:34:56"},
        {"mark", QStringLiteral("匿名备注")}, {"checker", QStringLiteral("匿名检验")},
        {"reviewer", QStringLiteral("匿名审核")}};
    QCOMPARE(output.headers, expectedHeader);
    QCOMPARE(output.rows.size(), 4);
    const QStringList expectedValues{QStringLiteral("7.00 ↓"), QStringLiteral("10.00"),
        QStringLiteral("13.00 ↑"), QStringLiteral("****")};
    for (int row = 0; row < output.rows.size(); ++row)
    {
        const QMap<QString, QString> expectedRow = {{"project", "PT"},
            {"project_name", QStringLiteral("匿名PT项目")}, {"project_result", expectedValues.at(row)},
            {"reference_range", "8.00-12.00"}, {"unit", "s"}};
        QCOMPARE(output.rows.at(row), expectedRow);
    }
    const QJsonObject serialized = HistoryReportDocument::toJson(output);
    HistoryPrintDocument reopened;
    QVERIFY2(HistoryReportDocument::fromJson(serialized, &reopened, &error), qPrintable(error));
    QCOMPARE(reopened.headers, expectedHeader);
    QCOMPARE(reopened.rows, output.rows);
    QCOMPARE(rawSnapshot(input), before);
    HistoryReport unknownSex = input;
    unknownSex.patient.sex = 99;
    QVERIFY2(HistoryReportDocument::build(unknownSex, options, &reopened, &error), qPrintable(error));
    QCOMPARE(reopened.headers.value(QStringLiteral("sex")), QStringLiteral("Unknown"));
    QJsonObject wrongVersion = serialized;
    wrongVersion.insert(QStringLiteral("schema"), QStringLiteral("unknown"));
    QVERIFY(!HistoryReportDocument::fromJson(wrongVersion, &reopened, &error));
    QVERIFY(!error.isEmpty());
    HistoryReport multiline = input;
    multiline.patient.name += QStringLiteral("\n不会静默裁切");
    QVERIFY(!HistoryReportDocument::build(multiline, options, &reopened, &error));
    QVERIFY(!error.isEmpty());
    HistoryPrintOptions badReference = options;
    badReference.columns[0].lower = 13;
    badReference.columns[0].upper = 8;
    QVERIFY(!HistoryReportDocument::build(input, badReference, &reopened, &error));
    HistoryPrintOptions nonMain = options;
    nonMain.columns[0].mainResult = false;
    QVERIFY(!HistoryReportDocument::build(input, nonMain, &reopened, &error));
    HistoryPrintOptions unconfigured = options;
    unconfigured.columns[0].project = QStringLiteral("APTT");
    QVERIFY(!HistoryReportDocument::build(input, unconfigured, &reopened, &error));
    QCOMPARE(rawSnapshot(input), before);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("document_contract.json")),
        {{"source", "hand-written anonymous headers, results and reference bounds; no clinical interval"},
         {"actual", serialized}, {"source_results_unchanged", true}}));
}

void HistoryReportTest::previewAndPdf()
{
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"),
        "Report GUI verification requires actual WSLg XCB");
    const QString directory = QDir(directory_).filePath(QStringLiteral("report_ui_data"));
    const QString renderer = QString::fromUtf8(qgetenv("SN100_REPORT_RENDERER"));
    QVERIFY2(QFileInfo(renderer).isAbsolute() && QFileInfo(renderer).isExecutable(),
        "SN100_REPORT_RENDERER must name the freshly built renderer executable");
    Session session(directory);
    QVERIFY2(session.ready(), qPrintable(session.status));
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(session.queryFixtureDay());
    QVERIFY(session.selectSample(3));
    HistoryReportController controller(&session.host);
    controller.setDataDirectory(directory);
    controller.setRendererPath(renderer);
    session.page->setReportPreviewEnabled(true);
    QObject::connect(session.page, &HistoryPage::reportPreviewRequested,
        &controller, &HistoryReportController::preview);
    QObject::connect(&controller, &HistoryReportController::busyChanged,
        session.page, &HistoryPage::setReportOperationBusy);
    QSignalSpy exported(&controller, &HistoryReportController::pdfExported);
    QSignalSpy failures(&controller, &HistoryReportController::failed);
    QSignalSpy closed(&controller, &HistoryReportController::previewClosed);
    QPointer<QPrintPreviewDialog> preview;
    QObject::connect(&controller, &HistoryReportController::previewReady, &controller,
        [&](QPrintPreviewDialog *dialog) { preview = dialog; });
    EventProbe probe;
    QPointer<QProcess> rendererProcess = controller.findChild<QProcess *>();
    probe.rendererPid = [rendererProcess]() { return rendererProcess ? rendererProcess->processId() : 0; };
    probe.sampleResources();
    const QString database = QDir(directory).filePath(QStringLiteral("history.sqlite"));
    QJsonObject before;
    QString error;
    QVERIFY2(databaseSnapshot(database, &before, &error), qPrintable(error));

    QSignalSpy previewRequests(session.page, &HistoryPage::reportPreviewRequested);
    QTest::mouseClick(session.button("btnDetail"), Qt::LeftButton);
    QPointer<QDialog> detail;
    QVERIFY(waitUntil([&]()
    {
        detail = session.page->findChild<QDialog *>(QStringLiteral("MaskDlgReportDetail"));
        return detail && detail->isVisible() && !session.page->isBusy();
    }));
    auto *info = detail->findChild<QPushButton *>(QStringLiteral("btnMenuInfo"));
    QVERIFY(info);
    reveal(info);
    QTest::mouseClick(info, Qt::LeftButton);
    auto *name = detail->findChild<QLineEdit *>(QStringLiteral("ldtName"));
    QVERIFY(name);
    reveal(name);
    QApplication::clipboard()->setText(QStringLiteral("未保存匿名"));
    QTest::mouseClick(name, Qt::LeftButton);
    QTest::keyClick(name, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(name, Qt::Key_V, Qt::ControlModifier);
    QVERIFY(session.page->hasUnsavedChanges());
    auto *detailPrint = detail->findChild<QPushButton *>(QStringLiteral("btnPrinter"));
    QVERIFY(detailPrint && detailPrint->isEnabled());
    reveal(detailPrint);
    QTest::mouseClick(detailPrint, Qt::LeftButton);
    QCOMPARE(previewRequests.size(), 0);
    QVERIFY(!preview && session.page->hasUnsavedChanges());
    QCOMPARE(name->text(), QStringLiteral("未保存匿名"));
    QTimer::singleShot(0, session.page, []()
    {
        auto *confirmation = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (confirmation && confirmation->button(QMessageBox::Discard))
            QTest::mouseClick(confirmation->button(QMessageBox::Discard), Qt::LeftButton);
    });
    QVERIFY(session.page->requestLeave(QStringLiteral("测试离页")));
    QVERIFY(!session.page->hasUnsavedChanges());
    QVERIFY(!detail || !detail->isVisible());
    QCOMPARE(previewRequests.size(), 0);

    // The unchanged anonymous fixture includes an intentionally multiline name
    // in report 3. The original fixed single-line template must reject it,
    // then allow an ordinary report without altering the stored patient.
    QTest::mouseClick(session.button("btnPrinter"), Qt::LeftButton);
    QVERIFY(waitUntil([&]() { return !failures.isEmpty(); }));
    QVERIFY(!preview);
    const QString multilineFailure = failures.last().last().toString();
    QVERIFY(!multilineFailure.isEmpty());
    failures.clear();
    QVERIFY(session.selectSample(2));

    QElapsedTimer elapsed;
    elapsed.start();
    auto *printButton = session.button("btnPrinter");
    QVERIFY(printButton && printButton->isVisible() && printButton->isEnabled());
    QTest::mouseClick(printButton, Qt::LeftButton);
    QVERIFY2(waitUntil([&]()
    {
        auto *widget = preview ? preview->findChild<QPrintPreviewWidget *>() : nullptr;
        return preview && preview->isVisible() && widget && widget->pageCount() > 0;
    }), qPrintable(session.status));
    QCOMPARE(preview->objectName(), QStringLiteral("historyPrintPreview"));
    QVERIFY(session.page->isBusy());
    QVERIFY(!session.page->requestLeave(QStringLiteral("预览期间离页")));
    auto *previewWidget = preview->findChild<QPrintPreviewWidget *>();
    const int initialPreviewPages = previewWidget->pageCount();
    probe.operations.append(QJsonObject{{"operation", "mouse print to populated preview"},
        {"elapsed_ms", double(elapsed.nsecsElapsed()) / 1000000.0}});
    QVERIFY(preview->grab().save(QDir(directory_).filePath(QStringLiteral("print_preview.png"))));
    QVERIFY(session.page->grab().save(QDir(directory_).filePath(QStringLiteral("report_full_page.png"))));

    bool physicalPrintDisabled = false;
    for (auto *action : preview->findChildren<QAction *>())
        if (action->toolTip() == QStringLiteral("实体打印待实物验证"))
        {
            QVERIFY(!action->isEnabled());
            physicalPrintDisabled = true;
        }
    QVERIFY(physicalPrintDisabled);
    QTest::keyClick(preview, Qt::Key_P, Qt::ControlModifier);
    QTest::qWait(50);
    for (auto *window : QApplication::topLevelWidgets())
        QVERIFY(!qobject_cast<QPrintDialog *>(window) || !window->isVisible());

    QToolButton *pdfButton = nullptr;
    for (auto *toolbar : preview->findChildren<QToolBar *>())
        for (auto *action : toolbar->actions())
            if (action->objectName() == QStringLiteral("actExpToPdf"))
                pdfButton = qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
    QVERIFY2(pdfButton && pdfButton->isVisible(), "The original Save as PDF toolbar button must be reachable");
    QTest::mouseClick(pdfButton, Qt::LeftButton);
    QPointer<QFileDialog> fileDialog;
    QVERIFY(waitUntil([&]()
    {
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == QStringLiteral("historyPdfFileDialog") && widget->isVisible())
                fileDialog = qobject_cast<QFileDialog *>(widget);
        return fileDialog != nullptr;
    }));
    // Cancel through the actual dialog; no export request or partial file.
    auto *fileButtons = fileDialog->findChild<QDialogButtonBox *>();
    QVERIFY(fileButtons && fileButtons->button(QDialogButtonBox::Cancel));
    QTest::mouseClick(fileButtons->button(QDialogButtonBox::Cancel), Qt::LeftButton);
    QVERIFY(waitUntil([&]() { return !fileDialog || !fileDialog->isVisible(); }));
    QCOMPARE(exported.size(), 0);
    QCOMPARE(failures.size(), 0);

    QTest::mouseClick(pdfButton, Qt::LeftButton);
    fileDialog = nullptr;
    QVERIFY(waitUntil([&]()
    {
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == QStringLiteral("historyPdfFileDialog") && widget->isVisible())
                fileDialog = qobject_cast<QFileDialog *>(widget);
        return fileDialog != nullptr;
    }));
    const QString initialPdf = QDir(directory).filePath(QStringLiteral("exports/from_original_preview.pdf"));
    auto *fileName = fileDialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
    QVERIFY(fileName);
    QApplication::clipboard()->setText(initialPdf);
    QTest::mouseClick(fileName, Qt::LeftButton);
    QTest::keyClick(fileName, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(fileName, Qt::Key_V, Qt::ControlModifier);
    QCOMPARE(fileName->text(), initialPdf);
    fileButtons = fileDialog->findChild<QDialogButtonBox *>();
    QVERIFY(fileButtons && fileButtons->button(QDialogButtonBox::Save));
    elapsed.restart();
    QTest::mouseClick(fileButtons->button(QDialogButtonBox::Save), Qt::LeftButton);
    QVERIFY(waitUntil([&]() { return !exported.isEmpty() || !failures.isEmpty(); }, 30000));
    QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.last().last().toString()));
    QCOMPARE(exported.last().at(0).toString(), initialPdf);
    probe.operations.append(QJsonObject{{"operation", "mouse save to actual PDF completion"},
        {"elapsed_ms", double(elapsed.nsecsElapsed()) / 1000000.0}});
    QFile generated(initialPdf);
    QVERIFY(generated.open(QIODevice::ReadOnly));
    const QByteArray initialBytes = generated.readAll();
    QVERIFY(initialBytes.startsWith("%PDF-") && initialBytes.size() > 1000);
    generated.close();
    QJsonObject pdf;
    QVERIFY2(inspectPdf(initialPdf, &pdf, &error), qPrintable(error));
    QCOMPARE(pdf.value(QStringLiteral("page_count")).toInt(), initialPreviewPages);
    const QString text = pdfText(pdf);
    for (const QString &token : {QStringLiteral("检验报告单"), QStringLiteral("项目代号"),
         QStringLiteral("项目名称"), QStringLiteral("参考范围"), QStringLiteral("单位"),
         QStringLiteral("APTT"), QStringLiteral("0.00"), QStringLiteral("匿名二号")})
        QVERIFY2(text.contains(token), qPrintable(QStringLiteral("Actual PDF missing: %1").arg(token)));
    QVERIFY(!text.contains(QStringLiteral("[hospital_name]")));
    QVERIFY(!text.contains(QStringLiteral("平均值")) && !text.contains(QStringLiteral("CV")));
    for (const auto &page : pdf.value(QStringLiteral("pages")).toArray())
    {
        const auto paper = page.toObject();
        QVERIFY(closeEnough(paper.value(QStringLiteral("width_pt")).toDouble(), 595.275590551, 1));
        QVERIFY(closeEnough(paper.value(QStringLiteral("height_pt")).toDouble(), 419.527559055, 1));
    }
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("preview_pdf.json")), pdf));
    QTest::keyClick(preview, Qt::Key_Escape);
    QVERIFY(waitUntil([&]() { return !preview && closed.size() == 1; }));

    // Repeated real button/close cycles exercise the heap preview and QtRPT
    // painter/printer ownership, including asynchronous deleteLater cleanup.
    for (int cycle = 0; cycle < 20; ++cycle)
    {
        elapsed.restart();
        QTest::mouseClick(session.button("btnPrinter"), Qt::LeftButton);
        QVERIFY(waitUntil([&]()
        {
            auto *widget = preview ? preview->findChild<QPrintPreviewWidget *>() : nullptr;
            return preview && preview->isVisible() && widget && widget->pageCount() > 0;
        }));
        probe.operations.append(QJsonObject{{"operation", "repeat preview"}, {"cycle", cycle},
            {"elapsed_ms", double(elapsed.nsecsElapsed()) / 1000000.0}});
        QTest::keyClick(preview, Qt::Key_Escape);
        QVERIFY(waitUntil([&]() { return !preview && closed.size() == cycle + 2; }));
    }

    QSignalSpy details(session.store, &HistoryStore::detailReady);
    QVERIFY(QMetaObject::invokeMethod(session.store, "detail", Qt::QueuedConnection,
        Q_ARG(qint64, 90001), Q_ARG(qint64, 3)));
    QVERIFY(waitUntil([&]() { return !details.isEmpty(); }));
    const HistoryReport original = qvariant_cast<HistoryReport>(details.first().at(1));
    const QJsonArray rawBefore = rawSnapshot(original);
    const HistoryReport printable = printableCopy(original);
    QVERIFY2(saveDocumentSnapshots(original, directory_, &error), qPrintable(error));
    const auto exportResult = [&](const HistoryReport &input, const QString &path, bool expectedSuccess)
    {
        const int done = exported.size();
        const int failed = failures.size();
        controller.exportPdf(input, path);
        if (!waitUntil([&]() { return exported.size() > done || failures.size() > failed; }, 30000))
            return false;
        return expectedSuccess ? exported.size() == done + 1 && failures.size() == failed
                               : failures.size() == failed + 1 && exported.size() == done;
    };
    const QString multilinePdf = QDir(directory).filePath(QStringLiteral("exports/multiline.pdf"));
    QVERIFY(exportResult(original, multilinePdf, false));
    QVERIFY(!QFileInfo::exists(multilinePdf));
    HistoryReport expression = printable;
    expression.patient.name = QStringLiteral("匿名[2+2]");
    const QString expressionPdf = QDir(directory).filePath(QStringLiteral("exports/expression.pdf"));
    QVERIFY(exportResult(expression, expressionPdf, false));
    QVERIFY(!QFileInfo::exists(expressionPdf));
    expression = printable;
    expression.results[0].values[0].unit = QStringLiteral("s<2+2>");
    const QString unitExpressionPdf = QDir(directory).filePath(QStringLiteral("exports/unit_expression.pdf"));
    QVERIFY(exportResult(expression, unitExpressionPdf, false));
    QVERIFY(!QFileInfo::exists(unitExpressionPdf));
    expression = printable;
    const uint unavailableCodepoint[] = {0x10ffff};
    expression.patient.name = QString::fromUcs4(unavailableCodepoint, 1);
    const QString missingGlyphPdf = QDir(directory).filePath(QStringLiteral("exports/missing_glyph.pdf"));
    QVERIFY(exportResult(expression, missingGlyphPdf, false));
    QVERIFY(!QFileInfo::exists(missingGlyphPdf));
    QVERIFY(exportResult(printable, initialPdf, false));
    QFile unchanged(initialPdf);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), initialBytes);
    unchanged.close();
    const QString outside = QDir(directory_).filePath(QStringLiteral("outside.pdf"));
    QVERIFY(exportResult(printable, outside, false));
    QVERIFY(!QFileInfo::exists(outside));
    QVERIFY(exportResult(printable, QStringLiteral("relative.pdf"), false));
    const QString empty = QDir(directory).filePath(QStringLiteral("exports/empty.pdf"));
    QVERIFY(exportResult(HistoryReport(), empty, false));
    QVERIFY(!QFileInfo::exists(empty));
    controller.setRendererPath(QDir(directory_).filePath(QStringLiteral("missing-renderer")));
    const QString missing = QDir(directory).filePath(QStringLiteral("exports/missing_renderer.pdf"));
    QVERIFY(exportResult(printable, missing, false));
    QVERIFY(!QFileInfo::exists(missing));
    QCOMPARE(QDir(QDir(directory).filePath(QStringLiteral("exports"))).entryList(
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot), QStringList({QStringLiteral("from_original_preview.pdf")}));
    controller.setRendererPath(renderer);
    const QString recovery = QDir(directory).filePath(QStringLiteral("exports/recovered.pdf"));
    QVERIFY(exportResult(printable, recovery, true));
    QCOMPARE(exported.last().at(1).toInt(), 7);
    QJsonObject recoveredPdf;
    QVERIFY2(inspectPdf(recovery, &recoveredPdf, &error), qPrintable(error));
    const QString recoveredText = pdfText(recoveredPdf);
    for (const QString &token : {QStringLiteral("PT"), QStringLiteral("INR"), QStringLiteral("12.3"),
         QStringLiteral("13.5"), QStringLiteral("1.24"), QStringLiteral("1.11"),
         QStringLiteral("35.68"), QStringLiteral("2.35"), QStringLiteral("123.456"),
         QStringLiteral("匿名三号")})
        QVERIFY2(recoveredText.contains(token), qPrintable(QStringLiteral("Recovered PDF missing: %1").arg(token)));
    QVERIFY(!recoveredText.contains(QStringLiteral("RAWX")));
    QVERIFY(recoveredText.contains(QStringLiteral("Unknown")));
    QVERIFY(!recoveredText.contains(QStringLiteral("平均值")) && !recoveredText.contains(QStringLiteral("CV")));
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("recovered_pdf.json")), recoveredPdf));
    QCOMPARE(rawSnapshot(original), rawBefore);

    const HistoryReport many = sixtyFourTasks(original);
    const QString paginated = QDir(directory).filePath(QStringLiteral("exports/64_original_rows.pdf"));
    elapsed.restart();
    const int beforeLongExport = exported.size();
    const int beforeDuplicateFailure = failures.size();
    controller.exportPdf(many, paginated);
    QVERIFY(session.page->isBusy());
    QVERIFY(!session.page->requestLeave(QStringLiteral("PDF期间离页")));
    const QString duplicate = QDir(directory).filePath(QStringLiteral("exports/duplicate_in_flight.pdf"));
    controller.exportPdf(many, duplicate);
    QCOMPARE(failures.size(), beforeDuplicateFailure + 1);
    QVERIFY(waitUntil([&]() { return exported.size() > beforeLongExport; }, 30000));
    QCOMPARE(exported.size(), beforeLongExport + 1);
    QCOMPARE(failures.size(), beforeDuplicateFailure + 1);
    QVERIFY(!QFileInfo::exists(duplicate));
    probe.operations.append(QJsonObject{{"operation", "64 original task rows PDF completion"},
        {"elapsed_ms", double(elapsed.nsecsElapsed()) / 1000000.0}});
    QJsonObject manyPdf;
    QVERIFY2(inspectPdf(paginated, &manyPdf, &error), qPrintable(error));
    QVERIFY(manyPdf.value(QStringLiteral("page_count")).toInt() > 1);
    const QString manyText = pdfText(manyPdf);
    for (int row = 0; row < 64; ++row)
    {
        const QString number = QString::number(8000 + row) + QStringLiteral(".1");
        QCOMPARE(manyText.count(number), 1);
    }
    QVERIFY(!manyText.contains(QStringLiteral("平均值")) && !manyText.contains(QStringLiteral("CV")));
    QCOMPARE(exported.last().at(1).toInt(), 64);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("pagination_pdf.json")), manyPdf));
    QJsonObject after;
    QVERIFY2(databaseSnapshot(database, &after, &error), qPrintable(error));
    QCOMPARE(after, before);
    probe.sampleResources();
    QJsonArray failedOperations;
    for (const auto &failure : failures)
        failedOperations.append(QJsonObject{{"operation", failure.at(0).toString()}, {"message", failure.at(1).toString()}});
    auto measurements = probe.result();
    measurements.insert(QStringLiteral("expected_failures"), failedOperations);
    measurements.insert(QStringLiteral("database_before"), before);
    measurements.insert(QStringLiteral("database_after"), after);
    measurements.insert(QStringLiteral("preview_cycles"), 21);
    measurements.insert(QStringLiteral("source_results_unchanged"), true);
    measurements.insert(QStringLiteral("multiline_original_preview_failure"), multilineFailure);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("report_measurements.json")), measurements));
}

void HistoryReportTest::errorLifecycle()
{
#ifndef Q_OS_LINUX
    QSKIP("Renderer executable-format/exec-sleep lifecycle checks use Linux process semantics");
#else
    const QString directory = QDir(directory_).filePath(QStringLiteral("lifecycle_data"));
    const QString renderer = QString::fromUtf8(qgetenv("SN100_REPORT_RENDERER"));
    QVERIFY(QFileInfo(renderer).isExecutable());
    Session session(directory);
    QVERIFY2(session.ready(), qPrintable(session.status));
    QSignalSpy details(session.store, &HistoryStore::detailReady);
    QVERIFY(QMetaObject::invokeMethod(session.store, "detail", Qt::QueuedConnection,
        Q_ARG(qint64, 91001), Q_ARG(qint64, 3)));
    QVERIFY(waitUntil([&]() { return !details.isEmpty(); }));
    const HistoryReport original = qvariant_cast<HistoryReport>(details.last().at(1));
    QString error;
    const QString snapshotDirectory = QDir(directory_).filePath(QStringLiteral("lifecycle_snapshots"));
    QVERIFY(QDir().mkpath(snapshotDirectory));
    QVERIFY2(saveDocumentSnapshots(original, snapshotDirectory, &error), qPrintable(error));
    const HistoryReport input = printableCopy(original);
    HistoryReportController controller(&session.host);
    controller.setDataDirectory(directory);
    QObject::connect(&controller, &HistoryReportController::busyChanged,
        session.page, &HistoryPage::setReportOperationBusy);
    QSignalSpy errors(&controller, &HistoryReportController::failed);
    QSignalSpy exported(&controller, &HistoryReportController::pdfExported);
    QProcess *process = controller.findChild<QProcess *>();
    QVERIFY(process);
    QSignalSpy processErrors(process, &QProcess::errorOccurred);
    EventProbe probe;
    probe.rendererPid = [process]() { return process->processId(); };
    probe.sampleResources();
    QJsonObject before;
    const QString database = QDir(directory).filePath(QStringLiteral("history.sqlite"));
    QVERIFY2(databaseSnapshot(database, &before, &error), qPrintable(error));
    const auto executable = [&](const QString &name, const QByteArray &source)
    {
        const QString path = QDir(directory_).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(source) != source.size())
            return QString();
        file.close();
        return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner) ? path : QString();
    };
    const QString invalidExecutable = executable(QStringLiteral("invalid_renderer"),
        QByteArrayLiteral("This file intentionally has no executable format.\n"));
    QVERIFY(!invalidExecutable.isEmpty());
    controller.setRendererPath(invalidExecutable);
    const QString formatFailure = QDir(directory).filePath(QStringLiteral("exports/format_error.pdf"));
    controller.exportPdf(input, formatFailure);
    QVERIFY(waitUntil([&]() { return !errors.isEmpty(); }));
    QVERIFY(!session.page->isBusy() && !QFileInfo::exists(formatFailure));
    bool failedToStart = false;
    for (const auto &failure : processErrors)
        failedToStart = failedToStart || failure.first().value<QProcess::ProcessError>() == QProcess::FailedToStart;
    QVERIFY2(failedToStart, "Executable-format failure must reach the actual QProcess FailedToStart path");
    const QString sleeper = executable(QStringLiteral("slow_renderer"),
        QByteArrayLiteral("#!/bin/sh\nexec /bin/sleep 60\n"));
    QVERIFY(!sleeper.isEmpty());
    controller.setRendererPath(sleeper);
    const QString timeoutPdf = QDir(directory).filePath(QStringLiteral("exports/timeout.pdf"));
    const int beforeTimeoutErrors = errors.size();
    QElapsedTimer elapsed;
    elapsed.start();
    controller.exportPdf(input, timeoutPdf);
    QVERIFY(session.page->isBusy());
    QVERIFY(waitUntil([&]() { return errors.size() > beforeTimeoutErrors; }, 40000));
    const double timeoutMs = double(elapsed.nsecsElapsed()) / 1000000.0;
    QVERIFY(timeoutMs >= 28000 && timeoutMs < 36000);
    QVERIFY(errors.last().last().toString().contains(QStringLiteral("30秒")));
    QVERIFY(process->state() == QProcess::NotRunning && !session.page->isBusy());
    QVERIFY(!QFileInfo::exists(timeoutPdf));
    QVERIFY(QDir(QDir(directory).filePath(QStringLiteral("exports"))).entryList(
        QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    probe.operations.append(QJsonObject{{"operation", "30 second timeout to recovered idle"}, {"elapsed_ms", timeoutMs}});

    controller.setRendererPath(renderer);
    QPointer<QPrintPreviewDialog> preview;
    QObject::connect(&controller, &HistoryReportController::previewReady, &controller,
        [&](QPrintPreviewDialog *dialog) { preview = dialog; });
    controller.preview(input);
    QVERIFY(waitUntil([&]()
    {
        auto *view = preview ? preview->findChild<QPrintPreviewWidget *>() : nullptr;
        return preview && preview->isVisible() && view && view->pageCount() > 0;
    }));
    const QString recovery = QDir(directory).filePath(QStringLiteral("exports/closed_preview_recovery.pdf"));
    const int beforeRecovery = exported.size();
    const int beforeRecoveryErrors = errors.size();
    controller.exportPdf(input, recovery);
    QVERIFY(session.page->isBusy() && process->state() != QProcess::NotRunning);
    QTest::keyClick(preview, Qt::Key_Escape);
    QVERIFY(waitUntil([&]() { return !preview; }));
    if (exported.size() == beforeRecovery)
        QVERIFY(session.page->isBusy());
    QVERIFY(waitUntil([&]() { return exported.size() > beforeRecovery || errors.size() > beforeRecoveryErrors; }, 30000));
    QCOMPARE(exported.size(), beforeRecovery + 1);
    QCOMPARE(errors.size(), beforeRecoveryErrors);
    QVERIFY(!session.page->isBusy() && QFileInfo::exists(recovery));
    QJsonObject recoveredPdf;
    QVERIFY2(inspectPdf(recovery, &recoveredPdf, &error), qPrintable(error));
    QVERIFY(pdfText(recoveredPdf).contains(QStringLiteral("匿名三号")));
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("lifecycle_recovered_pdf.json")), recoveredPdf));
    QJsonObject after;
    QVERIFY2(databaseSnapshot(database, &after, &error), qPrintable(error));
    QCOMPARE(after, before);
    probe.sampleResources();
    auto measurements = probe.result();
    QJsonArray actualErrors;
    for (const auto &failure : errors)
        actualErrors.append(QJsonObject{{"operation", failure.first().toString()}, {"message", failure.last().toString()}});
    measurements.insert(QStringLiteral("actual_errors"), actualErrors);
    measurements.insert(QStringLiteral("database_before"), before);
    measurements.insert(QStringLiteral("database_after"), after);
    measurements.insert(QStringLiteral("closed_preview_export_completed"), true);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("error_lifecycle.json")), measurements));
#endif
}

void HistoryReportTest::rendererOwnership()
{
#ifdef Q_OS_WIN
    // This ownership check does not require a physical printer. Isolate the
    // Windows print plugin so its default-driver discovery cannot block it;
    // restore every application library path after both borrowed objects die.
    struct ScopedPrinterPluginIsolation
    {
        ScopedPrinterPluginIsolation() : savedPaths(QCoreApplication::libraryPaths())
        {
            QCoreApplication::setLibraryPaths(QStringList());
        }
        ~ScopedPrinterPluginIsolation()
        {
            QCoreApplication::setLibraryPaths(savedPaths);
        }
        QStringList savedPaths;
    } printerPluginIsolation;
#endif
    QPrinter borrowedPrinter(QPrinter::HighResolution);
    QPainter borrowedPainter;
    {
        QtRPT engine;
        QVERIFY(engine.setPrinter(&borrowedPrinter));
        QVERIFY(engine.setPainter(&borrowedPainter));
        engine.setOfflinePreview(true);
        QVERIFY(engine.loadReport(QStringLiteral(":/history_report/report.xml")));
    }
    // Both stack objects must remain alive and usable after the engine dies.
    borrowedPrinter.setOutputFileName(QStringLiteral("borrowed-device-survived.pdf"));
    QCOMPARE(borrowedPrinter.outputFileName(), QStringLiteral("borrowed-device-survived.pdf"));
    QVERIFY(!borrowedPainter.isActive());
    QtRPT engine;
    engine.setOfflinePreview(true);
    QVERIFY(engine.loadReport(QStringLiteral(":/history_report/report.xml")));
    QSignalSpy errors(&engine, &QtRPT::renderError);
    engine.printPDF(QDir(directory_).filePath(QStringLiteral("uncreated_parent/invalid.pdf")), false);
    QVERIFY(!errors.isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(directory_).filePath(QStringLiteral("uncreated_parent/invalid.pdf"))));
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("renderer_ownership.json")),
        {{"borrowed_printer_survived", true}, {"borrowed_painter_survived", true},
         {"invalid_pdf_error", errors.last().first().toString()}}));
}

QTEST_MAIN(HistoryReportTest)
#include "history_report_test.moc"
