#include "sample_widgets.h"
#include "ui_cussamplerack.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
class LocalSampleHoleView : public QWidget
{
  public:
    LocalSampleHoleView(bool configured, QWidget *parent) : QWidget(parent), configured_(configured) {}

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.drawPixmap(0, 0, QPixmap(configured_ ? ":/samples/s_setted.png" : ":/samples/s_none.png"));
    }

  private:
    bool configured_;
};
SampleRackView::SampleRackView(const SampleRack &rack, int index, bool selected, QWidget *parent)
    : QWidget(parent), ui(new Ui::CusSampleRack), id_(rack.id)
{
    ui->setupUi(this);
    setFixedSize(1341, 116);
    ui->wgtRack->setRadius(60);
    ui->wgtRackInfo->setRadius(10);
    ui->wgtRack->setBackgroundColor(selected ? QColor("#B7D7FF") : QColor(Qt::white));
    ui->wgtRackInfo->setBackgroundColor(Qt::white);
    ui->lblRackId->setText(rack.emergency ? QStringLiteral("急诊")
                                          : QStringLiteral("预设%1:%2").arg(index).arg(rack.name));
    ui->lblLeftTime->setText(QStringLiteral("剩余时间:--:--:--"));
    ui->btnEdit->setVisible(!rack.emergency);
    connect(ui->btnEdit, &QPushButton::clicked, this, [this]() { emit renameRequested(id_); });
    int x = 20;
    for (const SampleHole &hole : rack.holes)
    {
        bool configured = false;
        for (const SampleTask &task : hole.tasks)
        {
            if (task.repeats > 0)
            {
                configured = true;
            }
        }
        auto *view = new LocalSampleHoleView(configured, ui->wgtRack);
        view->setGeometry(x, 20, 68, 68);
        x += 105;
        view->setToolTip(QStringLiteral("孔%1 / 本地预设；未下发").arg(hole.number));
    }
    ui->wgtRack->setFixedWidth(x);
    setToolTip(QStringLiteral("仅本地预设，真实执行状态未知；双击编辑"));
}
SampleRackView::~SampleRackView()
{
    delete ui;
}
void SampleRackView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit selected(id_);
    }
    QWidget::mousePressEvent(event);
}
void SampleRackView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit editRequested(id_);
    }
    QWidget::mouseDoubleClickEvent(event);
}
void SampleTaskView::setNames(QStringList names, QList<SampleTaskState> states)
{
    names_ = names;
    states_ = states;
    const int perRow = qMax(1, (width() - 3) / 75);
    setMinimumHeight(qMax(36, ((names.size() + perRow - 1) / perRow) * 33 + 3));
    update();
}
void SampleTaskView::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int perRow = qMax(1, (width() - 3) / 75);
    for (int i = 0; i < names_.size(); ++i)
    {
        // 沿用原CusSampleTaskView的四种状态图片；这里只显示已保存的本地草稿状态。
        const SampleTaskState state = i < states_.size() ? states_[i] : SampleTaskState::Idle;
        QString image = QStringLiteral(":/samples/t_set.png");
        if (state == SampleTaskState::Finished)
        {
            image = QStringLiteral(":/samples/t_finshed.png");
        }
        else if (state == SampleTaskState::Testing)
        {
            image = QStringLiteral(":/samples/t_testing.png");
        }
        else if (state == SampleTaskState::Error)
        {
            image = QStringLiteral(":/samples/t_errored.png");
        }
        const QPixmap background(image);
        const QRect rectangle(3 + (i % perRow) * 75, 3 + (i / perRow) * 33, 72, 30);
        painter.drawPixmap(rectangle, background);
        QFont font("Noto Sans CJK SC");
        font.setPixelSize(14);
        while (QFontMetrics(font).horizontalAdvance(names_[i]) > 72 && font.pixelSize() > 2)
        {
            font.setPixelSize(font.pixelSize() - 2);
        }
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(rectangle, Qt::AlignCenter, names_[i]);
    }
}
