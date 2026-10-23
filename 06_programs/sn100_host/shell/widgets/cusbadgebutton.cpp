#include "cusbadgebutton.h"
#include <QPainter>
#include <QPixmap>
#include <QEvent>
#include <QDebug>
#include <QPainterPath>


CusBadgeButton::CusBadgeButton(QWidget *parent) : QWidget(parent)
{
    effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(16);
    effect->setColor(QColor(0, 0, 0, 80));
    effect->setOffset(0, 6);
//    normalState->assignProperty(effect, "offset", QPointF(0, 6));
//    normalState->assignProperty(effect, "blurRadius", 16);
//    pressedState->assignProperty(effect, "offset", QPointF(0, 11));
//    pressedState->assignProperty(effect, "blurRadius", 28);


    this->setGraphicsEffect(effect);
    size = 52;
    sizeText = 15;
    text = "0";
    x = 18;
    y = 18;
    backgroundColor = QColor(232,51,35);
    textColor = "white";
    setMouseTracking(true);
    this->installEventFilter(this);
    //this->setMinimumSize(52,52);
}

void CusBadgeButton::setNormalBgImage(QString normalImage)
{
    if(this->normalImage != normalImage)
    {
        this->normalImage = normalImage;
        if(!pixmapImage.isEmpty() || pixmapImage != normalImage)
            pixmapImage = normalImage;
        update();
    }
}

void CusBadgeButton::setHoverBgImage(QString hoverImage)
{
    if(this->hoverImage != hoverImage)
        this->hoverImage = hoverImage;
}

void CusBadgeButton::setPressedBgImage(QString pressedImage)
{
    if(this->pressedImage != pressedImage)
        this->pressedImage = pressedImage;
}

void CusBadgeButton::setText(QString txt)
{
    if(this->text != txt)
    {
        this->text = txt;
        update();
    }
}

void CusBadgeButton::setSizeText(int sizeSet)
{
    sizeText = sizeSet;
    update();
}

void CusBadgeButton::setSize(int size)
{
    this->size = size;
    update();
}

void CusBadgeButton::setPos(int x, int y)
{
    this->x = x;
    this->y = y;
}

void CusBadgeButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawBg(&painter);
    if(text != "0")
        drawText(&painter);
}

void CusBadgeButton::drawBg(QPainter *painter)
{
    painter->save();
    const qreal hs = size/2;

    QBrush brush;
    brush.setStyle(Qt::SolidPattern);
    brush.setColor("transparent");
    painter->setPen(Qt::NoPen);
    painter->setBrush(brush);
    painter->drawEllipse(QRectF((width()-size)/2, (height()-size)/2,
                               size, size));
    QPainterPath path;
    path.addEllipse(width()/2-hs, height()/2-hs, size, size);
    painter->setClipPath(path);
    QImage image = QImage(pixmapImage);
    QPixmap pixmap = QPixmap::fromImage(image.scaled(size, size,
                                             Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation));
    painter->drawPixmap(QRect(width()/2-hs, height()/2-hs, size, size),pixmap);
    painter->restore();
}

void CusBadgeButton::drawText(QPainter *painter)
{
    painter->save();
    QFont font("Noto Sans CJK SC");
    font.setPixelSize(12);
    painter->setFont(font);
    painter->translate(x, y);

    QBrush brush;
    brush.setStyle(Qt::SolidPattern);
    brush.setColor(isEnabled() ? backgroundColor : "transparent");
    painter->setBrush(brush);
    painter->setPen(Qt::NoPen);

    const int s = sizeText;

    QRectF r(0, 0, s, s);
    r.translate(QPointF((size-s), (size-s))/2);

    painter->drawEllipse(r);
    painter->setPen(textColor);
    painter->setBrush(Qt::NoBrush);
    painter->drawText(r.translated(0, -0.5), Qt::AlignCenter, text);

    painter->restore();
}

bool CusBadgeButton::eventFilter(QObject *watched, QEvent *event)
{
    // 脱机禁用图标不得因直接投递鼠标事件发出设备操作信号。
    if (!isEnabled())
    {
        return QWidget::eventFilter(watched, event);
    }
    if (event->type() == QEvent::MouseButtonPress) {
        pixmapImage = pressedImage;
        update();
    }else if (event->type() == QEvent::MouseButtonRelease) {
        pixmapImage = hoverImage;
        update();
        emit clicked();
    }else if (event->type() == QEvent::Enter) {
        pixmapImage = hoverImage;
        update();
        return true;
    } else if (event->type() == QEvent::Leave
               || event->type() ==QEvent::FocusOut) {
        pixmapImage = normalImage;
        update();
    }
    return QWidget::eventFilter(watched, event);
}
