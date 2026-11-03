// 与正式main一起链接，只在板B的原资料和实际主控连接上核对界面，不生成账户或模拟报文。
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMovie>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QDebug>
#include <functional>
#include <algorithm>
#include <memory>

#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "shell/startup_splash.h"
#include "instrument/device_control.h"
#include "instrument/instrument_page.h"
#include "ui/maskdlglogin.h"
#include "ui/frmaccountsetting.h"
#include "ui/cusdialogmask.h"

namespace
{
bool until(const std::function<bool()> &condition, int timeout = 10000)
{
    QEventLoop loop;
    QTimer tick, deadline;
    tick.setInterval(20);
    deadline.setSingleShot(true);
    QObject::connect(&tick, &QTimer::timeout, &loop, [&]() { if (condition()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (condition()) return true;
    tick.start(); deadline.start(timeout); loop.exec();
    return condition();
}

void delay(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

// 检查结果与主程序退出码分开；必须让真实窗口守卫排空告警接收器。
void finishReview(int checkCode)
{
    if (qApp->property("connectedReviewClosing").toBool()) return;
    qApp->setProperty("connectedReviewClosing", true);
    qApp->setProperty("connectedReviewInitialCheckExit", checkCode);
    const QString output = qEnvironmentVariable("SN100_CONNECTED_REVIEW_DIR");
    QFile checkExit(QDir(output).filePath("check_exit.txt"));
    if (QDir::isAbsolutePath(output) && checkExit.open(QIODevice::WriteOnly))
        checkExit.write(QByteArray::number(checkCode) + "\n");
    else
        qCritical() << "REVIEW_CHECK_EXIT_SAVE_FAILED" << checkCode;
    qInfo() << "REVIEW_CHECK_EXIT" << checkCode;
    auto clock = std::make_shared<QElapsedTimer>();
    clock->start();
    auto *closer = new QTimer(qApp);
    closer->setInterval(100);
    QObject::connect(closer, &QTimer::timeout, qApp, [closer, clock]()
    {
        if (clock->elapsed() > 15000)
        {
            // 不调用exit、直接停receiver或自动确认草稿；保留失败交外层时限处理。
            qCritical() << "REVIEW_WINDOW_CLOSE_NOT_CONFIRMED";
            const int initialCode = qApp->property("connectedReviewInitialCheckExit").toInt();
            const int failureCode = initialCode ? initialCode : 4;
            QFile checkExit(QDir(qEnvironmentVariable("SN100_CONNECTED_REVIEW_DIR")).filePath("check_exit.txt"));
            if (checkExit.open(QIODevice::WriteOnly)) checkExit.write(QByteArray::number(failureCode) + "\n");
            qWarning() << "REVIEW_CHECK_EXIT_UPDATED" << failureCode;
            closer->stop();
            closer->deleteLater();
            return;
        }
        frmMain *shell = nullptr;
        for (QWidget *widget : QApplication::allWidgets())
            if ((shell = qobject_cast<frmMain *>(widget))) break;
        if (!shell) return;
        for (auto *dialog : shell->findChildren<MaskDlgLogin *>())
        {
            if (!dialog->isVisible()) continue;
            if (!dialog->isBusy()) dialog->reject();
            return; // 等原关闭动画完成；不强删登录卡或其提示。
        }
        if (QApplication::activeModalWidget()) return;
        QWidget *window = shell->window();
        if (!window->isVisible())
        {
            closer->stop();
            closer->deleteLater();
            return;
        }
        // 告警排空时正式代码会禁用主窗，并在stopped后自行再次close。
        if (!shell->isEnabled()) return;
        const bool accepted = window->close();
        qInfo() << "REVIEW_WINDOW_CLOSE_REQUEST accepted=" << accepted;
        if (accepted)
        {
            closer->stop();
            closer->deleteLater();
        }
    });
    closer->start();
}

void review()
{
    const QString output = qEnvironmentVariable("SN100_CONNECTED_REVIEW_DIR");
    QFile serial(QStringLiteral("/proc/device-tree/serial-number"));
    if (!QDir::isAbsolutePath(output) || !serial.open(QIODevice::ReadOnly)
        || serial.readAll().replace('\0', "") != "8314b1e8ffd0321f")
    { qCritical() << "CONNECTED_REVIEW_REQUIRES_BOARD_B_AND_OUTPUT"; finishReview(2); return; }
    if (QGuiApplication::platformName() != QStringLiteral("eglfs"))
    { qCritical() << "REVIEW_REQUIRES_EGLFS"; finishReview(2); return; }
    qInfo().noquote() << "REVIEW_RELEASE_ID" << SN100_REVIEW_RELEASE;
    if (qEnvironmentVariable("SN100_EXPECTED_RELEASE_ID") != QStringLiteral(SN100_REVIEW_RELEASE))
    { qCritical() << "REVIEW_RELEASE_BINDING_MISMATCH"; finishReview(2); return; }
    QDir().mkpath(output);
    QJsonArray checks;
    QJsonArray timings;
    int failed = 0;
    const auto check = [&](bool accepted, const QString &name)
    {
        checks.append(QJsonObject{{"name", name}, {"passed", accepted}});
        if (!accepted) ++failed;
        qInfo().noquote() << (accepted ? "PASS" : "FAIL") << name;
    };
    frmMain *shell = nullptr;
    for (QWidget *widget : QApplication::allWidgets())
        if ((shell = qobject_cast<frmMain *>(widget))) break;
    if (!shell || !shell->deviceControl() || !shell->instrumentPage())
    { qCritical() << "CONNECTED_REVIEW_REQUIRES_FULL_PRODUCTION_SHELL"; finishReview(2); return; }
    auto *control = shell->deviceControl();
    QList<QByteArray> sent;
    QObject reviewContext;
    QElapsedTimer eventClock, actionClock;
    eventClock.start();
    QList<qint64> eventIntervals;
    qint64 previousEvent = 0;
    QTimer eventProbe;
    eventProbe.setTimerType(Qt::PreciseTimer);
    eventProbe.setInterval(20);
    QObject::connect(&eventProbe, &QTimer::timeout, &reviewContext, [&]()
    {
        const qint64 now = eventClock.elapsed();
        eventIntervals.append(now - previousEvent); previousEvent = now;
    });
    eventProbe.start();
    const auto wireConnection = QObject::connect(control, &DeviceControl::wireActivity, &reviewContext,
        [&](const QString &direction, const QByteArray &bytes) { if (direction.startsWith("TX")) sent.append(bytes); });
    const auto stopEarly = [&](const QString &reason)
    {
        check(false, reason);
        control->close();
        QFile file(QDir(output).filePath("result.json"));
        if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(QJsonObject{
            {"board", "board_b"}, {"checks", checks}, {"failed", failed}, {"completed", false}}).toJson());
        finishReview(1);
    };
    const auto login = [&]() -> MaskDlgLogin *
    {
        for (auto *dialog : shell->findChildren<MaskDlgLogin *>()) if (dialog->isVisible()) return dialog;
        return nullptr;
    };
    const auto loginReady = [&]()
    {
        auto *dialog = login();
        auto *mask = dialog ? dialog->findChild<CusDialogMask *>("mask") : nullptr;
        auto *button = dialog ? dialog->findChild<QPushButton *>("btnLogin") : nullptr;
        return dialog && dialog->isVisible() && !dialog->isBusy() && mask && mask->isReadyForInput()
            && button && button->isEnabled();
    };
    if (!until([&]() { return loginReady() && control->state().connected && control->state().fresh; })
        || shell->accountSession().authenticated)
    {
        qWarning().noquote() << "CONNECTED_REVIEW_STARTUP_STATE"
            << "connected=" << control->state().connected << "fresh=" << control->state().fresh
            << "login_visible=" << (login() != nullptr) << "account_busy=" << shell->accountPage()->isBusy()
            << "instrument_busy=" << shell->instrumentPage()->isBusy()
            << "configuration=" << shell->instrumentPage()->property("connectionConfigurationFile").toString()
            << "detail=" << control->state().message;
        stopEarly(QStringLiteral("需要原配置自动登录卡、实际主控状态及尚未认证的会话")); return;
    }
    check(true, QStringLiteral("原配置启动后自动打开可输入的登录卡，并收到实际主控状态"));
    const QString address = control->state().address;
    const quint16 port = control->state().port;
    check(control->state().status.model == QStringLiteral("SN500"), QStringLiteral("当前实际连接为SN500"));
    check(QMovie::supportedFormats().contains("gif"), QStringLiteral("RK的GIF解码组件可用"));
    auto *splash = shell->window()->findChild<QWidget *>("startupSplash");
    check(splash && splash->property("startupAnimationValid").toBool() && !splash->isVisible(),
        QStringLiteral("原启动GIF已成功读取，数据就绪后结束"));
    auto *status = shell->findChild<QLabel *>("lblDivState");
    check(status && !status->text().isEmpty() && status->height() >= status->fontMetrics().height(),
        QStringLiteral("机型下方状态有文字，控件高度容纳Linux中文字体"));
    if (auto *header = shell->findChild<QWidget *>("statusArea"))
        header->grab().save(QDir(output).filePath("connected_header.png"));
    if (loginReady())
    {
        auto *card = login()->findChild<QWidget *>("view_frame");
        const QRect cardArea = card ? QRect(shell->window()->mapFromGlobal(card->mapToGlobal(QPoint())), card->size()) : QRect();
        check(card && shell->window()->rect().contains(cardArea), QStringLiteral("登录卡完整位于当前窗口对象范围内"));
        shell->window()->grab().save(QDir(output).filePath("startup_login.png"));
        login()->reject();
        check(until([&]() { return !login() || !login()->isVisible(); }), QStringLiteral("程序内取消后登录卡关闭"));
        delay(1200);
        check(!login(), QStringLiteral("取消后继续处理页面通知1.2秒，没有旧启动意图重新弹卡"));
    }
    // 真实断开一次TCP后核灰灯；不停止主控，不发送开始、暂停、归位或写入命令。
    control->close();
    if (!until([&]() { return !control->state().connected; }))
    { stopEarly(QStringLiteral("真实TCP必须先断开，登录核对期间不能发生资料同步")); return; }
    delay(130);
    auto *lamp = shell->findChild<QWidget *>("wgtDivStateLamp");
    const QImage first = lamp ? lamp->grab().toImage() : QImage();
    delay(320);
    const QImage second = lamp ? lamp->grab().toImage() : QImage();
    first.save(QDir(output).filePath("disconnected_lamp_first.png"));
    second.save(QDir(output).filePath("disconnected_lamp_second.png"));
    check(shell->property("instrumentStatusText").toString() == QStringLiteral("等待连接")
        && shell->property("instrumentStatusLamp").toString().endsWith("status_off.png")
        && shell->property("instrumentStatusLampFlashing").toBool() && !first.isNull() && first != second,
        QStringLiteral("实际断开后显示等待连接，灰灯两次控件渲染不同；完整明暗周期和实屏仍需人工核对"));
    if (lamp) lamp->grab().save(QDir(output).filePath("disconnected_lamp.png"));
    if (auto *header = shell->findChild<QWidget *>("statusArea"))
        header->grab().save(QDir(output).filePath("disconnected_header.png"));
    // 使用原登录窗口的空口令规则，得到INI明确指定的Guest默认角色；不设置模拟身份。
    auto *userButton = shell->findChild<CusBadgeButton *>("btnUser");
    if (!userButton || !userButton->isEnabled())
    { stopEarly(QStringLiteral("取消登录后头像按钮必须恢复可点")); return; }
    actionClock.start();
    const QPointF clickPoint = userButton->rect().center();
    QMouseEvent press(QEvent::MouseButtonPress, clickPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(userButton, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, clickPoint, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(userButton, &release);
    if (!until(loginReady))
    { stopEarly(QStringLiteral("点击头像后必须重新打开可输入的登录卡")); return; }
    timings.append(QJsonObject{{"action", "avatar_click_to_login_input_ready"}, {"ms", actionClock.elapsed()}});
    check(true, QStringLiteral("取消后可程序内触发控件头像重新打开登录卡"));
    if (loginReady())
    {
        login()->findChild<QLineEdit *>("ldtPassword")->clear();
        actionClock.restart();
        login()->findChild<QPushButton *>("btnLogin")->click();
        if (!until([&]() { return shell->accountSession().authenticated && !login(); })
            || shell->accountSession().role != 0 || shell->accountSession().source != AccountSessionSource::Configured)
        { stopEarly(QStringLiteral("原窗口空口令必须只得到INI指定的Configured访客角色0")); return; }
        timings.append(QJsonObject{{"action", "configured_guest_login_to_dialog_closed"}, {"ms", actionClock.elapsed()}});
        check(true, QStringLiteral("原窗口空口令建立配置默认Guest会话，未创建或修改账户，权限保持0"));
        auto *logoutButton = shell->findChild<QPushButton *>("btnLoginOut");
        if (!logoutButton || !until([&]() { return logoutButton->isEnabled(); }))
        { stopEarly(QStringLiteral("会话建立后注销按钮必须恢复可点")); return; }
        actionClock.restart();
        logoutButton->click();
        if (!until([&]() { return !shell->accountSession().authenticated && loginReady(); }))
        { stopEarly(QStringLiteral("程序内触发控件注销后必须清除身份并重开可输入的登录卡")); return; }
        timings.append(QJsonObject{{"action", "logout_click_to_login_input_ready"}, {"ms", actionClock.elapsed()}});
        check(true, QStringLiteral("程序内触发控件注销后账户回读完成，自动重开可输入的登录卡"));
        check(!shell->accountSession().authenticated, QStringLiteral("注销已清除实际身份"));
        if (loginReady()) login()->reject();
        until([&]() { return !login() || !login()->isVisible(); });
    }
    check(shell->selectMainPage(7), QStringLiteral("访客可进入原仪器状态页"));
    auto *instrument = shell->instrumentPage();
    bool adminHidden = true;
    for (const char *name : {"btnPort", "btnFunTest", "btn_CommunicationTest", "btn_board_status"})
    {
        auto *button = instrument->findChild<QPushButton *>(name);
        adminHidden = adminHidden && button && !button->isVisibleTo(instrument);
    }
    check(adminHidden, QStringLiteral("访客未显示管理员端口、功能、通信及板卡专属入口"));
    if (shell->accountSession().authenticated)
    { stopEarly(QStringLiteral("恢复真实主控连接前必须已实际注销")); return; }
    check(control->open(address, port) && until([&]() { return control->state().fresh && control->state().connected; }),
        QStringLiteral("恢复原INI中的实际主控连接，收到新状态"));
    const QString realText = shell->property("instrumentStatusText").toString();
    bool navigationPreserved = !realText.isEmpty();
    for (int page : {0, 1, 2, 3, 4, 5, 6, 7})
    {
        const bool entered = until([&]() { return shell->selectMainPage(page); }, 3000);
        delay(80);
        navigationPreserved = navigationPreserved && entered
            && shell->property("instrumentStatusText").toString() == status->text()
            && !status->text().contains(QStringLiteral("脱机"));
    }
    check(navigationPreserved, QStringLiteral("切换八个主页后仪器状态仍显示，未被本地业务提示覆盖"));
    const auto shown = [instrument](const char *name)
    { auto *label = instrument->findChild<QLabel *>(name); return label ? label->text() : QString(); };
    check(shown("lblDriverModel") == control->state().status.model
        && shown("lbl6Main_reagentScaner_6") == control->state().status.model
        && shown("lbl6Main_subSystemCount") == QString::number(control->state().status.subsystems.size())
        && shown("lbl6Main_mainPeriod") == control->state().status.generalFields.value(18),
        QStringLiteral("仪器页面的机型、子系统数、主控周期与实际Qj字段相同"));
    check(!control->state().status.subsystems.isEmpty()
        || (shown("lbl6Select_EnvirTemp") == QStringLiteral("未上报") && shown("lblEnvirTemp") == QStringLiteral("未上报")),
        QStringLiteral("当前主控未发子系统时，环境温度明确显示未上报"));
    instrument->grab().save(QDir(output).filePath("instrument_actual_status.png"));
    QJsonObject result{{"board", "board_b"}, {"checks", checks}, {"failed", failed},
        {"completed", true}, {"release_id", QStringLiteral(SN100_REVIEW_RELEASE)},
        {"evidence_scope", "production_objects_programmatic_widget_events_and_widget_grab"},
        {"physical_mouse_keyboard_verified", false}, {"physical_screen_verified", false},
        {"normal_window_close_reopen_verified", false},
        {"qt", QString::fromLatin1(qVersion())}, {"platform", QGuiApplication::platformName()},
        {"window_width", shell->window()->width()}, {"window_height", shell->window()->height()},
        {"status_label_height", status ? status->height() : 0}, {"status_font_height", status ? status->fontMetrics().height() : 0},
        {"main_status", control->state().status.mainStatus}, {"led_status", control->state().status.ledStatus},
        {"subsystems", control->state().status.subsystems.size()}, {"actual_status_text", realText},
        {"administrator_password_login", "pending_user_real_credentials"}, {"testing_green_animation", "pending_actual_testing_state"}};
    control->close();
    const auto expectedQuery = Sn100Protocol::encodeMessage("Qj", Sn100Protocol::Direction::HostToDevice);
    bool onlyQueries = !sent.isEmpty() && expectedQuery.size() == 1;
    for (const QByteArray &bytes : sent) onlyQueries = onlyQueries && bytes == expectedQuery.first();
    check(onlyQueries, QStringLiteral("就绪后观察区间发送字节均为Qj；完整启动起TX另由日志审核"));
    result["checks"] = checks; result["failed"] = failed;
    result["operation_response_ms"] = timings;
    eventProbe.stop();
    std::sort(eventIntervals.begin(), eventIntervals.end());
    result["event_loop_probe"] = QJsonObject{{"period_ms", 20}, {"samples", eventIntervals.size()},
        {"interval_p95_ms", eventIntervals.isEmpty() ? 0 : eventIntervals.at((eventIntervals.size() - 1) * 95 / 100)},
        {"interval_max_ms", eventIntervals.isEmpty() ? 0 : eventIntervals.last()}};
    QObject::disconnect(wireConnection);
    QFile file(QDir(output).filePath("result.json"));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(result).toJson()) < 0) ++failed;
    qInfo() << "CONNECTED_REVIEW_RESULT failed=" << failed;
    // 与正式main共用退出和数据库线程回收；检查工具不留后台窗口。
    finishReview(failed ? 1 : 0);
}

void scheduleReview()
{
    // 第三方控件初始化期间也可能处理事件；不能在这里嵌套等待而挡住正式main继续启动。
    auto *poll = new QTimer(qApp);
    poll->setInterval(50);
    QObject::connect(poll, &QTimer::timeout, qApp, [poll]()
    {
        if (qApp->property("connectedReviewClosing").toBool()) { poll->stop(); return; }
        for (QWidget *widget : QApplication::allWidgets())
        {
            auto *shell = qobject_cast<frmMain *>(widget);
            if (!shell || !shell->deviceControl() || !shell->instrumentPage()) continue;
            auto *splash = shell->window()->findChild<QWidget *>("startupSplash");
            // finish只在正式入口全部数据就绪时调用；不以“已经收到回包”掩盖连接或登录缺陷。
            if (!splash || !splash->property("startupAnimationValid").toBool() || splash->isVisible()) continue;
            poll->stop();
            QTimer::singleShot(500, qApp, review);
            return;
        }
    });
    poll->start();
    QTimer::singleShot(60000, qApp, [poll]()
    {
        if (poll->isActive())
        {
            poll->stop();
            qCritical() << "CONNECTED_REVIEW_STARTUP_NOT_READY";
            finishReview(1);
        }
    });
}
}

Q_COREAPP_STARTUP_FUNCTION(scheduleReview)
