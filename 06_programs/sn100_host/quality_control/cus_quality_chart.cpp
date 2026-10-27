#include "cus_quality_chart.h"
#include "ui_cus_quality_chart.h"
#include "qualitychart.h"
#include <QMouseEvent>
#include <QLabel>
#include <cmath>

using namespace Sn100QcHistory;

CusQualityChart::CusQualityChart(QWidget *parent) : QWidget(parent), ui(new Ui::CusQualityChart)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("CusQualityChart"));
    ui->wgtChartBg->setRadius(10);
    ui->btnAddCode->hide(); // 原“查看详细”本来隐藏；不发明新详细/导入窗口。
    ui->lblState->setText(QStringLiteral("—"));
    ui->lblState->setToolTip(tr("临床规则尚未启用；没有正常、警告或失控结论。"));
    for (QLabel *label : {ui->lblMean, ui->lblSD, ui->lblCV, ui->lblCount,
                         ui->lblResultVal, ui->lblDate, ui->lblTime}) label->setText(QStringLiteral("—"));
    connect(ui->wgtChart, &QualityChart::activated, this, &CusQualityChart::clicked);
    setChecked(false);
}

CusQualityChart::~CusQualityChart() { delete ui; }

void CusQualityChart::setData(const QString &controlName, const QString &batchName,
                             const QcHistoryResult &result, const Selection &selection, const Summary &summary)
{
    selection_ = selection;
    summary_ = summary;
    ui->lblControlName->setText(controlName);
    ui->lblCurrentBatch->setText(batchName);
    ui->lblResultName->setText(result.name);
    setProperty("qcResultType", int(result.type));
    setProperty("qcInvalidResultCount", selection.invalidResultCount);
    int validCount = 0;
    converted_ = summary.unknownUnits == 0 && !summary.unit.isEmpty() &&
        summary.errorCode != QStringLiteral("mixed_historical_units");
    QVector<QcPlotPoint> points;
    for (int index = 0; index < selection.plotOrder.size(); ++index)
    {
        const HistoryPoint &point = selection.plotOrder.at(index);
        QcPlotPoint plot;
        plot.previous = index < selection.previous.size();
        plot.valid = point.result.valid;
        if (plot.valid)
        {
            ++validCount;
            plot.value = converted_ ? point.displayValue : point.result.number;
            plot.valid = std::isfinite(plot.value);
        }
        points.append(plot);
    }
    ui->wgtChart->setPoints(points);
    ui->lblCount->setText(QString::number(validCount));
    QString explanation = converted_
        ? tr("同单位历史点；CV是比值，不是百分比。")
        : tr("原存储数值：历史点单位来源未知、不同或换算不可用；全卡未换算。");
    if (!summary.ok) explanation += tr(" Mean/SD/CV不可用，原因：%1。").arg(summary.errorCode);
    const QString counts = tr("有效数值%1；无效结果%2；日期/身份等排除%3条。")
        .arg(validCount).arg(selection.invalidResultCount).arg(selection.excluded.size());
    setToolTip(explanation + counts);
    ui->lblCount->setToolTip(counts);
    ui->lblResultName->setToolTip(explanation);
    ui->lblMean->setText(summary.ok ? QString::number(summary.statistics.mean, 'f', 1) : QStringLiteral("—"));
    ui->lblSD->setText(summary.ok && summary.statistics.sampleEstimateAvailable
        ? QString::number(summary.statistics.sampleStddev, 'f', 1) : QStringLiteral("—"));
    ui->lblCV->setText(summary.ok && summary.statistics.cvAvailable
        ? QString::number(summary.statistics.legacyCvRatio, 'f', 1) : QStringLiteral("—"));
    for (QLabel *label : {ui->lblMean,ui->lblSD,ui->lblCV}) label->setToolTip(explanation);
    // 原规则参数仍在manager维护。没有“规则单位=历史点单位”的证据，
    // 不把target/上下限叠加到原存储数值图，更不由未判定推断“正常”。
    ui->lblState->setText(QStringLiteral("—"));
    ui->lblState->setToolTip(tr("临床规则未启用；配置参考线的历史单位兼容性尚未核实，未叠加。"));
    counter_ = selection.plotOrder.size() - 1;
    traceData();
}

void CusQualityChart::setChecked(bool checked)
{
    checked_ = checked;
    ui->wgtChartBg->setBackgroundColor(QColor(checked ? "#E8F4FF" : "#FFFFFF"));
}
void CusQualityChart::clicked()
{
    if (checked_) return;
    setChecked(true); emit signalCheckdChanged();
}
void CusQualityChart::mousePressEvent(QMouseEvent *event)
{
    clicked(); QWidget::mousePressEvent(event);
}
void CusQualityChart::on_btnLeft_clicked()
{
    counter_ = qMin(selection_.plotOrder.size() - 1, counter_ + 1); traceData();
}
void CusQualityChart::on_btnRight_clicked()
{
    counter_ = qMax(0, counter_ - 1); traceData();
}
void CusQualityChart::traceData()
{
    // 精确保留原反向counter合同；不是“自动选最新点”的修正。
    const HistoryPoint *point = legacyTraceAtCounter(selection_, counter_);
    if (!point)
    {
        ui->lblResultVal->setText(QStringLiteral("—"));
        ui->lblDate->setText(QStringLiteral("—")); ui->lblTime->setText(QStringLiteral("—"));
        ui->btnLeft->setEnabled(false); ui->btnRight->setEnabled(false); ui->wgtChart->traceData(-1);
        return;
    }
    QString text;
    if (!point->result.valid) text = tr("无效");
    else if (converted_) text = point->displayText;
    else text = point->result.storedNumber + tr(" 单位未知");
    ui->lblResultVal->setToolTip(text + (point->result.valid ? QString() : QStringLiteral("：") + point->result.errorCode));
    ui->lblResultVal->setText(ui->lblResultVal->fontMetrics().elidedText(text, Qt::ElideMiddle, ui->lblResultVal->width()));
    ui->lblDate->setText(point->row.date); ui->lblTime->setText(point->row.time);
    ui->lblDate->setToolTip(tr("历史记录ID：%1；批次：%2").arg(point->row.id).arg(point->row.batchName));
    ui->btnLeft->setEnabled(counter_ < selection_.plotOrder.size() - 1);
    ui->btnRight->setEnabled(counter_ > 0);
    ui->wgtChart->traceData(selection_.plotOrder.size() - counter_ - 1);
}
