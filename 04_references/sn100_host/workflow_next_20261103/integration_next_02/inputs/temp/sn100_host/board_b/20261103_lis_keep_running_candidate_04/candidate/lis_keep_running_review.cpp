#include "lis_connection_observer_09.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QTimer>

namespace
{
struct ObservationParameters
{
    QString directory, id, expectedRelease;
};

QString processStartTicks()
{
    QFile stat(QStringLiteral("/proc/self/stat"));
    if (!stat.open(QIODevice::ReadOnly)) return QString();
    const QByteArray bytes = stat.read(8192);
    const int endOfName = bytes.lastIndexOf(')');
    if (endOfName < 0) return QString();
    // 括号后的第一个字段是state(3)，starttime(22)位于下标19。
    const auto fields = bytes.mid(endOfName + 1).simplified().split(' ');
    if (fields.size() <= 19) return QString();
    const QString ticks = QString::fromLatin1(fields.at(19));
    return QRegularExpression(QStringLiteral("^[0-9]+$")).match(ticks).hasMatch() ? ticks : QString();
}

bool readParameters(ObservationParameters *parameters)
{
    parameters->directory = qEnvironmentVariable("SN100_LIS_OBSERVATION_DIR");
    parameters->id = qEnvironmentVariable("SN100_LIS_OBSERVATION_ID");
    parameters->expectedRelease = qEnvironmentVariable("SN100_LIS_OBSERVATION_RELEASE");
    // 普通启动在创建观察对象、定时器、订阅和文件之前返回。
    if (parameters->directory.isEmpty() || parameters->id.isEmpty() || parameters->expectedRelease.isEmpty())
        return false;
    const QRegularExpression id(QStringLiteral("\\A[A-Za-z0-9_-]{1,64}\\z"));
    const QRegularExpression release(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    const QFileInfo directory(parameters->directory);
    const auto privateBits = QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
        | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther;
    const bool valid = id.match(parameters->id).hasMatch() && release.match(parameters->expectedRelease).hasMatch()
        && QDir::isAbsolutePath(parameters->directory) && directory.isDir() && !directory.isSymLink()
        && directory.canonicalFilePath() == parameters->directory
        && !(directory.permissions() & privateBits)
        && QDir(parameters->directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                | QDir::NoDotAndDotDot).isEmpty();
    if (!valid) qWarning() << "LIS_OBSERVATION_PARAMETERS_REJECTED";
    return valid;
}

class LisKeepRunningObservation : public QObject
{
public:
    explicit LisKeepRunningObservation(const ObservationParameters &parameters, QObject *app)
        : QObject(app), parameters_(parameters), lock_(QDir(parameters.directory).filePath(QStringLiteral("lis_observation.lock")))
    {
    }

    bool begin()
    {
        lock_.setStaleLockTime(0);
        if (!lock_.tryLock(0))
        {
            qWarning() << "LIS_OBSERVATION_DIRECTORY_ALREADY_CLAIMED";
            return false;
        }
        // 发布号是总控运行时给定的期望值，不在这里宣称已独立核包。
        startTicks_ = processStartTicks();
        if (startTicks_.isEmpty())
        {
            qWarning() << "LIS_OBSERVATION_PROCESS_IDENTITY_UNAVAILABLE";
            lock_.unlock();
            return false;
        }
        elapsed_.start();
        tick_ = new QTimer(this);
        tick_->setInterval(100);
        QObject::connect(tick_, &QTimer::timeout, this, [this]() { poll(); });
        tick_->start();
        return true;
    }

private:
    ObservationParameters parameters_;
    QLockFile lock_;
    QTimer *tick_ = nullptr;
    QElapsedTimer elapsed_, observing_;
    QPointer<frmMain> shell_;
    std::shared_ptr<LisConnectionObserver09> observer_;
    QString startTicks_, waitingReason_ = QStringLiteral("正式界面和LIS对象尚未就绪。");
    qint64 startupWaitMs_ = 0;
    bool finished_ = false;

    void poll()
    {
        // startup hook发生在QCoreApplication构造期；到事件循环再核实际GUI应用类型。
        if (!qobject_cast<QApplication *>(QCoreApplication::instance()))
        {
            waitingReason_ = QStringLiteral("实际应用不是QApplication，未读取GUI对象。");
            startupWaitMs_ = elapsed_.elapsed();
            finish(false);
            return;
        }
        if (observer_)
        {
            if (observing_.elapsed() >= 15000) finish(true);
            return;
        }
        // 截止后不再因这一轮刚好就绪而延长观察窗口。
        if (elapsed_.elapsed() >= 60000)
        {
            startupWaitMs_ = elapsed_.elapsed();
            finish(false);
            return;
        }
        QList<frmMain *> shells;
        for (QWidget *widget : QApplication::allWidgets())
            if (auto *shell = qobject_cast<frmMain *>(widget)) shells.append(shell);
        if (shells.size() == 1)
        {
            shell_ = shells.first();
            QWidget *host = shell_->window();
            auto *splash = host ? host->findChild<QWidget *>(QStringLiteral("startupSplash")) : nullptr;
            const auto controllers = qApp->findChildren<Sn100Lis::LisConnectionController *>(
                QStringLiteral("originalLisConnectionController"), Qt::FindDirectChildrenOnly);
            // 控件构造期间也可能处理事件；晚创建的启动画面和控制器未出现前不读lisPage()。
            if (host && host->isVisible() && shell_->isVisible() && splash && !splash->isVisible()
                && !controllers.isEmpty())
            {
                auto *page = shell_->lisPage();
                if (page && page->isReady() && !page->isBusy() && !page->isDirty())
                {
                    // 页面检查也会消耗时间；开始决定与结果记录共用这一次采样。
                    const qint64 beginWaitMs = elapsed_.elapsed();
                    startupWaitMs_ = beginWaitMs;
                    if (beginWaitMs >= 60000)
                    {
                        finish(false);
                        return;
                    }
                    observer_ = LisConnectionObserver09::start(shell_);
                    observing_.start();
                    return;
                }
            }
            waitingReason_ = QStringLiteral("正式窗口、启动画面结束、已提交LIS设置或真实连接对象尚未同时就绪。");
        }
        else
        {
            shell_.clear();
            waitingReason_ = QStringLiteral("正式frmMain对象缺失或不唯一，未选取任意窗口观察。");
        }
    }

    bool writeAtomically(const QString &name, const QByteArray &bytes)
    {
        const QString path = QDir(parameters_.directory).filePath(name);
        if (QFileInfo::exists(path)) return false;
        QSaveFile file(path);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) return false;
        if (!file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            || file.write(bytes) != bytes.size())
        {
            file.cancelWriting();
            return false;
        }
        return file.commit();
    }

    void finish(bool startupReady)
    {
        if (finished_) return;
        finished_ = true;
        tick_->stop();
        QJsonObject lis;
        const qint64 observationMs = observer_ ? observing_.elapsed() : 0;
        if (observer_)
        {
            lis = observer_->snapshot();
            observer_->stopObserving();
            observer_.reset();
        }
        else
        {
            lis = {{"status", "pending"}, {"reason", waitingReason_},
                {"connected", QJsonValue(QJsonValue::Null)}, {"state", QJsonValue(QJsonValue::Null)},
                {"business_verified", false}, {"close_verified_here", false},
                {"message_bodies_or_transport_bytes_recorded", false}};
        }
        const QJsonObject state = lis.value("state").toObject();
        const QRegularExpression tooltip(QStringLiteral(
            "^LIS：[^；。\\r\\n]{1,60}；配置版本([0-9]+)，连接代次([0-9]+)，已连接对端([0-9]+)。$"));
        const auto match = tooltip.match(lis.value("button_tooltip_status_prefix").toString());
        const bool tooltipMatches = match.hasMatch()
            && match.captured(1) == state.value("configuration").toObject().value("revision").toString()
            && match.captured(2) == state.value("generation").toString()
            && match.captured(3) == QString::number(state.value("connected_peers").toInt(-1));
        lis["tooltip_identity_matches_state"] = match.hasMatch()
            ? QJsonValue(tooltipMatches) : QJsonValue(QJsonValue::Null);
        const bool integrationError = lis.value("status").toString() == QStringLiteral("integration_error");
        const QRegularExpression positiveRevision(QStringLiteral("\\A[1-9][0-9]*\\z"));
        const QRegularExpression sourceHash(QStringLiteral("\\A[0-9a-f]{64}\\z"));
        const auto configuration = state.value("configuration").toObject();
        const bool settingsReady = lis.value("settings_ready").toBool()
            && lis.value("settings_busy").isBool() && !lis.value("settings_busy").toBool()
            && lis.value("settings_dirty").isBool() && !lis.value("settings_dirty").toBool();
        const bool readable = lis.value("configuration_matches_requested_context").toBool()
            && settingsReady
            && lis.value("state").isObject()
            && positiveRevision.match(configuration.value("revision").toString()).hasMatch()
            && sourceHash.match(configuration.value("source_fingerprint").toString()).hasMatch()
            && sourceHash.match(lis.value("committed_configuration").toObject().value("source_fingerprint").toString()).hasMatch()
            && lis.value("connection_and_settings_source_fingerprint_equal").isBool()
            && lis.value("icon_lookup").toObject().value("matched").toBool() && match.hasMatch();
        const bool evaluated = integrationError || readable;
        const bool passed = evaluated && !integrationError
            && lis.value("connection_and_settings_source_fingerprint_equal").toBool() && tooltipMatches;
        QJsonArray checks, pending;
        if (evaluated)
            checks.append(QJsonObject{{"name", "顶部LIS对象、结构提示与原配置来源可核对"}, {"passed", passed}});
        else
            pending.append(QStringLiteral("LIS软件观察条件不足；未执行项不记通过。"));
        pending.append(QStringLiteral("LIS下载、上传和业务确认未验证；没有真实对端时连接仍待验。"));
        pending.append(QStringLiteral("旧登录、注销、主通信重开、Qk及关闭检查本次未执行。"));
        const bool windowAlive = shell_ && shell_->window();
        const bool windowVisible = windowAlive && shell_->isVisible() && shell_->window()->isVisible();
        QJsonObject result{{"schema", "sn100-lis-keep-running-observation-v1"},
            {"observation_id", parameters_.id}, {"release_id", parameters_.expectedRelease},
            {"release_identity_source", "runtime_expected_not_independently_verified_here"},
            {"process", QJsonObject{{"pid", QString::number(QCoreApplication::applicationPid())}, {"start_ticks", startTicks_}}},
            {"observation_complete", true}, {"completion_kind", "snapshot_saved_keep_running"},
            {"startup_ready", startupReady}, {"startup_wait_ms", startupWaitMs_}, {"duration_ms", observationMs},
            {"window_alive_at_snapshot", windowAlive}, {"window_visible_at_snapshot", windowVisible},
            {"application_exit", QJsonValue(QJsonValue::Null)}, {"application_exit_evaluated", false},
            {"shutdown_evaluated", false}, {"close_verified", false}, {"qk_query_requested", false},
            {"software_state_evaluated", evaluated}, {"software_state_passed", passed},
            {"checks", checks}, {"failed", evaluated && !passed ? 1 : 0}, {"pending_items", pending},
            {"lis_connection_observation", lis}, {"physical_screen_verified", false}};
        const QByteArray json = QJsonDocument(result).toJson();
        bool saved = json.size() < 1024 * 1024 && writeAtomically(QStringLiteral("lis_observation.json"), json);
        if (saved)
        {
            const QByteArray digest = QCryptographicHash::hash(json, QCryptographicHash::Sha256).toHex();
            const QByteArray marker = QByteArray("SN100_LIS_OBSERVATION_READY_V1 ") + parameters_.id.toLatin1()
                + ' ' + parameters_.expectedRelease.toLatin1() + ' '
                + QByteArray::number(QCoreApplication::applicationPid()) + ' ' + digest + '\n';
            saved = writeAtomically(QStringLiteral("lis_observation.ready"), marker);
        }
        // 成功、条件不足或写失败都只结束观察；没有观察进程退出码。
        if (saved) qInfo() << "LIS_KEEP_RUNNING_OBSERVATION_SAVED" << parameters_.id;
        else qCritical() << "LIS_KEEP_RUNNING_OBSERVATION_SAVE_FAILED" << parameters_.id;
        lock_.unlock();
        deleteLater();
    }
};

void scheduleLisKeepRunningObservation()
{
    ObservationParameters parameters;
    if (!readParameters(&parameters)) return;
    auto *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() != app->thread()) return;
    auto *observation = new LisKeepRunningObservation(parameters, app);
    if (!observation->begin()) delete observation;
}
}

Q_COREAPP_STARTUP_FUNCTION(scheduleLisKeepRunningObservation)
