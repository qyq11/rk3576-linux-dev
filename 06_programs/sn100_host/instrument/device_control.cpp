#include "device_control.h"
#include "original_device_commands.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace
{
// 字段位置来自原commandreceiver.cpp::pcQueryStatus；缺失字段保持未知，不能当正常。
bool flag(const QStringList &fields, int index, bool *value)
{
    if (index < 0 || index >= fields.size()) return false;
    bool ok = false;
    const int number = fields[index].toInt(&ok);
    if (!ok || number < 0) return false;
    *value = number > 0;
    return true;
}

QString requireFlag(const Sn100Protocol::StatusSnapshot &status, int blockId, int field,
                    bool expected, const QString &message)
{
    for (const auto &block : status.subsystems)
        if (block.id == blockId)
        {
            bool value = false;
            if (!flag(block.fields, field, &value))
                return QStringLiteral("设备状态缺少%1，不能开始测试。").arg(message);
            return value == expected ? QString() : message;
        }
    return QStringLiteral("设备状态缺少%1，不能开始测试。").arg(message);
}
}

DeviceControl::DeviceControl(QObject *parent) : QObject(parent), transport_(this), timer_(this)
{
    qRegisterMetaType<DeviceControlState>();
    clock_.start();
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &DeviceControl::tick);
    connect(&transport_, &DeviceTransport::connected, this, [this](quint64)
    {
        state_.connected = true;
        statusAt_ = -1;
        queryAt_ = -1;
        publish(tr("已连接主控，正在读取设备状态。"));
        query(Sn100Protocol::ReadOnlyQuery::Status);
    });
    connect(&transport_, &DeviceTransport::disconnected, this, [this](quint64)
    {
        state_.connected = false;
        state_.fresh = false;
        if (!state_.pending.isEmpty())
        {
            uncertainCommand_ = state_.pending;
            state_.outcomeUnknown = true;
        }
        state_.pending.clear();
        statusAt_ = -1;
        publish(tr("主控连接已断开；旧设备状态已失效。"));
    });
    connect(&transport_, &DeviceTransport::errorOccurred, this, [this](const QString &message, quint64)
    { reject(message); });
    connect(&transport_, &DeviceTransport::wireQueued, this, [this](const QByteArray &bytes, quint64)
    { emit wireActivity(QStringLiteral("TX queued"), bytes); });
    connect(&transport_, &DeviceTransport::wireReceived, this, [this](const QByteArray &bytes, quint64)
    { emit wireActivity(QStringLiteral("RX"), bytes); });
    connect(&transport_, &DeviceTransport::messageReceived, this, [this](const QByteArray &message, quint64)
    { receive(message); });
}

bool DeviceControl::open(const QString &address, quint16 port)
{
    // 重复点击同一连接不会重建socket或重发控制命令。
    if (state_.address == address && state_.port == port && timer_.isActive())
        return reject(tr("该主控连接已启动，请等待结果或先断开。"));
    close();
    const bool unknown = state_.outcomeUnknown;
    state_ = DeviceControlState();
    state_.outcomeUnknown = unknown;
    state_.address = address;
    state_.port = port;
    QString message;
    if (!transport_.open(address, port, &message)) return reject(message);
    timer_.start();
    publish(tr("正在连接主控 %1:%2。").arg(address).arg(port));
    return true;
}

void DeviceControl::close()
{
    timer_.stop();
    transport_.close();
    state_.connected = false;
    state_.fresh = false;
    if (!state_.pending.isEmpty())
    {
        uncertainCommand_ = state_.pending;
        state_.outcomeUnknown = true;
    }
    state_.pending.clear();
    statusAt_ = queryAt_ = pendingAt_ = -1;
    publish(tr("主控连接已关闭。"));
}

bool DeviceControl::reject(const QString &message)
{
    state_.message = message;
    emit error(message);
    emit changed(state_);
    return false;
}

void DeviceControl::publish(const QString &message)
{
    if (!message.isEmpty()) state_.message = message;
    emit changed(state_);
}

bool DeviceControl::send(const QByteArray &payload, const QString &pending)
{
    if (!state_.connected) return reject(tr("主控未连接，请先连接设备。"));
    if (!pending.isEmpty() && (!state_.pending.isEmpty() || state_.outcomeUnknown))
        return reject(tr("上一条控制命令尚未确认，不能重复提交；请核对设备状态。"));
    QString message;
    if (!transport_.send(payload, &message)) return reject(message);
    if (!pending.isEmpty())
    {
        state_.pending = pending;
        pendingAt_ = clock_.elapsed();
        publish(tr("命令已进入发送队列，正在等待设备返回。"));
    }
    return true;
}

bool DeviceControl::query(Sn100Protocol::ReadOnlyQuery type)
{
    const QByteArray payload = Sn100Protocol::queryPayload(type);
    if (payload != "Qj" && payload != "Qk" && payload != "QH")
        return reject(tr("不支持的查询类型。"));
    const bool queued = send(payload);
    if (queued && type == Sn100Protocol::ReadOnlyQuery::Status) queryAt_ = clock_.elapsed();
    return queued;
}

QString DeviceControl::startBlocker() const
{
    if (!state_.connected) return tr("主控未连接，请先连接设备。");
    if (!state_.fresh) return tr("尚未取得有效设备状态，请等待状态返回。");
    if (state_.outcomeUnknown || !state_.pending.isEmpty()) return tr("控制命令尚未确认，请先核对设备状态。");
    const auto &status = state_.status;
    if (status.generalFields.size() < 14 || status.generalFields[13] != originalVersion())
        return tr("上下位机匹配号不一致；本程序沿用原Windows匹配号%1。").arg(originalVersion());
    if (status.mainStatus != 2 && status.mainStatus != 6)
        return tr("仪器未准备就绪或未完成暂停，不能开始测试。");
    if (status.ledStatus == 0 || status.ledStatus == 3 || status.ledStatus == 7)
        return tr("仪器连接或故障状态不允许开始测试。");
    bool normal = false;
    if (!flag(status.generalFields, 7, &normal) || !normal) return tr("温度异常或温度状态未知，不能开始测试。");
    if (status.generalFields.size() > 4 && !status.generalFields[4].isEmpty())
    {
        if (!alarmPolicyAvailable_) return tr("原告警配置未能读取，不能判断当前告警是否允许开始测试。");
        for (const QString &code : status.generalFields[4].split('|', Qt::SkipEmptyParts))
            if (blockingAlarmCodes_.contains(code)) return tr("当前告警%1按原配置禁止开始测试，请先处理。").arg(code);
    }
    const bool legacy = std::any_of(status.subsystems.cbegin(), status.subsystems.cend(),
                                  [](const Sn100Protocol::SubsystemStatus &s) { return s.id == 3; });
    const QList<QString> failures = legacy
        ? QList<QString>{requireFlag(status, 16, 13, false, tr("出样区域样本架已满")),
                         requireFlag(status, 6, 2, true, tr("试剂舱盖未关闭")),
                         requireFlag(status, 17, 3, true, tr("稀释液舱盖未关闭")),
                         requireFlag(status, 3, 6, true, tr("反应杯仓缺杯")),
                         requireFlag(status, 3, 5, true, tr("反应杯导轨缺杯")),
                         requireFlag(status, 14, 22, true, tr("清洗液缺少")),
                         requireFlag(status, 14, 23, true, tr("废液满")),
                         requireFlag(status, 12, 7, true, tr("废杯满"))}
        : QList<QString>{requireFlag(status, 20, 17, false, tr("出样区域样本架已满")),
                         requireFlag(status, 32, 12, true, tr("试剂舱盖未关闭")),
                         requireFlag(status, 26, 12, true, tr("反应杯仓缺杯")),
                         requireFlag(status, 26, 11, true, tr("反应杯导轨缺杯")),
                         requireFlag(status, 32, 14, true, tr("清洗液瓶液位异常")),
                         requireFlag(status, 31, 16, true, tr("废液满")),
                         requireFlag(status, 22, 9, true, tr("废杯满"))};
    for (const QString &failure : failures) if (!failure.isEmpty()) return failure;
    return {};
}

QString DeviceControl::pauseBlocker() const
{
    if (!state_.connected || !state_.fresh) return tr("主控未连接或设备状态已失效。");
    if (!state_.pending.isEmpty() || state_.outcomeUnknown) return tr("控制命令尚未确认，请等待设备返回。");
    if (state_.status.mainStatus < 3 || state_.status.mainStatus > 6)
        return tr("设备尚未开始测试，不能暂停。");
    return {};
}

bool DeviceControl::start(const QString &businessBlocker)
{
    const QString reason = startBlocker();
    if (!reason.isEmpty()) return reject(reason);
    if (!businessBlocker.isEmpty()) return reject(businessBlocker);
    return send(startPayload(), QStringLiteral("start"));
}

bool DeviceControl::pause()
{
    const QString reason = pauseBlocker();
    if (!reason.isEmpty()) return reject(reason);
    return send(pausePayload(), QStringLiteral("pause"));
}

bool DeviceControl::silenceAlarm()
{
    return send(QByteArrayLiteral("Mz"));
}

bool DeviceControl::maintenanceCommand(const QByteArray &payload)
{
    // 原仪器通信测试页直接发送这些固定命令；不提供任意文本执行入口。
    if (!isOriginalFixedCommand(payload)) return reject(tr("此按钮尚无已接入的原版命令。"));
    if (payload == "Ts") return send(payload, QStringLiteral("start"));
    if (payload == "Tp[0]") return send(payload, QStringLiteral("pause"));
    if (payload == "MP") return send(payload, QStringLiteral("stop"));
    if (!payload.startsWith('Q') && !payload.startsWith('U'))
    {
        const qint64 now = clock_.elapsed();
        if (payload == lastMaintenance_ && lastMaintenanceAt_ >= 0 && now - lastMaintenanceAt_ < 500)
            return reject(tr("同一维护动作刚刚提交，请勿连续点击。"));
        if (!send(payload)) return false;
        lastMaintenance_ = payload;
        lastMaintenanceAt_ = now;
        publish(tr("维护命令已进入发送队列；实际动作请核对设备和返回报文。"));
        return true;
    }
    return send(payload);
}

bool DeviceControl::updateDeviceTime()
{
    // 原on_pushButton_updateTime_clicked：主机时间加1秒，固定格式原样保留。
    return send("Ud[" + QDateTime::currentDateTime().addSecs(1).toString("MMddHHmmyyyy.ss").toLatin1() + ']');
}

void DeviceControl::setAlarmPolicy(const QStringList &blockingCodes, bool available)
{
    blockingAlarmCodes_ = QSet<QString>(blockingCodes.cbegin(), blockingCodes.cend());
    alarmPolicyAvailable_ = available;
}

bool DeviceControl::confirmObservedState()
{
    if (!state_.connected || !state_.fresh || !state_.pending.isEmpty())
        return reject(tr("请先读取当前有效状态，再确认设备实际情况。"));
    state_.outcomeUnknown = false;
    uncertainCommand_.clear();
    publish(tr("操作者已核对当前设备状态；旧命令不重发。"));
    return true;
}

void DeviceControl::receive(const QByteArray &message)
{
    if (message.startsWith("Qj"))
    {
        Sn100Protocol::StatusSnapshot parsed;
        QString problem;
        if (!Sn100Protocol::parseStatus(message, &parsed, &problem)) { reject(problem); return; }
        state_.status = parsed;
        state_.fresh = true;
        statusAt_ = clock_.elapsed();
        if (state_.pending == QStringLiteral("pause") && parsed.mainStatus == 6)
        {
            state_.pending.clear();
            pendingAt_ = -1;
        }
        if ((state_.pending == QStringLiteral("start-state") && parsed.mainStatus >= 3 && parsed.mainStatus <= 5) ||
            (state_.pending == QStringLiteral("stop") && parsed.mainStatus <= 2))
        {
            state_.pending.clear();
            pendingAt_ = -1;
        }
        if (state_.outcomeUnknown &&
            ((uncertainCommand_.startsWith(QStringLiteral("start")) && parsed.mainStatus >= 3 && parsed.mainStatus <= 5) ||
             (uncertainCommand_ == QStringLiteral("pause") && parsed.mainStatus == 6) ||
             (uncertainCommand_ == QStringLiteral("stop") && parsed.mainStatus <= 2)))
        {
            state_.outcomeUnknown = false;
            uncertainCommand_.clear();
        }
        publish(tr("已更新主控实际状态。"));
    }
    else if (message.startsWith("Ts"))
    {
        static const QRegularExpression result(QStringLiteral("^Ts\\[([0-9]+)\\]$"));
        const auto match = result.match(QString::fromLatin1(message));
        if (!match.hasMatch()) { reject(tr("开始命令返回格式不正确，保留等待状态。")); return; }
        if (state_.pending != QStringLiteral("start"))
        { reject(tr("收到无对应请求的开始返回，未据此改变按钮状态。")); return; }
        bool ok = false;
        const int accepted = match.captured(1).toInt(&ok);
        if (!ok) { reject(tr("开始返回数值无效。")); return; }
        // Ts[1]只表示接受请求；保留等待，直到Qj实际进入运行状态，防止旧就绪状态让人重复开始。
        const bool alreadyRunning = state_.fresh && state_.status.mainStatus >= 3 && state_.status.mainStatus <= 5;
        state_.pending = accepted > 0 && !alreadyRunning ? QStringLiteral("start-state") : QString();
        pendingAt_ = state_.pending.isEmpty() ? -1 : clock_.elapsed();
        publish(accepted > 0 ? tr("设备已接受开始请求，等待实时运行状态。") : tr("设备拒绝开始：仪器初始化未完成。"));
        emit startAccepted(accepted > 0);
        query(Sn100Protocol::ReadOnlyQuery::Status);
    }
    emit payloadReceived(message);
}

void DeviceControl::tick()
{
    const qint64 now = clock_.elapsed();
    if (!state_.connected) return;
    if (queryAt_ < 0 || now - queryAt_ >= 1000) query(Sn100Protocol::ReadOnlyQuery::Status);
    if (state_.fresh && (statusAt_ < 0 || now - statusAt_ >= 3000))
    {
        state_.fresh = false;
        publish(tr("设备状态超过3秒未更新，暂时停止使用旧状态。"));
    }
    if (!state_.pending.isEmpty() && pendingAt_ >= 0 && now - pendingAt_ >= 10000)
    {
        state_.outcomeUnknown = true;
        uncertainCommand_ = state_.pending;
        state_.pending.clear();
        pendingAt_ = -1;
        reject(tr("控制命令10秒未确认，执行结果未知；请核对设备，不自动重发。"));
    }
}
