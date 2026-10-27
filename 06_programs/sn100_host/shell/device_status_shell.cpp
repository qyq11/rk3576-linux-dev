#include "frmmain.h"
#include "ui_frmmain.h"
#include "protocol/status_replay.h"
#include "protocol/status_loopback.h"
#include "instrument/instrument_page.h"

#include <QFontMetrics>

namespace
{
QString mainStateText(int state)
{
    static const char *const names[] = {
        "未就绪", "育温中", "准备就绪", "准备测试", "准备测试", "正在测试", "暂停完成"
    };
    return state >= 0 && state < 7 ? QString::fromUtf8(names[state]) : frmMain::tr("状态异常");
}

QString observedStateText(const Sn100Protocol::StatusSnapshot &snapshot, QString *color)
{
    // 文件回放与本机模拟使用同一原版主状态/灯状态映射，来源提示分别处理。
    const int main = snapshot.mainStatus;
    const int led = snapshot.ledStatus;
    if (led == 0) return frmMain::tr("等待连接");
    if (led == 1)
    {
        *color = QStringLiteral("#38BF65");
        return mainStateText(main);
    }
    if (led == 2 || led == 4)
    {
        *color = QStringLiteral("#38BF65");
        return main <= 2 ? frmMain::tr("仪器动作") : main <= 5 ? mainStateText(main) : frmMain::tr("状态异常");
    }
    if (led == 5)
    {
        *color = QStringLiteral("rgb(42,118,242)");
        return main == 5 ? frmMain::tr("暂停中") : main == 6 ? frmMain::tr("已暂停") : frmMain::tr("暂停测试");
    }
    if (led == 6)
    {
        *color = QStringLiteral("rgb(249,192,75)");
        return frmMain::tr("试剂不足");
    }
    *color = QStringLiteral("#DB4437");
    return led == 3 && main == 5 ? frmMain::tr("异常停止") : frmMain::tr("异常状态");
}
}

void frmMain::setStatusReplayState(const Sn100Protocol::ReplayState &state)
{
    if (instrument_) instrument_->setReplayState(state);
    readOnlyStatusEnabled_ = true;
    readOnlyStatusColor_ = QStringLiteral("rgb(170,170,170)");
    QString text = tr("未知");
    // 对照原slotTimeTickerOut的主状态/灯状态文字；回放不复制其动作联动。
    if (!state.error.isEmpty())
    {
        text = tr("输入失败");
        readOnlyStatusColor_ = QStringLiteral("#DB4437");
    }
    else if (state.finished) text = state.stopped ? tr("已停止") : tr("已结束");
    else if (!state.fresh || !state.hasSnapshot)
    {
        text = state.linkState == QStringLiteral("status-timeout") ? tr("已超时") : tr("未知");
    }
    else
    {
        text = observedStateText(state.snapshot, &readOnlyStatusColor_);
    }
    readOnlyStatusText_ = tr("回放/%1").arg(text);
    readOnlyStatusDetail_ = tr("离线文件回放；真实仪器状态未知，设备动作禁用。\n"
        "事件 %1/%2；文件记录时间 %3ms（虚拟时间，不是响应实测）；连接代次 %4\n"
        "动作 %5；回放链路 %6；最新有效状态 %7")
        .arg(state.eventIndex).arg(state.eventCount).arg(state.sourceTimeMs).arg(state.epoch)
        .arg(state.action, state.linkState, state.fresh ? tr("是") : tr("否"));
    if (state.hasSnapshot && state.fresh)
        readOnlyStatusDetail_ += tr("\n记录型号 %1；主状态 %2；灯状态 %3；错误类型 %4")
            .arg(state.snapshot.model).arg(state.snapshot.mainStatus).arg(state.snapshot.ledStatus).arg(state.snapshot.errorWorkType);
    if (!state.error.isEmpty()) readOnlyStatusDetail_ += QStringLiteral("\n") + state.error;
    if (!state.notices.isEmpty()) readOnlyStatusDetail_ += QStringLiteral("\n") + state.notices.last().left(512);
    setProperty("statusReplayFresh", state.fresh && state.hasSnapshot && !state.finished);
    setProperty("statusReplayLink", state.linkState);
    setProperty("statusReplayText", readOnlyStatusText_);
    if (instrument_) updateInstrumentStatus();
    else updateReadOnlyStatusDisplay();
}

void frmMain::setLoopbackStatusState(const Sn100Protocol::LoopbackStatusState &state)
{
    readOnlyStatusEnabled_ = true;
    readOnlyStatusColor_ = QStringLiteral("rgb(170,170,170)");
    const bool fresh = state.running && state.connected && state.fresh && state.hasSnapshot
        && state.error.isEmpty() && !state.traceOverflow;
    QString text = !state.running ? tr("已停止") : !state.connected ? tr("等待连接")
        : fresh ? observedStateText(state.snapshot, &readOnlyStatusColor_) : tr("未知");
    if (!state.error.isEmpty() || state.traceOverflow)
    {
        text = state.traceOverflow ? tr("记录超限") : tr("失败");
        readOnlyStatusColor_ = QStringLiteral("#DB4437");
    }
    readOnlyStatusText_ = tr("模拟/%1").arg(text);
    readOnlyStatusDetail_ = tr("自建127.0.0.1:%1本机模拟端；真实仪器状态未知，设备动作禁用。\n"
        "运行 %2；连接代次 %3；链路 %4；最新有效观测 %5\n"
        "本机单调时间 %6ms；观测时间 %7ms；成功/失败请求 %8/%9")
        .arg(state.port).arg(state.runId).arg(state.connectionGeneration).arg(state.linkState)
        .arg(fresh ? tr("是") : tr("否")).arg(state.elapsedMs).arg(state.statusAtMs)
        .arg(state.requestsSucceeded).arg(state.requestsFailed);
    if (state.hasRequestResult)
        readOnlyStatusDetail_ += tr("\n最后请求终态 %1；最新观测不反转旧请求终态。")
            .arg(state.lastRequestSuccess ? tr("成功") : tr("失败"));
    if (!state.detail.isEmpty()) readOnlyStatusDetail_ += QStringLiteral("\n") + state.detail.left(512);
    if (!state.error.isEmpty()) readOnlyStatusDetail_ += tr("\n运行错误：%1").arg(state.error.left(512));
    if (instrument_)
        instrument_->setReadOnlyStatus(state.snapshot, fresh, tr("模拟"), readOnlyStatusDetail_);
    setProperty("statusLoopbackFresh", fresh);
    setProperty("statusLoopbackLink", state.linkState);
    setProperty("statusLoopbackText", readOnlyStatusText_);
    setProperty("statusLoopbackRun", QVariant::fromValue(state.runId));
    setProperty("statusLoopbackGeneration", QVariant::fromValue(state.connectionGeneration));
    if (instrument_) updateInstrumentStatus();
    else updateReadOnlyStatusDisplay();
}

void frmMain::updateReadOnlyStatusDisplay()
{
    if (!readOnlyStatusEnabled_ || currentMainPage() != 7) return;
    // 原81px区域、字体、几何不变；全文与来源放tooltip，其他业务页仍显示自身状态。
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(
        readOnlyStatusText_, Qt::ElideRight, ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(QStringLiteral("QLabel{color:%1;}").arg(readOnlyStatusColor_));
    ui->lblDivState->setToolTip(readOnlyStatusDetail_);
    ui->statusArea->setToolTip(readOnlyStatusDetail_);
}
