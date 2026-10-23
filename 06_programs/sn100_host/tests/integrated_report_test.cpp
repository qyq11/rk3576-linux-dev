// O03-4: real main-window report buttons, committed hospital data and lifecycle.
// All databases and outputs are anonymous and remain in a new explicit directory.
#include <QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPrintPreviewDialog>
#include <QPrintPreviewWidget>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTableView>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <functional>
#include <algorithm>
#include <cmath>
#include <memory>
#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#include "data/account_store.h"
#include "data/hospital_store.h"
#include "history/history_page.h"
#include "history/report_controller.h"
#include "shell/frmmain.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

namespace
{
const QString kHospital = QStringLiteral("整合匿名医院 O'Brien");
const QString kDraftHospital = QStringLiteral("未保存医院草稿不得进入报告");
const QString kDraftPatient = QStringLiteral("未保存患者草稿");

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

template<class T> T *control(QObject *parent, const char *name)
{
    return parent ? parent->findChild<T *>(QString::fromLatin1(name)) : nullptr;
}

void reveal(QWidget *widget)
{
    QCoreApplication::processEvents();
    for (QWidget *parent = widget ? widget->parentWidget() : nullptr; parent; parent = parent->parentWidget())
    {
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) scroll->ensureWidgetVisible(widget, 10, 10);
        if (parent->isWindow()) break;
    }
    QCoreApplication::processEvents();
}

bool click(QAbstractButton *button)
{
    if (!button || !button->isEnabled()) return false;
    reveal(button);
    if (!button->isVisible()) return false;
    QTest::mouseClick(button, Qt::LeftButton);
    return true;
}

bool paste(QLineEdit *edit, const QString &text)
{
    if (!edit || !edit->isEnabled() || edit->isReadOnly()) return false;
    reveal(edit);
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    QApplication::clipboard()->setText(text);
    QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
    return edit->text() == text;
}

bool finished(const QSignalSpy &spy, const QString &operation, bool success = true)
{
    for (const auto &event : spy)
        if (event.at(0).toString() == operation && event.at(1).toBool() == success) return true;
    return false;
}

// Owned dialogs only. A failed lookup cancels that dialog and fails the test,
// instead of leaving a nested modal event loop waiting forever.
std::shared_ptr<bool> answerNext(QWidget *owner, QMessageBox::StandardButton answer)
{
    const auto seen = std::make_shared<bool>(false);
    auto *timer = new QTimer(owner);
    auto deadline = std::make_shared<QElapsedTimer>();
    deadline->start();
    timer->setInterval(5);
    QObject::connect(timer, &QTimer::timeout, owner, [=]()
    {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        bool owned = false;
        for (QObject *parent = dialog; parent; parent = parent->parent())
            if (parent == owner) { owned = true; break; }
        if (owned && dialog->button(answer))
        {
            *seen = true;
            timer->stop();
            QTest::mouseClick(dialog->button(answer), Qt::LeftButton);
            timer->deleteLater();
        }
        else if (deadline->elapsed() > 10000)
        {
            timer->stop();
            QTest::qFail("Owned confirmation was not shown", __FILE__, __LINE__);
            if (owned) dialog->QDialog::reject();
            timer->deleteLater();
        }
    });
    timer->start();
    return seen;
}

QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool saveJson(const QString &path, const QJsonObject &object)
{
    QFile file(path);
    const QByteArray content = QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(content) == content.size();
}

bool inspectPdf(const QString &path, QJsonObject *result, QString *error)
{
    const QString python = QString::fromUtf8(qgetenv("SN100_REPORT_PDF_PYTHON"));
    if (python.isEmpty()) { *error = QStringLiteral("Set SN100_REPORT_PDF_PYTHON to an independent pypdf runtime"); return false; }
    QString input = path;
#ifdef Q_OS_LINUX
    if (python.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) && input.startsWith(QStringLiteral("/mnt/")) && input.size() > 7)
        input = input.mid(5, 1).toUpper() + QStringLiteral(":/") + input.mid(7);
#endif
    const QString script = QStringLiteral("import json,sys\nfrom pypdf import PdfReader\nr=PdfReader(sys.argv[1])\n"
        "print(json.dumps({'page_count':len(r.pages),'pages':[{'text':p.extract_text()} for p in r.pages]},ensure_ascii=True))");
    QProcess parser;
    parser.start(python, {QStringLiteral("-c"), script, input});
    if (!waitUntil([&]() { return parser.state() == QProcess::NotRunning; }, 20000)
        || parser.exitStatus() != QProcess::NormalExit || parser.exitCode() != 0)
    {
        *error = parser.errorString() + QString::fromUtf8(parser.readAllStandardError());
        parser.kill();
        return false;
    }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(parser.readAllStandardOutput(), &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) { *error = parse.errorString(); return false; }
    *result = document.object();
    return true;
}

QString pdfText(const QJsonObject &document)
{
    QString text;
    for (const auto &page : document.value(QStringLiteral("pages")).toArray()) text += page.toObject().value(QStringLiteral("text")).toString();
    return text;
}

bool selectDate(QDialog *dialog, const char *buttonName, const char *editName)
{
    auto *edit = control<QLineEdit>(dialog, editName);
    if (!edit) return false;
    if (edit->text() == QStringLiteral("2026/10/23")) return true;
    if (!click(control<QAbstractButton>(dialog, buttonName))) return false;
    QCalendarWidget *calendar = nullptr;
    if (!waitUntil([&]()
    {
        for (auto *candidate : dialog->findChildren<QCalendarWidget *>())
            if (candidate->isVisible()) { calendar = candidate; return true; }
        return false;
    })) return false;
    calendar->setCurrentPage(2026, 10);
    QCoreApplication::processEvents();
    auto *grid = control<QTableView>(calendar, "qt_calendar_calendarview");
    if (!grid) return false;
    for (int row = 1; row < grid->model()->rowCount(); ++row)
        for (int column = 0; column < grid->model()->columnCount(); ++column)
        {
            const auto index = grid->model()->index(row, column);
            if (index.data().toString() == QStringLiteral("23") && (index.flags() & Qt::ItemIsSelectable))
            {
                QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, grid->visualRect(index).center());
                return waitUntil([&]() { return edit->text() == QStringLiteral("2026/10/23"); });
            }
        }
    return false;
}

class Session
{
public:
    explicit Session(const QString &directory, bool reports = true, bool seed = true)
        : directory(directory), hospital(new HospitalStore), accounts(new AccountStore), history(new HistoryStore)
    {
        HospitalStore::registerTypes(); AccountStore::registerTypes(); HistoryStore::registerTypes();
        for (const auto &worker : workers())
        {
            worker.first->moveToThread(worker.second);
            QObject::connect(worker.second, &QThread::finished, worker.first, &QObject::deleteLater);
        }
        window = new frmMain(hospital, nullptr, accounts);
        window->setDataDirectory(QDir(directory).filePath(QStringLiteral("hospital")));
        beforeAttachRejected = !window->enableHistoryReports(QDir(directory).filePath(QStringLiteral("history")));
        page = new HistoryPage(history);
        page->setDataDirectory(historyDirectory());
        window->attachHistoryPage(page);
        reportEnabled = reports && window->enableHistoryReports(historyDirectory());
        host.setWidget(window);
        host.setWidgetResizable(false);
        host.resize(1600, 900);
        operations.reset(new QSignalSpy(page, &HistoryPage::operationFinished));
        hospitalOperations.reset(new QSignalSpy(window->hospitalPage(), &frmHospInfoSetting::operationFinished));
        const QString secret = seed ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString();
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        {
            const QDir folder(QDir(directory).filePath(QStringLiteral("hospital")));
            hospital->initialize(folder.filePath(QStringLiteral("hospital_Information.db")), folder.filePath(QStringLiteral("config.ini")));
        });
        QObject::connect(&accountThread, &QThread::started, accounts, [=]()
        { accounts->initialize(QDir(directory).filePath(QStringLiteral("account")), secret); });
        QObject::connect(&historyThread, &QThread::started, history, [=]() { history->initialize(historyDirectory()); });
        host.show();
        hospitalThread.start(); accountThread.start(); historyThread.start();
    }
    ~Session()
    {
        host.takeWidget();
        delete window;
        stopWorkers();
    }
    QList<QPair<QObject *, QThread *>> workers()
    { return {{hospital, &hospitalThread}, {accounts, &accountThread}, {history, &historyThread}}; }
    bool stopWorkers()
    {
        bool stopped = true;
        for (const auto &worker : workers())
            if (worker.second->isRunning())
            {
                QMetaObject::invokeMethod(worker.first, "close", Qt::BlockingQueuedConnection);
                worker.second->quit();
                stopped = worker.second->wait(5000) && stopped;
            }
        return stopped;
    }
    bool ready()
    {
        return waitUntil([&]() { return window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
            && window->accountPage()->isReady() && !window->accountPage()->isBusy() && page->isReady() && !page->isBusy(); });
    }
    bool navigate(int main, int user = -1)
    {
        const char *names[] = {"btnReagentAdptive", "btnSampleAdptive", "btnReportAdptive", "btnCalibrateAdptive",
            "btnQualityAdptive", "btnSystemAdptive", "btnUserSettingAdptive", "btnDivStatusAdptive"};
        if (main < 0 || main > 7 || !click(control<QAbstractButton>(window, names[main])) || window->currentMainPage() != main) return false;
        if (main == 6 && user >= 0)
            return click(control<QAbstractButton>(window, user == 0 ? "btnAccount" : "btnHospInfo")) && window->currentUserPage() == user;
        return true;
    }
    bool query()
    {
        operations->clear();
        if (!click(control<QAbstractButton>(page, "btnQuery"))) return false;
        auto *dialog = control<QDialog>(page, "MaskDlgQueryReport");
        if (!dialog || !waitUntil([&]() { return dialog->isVisible(); })) return false;
        if (!selectDate(dialog, "btnCalendarStart", "ldtStartDate") || !selectDate(dialog, "btnCalendarEnd", "ldtEndDate")) return false;
        if (!click(control<QAbstractButton>(dialog, "btnSave"))) return false;
        return waitUntil([&]() { return !page->isBusy() && finished(*operations, QStringLiteral("query")); });
    }
    QTableWidget *table() { return control<QTableWidget>(page, "tableResultReportOne"); }
    bool selectSample(int sample)
    {
        auto *reports = table();
        if (!reports) return false;
        for (int row = 0; row < reports->rowCount(); ++row)
            if (reports->item(row, 0)->text().toInt() == sample)
            {
                const auto index = reports->model()->index(row, 1);
                reports->scrollTo(index); reveal(reports);
                QTest::mouseClick(reports->viewport(), Qt::LeftButton, Qt::NoModifier, reports->visualRect(index).center());
                return reports->currentRow() == row;
            }
        return false;
    }
    bool saveHospital(const QString &name)
    {
        if (!navigate(6, 1) || !paste(control<QLineEdit>(window->hospitalPage(), "ldtHospName"), name)) return false;
        hospitalOperations->clear();
        if (!click(control<QAbstractButton>(window->hospitalPage(), "btnSave"))) return false;
        return waitUntil([&]() { return !window->hospitalPage()->isBusy() && finished(*hospitalOperations, QStringLiteral("save")); });
    }
    bool login()
    {
        if (!QMetaObject::invokeMethod(accounts, "setSession", Qt::QueuedConnection,
            Q_ARG(QString, QStringLiteral("offlineEngineer")), Q_ARG(bool, true))) return false;
        return waitUntil([&]() { return window->accountSession().authenticated && window->accountSession().role == 4
            && !window->accountPage()->isBusy(); });
    }
    QString historyDirectory() const { return QDir(directory).filePath(QStringLiteral("history")); }
    QString pdfPath(const QString &name) const { return QDir(historyDirectory()).filePath(QStringLiteral("exports/") + name); }
    QString directory;
    QThread hospitalThread, accountThread, historyThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    HistoryStore *history;
    frmMain *window;
    HistoryPage *page;
    QScrollArea host;
    std::unique_ptr<QSignalSpy> operations, hospitalOperations;
    bool beforeAttachRejected = false;
    bool reportEnabled = false;
};

QToolButton *pdfButton(QPrintPreviewDialog *preview)
{
    if (!preview) return nullptr;
    for (auto *toolbar : preview->findChildren<QToolBar *>())
        for (auto *action : toolbar->actions())
            if (action->objectName() == QStringLiteral("actExpToPdf"))
                return qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
    return nullptr;
}

QFileDialog *pdfChooser()
{
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == QStringLiteral("historyPdfFileDialog") && widget->isVisible()) return qobject_cast<QFileDialog *>(widget);
    return nullptr;
}

bool choosePdfPath(const QString &path)
{
    auto *dialog = pdfChooser();
    if (!dialog || !paste(control<QLineEdit>(dialog, "fileNameEdit"), path)) return false;
    auto *buttons = dialog->findChild<QDialogButtonBox *>();
    return buttons && click(buttons->button(QDialogButtonBox::Save));
}

bool populated(QPrintPreviewDialog *dialog)
{
    auto *widget = dialog ? dialog->findChild<QPrintPreviewWidget *>() : nullptr;
    return dialog && dialog->isVisible() && widget && widget->pageCount() > 0;
}

#ifdef Q_OS_LINUX
struct ProcReading
{
    bool available = false;
    qint64 pid = 0;
    qint64 ticks = 0;
    qint64 rssKb = -1;
    qint64 pssKb = -1;
    int threads = -1;
    int descriptors = -1;
};

ProcReading readProcess(qint64 pid)
{
    ProcReading reading;
    reading.pid = pid;
    if (pid <= 0) return reading;
    const QString root = QStringLiteral("/proc/%1/").arg(pid);
    const QByteArray stat = bytes(root + QStringLiteral("stat"));
    const int endName = stat.lastIndexOf(')');
    const QList<QByteArray> fields = stat.mid(endName + 2).simplified().split(' ');
    if (endName < 0 || fields.size() < 13) return reading;
    reading.available = true;
    reading.ticks = fields.at(11).toLongLong() + fields.at(12).toLongLong();
    const auto kilobytes = [](const QByteArray &content, const QByteArray &name) -> qint64
    {
        for (const auto &line : content.split('\n'))
            if (line.startsWith(name)) return line.mid(name.size()).simplified().split(' ').value(0).toLongLong();
        return -1;
    };
    const QByteArray status = bytes(root + QStringLiteral("status"));
    reading.rssKb = kilobytes(status, "VmRSS:");
    reading.threads = int(kilobytes(status, "Threads:"));
    reading.pssKb = kilobytes(bytes(root + QStringLiteral("smaps_rollup")), "Pss:");
    reading.descriptors = QDir(root + QStringLiteral("fd")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).size();
    return reading;
}

QJsonObject procJson(const ProcReading &reading)
{
    return {{QStringLiteral("pid"), double(reading.pid)}, {QStringLiteral("available"), reading.available},
        {QStringLiteral("cpu_ticks"), double(reading.ticks)}, {QStringLiteral("rss_kb"), double(reading.rssKb)},
        {QStringLiteral("pss_kb"), reading.pssKb < 0 ? QJsonValue() : QJsonValue(double(reading.pssKb))},
        {QStringLiteral("threads"), reading.threads}, {QStringLiteral("fd_count"), reading.descriptors}};
}

struct ResourcePeak
{
    struct Previous { qint64 ticks; qint64 atNs; };
    QMap<qint64, Previous> previous;
    QSet<qint64> pids;
    qint64 rssKb = -1, pssKb = -1, observedCpuTicks = 0;
    int threads = -1, descriptors = -1, samples = 0;
    double intervalCpuPeak = 0;
    void add(const ProcReading &reading, qint64 atNs, long ticksPerSecond)
    {
        if (!reading.available) return;
        ++samples;
        pids.insert(reading.pid);
        rssKb = qMax(rssKb, reading.rssKb); pssKb = qMax(pssKb, reading.pssKb);
        threads = qMax(threads, reading.threads); descriptors = qMax(descriptors, reading.descriptors);
        if (previous.contains(reading.pid))
        {
            const auto old = previous.value(reading.pid);
            const qint64 delta = qMax<qint64>(0, reading.ticks - old.ticks);
            observedCpuTicks += delta;
            if (atNs > old.atNs && ticksPerSecond > 0)
                intervalCpuPeak = qMax(intervalCpuPeak, 100.0 * double(delta) / double(ticksPerSecond) / (double(atNs - old.atNs) / 1e9));
        }
        previous.insert(reading.pid, {reading.ticks, atNs});
    }
    QJsonObject json(long ticksPerSecond, double durationMs) const
    {
        return {{QStringLiteral("samples"), samples}, {QStringLiteral("unique_pids"), pids.size()},
            {QStringLiteral("peak_rss_kb_sampled"), double(rssKb)},
            {QStringLiteral("peak_pss_kb_sampled"), pssKb < 0 ? QJsonValue() : QJsonValue(double(pssKb))},
            {QStringLiteral("peak_threads_sampled"), threads}, {QStringLiteral("peak_fd_sampled"), descriptors},
            {QStringLiteral("observed_cpu_ticks"), double(observedCpuTicks)},
            {QStringLiteral("cpu_percent_one_core_mean_observed"), durationMs > 0 ? 100.0 * double(observedCpuTicks) / double(ticksPerSecond) / (durationMs / 1000.0) : 0},
            {QStringLiteral("cpu_percent_one_core_interval_peak_sampled"), intervalCpuPeak}};
    }
};

QJsonObject distribution(QVector<double> values)
{
    if (values.isEmpty()) return {};
    std::sort(values.begin(), values.end());
    double sum = 0;
    for (double value : values) sum += value;
    const auto percentile = [&](double fraction) { return values.at(qBound(0, int(std::ceil(fraction * values.size())) - 1, values.size() - 1)); };
    return {{QStringLiteral("count"), values.size()}, {QStringLiteral("mean_ms"), sum / values.size()},
        {QStringLiteral("p95_ms"), percentile(.95)}, {QStringLiteral("p99_ms"), percentile(.99)},
        {QStringLiteral("maximum_ms"), values.last()}};
}

class ReportLoadProbe : public QObject
{
public:
    ReportLoadProbe(HistoryReportController *controller, const QString &exportDirectory)
        : QObject(controller), process(controller->findChild<QProcess *>()), exportDirectory(exportDirectory)
    {
        clock.start();
        ticksPerSecond = sysconf(_SC_CLK_TCK);
        heartbeat.setInterval(10); heartbeat.setTimerType(Qt::PreciseTimer);
        QObject::connect(&heartbeat, &QTimer::timeout, this, [this]()
        {
            const qint64 now = clock.nsecsElapsed();
            if (lastHeartbeatNs) heartbeatIntervals.append(double(now - lastHeartbeatNs) / 1e6);
            lastHeartbeatNs = now;
        });
        resources.setInterval(100);
        QObject::connect(&resources, &QTimer::timeout, this, [this]() { sample(); });
        if (process) QObject::connect(process, &QProcess::started, this, [this]() { sample(); });
        heartbeat.start(); resources.start(); sample();
    }
    void sample()
    {
        const qint64 at = clock.nsecsElapsed();
        const ProcReading main = readProcess(QCoreApplication::applicationPid());
        const ProcReading child = readProcess(process ? process->processId() : 0);
        parent.add(main, at, ticksPerSecond); helper.add(child, at, ticksPerSecond);
        qint64 exportBytes = 0;
        QDirIterator files(exportDirectory, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (files.hasNext()) { files.next(); exportBytes += files.fileInfo().size(); }
        exportPeakBytes = qMax(exportPeakBytes, exportBytes);
        if (initialExportBytes < 0) initialExportBytes = exportBytes;
        lastExportBytes = exportBytes;
        samples.append(QJsonObject{{QStringLiteral("elapsed_ms"), double(at) / 1e6},
            {QStringLiteral("parent"), procJson(main)}, {QStringLiteral("helper"), procJson(child)},
            {QStringLiteral("exports_bytes_including_temporary_jobs"), double(exportBytes)}});
    }
    QJsonArray residualHelpers() const
    {
        QJsonArray found;
        for (qint64 pid : helper.pids)
        {
            const QByteArray command = bytes(QStringLiteral("/proc/%1/cmdline").arg(pid));
            if (command.contains("sn100_report_renderer")) found.append(double(pid));
        }
        return found;
    }
    QJsonObject result() const
    {
        const double elapsed = double(clock.nsecsElapsed()) / 1e6;
        return {{QStringLiteral("elapsed_ms"), elapsed}, {QStringLiteral("cpu_clock_ticks_per_second"), double(ticksPerSecond)},
            {QStringLiteral("cpu_convention"), QStringLiteral("One fully used CPU core is 100%; sampled /proc utime+stime deltas; helper samples omit unobserved process start/end tails")},
            {QStringLiteral("parent"), parent.json(ticksPerSecond, elapsed)}, {QStringLiteral("helper"), helper.json(ticksPerSecond, elapsed)},
            {QStringLiteral("heartbeat_target_ms"), 10}, {QStringLiteral("heartbeat_intervals"), distribution(heartbeatIntervals)},
            {QStringLiteral("resource_sampling_target_ms"), 100},
            {QStringLiteral("exports_initial_bytes"), double(initialExportBytes)}, {QStringLiteral("exports_final_bytes"), double(lastExportBytes)},
            {QStringLiteral("exports_peak_bytes_including_temporary_jobs"), double(exportPeakBytes)},
            {QStringLiteral("known_helper_pids_still_running"), residualHelpers()},
            {QStringLiteral("samples"), samples}};
    }
    QPointer<QProcess> process;
    QString exportDirectory;
    QElapsedTimer clock;
    QTimer heartbeat, resources;
    qint64 lastHeartbeatNs = 0, initialExportBytes = -1, lastExportBytes = 0, exportPeakBytes = 0;
    long ticksPerSecond = 0;
    QVector<double> heartbeatIntervals;
    ResourcePeak parent, helper;
    QJsonArray samples;
};
#endif
}

class IntegratedReportTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void committedHospitalAndReportLifecycle();
    void persistedHospitalAndLegacyEntry();
    void reportLoadExperiment();
private:
    QString directory_;
    QString renderer_;
};

void IntegratedReportTest::initTestCase()
{
    directory_ = QString::fromUtf8(qgetenv("SN100_INTEGRATED_REPORT_EVIDENCE_DIR"));
    renderer_ = QString::fromUtf8(qgetenv("SN100_REPORT_RENDERER"));
    QVERIFY2(!directory_.isEmpty() && QDir::isAbsolutePath(directory_) && !QFileInfo::exists(directory_),
        "SN100_INTEGRATED_REPORT_EVIDENCE_DIR must be absolute and must not exist");
    QVERIFY(!directory_.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive));
    QVERIFY(QFileInfo(renderer_).isAbsolute() && QFileInfo(renderer_).isExecutable());
    QVERIFY(QDir().mkpath(directory_));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
    qInfo().noquote() << "INTEGRATED_REPORT_EVIDENCE" << directory_ << "QPA" << QGuiApplication::platformName();
}

void IntegratedReportTest::committedHospitalAndReportLifecycle()
{
    Session session(QDir(directory_).filePath(QStringLiteral("runtime")));
    QVERIFY(session.beforeAttachRejected && session.reportEnabled && session.ready());
    auto *controller = session.window->historyReports();
    QVERIFY(controller);
    QVERIFY(!session.window->enableHistoryReports(session.historyDirectory()));
    QCOMPARE(session.window->historyReports(), controller);
    controller->setRendererPath(renderer_);
    QSignalSpy failures(controller, &HistoryReportController::failed);
    QSignalSpy exported(controller, &HistoryReportController::pdfExported);
    QSignalSpy closed(controller, &HistoryReportController::previewClosed);
    QSignalSpy requests(session.page, &HistoryPage::reportPreviewRequested);
    QPointer<QPrintPreviewDialog> preview;
    QObject::connect(controller, &HistoryReportController::previewReady, controller,
        [&](QPrintPreviewDialog *dialog) { preview = dialog; });
    QVERIFY(session.login());

    // Draft hospital text cannot become an authoritative report heading.
    QVERIFY(session.navigate(6, 1));
    auto *hospitalName = control<QLineEdit>(session.window->hospitalPage(), "ldtHospName");
    QVERIFY(hospitalName && hospitalName->text().isEmpty());
    QVERIFY(paste(hospitalName, kDraftHospital));
    auto discardHospital = answerNext(session.window, QMessageBox::Yes);
    QVERIFY(session.navigate(2));
    QVERIFY(*discardHospital);
    QVERIFY(session.query() && session.selectSample(2));
    QVERIFY(click(control<QAbstractButton>(session.page, "btnPrinter")));
    QVERIFY(waitUntil([&]() { return !failures.isEmpty(); }));
    QVERIFY(!preview && !session.page->isBusy());
    QVERIFY(failures.last().last().toString().contains(QStringLiteral("医院")));
    failures.clear();

    QVERIFY(session.saveHospital(kHospital));
    QVERIFY(paste(hospitalName, kDraftHospital));
    auto keepHospital = answerNext(session.window, QMessageBox::No);
    QVERIFY(!session.window->selectMainPage(2));
    QVERIFY(*keepHospital && hospitalName->text() == kDraftHospital);
    discardHospital = answerNext(session.window, QMessageBox::Yes);
    QVERIFY(session.navigate(2));
    QVERIFY(*discardHospital);
    QVERIFY(session.selectSample(2));

    // Patient drafts retain the independent page's existing print guard.
    QVERIFY(click(control<QAbstractButton>(session.page, "btnDetail")));
    QPointer<QDialog> detail;
    QVERIFY(waitUntil([&]() { detail = control<QDialog>(session.page, "MaskDlgReportDetail"); return detail && detail->isVisible() && !session.page->isBusy(); }));
    QVERIFY(click(control<QAbstractButton>(detail, "btnMenuInfo")));
    QVERIFY(paste(control<QLineEdit>(detail, "ldtName"), kDraftPatient));
    QVERIFY(session.page->hasUnsavedChanges());
    const int requestCount = requests.count();
    QVERIFY(click(control<QAbstractButton>(detail, "btnPrinter")));
    QCOMPARE(requests.count(), requestCount);
    QVERIFY(!preview && session.page->hasUnsavedChanges());
    auto discardPatient = answerNext(session.window, QMessageBox::Discard);
    QVERIFY(session.page->requestLeave(QStringLiteral("整合测试放弃患者草稿")));
    QVERIFY(*discardPatient && !session.page->hasUnsavedChanges());

    // Successful preview starts only from the original page button.
    QVERIFY(click(control<QAbstractButton>(session.page, "btnPrinter")));
    QVERIFY(waitUntil([&]() { return populated(preview); }));
    QVERIFY(session.page->isBusy());
    QVERIFY(!session.window->selectMainPage(6));
    QVERIFY(!session.window->requestLogout());
    QVERIFY(!session.window->close());
    QCOMPARE(session.window->currentMainPage(), 2);
    QVERIFY(session.window->accountSession().authenticated);
    auto *savePdf = pdfButton(preview);
    QVERIFY(savePdf && savePdf->isEnabled());

    QVERIFY(click(savePdf));
    QVERIFY(waitUntil([]() { return pdfChooser() != nullptr; }));
    auto *buttons = pdfChooser()->findChild<QDialogButtonBox *>();
    QVERIFY(buttons && click(buttons->button(QDialogButtonBox::Cancel)));
    QVERIFY(waitUntil([]() { return pdfChooser() == nullptr; }));
    QCOMPARE(exported.count(), 0);
    QVERIFY(failures.isEmpty() && session.page->isBusy());

    // Missing helper gives an observable error and publishes no partial file.
    controller->setRendererPath(QDir(directory_).filePath(QStringLiteral("missing_renderer")));
    const QString missing = session.pdfPath(QStringLiteral("missing_helper.pdf"));
    QVERIFY(click(savePdf));
    QVERIFY(waitUntil([]() { return pdfChooser() != nullptr; }));
    QVERIFY(choosePdfPath(missing));
    QVERIFY(waitUntil([&]() { return !failures.isEmpty() && pdfChooser() == nullptr; }));
    QVERIFY(!QFileInfo::exists(missing));
    QCOMPARE(exported.count(), 0);
    const QString failedStatus = session.page->property("lastStatus").toString();
    auto *stateLabel = control<QLabel>(session.window, "lblDivState");
    QVERIFY(stateLabel && failedStatus.contains(QStringLiteral("渲染器")));
    QVERIFY(stateLabel->styleSheet().contains(QStringLiteral("#E83323")));
    QTest::keyClick(preview, Qt::Key_Escape);
    QVERIFY(waitUntil([&]() { return !preview && !session.page->isBusy(); }));
    QCOMPARE(session.page->property("lastStatus").toString(), failedStatus);
    QVERIFY(stateLabel->styleSheet().contains(QStringLiteral("#E83323")));
    QVERIFY(session.navigate(6, 0) && session.navigate(6, 1) && session.navigate(2));
    failures.clear();

    // Retry through the actual helper; closing preview cannot abandon export.
    controller->setRendererPath(renderer_);
    QVERIFY(click(control<QAbstractButton>(session.page, "btnPrinter")));
    QVERIFY(waitUntil([&]() { return populated(preview); }));
    QVERIFY(!stateLabel->styleSheet().contains(QStringLiteral("#E83323")));
    QVERIFY(click(pdfButton(preview)));
    QVERIFY(waitUntil([]() { return pdfChooser() != nullptr; }));
    const QString output = session.pdfPath(QStringLiteral("committed_hospital.pdf"));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(choosePdfPath(output));
    QVERIFY(session.page->isBusy());
    QTest::keyClick(preview, Qt::Key_Escape);
    if (session.page->isBusy())
    {
        QVERIFY(!session.window->selectMainPage(6));
        QVERIFY(!session.window->requestLogout());
        QVERIFY(!session.window->close());
    }
    QVERIFY(waitUntil([&]() { return !exported.isEmpty() || !failures.isEmpty(); }, 30000));
    QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.last().last().toString()));
    QCOMPARE(exported.last().at(0).toString(), output);
    QVERIFY(waitUntil([&]() { return !preview && !session.page->isBusy(); }));
    QVERIFY(session.page->property("lastStatus").toString().contains(output));
    QVERIFY(!stateLabel->styleSheet().contains(QStringLiteral("#E83323")));
    QVERIFY(bytes(output).startsWith("%PDF-"));
    QJsonObject pdf;
    QString error;
    QVERIFY2(inspectPdf(output, &pdf, &error), qPrintable(error));
    const QString text = pdfText(pdf);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("committed_hospital_pdf.json")), pdf));
    QVERIFY2(text.contains(kHospital), qPrintable(text));
    QString compact = text;
    compact.remove(QRegularExpression(QStringLiteral("\\s+")));
    QVERIFY(compact.contains(QStringLiteral("检验报告单")) && text.contains(QStringLiteral("匿名二号")));
    QVERIFY(!text.contains(kDraftHospital) && !text.contains(kDraftPatient));
    QVERIFY(!text.contains(QStringLiteral("[hospital_name]")));
    QVERIFY(session.window->requestLogout());
    QVERIFY(waitUntil([&]() { return !session.window->accountSession().authenticated && !session.window->accountPage()->isBusy(); }));
    QVERIFY(session.navigate(6, 0) && session.navigate(6, 1) && session.navigate(2));
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("lifecycle.json")),
        {{QStringLiteral("saved_hospital_in_pdf"), kHospital}, {QStringLiteral("draft_hospital_excluded"), true},
         {QStringLiteral("draft_patient_blocked"), true}, {QStringLiteral("preview_cancel_recovered"), true},
         {QStringLiteral("missing_helper_recovered"), true}, {QStringLiteral("preview_close_count"), closed.count()},
         {QStringLiteral("scope"), QStringLiteral("anonymous offline software checks; no physical printer, RK screen or instrument verification")}}));
    QVERIFY(session.stopWorkers());
}

void IntegratedReportTest::persistedHospitalAndLegacyEntry()
{
    // Reopening uses HospitalStore::loaded, rather than a prior save callback.
    {
        Session session(QDir(directory_).filePath(QStringLiteral("runtime")), true, false);
        QVERIFY(session.ready() && session.reportEnabled);
        auto *controller = session.window->historyReports();
        QVERIFY(controller);
        controller->setRendererPath(renderer_);
        QSignalSpy failures(controller, &HistoryReportController::failed);
        QPointer<QPrintPreviewDialog> preview;
        QObject::connect(controller, &HistoryReportController::previewReady, controller,
            [&](QPrintPreviewDialog *dialog) { preview = dialog; });
        QVERIFY(session.navigate(6, 1));
        QCOMPARE(control<QLineEdit>(session.window->hospitalPage(), "ldtHospName")->text(), kHospital);
        QVERIFY(session.navigate(2) && session.query() && session.selectSample(2));
        QVERIFY(click(control<QAbstractButton>(session.page, "btnPrinter")));
        QVERIFY(waitUntil([&]() { return populated(preview) || !failures.isEmpty(); }));
        QVERIFY(failures.isEmpty() && populated(preview));
        QTest::keyClick(preview, Qt::Key_Escape);
        QVERIFY(waitUntil([&]() { return !preview && !session.page->isBusy(); }));
        QVERIFY(session.stopWorkers());
    }
    // Existing callers which attach history without enabling heavy reports
    // retain their original three-page behaviour and disabled print entry.
    Session legacy(QDir(directory_).filePath(QStringLiteral("legacy")), false);
    QVERIFY(legacy.ready() && legacy.beforeAttachRejected);
    QVERIFY(!legacy.window->historyReports());
    auto *hospital = legacy.window->hospitalPage();
    auto *account = legacy.window->accountPage();
    QVERIFY(legacy.navigate(6, 0) && legacy.navigate(6, 1) && legacy.navigate(2));
    QVERIFY(legacy.query());
    QCOMPARE(legacy.table()->rowCount(), 3);
    QCOMPARE(legacy.window->hospitalPage(), hospital);
    QCOMPARE(legacy.window->accountPage(), account);
    QCOMPARE(legacy.window->historyPage(), legacy.page);
    QVERIFY(!control<QAbstractButton>(legacy.page, "btnPrinter")->isEnabled());
    QVERIFY(legacy.login());
    QVERIFY(legacy.window->requestLogout());
    QVERIFY(waitUntil([&]() { return !legacy.window->accountSession().authenticated; }));
    QVERIFY(legacy.stopWorkers());
}

void IntegratedReportTest::reportLoadExperiment()
{
#ifndef Q_OS_LINUX
    QSKIP("This baseline samples Linux /proc and must run in WSL/Linux");
#else
    const QString runtime = QDir(directory_).filePath(QStringLiteral("load_runtime"));
    Session session(runtime);
    QVERIFY(session.ready() && session.reportEnabled);
    QVERIFY(session.login() && session.saveHospital(kHospital));
    QVERIFY(session.navigate(2) && session.query() && session.selectSample(2));
    auto *controller = session.window->historyReports();
    QVERIFY(controller);
    controller->setRendererPath(renderer_);
    QSignalSpy exported(controller, &HistoryReportController::pdfExported);
    QSignalSpy failures(controller, &HistoryReportController::failed);
    QPointer<QPrintPreviewDialog> preview;
    QObject::connect(controller, &HistoryReportController::previewReady, controller,
        [&](QPrintPreviewDialog *dialog) { preview = dialog; });
    ReportLoadProbe probe(controller, QDir(session.historyDirectory()).filePath(QStringLiteral("exports")));
    QVERIFY(probe.process);
    QJsonArray rounds;
    QVector<double> previewTimes, exportTimes, navigationTimes, roundTimes;
    QElapsedTimer duration;
    duration.start();
    int count = 0;
    while (duration.elapsed() < 60000)
    {
        const qint64 roundStart = duration.nsecsElapsed();
        QElapsedTimer operation;
        operation.start();
        QVERIFY(click(control<QAbstractButton>(session.page, "btnPrinter")));
        QVERIFY(waitUntil([&]() { return populated(preview) || !failures.isEmpty(); }));
        QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.last().last().toString()));
        QVERIFY(populated(preview));
        const double previewMs = double(operation.nsecsElapsed()) / 1e6;
        const QString name = QStringLiteral("load_%1.pdf").arg(count + 1, 4, 10, QChar('0'));
        const QString output = session.pdfPath(name);
        QVERIFY(!QFileInfo::exists(output));
        operation.restart();
        QVERIFY(click(pdfButton(preview)));
        QVERIFY(waitUntil([]() { return pdfChooser() != nullptr; }));
        const int previousExports = exported.count();
        QVERIFY(choosePdfPath(output));
        QTest::keyClick(preview, Qt::Key_Escape);
        QVERIFY(waitUntil([&]() { return exported.count() > previousExports || !failures.isEmpty(); }, 30000));
        QVERIFY2(failures.isEmpty(), failures.isEmpty() ? "" : qPrintable(failures.last().last().toString()));
        QVERIFY(waitUntil([&]() { return !preview && !session.page->isBusy() && probe.process->state() == QProcess::NotRunning; }));
        const double exportMs = double(operation.nsecsElapsed()) / 1e6;
        QCOMPARE(exported.last().at(0).toString(), output);
        QVERIFY(bytes(output).startsWith("%PDF-"));
        operation.restart();
        QVERIFY(session.navigate(6, 1) && session.navigate(6, 0) && session.navigate(2));
        const double navigationMs = double(operation.nsecsElapsed()) / 1e6;
        const double roundMs = double(duration.nsecsElapsed() - roundStart) / 1e6;
        ++count;
        previewTimes.append(previewMs); exportTimes.append(exportMs); navigationTimes.append(navigationMs); roundTimes.append(roundMs);
        rounds.append(QJsonObject{{QStringLiteral("round"), count}, {QStringLiteral("elapsed_ms"), double(duration.nsecsElapsed()) / 1e6},
            {QStringLiteral("preview_ms"), previewMs}, {QStringLiteral("pdf_toolbar_to_export_idle_ms"), exportMs},
            {QStringLiteral("three_navigation_ms"), navigationMs}, {QStringLiteral("round_ms"), roundMs},
            {QStringLiteral("pdf_file"), QStringLiteral("exports/") + name}, {QStringLiteral("pdf_bytes"), double(QFileInfo(output).size())},
            {QStringLiteral("cumulative_exports"), exported.count()}, {QStringLiteral("helper_idle_after_round"), true}});
        QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("round_%1.json").arg(count, 4, 10, QChar('0'))), rounds.last().toObject()));
        qInfo().noquote() << "REPORT_LOAD_ROUND" << count << "preview_ms" << previewMs << "pdf_ms" << exportMs
            << "navigation_ms" << navigationMs << "cumulative_exports" << exported.count();
        // Fixed one-second starts give a reproducible mixed load; this is input
        // pacing, not a claim that any latency budget has been accepted.
        const qint64 nextStart = qint64(count) * 1000;
        while (duration.elapsed() < nextStart) QTest::qWait(5);
    }
    const qint64 loadElapsed = duration.elapsed();
    QTest::qWait(1000);
    probe.sample();
    QVERIFY(loadElapsed >= 60000);
    QCOMPARE(exported.count(), count);
    QVERIFY(failures.isEmpty() && probe.process->state() == QProcess::NotRunning);
    QVERIFY(probe.residualHelpers().isEmpty());
    QJsonObject baseline = probe.result();
    baseline.insert(QStringLiteral("schema"), QStringLiteral("sn100-integrated-report-mixed-load-v1"));
    baseline.insert(QStringLiteral("load_duration_ms"), double(loadElapsed));
    baseline.insert(QStringLiteral("round_count"), count);
    baseline.insert(QStringLiteral("successful_pdf_count"), exported.count());
    baseline.insert(QStringLiteral("failure_count"), failures.count());
    baseline.insert(QStringLiteral("load"), QStringLiteral("Original print -> populated preview -> original PDF toolbar -> distinct file using real helper -> close preview -> helper idle -> hospital/account/history navigation; one-second cycle starts"));
    baseline.insert(QStringLiteral("environment"), QJsonObject{{QStringLiteral("qpa"), QGuiApplication::platformName()},
        {QStringLiteral("qt"), QString::fromLatin1(qVersion())}, {QStringLiteral("build"), QStringLiteral("Release")},
        {QStringLiteral("font"), QApplication::font().family()}, {QStringLiteral("concurrent_work"), QStringLiteral("ARM/native compilation may be active; mixed desktop load, no isolated CPU or resource-budget acceptance")}});
    baseline.insert(QStringLiteral("measurement_limits"), QStringLiteral("UI injection to signal/idle; no compositor pixel timing; /proc sample peaks may miss short spikes; PSS unavailable is null; resource sampling and JSON evidence incur overhead; budgets unknown and no latency threshold is asserted"));
    baseline.insert(QStringLiteral("preview_latency"), distribution(previewTimes));
    baseline.insert(QStringLiteral("pdf_latency"), distribution(exportTimes));
    baseline.insert(QStringLiteral("three_navigation_latency"), distribution(navigationTimes));
    baseline.insert(QStringLiteral("round_latency"), distribution(roundTimes));
    baseline.insert(QStringLiteral("rounds"), rounds);
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("report_load_baseline.json")), baseline));
    QJsonObject pdf;
    QString error;
    const QString firstPdf = session.pdfPath(QStringLiteral("load_0001.pdf"));
    QVERIFY2(inspectPdf(firstPdf, &pdf, &error), qPrintable(error));
    QVERIFY(pdfText(pdf).contains(kHospital));
    QVERIFY(saveJson(QDir(directory_).filePath(QStringLiteral("load_first_pdf.json")), pdf));
    QVERIFY(session.stopWorkers());
#endif
}

QTEST_MAIN(IntegratedReportTest)
#include "integrated_report_test.moc"
