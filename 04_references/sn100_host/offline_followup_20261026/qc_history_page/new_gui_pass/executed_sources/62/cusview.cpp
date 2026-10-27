#include "cusview.h"

#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QPainterPath>

CusView::CusView(QWidget *parent) : QWidget(parent)
{
    effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(11);
    effect->setColor(QColor(0, 0, 0, 50));
    effect->setOffset(0, 3);
    this->setGraphicsEffect(effect);

    backgroundColor = "#FFFFFF";
    graBackground = false;
    radius = 0;
}

void CusView::setRadius(int value)
{
    radius = value;
    //effect->setBlurRadius(radius);
    this->update();
}

void CusView::setBackGroundImage(QString backGroundBg)
{
    this->backGroundBg = backGroundBg;
    this->update();
}

void CusView::setShadowEffect(int align)
{
    if(align == Qt::AlignTop)
        effect->setOffset(0, -3);
    else if(align == Qt::AlignRight)
        effect->setOffset(3, 0);
    else if(align == Qt::AlignLeft)
        effect->setOffset(-3, 0);
    else
        effect->setOffset(0, 3);
}

void CusView::setBackgroundColor(QColor backgroundColor)
{
    this->backgroundColor = backgroundColor;
    this->update();
}

void CusView::setGraBackgroundColor(QLinearGradient graBackgroundColor)
{
    if(!graBackground)
        graBackground = true;
    this->graBackgroundColor = graBackgroundColor;
    this->update();
}

void CusView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);

    QPainterPath positionPath;
//    positionPath.addRect(this->rect());
    positionPath.addRoundedRect(this->rect(),radius,radius);
    if(backGroundBg.isEmpty())
    {
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.setBrush(backgroundColor);
        painter.drawPath(positionPath);
        painter.restore();
        if(graBackground)
        {
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(graBackgroundColor);
            painter.drawPath(positionPath);
            painter.restore();
        }
    }
    else
    {
        QString bgPath = backGroundBg;
        QPixmap pix;
        pix.load(bgPath);
        painter.save();
        painter.drawPixmap(0,0,pix);
        painter.restore();
    }

}


