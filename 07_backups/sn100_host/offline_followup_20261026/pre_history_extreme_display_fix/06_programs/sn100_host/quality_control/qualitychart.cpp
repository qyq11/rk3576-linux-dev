#include "qualitychart.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

QualityChart::QualityChart(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAutoFillBackground(false);
}

QRectF QualityChart::plotRect() const
{
    return QRectF(52, 6, qMax(1, width() - 60), qMax(1, height() - 18));
}

QPointF QualityChart::pixel(int index, double value) const
{
    const QRectF area = plotRect();
    return QPointF(area.left() + (index - minX_) / (maxX_ - minX_) * area.width(),
                   area.bottom() - (value - minY_) / (maxY_ - minY_) * area.height());
}

void QualityChart::clearChartView()
{
    points_.clear();
    traced_ = -1;
    drawable_ = false;
    minX_ = resetMinX_ = -1; maxX_ = resetMaxX_ = 30;
    minY_ = resetMinY_ = 0; maxY_ = resetMaxY_ = 1;
    update();
}

void QualityChart::setPoints(const QVector<QcPlotPoint> &points)
{
    clearChartView();
    if (points.size() > 10000)
    { setToolTip(tr("超过单卡离线绘图预算；未截断结果或生成假点。")); return; }
    points_ = points;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -minimum;
    for (const QcPlotPoint &point : points_)
        if (point.valid && std::isfinite(point.value))
        {
            minimum = std::min(minimum, point.value);
            maximum = std::max(maximum, point.value);
        }
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(maximum - minimum))
    {
        setToolTip(tr("没有可绘数值，或原值跨度超出绘图范围；未生成零点。"));
        return;
    }
    // 原代码常数序列会有零跨度；这里只补有限非零轴范围，不修改任何结果。
    double padding = (maximum - minimum) / 2;
    if (!(padding > 0)) padding = std::max(1.0, std::fabs(minimum) * 0.1);
    minY_ = minimum - padding; maxY_ = maximum + padding;
    if (!std::isfinite(minY_) || !std::isfinite(maxY_) || !std::isfinite(maxY_ - minY_))
    {
        minY_ = minimum; maxY_ = maximum;
    }
    if (!(maxY_ > minY_)) return;
    minX_ = points_.size() > 30 ? points_.size() - 30 : -1;
    maxX_ = points_.size() > 30 ? points_.size() : 30;
    resetMinX_ = minX_; resetMaxX_ = maxX_;
    resetMinY_ = minY_; resetMaxY_ = maxY_;
    drawable_ = true;
    setToolTip(tr("原序号顺序：前批次后接当前批次；可拖动、缩放，双击恢复。未启用临床判定。"));
    update();
}

void QualityChart::traceData(int index)
{
    traced_ = index >= 0 && index < points_.size() ? index : -1;
    update();
}

void QualityChart::resetRangeSlot()
{
    minX_ = resetMinX_; maxX_ = resetMaxX_;
    minY_ = resetMinY_; maxY_ = resetMaxY_;
    update();
}

void QualityChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF area = plotRect();
    if (!drawable_)
    {
        painter.setPen(QColor("#444444"));
        painter.drawText(area, Qt::AlignCenter, QStringLiteral("—"));
        return;
    }
    QFont labelFont = font(); labelFont.setPointSize(12); painter.setFont(labelFont);
    for (int tick = 0; tick <= 4; ++tick)
    {
        const double value = minY_ + (maxY_ - minY_) * tick / 4;
        const double y = area.bottom() - area.height() * tick / 4;
        painter.setPen(QPen(QColor(210, 210, 210), 1, Qt::DotLine));
        painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        painter.setPen(QColor("#444444"));
        painter.drawText(QRectF(0, y - 10, 47, 20), Qt::AlignRight | Qt::AlignVCenter, QString::number(value, 'g', 4));
    }
    painter.setPen(QColor("#444444"));
    painter.drawLine(area.topLeft(), area.bottomLeft());
    painter.save(); painter.setClipRect(area.adjusted(-3, -3, 3, 3));
    // 仅绘可见序号附近，保留无效点断口，不连接跨批或跨缺失值的线。
    const int first = qMax(0, int(std::floor(minX_)) - 1);
    const int end = qMin(points_.size(), int(std::ceil(maxX_)) + 2);
    for (bool previous : {true, false})
    {
        const QColor color(previous ? "#62D93D" : "#3593F8");
        painter.setPen(QPen(color, 3)); painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        bool connected = false;
        for (int index = first; index < end; ++index)
        {
            const QcPlotPoint &point = points_.at(index);
            if (!point.valid || point.previous != previous || !std::isfinite(point.value))
            { connected = false; continue; }
            const QPointF position = pixel(index, point.value);
            if (connected) path.lineTo(position); else path.moveTo(position);
            connected = true;
        }
        painter.drawPath(path);
        for (int index = first; index < end; ++index)
        {
            const QcPlotPoint &point = points_.at(index);
            if (point.valid && point.previous == previous && std::isfinite(point.value))
                painter.drawEllipse(pixel(index, point.value), 2.5, 2.5);
        }
    }
    if (traced_ >= 0 && traced_ < points_.size())
    {
        const QcPlotPoint &point = points_.at(traced_);
        if (point.valid && std::isfinite(point.value))
        {
            const QPointF position = pixel(traced_, point.value);
            painter.setPen(QPen(QColor("#555555"), 1, Qt::DashLine));
            painter.drawLine(QPointF(position.x(), area.top()), QPointF(position.x(), area.bottom()));
            painter.drawLine(QPointF(area.left(), position.y()), QPointF(area.right(), position.y()));
        }
    }
    painter.restore();
}

void QualityChart::mousePressEvent(QMouseEvent *event)
{
    emit activated();
    if (event->button() == Qt::LeftButton && drawable_)
    {
        dragging_ = true; dragStart_ = event->pos();
        dragMinX_ = minX_; dragMaxX_ = maxX_; dragMinY_ = minY_; dragMaxY_ = maxY_;
        event->accept(); return;
    }
    QWidget::mousePressEvent(event);
}
void QualityChart::mouseMoveEvent(QMouseEvent *event)
{
    if (dragging_)
    {
        const QPoint delta = event->pos() - dragStart_;
        const QRectF area = plotRect();
        const double dx = delta.x() / area.width() * (dragMaxX_ - dragMinX_);
        const double dy = delta.y() / area.height() * (dragMaxY_ - dragMinY_);
        const double lowY = dragMinY_ + dy, highY = dragMaxY_ + dy;
        if (!std::isfinite(lowY) || !std::isfinite(highY) || !(highY > lowY))
        { event->accept(); return; }
        const double span = dragMaxX_ - dragMinX_;
        minX_ = qBound(-100000.0, dragMinX_ - dx, 100000.0 - span); maxX_ = minX_ + span;
        minY_ = lowY; maxY_ = highY;
        update(); event->accept(); return;
    }
    QWidget::mouseMoveEvent(event);
}
void QualityChart::mouseReleaseEvent(QMouseEvent *event)
{
    dragging_ = false; QWidget::mouseReleaseEvent(event);
}
void QualityChart::mouseDoubleClickEvent(QMouseEvent *event)
{
    resetRangeSlot(); event->accept();
}
void QualityChart::wheelEvent(QWheelEvent *event)
{
    if (!drawable_ || !event->angleDelta().y()) { event->ignore(); return; }
    const double factor = std::pow(1.2, -event->angleDelta().y() / 120.0);
    const double xSpan = (maxX_ - minX_) * factor, ySpan = (maxY_ - minY_) * factor;
    const double xMid = minX_ / 2 + maxX_ / 2, yMid = minY_ / 2 + maxY_ / 2;
    if (xSpan < 1 || xSpan > 100000 || !std::isfinite(ySpan) || !(ySpan > 0))
    { event->ignore(); return; }
    const double low = yMid - ySpan / 2, high = yMid + ySpan / 2;
    if (!std::isfinite(low) || !std::isfinite(high) || !(high > low)) { event->ignore(); return; }
    minX_ = xMid - xSpan / 2; maxX_ = xMid + xSpan / 2; minY_ = low; maxY_ = high;
    update(); event->accept();
}
