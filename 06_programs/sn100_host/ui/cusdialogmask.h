#ifndef SN100_CUSDIALOGMASK_H
#define SN100_CUSDIALOGMASK_H

#include <QParallelAnimationGroup>
#include <QPointer>
#include <QWidget>

class QEvent;
class QFrame;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace Ui
{
class CusDialogMask;
}

// Extracted mask and animation for the original SN100 custom dialogs.
// All positioning is local to the current host, rather than the desktop.
class CusDialogMask : public QWidget
{
    Q_OBJECT

public:
    explicit CusDialogMask(QWidget *parent = nullptr);
    ~CusDialogMask() override;

    void setupForm(int radius = 11, bool noOffset = false);
    void showAnimation();
    void closeAnimation();
    bool isReadyForInput() const;

signals:
    void closeFinished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    Ui::CusDialogMask *ui;
    QPointer<QWidget> dialog_;
    QPointer<QWidget> host_;
    QFrame *displayFrame_ = nullptr;
    QFrame *viewWidget_ = nullptr;
    QPoint viewPos_;
    QGraphicsOpacityEffect *backgroundOpacity_ = nullptr;
    QGraphicsOpacityEffect *pictureOpacity_ = nullptr;
    QPropertyAnimation *animateBackground_ = nullptr;
    QPropertyAnimation *animatePicture_ = nullptr;
    QPropertyAnimation *animateMove_ = nullptr;
    QParallelAnimationGroup animationGroup_;
    bool closing_ = false;
    bool configured_ = false;

    void alignToHost();
    void startAnimation(bool closing);
};

#endif
