#include "cuscalendar.h"
#include "qglobal.h"
#include "ui_cuscalendar.h"

#include <QPainter>
#include <QMouseEvent>
#include <QGraphicsDropShadowEffect>

CusCalendar::CusCalendar(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CusCalendar)
{
    ui->setupUi(this);

    this->setFixedSize(450,376);
    auto effect = new QGraphicsDropShadowEffect;
    effect->setBlurRadius(11);
    effect->setColor(QColor(0, 0, 0, 50));
    effect->setOffset(0, 3);
    this->setGraphicsEffect(effect);
}

CusCalendar::~CusCalendar()
{
    delete ui;
}


void CusCalendar::updateCalendar(const QDate &selectDate)
{
    // Private extraction: the history contract allows years1..9999 only.
    // Invalid QDate otherwise keeps month0 forever in the original while loop.
    if (!selectDate.isValid() || selectDate.year() < 1 || selectDate.year() > 9999)
        return;
    if (this->selectDate != selectDate) {
        int row = 0;
        DateItem dateItem[6][7];
        QDate date(selectDate.year(), selectDate.month(), 1);
        while (date.month() == selectDate.month()) {
            int weekDay = date.dayOfWeek();
            dateItem[row][weekDay - 1].year = date.year();
            dateItem[row][weekDay - 1].month = date.month();
            dateItem[row][weekDay - 1].day = date.day();
            date = date.addDays(1);
            if (weekDay == 7 && date.month() == date.month()) {
                row++;
            }
        }

        //重新赋值
        for (int i = 0; i < 6; i++) {
            for (int j = 0; j < 7; j++) {
                this->dateItem[i][j] = dateItem[i][j];
            }
        }
        this->selectDate = selectDate;
        ui->lblMonth->setText(this->selectDate.toString("M月"));
        ui->lblYear->setText(this->selectDate.toString("yyyy"));
        this->update();
    }
}

void CusCalendar::focusOutEvent(QFocusEvent *event)
{
  Q_UNUSED(event);
}

void CusCalendar::mousePressEvent(QMouseEvent *event)
{
    QPointF globalpoint = event->pos();
    for (int row = 0; row < 6; row++) {
        for (int column = 0; column < 7; column++) {
            if (dateItem[row][column].day > 0) {
                QRectF rect = dateItem[row][column].clickRect;
                if(rect.contains(globalpoint))
                {
                    QDate selectDate;
                    selectDate.setDate(dateItem[row][column].year,dateItem[row][column].month,dateItem[row][column].day);
                    updateCalendar(selectDate);
                    emit signalSelectDateChanged(selectDate.toString("yyyy/MM/dd"));
                }
            }
        }
    }
}

void CusCalendar::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing);

    int offset_y = 113;
    int sw = 450;
    int sh = 376 - offset_y;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#FFFFFF"));
    painter.drawRoundedRect(this->rect(),30,30);
    painter.restore();

    qreal iw = sw / 7.0;
    qreal ih = sh / 7.0;

    //绘制头部中文数字,先设置图像叠加模式为源在上面
#ifdef Q_OS_LINUX
    QFont font("Noto Sans CJK SC");
#else
    QFont font("苹方 粗体");
#endif
    font.setPixelSize(14);
    painter.setFont(font);
    painter.save();
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen("#AAAAAA");
    QStringList listHead;
    listHead << "一" << "二" << "三" << "四" << "五" << "六" << "日";
    for (int i = 0; i < 7; i++) {
        painter.drawText(i * iw, offset_y, iw, ih, Qt::AlignCenter, listHead.at(i));
    }
    painter.restore();
    //绘制日期
    for (int row = 0; row < 6; row++) {
        for (int column = 0; column < 7; column++) {
            if (dateItem[row][column].day > 0) {
                QRectF rect = QRectF(column * iw, (row + 1) * ih + offset_y, iw, ih).adjusted(3, 3, -3, -3);
                dateItem[row][column].clickRect = rect;
                //如果是选中的日期则突出绘制背景
                if (selectDate == QDate(dateItem[row][column].year, dateItem[row][column].month, dateItem[row][column].day)) {
                    painter.save();
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(QColor("#000000"));
                    painter.setOpacity(0.1);
                    painter.drawRoundedRect(rect, 5, 5);
                    painter.restore();
                }
                painter.save();
                painter.setPen("#222222");
                painter.drawText(rect, Qt::AlignCenter, QString::number(dateItem[row][column].day));
                painter.restore();
            }
        }
    }
}

void CusCalendar::on_btnLeft_clicked()
{
    updateCalendar(this->selectDate.addMonths(-1));
}

void CusCalendar::on_btnRight_clicked()
{
    updateCalendar(this->selectDate.addMonths(1));
}

void CusCalendar::on_btnClose_clicked()
{
   emit signalCloseDialog();
}
