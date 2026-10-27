#ifndef CUSVIEW_H
#define CUSVIEW_H

#include <QGraphicsDropShadowEffect>
#include <QWidget>

class CusView : public QWidget
{
    Q_OBJECT
public:
    explicit CusView(QWidget *parent = nullptr);

    void setBackgroundColor(QColor backgroundColor);
    void setGraBackgroundColor(QLinearGradient graBackgroundColor);
    void setShadowEffect(int align);
    void setRadius(int value);
    void setBackGroundImage(QString backGroundBg);

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

private:
    int radius;
    bool graBackground;
    QColor backgroundColor;
    QString backGroundBg;
    QLinearGradient graBackgroundColor;
    QGraphicsDropShadowEffect *effect;

};

#endif // CUSVIEW_H
