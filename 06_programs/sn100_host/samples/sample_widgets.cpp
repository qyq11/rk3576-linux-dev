#include "sample_widgets.h"
#include "ui_cussamplerack.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QElapsedTimer>
#include <QTimer>
class LocalSampleHoleView : public QWidget
{
  public:
    LocalSampleHoleView(SampleHoleState state, QWidget *parent) : QWidget(parent), state_(state) {}

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        // 原CusSampleHole::drawBg：准备、检测中、结束/错误各用对应状态图。
        QString image=QStringLiteral(":/samples/s_none.png");
        if (state_==SampleHoleState::Ready) image=QStringLiteral(":/samples/s_setted.png");
        else if (state_==SampleHoleState::Testing) image=QStringLiteral(":/samples/s_testting.png");
        else if (state_==SampleHoleState::Finished || state_==SampleHoleState::Error)
            image=QStringLiteral(":/samples/s_finished.png");
        painter.drawPixmap(0, 0, QPixmap(image));
    }

  private:
    SampleHoleState state_;
};
SampleRackView::SampleRackView(const SampleRack &rack, int index, bool selected, QWidget *parent)
    : QWidget(parent), ui(new Ui::CusSampleRack), id_(rack.id), selected_(selected)
{
    ui->setupUi(this);
    setFixedSize(1341, 116);
    ui->wgtRack->setRadius(60);
    ui->wgtRackInfo->setRadius(10);
    ui->wgtRack->setBackgroundColor(selected_ ? selectedColour_ : QColor(Qt::white));
    ui->wgtRackInfo->setBackgroundColor(Qt::white);
    ui->lblRackId->setText(rack.emergency ? QStringLiteral("急诊")
                                          : QStringLiteral("预设%1:%2").arg(index).arg(rack.name));
    ui->lblLeftTime->setText(QStringLiteral("剩余时间:--:--:--"));
    bool testing=false, hasError=false;
    for (const auto &hole : rack.holes)
    {
        testing=testing || sampleHoleState(sampleHoleTaskStates(hole))==SampleHoleState::Testing;
        hasError=hasError || sampleHoleTaskStates(hole).contains(SampleTaskState::Error);
    }
    if (hasError)
        ui->lblLeftTime->setStyleSheet(QStringLiteral("QLabel{background-color:white;color:#FF0000;border-radius:1px;border:1px solid white;}"));
    if (testing && rack.remainingSeconds>0)
    {
        auto *timer=new QTimer(this);
        const auto update=[this,rack]()
        {
            QElapsedTimer clock; clock.start();
            const qint64 elapsed=qMax(qint64(0),clock.msecsSinceReference()-rack.remainingAtMs)/1000;
            const qint64 seconds=qMax(qint64(1),qint64(rack.remainingSeconds)-elapsed);
            ui->lblLeftTime->setText(QStringLiteral("剩余时间:%1:%2:%3")
                .arg(seconds/3600,2,10,QLatin1Char('0')).arg(seconds/60%60,2,10,QLatin1Char('0')).arg(seconds%60,2,10,QLatin1Char('0')));
        };
        connect(timer,&QTimer::timeout,this,update);
        timer->start(1000); update();
    }
    ui->btnEdit->setVisible(!rack.emergency);
    connect(ui->btnEdit, &QPushButton::clicked, this, [this]() { emit renameRequested(id_); });
    int x = 20;
    for (const SampleHole &hole : rack.holes)
    {
        auto *view = new LocalSampleHoleView(sampleHoleState(sampleHoleTaskStates(hole)), ui->wgtRack);
        view->setGeometry(x, 20, 68, 68);
        x += 105;
        view->setToolTip(QStringLiteral("孔%1").arg(hole.number));
    }
    ui->wgtRack->setFixedWidth(x);
    setToolTip(QStringLiteral("样本架%1；双击编辑").arg(rack.name));
}
void SampleRackView::setSelectedColour(QColor colour)
{
    if (!colour.isValid()) return;
    selectedColour_=colour;
    ui->wgtRack->setBackgroundColor(selected_ ? selectedColour_ : QColor(Qt::white));
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
        // 状态图沿用原CusSampleTaskView；状态来自存储快照，设备进度保存后再刷新。
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
