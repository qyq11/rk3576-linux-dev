// 与正式main一起链接，只在板B的原资料和实际主控连接上核对界面，不生成账户或模拟报文。
#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QSet>
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
#include <QTableWidget>
#include "ad_wire_observer.h"
#include "lis_connection_observer_09.h"
#include "current_alarm_observer_09.h"
#include <QDebug>
#include <functional>
#include <algorithm>
#include <memory>

#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "shell/startup_splash.h"
#include "instrument/device_control.h"
#include "instrument/instrument_page.h"
#include "samples/sample_page.h"
#include "original_files/original_runtime_settings.h"
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


// 只读启动就绪后的干净急诊快照；不证明直接取得main中的局部bool。
QByteArray emergencyObservationFileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return {};
    return hash.result().toHex();
}

QByteArray emergencyObservationStartupHash(const QString &output, const QString &path)
{
    QFile manifest(QDir::cleanPath(QDir(output).filePath(QStringLiteral("../ini_before.sha256"))));
    if (!manifest.open(QIODevice::ReadOnly)) return {};
    QByteArray found;
    while (!manifest.atEnd())
    {
        QByteArray line = manifest.readLine();
        while (line.endsWith('\n') || line.endsWith('\r')) line.chop(1);
        if (line.size() < 67 || line.mid(64, 2) != "  " || QString::fromUtf8(line.mid(66)) != path) continue;
        const QByteArray candidate = line.left(64).toLower();
        bool hexadecimal = true;
        for (char c : candidate) hexadecimal = hexadecimal && ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        if (!hexadecimal || !found.isEmpty()) return {};
        found = candidate;
    }
    return found;
}

QJsonObject observeInitialEmergencyContainer(frmMain *shell, const QString &output)
{
    QJsonObject result{{"status", "pending"}, {"evaluated", false}, {"passed", false},
        {"checkpoint", "启动就绪后、检查器首次取消登录卡及页面动作之前"},
        {"expected_source", "同一原资料根只读重验的RuntimeSettings.user.containerIsTube"},
        {"same_main_memory_bool_read", false}, {"sample_identity_or_tasks_recorded", false},
        {"normal_new_rack_verified", false}, {"other_bool_branch_verified", false},
        {"save_reopen_manual_cal_clear_verified", false}, {"physical_screen_verified", false}};
    const auto pending = [&](const QString &reason)
    { QJsonObject value = result; value["reason"] = reason; return value; };
    auto *page = shell ? shell->samplePage() : nullptr;
    auto *control = shell ? shell->deviceControl() : nullptr;
    if (!page || QThread::currentThread() != page->thread() || !page->originalProfile()
        || !page->isReady() || page->isBusy() || page->hasUnsavedChanges())
        return pending(QStringLiteral("原资料样本页尚非就绪、无草稿的GUI线程观察点。"));
    if (!control || !control->state().connected || !control->state().fresh || control->state().status.mainStatus != 0)
        return pending(QStringLiteral("本次实际Qj尚未满足既定空闲观察条件。"));
    const QString root = QDir::cleanPath(page->property("originalRoot").toString());
    if (!QDir::isAbsolutePath(root) || QFileInfo(root).canonicalFilePath() != root)
        return pending(QStringLiteral("无法确认本次原资料根，未猜测其他INI。"));
    const QString config = QDir(root).filePath(QStringLiteral("config.ini"));
    const QString theme = QDir(root).filePath(QStringLiteral("theme_config.ini"));
    const QByteArray configBefore = emergencyObservationFileHash(config);
    const QByteArray themeBefore = emergencyObservationFileHash(theme);
    if (configBefore.isEmpty() || themeBefore.isEmpty()
        || configBefore != emergencyObservationStartupHash(output, config)
        || themeBefore != emergencyObservationStartupHash(output, theme))
        return pending(QStringLiteral("当前INI指纹不等于本次启动前记录，未当作main已验配置。"));
    bool isTube = false;
    int expectedHoles = 0;
    {
        OriginalRuntime::RuntimeSettings settings;
        QString ignoredError;
        if (!OriginalRuntime::loadRuntimeSettings(root, &settings, &ignoredError))
            return pending(QStringLiteral("本次原配置只读重验未通过；未输出其他配置或错误原文。"));
        isTube = settings.user.containerIsTube;
        expectedHoles = settings.theme.emergencyHoles;
        // 不序列化RuntimeSettings；配置值和原INI临时解析结果在本作用域销毁。
    }
    if (emergencyObservationFileHash(config) != configBefore || emergencyObservationFileHash(theme) != themeBefore)
        return pending(QStringLiteral("观察期间INI指纹变化，本次默认容器结果待验。"));
    const SampleSnapshot snapshot = page->snapshot();
    QList<SampleRack> emergencyRacks;
    for (const auto &rack : snapshot.racks) if (rack.emergency) emergencyRacks.append(rack);
    // 遇到已有样本身份、任务或类型转换时，不把其读库/手选/转换结果冒充初建。
    for (const auto &rack : emergencyRacks)
        for (const auto &hole : rack.holes)
            if (!hole.sampleId.isEmpty() || !hole.tasks.isEmpty() || hole.sampleType != 0)
                return pending(QStringLiteral("急诊孔已有样本或任务/转换状态，不能作为干净初建分支。"));
    const int expectedContainer = isTube ? 0 : 1;
    result["evaluated"] = true;
    result["container_is_tube"] = isTube;
    result["expected_container"] = expectedContainer;
    result["expected_emergency_holes"] = expectedHoles;
    result["config_sha256"] = QString::fromLatin1(configBefore);
    result["theme_sha256"] = QString::fromLatin1(themeBefore);
    result["startup_and_observation_ini_hashes_match"] = true;
    result["emergency_rack_count"] = emergencyRacks.size();
    QJsonArray rows;
    bool matches = emergencyRacks.size() == 1 && expectedHoles > 0;
    if (emergencyRacks.size() == 1)
    {
        const auto &rack = emergencyRacks.first();
        matches = matches && rack.id == 1 && rack.name == QStringLiteral("Emergency") && rack.holes.size() == expectedHoles;
        QSet<int> seen;
        for (const auto &hole : rack.holes)
        {
            matches = matches && hole.number >= 1 && hole.number <= expectedHoles
                && !seen.contains(hole.number) && hole.container == expectedContainer;
            seen.insert(hole.number);
            rows.append(QJsonObject{{"hole_number", hole.number}, {"actual_container", hole.container}});
        }
    }
    result["holes"] = rows;
    result["passed"] = matches;
    result["status"] = matches ? QStringLiteral("pass") : QStringLiteral("fail");
    result["scope"] = QStringLiteral("只核当前已验配置分支的干净急诊初建快照；不证明main局部bool直接可见、普通建架、另一bool、保存重开、手选或CAL清空。");
    return result;
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
    QJsonObject adResult{{"status", "pending"}, {"passed", false}};
    QJsonObject emergencyContainerResult{{"status", "pending"}, {"evaluated", false}, {"passed", false}};
    std::shared_ptr<LisConnectionObserver09> lisObserver;
    QJsonObject currentAlarmResult{{"status", "pending"}, {"evaluated", false}, {"passed", false},
        {"reason", "基础观察尚未完成，未触发当前告警查询。"}, {"page_opened", false}};
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
            {"board", "board_b"}, {"checks", checks}, {"failed", failed}, {"completed", false}, {"independent_ad", adResult}, {"emergency_default_container_observation", emergencyContainerResult}, {"lis_connection_observation", lisObserver ? lisObserver->snapshot() : QJsonObject{{"status", "pending"}}}, {"current_alarm_observation", currentAlarmResult}}).toJson());
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
    lisObserver = LisConnectionObserver09::start(shell);
    emergencyContainerResult = observeInitialEmergencyContainer(shell, output);
    if (emergencyContainerResult.value("evaluated").toBool())
        check(emergencyContainerResult.value("passed").toBool(), QStringLiteral("本次已验原INI布尔值与干净急诊初建孔的实际container一致"));
    else
        qInfo().noquote() << "REVIEW_EMERGENCY_CONTAINER_PENDING" << emergencyContainerResult.value("reason").toString();
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
    auto *adTable = instrument->findChild<QTableWidget *>("tableChannel");
    auto adObserver = std::make_shared<AdWireObserver>();
    const QJsonObject cleared = adObserver->compareCleared(adTable);
    adResult.insert("cleared_before_new_connection", cleared);
    check(cleared.value("ok").toBool(), QStringLiteral("旧连接失效后AD数值列为缺值且本次上报标记已清除"));
    if (!cleared.value("ok").toBool())
    { stopEarly(QStringLiteral("AD没有清空，不能用旧连接读数做06通过依据")); return; }
    const quint64 generationBeforeOpen = control->connectionGeneration();
    quint64 observedGeneration = 0;
    const auto adGenerationLink = QObject::connect(control, &DeviceControl::changed, &reviewContext,
        [control, adObserver, &observedGeneration, generationBeforeOpen](const DeviceControlState &state)
    {
        if (!state.connected) return;
        const quint64 generation = control->connectionGeneration();
        if (generation <= generationBeforeOpen || generation == observedGeneration) return;
        observedGeneration = generation;
        adObserver->reset(generation); // 只在真正新connected代次清空独立缓存。
    });
    const auto adRawLink = QObject::connect(control, &DeviceControl::wireActivity, &reviewContext,
        [control, adObserver](const QString &direction, const QByteArray &bytes)
    {
        if (direction == QStringLiteral("RX") && control->state().connected)
            adObserver->feed(bytes, control->connectionGeneration());
    });
    check(control->open(address, port) && until([&]() { return control->state().fresh && control->state().connected; }),
        QStringLiteral("恢复原INI中的实际主控连接，收到新状态"));
    const auto coverage = [&]()
    {
        QJsonArray missing;
        const QJsonObject channels = adObserver->report().value("channels_first_declared_pairs").toObject();
        int supplied = 0;
        for (int channel = 1; channel <= 24; ++channel)
        {
            const QString key = QString::number(channel);
            const bool present = channels.contains(key);
            const QStringList values = present ? channels.value(key).toString().split(QLatin1Char(' '), Qt::KeepEmptyParts) : QStringList();
            if (!present || values.size() < 4) missing.append(key);
            else supplied += 4;
        }
        return QJsonObject{{"ok", missing.isEmpty() && supplied == 96}, {"supplied_cells", supplied},
                           {"missing_or_short_keys", missing}};
    };
    // RX回调先独立记原帧；回到GUI循环后再等生产接收链更新可见表格。
    const bool adCompared = until([&]()
    {
        if (!adObserver->error().isEmpty()) return true;
        if (!control->state().connected || !control->state().fresh || !observedGeneration
            || control->connectionGeneration() != observedGeneration || !adObserver->currentQqReceived()) return false;
        return coverage().value("ok").toBool() && adObserver->compare(adTable).value("ok").toBool();
    }, 10000);
    const QJsonObject comparison = adObserver->compare(adTable);
    const QJsonObject currentCoverage = coverage();
    const bool sameNewConnection = observedGeneration > generationBeforeOpen
        && adObserver->generation() == observedGeneration && control->state().connected && control->state().fresh
        && control->connectionGeneration() == observedGeneration;
    const bool adPassed = adCompared && sameNewConnection && adObserver->error().isEmpty()
        && adObserver->currentQqReceived() && currentCoverage.value("ok").toBool() && comparison.value("ok").toBool();
    adResult.insert("status", adPassed ? "pass" : adObserver->currentQqReceived() ? "fail" : "pending");
    adResult.insert("passed", adPassed);
    adResult.insert("old_generation", QString::number(generationBeforeOpen));
    adResult.insert("new_generation", QString::number(observedGeneration));
    adResult.insert("same_new_connection", sameNewConnection);
    adResult.insert("raw_observation", adObserver->report());
    adResult.insert("current_qq_coverage", currentCoverage);
    adResult.insert("display_comparison", comparison);
    adResult.insert("physical_ad_validity_verified", false);
    check(adPassed, QStringLiteral("本次新连接实际Qq的1至24通道四项原文字与96个本次上报单元格一致"));
    if (!adPassed)
    { stopEarly(QStringLiteral("06端到端AD未通过；缺新Qq、字段不足或显示不符均不借旧读数放行")); return; }
    QJsonArray actualAdRows;
    for (int row = 0; adTable && row < adTable->rowCount(); ++row)
    {
        QJsonArray values, reported;
        for (int column = 2; column < 6; ++column)
        {
            const auto *item = adTable->item(row,column);
            values.append(item ? item->text() : QString());
            reported.append(item && item->data(Qt::UserRole).toBool());
        }
        actualAdRows.append(QJsonObject{{"key", adTable->item(row,0) ? adTable->item(row,0)->text() : QString()},
                                       {"values", values}, {"reported", reported}});
    }
    adResult.insert("table_rows_at_comparison", actualAdRows);
    adTable->grab().save(QDir(output).filePath("ad_current_generation.png"));
    QJsonArray headers;
    for (int column = 0; adTable && column < adTable->columnCount(); ++column)
        headers.append(adTable->horizontalHeaderItem(column) ? adTable->horizontalHeaderItem(column)->text() : QString());
    adResult.insert("observed_headers", headers);
    bool configuredPositionsValid = false, reportedPositionsValid = false;
    const int configuredPositions = shell->property("configuredReagentPositions").toInt(&configuredPositionsValid);
    const int reportedPositions = control->state().status.generalFields.value(20).toInt(&reportedPositionsValid);
    const bool originalReloadExpected = configuredPositionsValid && reportedPositionsValid && configuredPositions != reportedPositions;
    const bool headerBranchPreserved = adTable && adTable->horizontalHeaderItem(2) && adTable->horizontalHeaderItem(5)
        && (originalReloadExpected ? adTable->horizontalHeaderItem(2)->text() == QStringLiteral("450nm")
                                   : QStringList{QStringLiteral("405nm"), QStringLiteral("450nm")}.contains(adTable->horizontalHeaderItem(2)->text()))
        && adTable->horizontalHeaderItem(5)->text() == QStringLiteral("   ");
    check(headerBranchPreserved, QStringLiteral("保留真实Qj触发的405/450表头与24通道空白末列表头，仍核第四个原值"));
    bool baseHidden = true, channelSwitchesProtected = true;
    for (int row = 0; adTable && row < adTable->rowCount(); ++row)
    {
        const QString key = adTable->item(row,0) ? adTable->item(row,0)->text() : QString();
        if (key == QStringLiteral("Base")) baseHidden = false;
        auto *button = qobject_cast<QPushButton *>(adTable->cellWidget(row,1));
        if (!button || button->isEnabled()) channelSwitchesProtected = false;
    }
    check(baseHidden && channelSwitchesProtected, QStringLiteral("未认证角色仍隐藏Base且通道开关不可操作；不要求HIL库行"));
    adResult.insert("guest_base_hidden", baseHidden);
    adResult.insert("channel_switches_protected", channelSwitchesProtected);
    QObject::disconnect(adRawLink);
    QObject::disconnect(adGenerationLink);
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
        {"completed", true}, {"release_id", QStringLiteral(SN100_REVIEW_RELEASE)}, {"independent_ad", adResult}, {"emergency_default_container_observation", emergencyContainerResult},
        {"evidence_scope", "production_objects_programmatic_widget_events_and_widget_grab"},
        {"physical_mouse_keyboard_verified", false}, {"physical_screen_verified", false},
        {"normal_window_close_reopen_verified", false},
        {"qt", QString::fromLatin1(qVersion())}, {"platform", QGuiApplication::platformName()},
        {"window_width", shell->window()->width()}, {"window_height", shell->window()->height()},
        {"status_label_height", status ? status->height() : 0}, {"status_font_height", status ? status->fontMetrics().height() : 0},
        {"main_status", control->state().status.mainStatus}, {"led_status", control->state().status.ledStatus},
        {"subsystems", control->state().status.subsystems.size()}, {"actual_status_text", realText},
        {"administrator_password_login", "pending_user_real_credentials"}, {"testing_green_animation", "pending_actual_testing_state"}};
    const QByteArray expectedQuery = QByteArray::fromHex("0203000600000001516a3a0405");
    bool onlyQueries = !sent.isEmpty();
    for (const QByteArray &bytes : sent) onlyQueries = onlyQueries && bytes == expectedQuery;
    check(onlyQueries, QStringLiteral("就绪后观察区间发送字节均为Qj；完整启动起TX另由日志审核"));
    // 原24项的Qj观察到此结束；当前告警查询不混入原Qq比较窗口。
    eventProbe.stop();
    const QJsonObject lisSnapshot = lisObserver->snapshot();
    const bool lisEvaluated = lisSnapshot.value("status").toString() == QStringLiteral("integration_error")
        || (lisSnapshot.value("configuration_matches_requested_context").toBool()
            && lisSnapshot.value("state").isObject()
            && lisSnapshot.value("connection_and_settings_source_fingerprint_equal").isBool());
    QJsonObject lisResult = lisSnapshot;
    lisResult["software_state_evaluated"] = lisEvaluated;
    const bool lisStatePassed = lisSnapshot.value("status").toString() != QStringLiteral("integration_error")
        && lisSnapshot.value("configuration_matches_requested_context").toBool()
        && lisSnapshot.value("connection_and_settings_source_fingerprint_equal").toBool();
    lisResult["software_state_passed"] = lisEvaluated && lisStatePassed;
    if (lisEvaluated)
        check(lisStatePassed, QStringLiteral("原LIS配置来源与实际连接状态可读；无对端不冒称通信通过"));
    else qInfo() << "REVIEW_LIS_STATE_PENDING";
    result["lis_connection_observation"] = lisResult;
    currentAlarmResult = CurrentAlarmObserver09::run(shell);
    result["current_alarm_observation"] = currentAlarmResult;
    if (currentAlarmResult.value("evaluated").toBool() || currentAlarmResult.value("status").toString() == QStringLiteral("fail"))
        check(currentAlarmResult.value("passed").toBool(), QStringLiteral("明确一次Qk的真实当前告警回包与当前页一致并正常关闭"));
    else qInfo() << "REVIEW_CURRENT_ALARM_PENDING";
    result["new_construction_proven"] = false;
    result["same_main_memory_bool_read"] = false;
    control->close();

    result["checks"] = checks; result["failed"] = failed;
    result["check_count"] = checks.size();
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
