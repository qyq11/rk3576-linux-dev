#include "cusracklistwidget.h"
#include <QWheelEvent>

CusRackListWidget::CusRackListWidget(QWidget *parent):QListWidget(parent)
{
    scrollerRack = QScroller::scroller(this);
    setVerticalScrollMode(QListWidget::ScrollPerPixel);
    setScrollEnabled(true);
}

void CusRackListWidget::setScrollEnabled(bool enabled)
{
    scrollEnabled = enabled;
    if(enabled)
    {
        scrollerRack->grabGesture(this,QScroller::LeftMouseButtonGesture);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    }
    else
    {
        scrollerRack->ungrabGesture(this);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
}

void CusRackListWidget::wheelEvent(QWheelEvent *event)
{
    if(scrollEnabled)
        QListWidget::wheelEvent(event);
    else
        event->ignore();
}

void CusRackListWidget::mouseMoveEvent(QMouseEvent *event)
{
    if(scrollEnabled)
        QListWidget::mouseMoveEvent(event);
    else
        event->ignore();
}
