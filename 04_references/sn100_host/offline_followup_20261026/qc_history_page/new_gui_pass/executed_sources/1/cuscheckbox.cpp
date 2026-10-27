#include "cuscheckbox.h"
#include <QPainter>
#include <QIcon>
#include <QTableWidget>

CusCheckBox::CusCheckBox(QWidget *parent) : QWidget(parent)
{
    checkState = Qt::Unchecked;
    isAuto = false;
}

 void CusCheckBox::setCheckState(Qt::CheckState checkState)
 {
     this->checkState = checkState;
     this->update();
 }

void CusCheckBox::setText(QString txt)
{
    boxText = txt;
    this->update();
}

Qt::CheckState CusCheckBox::getCheckState()
{
    return checkState;
}

bool CusCheckBox::isChecked()
{
    return checkState == Qt::Checked;
}

void CusCheckBox::setAutoEx(bool isAuto)
{
    this->isAuto = isAuto;
}

QString CusCheckBox::getText()
{
    return boxText;
}

void CusCheckBox::mousePressEvent(QMouseEvent *)
{
    if (!isEnabled())
    {
        return;
    }
    if(checkState == Qt::Unchecked)
        checkState = Qt::Checked;
    else if(!isAuto)
        checkState = Qt::Unchecked;
    emit signalClicked();
    emit signalClicked(checkState == Qt::Checked);
    //emit signalClicked(this);
    this->update();
}


void CusCheckBox::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    drawCheckImage(&painter);
    drawText(&painter);
}

void CusCheckBox::drawCheckImage(QPainter *painter)
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
        painter->drawPixmap(0, 0, pixmap);
    }
    painter->restore();
}

void CusCheckBox::drawText(QPainter *painter)
{
    painter->save();
    QRectF textRectf = QRectF(0,0,this->width(),this->height());
    QColor nameTextColor = "#4A4A4A";
    QFont font;
//    font.setPixelSize(16);
    font = this->font();
    painter->setFont(font);
    painter->setPen(nameTextColor);
    painter->drawText(textRectf,Qt::AlignCenter, boxText);
    painter->restore();
}
