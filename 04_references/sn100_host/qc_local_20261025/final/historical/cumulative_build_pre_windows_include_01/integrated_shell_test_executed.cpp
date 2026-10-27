// 三页组合回归：测试数据只进入显式指定的新目录，账户密码不写入证据。
#include <QtTest>
#include <QAbstractButton>
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
#include <QFileInfo>
#include <QFileDialog>
#include <QFont>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLibrary>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QProcess>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <functional>
#include <algorithm>
#include <memory>

#include "data/account_store.h"
#include "data/hospital_store.h"
#include "history/history_page.h"
#include "shell/frmmain.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmhospinfosetting.h"

#ifdef Q_OS_LINUX
#include <X11/Xlib.h>
#endif

namespace
{
const QString kSavedHospital = QStringLiteral("三页集成匿名医院 O'Brien");
const QString kBarcode = QStringLiteral("ANON,\"003");
constexpr int kFinalAge = 43;

bool waitUntil(const std::function<bool()> &predicate, int timeout = 10000)
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

template<class T> T *control(QObject *parent, const char *name)
{
    return parent ? parent->findChild<T *>(QString::fromLatin1(name)) : nullptr;
}

void reveal(QWidget *widget)
{
    // 先交付换页后排队的焦点恢复，再按本次操作目标滚动；模拟真实下一次输入。
    QCoreApplication::processEvents();
    for (QWidget *parent = widget ? widget->parentWidget() : nullptr; parent; parent = parent->parentWidget())
    {
        if (auto *scroll = qobject_cast<QScrollArea *>(parent)) scroll->ensureWidgetVisible(widget, 12, 12);
        // 顶层详情/文件框不受其逻辑父页面的viewport裁切，不反向滚动隐藏的主壳。
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

bool paste(QLineEdit *edit, const QString &text, bool keyboardOnly = false)
{
    if (!edit || !edit->isEnabled() || edit->isReadOnly()) return false;
    reveal(edit);
    QTest::mouseClick(edit, Qt::LeftButton);
    QTest::keyClick(edit, Qt::Key_A, Qt::ControlModifier);
    if (text.isEmpty()) QTest::keyClick(edit, Qt::Key_Backspace);
    else if (keyboardOnly)
    {
        for (QChar character : text) if (character.unicode() > 127) return false;
        QTest::keyClicks(edit, text);
    }
    else
    {
        QApplication::clipboard()->setText(text);
        QTest::keyClick(edit, Qt::Key_V, Qt::ControlModifier);
    }
    return edit->text() == text;
}

std::shared_ptr<bool> answerNext(QWidget *owner, QMessageBox::StandardButton answer)
{
    auto seen = std::make_shared<bool>(false);
    auto *timer = new QTimer(owner);
    timer->setInterval(5);
    auto deadline = std::make_shared<QElapsedTimer>();
    deadline->start();
    const QPointer<QWidget> sessionOwner(owner);
    QObject::connect(timer, &QTimer::timeout, owner, [=]()
    {
        // QMessageBox/详情属于独立顶层window。QWidget::isAncestorOf在窗口边界
        // 可能返回false；用QObject所有权链确认归属，绝不处理其它Session的模态框。
        QWidget *active = QApplication::activeModalWidget();
        bool owned = false;
        for (QObject *parent = active; parent && sessionOwner; parent = parent->parent())
        {
            if (parent == sessionOwner.data()) { owned = true; break; }
        }
        auto *box = qobject_cast<QMessageBox *>(active);
        if (box && owned && box->button(answer))
        {
            *seen = true;
            timer->stop();
            QTest::mouseClick(box->button(answer), Qt::LeftButton);
            timer->deleteLater();
        }
        else if (deadline->elapsed() > 10000)
        {
            timer->stop();
            qWarning().noquote() << "INTEGRATION_CONFIRMATION_TIMEOUT"
                << "owner=" << (sessionOwner ? sessionOwner->objectName() : QStringLiteral("destroyed"))
                << "active_class=" << (active ? active->metaObject()->className() : "none")
                << "active_object=" << (active ? active->objectName() : QString())
                << "owned=" << owned << "requested_standard_button=" << int(answer);
            // 不打印弹窗正文或账户输入。超时只安全取消本Session的活动对话框，
            // 让外层exec返回并产生明确失败，不能把计时器删掉后永久卡在确认框。
            QTest::qFail("Timed out waiting for the owned confirmation dialog; see INTEGRATION_CONFIRMATION_TIMEOUT", __FILE__, __LINE__);
            if (owned)
            {
                // 超时是测试失败清理，调用基类取消避免详情重载reject再弹一层确认。
                if (auto *dialog = qobject_cast<QDialog *>(active)) dialog->QDialog::reject();
            }
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

class DatabaseConnection
{
public:
    explicit DatabaseConnection(const QString &path, bool readOnly = true)
        : name(QUuid::createUuid().toString()), database(QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name))
    {
        if (readOnly) database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(path);
        database.open();
    }
    ~DatabaseConnection()
    {
        database.close();
        database = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    QString name;
    QSqlDatabase database;
};

QJsonObject patientSnapshot(const QString &path)
{
    DatabaseConnection connection(path);
    QSqlQuery query(connection.database);
    if (!connection.database.isOpen() || !query.exec(QStringLiteral("SELECT * FROM report_data WHERE id=3")) || !query.next()) return {};
    QJsonObject patient;
    const QSqlRecord columns = query.record();
    for (int index = 0; index < columns.count(); ++index)
        patient.insert(columns.fieldName(index), QJsonValue::fromVariant(query.value(index)));
    return patient;
}

bool operationFinished(const QSignalSpy &spy, const QString &operation, bool success)
{
    for (const auto &event : spy)
        if (event.at(0).toString() == operation && event.at(1).toBool() == success) return true;
    return false;
}

bool selectCalendarDay(QDialog *dialog, const char *buttonName, const char *editName)
{
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
    QModelIndex day;
    int count = 0;
    for (int row = 1; row < grid->model()->rowCount(); ++row)
        for (int column = 0; column < grid->model()->columnCount(); ++column)
        {
            const QModelIndex index = grid->model()->index(row, column);
            if (index.data().toString() == QStringLiteral("23") && (index.flags() & Qt::ItemIsSelectable)) { day = index; ++count; }
        }
    if (count != 1) return false;
    QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, grid->visualRect(day).center());
    auto *edit = control<QLineEdit>(dialog, editName);
    return edit && waitUntil([&]() { return edit->text() == QStringLiteral("2026/10/23"); });
}

// 独立解析GBK和CSV引号，不按换行拆记录；匿名姓名本身包含换行。
bool readCsv(const QString &path, QList<QStringList> *rows)
{
    const QByteArray content = bytes(path);
    QTextCodec *codec = QTextCodec::codecForName("GBK");
    if (content.isEmpty() || !codec || content.startsWith("\xEF\xBB\xBF")) return false;
    QTextCodec::ConverterState state;
    const QString text = codec->toUnicode(content.constData(), content.size(), &state);
    if (state.invalidChars) return false;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int index = 0; index < text.size(); ++index)
    {
        const QChar ch = text.at(index);
        if (ch == QLatin1Char('"'))
        {
            if (quoted && index + 1 < text.size() && text.at(index + 1) == ch) { field += ch; ++index; }
            else quoted = !quoted;
        }
        else if (!quoted && ch == QLatin1Char(',')) { row.append(field); field.clear(); }
        else if (!quoted && (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')))
        {
            if (ch == QLatin1Char('\r') && index + 1 < text.size() && text.at(index + 1) == QLatin1Char('\n')) ++index;
            row.append(field); rows->append(row); row.clear(); field.clear();
        }
        else field += ch;
    }
    if (quoted) return false;
    if (!row.isEmpty() || !field.isEmpty()) { row.append(field); rows->append(row); }
    return true;
}

QJsonObject distribution(QVector<double> samples)
{
    std::sort(samples.begin(), samples.end());
    if (samples.isEmpty()) return {{QStringLiteral("samples"), 0}, {QStringLiteral("p95_ms"), QJsonValue()},
        {QStringLiteral("p99_ms"), QJsonValue()}, {QStringLiteral("max_ms"), QJsonValue()}};
    const auto rank = [&](int percent) { return samples.at((samples.size() * percent + 99) / 100 - 1); };
    return {{QStringLiteral("samples"), samples.size()}, {QStringLiteral("p95_ms"), rank(95)},
        {QStringLiteral("p99_ms"), rank(99)}, {QStringLiteral("max_ms"), samples.last()}};
}

#ifdef Q_OS_LINUX
bool nativeClose(qulonglong windowId)
{
    // 只给已按子进程PID定位的窗口发送WM_DELETE_WINDOW，不激活/关闭其它聊天窗口。
    QLibrary library(QStringLiteral("libX11.so.6"));
    if (!library.load()) return false;
    const auto openDisplay = reinterpret_cast<Display *(*)(const char *)>(library.resolve("XOpenDisplay"));
    const auto internAtom = reinterpret_cast<Atom (*)(Display *, const char *, Bool)>(library.resolve("XInternAtom"));
    const auto sendEvent = reinterpret_cast<Status (*)(Display *, Window, Bool, long, XEvent *)>(library.resolve("XSendEvent"));
    const auto flush = reinterpret_cast<int (*)(Display *)>(library.resolve("XFlush"));
    const auto closeDisplay = reinterpret_cast<int (*)(Display *)>(library.resolve("XCloseDisplay"));
    if (!openDisplay || !internAtom || !sendEvent || !flush || !closeDisplay) return false;
    Display *display = openDisplay(nullptr);
    if (!display) return false;
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

struct ChildCleanup
{
    QProcess *process;
    ~ChildCleanup()
    {
        // 失败清理仅作用于本测试启动的子进程；不能记作产品正常退出证据。
        if (process->state() != QProcess::NotRunning)
        {
            process->terminate();
            if (!process->waitForFinished(1000)) { process->kill(); process->waitForFinished(1000); }
        }
    }
};
#endif

class Session
{
public:
    explicit Session(const QString &directory, bool seed)
        : directory(directory), hospital(new HospitalStore), accounts(new AccountStore), history(new HistoryStore)
    {
        HospitalStore::registerTypes(); AccountStore::registerTypes(); HistoryStore::registerTypes();
        const QList<QPair<QObject *, QThread *>> workers = {{hospital, &hospitalThread}, {accounts, &accountThread}, {history, &historyThread}};
        for (const auto &worker : workers)
        {
            worker.first->moveToThread(worker.second);
            QObject::connect(worker.second, &QThread::finished, worker.first, &QObject::deleteLater);
        }
        window = new frmMain(hospital, nullptr, accounts);
        window->setDataDirectory(QDir(directory).filePath(QStringLiteral("hospital")));
        page = new HistoryPage(history);
        page->setDataDirectory(QDir(directory).filePath(QStringLiteral("history")));
        window->attachHistoryPage(page);
        host.setWidget(window);
        host.setWidgetResizable(false);
        host.resize(1600, 900);
        operations.reset(new QSignalSpy(page, &HistoryPage::operationFinished));
        hospitalOperations.reset(new QSignalSpy(window->hospitalPage(), &frmHospInfoSetting::operationFinished));
        sessions.reset(new QSignalSpy(window, &frmMain::sessionChanged));
        hospitalClosed.reset(new QSignalSpy(hospital, &HospitalStore::closed));
        accountsClosed.reset(new QSignalSpy(accounts, &AccountStore::closed));
        historyClosed.reset(new QSignalSpy(history, &HistoryStore::closed));
        const QString secret = seed ? QUuid::createUuid().toString(QUuid::WithoutBraces) : QString();
        QObject::connect(&hospitalThread, &QThread::started, hospital, [=]()
        {
            const QDir folder(QDir(directory).filePath(QStringLiteral("hospital")));
            hospital->initialize(folder.filePath(QStringLiteral("hospital_Information.db")), folder.filePath(QStringLiteral("config.ini")));
        });
        QObject::connect(&accountThread, &QThread::started, accounts, [=]()
        { accounts->initialize(QDir(directory).filePath(QStringLiteral("account")), secret); });
        QObject::connect(&historyThread, &QThread::started, history, [=]()
        { history->initialize(QDir(directory).filePath(QStringLiteral("history"))); });
        host.show();
        hospitalThread.start(); accountThread.start(); historyThread.start();
    }
    ~Session()
    {
        stopWorkers();
        host.takeWidget();
        delete window;
    }
    bool ready()
    {
        return waitUntil([&]() { return window->hospitalPage()->isReady() && !window->hospitalPage()->isBusy()
            && window->accountPage()->isReady() && !window->accountPage()->isBusy() && page->isReady() && !page->isBusy(); });
    }
    bool stopWorkers()
    {
        if (stopped) return closedNormally;
        for (const auto &worker : QList<QPair<QObject *, QThread *>>{{hospital, &hospitalThread}, {accounts, &accountThread}, {history, &historyThread}})
        {
            if (worker.second->isRunning())
            {
                QMetaObject::invokeMethod(worker.first, "close", Qt::BlockingQueuedConnection);
                worker.second->quit();
                if (!worker.second->wait(5000)) return false;
            }
        }
        QCoreApplication::processEvents();
        stopped = true;
        closedNormally = hospitalClosed->count() == 1 && accountsClosed->count() == 1 && historyClosed->count() == 1;
        return closedNormally;
    }
    bool login(const QString &name, int role)
    {
        sessions->clear();
        if (!QMetaObject::invokeMethod(accounts, "setSession", Qt::QueuedConnection, Q_ARG(QString, name), Q_ARG(bool, true))) return false;
        return waitUntil([&]() { return !sessions->isEmpty() && window->accountSession().name == name
            && window->accountSession().role == role && !window->accountPage()->isBusy(); });
    }
    bool navigate(int main, int user = -1)
    {
        const char *names[] = {"btnReagentAdptive", "btnSampleAdptive", "btnReportAdptive", "btnCalibrateAdptive",
            "btnQualityAdptive", "btnSystemAdptive", "btnUserSettingAdptive", "btnDivStatusAdptive"};
        if (main < 0 || main > 7 || !click(control<QAbstractButton>(window, names[main]))) return false;
        if (window->currentMainPage() != main) return false;
        if (main == 6 && user >= 0)
        {
            if (!click(control<QAbstractButton>(window, user == 0 ? "btnAccount" : "btnHospInfo"))) return false;
            return window->currentUserPage() == user;
        }
        return true;
    }
    QTableWidget *table() const { return control<QTableWidget>(page, "tableResultReportOne"); }
    bool query(bool sampleThree, bool keyboardOnly = false)
    {
        operations->clear();
        if (!click(control<QAbstractButton>(page, "btnQuery"))) return false;
        auto *dialog = control<QDialog>(page, "MaskDlgQueryReport");
        if (!dialog || !waitUntil([=]() { return dialog->isVisible(); })) return false;
        if (!selectCalendarDay(dialog, "btnCalendarStart", "ldtStartDate") ||
            !selectCalendarDay(dialog, "btnCalendarEnd", "ldtEndDate")) return false;
        auto *date = control<QCheckBox>(dialog, "ckxByDate");
        auto *code = control<QCheckBox>(dialog, "ckxBySampleCode");
        if (!date || !code) return false;
        if (!date->isChecked() && !click(date)) return false;
        if (code->isChecked() != sampleThree && !click(code)) return false;
        if (!paste(control<QLineEdit>(dialog, "ldtSampleID"), sampleThree ? QStringLiteral("3") : QString(), keyboardOnly) ||
            !paste(control<QLineEdit>(dialog, "ldtSampleCode"), sampleThree ? kBarcode : QString(), keyboardOnly) ||
            !click(control<QAbstractButton>(dialog, "btnSave"))) return false;
        return waitUntil([&]() { return !page->isBusy() && operationFinished(*operations, QStringLiteral("query"), true); });
    }
    QDialog *detail()
    {
        for (int row = 0; row < table()->rowCount(); ++row)
        {
            if (table()->item(row, 0)->text().toInt() == 3)
            {
                const QModelIndex index = table()->model()->index(row, 1);
                table()->scrollTo(index); reveal(table());
                QTest::mouseClick(table()->viewport(), Qt::LeftButton, Qt::NoModifier, table()->visualRect(index).center());
                break;
            }
        }
        if (!click(control<QAbstractButton>(page, "btnDetail"))) return nullptr;
        QDialog *dialog = nullptr;
        if (!waitUntil([&]() { dialog = control<QDialog>(page, "MaskDlgReportDetail"); return dialog && dialog->isVisible() && !page->isBusy(); })) return nullptr;
        if (!click(control<QAbstractButton>(dialog, "btnMenuInfo"))) return nullptr;
        return dialog;
    }
    bool saveAge(QDialog *dialog, int age, bool success = true)
    {
        operations->clear();
        if (!paste(control<QLineEdit>(dialog, "ldtAge"), QString::number(age)) || !click(control<QAbstractButton>(dialog, "btnSave"))) return false;
        return waitUntil([&]() { return !page->isBusy() && operationFinished(*operations, QStringLiteral("savePatient"), success); });
    }
    bool exportCsv(const QString &path, bool all)
    {
        operations->clear();
        if (!click(control<QAbstractButton>(page, "btnExport"))) return false;
        auto *range = control<QDialog>(page, "MaskDlgExport");
        if (!range || !click(control<QAbstractButton>(range, all ? "ckxByAll" : "ckxBySelect")) || !click(control<QAbstractButton>(range, "btnSave"))) return false;
        QFileDialog *chooser = nullptr;
        if (!waitUntil([&]() { chooser = control<QFileDialog>(page, "historyCsvFileDialog"); return chooser && chooser->isVisible(); })) return false;
        chooser->selectFile(path);
        if (!QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection)) return false;
        return waitUntil([&]() { return !page->isBusy() && operationFinished(*operations, QStringLiteral("exportCsv"), true); });
    }
    QString historyPath() const { return QDir(directory).filePath(QStringLiteral("history/history.sqlite")); }
    QString directory;
    QThread hospitalThread, accountThread, historyThread;
    HospitalStore *hospital;
    AccountStore *accounts;
    HistoryStore *history;
    frmMain *window;
    HistoryPage *page;
    QScrollArea host;
    std::unique_ptr<QSignalSpy> operations, hospitalOperations, sessions, hospitalClosed, accountsClosed, historyClosed;
    bool stopped = false;
    bool closedNormally = false;
};
}

class IntegratedShellTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void writeExperiment();
    void readExperiment();
    void measureExperiment();
    void applicationExitExperiment();
    void viewportExperiment();
    void initializationFailureExperiment();
private:
    QString directory;
};

void IntegratedShellTest::initTestCase()
{
    directory = QString::fromUtf8(qgetenv("SN100_INTEGRATION_EVIDENCE_DIR"));
    QVERIFY2(!directory.isEmpty() && QDir::isAbsolutePath(directory), "Set a new absolute SN100_INTEGRATION_EVIDENCE_DIR");
    QVERIFY(!directory.toLower().contains(QStringLiteral("sn100_host_reference")));
    QVERIFY(QDir().mkpath(directory));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QApplication::setQuitOnLastWindowClosed(false);
}

void IntegratedShellTest::writeExperiment()
{
    for (const QString &name : {QStringLiteral("hospital"), QStringLiteral("account"), QStringLiteral("history")})
        QVERIFY2(!QFileInfo::exists(QDir(directory).filePath(name)), "Refusing to reuse previous integration data");
    Session session(directory, true);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QCOMPARE(session.window->historyPage(), session.page);
    QVERIFY(!session.window->accountSession().authenticated);
    auto *hospitalPage = session.window->hospitalPage();
    auto *accountPage = session.window->accountPage();
    QVERIFY(session.navigate(2));
    QVERIFY(session.query(false));
    QCOMPARE(session.table()->rowCount(), 3);
    QVERIFY(session.query(true));
    QCOMPARE(session.table()->rowCount(), 1);
    QCOMPARE(session.table()->item(0, 1)->text(), kBarcode);
    const QJsonObject initialPatient = patientSnapshot(session.historyPath());
    QVERIFY(!initialPatient.isEmpty());
    QDialog *detail = session.detail();
    QVERIFY(detail);
    QVERIFY(session.saveAge(detail, 42));
    QJsonObject expectedPatient = initialPatient;
    expectedPatient.insert(QStringLiteral("age"), 42);
    QCOMPARE(patientSnapshot(session.historyPath()), expectedPatient);
    QVERIFY(detail->close());

    QVERIFY(session.navigate(6, 1));
    auto *hospitalName = control<QLineEdit>(hospitalPage, "ldtHospName");
    const QString initialHospital = kSavedHospital + QStringLiteral(" 初存");
    QVERIFY(paste(hospitalName, initialHospital));
    session.hospitalOperations->clear();
    QVERIFY(click(control<QAbstractButton>(hospitalPage, "btnSave")));
    QVERIFY(waitUntil([&]() { return !hospitalPage->isBusy() && operationFinished(*session.hospitalOperations, QStringLiteral("save"), true); }));
    // 真实医院写锁期间，注销/导航/关闭均须拒绝，不能先清身份或部分提交。
    QVERIFY(session.login(QStringLiteral("offlineEngineer"), 4));
    const QByteArray hospitalBeforeLock = bytes(QDir(directory).filePath(QStringLiteral("hospital/hospital_Information.db")));
    {
        DatabaseConnection lock(QDir(directory).filePath(QStringLiteral("hospital/hospital_Information.db")), false);
        QVERIFY(lock.database.isOpen());
        QSqlQuery query(lock.database);
        QVERIFY(query.exec(QStringLiteral("BEGIN IMMEDIATE")));
        session.hospitalOperations->clear();
        QVERIFY(paste(hospitalName, kSavedHospital));
        QVERIFY(click(control<QAbstractButton>(hospitalPage, "btnSave")));
        QVERIFY(hospitalPage->isBusy());
        bool guardsSeen = false, logoutAccepted = true, navigationAccepted = true, closeAccepted = true;
        QTimer::singleShot(0, session.window, [&]()
        {
            logoutAccepted = session.window->requestLogout();
            navigationAccepted = session.window->selectMainPage(2);
            closeAccepted = session.window->close();
            guardsSeen = true;
        });
        QVERIFY(waitUntil([&]() { return guardsSeen && !hospitalPage->isBusy()
            && operationFinished(*session.hospitalOperations, QStringLiteral("save"), false); }));
        QVERIFY(!logoutAccepted && !navigationAccepted && !closeAccepted);
        QCOMPARE(session.window->currentMainPage(), 6);
        QCOMPARE(session.window->currentUserPage(), 1);
        QVERIFY(session.window->accountSession().authenticated);
        QCOMPARE(session.window->accountSession().name, QStringLiteral("offlineEngineer"));
        QCOMPARE(session.window->accountSession().role, 4);
        QCOMPARE(bytes(QDir(directory).filePath(QStringLiteral("hospital/hospital_Information.db"))), hospitalBeforeLock);
        QCOMPARE(hospitalName->text(), kSavedHospital);
        QVERIFY(query.exec(QStringLiteral("ROLLBACK")));
        session.hospitalOperations->clear();
        QVERIFY(click(control<QAbstractButton>(hospitalPage, "btnSave")));
        QVERIFY(waitUntil([&]() { return !hospitalPage->isBusy()
            && operationFinished(*session.hospitalOperations, QStringLiteral("save"), true); }));
    }
    QVERIFY(bytes(QDir(directory).filePath(QStringLiteral("hospital/hospital_Information.db"))) != hospitalBeforeLock);
    const QByteArray hospitalBeforeDraft = bytes(QDir(directory).filePath(QStringLiteral("hospital/hospital_Information.db")));
    QVERIFY(paste(hospitalName, QStringLiteral("不得写盘的医院草稿")));
    auto keepHospital = answerNext(session.window, QMessageBox::No);
    QVERIFY(!session.window->selectMainPage(2));
    QVERIFY(*keepHospital);
    QVERIFY(hospitalPage->hasUnsavedChanges());
    auto discardHospital = answerNext(session.window, QMessageBox::Yes);
    QVERIFY(session.window->selectMainPage(2));
    QVERIFY(*discardHospital);
    QCOMPARE(hospitalName->text(), kSavedHospital);
    QCOMPARE(bytes(QDir(directory).filePath(QStringLiteral("hospital/hospital_Information.db"))), hospitalBeforeDraft);

    detail = session.detail();
    QVERIFY(detail);
    const QByteArray historyBeforeDraft = bytes(session.historyPath());
    QVERIFY(paste(control<QLineEdit>(detail, "ldtAge"), QStringLiteral("88")));
    auto keepHistory = answerNext(session.window, QMessageBox::Cancel);
    QVERIFY(!session.window->selectMainPage(6));
    QVERIFY(*keepHistory);
    QVERIFY(session.page->hasUnsavedChanges());
    auto discardHistory = answerNext(session.window, QMessageBox::Discard);
    QVERIFY(session.window->selectMainPage(6));
    QVERIFY(*discardHistory);
    QCOMPARE(bytes(session.historyPath()), historyBeforeDraft);
    QCOMPARE(patientSnapshot(session.historyPath()), expectedPatient);

    const QStringList actors = {QStringLiteral("offlineSend"), QStringLiteral("offlineTest"), QStringLiteral("offlineCheck"), QStringLiteral("offlineEngineer"), QStringLiteral("offlineAdmin")};
    QJsonArray roleSets;
    for (int index = 0; index < actors.size(); ++index)
    {
        QVERIFY(session.navigate(6, 0));
        QVERIFY(session.login(actors.at(index), index + 1));
        QVERIFY(session.window->accountSession().authenticated);
        auto *label = control<QLabel>(session.window, "lblUserName");
        QVERIFY(label && label->toolTip().contains(actors.at(index)));
        QVERIFY(label->toolTip().contains(AccountStore::roleName(index + 1)));
        QCOMPARE(control<QComboBox>(accountPage, "cbxTypeSelectAdd")->count(), qMin(index + 1, 4));
        QCOMPARE(control<QComboBox>(accountPage, "cbxTypeSelectUpdate")->currentIndex(), index == 4 ? -1 : index);
        QStringList actualSet, expectedSet;
        auto *table = control<QTableWidget>(accountPage, "tbl_Account");
        QVERIFY(table && !table->isHidden());
        for (int row = 0; row < table->rowCount(); ++row)
        {
            QVERIFY(table->item(row, 1) && table->item(row, 2));
            actualSet.append(table->item(row, 1)->text() + QChar('|') + table->item(row, 2)->text());
        }
        // 原规则：角色1..4均只显示1..3；管理员显示1..4，不用简单“<=当前角色”推断。
        for (int candidate = 1; candidate <= (index == 4 ? 4 : 3); ++candidate)
            expectedSet.append(AccountStore::roleName(candidate) + QChar('|') + actors.at(candidate - 1));
        std::sort(actualSet.begin(), actualSet.end());
        std::sort(expectedSet.begin(), expectedSet.end());
        QCOMPARE(actualSet, expectedSet);
        roleSets.append(QJsonObject{{QStringLiteral("actor"), actors.at(index)}, {QStringLiteral("role"), index + 1},
            {QStringLiteral("actual_public_name_role_set"), QJsonArray::fromStringList(actualSet)}});
        QVERIFY(session.window->requestLogout());
        QVERIFY(waitUntil([&]() { return !session.window->accountSession().authenticated && !accountPage->isBusy(); }));
        QCOMPARE(session.window->accountSession().role, 0);
        QVERIFY(label->text().contains(QStringLiteral("Guest")) || label->toolTip().contains(QStringLiteral("Guest")));
        QCOMPARE(control<QComboBox>(accountPage, "cbxTypeSelectAdd")->count(), 0);
        QVERIFY(control<QTableWidget>(accountPage, "tbl_Account")->isHidden());
        QCOMPARE(control<QTableWidget>(accountPage, "tbl_Account")->rowCount(), 0);
    }
    // 原报告操作没有Guest门禁；注销后仍按原规则可查询/编辑/导出。
    QVERIFY(session.navigate(2));
    QVERIFY(session.query(true));
    for (int round = 0; round < 100; ++round)
    {
        QVERIFY(session.navigate(6, 0)); QVERIFY(session.navigate(6, 1)); QVERIFY(session.navigate(2));
        QCOMPARE(session.window->accountPage(), accountPage);
        QCOMPARE(session.window->hospitalPage(), hospitalPage);
        QCOMPARE(session.window->historyPage(), session.page);
        QCOMPARE(session.page->currentFilter().sampleNumber, QStringLiteral("3"));
        QCOMPARE(session.page->currentFilter().barcode, kBarcode);
    }
    QVERIFY(session.login(QStringLiteral("offlineEngineer"), 4));
    detail = session.detail();
    QVERIFY(detail);
    {
        DatabaseConnection lock(session.historyPath(), false);
        QVERIFY(lock.database.isOpen());
        QSqlQuery query(lock.database);
        QVERIFY(query.exec(QStringLiteral("BEGIN IMMEDIATE")));
        session.operations->clear();
        QVERIFY(paste(control<QLineEdit>(detail, "ldtAge"), QString::number(kFinalAge)));
        QVERIFY(click(control<QAbstractButton>(detail, "btnSave")));
        QVERIFY(session.page->isBusy());
        bool guardsSeen = false, navigationAccepted = true, logoutAccepted = true, closeAccepted = true;
        QTimer::singleShot(0, session.window, [&]()
        {
            navigationAccepted = session.window->selectMainPage(6);
            logoutAccepted = session.window->requestLogout();
            closeAccepted = session.window->close();
            guardsSeen = true;
        });
        QVERIFY(waitUntil([&]() { return guardsSeen && !session.page->isBusy() && operationFinished(*session.operations, QStringLiteral("savePatient"), false); }));
        QVERIFY(!navigationAccepted && !logoutAccepted && !closeAccepted);
        QCOMPARE(session.window->currentMainPage(), 2);
        QVERIFY(session.window->accountSession().authenticated);
        QCOMPARE(patientSnapshot(session.historyPath()), expectedPatient);
        QVERIFY(query.exec(QStringLiteral("ROLLBACK")));
        QVERIFY(session.saveAge(detail, kFinalAge));
        expectedPatient.insert(QStringLiteral("age"), kFinalAge);
        QCOMPARE(patientSnapshot(session.historyPath()), expectedPatient);
    }
    QVERIFY(detail->close());
    QVERIFY(session.window->requestLogout());
    QVERIFY(waitUntil([&]() { return !session.window->accountSession().authenticated; }));
    QVERIFY(session.query(false));
    const QString allCsv = QDir(directory).filePath(QStringLiteral("history/exports/integrated_all.csv"));
    QVERIFY(session.exportCsv(allCsv, true));
    QList<QStringList> allRows;
    QVERIFY(readCsv(allCsv, &allRows));
    QCOMPARE(allRows.size(), 5);
    QVERIFY(allRows.first().contains(QStringLiteral("样本条码")));
    bool sawQuotedBarcode = false;
    for (const auto &row : allRows) if (row.contains(kBarcode)) sawQuotedBarcode = true;
    QVERIFY(sawQuotedBarcode);
    for (int row = 0; row < session.table()->rowCount(); ++row)
    {
        auto *item = session.table()->item(row, 0);
        const Qt::CheckState expected = item->text() == QStringLiteral("3") ? Qt::Checked : Qt::Unchecked;
        if (item->checkState() != expected)
        {
            session.table()->setCurrentCell(row, 0);
            reveal(session.table());
            QTest::keyClick(session.table(), Qt::Key_Space);
        }
        QCOMPARE(item->checkState(), expected);
    }
    const QString selectedCsv = QDir(directory).filePath(QStringLiteral("history/exports/integrated_selected.csv"));
    QVERIFY(session.exportCsv(selectedCsv, false));
    QList<QStringList> selectedRows;
    QVERIFY(readCsv(selectedCsv, &selectedRows));
    QCOMPARE(selectedRows.size(), 3);
    for (int row = 1; row < selectedRows.size(); ++row) QCOMPARE(selectedRows.at(row).first(), QStringLiteral("3"));
    const QByteArray beforeOverwrite = bytes(allCsv);
    session.operations->clear();
    session.page->exportCsv(allCsv, true); // 明确覆盖负例直接覆盖公开槽，不能通过文件框覆盖旧文件。
    QVERIFY(waitUntil([&]() { return !session.page->isBusy() && operationFinished(*session.operations, QStringLiteral("exportCsv"), false); }));
    QCOMPARE(bytes(allCsv), beforeOverwrite);
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("write_result.json")), {
        {QStringLiteral("patient_before"), initialPatient}, {QStringLiteral("patient_after"), expectedPatient},
        {QStringLiteral("writer_process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("hospital_name"), kSavedHospital}, {QStringLiteral("navigation_rounds"), 100},
        {QStringLiteral("hospital_locked_save_guards_and_retry"), true}, {QStringLiteral("account_public_sets"), roleSets},
        {QStringLiteral("roles_checked"), 6}, {QStringLiteral("csv_all_records"), allRows.size() - 1},
        {QStringLiteral("csv_selected_records"), selectedRows.size() - 1},
        {QStringLiteral("all_csv_sha256"), QString::fromLatin1(QCryptographicHash::hash(beforeOverwrite, QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("partial_commit_detected"), false}, {QStringLiteral("credentials_exported"), false}}));
    QVERIFY(session.window->close());
    QVERIFY(session.stopWorkers());
}

void IntegratedShellTest::readExperiment()
{
    QVERIFY2(QFileInfo::exists(QDir(directory).filePath(QStringLiteral("write_result.json"))), "Run writeExperiment in a separate process first");
    const QJsonObject written = QJsonDocument::fromJson(bytes(QDir(directory).filePath(QStringLiteral("write_result.json")))).object();
    QVERIFY(written.value(QStringLiteral("writer_process_id")).toDouble() > 0.0);
    QVERIFY2(written.value(QStringLiteral("writer_process_id")).toDouble() != double(QCoreApplication::applicationPid()), "readExperiment must run in a new process");
    Session session(directory, false);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(!session.window->accountSession().authenticated);
    QVERIFY(session.navigate(6, 1));
    QCOMPARE(control<QLineEdit>(session.window->hospitalPage(), "ldtHospName")->text(), kSavedHospital);
    QCOMPARE(patientSnapshot(session.historyPath()), written.value(QStringLiteral("patient_after")).toObject());
    QVERIFY(session.navigate(2));
    QVERIFY(session.query(true));
    QCOMPARE(session.table()->rowCount(), 1);
    QDialog *detail = session.detail();
    QVERIFY(detail);
    QCOMPARE(control<QLineEdit>(detail, "ldtAge")->text(), QString::number(kFinalAge));
    QVERIFY(detail->close());
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("read_result.json")), {
        {QStringLiteral("independent_process_readback_equal"), true},
        {QStringLiteral("guest_after_restart"), true}, {QStringLiteral("age"), kFinalAge},
        {QStringLiteral("hospital_name"), kSavedHospital}, {QStringLiteral("credentials_exported"), false}}));
    QVERIFY(session.window->close());
    QVERIFY(session.stopWorkers());
}

void IntegratedShellTest::measureExperiment()
{
#ifndef QT_NO_DEBUG
    QSKIP("Build Release before running the timed workload");
#endif
    QVERIFY2(QFileInfo::exists(QDir(directory).filePath(QStringLiteral("write_result.json"))), "Use the saved write directory in a separate process");
    const QJsonObject written = QJsonDocument::fromJson(bytes(QDir(directory).filePath(QStringLiteral("write_result.json")))).object();
    QVERIFY(written.value(QStringLiteral("writer_process_id")).toDouble() > 0.0);
    QVERIFY2(written.value(QStringLiteral("writer_process_id")).toDouble() != double(QCoreApplication::applicationPid()), "measureExperiment must run in a new process");
    QVERIFY2(!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("measurement_result.json"))), "Do not overwrite an earlier measurement");
    Session session(directory, false);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(&session.host));
    QVERIFY(session.navigate(2));
    // 此负载字段全为ASCII，用实际按键输入与WSLg剪贴板所有权分离。
    // 中文/引号/换行剪贴板和持久化仍由write/read独立功能用例覆盖。
    QVERIFY(session.query(true, true));
    QVector<double> heartbeatGaps, navigationTimes, queryTimes, saveTimes;
    int failed = 0;
    QString failedStage;
    int rounds = 0;
    QElapsedTimer workload;
    workload.start();
    qint64 lastHeartbeat = 0;
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(10);
    connect(&heartbeat, &QTimer::timeout, this, [&]()
    {
        const qint64 now = workload.nsecsElapsed();
        heartbeatGaps.append(double(now - lastHeartbeat) / 1000000.0);
        lastHeartbeat = now;
    });
    heartbeat.start();
    const auto navigation = [&](int main, int user)
    {
        QElapsedTimer clock;
        clock.start();
        double feedback = -1.0;
        const auto connection = connect(session.window, &frmMain::navigationChanged, this,
            [&](int actualMain, int actualUser)
        {
            if (feedback < 0.0 && actualMain == main && (user < 0 || actualUser == user))
                feedback = double(clock.nsecsElapsed()) / 1000000.0;
        });
        const bool accepted = session.navigate(main, user);
        disconnect(connection);
        if (accepted && feedback >= 0.0) navigationTimes.append(feedback);
        return accepted && feedback >= 0.0;
    };
    // 固定60秒混合操作，无截图、PNG编码、另进程SQL核验或资源采样开销。
    while (workload.elapsed() < 60000)
    {
        if (!navigation(6, 0) || !navigation(6, 1) || !navigation(2, -1))
        { ++failed; failedStage = QStringLiteral("navigation"); break; }
        session.operations->clear();
        QElapsedTimer queryClock;
        queryClock.start();
        session.page->refresh();
        if (!waitUntil([&]() { return !session.page->isBusy() && operationFinished(*session.operations, QStringLiteral("query"), true); }))
        { ++failed; failedStage = QStringLiteral("refresh"); break; }
        queryTimes.append(double(queryClock.nsecsElapsed()) / 1000000.0);
        session.operations->clear();
        if (rounds % 10 == 0)
        {
            QDialog *detail = session.detail();
            if (!detail || !paste(control<QLineEdit>(detail, "ldtAge"), QString::number(kFinalAge), true))
            { ++failed; failedStage = QStringLiteral("detail_input"); break; }
            session.operations->clear();
            QElapsedTimer saveClock;
            saveClock.start();
            if (!click(control<QAbstractButton>(detail, "btnSave")) ||
                !waitUntil([&]() { return !session.page->isBusy() && operationFinished(*session.operations, QStringLiteral("savePatient"), true); }))
            { ++failed; failedStage = QStringLiteral("save_patient"); break; }
            saveTimes.append(double(saveClock.nsecsElapsed()) / 1000000.0);
            session.operations->clear();
            if (!detail->close()) { ++failed; failedStage = QStringLiteral("detail_close"); break; }
        }
        ++rounds;
    }
    heartbeat.stop();
    const double elapsed = double(workload.nsecsElapsed()) / 1000000.0;
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("measurement_result.json")), {
        {QStringLiteral("condition"), QStringLiteral("60-second real-Widgets three-page workload; ASCII key events without clipboard; no screenshot/PNG; separate from product PID resource sampling")},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("test_process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("elapsed_ms"), elapsed}, {QStringLiteral("completed_rounds"), rounds},
        {QStringLiteral("failed"), failed}, {QStringLiteral("failed_stage"), failedStage},
        {QStringLiteral("navigation_signal_feedback"), distribution(navigationTimes)},
        {QStringLiteral("query_completion"), distribution(queryTimes)},
        {QStringLiteral("save_completion"), distribution(saveTimes)},
        {QStringLiteral("heartbeat_interval_ms"), 10}, {QStringLiteral("heartbeat_gap"), distribution(heartbeatGaps)},
        {QStringLiteral("timing_scope"), QStringLiteral("navigation: UI injection to matching navigationChanged; query: refresh request to completion; save: save-button injection to committed completion. No compositor/pixel or RK guarantee.")}}));
    QCOMPARE(failed, 0);
    QVERIFY(elapsed >= 60000.0);
    QVERIFY(!navigationTimes.isEmpty() && !queryTimes.isEmpty() && !saveTimes.isEmpty() && !heartbeatGaps.isEmpty());
    QVERIFY(session.window->close());
    QVERIFY(session.stopWorkers());
}

void IntegratedShellTest::applicationExitExperiment()
{
#ifndef Q_OS_LINUX
    QSKIP("PID-targeted native X11 close requires Linux/WSLg/XCB");
#else
    if (QGuiApplication::platformName() != QStringLiteral("xcb")) QSKIP("Native product close evidence requires the XCB platform");
    const QString application = qEnvironmentVariable("SN100_SHELL_APP");
    QVERIFY2(QFileInfo(application).isAbsolute() && QFileInfo(application).isExecutable(), "SN100_SHELL_APP must name the fresh final product");
    const QString finder = QStandardPaths::findExecutable(QStringLiteral("xdotool"));
    QVERIFY2(!finder.isEmpty(), "xdotool is required to discover only the owned product PID window");
    const QString runtime = QDir(directory).filePath(QStringLiteral("application_exit_runtime"));
    QVERIFY2(!QFileInfo::exists(runtime), "Formal product runtime must be new");
    QVERIFY2(!QFileInfo::exists(QDir(directory).filePath(QStringLiteral("ready_pid.json"))), "Do not overwrite an earlier PID handoff");
    QTemporaryFile secret(QDir(directory).filePath(QStringLiteral("application_fixture_XXXXXX.secret")));
    QVERIFY(secret.open());
    const QByteArray privateInput = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    QCOMPARE(secret.write(privateInput), qint64(privateInput.size()));
    QVERIFY(secret.flush());
    const QString secretPath = secret.fileName();
    secret.close();
    QProcess process;
    ChildCleanup cleanup{&process};
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(application, {QStringLiteral("--offline"), QStringLiteral("--accounts"), QStringLiteral("--history"),
        QStringLiteral("--data-dir"), runtime, QStringLiteral("--seed-anonymous"),
        QStringLiteral("--test-secret-file"), secretPath, QStringLiteral("--viewport"), QStringLiteral("1600x900")});
    QVERIFY(process.waitForStarted(10000));
    const qint64 pid = process.processId();
    QByteArray output;
    bool credentialObserved = false;
    const auto collectOutput = [&]()
    {
        output += process.readAll();
        if (output.contains(privateInput)) credentialObserved = true;
        output.replace(privateInput, QByteArray("<redacted>"));
    };
    QVERIFY(waitUntil([&]() { collectOutput(); return output.contains("SHELL_READY") || process.state() == QProcess::NotRunning; }));
    QVERIFY2(output.contains("SHELL_READY") && process.state() != QProcess::NotRunning, output.constData());
    QVERIFY(secret.remove());
    QByteArray windowId;
    QVERIFY(waitUntil([&]()
    {
        QProcess search;
        search.start(finder, {QStringLiteral("search"), QStringLiteral("--onlyvisible"), QStringLiteral("--pid"), QString::number(pid)});
        if (!search.waitForFinished(1000)) return false;
        windowId = search.readAllStandardOutput().trimmed().split('\n').value(0);
        return search.exitCode() == 0 && !windowId.isEmpty();
    }));
    bool validId = false;
    const qulonglong nativeId = windowId.toULongLong(&validId);
    QVERIFY(validId && nativeId > 0);
    QProcess owner;
    owner.start(finder, {QStringLiteral("getwindowpid"), QString::fromLatin1(windowId)});
    QVERIFY(owner.waitForFinished(1000));
    QCOMPARE(owner.exitCode(), 0);
    QCOMPARE(owner.readAllStandardOutput().trimmed().toLongLong(), pid);
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("ready_pid.json")), {
        {QStringLiteral("application_process_id"), double(pid)}, {QStringLiteral("test_process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("native_window_id"), QString::number(nativeId)},
        {QStringLiteral("application"), application},
        {QStringLiteral("application_sha256"), QString::fromLatin1(QCryptographicHash::hash(bytes(application), QCryptographicHash::Sha256).toHex())},
        {QStringLiteral("runtime"), runtime}, {QStringLiteral("requested_viewport"), QStringLiteral("1600x900")},
        {QStringLiteral("ready"), true}, {QStringLiteral("idle_observation_seconds"), 12},
        {QStringLiteral("mode"), QStringLiteral("--offline --accounts --history; default Guest; transient secret removed; no instrument")}}));
    QElapsedTimer idle;
    idle.start();
    // 让外部10秒资源采样在正常关闭前完成；就绪文件发现和启动观察器也耗时。
    while (idle.elapsed() < 12000 && process.state() != QProcess::NotRunning) QTest::qWait(50);
    QVERIFY2(process.state() != QProcess::NotRunning, "Product exited during ready idle observation");
    const double observedIdleMilliseconds = double(idle.elapsed());
    QVERIFY(nativeClose(nativeId));
    QVERIFY(waitUntil([&]() { collectOutput(); return process.state() == QProcess::NotRunning; }));
    collectOutput();
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 0);
    QVERIFY2(output.contains("HOSPITAL_STORE_CLOSED"), output.constData());
    QVERIFY2(output.contains("ACCOUNT_STORE_CLOSED"), output.constData());
    QVERIFY2(output.contains("HISTORY_STORE_CLOSED"), output.constData());
    QCOMPARE(output.count("HISTORY_INITIAL_QUERY_READY"), 1);
    QCOMPARE(output.count("SHELL_READY"), 1);
    QVERIFY(output.indexOf("HISTORY_INITIAL_QUERY_READY") < output.indexOf("SHELL_READY"));
    QVERIFY2(output.contains("THREADS_STOPPED true"), output.constData());
    QVERIFY2(QRegularExpression(QStringLiteral("EXIT code=\\s*0(?:\\s|$)")).match(QString::fromUtf8(output)).hasMatch(), output.constData());
    QVERIFY2(!credentialObserved, "Product output exposed transient credential material; captured evidence was redacted");
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("application_exit_result.json")), {
        {QStringLiteral("application_process_id"), double(pid)}, {QStringLiteral("idle_observed_ms"), observedIdleMilliseconds},
        {QStringLiteral("exit_code"), process.exitCode()}, {QStringLiteral("all_three_workers_closed_and_stopped"), true},
        {QStringLiteral("history_initial_query_ready_before_shell_ready"), true},
        {QStringLiteral("close_injection"), QStringLiteral("X11 WM_DELETE_WINDOW to independently PID-verified visible product window")},
        {QStringLiteral("credentials_exported"), false}, {QStringLiteral("actual_output"), QString::fromUtf8(output)}}));
#endif
}

void IntegratedShellTest::viewportExperiment()
{
    QJsonArray resolutions;
    for (const QSize &requested : {QSize(1600, 900), QSize(1920, 1080)})
    {
        const QString name = QStringLiteral("viewport_%1x%2").arg(requested.width()).arg(requested.height());
        const QString runtime = QDir(directory).filePath(name + QStringLiteral("_runtime"));
        QVERIFY2(!QFileInfo::exists(runtime), "Viewport checks require fresh anonymous runtime data");
        Session session(runtime, true);
        session.host.resize(requested);
        session.host.move(0, 0);
        session.host.raise();
        session.host.activateWindow();
        QVERIFY(session.ready());
        QVERIFY(QTest::qWaitForWindowExposed(&session.host));
        QCOMPARE(session.host.size(), requested);
        QVERIFY(session.login(QStringLiteral("offlineEngineer"), 4));
        QJsonArray hits;
        QStringList focusTrail;
        QObject focusRecorder; // 先于focusTrail析构，避免关闭Session时回调已释放的记录。
        connect(qApp, &QApplication::focusChanged, &focusRecorder, [&](QWidget *, QWidget *now)
        {
            focusTrail.append(now ? now->objectName() : QStringLiteral("none"));
        });
        const auto accessible = [&](QWidget *widget)
        {
            if (!widget) return false;
            // isVisible()先于X11窗口映射完成；真实命中检查须等目标顶层窗激活。
            QWidget *targetWindow = widget->window();
            targetWindow->raise();
            targetWindow->activateWindow();
            if (!QTest::qWaitForWindowExposed(targetWindow)
                || !QTest::qWaitForWindowActive(targetWindow)) return false;
            reveal(widget);
            const QPoint center = widget->rect().center();
            QWidget *hit = nullptr;
            const bool hitMatches = waitUntil([&]()
            {
                hit = QApplication::widgetAt(widget->mapToGlobal(center));
                return hit && (hit == widget || widget->isAncestorOf(hit));
            }, 3000);
            const QPoint global = widget->mapToGlobal(center);
            const QRect globalRectangle(widget->mapToGlobal(QPoint(0, 0)), widget->size());
            QScreen *screen = QGuiApplication::screenAt(global);
            bool wholeVisible = widget->isVisible() && widget->visibleRegion().contains(widget->rect())
                && screen && screen->availableGeometry().contains(globalRectangle);
            QJsonArray scrollPositions;
            for (QWidget *parent = widget->parentWidget(); parent; parent = parent->parentWidget())
            {
                if (auto *scroll = qobject_cast<QScrollArea *>(parent))
                {
                    const QRect viewportGlobal(scroll->viewport()->mapToGlobal(QPoint(0, 0)), scroll->viewport()->size());
                    wholeVisible = wholeVisible && viewportGlobal.contains(globalRectangle);
                    scrollPositions.append(QJsonObject{{QStringLiteral("object"), scroll->objectName()},
                        {QStringLiteral("horizontal"), scroll->horizontalScrollBar()->value()},
                        {QStringLiteral("vertical"), scroll->verticalScrollBar()->value()},
                        {QStringLiteral("horizontal_max"), scroll->horizontalScrollBar()->maximum()},
                        {QStringLiteral("vertical_max"), scroll->verticalScrollBar()->maximum()}});
                }
                if (parent->isWindow()) break;
            }
            hits.append(QJsonObject{{QStringLiteral("object"), widget->objectName()},
                {QStringLiteral("whole_control_visible"), wholeVisible},
                {QStringLiteral("actual_widget_at_click_matches"), hitMatches},
                {QStringLiteral("click_global_x"), global.x()}, {QStringLiteral("click_global_y"), global.y()},
                {QStringLiteral("scroll_positions"), scrollPositions}});
            if (!wholeVisible || !hitMatches)
            {
                qWarning() << "VIEWPORT_REACHABILITY_FAILURE" << widget->objectName()
                    << "rectangle=" << globalRectangle << "whole_visible=" << wholeVisible
                    << "hit=" << (hit ? hit->objectName() : QStringLiteral("none"))
                    << "screen=" << (screen ? screen->availableGeometry() : QRect())
                    << QJsonDocument(scrollPositions).toJson(QJsonDocument::Compact);
            }
            return wholeVisible && hitMatches;
        };
        const auto accessibleClick = [&](QObject *owner, const char *objectName)
        {
            auto *button = control<QAbstractButton>(owner, objectName);
            return accessible(button) && click(button);
        };
        const auto screenshot = [&](const QString &page)
        {
            const QString path = QDir(directory).filePath(name + QChar('_') + page + QStringLiteral(".png"));
            return !QFileInfo::exists(path) && session.host.grab().save(path);
        };

        QVERIFY(accessibleClick(session.window, "btnUserSettingAdptive"));
        QVERIFY(accessibleClick(session.window, "btnHospInfo"));
        auto *hospitalName = control<QLineEdit>(session.window->hospitalPage(), "ldtHospName");
        QVERIFY(accessible(hospitalName));
        QTest::mouseClick(hospitalName, Qt::LeftButton);
        QVERIFY(waitUntil([&]() { return QApplication::focusWidget() == hospitalName; }));
        QTest::keyClick(hospitalName, Qt::Key_Tab);
        QWidget *tabFocus = QApplication::focusWidget();
        QVERIFY(tabFocus && tabFocus->isVisible() && session.window->isAncestorOf(tabFocus));
        QTest::mouseClick(hospitalName, Qt::LeftButton);
        QVERIFY(screenshot(QStringLiteral("hospital")));

        QVERIFY(accessibleClick(session.window, "btnAccount"));
        auto *accountName = control<QLineEdit>(session.window->accountPage(), "ldtUserNameAdd");
        QVERIFY(accessible(accountName));
        QTest::mouseClick(accountName, Qt::LeftButton);
        QVERIFY(waitUntil([&]() { return QApplication::focusWidget() == accountName; }));
        QVERIFY(screenshot(QStringLiteral("account")));

        QVERIFY(accessibleClick(session.window, "btnReportAdptive"));
        QVERIFY(accessible(control<QAbstractButton>(session.page, "btnQuery")));
        QVERIFY(session.query(true));
        QCOMPARE(session.table()->rowCount(), 1);
        QVERIFY(accessible(control<QAbstractButton>(session.page, "btnDetail")));
        QDialog *detail = session.detail();
        QVERIFY(detail);
        auto *age = control<QLineEdit>(detail, "ldtAge");
        QVERIFY(accessible(age));
        QTest::mouseClick(age, Qt::LeftButton);
        QVERIFY(waitUntil([&]() { return QApplication::focusWidget() == age; }));
        // 嵌入详情中的Esc必须走外层守卫，不能只隐藏业务内容而遗留模态窗口。
        QTest::keyClick(age, Qt::Key_Escape);
        QVERIFY(waitUntil([&]() { return !detail->isVisible(); }));
        QVERIFY(!QApplication::activeModalWidget());
        QVERIFY(accessible(control<QAbstractButton>(session.page, "btnExport")));
        QVERIFY(screenshot(QStringLiteral("history")));

        QVERIFY(accessibleClick(session.window, "btnUserSettingAdptive"));
        QVERIFY(accessibleClick(session.window, "btnHospInfo"));
        QCOMPARE(session.window->currentMainPage(), 6);
        QCOMPARE(session.window->currentUserPage(), 1);
        session.host.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&session.host));
        const bool restored = waitUntil([&]() { return QApplication::focusWidget() == hospitalName; });
        if (!restored) qWarning() << "HOSPITAL_FOCUS_RESTORE_FAILURE"
            << (QApplication::focusWidget() ? QApplication::focusWidget()->objectName() : QStringLiteral("none"))
            << "hospital_visible=" << hospitalName->isVisible() << "host_active=" << session.host.isActiveWindow()
            << "active=" << (QApplication::activeWindow() ? QApplication::activeWindow()->objectName() : QStringLiteral("none"))
            << focusTrail;
        QVERIFY(restored);
        resolutions.append(QJsonObject{{QStringLiteral("requested_width"), requested.width()},
            {QStringLiteral("requested_height"), requested.height()},
            {QStringLiteral("actual_width"), session.host.width()}, {QStringLiteral("actual_height"), session.host.height()},
            {QStringLiteral("reachability"), hits}, {QStringLiteral("hospital_focus_restored"), true},
            {QStringLiteral("detail_escape_closed_outer_dialog"), true}});
        QVERIFY(session.window->close());
        QVERIFY(session.stopWorkers());
    }
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("viewport_result.json")), {
        {QStringLiteral("scope"), QStringLiteral("Real migrated Widgets only: scrolling, whole-control visibility, hit target, focus and Escape; screenshots are not original-Windows parity evidence")},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("resolutions"), resolutions}, {QStringLiteral("original_visual_comparison_performed"), false}}));
}

void IntegratedShellTest::initializationFailureExperiment()
{
#ifndef Q_OS_LINUX
    QSKIP("Formal three-worker initialization failure evidence uses the Linux product");
#else
    const QString application = qEnvironmentVariable("SN100_SHELL_APP");
    QVERIFY(QFileInfo(application).isAbsolute() && QFileInfo(application).isExecutable());
    const QString runtime = QDir(directory).filePath(QStringLiteral("initialization_failure_runtime"));
    QVERIFY2(!QFileInfo::exists(runtime), "Failure injection must only create a fresh dedicated runtime");
    const QString history = QDir(runtime).filePath(QStringLiteral("history"));
    QVERIFY(QDir().mkpath(history));
    const QString sentinelPath = QDir(history).filePath(QStringLiteral("unmarked-content.txt"));
    QFile sentinel(sentinelPath);
    QVERIFY(sentinel.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    const QByteArray sentinelContent("Deliberately invalid anonymous test directory; preserve this file.\n");
    QCOMPARE(sentinel.write(sentinelContent), qint64(sentinelContent.size()));
    sentinel.close();
    QTemporaryFile secret(QDir(directory).filePath(QStringLiteral("failure_fixture_XXXXXX.secret")));
    QVERIFY(secret.open());
    const QByteArray privateInput = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
    QCOMPARE(secret.write(privateInput), qint64(privateInput.size()));
    QVERIFY(secret.flush());
    const QString secretPath = secret.fileName();
    secret.close();
    QProcess process;
    ChildCleanup cleanup{&process};
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(application, {QStringLiteral("--offline"), QStringLiteral("--accounts"), QStringLiteral("--history"),
        QStringLiteral("--data-dir"), runtime, QStringLiteral("--seed-anonymous"),
        QStringLiteral("--test-secret-file"), secretPath, QStringLiteral("--viewport"), QStringLiteral("1600x900")});
    QVERIFY(process.waitForStarted(10000));
    const qint64 pid = process.processId();
    QVERIFY(waitUntil([&]() { return process.state() == QProcess::NotRunning; }));
    QByteArray output = process.readAll();
    const bool credentialObserved = output.contains(privateInput);
    output.replace(privateInput, QByteArray("<redacted>"));
    QVERIFY(secret.remove());
    QCOMPARE(process.exitStatus(), QProcess::NormalExit);
    QCOMPARE(process.exitCode(), 3);
    QVERIFY2(output.contains("HISTORY_ERROR"), output.constData());
    QVERIFY2(!output.contains("SHELL_READY"), output.constData());
    QVERIFY2(output.contains("HOSPITAL_STORE_CLOSED") && output.contains("ACCOUNT_STORE_CLOSED")
        && output.contains("HISTORY_STORE_CLOSED") && output.contains("THREADS_STOPPED true"), output.constData());
    QCOMPARE(bytes(sentinelPath), sentinelContent);
    QVERIFY(!QFileInfo::exists(QDir(history).filePath(QStringLiteral("history.sqlite"))));
    QVERIFY2(!credentialObserved, "Product output exposed a transient secret; evidence output was redacted");
    QVERIFY(saveJson(QDir(directory).filePath(QStringLiteral("initialization_failure_result.json")), {
        {QStringLiteral("application_process_id"), double(pid)}, {QStringLiteral("exit_code"), 3},
        {QStringLiteral("failure_input"), QStringLiteral("Dedicated new history directory contains a nonempty unmarked sentinel")},
        {QStringLiteral("sentinel_unchanged"), true}, {QStringLiteral("all_three_workers_closed_and_stopped"), true},
        {QStringLiteral("ready_was_not_reported"), true}, {QStringLiteral("credentials_exported"), false},
        {QStringLiteral("actual_output"), QString::fromUtf8(output)}}));
#endif
}

QTEST_MAIN(IntegratedShellTest)
#include "integrated_shell_test.moc"
