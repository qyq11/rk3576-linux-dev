#include "cusflageimage.h"
#include "ui_cusflageimage.h"
#include <QPainter>
#include <QDebug>

CusFlageImage::CusFlageImage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CusFlageImage)
{
    ui->setupUi(this);
    flash = false;
    isOn = true;
    flashImage = ":/shell/main_state/status_off.png";
    animFlash = new QTimeLine(800,this);
    animFlash->setFrameRange(0,1000);
    connect(animFlash,SIGNAL(frameChanged(int)),this,SLOT(slotAnimLoop(int)));
    connect(animFlash,SIGNAL(finished()),this,SLOT(slotAnimFinished()));
    setImageOpacity(1.0);
}

CusFlageImage::~CusFlageImage()
{
    delete ui;
}

void CusFlageImage::mousePressEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    // 未连接设备时保持离线灯，不允许鼠标把未知状态变成闪烁告警。
}

void CusFlageImage::setFlashImage(QString imageSet)
{
    if(imageSet == flashImage)
        return;
    flashImage = imageSet;
    setImageOpacity(1.0);
}

void CusFlageImage::setFlash(bool flashSet)
{
    if(flash == flashSet)
        return;
    flash = flashSet;
    if(flash)
        animFlash->start();
    else
    {
        animFlash->stop();
        setImageOpacity(1.0);
    }
}

void CusFlageImage::slotAnimLoop(int loopVal)
{
    if(isOn)
        setImageOpacity(1 - ((double)loopVal/1000.0));
    else
        setImageOpacity((double)loopVal/1000.0);
}

void CusFlageImage::slotAnimFinished()
{
    if(flash)
    {
        isOn = !isOn;
        animFlash->start();
    }
    else
    {
        setImageOpacity(1.0);
    }
}

void CusFlageImage::setImageOpacity(double Opacity)
{
    curOpacity = Opacity;
    QPixmap pix1_(flashImage);
    pix1_ = pix1_.scaled(42,79,Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap temp(pix1_.size());
    temp.fill(Qt::transparent);
    QPainter painter(&temp);
    painter.setOpacity(curOpacity);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawPixmap(0, 0, pix1_);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.end();
    pix1_ = temp;
    ui->flashImage->setPixmap(pix1_);
}


