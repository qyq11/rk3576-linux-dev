#include "calibrationchart.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

CalibrationChart::CalibrationChart(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(300, 220);
}
void CalibrationChart::setRawPoints(const QList<QPointF> &points, const QString &concentrationUnit,
                                   const QString &resultUnit)
{
    m_points = points;
    m_concentrationUnit = concentrationUnit;
    m_resultUnit = resultUnit;
    update();
}
void CalibrationChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);
    painter.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
    const QRectF plot(90, 28, width() - 114, height() - 89);
    painter.setPen(QColor(68, 68, 68));
    painter.drawLine(plot.bottomLeft(), plot.bottomRight());
    painter.drawLine(plot.bottomLeft(), plot.topLeft());
    painter.drawText(QRectF(65, height() - 30, width() - 80, 25), Qt::AlignCenter,
                     tr("浓度 (%1)").arg(m_concentrationUnit));
    painter.save();
    painter.translate(12, height() / 2.0);
    painter.rotate(-90);
    painter.drawText(QRectF(-height() / 2.0, -12, height(), 24), Qt::AlignCenter,
                     tr("测试平均值 (%1)").arg(m_resultUnit));
    painter.restore();
    painter.drawText(QRectF(65, 0, width() - 80, 24), Qt::AlignCenter, tr("原始点（未拟合）"));
    if (m_points.isEmpty())
    {
        painter.drawText(plot, Qt::AlignCenter, tr("无有效原始点"));
        return;
    }
    double minX = m_points.first().x(), maxX = minX;
    double minY = m_points.first().y(), maxY = minY;
    for (const QPointF &point : m_points)
    {
        minX = std::min(minX, point.x()); maxX = std::max(maxX, point.x());
        minY = std::min(minY, point.y()); maxY = std::max(maxY, point.y());
    }
    const double marginX = maxX == minX ? std::max(1.0, std::abs(minX) * 0.05) : (maxX - minX) * 0.08;
    const double marginY = maxY == minY ? std::max(1.0, std::abs(minY) * 0.05) : (maxY - minY) * 0.08;
    minX -= marginX; maxX += marginX; minY -= marginY; maxY += marginY;
    for (int tick = 0; tick <= 4; ++tick)
    {
        const double fraction = tick / 4.0;
        const double x = plot.left() + fraction * plot.width();
        const double y = plot.bottom() - fraction * plot.height();
        painter.setPen(QColor(228, 232, 235));
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(QColor(68, 68, 68));
        painter.drawText(QRectF(x - 40, plot.bottom() + 5, 80, 20), Qt::AlignCenter,
                         QString::number(minX + fraction * (maxX - minX), 'g', 5));
        painter.drawText(QRectF(26, y - 10, 58, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(minY + fraction * (maxY - minY), 'g', 5));
    }
    // Match the original raw-point black crosses. No connecting or fitting line:
    // joining points would imply a model that this local record does not have.
    painter.setPen(QPen(QColor(120, 120, 120), 2));
    for (const QPointF &point : m_points)
    {
        const QPointF pixel(plot.left() + (point.x() - minX) / (maxX - minX) * plot.width(),
                            plot.bottom() - (point.y() - minY) / (maxY - minY) * plot.height());
        painter.drawLine(pixel + QPointF(-5, -5), pixel + QPointF(5, 5));
        painter.drawLine(pixel + QPointF(-5, 5), pixel + QPointF(5, -5));
    }
}
