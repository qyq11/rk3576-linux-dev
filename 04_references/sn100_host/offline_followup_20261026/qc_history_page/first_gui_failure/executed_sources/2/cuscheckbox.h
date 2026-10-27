#ifndef CUSCHECKBOX_H
#define CUSCHECKBOX_H

#include <QObject>
#include <QWidget>

class CusCheckBox : public QWidget
{
    Q_OBJECT
public:
    explicit CusCheckBox(QWidget *parent = nullptr);
    void setCheckState(Qt::CheckState checkState);
    void setText(QString txt);
    Qt::CheckState getCheckState();
    bool isChecked();
    void setAutoEx(bool);
    QString getText();

signals:
    void signalClicked();
    void signalClicked(bool);
    //void signalClicked(CusCheckBox *);

protected:
    void mousePressEvent(QMouseEvent *event);
    void paintEvent(QPaintEvent *);
    void drawCheckImage(QPainter *painter);
    void drawText(QPainter *painter);


private:
    Qt::CheckState checkState;
    QString boxText;
    QColor imgColor;
    bool isAuto;

};

#endif // CUSCHECKBOX_H
