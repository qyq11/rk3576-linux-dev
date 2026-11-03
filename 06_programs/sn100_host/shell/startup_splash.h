#ifndef SN100_STARTUP_SPLASH_H
#define SN100_STARTUP_SPLASH_H

#include <QWidget>

class QLabel;
class QMovie;

// 原启动GIF放在显示宿主内，避免RK的EGLFS显示方式另建启动窗口。
class StartupSplash : public QWidget
{
public:
    explicit StartupSplash(QWidget *parent);
    bool start(QString *error = nullptr);
    void finish();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLabel *animation_;
    QMovie *movie_;
    QSize animationSize_;
    void updateGeometryForHost();
};

#endif
