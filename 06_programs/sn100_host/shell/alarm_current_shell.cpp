#include "frmmain.h"
#include "ui_frmmain.h"
#include "alarm_history/alarm_history_host.h"
#include "alarm_history/original_alarm_receiver.h"
#include "alarm_history/original_current_alarm_state.h"
#include "instrument/device_control.h"
#include <QDebug>
#include <QTimer>

bool frmMain::enableOriginalCurrentAlarms(const OriginalRuntime::SystemProfile &profile,
                                        int queryWaitMilliseconds)
{
    if(currentAlarmState_||!deviceControl_||!alarmHistory_||!originalAlarmReceiver_||
        queryWaitMilliseconds<1||queryWaitMilliseconds>60000||
        !alarmHistory_->enableCurrentAlarms(alarmHistoryPath_))return false;
    currentAlarmWaitMilliseconds_=queryWaitMilliseconds;
    auto *observer=new AlarmHistory::OriginalCurrentAlarmState(profile.alarms,this);
    currentAlarmState_=observer;
    connect(observer,&AlarmHistory::OriginalCurrentAlarmState::stateChanged,this,
        [this](AlarmHistory::CurrentAlarmQueryState state)
    {
        if(alarmHistory_)alarmHistory_->setCurrentStatus(state.message,!state.valid);
        ui->btnAbnormal->setToolTip(state.message);
        qInfo().noquote()<<"ORIGINAL_CURRENT_ALARM_STATE"<<"connection="<<state.connection
            <<"valid_observation="<<state.valid<<"refresh_pending="<<state.refreshPending
            <<"local_refresh="<<state.refreshId<<state.message;
    });
    connect(observer,&AlarmHistory::OriginalCurrentAlarmState::currentObserved,this,
        [this](AlarmHistory::CurrentAlarmObservation observation)
    {
        if(!currentAlarmState_||!deviceControl_||
            observation.connection!=deviceControl_->connectionGeneration()||!deviceControl_->state().connected)return;
        qInfo()<<"ORIGINAL_CURRENT_ALARM_OBSERVED"<<observation.command<<"rows="<<observation.rows.size()
               <<"connection="<<observation.connection<<"history_append=false";
        if(alarmHistory_)alarmHistory_->showCurrentObservation(observation);
    });
    connect(alarmHistory_,&AlarmHistoryHost::currentDisplayRejected,this,[this](const QString &reason)
    {
        if(currentAlarmState_&&deviceControl_)
            currentAlarmState_->invalidateCurrent(deviceControl_->connectionGeneration(),reason);
    });
    connect(deviceControl_,&DeviceControl::changed,this,[this](const DeviceControlState &state)
    {
        if(currentAlarmState_&&deviceControl_)
            currentAlarmState_->setConnection(deviceControl_->connectionGeneration(),state.connected);
    });
    connect(deviceControl_,&DeviceControl::payloadReceivedForConnection,this,
        [this](const QByteArray &payload,quint64 connection)
    {
        if(currentAlarmState_)currentAlarmState_->receivePayload(payload,connection,QDateTime::currentDateTime());
    });
    connect(originalAlarmReceiver_,&AlarmHistory::OriginalAlarmReceiver::newEventsReceived,this,
        [this](quint64 request,const AlarmHistory::Rows &rows)
    {
        if(!alarmHistory_||!deviceControl_||!deviceControl_->state().connected)return;
        QVector<AlarmHistory::CurrentAlarmRow> display;
        display.reserve(rows.size());
        for(const auto &row:rows)
            display.append({row.errorCode,row.description,row.measure,row.dateTime});
        alarmHistory_->showReceivedEvents(display,
            tr("收到新告警事件%1条；历史是否保存以存储回执为准，当前全量状态未由事件确认。").arg(rows.size()));
        qInfo()<<"ORIGINAL_ALARM_EVENTS_DISPLAYED"<<request<<"rows="<<display.size()<<"history_saved=false";
    });
    connect(originalAlarmReceiver_,&AlarmHistory::OriginalAlarmReceiver::emptyBoardNotificationReceived,this,[this]()
    {
        if(alarmHistory_&&deviceControl_&&deviceControl_->state().connected)
            alarmHistory_->showReceivedEvents({},tr("收到完整QL新异常空通知；没有新增历史行，不代表全部正常。"));
    });
    observer->setConnection(deviceControl_->connectionGeneration(),deviceControl_->state().connected);
    return true;
}

void frmMain::queryCurrentAlarms()
{
    if(!currentAlarmState_||!alarmHistory_||!deviceControl_)return;
    const quint64 connection=deviceControl_->connectionGeneration();
    const DeviceControlState state=deviceControl_->state();
    currentAlarmState_->setConnection(connection,state.connected);
    // 原入口先空刷新并打开当前页，再判断设备是否已连接；禁止关闭过程中重开。
    if(!alarmHistory_->showCurrent())return;
    if(!state.connected||!state.fresh||state.status.ledStatus==0)
    {
        currentAlarmState_->invalidateCurrent(connection,
            tr("尚无有效的主控连接状态，当前告警未查询；旧显示仅供核对。"));
        return;
    }
    const quint64 request=currentAlarmState_->beginRefresh(connection);
    if(!request)return;
    const bool queued=deviceControl_->query(Sn100Protocol::ReadOnlyQuery::Alarm);
    if(!currentAlarmState_)return;
    currentAlarmState_->finishRefreshQueue(connection,request,queued);
    if(!queued)return;
    // 仅解除本地等待并提示未知，不发重试、不构造ACK；线上无请求编号，迟到包只记接收事实。
    QTimer::singleShot(currentAlarmWaitMilliseconds_,this,[this,connection,request]()
    {if(currentAlarmState_)currentAlarmState_->refreshTimedOut(connection,request);});
}
