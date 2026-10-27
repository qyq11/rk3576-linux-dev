#ifndef CUSCALENDAR_H
#define CUSCALENDAR_H

#include <QWidget>
#include <QDate>

namespace Ui {
class CusCalendar;
}

class CusCalendar : public QWidget
{
    Q_OBJECT
public:
    struct DateItem {
        int year;
        int month;
        int day;
        QRectF clickRect;

        DateItem()
        {
            year = -1;
            month = -1;
            day = -1;
            clickRect = QRectF(0,0,0,0);
        }
    };

    explicit CusCalendar(QWidget *parent = nullptr);
    ~CusCalendar() override;
    void updateCalendar(const QDate &selectDate);

signals:
    void signalSelectDateChanged(const QString&);
    void signalCloseDialog();

protected:
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void paintEvent(QPaintEvent *) override;

private slots:
    void on_btnLeft_clicked();
    void on_btnRight_clicked();
    void on_btnClose_clicked();

private:
    Ui::CusCalendar *ui;

    QDate displayDate;
    QDate selectDate;
    DateItem dateItem[6][7];
};

#endif // CUSCALENDAR_H
