#ifndef CUSRACKLISTWIDGET_H
#define CUSRACKLISTWIDGET_H

#include <QListWidget>
#include <QScroller>

class CusRackListWidget : public QListWidget
{
    Q_OBJECT
public:
    CusRackListWidget(QWidget *parent = nullptr);

    void setScrollEnabled(bool enabled);
protected:
    virtual void wheelEvent(QWheelEvent* event) override;
    virtual void mouseMoveEvent(QMouseEvent* event) override;

private:
    bool scrollEnabled = true;
    QScroller *scrollerRack;
};

#endif // CUSRACKLISTWIDGET_H
