#include "lis_inbox_page.h"
#include <QAbstractItemView>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace
{
QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    return label;
}
void readOnlyTable(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size()); table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSortingEnabled(false);
    table->horizontalHeader()->setStretchLastSection(true);
}
}
LisInboxPage::LisInboxPage(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("离线原报文收件箱"));
    setWindowModality(Qt::NonModal);
    resize(960, 720);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(plainLabel(tr("原始报文／未映射／未应用检测任务"), this));
    layout->addWidget(plainLabel(tr("这是新增的离线导入查看窗口。仅接收不超过1MiB（1048576字节）的匿名UTF-8、单个完整DSR报文；不会连接LIS或发送确认。选择文件不保存，点击“确认导入”才读取并提交。"), this));
    admission_ = plainLabel(QString(), this); layout->addWidget(admission_);
    auto *form = new QFormLayout;
    file_ = new QLineEdit(this); file_->setReadOnly(true);
    expected_ = new QLineEdit(this);
    identity_ = new QLineEdit(this); identity_->setReadOnly(true);
    form->addRow(tr("所选匿名文件"), file_);
    form->addRow(tr("报文控制号（MSH-10，原样核对）"), expected_);
    form->addRow(tr("本地接收标识（错误重试保持不变）"), identity_);
    layout->addLayout(form);
    layout->addWidget(plainLabel(tr("首次读得的字节、控制号及接收标识会用于重试。需换内容或控制号时先取消草稿；取消不会删除已保存记录。"), this));
    auto *actions = new QHBoxLayout;
    choose_ = new QPushButton(tr("选择匿名文件"), this);
    import_ = new QPushButton(tr("确认导入"), this);
    discard_ = new QPushButton(tr("取消草稿"), this);
    open_ = new QPushButton(tr("重新打开收件箱"), this);
    for (auto *button : {choose_, import_, discard_, open_}) { button->setAutoDefault(false); actions->addWidget(button); }
    layout->addLayout(actions);
    receipt_ = plainLabel(QString(), this); layout->addWidget(receipt_);
    auto *tabs = new QTabWidget(this);
    auto *listPage = new QWidget(tabs); auto *listLayout = new QVBoxLayout(listPage);
    list_ = new QTableWidget(listPage);
    readOnlyTable(list_, {tr("实际收件ID"), tr("保存时间"), tr("报文控制号"), tr("报文类型"), tr("字节数"), tr("原始状态")});
    listLayout->addWidget(list_);
    auto *navigation = new QHBoxLayout;
    refresh_ = new QPushButton(tr("刷新列表"), listPage);
    previous_ = new QPushButton(tr("上一页"), listPage); next_ = new QPushButton(tr("下一页"), listPage);
    detail_ = new QPushButton(tr("查看所选详情"), listPage); range_ = plainLabel(QString(), listPage);
    for (auto *button : {refresh_, previous_, next_, detail_}) { button->setAutoDefault(false); navigation->addWidget(button); }
    navigation->addWidget(range_); listLayout->addLayout(navigation);
    tabs->addTab(listPage, tr("真实收件列表"));
    auto *detailPage = new QWidget(tabs); auto *detailLayout = new QVBoxLayout(detailPage);
    detailMetadata_ = plainLabel(tr("请选择实际收件ID读取详情。"), detailPage); detailLayout->addWidget(detailMetadata_);
    raw_ = new QPlainTextEdit(detailPage); raw_->setReadOnly(true);
    detailLayout->addWidget(raw_);
    detailLayout->addWidget(plainLabel(tr("全文按纯文本显示，控制字符可能不可见；数据库仍保留完整原始字节，不按HTML解释。"), detailPage));
    tabs->addTab(detailPage, tr("原始全文"));
    auto *fieldPage = new QWidget(tabs); auto *fieldLayout = new QVBoxLayout(fieldPage);
    segments_ = new QComboBox(fieldPage); fieldLayout->addWidget(segments_);
    segmentRaw_ = new QPlainTextEdit(fieldPage); segmentRaw_->setReadOnly(true); segmentRaw_->setMaximumHeight(100);
    fieldLayout->addWidget(segmentRaw_);
    fields_ = new QTableWidget(fieldPage); readOnlyTable(fields_, {tr("原字段位置"), tr("原始字符串（空值保留）")});
    fieldLayout->addWidget(fields_); tabs->addTab(fieldPage, tr("原始段与字段"));
    layout->addWidget(tabs, 1);
    status_ = plainLabel(tr("等待收件箱初始化回执。"), this); layout->addWidget(status_);
    close_ = new QPushButton(tr("关闭"), this); close_->setAutoDefault(false); layout->addWidget(close_);
    connect(choose_, &QPushButton::clicked, this, [this]()
    {
        const QString path = QFileDialog::getOpenFileName(this, tr("选择用户提供的匿名原报文"), QString(), tr("原报文 (*.dsr *.hl7 *.txt);;所有文件 (*)"));
        if (path.isEmpty()) return;
        if (path == path_) { showStatus(tr("仍是同一文件草稿；保留现有接收身份。")); return; }
        if (!path_.isEmpty() && !confirmDiscard()) return;
        emit fileSelected(path);
    });
    connect(import_, &QPushButton::clicked, this, [this]() { emit importRequested(expected_->text()); });
    connect(discard_, &QPushButton::clicked, this, [this]() { if (confirmDiscard()) emit discardRequested(); });
    connect(open_, &QPushButton::clicked, this, &LisInboxPage::reopenRequested);
    connect(refresh_, &QPushButton::clicked, this, [this]() { emit listRequested(offset_); });
    connect(previous_, &QPushButton::clicked, this, [this]() { emit listRequested(qMax(0, offset_ - 20)); });
    connect(next_, &QPushButton::clicked, this, [this]() { emit listRequested(offset_ + 20); });
    auto readSelected = [this]()
    {
        const int row = list_->currentRow();
        if (row >= 0 && list_->item(row, 0)) emit detailRequested(list_->item(row, 0)->data(Qt::UserRole).toLongLong());
    };
    connect(detail_, &QPushButton::clicked, this, readSelected);
    connect(list_, &QTableWidget::cellDoubleClicked, this, [readSelected](int, int) { readSelected(); });
    connect(list_, &QTableWidget::itemSelectionChanged, this, &LisInboxPage::updateControls);
    connect(segments_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LisInboxPage::showSegment);
    connect(expected_, &QLineEdit::textChanged, this, &LisInboxPage::updateControls);
    connect(close_, &QPushButton::clicked, this, &QWidget::close);
    updateControls();
}
void LisInboxPage::setState(bool ready, bool canImport, bool busy, bool closing)
{
    ready_ = ready; canImport_ = canImport; busy_ = busy; closing_ = closing;
    admission_->setText(canImport_ ? tr("当前会话允许确认导入；后台仍会复核实际会话。")
                                 : tr("当前会话只读：访客或未获有效认证，不能导入文件。"));
    updateControls();
}
void LisInboxPage::setDraft(QString path, QString identity, QString expected, bool committed)
{
    if (path != path_ || identity.isEmpty()) receipt_->clear();
    path_ = path; file_->setText(path); identity_->setText(identity);
    identityLocked_ = !identity.isEmpty(); committed_ = committed;
    expected_->setText(expected); updateControls();
}
void LisInboxPage::showStatus(QString message, bool error)
{
    status_->setText((error ? tr("失败／未确认：") : QString()) + message);
}
void LisInboxPage::showReceipt(qint64 actualId, bool duplicate)
{
    receipt_->setText(duplicate ? tr("已确认同一接收身份先前已保存，实际收件ID：%1").arg(actualId)
                               : tr("已收到真实提交成功回执，实际收件ID：%1").arg(actualId));
}
void LisInboxPage::showList(LisDsrRaw::Summaries rows, qint64 total, int offset)
{
    offset_ = offset; total_ = total; shownRows_ = rows.size(); list_->setRowCount(rows.size());
    for (int index = 0; index < rows.size(); ++index)
    {
        const auto &row = rows.at(index);
        const QStringList values{QString::number(row.id), row.receivedAt.toString(Qt::ISODateWithMs), row.controlId,
                                 row.messageType, QString::number(row.frameBytes), row.status};
        for (int column = 0; column < values.size(); ++column) list_->setItem(index, column, new QTableWidgetItem(values.at(column)));
        list_->item(index, 0)->setData(Qt::UserRole, row.id);
    }
    range_->setText(tr("共%1条；本页%2条，起点%3").arg(total_).arg(shownRows_).arg(offset_));
    updateControls();
}
void LisInboxPage::showDetail(LisDsrRaw::Message actual)
{
    actual_ = actual;
    detailMetadata_->setText(tr("实际ID：%1\n本地接收标识：%2\n控制号：%3\n原类型：%4；原状态：%5\n原始字节：%6；原始段数：%7")
        .arg(actual.id).arg(actual.receptionIdentity, actual.controlId, actual.messageType, actual.status)
        .arg(actual.frame.size()).arg(actual.segments.size()));
    raw_->setPlainText(QString::fromUtf8(actual.frame));
    const QSignalBlocker blocker(segments_);
    segments_->clear();
    for (int index = 0; index < actual.segments.size(); ++index)
        segments_->addItem(tr("段%1：%2").arg(index).arg(actual.segments.at(index).name));
    if (!actual.segments.isEmpty()) segments_->setCurrentIndex(0);
    showSegment(actual.segments.isEmpty() ? -1 : 0);
}
void LisInboxPage::showSegment(int index)
{
    fields_->setRowCount(0); segmentRaw_->clear();
    if (index < 0 || index >= actual_.segments.size()) return;
    const auto &segment = actual_.segments.at(index); segmentRaw_->setPlainText(segment.rawText);
    fields_->setRowCount(segment.fields.size());
    for (int field = 0; field < segment.fields.size(); ++field)
    {
        fields_->setItem(field, 0, new QTableWidgetItem(QString::number(field)));
        fields_->setItem(field, 1, new QTableWidgetItem(segment.fields.at(field)));
    }
}
void LisInboxPage::updateControls()
{
    const bool idle = !busy_ && !closing_;
    choose_->setEnabled(idle && ready_ && canImport_);
    expected_->setEnabled(idle && ready_ && canImport_ && !path_.isEmpty() && !identityLocked_);
    import_->setEnabled(idle && ready_ && canImport_ && !path_.isEmpty() && !expected_->text().isEmpty() && !committed_);
    import_->setText(committed_ ? tr("本草稿已确认保存") : identityLocked_ ? tr("重试导入（同一身份）") : tr("确认导入"));
    discard_->setEnabled(idle && !path_.isEmpty()); open_->setEnabled(idle);
    refresh_->setEnabled(idle && ready_); previous_->setEnabled(idle && ready_ && offset_ > 0);
    next_->setEnabled(idle && ready_ && offset_ + shownRows_ < total_);
    list_->setEnabled(idle && ready_); detail_->setEnabled(idle && ready_ && list_->currentRow() >= 0);
    close_->setEnabled(!closing_);
}
bool LisInboxPage::confirmDiscard()
{
    return QMessageBox::question(this, tr("取消当前文件草稿"),
        tr("这会放弃当前文件草稿及其重试身份，不会删除已保存记录。是否继续？"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}
void LisInboxPage::closeEvent(QCloseEvent *event)
{
    if (finalClose_) { event->accept(); return; }
    event->ignore();
    if (closing_) return;
    if (busy_) { showStatus(tr("后台操作尚未返回，请等待实际结果后再关闭。关闭不会用来取消正在提交的事务。")); return; }
    if (!path_.isEmpty() && !confirmDiscard()) return;
    emit closeRequested();
}
void LisInboxPage::reject() { close(); }
void LisInboxPage::finishClose()
{
    finalClose_ = true; close(); deleteLater();
}
