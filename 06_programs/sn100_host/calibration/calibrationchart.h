#ifndef SN100_LOCAL_CALIBRATION_CHART_H
#define SN100_LOCAL_CALIBRATION_CHART_H
#include <QWidget>
#include <QPointF>

// The original .ui promotes this widget to CalibrationChart. This offline
// implementation shows raw points only; it never supplies a fitted function.
class CalibrationChart : public QWidget
{
    Q_OBJECT
public:
    explicit CalibrationChart(QWidget *parent = nullptr);
    void setRawPoints(const QList<QPointF> &points, const QString &concentrationUnit,
                      const QString &resultUnit);
    QList<QPointF> rawPoints() const { return m_points; }
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QList<QPointF> m_points;
    QString m_concentrationUnit;
    QString m_resultUnit;
};
#endif
