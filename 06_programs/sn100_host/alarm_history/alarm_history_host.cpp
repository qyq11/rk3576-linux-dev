#include "alarm_history_host.h"
#include <QApplication>
#include <QThread>

AlarmHistoryHost::AlarmHistoryHost(AlarmHistory::Store *store,QWidget *owner,QObject *parent)
    : QObject(parent),store_(store),owner_(owner)
{
    if(store)connect(store,&AlarmHistory::Store::closed,this,&AlarmHistoryHost::storageClosed,Qt::QueuedConnection);
}
bool AlarmHistoryHost::showExistingHistory(QString path,QDate today)
{
    if(!store_||!owner_||!store_->thread()||store_->thread()==qApp->thread()||!store_->thread()->isRunning())
    {emit statusChanged(tr("历史只读worker尚未就绪；没有创建或打开历史库。"),true);return false;}
    if(page_)
    {
        if(path!=path_){emit statusChanged(tr("请先关闭已有历史来源，再切换来源。"),true);return false;}
        if(page_->isClosing()){emit statusChanged(tr("历史页正在正常关闭，请等待来源释放。"),false);return false;}
        page_->raise();page_->activateWindow();return true;
    }
    auto *page=new AlarmHistoryPage(store_,owner_);page_=page;path_=path;
    connect(page,&AlarmHistoryPage::statusChanged,this,&AlarmHistoryHost::statusChanged);
    connect(page,&AlarmHistoryPage::busyChanged,this,&AlarmHistoryHost::busyChanged);
    connect(page,&QObject::destroyed,this,[this](){page_=nullptr;path_.clear();emit busyChanged(false);emit viewClosed();});
    return page->openExistingHistory(path,today);
}
bool AlarmHistoryHost::requestLeave(QString action)
{
    if(!page_)return true;
    if(!page_->requestClose())
    {emit statusChanged(tr("历史日志正在取消/关闭，请等只读来源释放后再%1；长SQL取消时延未获保证。").arg(action),false);return false;}
    return page_.isNull();
}
