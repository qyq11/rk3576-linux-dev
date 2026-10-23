#ifndef CUSFLAGEIMAGE_H
#define CUSFLAGEIMAGE_H

#include <QWidget>
#include <QTimeLine>

namespace Ui {
class CusFlageImage;
}

class CusFlageImage : public QWidget
{
    Q_OBJECT

public:
    explicit CusFlageImage(QWidget *parent = nullptr);
    ~CusFlageImage();
    void mousePressEvent(QMouseEvent *event);
    void setFlashImage(QString imageSet);
    void setFlash(bool flashSet);

private:
    Ui::CusFlageImage *ui;
    QTimeLine *animFlash;
    bool isOn;
    bool flash;
    QString flashImage;
    double curOpacity;

    void setImageOpacity(double Opacity);

private slots:
    void slotAnimFinished();
    void slotAnimLoop(int loopVal);
};

#endif // CUSFLAGEIMAGE_H
