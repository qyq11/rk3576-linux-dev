#include "cuscheckheader.h"
#include <QPainter>
#include <QMouseEvent>

//CusCheckHeader::CusCheckHeader(Qt::Orientation orientation,QWidget * parent = 0):QHeaderView(orientation, parent)
//{
//    checkState = Qt::Unchecked;
//}

void CusCheckHeader:: setCheckState(Qt::CheckState boxChecked)
{
    checkState = boxChecked;
    this->updateSection(m_checkColIdx);
    emit signalCheckStausChange(checkState);
}


void CusCheckHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    painter->save();
    QHeaderView::paintSection(painter, rect, logicalIndex);
    painter->restore();
    if (logicalIndex == m_checkColIdx)
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QIcon m_icon;
        if(checkState == Qt::Checked)
            m_icon = QIcon(":/icons/toggle/svg/production/ic_check_box_24px.svg");
        else
            m_icon = QIcon(":/icons/toggle/svg/production/ic_check_box_outline_blank_24px.svg");
        int m_iconSize = 24;
        QPixmap pixmap = m_icon.pixmap(m_iconSize, m_iconSize);
        if (!pixmap.isNull())
        {
            const qreal pX = static_cast<qreal>((60)- m_iconSize)/2;
            const qreal pY = static_cast<qreal>((height())- m_iconSize)/2;
            const qreal z = m_iconSize/24;

            QTransform t;
            t.translate(pX, pY);
            t.scale(z, z);
            painter->setTransform(t);

            QPainter icon(&pixmap);
            icon.setCompositionMode(QPainter::CompositionMode_SourceIn);
            icon.fillRect(pixmap.rect(), imgColor);
            painter->drawPixmap(rect.x(), rect.y(), pixmap);
        }
        painter->restore();
    }
}

void CusCheckHeader::mousePressEvent(QMouseEvent *event)
{
    if (visualIndexAt(event->pos().x()) == m_checkColIdx)
    {
        if(checkState == Qt::Unchecked)
            checkState = Qt::Checked;
        else
            checkState = Qt::Unchecked;
        this->updateSection(m_checkColIdx);
        emit signalCheckStausChange(checkState);
    }
    QHeaderView::mousePressEvent(event);
}



