// Extracted from reference customwidgets/cusdialogmask.cpp. Retains 280 ms
// opacity/move animation, OutCubic, -50 px offset, rgba(0,0,0,100) mask and
// the original 11 px shadow/radius. The overlay covers only its host window.
#include "cusdialogmask.h"
#include "ui_cusdialogmask.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QLayout>
#include <QPropertyAnimation>

CusDialogMask::CusDialogMask(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CusDialogMask),
    dialog_(parent),
    animationGroup_(this)
{
    ui->setupUi(this);
    connect(&animationGroup_, &QParallelAnimationGroup::finished, this, [this]()
    {
        if (closing_)
        {
            emit closeFinished();
        }
        else
        {
            ui->stkAnimaView->setCurrentIndex(1);
        }
    });
}

CusDialogMask::~CusDialogMask()
{
    animationGroup_.stop();
    delete ui;
}

void CusDialogMask::setupForm(int radius, bool noOffset)
{
    if (configured_ || !dialog_)
    {
        return;
    }
    displayFrame_ = findChild<QFrame *>(QStringLiteral("display_frame"));
    viewWidget_ = findChild<QFrame *>(QStringLiteral("view_frame"));
    if (!displayFrame_ || !viewWidget_)
    {
        return;
    }
    host_ = dialog_->parentWidget() ? dialog_->parentWidget()->window() : QApplication::activeWindow();
    if (host_ == dialog_)
    {
        host_.clear();
    }
    dialog_->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog_->setAttribute(Qt::WA_TranslucentBackground, true);
    dialog_->setWindowModality(host_ ? Qt::WindowModal : Qt::ApplicationModal);
    if (host_)
    {
        host_->installEventFilter(this);
    }
    displayFrame_->setStyleSheet(QStringLiteral("QFrame#display_frame { background: rgba(0,0,0,100); }"));
    ui->stkAnimaView->insertWidget(1, displayFrame_);

    backgroundOpacity_ = new QGraphicsOpacityEffect(ui->lblDisplayBg);
    ui->lblDisplayBg->setGraphicsEffect(backgroundOpacity_);
    pictureOpacity_ = new QGraphicsOpacityEffect(ui->lblDisplayPic);
    ui->lblDisplayPic->setGraphicsEffect(pictureOpacity_);
    animateBackground_ = new QPropertyAnimation(backgroundOpacity_, "opacity");
    animatePicture_ = new QPropertyAnimation(pictureOpacity_, "opacity");
    animateBackground_->setDuration(noOffset ? 150 : 280);
    animatePicture_->setDuration(noOffset ? 150 : 280);
    animationGroup_.addAnimation(animateBackground_);
    animationGroup_.addAnimation(animatePicture_);
    animateMove_ = new QPropertyAnimation(ui->lblDisplayPic, "pos", this);
    animateMove_->setEasingCurve(QEasingCurve::OutCubic);
    animateMove_->setDuration(280);
    if (!noOffset)
    {
        animationGroup_.addAnimation(animateMove_);
    }

    auto *shadow = new QGraphicsDropShadowEffect(viewWidget_);
    shadow->setBlurRadius(11);
    shadow->setColor(QColor(0, 0, 0, 100));
    shadow->setOffset(0, 3);
    viewWidget_->setGraphicsEffect(shadow);
    viewWidget_->setStyleSheet(QStringLiteral(
        "QFrame#view_frame { background: #FFFFFF; border-radius: %1px; }").arg(radius));
    configured_ = true;
    alignToHost();
}

void CusDialogMask::alignToHost()
{
    if (!dialog_ || !configured_)
    {
        return;
    }
    if (host_)
    {
        dialog_->setGeometry(QRect(host_->mapToGlobal(QPoint(0, 0)), host_->size()));
    }
    resize(dialog_->size());
    if (layout())
    {
        layout()->activate();
    }
    viewPos_ = QPoint((width() - viewWidget_->width()) / 2,
                      (height() - viewWidget_->height()) / 2);
    viewWidget_->move(viewPos_);
    ui->lblDisplayBg->setGeometry(rect());
    ui->lblDisplayPic->resize(viewWidget_->size());
    ui->lblDisplayPic->move(viewPos_);
}

bool CusDialogMask::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == host_ && (event->type() == QEvent::Move || event->type() == QEvent::Resize))
    {
        alignToHost();
    }
    return QWidget::eventFilter(watched, event);
}

bool CusDialogMask::isReadyForInput() const
{
    return configured_ && !closing_ && animationGroup_.state() != QAbstractAnimation::Running
            && ui->stkAnimaView->currentIndex() == 1;
}

void CusDialogMask::startAnimation(bool closing)
{
    if (!configured_)
    {
        if (closing)
        {
            emit closeFinished();
        }
        return;
    }
    animationGroup_.stop();
    closing_ = closing;
    alignToHost();
    ui->lblDisplayPic->setPixmap(viewWidget_->grab());
    const QPoint above(viewPos_.x(), viewPos_.y() - 50);
    const qreal from = closing ? 1.0 : 0.0;
    const qreal to = closing ? 0.0 : 1.0;
    backgroundOpacity_->setOpacity(from);
    pictureOpacity_->setOpacity(from);
    animateBackground_->setStartValue(from);
    animateBackground_->setEndValue(to);
    animatePicture_->setStartValue(from);
    animatePicture_->setEndValue(to);
    animateMove_->setStartValue(closing ? viewPos_ : above);
    animateMove_->setEndValue(closing ? above : viewPos_);
    ui->stkAnimaView->setCurrentIndex(0);
    animationGroup_.start();
}

void CusDialogMask::showAnimation()
{
    startAnimation(false);
}

void CusDialogMask::closeAnimation()
{
    if (!closing_)
    {
        startAnimation(true);
    }
}
