// Sources: frm_report/{frm_report_material,mask_dlg_query_report,
// mask_dlg_report_detail,mask_dlg_export}.{ui,cpp}. Original inner geometry,
// table arrangement and operation order are retained. Native Qt5 controls
// replace Material/ripple/mask/chart classes for this independent offline slice;
// QSS restores the known source/theme colors. Original Windows visual parity,
// chart/recalculation/LIS and template printing remain separately unverified.
#include "history_page.h"
#include "result_statistics.h"
#include "ui_frm_report_material.h"
#include "ui_mask_dlg_query_report.h"
#include "ui_mask_dlg_report_detail.h"
#include "ui_mask_dlg_export.h"

#include <QAbstractButton>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QRadioButton>
#include <QRegExpValidator>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QVBoxLayout>
#include <functional>

namespace {
QString buttonStyle()
{
    // Same reference visual keys already established by O01; no runtime INI.
    return QStringLiteral("QDialog {background:white;} .QPushButton {"
        "background:rgba(42,118,242,255);color:white;border-radius:4px;padding:2px;}"
        ".QPushButton:hover{background:rgba(42,118,242,170);}"
        ".QPushButton:pressed{background:rgba(135,206,250,255);}"
        ".QPushButton:disabled{background:#DDDDDD;color:#777777;}");
}

QString tableStyle()
{
    return QStringLiteral("QHeaderView::section{color:#444444;font:bold 15px \"Noto Sans CJK SC\";"
        "height:60px;background:#D6D6D6;border:1px solid #DDDDDD;}"
        "QTableWidget{background:white;alternate-background-color:#EEEEEE;gridline-color:#D6D6D6;}"
        "QTableWidget::item{color:#444444;}"
        "QTableWidget::item:selected{background:rgba(183,215,255,255);color:#444444;}");
}

void disableControl(QWidget *widget, const QString &reason)
{
    widget->setEnabled(false);
    widget->setToolTip(reason);
}

class DetailDialog : public QDialog
{
public:
    explicit DetailDialog(QWidget *parent) : QDialog(parent) {}
    std::function<bool()> mayClose;
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (mayClose && !mayClose())
        {
            event->ignore();
            return;
        }
        // QDialog::closeEvent可能再次调用reject，已批准的关闭不重复弹确认。
        m_closeApproved = true;
        QDialog::closeEvent(event);
        m_closeApproved = false;
    }
    void reject() override
    {
        if (m_closeApproved || !mayClose || mayClose()) QDialog::reject();
    }
private:
    bool m_closeApproved = false;
};

class DetailContent : public QDialog
{
public:
    explicit DetailContent(QWidget *parent) : QDialog(parent, Qt::Widget) {}
    std::function<void()> requestClose;
protected:
    void reject() override
    {
        // Escape in the embedded QDialog must close its outer guarded dialog,
        // rather than hide just the fixed-size business content.
        if (requestClose) requestClose();
    }
};

void setCell(QTableWidget *table, int row, int column, const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    table->setItem(row, column, item);
}
}

HistoryPage::HistoryPage(HistoryStore *store, QWidget *parent) :
    QWidget(parent), ui(new Ui::frmReportMaterial)
{
    ui->setupUi(this);
    setFixedSize(1751, 968);
    setObjectName(QStringLiteral("historyPage"));
    setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
    setStyleSheet(tableStyle());
    ui->wgtReport->setStyleSheet(QStringLiteral("#wgtReport{background:white;border-radius:8px;}"));
    // Non-sensitive shipped theme keys: rightBaseColor=28.28.28.255,
    // rightBtnColor=127.127.127.255, rightBtnFontColor defaults to white.
    ui->reportFunMenu->setStyleSheet(QStringLiteral(
        "#reportFunMenu{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #1C1C1C,stop:1 #323232);}"
        "QPushButton{background:#7F7F7F;color:white;border:none;border-radius:8px;}"
        "QPushButton:disabled{background:#686868;color:#B0B0B0;}"));
    const QString unavailable = tr("O04-1 未接入此功能；保留原控件，后续按独立模块验收。");
    for (QWidget *control : QList<QWidget *>{ui->btnDisplay, ui->btnUpdate,
                            ui->btnPrinter, ui->btnDel, ui->btn_save_modify})
        disableControl(control, unavailable);
    // Original constructor hides this button; preserve that source behavior.
    ui->btn_save_modify->hide();
    const QList<QTableWidget *> tables{ui->tableResultReportOne, ui->tablePatientInfo, ui->tableResultReport};
    for (auto *table : tables)
    {
        table->setStyleSheet(tableStyle());
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->verticalHeader()->hide();
        table->setAlternatingRowColors(true);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        if (table != ui->tableResultReport)
        {
            connect(table, &QTableWidget::cellClicked, this, [this](int row, int) { selectRow(row); });
            connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) { selectRow(row); openDetail(); });
        }
    }
    ui->tableResultReportOne->setColumnCount(3);
    ui->tableResultReportOne->setHorizontalHeaderLabels({tr("☐ 样本号"), tr("样本条码"), tr("报告日期")});
    ui->tableResultReportOne->setFixedWidth(550);
    ui->tableResultReportOne->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    ui->tableResultReportOne->setColumnWidth(0, 150);
    ui->tableResultReportOne->setColumnWidth(1, 180);
    ui->tableResultReportOne->setColumnWidth(2, 220);
    ui->tableResultReportOne->horizontalHeader()->setSectionsClickable(true);
    connect(ui->tableResultReportOne->horizontalHeader(), &QHeaderView::sectionClicked, this, [this](int section) {
        if (section != 0 || m_reports.isEmpty()) return;
        bool allChecked = true;
        for (int row = 0; row < m_reports.size(); ++row)
            allChecked = allChecked && ui->tableResultReportOne->item(row, 0)->checkState() == Qt::Checked;
        for (int row = 0; row < m_reports.size(); ++row)
            ui->tableResultReportOne->item(row, 0)->setCheckState(allChecked ? Qt::Unchecked : Qt::Checked);
        ui->tableResultReportOne->horizontalHeaderItem(0)->setText(allChecked ? tr("☐ 样本号") : tr("☑ 样本号"));
    });
    ui->tablePatientInfo->setColumnCount(4);
    ui->tablePatientInfo->setHorizontalHeaderLabels({tr("姓名"), tr("年龄"), tr("性别"), tr("状态")});
    ui->tablePatientInfo->setFixedWidth(320);
    ui->tablePatientInfo->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    for (int column = 0; column < 4; ++column)
        ui->tablePatientInfo->setColumnWidth(column, column == 0 ? 120 : (column == 3 ? 80 : 60));
    connect(ui->tableResultReportOne->verticalScrollBar(), &QScrollBar::valueChanged,
            ui->tablePatientInfo->verticalScrollBar(), &QScrollBar::setValue);
    // Shipped ReportTableType=1 is the original vertical result display.
    ui->horizontalLayout_2->removeWidget(ui->tableResultReport);
    ui->horizontalLayout_2->addSpacing(20);
    ui->horizontalLayout_2->addWidget(ui->tableResultReport);
    ui->tableResultReport->setFixedWidth(599);
    ui->tableResultReport->setColumnCount(5);
    ui->tableResultReport->setHorizontalHeaderLabels({tr("项目"), tr("结果"), tr("单位"), tr("时间"), QStringLiteral("taskID")});
    ui->tableResultReport->setColumnHidden(4, true);
    ui->tableResultReport->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(ui->btnQuery, &QPushButton::clicked, this, &HistoryPage::openQuery);
    connect(ui->btnDetail, &QPushButton::clicked, this, &HistoryPage::openDetail);
    connect(ui->btnExport, &QPushButton::clicked, this, &HistoryPage::openExport);
    connect(ui->btnFirstPage, &QPushButton::clicked, this, [this]() { queryPage(0); });
    connect(ui->btnPrvPage, &QPushButton::clicked, this, [this]() { queryPage(m_filter.offset / 20 - 1); });
    connect(ui->btnNextPage, &QPushButton::clicked, this, [this]() { queryPage(m_filter.offset / 20 + 1); });
    connect(ui->btnLastPage, &QPushButton::clicked, this, [this]() { queryPage(qMax<qint64>(0, (m_total - 1) / 20)); });
    connect(ui->btnPrvRow, &QPushButton::clicked, this, [this]() { moveRow(-1); });
    connect(ui->btnNextRow, &QPushButton::clicked, this, [this]() { moveRow(1); });
    ui->btnFirstPage->setText(QStringLiteral("|◀"));
    ui->btnPrvPage->setText(QStringLiteral("◀◀"));
    ui->btnPrvRow->setText(QStringLiteral("◀"));
    ui->btnNextRow->setText(QStringLiteral("▶"));
    ui->btnNextPage->setText(QStringLiteral("▶▶"));
    ui->btnLastPage->setText(QStringLiteral("▶|"));

    connect(this, &HistoryPage::requestQuery, store, &HistoryStore::query, Qt::QueuedConnection);
    connect(this, &HistoryPage::requestDetail, store, &HistoryStore::detail, Qt::QueuedConnection);
    connect(this, &HistoryPage::requestSavePatient, store, &HistoryStore::savePatient, Qt::QueuedConnection);
    connect(this, &HistoryPage::requestExportCsv, store, &HistoryStore::exportCsv, Qt::QueuedConnection);
    connect(store, &HistoryStore::ready, this, [this](qint64, const QString &) {
        m_ready = true;
        emit operationFinished(QStringLiteral("initialize"), true);
        queryPage(0);
    });
    connect(store, &HistoryStore::queryReady, this,
            [this](qint64 id, HistoryFilter filter, HistoryReports reports, qint64 total) {
        if (id != m_pending) return;
        m_filter = filter;
        m_reports = reports;
        m_total = total;
        displayReports();
        status(tr("查询完成：%1 条报告，第 %2 页。").arg(total).arg(filter.offset / 20 + 1));
        finish(QStringLiteral("query"), true);
    });
    connect(store, &HistoryStore::detailReady, this, [this](qint64 id, HistoryReport report) {
        if (id != m_pending) return;
        showDetail(report);
        finish(QStringLiteral("detail"), true);
    });
    connect(store, &HistoryStore::patientSaved, this, [this](qint64 id, HistoryPatient patient) {
        if (id != m_pending) return;
        m_detailReport.patient = patient;
        for (auto &report : m_reports) if (report.id == patient.reportId) report.patient = patient;
        if (m_detailUi)
        {
            for (auto *edit : m_detailUi->page_4->findChildren<QLineEdit *>())
                edit->setProperty("loadedText", edit->text());
            m_detailUi->cbxSex->setProperty("selectionEdited", false);
        }
        m_dirty = false;
        displayReports();
        status(tr("保存成功！患者信息已提交数据库。"));
        finish(QStringLiteral("savePatient"), true);
    });
    connect(store, &HistoryStore::csvExported, this,
            [this](qint64 id, const QString &path, qint64 reports, qint64 rows) {
        if (id != m_pending) return;
        status(tr("导出完成：%1 条报告 / %2 行，%3").arg(reports).arg(rows).arg(path));
        finish(QStringLiteral("exportCsv"), true);
    });
    connect(store, &HistoryStore::error, this, [this](qint64 id, const QString &operation, const QString &message) {
        if (id != 0 && id != m_pending) return;
        status(operation + QStringLiteral(": ") + message);
        finish(id == 0 ? QStringLiteral("initialize") : m_operation, false);
    });
    setBusy(false);
    connect(ui->btnPrinter, &QPushButton::clicked, this, [this]() { printReport(false); });
}

HistoryPage::~HistoryPage()
{
    if (m_detailDialog) delete m_detailDialog.data();
    delete m_detailUi;
    delete ui;
}

void HistoryPage::setDataDirectory(const QString &directory) { m_directory = directory; }

void HistoryPage::setReportPreviewEnabled(bool enabled)
{
    m_reportPreviewEnabled = enabled;
    ui->btnPrinter->setToolTip(enabled ? tr("使用原版模板预览；脱机不连接物理打印机。") : tr("当前入口未绑定报告预览。"));
    setBusy(m_busy);
}

void HistoryPage::setReportOperationBusy(bool busy)
{
    m_reportBusy = busy;
    setBusy(m_busy);
}

void HistoryPage::printReport(bool detail)
{
    if (!m_reportPreviewEnabled || !m_ready || isBusy()) return;
    if (m_dirty)
    {
        status(tr("患者信息尚未保存，请先保存或取消修改，再预览报告。"));
        return;
    }
    if (detail && m_detailUi) emit reportPreviewRequested(m_detailReport);
    else if (hasCurrentRow()) emit reportPreviewRequested(m_reports.at(ui->tableResultReportOne->currentRow()));
    else status(tr("请先选择一条报告。"));
}

bool HistoryPage::confirmLeave(const QString &action)
{
    if (!m_ready || isBusy())
    {
        status(tr("历史数据操作尚未结束，请等待结果后再%1。").arg(action));
        return false;
    }
    if (!m_dirty)
    {
        return true;
    }
    QWidget *parent = this;
    if (m_detailDialog && m_detailDialog->isVisible()) parent = m_detailDialog.data();
    QMessageBox dialog(QMessageBox::Question, tr("尚未保存"),
                       tr("%1将放弃当前患者信息修改，是否继续？").arg(action),
                       QMessageBox::Discard | QMessageBox::Cancel, parent);
    dialog.setObjectName(QStringLiteral("historyDiscardDialog"));
    dialog.setDefaultButton(QMessageBox::Cancel);
    dialog.setEscapeButton(QMessageBox::Cancel);
    dialog.setButtonText(QMessageBox::Discard, tr("放弃并继续"));
    dialog.setButtonText(QMessageBox::Cancel, tr("保留更改"));
    for (QAbstractButton *button : dialog.buttons())
    {
        const int iconWidth = button->icon().isNull() ? 0 : button->iconSize().width() + 8;
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + iconWidth + 24);
    }
    if (dialog.exec() != QMessageBox::Discard)
    {
        return false;
    }
    // m_detailReport仅在读取明细或收到patientSaved后更新，是最后成功快照。
    // 放弃绝不emit requestSavePatient或重读数据库，切回页面不会留下草稿。
    if (m_detailUi)
    {
        const HistoryReport snapshot = m_detailReport;
        fillDetail(snapshot);
    }
    m_dirty = false;
    status(tr("已放弃未保存的患者信息，恢复上次成功读取或提交的数据。"));
    return true;
}

bool HistoryPage::requestLeave(const QString &action)
{
    if (!confirmLeave(action))
    {
        return false;
    }
    if (m_detailDialog && m_detailDialog->isVisible() && !m_detailDialog->close())
    {
        return false;
    }
    // 导航不会触发子对话框closeEvent。显式取消尚未提交的筛选/导出选择，
    // 包括筛选日期弹窗，避免页面隐藏后留下模态窗口；不触发查询或文件写入。
    for (const QPointer<QDialog> &dialog : {m_queryDialog, m_exportDialog, m_exportFileDialog})
    {
        if (!dialog || !dialog->isVisible()) continue;
        for (QDialog *child : dialog->findChildren<QDialog *>())
        {
            if (child->isWindow() && child->isVisible() && !child->close()) return false;
        }
        if (!dialog->close()) return false;
    }
    return true;
}

void HistoryPage::status(const QString &message)
{
    setProperty("lastStatus", message);
    if (m_detailDialog) m_detailDialog->setWindowTitle(message);
    emit statusChanged(message);
}

void HistoryPage::setBusy(bool busy)
{
    m_busy = busy;
    busy = isBusy();
    for (auto *control : {ui->btnQuery, ui->btnDetail, ui->btnExport, ui->btnFirstPage,
                          ui->btnPrvPage, ui->btnNextPage, ui->btnLastPage, ui->btnPrvRow, ui->btnNextRow})
        control->setEnabled(m_ready && !busy);
    ui->btnPrinter->setEnabled(m_reportPreviewEnabled && m_ready && !busy);
    if (m_detailUi)
    {
        m_detailUi->btnSave->setEnabled(!busy);
        m_detailUi->btnCancel->setEnabled(!busy);
        m_detailUi->btnClose->setEnabled(!busy);
        m_detailUi->page_4->setEnabled(!busy);
        m_detailUi->btnPrinter->setEnabled(m_reportPreviewEnabled && m_ready && !busy);
    }
}

void HistoryPage::finish(const QString &operation, bool success)
{
    setBusy(false);
    emit operationFinished(operation, success);
}

void HistoryPage::refresh() { queryPage(m_filter.offset / 20); }
void HistoryPage::queryPage(int page)
{
    if (!m_ready || isBusy()) return;
    const int lastPage = qMax<qint64>(0, (m_total - 1) / 20);
    page = qBound(0, page, lastPage);
    m_filter.offset = page * 20;
    m_filter.limit = 20;
    m_pending = ++m_serial;
    m_operation = QStringLiteral("query");
    setBusy(true);
    status(tr("正在查询…"));
    emit requestQuery(m_pending, m_filter);
}

void HistoryPage::displayReports()
{
    qint64 selectedId = -1;
    if (hasCurrentRow()) selectedId = m_reports.at(ui->tableResultReportOne->currentRow()).id;
    ui->tableResultReport->setRowCount(0);
    ui->tableResultReportOne->horizontalHeaderItem(0)->setText(tr("☐ 样本号"));
    for (auto *table : {ui->tableResultReportOne, ui->tablePatientInfo})
    {
        table->clearContents();
        table->setRowCount(m_reports.size());
    }
    for (int row = 0; row < m_reports.size(); ++row)
    {
        const auto &report = m_reports.at(row);
        setCell(ui->tableResultReportOne, row, 0, QString::number(report.sampleNumber));
        ui->tableResultReportOne->item(row, 0)->setCheckState(Qt::Unchecked);
        ui->tableResultReportOne->item(row, 0)->setData(Qt::UserRole, report.id);
        setCell(ui->tableResultReportOne, row, 1, report.barcode);
        setCell(ui->tableResultReportOne, row, 2, report.dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
        setCell(ui->tablePatientInfo, row, 0, report.patient.name);
        setCell(ui->tablePatientInfo, row, 1, QString::number(report.patient.age));
        setCell(ui->tablePatientInfo, row, 2, HistoryStore::sexText(report.patient.sex));
        setCell(ui->tablePatientInfo, row, 3, report.state);
        for (auto *table : {ui->tableResultReportOne, ui->tablePatientInfo}) table->setRowHeight(row, 40);
    }
    ui->ldtCurPage->setText(QString::number(m_filter.offset / 20 + 1));
    ui->lblTotalPage->setText(QString::number(qMax<qint64>(1, (m_total + 19) / 20)));
    if (!m_reports.isEmpty())
    {
        int selectedRow = 0;
        for (int row = 0; row < m_reports.size(); ++row) if (m_reports.at(row).id == selectedId) selectedRow = row;
        selectRow(selectedRow);
    }
}

void HistoryPage::setAverageEnabled(bool enabled)
{
    m_averageEnabled = enabled;
    displaySelectedResults(ui->tableResultReportOne->currentRow());
}

void HistoryPage::displaySelectedResults(int reportRow)
{
    auto *table = ui->tableResultReport;
    table->setRowCount(0);
    if (reportRow < 0 || reportRow >= m_reports.size()) return;
    const auto &report = m_reports.at(reportRow);
    const auto groups = HistoryResultStatistics::groups(report);
    for (const auto &group : groups)
    {
        for (const auto &entry : group.entries)
        {
            const int row = table->rowCount();
            table->insertRow(row);
            setCell(table, row, 0, entry.value.label);
            setCell(table, row, 1, entry.valuePresent
                ? (entry.abnormalType == QStringLiteral("1") ? QStringLiteral("*") : QString()) + HistoryStore::displayValue(entry.value)
                : QString());
            if (!entry.valuePresent)
                table->item(row, 1)->setToolTip(tr("任务 %1 未保存此结果类型；显示为空并计入无效复测，不补写原始结果。").arg(entry.taskId));
            setCell(table, row, 2, entry.value.unit);
            setCell(table, row, 3, entry.testedAt.toString(QStringLiteral("HH:mm:ss")));
            setCell(table, row, 4, QString::number(entry.taskId));
            table->setRowHeight(row, 50);
        }
        if (!m_averageEnabled || !group.hasAverageRow())
            continue;
        const int row = table->rowCount();
        table->insertRow(row);
        // Preserve slot_showSingleRowResult's five original columns. The
        // average/CV is a display-only summary, never a replacement task.
        setCell(table, row, 0, tr("平均值"));
        setCell(table, row, 1, group.meanText);
        setCell(table, row, 2, QStringLiteral("CV"));
        setCell(table, row, 3, group.cvText);
        setCell(table, row, 4, QString());
        table->setRowHeight(row, 50);
        const QString sampleInfo = tr("%1 / %2：有效 %3，未参与 %4；样本标准差使用 n-1；CV=SD/均值×100%。")
            .arg(group.project, group.label).arg(group.validCount).arg(group.invalidCount);
        for (int column = 0; column < table->columnCount(); ++column)
            table->item(row, column)->setToolTip(sampleInfo);
        if (!group.meanUnavailableReason.isEmpty())
            table->item(row, 1)->setToolTip(sampleInfo + QChar('\n') + group.meanUnavailableReason);
        if (!group.cvUnavailableReason.isEmpty())
            table->item(row, 3)->setToolTip(sampleInfo + QChar('\n') + group.cvUnavailableReason);
    }
}

bool HistoryPage::hasCurrentRow() const
{
    const int row = ui->tableResultReportOne->currentRow();
    return row >= 0 && row < m_reports.size();
}
void HistoryPage::selectRow(int row)
{
    if (row < 0 || row >= m_reports.size()) return;
    for (auto *table : {ui->tableResultReportOne, ui->tablePatientInfo}) table->selectRow(row);
    displaySelectedResults(row);
}
void HistoryPage::moveRow(int delta)
{
    const int row = ui->tableResultReportOne->currentRow() + delta;
    if (row >= 0 && row < m_reports.size()) selectRow(row);
    else queryPage(m_filter.offset / 20 + delta);
}

void HistoryPage::openQuery()
{
    if (!m_ready || isBusy()) return;
    if (m_queryDialog && m_queryDialog->isVisible()) return;
    auto *dialog = new QDialog(this);
    m_queryDialog = dialog;
    dialog->setObjectName(QStringLiteral("MaskDlgQueryReport"));
    auto *form = new Ui::MaskDlgQueryReport;
    form->setupUi(dialog);
    dialog->setWindowTitle(tr("报告查询"));
    dialog->setFixedSize(501, 501);
    dialog->setStyleSheet(buttonStyle());
    form->btnClose->setText(QStringLiteral("×"));
    form->btnCalendarStart->setText(QStringLiteral("…"));
    form->btnCalendarEnd->setText(QStringLiteral("…"));
    disableControl(form->btnEditBatch, tr("条码直接输入；此切片不接入软键盘。"));
    form->ckxByDate->setText(tr("按日期查询"));
    form->ckxBySampleCode->setText(tr("按样本条码查询"));
    form->ckxByDate->setChecked(m_filter.byDate);
    form->ckxBySampleCode->setChecked(!m_filter.barcode.isEmpty());
    form->ldtStartDate->setText(m_filter.start.toString(QStringLiteral("yyyy/MM/dd")));
    form->ldtEndDate->setText(m_filter.end.toString(QStringLiteral("yyyy/MM/dd")));
    form->ldtSampleID->setText(m_filter.sampleNumber);
    form->ldtSampleCode->setText(m_filter.barcode);
    form->ldtSampleID->setValidator(new QRegExpValidator(QRegExp(QStringLiteral("^[0-9A-Za-z]{4}$")), dialog));
    form->ldtSampleCode->setValidator(new QRegExpValidator(QRegExp(QStringLiteral("^[A-Za-z0-9].{32}$")), dialog));
    const auto calendar = [dialog](QLineEdit *edit) {
        auto *popup = new QDialog(dialog);
        popup->setWindowTitle(QObject::tr("选择日期"));
        auto *layout = new QVBoxLayout(popup);
        auto *view = new QCalendarWidget(popup);
        layout->addWidget(view);
        const QDate date = QDate::fromString(edit->text(), QStringLiteral("yyyy/MM/dd"));
        if (date.isValid()) view->setSelectedDate(date);
        QObject::connect(view, &QCalendarWidget::clicked, popup, [edit, popup](const QDate &chosen) {
            edit->setText(chosen.toString(QStringLiteral("yyyy/MM/dd"))); popup->accept();
        });
        popup->setAttribute(Qt::WA_DeleteOnClose);
        popup->open();
    };
    connect(form->btnCalendarStart, &QPushButton::clicked, dialog, [=]() { calendar(form->ldtStartDate); });
    connect(form->btnCalendarEnd, &QPushButton::clicked, dialog, [=]() { calendar(form->ldtEndDate); });
    connect(form->btnCancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(form->btnSave, &QPushButton::clicked, dialog, [this, dialog, form]() {
        HistoryFilter next = m_filter;
        next.byDate = form->ckxByDate->isChecked();
        next.start = QDateTime(QDate::fromString(form->ldtStartDate->text(), QStringLiteral("yyyy/MM/dd")), QTime(0, 0));
        next.end = QDateTime(QDate::fromString(form->ldtEndDate->text(), QStringLiteral("yyyy/MM/dd")), QTime(23, 59, 59));
        if (next.byDate && (!next.start.isValid() || !next.end.isValid() || next.start > next.end))
        { dialog->setWindowTitle(tr("请选择有效日期，起始日期不能晚于结束日期。")); return; }
        if (form->ckxBySampleCode->isChecked() && form->ldtSampleCode->text().isEmpty())
        { dialog->setWindowTitle(tr("请输入查询样本条码！")); return; }
        next.sampleNumber = form->ldtSampleID->text();
        // The original DAL receives these text values regardless of checkbox
        // state. Keep that surprising behavior until the Windows baseline is
        // available; the checkbox only controls the nonempty-input validation.
        next.barcode = form->ldtSampleCode->text();
        m_filter = next;
        dialog->accept();
        queryPage(0);
    });
    connect(dialog, &QObject::destroyed, this, [form]() { delete form; });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void HistoryPage::openDetail()
{
    if (!m_ready || isBusy() || !hasCurrentRow()) return;
    m_pending = ++m_serial;
    m_operation = QStringLiteral("detail");
    setBusy(true);
    emit requestDetail(m_pending, m_reports.at(ui->tableResultReportOne->currentRow()).id);
}

void HistoryPage::showDetail(const HistoryReport &report)
{
    if (!m_detailDialog)
    {
        auto *dialog = new DetailDialog(this);
        dialog->mayClose = [this]() { return confirmLeave(tr("关闭报告明细")); };
        m_detailDialog = dialog;
        m_detailUi = new Ui::MaskDlgReportDetail;
        auto *content = new DetailContent(dialog);
        m_detailUi->setupUi(content);
        content->setObjectName(QStringLiteral("historyDetailContent"));
        content->setFixedSize(1341, 930);
        content->requestClose = [dialog]() { dialog->close(); };
        auto *scroll = new QScrollArea(dialog);
        scroll->setObjectName(QStringLiteral("historyDetailScroll"));
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(false);
        scroll->setWidget(content);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(scroll);
        dialog->setObjectName(QStringLiteral("MaskDlgReportDetail"));
        const QScreen *screen = QGuiApplication::primaryScreen();
        const QSize available = screen ? screen->availableGeometry().size() - QSize(40, 60) : QSize(1365, 990);
        dialog->resize(QSize(1365, 954).boundedTo(available));
        dialog->setStyleSheet(buttonStyle());
        m_detailUi->btnClose->setText(QStringLiteral("×"));
        m_detailUi->projectResultList->setStyleSheet(tableStyle());
        m_detailUi->projectResultList->setStyleSheet(QStringLiteral(
            "QHeaderView::section{color:#444444;font:bold 15px \"Noto Sans CJK SC\";height:40px;"
            "background:transparent;border:1px solid #DDDDDD;border-left:none;border-right:none;border-top:none;}"
            "QTableWidget{background:transparent;alternate-background-color:#EEEEEE;}"
            "QTableWidget::item:selected{background:rgba(183,215,255,255);}"));
        m_detailUi->wgtTabs_2->setStyleSheet(QStringLiteral(
            ".QPushButton{color:#AAAAAA;border:none;background:transparent;border-top:5px solid transparent;}"
            ".QPushButton:checked{color:#444444;border-top:5px solid rgba(232,51,35,255);}"));
        m_detailUi->projectResultList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        m_detailUi->projectResultList->setColumnHidden(4, true);
        m_detailUi->projectResultList->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        const QString reason = tr("待 O04 后续切片接入原始曲线/算法/设备/LIS/打印模板；本页不执行该动作。");
        for (QWidget *control : QList<QWidget *>{m_detailUi->btnPrinter, m_detailUi->btnUpdateLis,
                                m_detailUi->btn_recal, m_detailUi->cbxLightType, m_detailUi->cbxYaixsType,
                                m_detailUi->tableResultSelect, m_detailUi->btnUpRow, m_detailUi->btnNextRow})
            disableControl(control, reason);
        m_detailUi->wgtChart->setToolTip(reason);
        m_detailUi->ldtSmapleID->setReadOnly(true);
        m_detailUi->ldtReportDate->setReadOnly(true);
        m_detailUi->ldtAge->setValidator(new QIntValidator(0, 999, dialog));
        for (QLineEdit *edit : {m_detailUi->ldtName, m_detailUi->ldtKeShi, m_detailUi->ldtBingRenLeiXin,
                               m_detailUi->ldtApplyNumber, m_detailUi->ldtChuangHao, m_detailUi->ldtJianYan,
                               m_detailUi->ldtSongJian, m_detailUi->ldtShenHe})
        {
            // Retain original validator (including its exact-length quirk),
            // rather than maxLength: setText must never truncate stored fields.
            edit->setValidator(new QRegExpValidator(QRegExp(QStringLiteral("^[A-Za-z0-9].{8}$")), dialog));
            edit->setToolTip(tr("沿用原输入校验：首字符为字母/数字，其后 8 个字符；已有数据原样载入。"));
        }
        m_detailUi->ldtBeiZhu->setValidator(new QRegExpValidator(QRegExp(QStringLiteral("^[A-Za-z0-9].+$")), dialog));
        for (auto *edit : m_detailUi->page_4->findChildren<QLineEdit *>())
            connect(edit, &QLineEdit::textChanged, dialog, [this]() { if (!m_filling) m_dirty = true; });
        connect(m_detailUi->cbxSex, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog,
                [this](int) { if (!m_filling) { m_dirty = true; m_detailUi->cbxSex->setProperty("selectionEdited", true); } });
        connect(m_detailUi->btnMenuResult, &QPushButton::clicked, dialog, [this]() { m_detailUi->stkResult->setCurrentIndex(0); });
        connect(m_detailUi->btnMenuInfo, &QPushButton::clicked, dialog, [this]() { m_detailUi->stkResult->setCurrentIndex(1); });
        connect(m_detailUi->btnMenuResultInfo, &QPushButton::clicked, dialog, [this]() { m_detailUi->stkTestInfo->setCurrentIndex(0); });
        connect(m_detailUi->btnMenuErrorInfo, &QPushButton::clicked, dialog, [this]() { m_detailUi->stkTestInfo->setCurrentIndex(1); });
        connect(m_detailUi->btnSave, &QPushButton::clicked, this, &HistoryPage::savePatient);
        connect(m_detailUi->btnPrinter, &QPushButton::clicked, this, [this]() { printReport(true); });
        connect(m_detailUi->btnCancel, &QPushButton::clicked, dialog, [this, dialog]() {
            fillDetail(m_detailReport); // Explicit Cancel discards the editor draft.
            dialog->close();
        });
        connect(m_detailUi->btnClose, &QPushButton::clicked, dialog, &QDialog::close);
        // 关闭前的共用守卫已恢复快照；不再靠finished静默清掉未保存标记。
    }
    fillDetail(report);
    m_detailDialog->setWindowTitle(tr("报告明细"));
    m_detailDialog->open();
}

void HistoryPage::fillDetail(const HistoryReport &report)
{
    m_filling = true;
    m_detailReport = report;
    const auto &patient = report.patient;
    m_detailUi->ldtSmapleID->setText(report.barcode);
    m_detailUi->ldtReportDate->setText(report.dateTime.toString(QStringLiteral("yyyy-MM-dd HH-mm-ss")));
    m_detailUi->ldtName->setText(patient.name);
    m_detailUi->ldtAge->setText(QString::number(patient.age));
    m_detailUi->cbxSex->setCurrentIndex(patient.sex == 1 ? 1 : 0);
    m_detailUi->ldtKeShi->setText(patient.department);
    m_detailUi->ldtBingRenLeiXin->setText(patient.patientType);
    m_detailUi->ldtApplyNumber->setText(patient.applicationSerial);
    m_detailUi->ldtChuangHao->setText(patient.bedNumber);
    m_detailUi->ldtJianYan->setText(patient.examiner);
    m_detailUi->ldtSongJian->setText(patient.sender);
    m_detailUi->ldtShenHe->setText(patient.auditor);
    m_detailUi->ldtBeiZhu->setText(patient.remark);
    // QLineEdit is a single-line editor. Preserve untouched original strings
    // (including legacy newline data) instead of serializing its display text.
    for (auto *edit : m_detailUi->page_4->findChildren<QLineEdit *>())
        edit->setProperty("loadedText", edit->text());
    m_detailUi->cbxSex->setProperty("loadedIndex", m_detailUi->cbxSex->currentIndex());
    m_detailUi->cbxSex->setProperty("selectionEdited", false);
    auto *table = m_detailUi->projectResultList;
    table->setRowCount(0);
    m_detailUi->lstResultInfo->clear();
    m_detailUi->lstErrorInfo->clear();
    m_detailUi->lstTestInfo->clear();
    for (const auto &result : report.results)
    {
        if (result.values.isEmpty()) continue;
        const auto &value = result.values.first();
        const int row = table->rowCount();
        table->insertRow(row);
        setCell(table, row, 0, result.project);
        setCell(table, row, 1, (result.abnormalType == QStringLiteral("1") ? QStringLiteral("*") : QString()) + HistoryStore::displayValue(value));
        setCell(table, row, 2, value.unit);
        setCell(table, row, 3, result.abnormalType == QStringLiteral("1") ? QStringLiteral("*") : QString());
        setCell(table, row, 4, QString::number(result.taskId));
        table->setRowHeight(row, 41);
        for (const auto &entry : result.values)
            m_detailUi->lstResultInfo->addItem(result.project + QStringLiteral(" ") + entry.label + QStringLiteral(": ") + HistoryStore::displayValue(entry) + QStringLiteral(" ") + entry.unit);
        if (!result.abnormalType.isEmpty() && result.abnormalType != QStringLiteral("0"))
            m_detailUi->lstErrorInfo->addItem(tr("任务 %1 异常码：%2").arg(result.taskId).arg(result.abnormalType));
    }
    m_detailUi->stkResult->setCurrentIndex(0);
    m_detailUi->stkTestInfo->setCurrentIndex(0);
    m_filling = false;
    m_dirty = false;
}

void HistoryPage::savePatient()
{
    if (!m_detailUi || isBusy()) return;
    HistoryPatient patient = m_detailReport.patient;
    patient.reportId = m_detailReport.id;
    const auto editedText = [](QLineEdit *edit, const QString &original) {
        return edit->text() == edit->property("loadedText").toString() ? original : edit->text();
    };
    patient.name = editedText(m_detailUi->ldtName, patient.name);
    bool ageValid = false;
    patient.age = m_detailUi->ldtAge->text().toInt(&ageValid);
    if (!ageValid || patient.age < 0 || patient.age > 999) { status(tr("年龄必须为 0–999 的整数。")); return; }
    if (m_detailUi->cbxSex->property("selectionEdited").toBool())
        patient.sex = m_detailUi->cbxSex->currentIndex() == 0 ? 0 : 1;
    patient.department = editedText(m_detailUi->ldtKeShi, patient.department);
    patient.patientType = editedText(m_detailUi->ldtBingRenLeiXin, patient.patientType);
    patient.applicationSerial = editedText(m_detailUi->ldtApplyNumber, patient.applicationSerial);
    patient.bedNumber = editedText(m_detailUi->ldtChuangHao, patient.bedNumber);
    patient.examiner = editedText(m_detailUi->ldtJianYan, patient.examiner);
    patient.sender = editedText(m_detailUi->ldtSongJian, patient.sender);
    patient.auditor = editedText(m_detailUi->ldtShenHe, patient.auditor);
    patient.remark = editedText(m_detailUi->ldtBeiZhu, patient.remark);
    m_pending = ++m_serial;
    m_operation = QStringLiteral("savePatient");
    setBusy(true);
    status(tr("正在保存，请等待数据库结果…"));
    emit requestSavePatient(m_pending, patient);
}

void HistoryPage::openExport()
{
    if (!m_ready || isBusy()) return;
    if (m_exportDialog && m_exportDialog->isVisible()) return;
    auto *dialog = new QDialog(this);
    m_exportDialog = dialog;
    dialog->setObjectName(QStringLiteral("MaskDlgExport"));
    auto *form = new Ui::MaskDlgExport;
    form->setupUi(dialog);
    dialog->setFixedSize(501, 291);
    dialog->setWindowTitle(tr("导出报告"));
    dialog->setStyleSheet(buttonStyle());
    form->ckxByAll->setText(tr("导出当前查询报告"));
    form->ckxBySelect->setText(tr("导出当前选中报告"));
    form->ckxBySelect->setChecked(true);
    connect(form->btnCancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(form->btnSave, &QPushButton::clicked, dialog, [this, dialog, form]() {
        const bool all = form->ckxByAll->isChecked();
        auto *chooser = new QFileDialog(this, tr("请选择报告保存路径…"),
            QDir(m_directory).filePath(QStringLiteral("exports/") + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmmsszzz")) + QStringLiteral(".csv")),
            tr("Config Files (*.csv)"));
        m_exportFileDialog = chooser;
        chooser->setObjectName(QStringLiteral("historyCsvFileDialog"));
        chooser->setOption(QFileDialog::DontUseNativeDialog);
        chooser->setAcceptMode(QFileDialog::AcceptSave);
        chooser->setDefaultSuffix(QStringLiteral("csv"));
        chooser->setAttribute(Qt::WA_DeleteOnClose);
        connect(chooser, &QFileDialog::fileSelected, this, [this, all](const QString &path) { exportCsv(path, all); });
        dialog->accept();
        chooser->open();
    });
    connect(dialog, &QObject::destroyed, this, [form]() { delete form; });
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
}

void HistoryPage::exportCsv(QString path, bool all)
{
    if (!m_ready || isBusy()) return;
    HistoryIds selected;
    if (!all)
    {
        for (int row = 0; row < m_reports.size(); ++row)
            if (ui->tableResultReportOne->item(row, 0)->checkState() == Qt::Checked) selected << m_reports.at(row).id;
        if (selected.isEmpty()) { status(tr("导出数据不能为空！请勾选报告。")); emit operationFinished(QStringLiteral("exportCsv"), false); return; }
    }
    m_pending = ++m_serial;
    m_operation = QStringLiteral("exportCsv");
    setBusy(true);
    status(tr("正在导出…"));
    emit requestExportCsv(m_pending, m_filter, selected, path);
}

void HistoryPage::closeEvent(QCloseEvent *event)
{
    if (!requestLeave(tr("关闭窗口"))) { event->ignore(); return; }
    QWidget::closeEvent(event);
}
