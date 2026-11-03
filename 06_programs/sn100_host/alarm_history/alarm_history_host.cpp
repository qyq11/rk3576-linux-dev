#include "alarm_history_host.h"
#include <QApplication>
#include <QDir>
#include <QThread>

AlarmHistoryHost::AlarmHistoryHost(AlarmHistory::Store *store,QWidget *owner,QObject *parent)
    : QObject(parent),store_(store),owner_(owner)
{
    if(store)connect(store,&AlarmHistory::Store::closed,this,&AlarmHistoryHost::storageClosed,Qt::QueuedConnection);
}
AlarmHistoryPage *AlarmHistoryHost::createPage(QString path)
{
    if(!owner_)return nullptr;
    auto *page=new AlarmHistoryPage(store_,owner_);page_=page;path_=path;
    if(currentEnabled_)
    {
        page->enableCurrentAlarms(currentHistoryPath_,QDate::currentDate());
        page->setCurrentStatus(currentMessage_,currentError_);
    }
    connect(page,&AlarmHistoryPage::statusChanged,this,&AlarmHistoryHost::statusChanged);
    connect(page,&AlarmHistoryPage::currentDisplayRejected,this,&AlarmHistoryHost::currentDisplayRejected);
    connect(page,&AlarmHistoryPage::busyChanged,this,&AlarmHistoryHost::busyChanged);
    connect(page,&QObject::destroyed,this,[this]()
    {page_=nullptr;path_.clear();emit busyChanged(false);emit viewClosed();});
    return page;
}

bool AlarmHistoryHost::enableCurrentAlarms(QString historyPath)
{
    if(currentEnabled_||!QDir::isAbsolutePath(historyPath)||historyPath.contains(QChar::Null))return false;
    if(page_&&(page_->isClosing()||path_!=historyPath))return false;
    currentHistoryPath_=historyPath;currentEnabled_=true;
    currentMessage_=tr("尚未收到本连接的完整当前告警消息。");
    if(page_)page_->enableCurrentAlarms(historyPath,QDate::currentDate());
    return true;
}

bool AlarmHistoryHost::showExistingHistory(QString path,QDate today)
{
    if(!store_||!owner_||!store_->thread()||store_->thread()==qApp->thread()||!store_->thread()->isRunning())
    {emit statusChanged(tr("历史只读worker尚未就绪；没有创建或打开历史库。"),true);return false;}
    if(page_)
    {
        if(path!=path_){emit statusChanged(tr("请先关闭已有历史来源，再切换来源。"),true);return false;}
        if(page_->isClosing()){emit statusChanged(tr("历史页正在正常关闭，请等待来源释放。"),false);return false;}
        if(currentEnabled_)page_->showHistoricalRecords();
        // 日历或错误确认仍由原页面处理，不用raise抢走它们的焦点。
        if(!page_->isBusy()){page_->raise();page_->activateWindow();}
        return true;
    }
    auto *page=createPage(path);
    return page&&page->openExistingHistory(path,today);
}

bool AlarmHistoryHost::showCurrent()
{
    if(!currentEnabled_||!owner_||(page_&&page_->isClosing()))return false;
    deliverRows({},currentMessage_,currentError_);
    if(page_)page_->queueCurrentHistoryRefresh();
    return true;
}

void AlarmHistoryHost::deliverRows(QVector<AlarmHistory::CurrentAlarmRow> rows,QString message,bool error)
{
    if(!currentEnabled_||!owner_||(page_&&page_->isClosing()))return;
    // 先于create/show/raise记录活动状态，不能用弹窗后的状态反推。
    const bool activeBeforeDelivery=page_&&page_->isActiveWindow();
    auto *page=page_?page_.data():createPage(currentHistoryPath_);
    if(page)page->showCurrentRows(rows,message,activeBeforeDelivery,error);
}

void AlarmHistoryHost::showCurrentObservation(AlarmHistory::CurrentAlarmObservation observation)
{
    deliverRows(observation.rows,currentMessage_,currentError_);
    if(page_&&!page_->isClosing())page_->queueCurrentHistoryRefresh();
}

void AlarmHistoryHost::showReceivedEvents(QVector<AlarmHistory::CurrentAlarmRow> rows,QString message)
{
    deliverRows(rows,message);
}

void AlarmHistoryHost::setCurrentStatus(QString message,bool error)
{
    currentMessage_=message;currentError_=error;
    if(page_)page_->setCurrentStatus(message,error);
}
bool AlarmHistoryHost::requestLeave(QString action)
{
    if(!page_)return true;
    if(!page_->requestClose())
    {emit statusChanged(tr("历史日志正在取消/关闭，请等只读来源释放后再%1；长SQL取消时延未获保证。").arg(action),false);return false;}
    return page_.isNull();
}

void AlarmHistoryHost::originalEventsCommitted(AlarmHistory::Rows rows)
{
    if (page_) page_->originalEventsCommitted(rows);
}
