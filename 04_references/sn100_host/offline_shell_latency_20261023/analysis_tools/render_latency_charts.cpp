// Temporary evidence renderer. Compile with the existing Windows Qt 5
// Charts/Widgets/Svg SDK; do not add this helper to any product build.
// Usage: render_latency_charts.exe chart_data.json output_stem
// It never shows a window and never reads runtime databases or credentials.
#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGraphicsLayout>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPainter>
#include <QPen>
#include <QSvgGenerator>
#include <QTextStream>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QLegend>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

QT_CHARTS_USE_NAMESPACE

namespace
{
const int kWidth = 1800;
const int kHeight = 1120;
const double kCandidateMs = 500.0;

QFont figureFont(int nominalPoints, int weight = QFont::Normal)
{
    QFont font(QStringLiteral("Microsoft YaHei"));
    // Pixel fonts keep metrics identical across the offscreen screen, raster
    // image and SVG generator; point fonts depend on each paint device's DPI.
    font.setPixelSize(qRound(nominalPoints * 1.5));
    font.setWeight(weight);
    return font;
}

struct Figure
{
    QGraphicsScene scene;
    QChart *chart = nullptr;
    Figure()
    {
        scene.setSceneRect(0, 0, kWidth, kHeight);
        scene.setBackgroundBrush(Qt::white);
        chart = new QChart;
        chart->setTheme(QChart::ChartThemeLight);
        chart->setAnimationOptions(QChart::NoAnimation);
        chart->setBackgroundRoundness(0);
        chart->setBackgroundBrush(Qt::white);
        chart->setPlotAreaBackgroundVisible(true);
        chart->setPlotAreaBackgroundBrush(QColor("#FFFFFF"));
        chart->setMargins(QMargins(25, 15, 30, 15));
        chart->setGeometry(QRectF(20, 150, 1760, 790));
        chart->legend()->setFont(figureFont(13));
        chart->legend()->setAlignment(Qt::AlignBottom);
        scene.addItem(chart);
    }
};

double number(const QJsonValue &value, const QString &field)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0)
    {
        throw std::runtime_error((QStringLiteral("Expected finite nonnegative number: ") + field).toStdString());
    }
    return value.toDouble();
}

QGraphicsTextItem *text(Figure &figure, const QString &value, qreal x, qreal y,
                        qreal width, int points = 14, bool bold = false,
                        const QColor &color = QColor("#273245"))
{
    auto *item = figure.scene.addText(value, figureFont(points, bold ? QFont::Bold : QFont::Normal));
    item->setDefaultTextColor(color);
    item->setTextWidth(width);
    item->setPos(x, y);
    item->setZValue(20);
    return item;
}

void header(Figure &figure, const QString &title, const QString &subtitle)
{
    text(figure, title, 52, 25, 1690, 25, true);
    text(figure, subtitle, 56, 85, 1680, 14, false, QColor("#56647A"));
}

void axisStyle(QValueAxis *axis, const QString &title, int ticks = 7)
{
    axis->setTitleText(title);
    axis->setTitleFont(figureFont(14));
    axis->setLabelsFont(figureFont(13));
    axis->setTickCount(ticks);
    axis->setGridLineColor(QColor("#DEE4ED"));
    axis->setLinePen(QPen(QColor("#7A879B"), 1));
}

QLineSeries *candidateLine(QChart *chart, double beginX, double endX)
{
    auto *line = new QLineSeries;
    line->setName(QStringLiteral("500 ms 候选观察线（非放行门槛）"));
    line->append(beginX, kCandidateMs);
    line->append(endX, kCandidateMs);
    line->setPen(QPen(QColor("#B84A49"), 2, Qt::DashLine));
    chart->addSeries(line);
    return line;
}

void layOut(Figure &figure)
{
    // A scene render activates the chart's private graphics layouts without
    // showing a native window. Reuse the exact geometry for PNG and SVG.
    figure.chart->layout()->activate();
    QImage scratch(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
    scratch.fill(Qt::white);
    QPainter painter(&scratch);
    figure.scene.render(&painter, QRectF(0, 0, kWidth, kHeight), figure.scene.sceneRect());
    painter.end();
    QApplication::processEvents();
    for (QGraphicsItem *item : figure.scene.items()) item->setCacheMode(QGraphicsItem::NoCache);
}

std::unique_ptr<Figure> summaryFigure(const QJsonArray &groups)
{
    if (groups.isEmpty())
    {
        throw std::runtime_error("groups must not be empty");
    }
    auto figure = std::unique_ptr<Figure>(new Figure);
    header(*figure, QStringLiteral("心跳峰值分离复测：三个独立进程逐次列出"),
        QStringLiteral("WSL · Qt 5 Release · XCB | 10 ms GUI 心跳实际处理间隔 | 诊断统计图，非原始界面截图"));
    auto *series = new QBarSeries;
    const QColor colors[] = {QColor("#386FB4"), QColor("#56A397"), QColor("#D99342")};
    QBarSet *sets[3];
    for (int run = 0; run < 3; ++run)
    {
        sets[run] = new QBarSet(QStringLiteral("独立进程 %1").arg(run + 1));
        sets[run]->setColor(colors[run]);
        sets[run]->setBorderColor(colors[run].darker(112));
        sets[run]->setLabelFont(figureFont(12));
        sets[run]->setLabelColor(QColor("#243247"));
        series->append(sets[run]);
    }
    QStringList categories;
    double largest = kCandidateMs;
    QStringList kindNotes;
    for (int groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
    {
        const QJsonObject group = groups[groupIndex].toObject();
        const QString label = group.value(QStringLiteral("label")).toString();
        const QJsonArray values = group.value(QStringLiteral("values_ms")).toArray();
        if (label.isEmpty() || values.size() != 3)
        {
            throw std::runtime_error("Every group requires label and exactly three independently measured values_ms; no missing values are synthesized");
        }
        categories << label;
        const QString kind = group.value(QStringLiteral("kind")).toString();
        if (!kind.isEmpty())
        {
            kindNotes << QStringLiteral("%1：%2").arg(label, kind);
        }
        for (int run = 0; run < 3; ++run)
        {
            const double value = number(values[run], QStringLiteral("groups[%1].values_ms[%2]").arg(groupIndex).arg(run));
            *sets[run] << value;
            largest = std::max(largest, value);
        }
    }
    series->setBarWidth(0.78);
    series->setLabelsVisible(true);
    series->setLabelsFormat(QStringLiteral("@value"));
    series->setLabelsPrecision(5);
    series->setLabelsPosition(QAbstractBarSeries::LabelsOutsideEnd);
    figure->chart->addSeries(series);
    auto *x = new QBarCategoryAxis;
    x->append(categories);
    x->setLabelsFont(figureFont(groups.size() > 6 ? 11 : 13));
    x->setTitleText(QStringLiteral("受测负载 / 阶段（每柱为该独立进程的最大心跳间隔）"));
    x->setTitleFont(figureFont(14));
    figure->chart->addAxis(x, Qt::AlignBottom);
    auto *y = new QValueAxis;
    axisStyle(y, QStringLiteral("最大心跳间隔 / ms"));
    y->setLabelFormat(QStringLiteral("%.0f"));
    y->setTickCount(6);
    y->setRange(0, std::ceil(largest * 1.15 / 500.0) * 500.0);
    figure->chart->addAxis(y, Qt::AlignLeft);
    series->attachAxis(x);
    series->attachAxis(y);
    auto *limit = candidateLine(figure->chart, -0.5, groups.size() - 0.5);
    auto *lineX = new QValueAxis;
    lineX->setRange(-0.5, groups.size() - 0.5);
    lineX->setVisible(false);
    figure->chart->addAxis(lineX, Qt::AlignBottom);
    limit->attachAxis(lineX);
    limit->attachAxis(y);
    text(*figure, QStringLiteral("单位：ms；图上数值四舍五入，原值见 JSON。三个进程分别展示。500 ms 是候选观察值，不能据此宣布 RK、像素反馈或完整系统验收通过。"),
        57, 947, 1680, 13);
    if (!kindNotes.isEmpty())
    {
        text(*figure, kindNotes.join(QStringLiteral("；")), 57, 1000, 1680, 12, false, QColor("#637087"));
    }
    layOut(*figure);
    return figure;
}

std::unique_ptr<Figure> timelineFigure(const QJsonObject &timeline)
{
    const QJsonArray beats = timeline.value(QStringLiteral("heartbeats")).toArray();
    const QJsonArray phases = timeline.value(QStringLiteral("phases")).toArray();
    if (beats.isEmpty())
    {
        throw std::runtime_error("timeline.heartbeats must contain measured points");
    }
    auto figure = std::unique_ptr<Figure>(new Figure);
    QString title = timeline.value(QStringLiteral("title")).toString();
    if (title.isEmpty()) title = QStringLiteral("心跳与采证阶段的单调时间轴");
    header(*figure, title, QStringLiteral("WSL · Qt 5 Release · XCB | 真实逐次心跳点与阶段区间 | 诊断图，非原始界面截图"));
    auto *line = new QLineSeries;
    line->setName(QStringLiteral("实际回调时刻对应的心跳间隔（竖线）"));
    line->setPen(QPen(QColor("#386FB4"), 1.8));
    auto *points = new QScatterSeries;
    points->setName(QStringLiteral("心跳区间末端的离散采样点"));
    points->setMarkerSize(7);
    points->setColor(QColor("#386FB4"));
    points->setBorderColor(QColor("#386FB4"));
    double largest = kCandidateMs;
    double endMs = 0;
    double previous = -1;
    for (int index = 0; index < beats.size(); ++index)
    {
        const QJsonObject beat = beats[index].toObject();
        const double now = number(beat.value(QStringLiteral("now_ms")), QStringLiteral("heartbeats.now_ms"));
        const double gap = number(beat.value(QStringLiteral("gap_ms")), QStringLiteral("heartbeats.gap_ms"));
        if (now < previous) throw std::runtime_error("Heartbeat times must be monotonic, no reordering is performed");
        previous = now;
        points->append(now / 1000.0, gap);
        largest = std::max(largest, gap);
        endMs = std::max(endMs, now);
    }
    for (const QJsonValue &value : phases)
    {
        const QJsonObject phase = value.toObject();
        const double begin = number(phase.value(QStringLiteral("begin_ms")), QStringLiteral("phases.begin_ms"));
        const double end = number(phase.value(QStringLiteral("end_ms")), QStringLiteral("phases.end_ms"));
        if (end < begin) throw std::runtime_error("Phase end precedes begin");
        endMs = std::max(endMs, end);
    }
    figure->chart->addSeries(line);
    figure->chart->addSeries(points);
    // Keep an invisible mapper series with two endpoints. The visible data are
    // discrete points and individual stems, never a long diagonal interpolation
    // across an event-loop stall where no heartbeat was delivered.
    line->append(0, 0);
    line->append(std::max(0.1, endMs / 1000.0 * 1.02), 0);
    line->setVisible(false);
    auto *x = new QValueAxis;
    axisStyle(x, QStringLiteral("测试单调时钟 / s"), 9);
    x->setLabelFormat(QStringLiteral("%.1f"));
    x->setRange(0, std::max(0.1, endMs / 1000.0 * 1.02));
    figure->chart->addAxis(x, Qt::AlignBottom);
    auto *y = new QValueAxis;
    axisStyle(y, QStringLiteral("相邻心跳实际间隔 / ms"));
    y->setLabelFormat(QStringLiteral("%.0f"));
    y->setTickCount(6);
    y->setRange(0, std::ceil(largest * 1.15 / 500.0) * 500.0);
    figure->chart->addAxis(y, Qt::AlignLeft);
    line->attachAxis(x);
    line->attachAxis(y);
    points->attachAxis(x);
    points->attachAxis(y);
    auto *limit = candidateLine(figure->chart, 0, x->max());
    limit->attachAxis(x);
    limit->attachAxis(y);
    // Qt 5's offscreen raster path clips the timeline's internally cached axis
    // label prefix after overlay children are added. Keep the axis geometry,
    // but paint the same tick values as uncached top-level scene text below.
    y->setLabelsColor(QColor(0, 0, 0, 0));
    layOut(*figure);
    const QRectF plot = figure->chart->plotArea();
    for (int index = 0; index < y->tickCount(); ++index)
    {
        const qreal value = y->min() + (y->max() - y->min()) * index / (y->tickCount() - 1);
        auto *label = figure->scene.addSimpleText(QString::number(value, 'f', 0), figureFont(13));
        label->setBrush(QColor("#404044"));
        const QPointF point = figure->chart->mapToScene(QPointF(plot.left(), plot.bottom() - plot.height() * index / (y->tickCount() - 1)));
        label->setPos(point.x() - label->boundingRect().width() - 6, point.y() - label->boundingRect().height() / 2);
        label->setZValue(30);
    }
    for (const QJsonValue &value : beats)
    {
        const QJsonObject beat = value.toObject();
        const qreal now = beat.value(QStringLiteral("now_ms")).toDouble() / 1000.0;
        const qreal gap = beat.value(QStringLiteral("gap_ms")).toDouble();
        const QPointF bottom = figure->chart->mapToPosition(QPointF(now, 0), points);
        const QPointF top = figure->chart->mapToPosition(QPointF(now, gap), points);
        auto *stem = new QGraphicsLineItem(QLineF(bottom, top), figure->chart);
        stem->setPen(QPen(QColor("#386FB4"), 1.8));
        stem->setZValue(5);
    }
    const QColor phaseColors[] = {QColor("#EBB953"), QColor("#6B9ED4"), QColor("#73B6A8"), QColor("#B58AC9")};
    QStringList phaseNotes;
    for (int index = 0; index < phases.size(); ++index)
    {
        const QJsonObject phase = phases[index].toObject();
        const double begin = phase.value(QStringLiteral("begin_ms")).toDouble();
        const double end = phase.value(QStringLiteral("end_ms")).toDouble();
        const QString name = phase.value(QStringLiteral("name")).toString();
        const qreal left = figure->chart->mapToPosition(QPointF(begin / 1000.0, 0), line).x();
        const qreal right = figure->chart->mapToPosition(QPointF(end / 1000.0, 0), line).x();
        QColor tint = phaseColors[index % 4];
        tint.setAlpha(36);
        auto *area = new QGraphicsRectItem(QRectF(left, plot.top(), std::max<qreal>(1, right - left), plot.height()), figure->chart);
        area->setBrush(tint);
        area->setPen(Qt::NoPen);
        area->setZValue(6);
        const QColor edge = phaseColors[index % 4].darker(135);
        for (qreal boundary : {left, right})
        {
            auto *marker = new QGraphicsLineItem(QLineF(boundary, plot.top(), boundary, plot.bottom()), figure->chart);
            marker->setPen(QPen(edge, 1, Qt::DashLine));
            marker->setZValue(7);
        }
        auto *label = new QGraphicsSimpleTextItem(QString::number(index + 1), figure->chart);
        label->setFont(figureFont(11, QFont::Bold));
        label->setBrush(edge);
        label->setPos(std::min(left + 2, plot.right() - 18), plot.top() + 4 + (index % 3) * 22);
        label->setZValue(8);
        phaseNotes << QStringLiteral("%1 %2 [%3–%4 ms]").arg(index + 1).arg(name).arg(begin, 0, 'f', 1).arg(end, 0, 'f', 1);
    }
    text(*figure, QStringLiteral("每点是 [前次回调，本次回调] 区间的末端，竖线只表示该次间隔；点间没有连续采样。浅色区与虚线为阶段起止，归因须检查该区间与阶段的交集。"),
        57, 947, 1680, 13);
    text(*figure, phaseNotes.join(QStringLiteral("；")), 57, 1000, 1680, 11, false, QColor("#637087"));
    return figure;
}

void save(Figure &figure, const QString &stem)
{
    QImage image(kWidth, kHeight, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    image.setDotsPerMeterX(5669);
    image.setDotsPerMeterY(5669);
    QPainter bitmap(&image);
    bitmap.setRenderHint(QPainter::Antialiasing);
    bitmap.setRenderHint(QPainter::TextAntialiasing);
    figure.scene.render(&bitmap, QRectF(0, 0, kWidth, kHeight), figure.scene.sceneRect());
    bitmap.end();
    if (!image.save(stem + QStringLiteral(".png"))) throw std::runtime_error("PNG output failed");
    QSvgGenerator generator;
    generator.setFileName(stem + QStringLiteral(".svg"));
    generator.setSize(QSize(kWidth, kHeight));
    generator.setViewBox(QRect(0, 0, kWidth, kHeight));
    generator.setResolution(144);
    generator.setTitle(QStringLiteral("O03-2 WSL 心跳诊断图"));
    generator.setDescription(QStringLiteral("由实测 JSON 使用 Qt Charts 生成；文字与图形为 SVG 元素。诊断图不是原始界面截图；500 ms 为候选观察线，非放行门槛。"));
    QPainter vector;
    if (!vector.begin(&generator)) throw std::runtime_error("SVG output failed");
    vector.setRenderHint(QPainter::Antialiasing);
    vector.setRenderHint(QPainter::TextAntialiasing);
    figure.scene.render(&vector, QRectF(0, 0, kWidth, kHeight), figure.scene.sceneRect());
    vector.end();
}
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    // Unlike the Windows native platform plugin, this Qt 5 offscreen build
    // does not discover Windows system fonts automatically. Point its basic
    // font database at the real installed fonts and load YaHei explicitly.
    qputenv("QT_QPA_FONTDIR", QByteArray("C:/Windows/Fonts"));
    QApplication application(argc, argv);
    const int regularFont = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyh.ttc"));
    const int boldFont = QFontDatabase::addApplicationFont(QStringLiteral("C:/Windows/Fonts/msyhbd.ttc"));
    QTextStream(stdout) << "Offscreen font load: regular=" << regularFont << " bold=" << boldFont
        << " availableFamilies=" << QFontDatabase().families().size() << '\n';
    if (regularFont < 0 || QFontDatabase::applicationFontFamilies(regularFont).isEmpty())
    {
        QTextStream(stderr) << "Microsoft YaHei could not be loaded; refusing textless evidence charts.\n";
        return 2;
    }
    application.setFont(figureFont(13));
    try
    {
        if (argc != 3) throw std::runtime_error("Usage: render_latency_charts chart_data.json output_stem");
        QFile file(QString::fromLocal8Bit(argv[1]));
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open chart_data.json");
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) throw std::runtime_error("Invalid chart JSON object");
        const QJsonObject data = document.object();
        const QString stem = QString::fromLocal8Bit(argv[2]);
        auto summary = summaryFigure(data.value(QStringLiteral("groups")).toArray());
        auto timeline = timelineFigure(data.value(QStringLiteral("timeline")).toObject());
        save(*summary, stem + QStringLiteral("_summary"));
        save(*timeline, stem + QStringLiteral("_timeline"));
        QTextStream(stdout) << "Saved summary/timeline PNG and editable SVG from measured JSON.\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        QTextStream(stderr) << "Chart rendering refused: " << error.what() << '\n';
        return 2;
    }
}
