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

DeviceControl::DeviceControl(QObject *parent) : QObject(parent), transport_(this), timer_(this), reconnectTimer_(this)
{
    qRegisterMetaType<DeviceControlState>();
    clock_.start();
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &DeviceControl::tick);
    reconnectTimer_.setTimerType(Qt::PreciseTimer);
    connect(&reconnectTimer_, &QTimer::timeout, this, [this]()
    {
        if (!reconnectEnabled_ || !connectionRequested_ || connecting_ || state_.connected ||
            retryIntent_!=connectionIntent_ || retryGeneration_!=transport_.generation()) return;
        beginConnection(connectionIntent_);
    });
    connect(&transport_, &DeviceTransport::connected, this, [this](quint64 connection)
    {
        if (!connectionRequested_ || connection!=transport_.generation()) return;
        const quint64 intent=connectionIntent_;
        connecting_ = false;
        reconnectTimer_.stop();
        state_.connected = true;
        state_.fresh = false;
        statusAt_ = -1;
        queryAt_ = -1;
        timer_.start();
        publish(tr("已连接主控，正在读取设备状态。"));
        if (intent!=connectionIntent_ || connection!=transport_.generation() || !state_.connected) return;
        query(Sn100Protocol::ReadOnlyQuery::Status);
    });
    connect(&transport_, &DeviceTransport::disconnected, this, [this](quint64 connection)
    {
        // 底层先使旧代次失效；错误通知的接收者也可能已重连，不能再改新连接。
        if (!connectionRequested_ || connection+1!=transport_.generation()) return;
        rememberConnectionLoss();
        scheduleReconnect();
        publish(reconnectTimer_.isActive()
            ? tr("主控连接已断开，按%1毫秒间隔重连；旧业务结果仍保留待核对。").arg(reconnectMilliseconds_)
            : tr("主控连接已断开；旧设备状态已失效。"));
    });
    connect(&transport_, &DeviceTransport::errorOccurred, this, [this](const QString &message, quint64 connection)
    {
        if (!connectionRequested_ || connection+1!=transport_.generation()) return;
        rememberConnectionLoss();
        reject(message);
    });
    connect(&transport_, &DeviceTransport::wireQueued, this, [this](const QByteArray &bytes, quint64 connection)
    { if (connection==transport_.generation()) emit wireActivity(QStringLiteral("TX queued"), bytes); });
    connect(&transport_, &DeviceTransport::wireReceived, this, [this](const QByteArray &bytes, quint64 connection)
    { if (connection==transport_.generation()) emit wireActivity(QStringLiteral("RX"), bytes); });
    connect(&transport_, &DeviceTransport::messageReceived, this, [this](const QByteArray &message, quint64 connection)
    { receive(message,connection); });
}

bool DeviceControl::open(const QString &address, quint16 port)
{
    // 重复点击同一连接不会重建socket或重发控制命令。
    if (state_.address == address && state_.port == port && connectionRequested_ &&
        (connecting_ || state_.connected || reconnectTimer_.isActive()))
        return reject(tr("该主控连接已启动，请等待结果或先断开。"));
    const quint64 intent=connectionIntent_+1;
    close();
    if (intent!=connectionIntent_) return false;
    const bool unknown = state_.outcomeUnknown;
    state_ = DeviceControlState();
    state_.outcomeUnknown = unknown;
    state_.address = address;
    state_.port = port;
    connectionRequested_ = true;
    return beginConnection(intent);
}

void DeviceControl::setReconnectOptions(bool enabled, int milliseconds)
{
    const bool cancellable=connectionRequested();
    reconnectEnabled_ = enabled;
    reconnectMilliseconds_ = qMax(2000,milliseconds);
    reconnectTimer_.stop();
    // 配置可能晚于首连立即失败；保留先前连接意图，但配置本身不新建连接。
    if (connectionRequested_ && !connecting_ && !state_.connected) scheduleReconnect();
    if (cancellable!=connectionRequested())
        publish(connectionRequested()
            ? tr("已启用主控自动重连，按%1毫秒间隔等待；旧业务命令不重发。").arg(reconnectMilliseconds_)
            : tr("已停止主控自动重连，可手动再次连接。"));
}

bool DeviceControl::beginConnection(quint64 intent)
{
    if (intent!=connectionIntent_ || !connectionRequested_) return false;
    connecting_ = true;
    state_.connected = state_.fresh = false;
    statusAt_ = queryAt_ = -1;
    publish(tr("正在连接主控 %1:%2。").arg(state_.address).arg(state_.port));
    if (intent!=connectionIntent_ || !connectionRequested_) return false;
    const quint64 connection=transport_.generation()+1;
    QString message;
    const bool opened=transport_.open(state_.address,state_.port,&message);
    if (intent!=connectionIntent_ || !connectionRequested_) return false;
    // 同步失败已由错误/断线槽处理，或通知期间另有连接；不能覆盖其重试或状态。
    if (connection!=transport_.generation()) return false;
    if (!opened)
    {
        connecting_ = false;
        connectionRequested_ = false; // 地址/端口校验失败，不重复尝试无效配置。
        reconnectTimer_.stop();
        return reject(message);
    }
    return true;
}

void DeviceControl::rememberConnectionLoss()
{
    timer_.stop();
    connecting_ = false;
    state_.connected = false;
    state_.fresh = false;
    if (!state_.pending.isEmpty())
    {
        uncertainCommand_ = state_.pending;
        state_.outcomeUnknown = true;
    }
    if (state_.outcomeUnknown) uncertainAcrossConnection_ = true;
    state_.pending.clear();
    statusAt_ = queryAt_ = pendingAt_ = -1;
}

void DeviceControl::scheduleReconnect()
{
    if (!reconnectEnabled_ || !connectionRequested_ || connecting_ || state_.connected) return;
    retryIntent_ = connectionIntent_;
    retryGeneration_ = transport_.generation();
    // 与原循环计时一致，失败不追加一次完整间隔；尚在连接时下一拍只等待，不并发建连。
    if (!reconnectTimer_.isActive()) reconnectTimer_.start(reconnectMilliseconds_);
}

void DeviceControl::close()
{
    const quint64 intent=++connectionIntent_;
    connectionRequested_ = false;
    reconnectTimer_.stop();
    rememberConnectionLoss();
    transport_.close();
    if (intent!=connectionIntent_) return;
    publish(tr("主控连接已关闭。"));
}

bool DeviceControl::reject(const QString &message)
{
    state_.message = message;
    const quint64 intent=connectionIntent_, connection=transport_.generation();
    emit error(message);
    if (intent!=connectionIntent_ || connection!=transport_.generation()) return false;
    emit changed(state_);
    return false;
}

void DeviceControl::publish(const QString &message)
{
    if (!message.isEmpty()) state_.message = message;
    emit changed(state_);
}

bool DeviceControl::send(const QByteArray &payload, const QString &pending, bool ownProbe)
{
    if (!state_.connected) return reject(tr("主控未连接，请先连接设备。"));
    if (!ownProbe && reagentProbeUnresolved() && payload!="Qj" && payload!="Qk" && payload!="QH" && payload!="Mz" && payload!="MP")
        return reject(tr("单孔探测尚在收尾或执行结果待核；未并发发送其他业务/机械命令。"));
    if (!pending.isEmpty() && (!state_.pending.isEmpty() || state_.outcomeUnknown))
        return reject(tr("上一条控制命令尚未确认，不能重复提交；请核对设备状态。"));
    const quint64 intent=connectionIntent_, connection=transport_.generation();
    // 入队通知可能同步断线。先登记正在提交的控制动作，使断线能保留它的未知结果。
    if (!pending.isEmpty())
    {
        state_.pending = pending;
        pendingAt_ = clock_.elapsed();
    }
    QString message;
    const bool queued=transport_.send(payload,&message);
    if (intent!=connectionIntent_ || connection!=transport_.generation() || !state_.connected) return false;
    if (!queued)
    {
        if (!pending.isEmpty()) { state_.pending.clear(); pendingAt_=-1; }
        return reject(message);
    }
    if (!pending.isEmpty())
    {
        publish(tr("命令已进入发送队列，正在等待设备返回。"));
        if (intent!=connectionIntent_ || connection!=transport_.generation() || !state_.connected) return false;
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
    if (reagentProbeUnresolved()) return tr("单孔探测尚未收尾或执行结果待核，不能开始测试。");
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

bool DeviceControl::sendTaskReply(const QByteArray &payload)
{
    if (!payload.startsWith("Tt[") && payload!="TF" && payload!="TF[Emergency]")
        return reject(tr("不是已经准备完成的原任务或预设架回应。"));
    return send(payload);
}

bool DeviceControl::sendOriginalConfiguration(const QByteArray &payload)
{
    const bool query=payload=="Ur" || (payload.startsWith("UP[") && payload.endsWith(']'));
    const bool taskReply=payload.startsWith("Tu[");
    const bool update=payload.startsWith("Up[") || payload.startsWith("Us[");
    if (!query && !taskReply && !update) return reject(tr("不是已接入的原配置命令。"));
    if (payload.size()>Sn100Protocol::kMaxMessageBytes) return reject(tr("原配置命令超过长度上限，未截断。"));
    if (update && (!state_.fresh || state_.outcomeUnknown || !state_.pending.isEmpty() || state_.status.mainStatus>2))
        return reject(tr("设备尚未就绪或有执行结果待核对，未修改设备配置。"));
    return send(payload);
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
    uncertainAcrossConnection_ = false;
    uncertainCommand_.clear();
    publish(tr("操作者已核对当前设备状态；旧命令不重发。"));
    return true;
}

void DeviceControl::receive(const QByteArray &message, quint64 connection)
{
    // wire日志或更早的直接槽可同步关闭/重连；不能把旧包贴上当前代次。
    const auto current=[this,connection]()
    { return connection==transport_.generation() && state_.connected; };
    if (!current()) return;
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
        if (state_.outcomeUnknown && !uncertainAcrossConnection_ &&
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
        if (!current()) return;
        emit startAccepted(accepted > 0);
        if (!current()) return;
        query(Sn100Protocol::ReadOnlyQuery::Status);
    }
    // publish、startAccepted及发送日志均允许接收者重入连接操作。
    if (!current()) return;
    emit payloadReceivedForConnection(message,connection);
    if (!current()) return;
    emit payloadReceived(message);
}

void DeviceControl::tick()
{
    const qint64 now = clock_.elapsed();
    if (!state_.connected) return;
    const quint64 intent=connectionIntent_, connection=transport_.generation();
    if (queryAt_ < 0 || now - queryAt_ >= 1000) query(Sn100Protocol::ReadOnlyQuery::Status);
    if (intent!=connectionIntent_ || connection!=transport_.generation() || !state_.connected) return;
    if (state_.fresh && (statusAt_ < 0 || now - statusAt_ >= 3000))
    {
        state_.fresh = false;
        publish(tr("设备状态超过3秒未更新，暂时停止使用旧状态。"));
        if (intent!=connectionIntent_ || connection!=transport_.generation() || !state_.connected) return;
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


QString DeviceControl::reagentProbeBlocker(const QString &area, bool executing) const
{
    if (!state_.connected || !state_.fresh) return tr("主控未连接或本连接Qj状态已失效。");
    if (probeUnknown_) return tr("此前单孔探测执行结果待核，Qj或通用确认不能清除。");
    if (!executing && !probeOwner_.isEmpty()) return tr("已有单孔探测正在处理。");
    if (state_.outcomeUnknown || !state_.pending.isEmpty()) return tr("其他控制动作尚未确认。");
    if (!executing && (state_.status.ledStatus!=1 || (state_.status.mainStatus!=1 && state_.status.mainStatus!=2)))
        return tr("未处于原Idle灯配育温/就绪状态，不能探测。");
    const bool legacy=std::any_of(state_.status.subsystems.cbegin(),state_.status.subsystems.cend(),
        [](const Sn100Protocol::SubsystemStatus &s){return s.id==3;});
    if (area==QStringLiteral("r_area"))
        return requireFlag(state_.status,legacy ? 6 : 32,legacy ? 2 : 12,true,tr("试剂舱盖未关闭或该字段未知"));
    if (area==QStringLiteral("s_area"))
    {
        if (!legacy) return tr("当前协议稀释液舱盖来源尚未确认，未猜测默认关门。");
        return requireFlag(state_.status,17,3,true,tr("稀释液舱盖未关闭或该字段未知"));
    }
    return tr("目标孔区域不是原试剂/稀释液区域。");
}

bool DeviceControl::sendReagentProbe(const QString &token, quint64 connection, const QByteArray &payload,
    const QString &hole, const QString &area, std::chrono::steady_clock::time_point prepareUntil)
{
    if (token.isEmpty() || !connection || connection!=connectionGeneration() ||
        std::chrono::steady_clock::now()>=prepareUntil)
        return reject(tr("单孔探测来源或连接已失效。"));
    const QString reason=reagentProbeBlocker(area);
    if (!reason.isEmpty()) return reject(reason);
    // 窄入口仅接受一个合法孔的原8字段；没有本地token写到线上。
    if (!payload.startsWith("Mg[1][") || !payload.endsWith(']') || payload.size()>128000)
        return reject(tr("不是已准备的单孔Mg正文。"));
    QList<QByteArray> fields;
    for (int cursor=2;cursor<payload.size();)
    {
        if (payload[cursor++]!='[') return reject(tr("Mg字段不完整。"));
        const int end=payload.indexOf(']',cursor);
        if (end<cursor || end-cursor>4096) return reject(tr("Mg字段长度无效。"));
        const QByteArray value=payload.mid(cursor,end-cursor);
        for (unsigned char ch : value) if (ch<32 || ch=='[') return reject(tr("Mg字段含控制字符或嵌套括号。"));
        fields.append(value); cursor=end+1;
    }
    if (fields.size()!=9 || fields[0]!="1" || fields[1]!=hole.toLatin1() || fields[3]!=area.toLatin1())
        return reject(tr("Mg正文与冻结的单孔/区域不对应。"));
    if (std::chrono::steady_clock::now()>=prepareUntil) return reject(tr("单孔Mg入队前准备期限已过。"));
    probeOwner_=token; probeMayHaveQueued_=true;
    // 自己的入队例外由调用栈传递；重入的其他公开发送仍受全局门保护。
    return send(payload,QString(),true);
}

void DeviceControl::finishReagentProbe(const QString &token, bool unknown)
{
    if (token!=probeOwner_ || token.isEmpty()) return;
    probeUnknown_=probeUnknown_ || unknown;
    probeOwner_.clear(); probeMayHaveQueued_=false;
    publish(unknown ? tr("单孔探测执行结果待核；没有重发、退款或通用确认复位。") :
        tr("单孔探测本地等待已收尾，动作和液面资料结果分别记录。"));
}
