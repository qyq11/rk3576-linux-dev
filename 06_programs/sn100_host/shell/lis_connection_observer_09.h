#ifndef SN100_LIS_CONNECTION_OBSERVER_09_H
#define SN100_LIS_CONNECTION_OBSERVER_09_H

#include "shell/frmmain.h"
#include "shell/widgets/cusbadgebutton.h"
#include "shell/widgets/cusview.h"
#include "ui/frmlissetting.h"
#include "lis_transport/lis_connection_controller.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <memory>

// 仅观察本次已授权的LIS连接。所有调用须在GUI线程；不改变配置或通信状态。
// 原startup检查成功后调用一次：auto observer = LisConnectionObserver09::start(shell);
// 观察结束保存snapshot并stopObserving；快照不证明应用已经退出或正常关闭。
class LisConnectionObserver09
{
public:
    static std::shared_ptr<LisConnectionObserver09> start(frmMain *shell)
    {
        const auto observer = std::shared_ptr<LisConnectionObserver09>(new LisConnectionObserver09);
        auto *app = qobject_cast<QApplication *>(QCoreApplication::instance());
        observer->app_ = app;
        observer->shell_ = shell;
        if (!app || QThread::currentThread() != app->thread())
        {
            observer->startError_ = QStringLiteral("LIS观察必须在实际QApplication所属GUI线程开始。");
            return observer;
        }
        if (!shell || shell->thread() != app->thread())
        {
            observer->startError_ = QStringLiteral("缺少同GUI线程的正式主窗口。");
            return observer;
        }
        const auto objects = app->findChildren<Sn100Lis::LisConnectionController *>(
            QStringLiteral("originalLisConnectionController"), Qt::FindDirectChildrenOnly);
        if (objects.isEmpty()) return observer;
        if (objects.size() != 1 || objects.first()->thread() != app->thread())
        {
            observer->startError_ = QStringLiteral("实际LIS连接对象不唯一或不属于GUI线程。");
            return observer;
        }
        auto *controller = objects.first();
        observer->controller_ = controller;
        observer->initialState_ = stateJson(controller->state());
        // context为qApp；闭包只保留弱引用，观察结束仅断开自己的两条订阅。
        const std::weak_ptr<LisConnectionObserver09> weakObserver = observer;
        observer->stateConnection_ = QObject::connect(controller, &Sn100Lis::LisConnectionController::stateChanged, app,
            [weakObserver](const Sn100Lis::ConnectionState &state)
            {
                const auto target = weakObserver.lock();
                if (target) target->append(target->states_, stateJson(state), target->statesDropped_);
            });
        observer->errorConnection_ = QObject::connect(controller, &Sn100Lis::LisConnectionController::error, app,
            [weakObserver](const Sn100Lis::ConfigurationIdentity &identity, const QString &operation,
                       quint64 generation, quint64 peer, const QString &detail)
            {
                const auto target = weakObserver.lock();
                if (!target) return;
                const QStringList operations{QStringLiteral("configure"), QStringLiteral("open"),
                    QStringLiteral("connection"), QStringLiteral("listen"), QStringLiteral("accept"),
                    QStringLiteral("encode"), QStringLiteral("decode"), QStringLiteral("write"),
                    QStringLiteral("send-result")};
                const QJsonObject event{{"operation", operations.contains(operation) ? operation : QStringLiteral("other")},
                    {"configuration", configurationJson(identity)}, {"generation", QString::number(generation)},
                    {"peer", QString::number(peer)}, {"detail_present", !detail.isEmpty()},
                    {"detail_sha256", textHash(detail)}};
                target->append(target->errors_, event, target->errorsDropped_);
            });
        return observer;
    }

    // 只解除本观察器的信号订阅；不停止LIS、主通信或正式界面。
    void stopObserving()
    {
        if (!app_ || QThread::currentThread() != app_->thread()) return;
        QObject::disconnect(stateConnection_);
        QObject::disconnect(errorConnection_);
        stateConnection_ = QMetaObject::Connection();
        errorConnection_ = QMetaObject::Connection();
    }

    QJsonObject snapshot() const
    {
        if (!app_ || QThread::currentThread() != app_->thread())
            return {{"schema", "sn100-lis-connection-observer-09-v1"}, {"status", "integration_error"},
                {"reason", "当前不在实际GUI线程，未读取控件、连接或事件缓存。"},
                {"connected", QJsonValue(QJsonValue::Null)}, {"state", QJsonValue(QJsonValue::Null)},
                {"committed_configuration", QJsonValue(QJsonValue::Null)}, {"close_verified_here", false}};
        QJsonObject result{{"schema", "sn100-lis-connection-observer-09-v1"}, {"status", "pending"},
            {"connected", QJsonValue(QJsonValue::Null)}, {"state", QJsonValue(QJsonValue::Null)},
            {"committed_configuration", QJsonValue(QJsonValue::Null)},
            {"startup_events_complete", false}, {"initial_state", initialState_},
            {"state_events", states_}, {"error_events", errors_},
            {"state_events_dropped", QString::number(statesDropped_)},
            {"error_events_dropped", QString::number(errorsDropped_)},
            {"message_bodies_or_transport_bytes_recorded", false}, {"business_verified", false},
            {"close_verified_here", false},
            {"close_proof_source", "正常关闭后的main日志与最终合同；本快照不预判已经停止。"}};
        const auto pending = [&result](const QString &reason)
        { result["reason"] = reason; return result; };
        const auto integrationError = [&result](const QString &reason)
        { result["status"] = QStringLiteral("integration_error"); result["reason"] = reason; return result; };
        if (!startError_.isEmpty()) return integrationError(startError_);
        if (!shell_) return pending(QStringLiteral("正式主窗口已不可用。"));
        if (!controller_) return pending(QStringLiteral("尚无可读取的originalLisConnectionController对象。"));
        if (controller_->thread() != app_->thread() || shell_->thread() != app_->thread())
            return integrationError(QStringLiteral("实际连接对象或主窗口的线程归属已经改变。"));

        const auto state = controller_->state();
        result["state"] = stateJson(state);
        result["connected"] = !state.closing && state.connectedPeers > 0;
        if (!knownState(state.state) || state.connectedPeers < 0)
            return integrationError(QStringLiteral("实际LIS状态枚举或对端数不在已审接口范围内。"));

        auto *page = shell_->lisPage();
        if (!page || page->thread() != app_->thread())
            return pending(QStringLiteral("实际LIS设置页尚不可读。"));
        result["settings_ready"] = page->isReady();
        result["settings_busy"] = page->isBusy();
        result["settings_dirty"] = page->isDirty();
        if (!page->isReady() || page->isBusy() || page->isDirty())
            return pending(QStringLiteral("LIS设置页尚未就绪、正在操作或存在草稿；不采用草稿连接值。"));
        const LisSettings settings = page->committedSettings();
        result["committed_configuration"] = QJsonObject{
            {"original_profile", settings.originalProfile}, {"type", settingToken(settings.type)},
            {"ip", settingToken(settings.ip)}, {"port", settingToken(settings.port)},
            {"tcp_type", settingToken(settings.tcpType)}, {"link", settings.link},
            {"auto_get", settings.autoGet}, {"auto_send", settings.autoSend}, {"simulation", settings.simulation},
            {"source_fingerprint", fingerprint(settings.originalFingerprint)}};
        const bool expectedConfiguration = settings.originalProfile && settings.type == QStringLiteral("tcp")
            && settings.tcpType == QStringLiteral("client") && settings.ip == QStringLiteral("127.0.0.1")
            && settings.port == QStringLiteral("3800") && settings.link && !settings.autoGet
            && !settings.autoSend && !settings.simulation;
        result["configuration_matches_requested_context"] = expectedConfiguration;
        result["connection_and_settings_source_fingerprint_equal"] =
            state.configuration.sourceFingerprint.isEmpty() || settings.originalFingerprint.isEmpty()
                ? QJsonValue(QJsonValue::Null)
                : QJsonValue(state.configuration.sourceFingerprint == settings.originalFingerprint);

        // 主窗口和设置导航都有btnLis；按原UI的实际父子路径只取顶部状态图标。
        const auto identity = [](QWidget *widget)
        {
            QObject *parent = widget ? widget->parent() : nullptr;
            return QJsonObject{{"object_name", widget ? widget->objectName() : QString()},
                {"class", widget ? QString::fromLatin1(widget->metaObject()->className()) : QString()},
                {"parent_object_name", parent ? parent->objectName() : QString()},
                {"parent_class", parent ? QString::fromLatin1(parent->metaObject()->className()) : QString()}};
        };
        QJsonObject lookup{{"scope", "frmMain/frmBg/statusArea/btnLis"}, {"matched", false},
            {"background_count", QJsonValue(QJsonValue::Null)},
            {"status_area_count", QJsonValue(QJsonValue::Null)},
            {"button_count", QJsonValue(QJsonValue::Null)}};
        const auto backgrounds = shell_->findChildren<QWidget *>(QStringLiteral("frmBg"), Qt::FindDirectChildrenOnly);
        lookup["background_count"] = backgrounds.size();
        if (backgrounds.size() == 1) lookup["background"] = identity(backgrounds.first());
        result["icon_lookup"] = lookup;
        if (backgrounds.size() != 1 || !qobject_cast<QFrame *>(backgrounds.first()))
            return integrationError(QStringLiteral("正式顶部状态区的frmBg缺失、不唯一或类型不符。"));
        const auto areas = backgrounds.first()->findChildren<QWidget *>(QStringLiteral("statusArea"), Qt::FindDirectChildrenOnly);
        lookup["status_area_count"] = areas.size();
        if (areas.size() == 1) lookup["status_area"] = identity(areas.first());
        result["icon_lookup"] = lookup;
        if (areas.size() != 1 || !qobject_cast<CusView *>(areas.first()))
            return integrationError(QStringLiteral("正式顶部statusArea缺失、不唯一或类型不符。"));
        const auto buttons = areas.first()->findChildren<QWidget *>(QStringLiteral("btnLis"), Qt::FindDirectChildrenOnly);
        lookup["button_count"] = buttons.size();
        if (buttons.size() == 1) lookup["button"] = identity(buttons.first());
        result["icon_lookup"] = lookup;
        if (buttons.size() != 1 || !qobject_cast<CusBadgeButton *>(buttons.first()))
            return integrationError(QStringLiteral("正式顶部CusBadgeButton/btnLis缺失、不唯一或类型不符。"));
        lookup["matched"] = true;
        result["icon_lookup"] = lookup;
        const QString tooltip = buttons.first()->toolTip();
        // 只保留实际提示的结构化状态前缀；句号后的自由错误详情不保存全文。
        const QRegularExpression prefix(QStringLiteral(
            "^LIS：[^；。\\r\\n]{1,60}；配置版本[0-9]+，连接代次[0-9]+，已连接对端[0-9]+。"));
        const auto match = prefix.match(tooltip);
        result["button_tooltip_sha256"] = textHash(tooltip);
        result["button_tooltip_status_prefix"] = match.hasMatch()
            ? QJsonValue(match.captured()) : QJsonValue(QJsonValue::Null);
        result["button_tooltip_free_detail_recorded"] = false;
        if (!expectedConfiguration)
            return pending(QStringLiteral("当前已提交LIS配置不属于本次获准观察条件。"));
        if (!match.hasMatch())
            return pending(QStringLiteral("图标提示尚无可安全读取的结构化状态前缀。"));
        if (state.configuration.revision == 0 || settings.originalFingerprint.isEmpty()
            || state.configuration.sourceFingerprint.isEmpty())
            return pending(QStringLiteral("连接配置身份或原设置来源尚未记录。"));
        if (state.configuration.sourceFingerprint != settings.originalFingerprint)
            return pending(QStringLiteral("连接与已提交设置的来源指纹不同，不能重贴来源或推断已一致。"));
        if (state.closing || state.connectedPeers == 0)
            return pending(QStringLiteral("当前没有可采用的真实已连接对端；LIS收发及业务继续待验。"));
        result["status"] = QStringLiteral("observed");
        result["reason"] = QStringLiteral("已观察到当前真实已连接对端；不表示收到业务确认或通过LIS业务验收。");
        return result;
    }

private:
    LisConnectionObserver09() = default;
    QMetaObject::Connection stateConnection_, errorConnection_;
    QPointer<QApplication> app_;
    QPointer<frmMain> shell_;
    QPointer<Sn100Lis::LisConnectionController> controller_;
    QString startError_;
    QJsonObject initialState_;
    QJsonArray states_, errors_;
    quint64 statesDropped_ = 0, errorsDropped_ = 0;

    static QString textHash(const QString &text)
    { return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex()); }

    static QJsonValue fingerprint(const QString &text)
    {
        static const QRegularExpression hash(QStringLiteral("^[0-9a-f]{64}$"));
        return hash.match(text).hasMatch() ? QJsonValue(text) : QJsonValue(QJsonValue::Null);
    }

    static QJsonValue settingToken(const QString &text)
    {
        static const QRegularExpression token(QStringLiteral("^[A-Za-z0-9_.:-]{0,1024}$"));
        return token.match(text).hasMatch() ? QJsonValue(text) : QJsonValue(QJsonValue::Null);
    }

    static bool knownState(const QString &state)
    {
        const QStringList values{QStringLiteral("Unconfigured"), QStringLiteral("Configured"),
            QStringLiteral("LocalSettingsOnly"), QStringLiteral("SerialPending"), QStringLiteral("SettingsUnconfirmed"),
            QStringLiteral("Listening"), QStringLiteral("Connecting"), QStringLiteral("Connected"),
            QStringLiteral("ReconnectWaiting"), QStringLiteral("Disconnected"), QStringLiteral("Failed"),
            QStringLiteral("Stopped")};
        return values.contains(state);
    }

    static QJsonObject configurationJson(const Sn100Lis::ConfigurationIdentity &identity)
    {
        return {{"revision", QString::number(identity.revision)}, {"effective_key", fingerprint(identity.effectiveKey)},
            {"transport_token", fingerprint(identity.transportToken)}, {"source_fingerprint", fingerprint(identity.sourceFingerprint)}};
    }

    static QJsonObject stateJson(const Sn100Lis::ConnectionState &state)
    {
        return {{"state", knownState(state.state) ? QJsonValue(state.state) : QJsonValue(QJsonValue::Null)},
            {"configuration", configurationJson(state.configuration)}, {"generation", QString::number(state.generation)},
            {"connected_peers", state.connectedPeers}, {"closing", state.closing}};
    }

    void append(QJsonArray &events, const QJsonObject &event, quint64 &dropped)
    {
        if (events.size() == 32) { events.removeAt(0); ++dropped; }
        events.append(event);
    }
};

#endif
