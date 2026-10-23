#ifndef CUSBADGEBUTTON_H
#define CUSBADGEBUTTON_H

#include <QObject>
#include <QWidget>
#include <QIcon>
#include <QGraphicsDropShadowEffect>

class CusBadgeButton : public QWidget
{
    Q_OBJECT
public:
    explicit CusBadgeButton(QWidget *parent = nullptr);
    void setNormalBgImage(QString normalImage);
    void setHoverBgImage(QString hoverImage);
    void setPressedBgImage(QString pressedImage);
    void setText(QString txt);
    void setSizeText(int);
    void setSize(int);
    void setPos(int x,int y);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event);
    void drawBg(QPainter * painter);
    void drawText(QPainter * painter);
    bool eventFilter(QObject *watched, QEvent *event);

private:
    QGraphicsDropShadowEffect *effect;

    QString                text;
    QColor                 textColor;
    QColor                 backgroundColor;
    int                    size;
    int                    sizeText;
    QString                pixmapImage;
    qreal                  x;
    qreal                  y;

    QString normalImage;
    QString hoverImage;
    QString pressedImage;

};

#endif // CUSBADGEBUTTON_H
