#ifndef SN100_HISTORY_CUS_QUALITY_CHART_H
#define SN100_HISTORY_CUS_QUALITY_CHART_H
#include "qc_history_reply.h"
#include <QWidget>

namespace Ui { class CusQualityChart; }

class CusQualityChart : public QWidget
{
    Q_OBJECT
public:
    explicit CusQualityChart(QWidget *parent = nullptr);
    ~CusQualityChart() override;
    void setData(const QString &controlName, const QString &batchName, const QcHistoryResult &result,
                 const Sn100QcHistory::Selection &selection, const Sn100QcHistory::Summary &summary);
    void setChecked(bool checked);
    bool getChecked() const { return checked_; }
    bool usesHistoricalDisplayUnits() const { return converted_; }
    int traceCounter() const { return counter_; }
signals:
    void signalCheckdChanged();
public slots:
    void clicked();
private slots:
    void on_btnLeft_clicked();
    void on_btnRight_clicked();
protected:
    void mousePressEvent(QMouseEvent *) override;
private:
    Ui::CusQualityChart *ui;
    Sn100QcHistory::Selection selection_;
    Sn100QcHistory::Summary summary_;
    bool checked_ = false;
    bool converted_ = false;
    int counter_ = -1;
    void traceData();
};
#endif
