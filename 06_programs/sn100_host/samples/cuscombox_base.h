#ifndef CUSCOMBOX_BASE_H
#define CUSCOMBOX_BASE_H

#include "qtmaterialscrollbar.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QListWidget>

class CusCombox_Base : public QWidget
{
    Q_OBJECT
public:
    explicit CusCombox_Base(QWidget *parent = nullptr);
    ~CusCombox_Base();
    void setSourceData(QStringList txts);
    void setCurrentText(QString txt);
    void setCurrentIndex(int index);
    QString getCurrentText();
    int getCurrentIndex();
    void setParent(QWidget *parent);
    void drawSelectButton();

signals:
    void itemSelected(int index, QString text);
protected:
    void focusOutEvent(QFocusEvent *event);
    bool eventFilter(QObject *watched, QEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *);
    void drawArrow(QPainter *painter);
    void drawCurrentText(QPainter *painter);

private:
    int height_button;
    QStringList sourceData;
    QString currentData;
    QListWidget  *menu;
    QWidget  *frame;
    QtMaterialScrollBar *m_verticalScrollbar;
};

#endif // CUSCOMBOX_BASE_H
