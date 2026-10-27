// RK专项测量：正式ProjectPage、真实存储线程；不截图、不做SQL内容审计、不打开辅助菜单。
#include "project_page.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <functional>

static bool waitFor(const std::function<bool()> &condition, int timeout = 10000)
{
    QElapsedTimer timer; timer.start();
    while (!condition() && timer.elapsed() < timeout)
    {
        QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
    }
    return condition();
}
static void settle(int milliseconds)
{
    QEventLoop loop; QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit); loop.exec();
}
static bool mouseClick(QWidget *widget)
{
    if (!widget || !widget->isEnabled() || !widget->isVisible()) return false;
    const QPoint point = widget->rect().center(), global = widget->mapToGlobal(point);
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press); QApplication::sendEvent(widget, &release);
    return true;
}
static void typeText(QLineEdit *edit, const QString &text)
{
    edit->setFocus();
    QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier), erase(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QKeyEvent type(QEvent::KeyPress, 0, Qt::NoModifier, text);
    QApplication::sendEvent(edit, &select); QApplication::sendEvent(edit, &erase); QApplication::sendEvent(edit, &type);
}
static QJsonObject distribution(QVector<double> values)
{
    std::sort(values.begin(), values.end());
    if (values.isEmpty()) return {{"samples", 0}};
    return {{"samples", values.size()}, {"p95Ms", values[(values.size()-1)*95/100]},
            {"p99Ms", values[(values.size()-1)*99/100]}, {"maxMs", values.last()}};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv); app.setStyle("Fusion"); app.setFont(QFont("Noto Sans CJK SC", 11));
    QCommandLineParser parser;
    parser.addOptions({{"data-dir", "专用原配置工作副本", "root"}, {"output-dir", "测量JSON目录", "output"},
                       {"iterations", "正常保存次数，最少2次", "number", "12"}});
    parser.process(app);
    const QString root = parser.value("data-dir"), output = parser.value("output-dir");
    bool countOK = false; const int iterations = parser.value("iterations").toInt(&countOK);
    if (!QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(output) || !countOK || iterations < 2 || iterations > 100 ||
        !QFileInfo::exists(root + "/.sn100-original-work-v1") || !QDir().mkpath(output)) return 2;
    int passed = 0, failed = 0;
    QJsonArray assertions, measurements;
    const auto check = [&](bool good, const QString &name)
    { good ? ++passed : ++failed; assertions.append(QJsonObject{{"passed", good}, {"name", name}}); };
    ProjectsStore::registerTypes();
    QThread worker; auto *store = new ProjectsStore; store->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new ProjectPage(store);
    AccountSession engineer; engineer.name = "rkProfileEngineer"; engineer.role = 4;
    engineer.authenticated = true; engineer.source = AccountSessionSource::Simulated; page->setSession(engineer);
    QScrollArea host; host.setWidget(page); host.setWidgetResizable(false); host.resize(1600, 900); host.show();
    QElapsedTimer clock; clock.start();
    const auto nowMs = [&]() { return double(clock.nsecsElapsed()) / 1000000.0; };
    bool measuring = false; QString currentName;
    double startMs = 0, lastBeatMs = 0, busyMs = -1, receiptMs = -1, finishedMs = -1, loadReceiptMs = -1;
    int saveCount = 0, loadCount = 0; bool lastSaveOK = false;
    QVector<double> gaps;
    QTimer heartbeat; heartbeat.setInterval(10); heartbeat.setTimerType(Qt::PreciseTimer);
    QObject::connect(&heartbeat, &QTimer::timeout, &app, [&]()
    {
        const double now = nowMs();
        if (measuring) gaps.append(now - lastBeatMs);
        lastBeatMs = now;
    });
    QObject::connect(page, &ProjectPage::busyChanged, &app, [&](bool busy)
    {
        if (!measuring) return;
        if (busy && busyMs < 0) busyMs = nowMs() - startMs;
        if (!busy) finishedMs = nowMs() - startMs;
    });
    QObject::connect(page, &ProjectPage::operationFinished, &app, [&](const QString &operation, bool good)
    {
        if (operation == "save")
        {
            ++saveCount; lastSaveOK = good;
            if (measuring) receiptMs = nowMs() - startMs;
        }
        if (operation == "load")
        {
            ++loadCount;
            if (measuring) loadReceiptMs = nowMs() - startMs;
        }
    });
    int errorDialogs = 0;
    QTimer dismissError;
    QObject::connect(&dismissError, &QTimer::timeout, &app, [&]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            if (mouseClick(box->button(QMessageBox::Ok))) ++errorDialogs;
    });
    dismissError.start(20); heartbeat.start(); worker.start();
    const auto begin = [&](const QString &name)
    {
        currentName = name; gaps.clear(); busyMs = -1; receiptMs = -1; finishedMs = -1; loadReceiptMs = -1;
        startMs = nowMs(); lastBeatMs = startMs; measuring = true;
    };
    const auto end = [&](double handlerMs, bool good)
    {
        // 给操作末尾最后一个不完整10ms周期留出一个实际heartbeat，避免漏掉重建控件的尾段。
        settle(15); measuring = false;
        measurements.append(QJsonObject{{"name", currentName}, {"success", good}, {"sendMouseEventReturnMs", handlerMs},
            {"busySignalMs", busyMs}, {"saveReceiptMs", receiptMs}, {"busyFalseMs", finishedMs},
            {"loadReceiptMs", loadReceiptMs},
            {"totalObservedMs", nowMs() - startMs}, {"heartbeatGaps", distribution(gaps)}});
    };
    settle(1000);
    begin("initial_load");
    QMetaObject::invokeMethod(store, [=]() { store->initializeOriginal(root); }, Qt::QueuedConnection);
    const bool loaded = waitFor([&]() { return page->isReady() && !page->isBusy() && page->projects().size() == 7; });
    check(loaded, "7 original projects loaded"); end(-1, loaded);
    auto *edit = page->findChild<QLineEdit *>("project_lis_code");
    auto *saveButton = page->findChild<QPushButton *>("btnSave");
    auto *list = page->findChild<QTableWidget *>("lstProject");
    ProjectRecord selected;
    for (const auto &project : page->projects()) if (project.name == "PT") selected = project;
    for (int row = 0; list && row < list->rowCount(); ++row)
        if (list->item(row, 0)->data(Qt::UserRole).toLongLong() == selected.id) list->selectRow(row);
    check(edit && saveButton && selected.id > 0, "profile controls available");
    if (!loaded || !edit || !saveButton || selected.id <= 0) return 3;
    const QString oldCode = selected.lisCode;
    host.ensureWidgetVisible(saveButton); settle(300);
    const auto runSave = [&](const QString &name, const QString &code, bool expected)
    {
        // 输入和滚动在测量区间之外；测量从鼠标按下前开始，包含业务校验、忙碌反馈与读回。
        host.ensureWidgetVisible(edit); typeText(edit, code); host.ensureWidgetVisible(saveButton); settle(100);
        const int before = saveCount;
        begin(name);
        const bool clicked = mouseClick(saveButton); const double handler = nowMs() - startMs;
        const bool completed = clicked && waitFor([&]() { return saveCount > before && !page->isBusy(); });
        const bool good = completed && saveCount == before + 1 && lastSaveOK == expected;
        end(handler, good); check(good, name);
        settle(100);
        return good;
    };
    for (int index = 0; index < iterations; ++index)
        runSave(QStringLiteral("normal_save_%1").arg(index + 1), index % 2 ? "PROFB" : "PROFA", true);
    {
        auto lockDatabase = QSqlDatabase::addDatabase("QSQLITE", "profile_write_lock");
        lockDatabase.setDatabaseName(QDir(root).filePath("project/" + selected.originalFileName));
        check(lockDatabase.open(), "lock connection opened");
        QSqlQuery lock(lockDatabase); check(lock.exec("BEGIN IMMEDIATE"), "real write lock acquired");
        runSave("locked_save_expected_failure", "PROFLOCK", false);
        check(page->hasUnsavedChanges(), "failed save retains draft");
        check(lock.exec("ROLLBACK"), "write lock released"); lockDatabase.close();
    }
    QSqlDatabase::removeDatabase("profile_write_lock");
    // 重试保留在原输入框中的草稿，不通过另一次输入掩盖失败恢复状态。
    {
        host.ensureWidgetVisible(saveButton); settle(100); const int before = saveCount;
        begin("retry_after_unlock"); const bool clicked = mouseClick(saveButton); const double handler = nowMs() - startMs;
        const bool good = clicked && waitFor([&]() { return saveCount > before && !page->isBusy(); }) && lastSaveOK;
        end(handler, good); check(good, "retry after unlocking saved original draft");
    }
    runSave("restore_original_lis_code", oldCode, true);
    settle(200);
    const int beforeReload = loadCount; begin("explicit_reload"); page->reload();
    const double reloadHandler = nowMs() - startMs;
    const bool reloaded = waitFor([&]() { return loadCount > beforeReload && !page->isBusy(); });
    end(reloadHandler, reloaded); check(reloaded, "explicit original reload completed");
    heartbeat.stop(); dismissError.stop();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    worker.quit(); check(worker.wait(3000), "store thread stopped"); host.close();
    const QJsonObject report{{"passed", passed}, {"failed", failed}, {"iterations", iterations},
        {"measurements", measurements}, {"assertions", assertions}, {"errorDialogsDismissed", errorDialogs},
        {"heartbeatIntervalMs", 10}, {"screenshots", 0}, {"sqlContentAudit", false}, {"instrumentActions", 0},
        {"notes", "Times start immediately before actual save mouse events. Inputs, scrolling, and lock SQL occur outside measured intervals. busySignal is software state feedback, not confirmed painted pixels. busyFalse includes worker save and UI readback. Only a real lock-failure warning is auto-dismissed. No performance pass threshold is assumed."}};
    QFile result(output + "/page_profile.json");
    if (!result.open(QIODevice::WriteOnly) || result.write(QJsonDocument(report).toJson()) <= 0) return 4;
    qInfo() << "PROFILE_RESULT" << "passed" << passed << "failed" << failed << "savedMeasurements" << measurements.size();
    return failed ? 1 : 0;
}
