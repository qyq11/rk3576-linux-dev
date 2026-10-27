#include "report_dictionary_dialog.h"
#include "ui_mask_dlg_select.h"

#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHeaderView>
#include <QScreen>
#include <QScrollArea>
#include <QValidator>
#include <QVBoxLayout>

namespace
{
class DictionaryFilterValidator : public QValidator
{
public:
    explicit DictionaryFilterValidator(QObject *parent) : QValidator(parent) {}

    State validate(QString &input, int &) const override
    {
        return input.size() <= kHospitalDictionaryFilterLimit ? Acceptable : Invalid;
    }
};

bool isDictionaryType(const QString &type)
{
    return type == QStringLiteral("keshi") || type == QStringLiteral("bingren")
            || type == QStringLiteral("jianyan") || type == QStringLiteral("songjian")
            || type == QStringLiteral("shenhe");
}

void setCell(QTableWidget *table, int row, int column, const QString &value)
{
    auto *item = new QTableWidgetItem(value);
    item->setTextAlignment(Qt::AlignCenter);
    table->setItem(row, column, item);
}
}

ReportDictionaryDialog::ReportDictionaryDialog(const QString &type, QWidget *parent) :
    QDialog(parent), ui(new Ui::MaskDlgSelect), m_type(type)
{
    registerHospitalDictionaryTypes();
    ui->setupUi(this);
    setObjectName(QStringLiteral("MaskDlgSelect"));
    setWindowTitle(tr("选择医院字典"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setWindowModality(Qt::WindowModal);

    // 原 mask 运行时把491×751内容居中；保留内层几何，只为小屏提供外层滚动。
    auto *container = new QWidget(this);
    auto *center = new QVBoxLayout(container);
    center->setContentsMargins(11, 11, 11, 11);
    ui->view_frame->setParent(container);
    ui->view_frame->setFixedSize(491, 751);
    ui->view_frame->setStyleSheet(QStringLiteral(
        "QFrame#view_frame{background:#FFFFFF;border-radius:11px;}"));
    center->addWidget(ui->view_frame, 0, Qt::AlignCenter);
    auto *shadow = new QGraphicsDropShadowEffect(ui->view_frame);
    shadow->setBlurRadius(11);
    shadow->setColor(QColor(0, 0, 0, 100));
    shadow->setOffset(0, 3);
    ui->view_frame->setGraphicsEffect(shadow);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("dictionaryScreenScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(container);
    ui->horizontalLayout->removeWidget(ui->mask);
    ui->mask->hide();
    ui->horizontalLayout->addWidget(scroll);
    setStyleSheet(QStringLiteral(
        "QDialog#MaskDlgSelect{background:rgba(0,0,0,100);}"
        "QScrollArea#dictionaryScreenScroll,QScrollArea#dictionaryScreenScroll>QWidget>QWidget{background:transparent;}"
        "QPushButton{background:rgba(42,118,242,255);color:white;border-radius:4px;}"
        "QPushButton:hover{background:rgba(42,118,242,170);}"
        "QPushButton:pressed{background:rgba(135,206,250,255);}"
        "QPushButton:disabled{background:#DDDDDD;color:#777777;}"
        "QPushButton#btnClose{background:transparent;border:none;}"));

    // 保留原 tableStyle::getStrSheet2 动态样式，不把Designer的粉色当运行主题。
    ui->lstSelect->setStyleSheet(QStringLiteral(
        "QHeaderView{background:transparent;}"
        "QHeaderView::section{color:#444444;font:bold 15px \"苹方 粗体\";"
        "height:40px;background:transparent;border:1px solid #DDDDDD;"
        "border-left:none;border-right:none;border-top:none;}"
        "QTableWidget::item{border:0px solid #DDDDDD;border-bottom:1px solid #DDDDDD;color:#444444;}"
        "QTableView::item:selected{background:rgba(183,215,255,255);}"
        "QTableWidget{background:transparent;border:1px solid #E4E8EB;border-radius:5px;}"));
    if (!QFontDatabase().families().contains(QStringLiteral("苹方 粗体")))
    {
        for (QWidget *widget : findChildren<QWidget *>())
        {
            QFont font = widget->font();
            font.setFamily(QStringLiteral("Noto Sans CJK SC"));
            widget->setFont(font);
            widget->setStyleSheet(widget->styleSheet().replace(
                QStringLiteral("苹方 粗体"), QStringLiteral("Noto Sans CJK SC")));
        }
    }
    // 修复固定33字/ASCII开头的误用，但保留原筛选容量；拒绝超长而非截断。
    ui->ldtConditionSelect->setValidator(new DictionaryFilterValidator(ui->ldtConditionSelect));
    ui->ldtConditionSelect->setToolTip(tr("按名称或助记符查找，最多 %1 个字符；% 和 _ 沿用原 LIKE 通配符。")
                                      .arg(kHospitalDictionaryFilterLimit));
    ui->lstSelect->setColumnCount(3);
    ui->lstSelect->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->lstSelect->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->lstSelect->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->lstSelect->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->btnSave->setAutoDefault(false);
    ui->btnClose->setAutoDefault(false);
    ui->btnSave->setEnabled(false);
    connect(ui->ldtConditionSelect, &QLineEdit::textChanged,
            this, &ReportDictionaryDialog::query);
    connect(ui->btnSave, &QPushButton::clicked,
            this, &ReportDictionaryDialog::confirmSelection);
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::reject);
}

ReportDictionaryDialog::~ReportDictionaryDialog()
{
    delete ui;
}

void ReportDictionaryDialog::openForSelection()
{
    if (m_active)
    {
        return;
    }
    QScreen *screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    if (screen)
    {
        setGeometry(screen->availableGeometry());
    }
    m_active = true;
    open();
    query(ui->ldtConditionSelect->text());
}

void ReportDictionaryDialog::showTableMessage(const QString &message)
{
    m_entries.clear();
    ui->lstSelect->clearContents();
    ui->lstSelect->setRowCount(0);
    ui->lstSelect->setCurrentCell(-1, -1);
    if (!message.isEmpty())
    {
        ui->lstSelect->setRowCount(1);
        ui->lstSelect->setSpan(0, 0, 1, 3);
        auto *item = new QTableWidgetItem(message);
        item->setFlags(Qt::NoItemFlags);
        item->setTextAlignment(Qt::AlignCenter);
        ui->lstSelect->setItem(0, 0, item);
        ui->lstSelect->setRowHeight(0, 50);
    }
}

void ReportDictionaryDialog::query(const QString &filter)
{
    if (!m_active)
    {
        return;
    }
    m_ready = false;
    ui->btnSave->setEnabled(false);
    ui->lstSelect->clearSpans();
    m_request = HospitalDictionaryRequest();
    // setText可以绕过validator；这种输入也不能截成另一个有效查询。
    if (filter.size() > kHospitalDictionaryFilterLimit)
    {
        showTableMessage(tr("筛选条件最多 %1 个字符；未查询，未截断。")
                         .arg(kHospitalDictionaryFilterLimit));
        return;
    }
    if (!isDictionaryType(m_type))
    {
        showTableMessage(tr("不支持的字典类别；请关闭后重试。"));
        return;
    }
    m_request.requestId = QUuid::createUuid();
    m_request.type = m_type;
    m_request.filter = filter;
    showTableMessage(tr("正在读取…"));
    emit queryRequested(m_request);
}

void ReportDictionaryDialog::receiveReply(HospitalDictionaryReply reply)
{
    if (!m_active || !sameHospitalDictionaryRequest(reply.request, m_request))
    {
        return;
    }
    ui->lstSelect->clearSpans();
    if (!reply.succeeded)
    {
        m_ready = false;
        ui->btnSave->setEnabled(false);
        showTableMessage(tr("读取失败，请改筛选重试或关闭。\n%1").arg(reply.error));
        return;
    }
    showTableMessage(QString());
    m_entries = reply.entries;
    ui->lstSelect->setRowCount(m_entries.size());
    for (int row = 0; row < m_entries.size(); ++row)
    {
        const HospitalDictionaryEntry &entry = m_entries.at(row);
        setCell(ui->lstSelect, row, 0, QString::number(row + 1));
        setCell(ui->lstSelect, row, 1, entry.name);
        setCell(ui->lstSelect, row, 2, entry.mnemonic);
        ui->lstSelect->setRowHeight(row, 50);
    }
    if (!m_entries.isEmpty())
    {
        ui->lstSelect->selectRow(0);
    }
    m_ready = true;
    // 原版空结果点确定也关闭，但不发送名称。
    ui->btnSave->setEnabled(true);
}

void ReportDictionaryDialog::confirmSelection()
{
    if (!m_active || !m_ready)
    {
        return;
    }
    const int row = ui->lstSelect->currentRow();
    if (row >= 0 && row < m_entries.size())
    {
        emit nameSelected(m_entries.at(row).name);
    }
    accept();
}

void ReportDictionaryDialog::done(int result)
{
    m_active = false;
    m_ready = false;
    m_request = HospitalDictionaryRequest();
    QDialog::done(result);
}
