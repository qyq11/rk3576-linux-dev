#ifndef SN100_QC_HISTORY_QUALITYCHART_H
#define SN100_QC_HISTORY_QUALITYCHART_H
#include <QWidget>
#include <QVector>
#include <limits>

struct QcPlotPoint
{
    bool valid = false;
    bool previous = false;
    double value = std::numeric_limits<double>::quiet_NaN();
};

// 保留原QualityChart promoted类名、前绿/当前蓝的线圆点与序号窗口。
// QWidget只绘制已解析值，不读SQL、单位字典，不计算临床参考线/失控状态。
class QualityChart : public QWidget
{
    Q_OBJECT
public:
    explicit QualityChart(QWidget *parent = nullptr);
    void setPoints(const QVector<QcPlotPoint> &points);
    void clearChartView();
    void traceData(int plotIndex);
    QVector<QcPlotPoint> points() const { return points_; }
    int tracedIndex() const { return traced_; }
    double ySpan() const { return maxY_ - minY_; }
    bool drawable() const { return drawable_; }
    QVector<double> yTickValues() const;
    bool mapPoint(int index, QPointF *mapped) const;
signals:
    void activated();
public slots:
    void resetRangeSlot();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;
private:
    QVector<QcPlotPoint> points_;
    int traced_ = -1;
    bool drawable_ = false;
    bool dragging_ = false;
    QPoint dragStart_;
    double dragMinX_ = 0, dragMaxX_ = 1, dragMinY_ = 0, dragMaxY_ = 1;
    double minX_ = -1, maxX_ = 30, minY_ = 0, maxY_ = 1;
    double resetMinX_ = -1, resetMaxX_ = 30, resetMinY_ = 0, resetMaxY_ = 1;
    QRectF plotRect() const;
    QPointF pixel(int index, double value) const;
};
#endif
