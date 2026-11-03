#include "cusreagenthole.h"
#include <QDate>
#include <QDateTime>
#include <QPainter>
#include <QPainterPath>

cusReagentHole::cusReagentHole(QWidget* parent)
    : QWidget(parent)
{
}
void cusReagentHole::setInformation(const ReagentHole& h, const ReagentRecord& r, const ReagentBottle& b)
{
    setProperty("originalNView",false); // 页面随后按当前独立运行来源重新绘制，不把库中used称为实测。
    setProperty("originalLevelUnavailable",false);
    setProperty("originalNTestsAvailable",false);
    hole_ = h;
    reagent_ = r;
    bottle_ = b;
    update();
}
void cusReagentHole::setSelected(bool selected)
{
    selected_ = selected;
    update();
}
void cusReagentHole::setSelectedColour(QColor colour)
{
    if (!colour.isValid() || colour==selectedColour_) return;
    selectedColour_=colour;
    update();
}
void cusReagentHole::setDisplayMode(int mode)
{
    displayMode_ = mode;
    update();
}
void cusReagentHole::mousePressEvent(QMouseEvent*)
{
    if (isEnabled())
        emit clicked(objectName());
}
void cusReagentHole::mouseDoubleClickEvent(QMouseEvent*)
{
    if (isEnabled())
        emit doubleClicked(objectName());
}
void cusReagentHole::paintEvent(QPaintEvent*)
{
    // Original cusReagentHole drawBg/drawTopBg/drawBottom/text geometry and
    // colors. Only the model is replaced; empty slots have no fake volume.
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const int side = qMin(width(), height()) - 12;
    const double x = (width() - side) / 2.0, y = (height() - side) / 2.0;
    QRectF all(x, y, side, side), border(x - 3, y - 3, side + 6, side + 6);
    QPen pen(selected_ ? selectedColour_ : QColor("#C8C8C8"));
    pen.setWidth(6);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(QColor("#F6F6F6"));
    p.drawEllipse(border);
    p.setPen(Qt::NoPen);
    p.drawEllipse(all);
    const bool hasReagent = hole_.reagentId > 0;
    const bool levelView=property("originalNView").toBool();
    const bool unknownLevel=property("originalLevelUnavailable").toBool();
    const double shownAmount=levelView ? property("originalNAmount").toDouble() : hole_.remainingUl;
    const double shownMaximum=levelView ? property("originalNMaximum").toDouble() : bottle_.capacityUl;
    const bool missing = hasReagent && !unknownLevel && (levelView ? property("originalNState").toString()==QStringLiteral("r_useout") : hole_.remainingUl<=reagent_.outUl);
    const bool warning = hasReagent && !unknownLevel && (levelView ? property("originalNState").toString()==QStringLiteral("r_warning") : hole_.remainingUl<=reagent_.warningUl);
    QPainterPath circle;
    circle.addEllipse(all);
    if (hasReagent)
    {
        QPainterPath top;
        top.addRect(QRectF(x, y, side, side * .3));
        p.setBrush(QColor(missing ? "#FFA6A6" : warning ? "#FFE1A6" : "#D8D8D8"));
        p.drawPath(circle.intersected(top));
        if (!unknownLevel && !missing && shownMaximum > 0)
        {
            const double fraction = qBound(0.0, shownAmount / shownMaximum, 1.0);
            QPainterPath water;
            water.addRect(QRectF(x, y + side * .3 + (1 - fraction) * side * .7, side, side * .7));
            p.setBrush(QColor("#B7D7FF"));
            p.drawPath(circle.intersected(water));
        }
    }
    p.setPen(QColor("#4A4A4A"));
    QFont font("Noto Sans CJK SC");
    font.setPixelSize(objectName().startsWith("SD_") ? 16 : 14);
    p.setFont(font);
    p.drawText(QRectF(x + side * .21, y + side * .06, side * .58, side * .24), Qt::AlignCenter, objectName());
    if (!hasReagent)
        return;
    auto fittedText = [&](QRectF rect, QString text)
    {
        QFont f = font;
        while (QFontMetrics(f).horizontalAdvance(text) > rect.width() && f.pixelSize() > 7)
            f.setPixelSize(f.pixelSize() - 1);
        p.setFont(f);
        p.drawText(rect, Qt::AlignCenter, text);
    };
    fittedText(QRectF(x + side * .05, y + side * .3, side * .9, side * .28), reagent_.name);
    QString amount = QString::number(shownAmount / 1000.0, 'f', 1) + "ml";
    if (unknownLevel) amount=QStringLiteral("-ml");
    if (displayMode_ == 1)
    {
        const int tests=property("originalNTests").toInt();
        amount=!property("originalNTestsAvailable").toBool() || !tests ? QStringLiteral("- tests") :
            tests>999 ? QStringLiteral(">999 tests") : QString::number(tests)+QStringLiteral(" tests");
    }
    if (displayMode_ == 2)
    {
        const QDateTime loaded = QDateTime::fromString(hole_.changedAt, Qt::ISODate);
        const qint64 hours
            = loaded.isValid() ? qMax<qint64>(0, loaded.secsTo(QDateTime::currentDateTime()) / 3600) : 0;
        amount = hours > 999 ? QStringLiteral(">999h") : QString::number(hours) + "h";
    }
    fittedText(QRectF(x + side * .13, y + side * .58, side * .74, side * .24), amount);
    if (reagent_.checkExpiry && QDate::fromString(hole_.expiry, "yyyy-MM-dd") < QDate::currentDate())
    {
        p.setPen(QPen(Qt::red, 1));
        p.setBrush(Qt::red);
        const double startY = 6 + side * .3, endY = startY + side * .3, w = side - 6;
        QPainterPath cross;
        QPolygonF first, second;
        first << QPointF(x + 2, startY) << QPointF(x + 12, startY) << QPointF(w + 10, endY)
              << QPointF(w, endY);
        second << QPointF(w, startY) << QPointF(w + 10, startY) << QPointF(x + 12, endY)
               << QPointF(x + 2, endY);
        cross.addPolygon(first);
        cross.addPolygon(second);
        p.drawPath(cross);
    }
}
