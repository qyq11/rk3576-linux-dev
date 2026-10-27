#include "report_remaining_display_dialog.h"
#include "ui_mask_dlg_display_setting.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGuiApplication>
#include <QListWidget>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

namespace
{
const char kColumnMime[] = "application/x-sn100-report-column";

class DisplayButton : public QPushButton
{
public:
    explicit DisplayButton(const QString &title, QWidget *parent)
        : QPushButton(title, parent) {}
    std::function<void()> dragFinished;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        m_start = event->pos();
        m_dragged = false;
        QPushButton::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!(event->buttons() & Qt::LeftButton) || m_dragged
            || (event->pos() - m_start).manhattanLength() < QApplication::startDragDistance())
        {
            QPushButton::mouseMoveEvent(event);
            return;
        }
        m_dragged = true;
        setDown(false);
        QDrag drag(this);
        auto *mime = new QMimeData;
        mime->setData(kColumnMime, property("columnKey").toString().toUtf8());
        drag.setMimeData(mime);
        drag.setPixmap(grab());
        drag.setHotSpot(m_start);
        drag.exec(Qt::MoveAction);
        // Rebuilding after the drag loop unwinds preserves the source button's
        // lifetime, even when a drop moved it to a different list position.
        if (dragFinished) dragFinished();
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (m_dragged)
        {
            m_dragged = false;
            setDown(false);
            event->accept();
            return;
        }
        QPushButton::mouseReleaseEvent(event);
    }
private:
    QPoint m_start;
    bool m_dragged = false;
};

class DisplayContent : public QDialog
{
public:
    explicit DisplayContent(QWidget *parent) : QDialog(parent, Qt::Widget) {}
    std::function<void()> requestClose;
protected:
    void reject() override { if (requestClose) requestClose(); }
};
}

ReportRemainingDisplayDialog::ReportRemainingDisplayDialog(HistoryDisplayColumns columns,
    HistoryDisplayColumns defaults, QWidget *parent)
    : QDialog(parent), ui(new Ui::MaskDlgDisplaySetting),
      m_columns(columns), m_defaults(defaults), m_initialColumns(columns)
{
    setObjectName(QStringLiteral("MaskDlgDisplaySetting"));
    setWindowTitle(tr("报告显示设置"));
    auto *content = new DisplayContent(this);
    ui->setupUi(content);
    content->setObjectName(QStringLiteral("historyDisplayContent"));
    // Preserve every original business coordinate. Only the unused outer mask
    // width is cropped; a scroll host makes the bottom buttons reachable on
    // the 1600x900 desktop without shrinking their text or click targets.
    content->setFixedSize(1050, 995);
    content->requestClose = [this]() { reject(); };
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("historyDisplayScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(false);
    scroll->setWidget(content);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scroll);
    const QScreen *screen = QGuiApplication::primaryScreen();
    const QSize available = screen ? screen->availableGeometry().size() - QSize(40, 60) : QSize(1100, 940);
    resize(QSize(1074, 1019).boundedTo(available));
    setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    setStyleSheet(QStringLiteral("QDialog{background:white;}"
        "QPushButton{background:rgba(42,118,242,255);color:white;border-radius:4px;}"
        "QPushButton:hover{background:rgba(42,118,242,170);}"
        "QPushButton:pressed{background:rgba(135,206,250,255);}"
        "QPushButton:disabled{background:#DDDDDD;color:#777777;}"));
    ui->btnClose->setText(QStringLiteral("×"));
    for (QListWidget *list : {ui->lstDisplay_patientInfo, ui->lstDisplay_project})
    {
        list->setDragDropMode(QAbstractItemView::InternalMove);
        list->setDefaultDropAction(Qt::MoveAction);
        list->setAcceptDrops(true);
        list->viewport()->setAcceptDrops(true);
        list->viewport()->installEventFilter(this);
        list->setMovement(QListView::Static);
        list->setResizeMode(QListView::Adjust);
        list->setWrapping(true);
        list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    }
    connect(ui->btnClose, &QPushButton::clicked, this, &ReportRemainingDisplayDialog::reject);
    connect(ui->btnCancel, &QPushButton::clicked, this, &ReportRemainingDisplayDialog::reject);
    connect(ui->btnDefaultSort, &QPushButton::clicked, this, &ReportRemainingDisplayDialog::restoreDefaultOrder);
    connect(ui->btnSave, &QPushButton::clicked, this, [this]()
    {
        if (m_saving) return;
        setSaving(true);
        emit saveRequested(m_columns);
    });
    fillLists();
}

ReportRemainingDisplayDialog::~ReportRemainingDisplayDialog() { delete ui; }

void ReportRemainingDisplayDialog::fillList(QListWidget *list, int group)
{
    list->clear();
    for (const HistoryDisplayColumn &column : m_columns)
    {
        if (column.group != group) continue;
        auto *item = new QListWidgetItem(list);
        item->setData(Qt::UserRole, column.key);
        item->setSizeHint(QSize(154, 84));
        auto *button = new DisplayButton(column.title, list);
        button->setObjectName(QStringLiteral("displayColumn_") + column.key);
        button->setProperty("columnKey", column.key);
        button->setCheckable(true);
        button->setChecked(column.visible || column.required);
        button->setFixedSize(154, 84);
        button->setFocusPolicy(Qt::NoFocus);
        QFont font(QStringLiteral("Noto Sans CJK SC"));
        font.setPixelSize(16);
        button->setFont(font);
        button->setStyleSheet(QStringLiteral("QPushButton{background:#FFFFFF;color:#444444;"
            "border-radius:5px;border:1px solid #DDDDDD;}"
            "QPushButton:checked{color:#FFFFFF;background:rgba(42,118,242,255);}"
            "QPushButton:disabled{color:#777777;border-color:#DDDDDD;}"));
        const QString requiredReason = tr("此为主显示单位，或尚未确认主显示合同，无法取消；历史结果与单位保持原值。");
        button->setToolTip(column.required ? requiredReason : tr("点击选择显示；拖动改变同组顺序。"));
        connect(button, &QPushButton::clicked, this, [this, button, key = column.key, requiredReason](bool checked)
        {
            for (HistoryDisplayColumn &current : m_columns)
            {
                if (current.key != key) continue;
                if (current.required && !checked)
                {
                    button->setChecked(true);
                    setWindowTitle(requiredReason);
                    return;
                }
                current.visible = checked;
                return;
            }
        });
        button->dragFinished = [this]() { QTimer::singleShot(0, this, [this]() { fillLists(); }); };
        list->setItemWidget(item, button);
    }
}

void ReportRemainingDisplayDialog::fillLists()
{
    fillList(ui->lstDisplay_patientInfo, 1);
    fillList(ui->lstDisplay_project, 2);
}

bool ReportRemainingDisplayDialog::moveColumn(const QString &sourceKey, const QString &targetKey, int group)
{
    int source = -1;
    int target = -1;
    for (int index = 0; index < m_columns.size(); ++index)
    {
        if (m_columns.at(index).group != group) continue;
        if (m_columns.at(index).key == sourceKey) source = index;
        if (m_columns.at(index).key == targetKey) target = index;
    }
    if (source < 0 || target < 0) return false;
    if (source != target) m_columns.move(source, target);
    return true;
}

bool ReportRemainingDisplayDialog::eventFilter(QObject *watched, QEvent *event)
{
    QListWidget *list = nullptr;
    int group = 0;
    if (watched == ui->lstDisplay_patientInfo->viewport()) { list = ui->lstDisplay_patientInfo; group = 1; }
    if (watched == ui->lstDisplay_project->viewport()) { list = ui->lstDisplay_project; group = 2; }
    if (!list) return QDialog::eventFilter(watched, event);
    if (event->type() != QEvent::DragEnter && event->type() != QEvent::DragMove
        && event->type() != QEvent::Drop) return QDialog::eventFilter(watched, event);
    auto *drop = static_cast<QDropEvent *>(event);
    auto *source = qobject_cast<QPushButton *>(drop->source());
    const QString key = QString::fromUtf8(drop->mimeData()->data(kColumnMime));
    if (m_saving || !source || !list->isAncestorOf(source)
        || !drop->mimeData()->hasFormat(kColumnMime) || key != source->property("columnKey").toString())
    {
        drop->ignore();
        return true;
    }
    if (event->type() == QEvent::Drop)
    {
        const QListWidgetItem *target = list->itemAt(drop->pos());
        if (!target && list->count() > 0) target = list->item(list->count() - 1);
        if (!target || !moveColumn(key, target->data(Qt::UserRole).toString(), group))
        {
            drop->ignore();
            return true;
        }
    }
    drop->setDropAction(Qt::MoveAction);
    drop->accept();
    return true;
}

void ReportRemainingDisplayDialog::restoreDefaultOrder()
{
    if (m_saving) return;
    HistoryDisplayColumns ordered;
    QSet<QString> used;
    // Default order is a draft operation. Preserve the user's current display
    // choices and do not write storage until Save receives a successful commit.
    for (const HistoryDisplayColumn &baseline : m_defaults)
    {
        for (const HistoryDisplayColumn &column : m_columns)
        {
            if (column.key == baseline.key && column.group == baseline.group && !used.contains(column.key))
            {
                ordered.append(column);
                used.insert(column.key);
            }
        }
    }
    for (const HistoryDisplayColumn &column : m_columns)
        if (!used.contains(column.key)) { ordered.append(column); used.insert(column.key); }
    m_columns = ordered;
    fillLists();
    setWindowTitle(tr("报告显示设置"));
}

void ReportRemainingDisplayDialog::setSaving(bool saving)
{
    m_saving = saving;
    for (QWidget *control : QList<QWidget *>{ui->btnSave, ui->btnCancel, ui->btnClose,
         ui->btnDefaultSort, ui->lstDisplay_patientInfo, ui->lstDisplay_project})
        control->setEnabled(!saving);
    if (saving) setWindowTitle(tr("正在保存显示设置，请等待数据库结果…"));
}

void ReportRemainingDisplayDialog::saveFailed(QString message)
{
    setSaving(false);
    setWindowTitle(tr("显示设置保存失败：") + message);
}

void ReportRemainingDisplayDialog::acceptSaved()
{
    m_saving = false;
    QDialog::accept();
}

bool ReportRemainingDisplayDialog::hasUnsavedChanges() const
{
    if (m_columns.size() != m_initialColumns.size()) return true;
    for (int index = 0; index < m_columns.size(); ++index)
    {
        const HistoryDisplayColumn &current = m_columns.at(index);
        const HistoryDisplayColumn &initial = m_initialColumns.at(index);
        if (current.key != initial.key || current.group != initial.group
            || current.visible != initial.visible) return true;
    }
    return false;
}

void ReportRemainingDisplayDialog::closeEvent(QCloseEvent *event)
{
    if (m_saving) { event->ignore(); return; }
    QDialog::closeEvent(event);
}

void ReportRemainingDisplayDialog::reject()
{
    if (!m_saving) QDialog::reject();
}
