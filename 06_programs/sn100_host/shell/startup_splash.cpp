#include "startup_splash.h"

#include <QDebug>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QLabel>
#include <QMovie>
#include <QScreen>

StartupSplash::StartupSplash(QWidget *parent) :
    QWidget(parent),
    animation_(new QLabel(this)),
    movie_(new QMovie(QStringLiteral(":/shell/logo/loading.gif"), QByteArray(), this))
{
    setObjectName(QStringLiteral("startupSplash"));
    animation_->setObjectName(QStringLiteral("startupAnimation"));
    animation_->setAlignment(Qt::AlignCenter);
    auto *opacity = new QGraphicsOpacityEffect(animation_);
    opacity->setOpacity(0.7);
    animation_->setGraphicsEffect(opacity);
    animation_->setMovie(movie_);
    movie_->setCacheMode(QMovie::CacheNone);
    movie_->setSpeed(100);
    if (parent) parent->installEventFilter(this);
    hide();
}

bool StartupSplash::start(QString *error)
{
    if (error) error->clear();
    if (!parentWidget() || !movie_->isValid() || !movie_->jumpToFrame(0))
    {
        const QString message = tr("启动动画资源无法读取，继续打开主界面。");
        if (error) *error = message;
        setProperty("startupAnimationValid", false);
        qWarning().noquote() << "STARTUP_ANIMATION_UNAVAILABLE" << movie_->lastErrorString();
        return false;
    }
    const QSize original = movie_->currentPixmap().size();
    const QSize screen = QGuiApplication::primaryScreen()
        ? QGuiApplication::primaryScreen()->geometry().size() : parentWidget()->size();
    // 与原main相同：横图宽取屏幕1/3，竖图高取屏幕1/3，另一边等比缩放。
    const QSize bounds = original.width() > original.height()
        ? QSize(screen.width() / 3, screen.height())
        : QSize(screen.width(), screen.height() / 3);
    animationSize_ = original.scaled(bounds, Qt::KeepAspectRatio);
    movie_->setScaledSize(animationSize_);
    updateGeometryForHost();
    setProperty("startupAnimationValid", true);
    setProperty("startupAnimationResource", movie_->fileName());
    setProperty("startupAnimationFrameCount", movie_->frameCount());
    show();
    raise();
    movie_->start();
    qInfo().noquote() << "STARTUP_ANIMATION_STARTED" << movie_->fileName()
        << "frames=" << movie_->frameCount() << "size=" << animationSize_;
    return true;
}

void StartupSplash::finish()
{
    movie_->stop();
    hide();
    qInfo() << "STARTUP_ANIMATION_FINISHED";
}

void StartupSplash::updateGeometryForHost()
{
    if (!parentWidget()) return;
    setGeometry(parentWidget()->rect());
    animation_->setGeometry((width() - animationSize_.width()) / 2,
        (height() - animationSize_.height()) / 2, animationSize_.width(), animationSize_.height());
}

bool StartupSplash::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize) updateGeometryForHost();
    return QWidget::eventFilter(watched, event);
}
