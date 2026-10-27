#include "project_page.h"
#include "project_format_dialog.h"
#include "../history/history_current_display_state.h"
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QTimer>
#include "ui_frmsystemmaterial.h"
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QInputDialog>
#include <QIntValidator>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLocale>
#include <QScopedValueRollback>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QDir>
#include <functional>

namespace
{
// 单位写入尚未回执时保留弹窗，避免用户以为关闭窗口就撤销了写入。
class ProjectUnitDialog : public QDialog
{
public:
    explicit ProjectUnitDialog(QWidget *parent) : QDialog(parent) {}
    std::function<bool()> mayClose;
    void reject() override
    {
        if (!mayClose || mayClose()) QDialog::reject();
    }
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (mayClose && !mayClose()) event->ignore();
        else QDialog::closeEvent(event);
    }
};

int sourceResultId(const QString &type)
{
    const QStringList types = {QStringLiteral("Original"), QStringLiteral("Calibration"),
                              QStringLiteral("Rate"), QStringLiteral("INR")};
    return types.indexOf(type) + 1;
}
bool editableOriginalResult(const ProjectResult &result)
{
    const int expected = sourceResultId(result.type);
    return result.selectionKnown && expected > 0 && result.sourceResultId == expected;
}
QString resultSwitchText(const QString &type, bool unknown = false)
{
    if (type == QStringLiteral("Calibration"))
        return unknown ? QStringLiteral("浓度启用（待确认）") : QStringLiteral("启用浓度结果计算");
    return unknown ? QStringLiteral("%1启用（待确认）").arg(type == QStringLiteral("Rate") ? QStringLiteral("FBG") : type) :
                     QStringLiteral("启用%1结果计算").arg(type == QStringLiteral("Rate") ? QStringLiteral("FBG") : type);
}
QLocale projectNumberLocale()
{
    QLocale locale = QLocale::c();
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    return locale;
}
}

ProjectPage::ProjectPage(ProjectsStore *store, QWidget *parent)
    : QWidget(parent), ui(new Ui::frmSystemMaterial), m_store(store)
{
    ui->setupUi(this);
    setFixedSize(1751, 968);
    applyStyle();
    ui->lstProject->setColumnHidden(0, true);
    ui->lstProject->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->lstProject->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->lstProject->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->lstProject->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->lstProject->verticalHeader()->hide();
    ui->lstProject->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->lstProject, &QWidget::customContextMenuRequested, this, [this](const QPoint &point)
    { openProjectMenu(ui->lstProject->viewport()->mapToGlobal(point)); });
    ui->resultTable->verticalHeader()->hide();
    ui->resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->resultTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->resultTable, &QWidget::customContextMenuRequested, this, [this](const QPoint &point)
    {
        if (!m_ready || m_busy || m_promptOpen || m_auxiliaryDialog || m_formatDialog) return;
        QMenu menu(this);
        auto *configure = menu.addAction(tr("配置结果显示…"));
        configure->setObjectName(QStringLiteral("configureCurrentResultFormat"));
        configure->setEnabled(m_ready && !m_busy && !m_creating && !m_refreshRequired &&
                              !m_promptOpen && m_auxiliaryDialog.isNull() && m_selectedId > 0);
        QAction *chosen;
        {
            QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
            updateActions();
            chosen = menu.exec(ui->resultTable->viewport()->mapToGlobal(point));
        }
        updateActions();
        if (chosen == configure) openResultFormat();
    });
    // Keep the original .ui 116px columns and last-column stretch. Resizing
    // to editor hints would push the unit column offscreen on Linux.
    ui->is_used->setText(tr("项目启用"));
    ui->is_related->setText(tr("组分管理"));
    ui->ckxCalibrationUsed->setText(tr("启用浓度结果计算"));
    ui->ckxRUsed->setText(tr("启用FBG结果计算"));
    ui->ckxINRUsed->setText(tr("启用INR结果计算"));
    ui->ckxMdaUsed->setText(tr("启用MDA结果计算"));
    // Preserve original hidden controls. Unported tabs stay in their original
    // positions and explicitly report unavailable; no instrument is constructed.
    ui->wgtMda->hide();
    ui->btnReTest->hide();
    ui->btnCalculate->hide();
    ui->btnReport->hide();
    // 使用原侧栏按钮位置补空库入口，不改变原页面的业务分组和尺寸。
    ui->btnChange->setText(tr("项目管理…"));
    ui->btnChange->setToolTip(tr("新建项目、导入项目配置、维护单位和重新读取。"));
    connect(ui->btnChange, &QPushButton::clicked, this, [this]()
    { openProjectMenu(ui->btnChange->mapToGlobal(QPoint(0, ui->btnChange->height()))); });
    ui->btnDect->hide();
    ui->btnScan->hide();
    ui->label_32->hide();
    ui->is_related->hide();
    for (QPushButton *tab : {ui->btnCalibrate})
    {
        tab->setEnabled(false);
        tab->setToolTip(tr("校准计算设置尚未验收；本页仅保存本地测试方案资料。"));
    }
    ui->ckxMdaUsed->setEnabled(false);
    const QList<QPair<CusCheckBox *, QString>> resultSwitches = {
        {ui->ckxCalibrationUsed, QStringLiteral("Calibration")},
        {ui->ckxRUsed, QStringLiteral("Rate")}, {ui->ckxINRUsed, QStringLiteral("INR")}};
    for (const auto &resultSwitch : resultSwitches)
        connect(resultSwitch.first, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
                [this, resultSwitch](bool checked) { updateResultSelection(resultSwitch.second, checked); });
    ui->test_analysis_method->setEnabled(false);
    ui->d_T->setReadOnly(true);
    ui->d_T->clear();
    ui->d_T->setToolTip(tr("原字段命名与显示单位尚待核对，当前不编辑或保存采样频率。"));
    ui->manager_id->setReadOnly(true);
    ui->change_time->setReadOnly(true);
    ui->ldt_methon->setReadOnly(true);
    for (QLineEdit *edit : {ui->xishu_a, ui->xishu_b, ui->sample_warmup_length})
    {
        edit->clear();
        auto *validator = new QDoubleValidator(-9999, 9999, 4, edit);
        validator->setLocale(projectNumberLocale());
        validator->setNotation(QDoubleValidator::StandardNotation);
        edit->setValidator(validator);
        edit->setToolTip(tr("算法系数：-9999至9999，最多4位小数；全部留空表示未配置。仅保存参数，未执行完整算法。"));
        connect(edit, &QLineEdit::textEdited, this, &ProjectPage::markDirty);
    }
    ui->sample_warmup_length->setToolTip(tr("原字段在本页表示系数C，不是样本预热时间。全部系数留空表示未配置。"));
    connect(ui->auto_apply, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
            [this](bool) { markDirty(); });
    ui->project_cn_name->setMaxLength(16);
    ui->project_en_name->setMaxLength(16);
    ui->project_lis_code->setMaxLength(8);
    ui->stkPanel->setCurrentIndex(0);
    m_advanced = new ProjectAdvancedEditor(this, this);
    connect(m_advanced, &ProjectAdvancedEditor::edited, this, [this]()
    {
        if (!m_populating && !m_busy && ProjectsStore::canEdit(m_session))
        {
            m_advancedEdited = true;
            markDirty();
        }
    });
    connect(ui->btnBaseInfo, &QPushButton::clicked, this, [this]() { ui->stkPanel->setCurrentIndex(0); });
    connect(ui->btnSampleAdd, &QPushButton::clicked, this, [this]() { ui->stkPanel->setCurrentIndex(1); });
    connect(ui->btnReagentAdd, &QPushButton::clicked, this, [this]() { ui->stkPanel->setCurrentIndex(2); });
    for (QLineEdit *edit : {ui->project_cn_name, ui->project_en_name, ui->project_lis_code})
    {
        connect(edit, &QLineEdit::textChanged, this, &ProjectPage::markDirty);
    }
    connect(ui->is_used, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
            [this](bool) { markDirty(); });
    connect(ui->btnSave, &QPushButton::clicked, this, &ProjectPage::save);
    connect(ui->btnCopy, &QPushButton::clicked, this, &ProjectPage::copy);
    connect(ui->btnDelProj, &QPushButton::clicked, this, &ProjectPage::remove);
    connect(ui->btnCancel, &QPushButton::clicked, this, &ProjectPage::cancel);
    connect(ui->lstProject, &QTableWidget::itemSelectionChanged, this,
            [this]()
            {
                if (m_populating || m_busy || m_promptOpen || m_auxiliaryDialog || m_formatDialog ||
                    ui->lstProject->currentRow() < 0)
                {
                    return;
                }
                const qint64 id =
                    ui->lstProject->item(ui->lstProject->currentRow(), 0)->data(Qt::UserRole).toLongLong();
                if (id == m_selectedId)
                {
                    return;
                }
                if (!prepareToLeave())
                {
                    QSignalBlocker blocker(ui->lstProject);
                    for (int row = 0; row < ui->lstProject->rowCount(); ++row)
                    {
                        if (ui->lstProject->item(row, 0)->data(Qt::UserRole).toLongLong() == m_selectedId)
                        {
                            ui->lstProject->selectRow(row);
                        }
                    }
                    return;
                }
                showProject(id);
            });
    connect(store, &ProjectsStore::sourceProfileChanged, this, [this](const QString &root, bool original)
    {
        const bool changed = original != m_originalProfile || root != m_originalRoot;
        m_originalProfile = original;
        m_originalRoot = original ? root : QString();
        setProperty("originalProfile", original);
        setProperty("originalRoot", m_originalRoot);
        m_advanced->setOriginalProfile(original);
        if (changed)
        {
            m_advancedSourcePath.clear();
            emit currentFormatInvalidated(tr("项目数据来源已切换，旧快照失效。"));
            if (m_pendingOperation == QStringLiteral("load"))
            {
                ++m_request;
                m_pendingOperation.clear();
                setBusy(false);
            }
            if (m_auxiliaryDialog && !m_busy) m_auxiliaryDialog->reject();
        }
        updateActions();
    });
    connect(store, &ProjectsStore::ready, this,
            [this](const QString &databasePath)
            {
                m_ready = true;
                m_catalogDatabasePath = QFileInfo(databasePath).canonicalFilePath();
                if (m_formatDialog)
                {
                    m_formatRefreshAfterClose = true;
                    m_formatDialog->invalidateContext(tr("项目资料源已重新打开，请关闭后重新进入。"));
                }
                emit currentFormatInvalidated(tr("项目资料源已更新。"));
                reload();
            });
    connect(store, &ProjectsStore::closed, this, [this]()
    {
        m_advancedSourcePath.clear();
        m_ready = false;
        ++m_request;
        m_pendingOperation.clear();
        setBusy(false);
        m_catalogDatabasePath.clear();
        emit currentFormatInvalidated(tr("项目资料源已关闭。"));
        updateActions();
    });
    connect(store, &QObject::destroyed, this, [this]()
    {
        m_ready = false;
        ++m_request;
        m_pendingOperation.clear();
        m_catalogDatabasePath.clear();
        emit currentFormatInvalidated(tr("项目存储已销毁。"));
        if (!m_formatDialog) setBusy(false);
        else updateActions();
    });
    connect(store, &ProjectsStore::reagentOptionsReady, m_advanced, &ProjectAdvancedEditor::setReagentChoices);
    connect(store, &ProjectsStore::diluentOptionsReady, m_advanced, &ProjectAdvancedEditor::setDiluentChoices);
    connect(
        store, &ProjectsStore::snapshotReady, this,
        [this](qint64 request, ProjectRecords projects, ProjectUnits units)
        {
            if (request != m_request || m_pendingOperation != QStringLiteral("load"))
            {
                return;
            }
            m_projects = projects;
            m_units = units;
            m_populating = true;
            ui->lstProject->setRowCount(projects.size());
            for (int row = 0; row < projects.size(); ++row)
            {
                auto *id = new QTableWidgetItem(QString::number(projects[row].id));
                id->setData(Qt::UserRole, projects[row].id);
                ui->lstProject->setItem(row, 0, id);
                ui->lstProject->setItem(row, 1, new QTableWidgetItem(projects[row].name));
                ui->lstProject->setRowHeight(row, 41);
            }
            m_populating = false;
            qint64 selected = m_selectAfterLoad > 0 ? m_selectAfterLoad : m_selectedId;
            bool exists = false;
            for (const ProjectRecord &project : projects)
            {
                if (project.id == selected)
                {
                    exists = true;
                }
            }
            if (!exists)
            {
                selected = projects.isEmpty() ? 0 : projects.first().id;
            }
            m_selectAfterLoad = 0;
            m_pendingOperation.clear();
            m_refreshRequired = false;
            m_creating = false;
            showProject(selected);
            setBusy(false);
            emit snapshotChanged(m_projects, m_units);
            if (projects.isEmpty())
            {
                report(tr(
                    "项目库为空；请从右侧“项目管理”维护单位并新建项目，或导入已确认的项目配置。"));
            }
            else
            {
                report(m_originalProfile ? tr("原项目DB及单位已读回；保存将直接写回对应原库，未发送仪器动作。") :
                       tr("项目/单位已读取；当前仅保存本地配置，未同步仪器。"));
            }
            emit operationFinished(QStringLiteral("load"), true);
        });
    connect(store, &ProjectsStore::succeeded, this,
            [this](qint64 request, const QString &operation, qint64 id)
            {
                // The auxiliary writer owns its receipt; never clear the ordinary Page draft for it.
                if (operation == QStringLiteral("replaceCurrentResultFormat")) return;
                if (request != m_request || operation != m_pendingOperation)
                {
                    return;
                }
                if (operation == QStringLiteral("exportOriginalBundle"))
                {
                    m_pendingOperation.clear();
                    setBusy(false);
                    report(tr("原DB及必要关联资料已完整导出，可从目标目录重新读取。"));
                    emit operationFinished(operation, true);
                    return;
                }
                emit currentFormatInvalidated(tr("项目配置已实际提交。"));
                m_dirty = false;
                m_refreshRequired = true;
                m_selectAfterLoad = id > 0 ? id : m_selectedId;
                if (operation == QStringLiteral("remove")) m_selectAfterLoad = 0;
                report(tr("配置已实际写入，正在重新读取；尚未同步仪器。"));
                emit operationFinished(operation, true);
                requestLoad();
            });
    connect(store, &ProjectsStore::error, this,
            [this](qint64 request, const QString &operation, const QString &message)
            {
                if (operation == QStringLiteral("replaceCurrentResultFormat") || operation == QStringLiteral("loadCurrentProjectFormat"))
                {
                    if (request == m_request)
                    {
                        report(message);
                        emit currentFormatInvalidated(message);
                        emit operationFinished(operation, false);
                    }
                    return;
                }
                if (request == 0)
                {
                    if (operation != QStringLiteral("initialize") && operation != QStringLiteral("initializeOriginal")) return;
                    m_ready = false;
                    ++m_request;
                    m_catalogDatabasePath.clear();
                }
                else if (request != m_request || operation != m_pendingOperation)
                {
                    return;
                }
                m_pendingOperation.clear();
                setBusy(false);
                report(message);
                emit currentFormatInvalidated(message);
                emit operationFinished(operation, false);
                // Failed writes retain the editable draft, and the next action may retry.
                if (isVisible())
                {
                    QMessageBox::warning(this, tr("项目设置"), message);
                }
            });
    updateActions();
}
ProjectPage::~ProjectPage()
{
    // Destruction is not a committed configuration event. Peer pages may still
    // exist while their outer shell's derived members are already destroyed.
    QObject::disconnect(this, &ProjectPage::currentFormatInvalidated, nullptr, nullptr);
    QObject::disconnect(this, &ProjectPage::statusChanged, nullptr, nullptr);
    QObject::disconnect(this, &ProjectPage::operationFinished, nullptr, nullptr);
    if (auto *dialog = m_formatDialog.data())
    {
        QObject::disconnect(dialog, nullptr, this, nullptr);
        delete dialog; // No accept/reject/save or fabricated write acknowledgement.
        m_formatDialog.clear();
    }
    delete ui;
}
void ProjectPage::applyStyle()
{
    // Source public/stylecolor.cpp buttonStyle/listWidgetStyle/tableStyle2/
    // comboboxStyle + frmsystemmaterial.cpp constructor; shipped theme values
    // already used by the existing shell. Active Windows theme remains unverified.
    setStyleSheet(QStringLiteral(
        ".QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
        ".QPushButton:hover{background-color:rgba(42,118,242,170);}"
        ".QPushButton:pressed{background-color:rgba(135,206,250,255);}"
        "QHeaderView{background-color:transparent;}"
        "QHeaderView::section{color:#444444;font:bold 15px 'Noto Sans CJK "
        "SC';height:40px;background-color:transparent;border:1px solid "
        "#DDDDDD;border-left:none;border-right:none;border-top:none;}"
        "QTableWidget::item{border:0;border-bottom:1px solid #DDDDDD;}"
        "QTableView::item:selected{background:rgba(183,215,255,255);}"
        "QTableWidget{background-color:transparent;border:1px solid #E4E8EB;border-radius:5px;}"
        "QComboBox{border:1px solid #E4E8EB;border-radius:5px;padding:2px "
        "4px;background-color:transparent;color:#444444;font:18px 'Noto Sans CJK SC';}"
        "QComboBox:disabled{background-color:#E4E4E4;}"
        "QComboBox QAbstractItemView{border:none;color:#444444;font:20px 'Noto Sans CJK SC';}"
        "QComboBox QAbstractItemView::item{height:50px;}"
        "QComboBox QAbstractItemView::item:selected{background:rgba(183,215,255,255);color:white;}"
        "QComboBox::drop-down{width:20px;border:none;}QComboBox::down-arrow{width:25px;height:25px;image:url("
        ":/image/arrow_drop_down.svg);}"));
    const QString tabs = QStringLiteral(
        ".QPushButton{font:bold 18px 'Noto Sans CJK "
        "SC';color:#AAAAAA;border:none;background-color:transparent;border-bottom:5px solid transparent;}"
        ".QPushButton:checked{color:#444444;border-bottom:5px solid rgba(232,51,35,255);}");
    ui->wgtTabs->setStyleSheet(tabs);
    for (QPushButton *tab : {ui->btnBaseInfo, ui->btnSampleAdd, ui->btnReagentAdd, ui->btnCalibrate,
                             ui->btnReport, ui->btnReTest, ui->btnCalculate})
    {
        tab->setStyleSheet(tabs);
    }
    ui->systemFunMenu->setBackgroundColor(QColor(28, 28, 28));
    QLinearGradient gradient(0, 0, 0, ui->systemFunMenu->height());
    gradient.setColorAt(0, QColor(255, 255, 255, 0));
    gradient.setColorAt(1, QColor(255, 255, 255, 25));
    ui->systemFunMenu->setGraBackgroundColor(gradient);
    ui->systemFunMenu->setShadowEffect(Qt::AlignLeft);
    ui->wgtProjTableBg->setRadius(10);
    ui->wgtProjTableBg->setBackgroundColor(Qt::white);
    ui->wgtProjDetailBg->setRadius(10);
    ui->wgtProjDetailBg->setBackgroundColor(Qt::white);
    for (QtMaterialFlatButton *button :
         {ui->btnChange, ui->btnDect, ui->btnScan, ui->btnSave, ui->btnCancel, ui->btnCopy, ui->btnDelProj})
    {
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setBackgroundColor(QColor(127, 127, 127));
        button->setForegroundColor(Qt::white);
        button->setUseThemeColors(false);
    }
}
void ProjectPage::setSession(AccountSession session)
{
    m_session = session;
    if (m_formatDialog) m_formatDialog->setSession(session);
    if (m_store) QMetaObject::invokeMethod(m_store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, session));
    updateActions();
    emit busyChanged(m_busy); // 单位弹窗也立即使用当前身份，不能沿用打开时的权限。
}
void ProjectPage::setBusy(bool busy)
{
    if (busy)
    {
        emit currentFormatInvalidated(tr("项目操作开始，当前结果显示需重新核查。"));
    }
    if (m_busy != busy)
    {
        m_busy = busy;
        emit busyChanged(busy);
    }
    updateActions();
}
void ProjectPage::updateActions()
{
    const bool available = m_ready && m_store && !m_busy && !m_refreshRequired && !m_promptOpen &&
                           m_formatDialog.isNull() && m_auxiliaryDialog.isNull();
    const bool writable = available && (m_selectedId > 0 || m_creating) && ProjectsStore::canEdit(m_session);
    ui->btnChange->setEnabled(m_store && !m_busy && !m_promptOpen &&
                              m_formatDialog.isNull() && m_auxiliaryDialog.isNull());
    ui->btnSave->setEnabled(writable && (m_dirty || m_creating));
    ui->btnCancel->setEnabled(available && (m_selectedId > 0 || m_creating));
    ui->btnCopy->setEnabled(writable && !m_creating);
    ui->btnDelProj->setEnabled(writable && !m_creating && !m_draftProject.fixed);
    ui->btnDelProj->setToolTip(m_draftProject.fixed ? tr("内置固定项目不能删除。") :
                              tr("被试剂、样本或其他记录引用的项目不能删除。"));
    ui->lstProject->setEnabled(available);
    if (!m_ready) ui->btnSave->setToolTip(tr("项目库尚未就绪，请先核对加载错误。"));
    else if (!ProjectsStore::canEdit(m_session)) ui->btnSave->setToolTip(tr("须登录可操作的工程师或管理员账户后维护项目。"));
    else if (m_busy) ui->btnSave->setToolTip(tr("正在等待当前操作回执，请稍后。"));
    else if (m_refreshRequired) ui->btnSave->setToolTip(tr("写入后读取失败，请从项目管理重新读取再编辑。"));
    else if (!m_selectedId && !m_creating) ui->btnSave->setToolTip(tr("请先从项目管理新建或导入项目。"));
    else if (!m_dirty && !m_creating) ui->btnSave->setToolTip(tr("当前没有新的未保存修改，不会重复写入。"));
    else ui->btnSave->setToolTip(m_originalProfile ? tr("写回当前选中的原项目DB；收到实际写入回执后重新读取。") :
                                                    tr("保存当前配置；收到实际写入回执后重新读取。"));
    for (QLineEdit *edit : {ui->project_en_name, ui->project_cn_name, ui->project_lis_code})
    {
        edit->setReadOnly(!writable);
    }
    ui->is_used->setEnabled(writable);
    for (QLineEdit *edit : {ui->xishu_a, ui->xishu_b, ui->sample_warmup_length}) edit->setReadOnly(!writable);
    ui->auto_apply->setEnabled(writable);
    const QList<QPair<CusCheckBox *, QString>> switches = {
        {ui->ckxCalibrationUsed, QStringLiteral("Calibration")},
        {ui->ckxRUsed, QStringLiteral("Rate")}, {ui->ckxINRUsed, QStringLiteral("INR")}};
    for (const auto &resultSwitch : switches)
    {
        bool exists = false;
        for (const auto &result : m_draftProject.results) if (result.type == resultSwitch.second) exists = true;
        resultSwitch.first->setEnabled(writable && exists);
    }
    ui->resultTable->setEnabled(available);
    for (int row = 0; row < ui->resultTable->rowCount(); ++row)
        for (int column = 0; column < ui->resultTable->columnCount(); ++column)
        {
            const bool rowWritable = writable && (!m_originalProfile ||
                (row < m_draftProject.results.size() && editableOriginalResult(m_draftProject.results[row])));
            auto *widget = ui->resultTable->cellWidget(row, column);
            if (auto *edit = qobject_cast<QLineEdit *>(widget))
                edit->setReadOnly(!rowWritable || column == 1 || column == 9);
            else if (widget) widget->setEnabled(rowWritable);
        }
    m_advanced->setWritable(writable);
    ui->btnSampleAdd->setEnabled(available && (m_selectedId > 0 || m_creating));
    ui->btnReagentAdd->setEnabled(available && (m_selectedId > 0 || m_creating));
}
bool ProjectPage::canStartOperation(bool requireProject) const
{
    return m_ready && m_store && !m_busy && !m_refreshRequired && !m_promptOpen &&
           m_formatDialog.isNull() && m_auxiliaryDialog.isNull() &&
           ProjectsStore::canEdit(m_session) && (!requireProject || (m_selectedId > 0 && !m_creating));
}
qint64 ProjectPage::beginOperation(const QString &operation)
{
    m_pendingOperation = operation;
    ++m_request;
    setBusy(true);
    return m_request;
}
void ProjectPage::requestLoad()
{
    if (!m_ready || !m_store) return;
    const qint64 request = beginOperation(QStringLiteral("load"));
    QMetaObject::invokeMethod(m_store, "load", Qt::QueuedConnection, Q_ARG(qint64, request));
}
void ProjectPage::reload()
{
    if (m_formatDialog || m_auxiliaryDialog || m_promptOpen)
    {
        report(tr("请先完成或关闭结果显示配置；未覆盖任何草稿。"));
        return;
    }
    if (!m_ready || !m_store || m_busy)
    {
        return;
    }
    if (m_dirty && !prepareToLeave())
    {
        return;
    }
    requestLoad();
}
void ProjectPage::showProject(qint64 id)
{
    if (m_formatDialog) return; // A late ordinary reply cannot repopulate an open auxiliary editor.
    const bool sameProject = id > 0 && id == m_selectedId && !m_creating;
    const ProjectRecord previousProject = m_draftProject;
    QStringList previousRateLabels;
    for (const auto &rate : previousProject.sampleRates) previousRateLabels.append(rate.sampleRate);
    m_populating = true;
    m_selectedId = id;
    ProjectRecord project = m_creating ? m_draftProject : ProjectRecord();
    for (const ProjectRecord &candidate : m_projects)
    {
        if (!m_creating && candidate.id == id)
        {
            project = candidate;
        }
    }
    m_draftProject = project;
    ui->project_en_name->setText(project.name);
    ui->project_cn_name->setText(project.fullName);
    ui->project_lis_code->setText(project.lisCode);
    ui->manager_id->setText(project.instrumentProjectId);
    ui->change_time->setText(project.changedAt);
    ui->is_used->setCheckState(project.enabled && (id > 0 || m_creating) ? Qt::Checked : Qt::Unchecked);
    const QList<QPair<QLineEdit *, double>> coefficients = {
        {ui->xishu_a, project.algorithm.coefficientA}, {ui->xishu_b, project.algorithm.coefficientB},
        {ui->sample_warmup_length, project.algorithm.coefficientC}};
    for (const auto &coefficient : coefficients)
    {
        coefficient.first->setText(project.algorithm.configured ?
            QString::number(coefficient.second, m_originalProfile ? 'g' : 'f', m_originalProfile ? 17 : 4) : QString());
        coefficient.first->setProperty("originalText", coefficient.first->text());
    }
    ui->auto_apply->setCheckState(project.algorithm.autoApply ? Qt::Checked : Qt::Unchecked);
    const QList<QPair<CusCheckBox *, QString>> switches = {
        {ui->ckxCalibrationUsed, QStringLiteral("Calibration")},
        {ui->ckxRUsed, QStringLiteral("Rate")}, {ui->ckxINRUsed, QStringLiteral("INR")}};
    for (const auto &resultSwitch : switches)
    {
        resultSwitch.first->setCheckState(Qt::Unchecked);
        resultSwitch.first->setText(resultSwitchText(resultSwitch.second));
        resultSwitch.first->setToolTip(tr("当前项目没有此结果类型。新建时选择所需类型，或导入完整配置。"));
        for (const auto &result : project.results)
            if (result.type == resultSwitch.second)
            {
                resultSwitch.first->setCheckState(result.selectionKnown && result.used ? Qt::Checked : Qt::Unchecked);
                resultSwitch.first->setText(resultSwitchText(result.type, !result.selectionKnown));
                resultSwitch.first->setToolTip(result.selectionKnown ? tr("只保存此结果的启用配置；不代表完整算法已执行。") :
                    tr("旧库启用值尚未确认；点击后明确保存启用状态。结果显示状态仍需在结果显示配置中确认。"));
            }
    }
    ui->ldt_methon->clear();
    ui->test_analysis_method->setCurrentIndex(-1);
    ui->d_T->clear();
    if (m_originalProfile)
    {
        // 原界面把collect_rate_ms直接显示在这个只读框，沿用原值，不猜单位换算。
        ui->d_T->setText(project.originalBaseValues.value(QStringLiteral("collect_rate_ms")));
        const QString analysis = project.originalBaseValues.value(QStringLiteral("test_analysis_method"));
        if (!analysis.isEmpty())
        {
            if (ui->test_analysis_method->findText(analysis) < 0) ui->test_analysis_method->addItem(analysis);
            ui->test_analysis_method->setCurrentText(analysis);
        }
        const QString method = project.originalAlgorithmValues.value(QStringLiteral("Coa_endMode"));
        ui->ldt_methon->setText(method == QStringLiteral("0") ? tr("终点法") :
                               method == QStringLiteral("1") ? tr("阈值法") : method);
    }
    QStringList rateLabels;
    QMap<QString, bool> rateOptions;
    QString defaultRate;
    for (const auto &rate : project.sampleRates)
    {
        rateLabels.append(rate.sampleRate);
        rateOptions.insert(rate.sampleRate, rate.sampleOption);
        if (rate.defaultOption) defaultRate = rate.sampleRate;
    }
    // 每次保存后都会读回原库。仅改名称、结果或系数时，高级加样/试剂值并没有变化，
    // 不必销毁并重新创建其大量控件。必须比较完整快照，不能只看当前可见页或行数。
    const bool reuseAdvanced = sameProject && !m_advancedEdited &&
        !m_advancedSourcePath.isEmpty() && m_advancedSourcePath == m_catalogDatabasePath &&
        previousProject.originalRoot == project.originalRoot &&
        previousProject.originalFileName == project.originalFileName &&
        previousProject.advanced.configured == project.advanced.configured &&
        previousProject.advanced.fields == project.advanced.fields &&
        previousProject.advanced.samples == project.advanced.samples &&
        previousProject.advanced.reagents == project.advanced.reagents &&
        previousRateLabels == rateLabels && m_advanced->sampleRateLabels() == rateLabels;
    if (!reuseAdvanced) m_advanced->showAdvanced(project.advanced, rateLabels);
    m_advancedSourcePath = m_catalogDatabasePath;
    // 复用动作控件时仍接受最新选用/默认状态；最终updateActions重新核对可写权限。
    m_advanced->setSampleSelections(rateOptions, defaultRate);
    ui->resultTable->clearContents();
    ui->resultTable->setRowCount(project.results.size());
    for (int row = 0; row < project.results.size(); ++row)
    {
        const ProjectResult &result = project.results[row];
        ui->resultTable->setRowHidden(row, result.selectionKnown && !result.used);
        auto *main = new CusCheckBox(ui->resultTable);
        main->setCheckState(result.mainResult ? Qt::Checked : Qt::Unchecked);
        main->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
        main->setText(tr("选用"));
        main->setFocusPolicy(Qt::NoFocus);
        ui->resultTable->setCellWidget(row, 0, main);
        connect(main, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
                [this, main](bool checked)
                {
                    if (checked)
                    {
                        for (int otherRow = 0; otherRow < ui->resultTable->rowCount(); ++otherRow)
                        {
                            if (m_originalProfile && !editableOriginalResult(m_draftProject.results[otherRow]))
                                continue;
                            auto *other =
                                qobject_cast<CusCheckBox *>(ui->resultTable->cellWidget(otherRow, 0));
                            if (other && other != main)
                            {
                                other->setCheckState(Qt::Unchecked);
                            }
                        }
                    }
                    else
                    {
                        main->setCheckState(Qt::Checked);
                    }
                    markDirty();
                });
        const QStringList values = {result.type,
                                    result.name,
                                    QString::number(result.decimals),
                                    QString::number(result.testLow),
                                    QString::number(result.testHigh),
                                    QString::number(result.normalLow),
                                    QString::number(result.normalHigh),
                                    result.lisCode};
        for (int index = 0; index < values.size(); ++index)
        {
            const int column = index + 1;
            if (column == 3)
            {
                auto *decimals = new QComboBox(ui->resultTable);
                decimals->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                                    QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6")});
                if (m_originalProfile && decimals->findText(values[index]) < 0)
                    decimals->addItem(values[index]);
                decimals->setCurrentText(values[index]);
                decimals->setProperty("originalText", values[index]);
                decimals->setView(new QListView);
                decimals->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
                connect(decimals, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this](int) { markDirty(); });
                ui->resultTable->setCellWidget(row, column, decimals);
                continue;
            }
            auto *edit = new QLineEdit(values[index], ui->resultTable);
            edit->setProperty("originalText", values[index]);
            edit->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
            edit->setAlignment(Qt::AlignCenter);
            edit->setStyleSheet(QStringLiteral("border:1px solid #E4E8EB;border-radius:5px;padding:2px "
                                               "4px;background-color:transparent;color:#444444;"));
            edit->setReadOnly(column == 1);
            edit->setContextMenuPolicy(Qt::NoContextMenu);
            if (column == 2)
            {
                edit->setMaxLength(16);
            }
            if (column == 8)
            {
                edit->setMaxLength(8);
            }
            if (column >= 4 && column <= 7)
            {
                auto *validator = new QDoubleValidator(0, 999, 3, edit);
                validator->setLocale(projectNumberLocale());
                validator->setNotation(QDoubleValidator::StandardNotation);
                edit->setValidator(validator);
            }
            connect(edit, &QLineEdit::textEdited, this, &ProjectPage::markDirty);
            ui->resultTable->setCellWidget(row, column, edit);
        }
        if (result.type == QStringLiteral("Calibration") || result.type == QStringLiteral("Rate"))
        {
            auto *unit = new QComboBox(ui->resultTable);
            unit->setView(new QListView);
            for (const ProjectUnit &value : m_units)
            {
                unit->addItem(value.name, value.ratio);
            }
            if (unit->findText(result.unit) < 0) unit->addItem(result.unit);
            unit->setCurrentText(result.unit);
            unit->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
            ui->resultTable->setCellWidget(row, 9, unit);
            connect(unit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { markDirty(); });
        }
        else
        {
            auto *unit = new QLineEdit(result.unit, ui->resultTable);
            unit->setReadOnly(true);
            unit->setAlignment(Qt::AlignCenter);
            unit->setStyleSheet(QStringLiteral("border:1px solid #E4E8EB;border-radius:5px;padding:2px "
                                               "4px;background-color:transparent;color:#444444;"));
            ui->resultTable->setCellWidget(row, 9, unit);
        }
        ui->resultTable->setRowHeight(row, 41);
    }
    QSignalBlocker selectionBlocker(ui->lstProject);
    if (m_creating) ui->lstProject->clearSelection();
    for (int row = 0; !m_creating && row < ui->lstProject->rowCount(); ++row)
    {
        if (ui->lstProject->item(row, 0)->data(Qt::UserRole).toLongLong() == id)
        {
            ui->lstProject->selectRow(row);
        }
    }
    m_dirty = false;
    m_advancedEdited = false;
    m_populating = false;
    updateActions();
}
ProjectRecord ProjectPage::editedProject() const
{
    ProjectRecord project = m_draftProject;
    project.name = ui->project_en_name->text();
    project.fullName = ui->project_cn_name->text();
    project.lisCode = ui->project_lis_code->text();
    project.enabled = ui->is_used->isChecked();
    // 原库可能有未选用的旧默认及未映射列。未编辑高级区时直接保留其原快照，
    // 不能因另改一个名称或系数，就用控件的格式化值重写全部动作参数。
    if (!m_originalProfile || m_advancedEdited)
    {
        project.advanced = m_advanced->advanced();
        project.sampleRates.clear();
        const auto rateLabels = m_advanced->sampleRateLabels();
        const auto rateOptions = m_advanced->sampleSelections();
        for (int index = 0; index < rateLabels.size(); ++index)
            project.sampleRates.append({rateLabels[index], rateOptions.value(rateLabels[index]),
                rateLabels[index] == m_advanced->defaultSampleLabel(), index + 1});
    }
    project.algorithm.configured = !ui->xishu_a->text().isEmpty() || !ui->xishu_b->text().isEmpty() ||
                                   !ui->sample_warmup_length->text().isEmpty();
    // 原库允许存在比输入框规则更精细的旧系数。未改的值沿用读入快照，
    // 避免仅改项目名称时，把显示转换或小数截断一起写回原DB。
    const auto coefficientValue = [this](QLineEdit *edit, double stored)
    {
        return m_originalProfile && edit->text() == edit->property("originalText").toString() ? stored
                                                                                           : edit->text().toDouble();
    };
    project.algorithm.coefficientA = coefficientValue(ui->xishu_a, project.algorithm.coefficientA);
    project.algorithm.coefficientB = coefficientValue(ui->xishu_b, project.algorithm.coefficientB);
    project.algorithm.coefficientC = coefficientValue(ui->sample_warmup_length, project.algorithm.coefficientC);
    project.algorithm.autoApply = ui->auto_apply->isChecked();
    for (int row = 0; row < project.results.size(); ++row)
    {
        ProjectResult &result = project.results[row];
        if (m_originalProfile && !editableOriginalResult(result)) continue;
        auto text = [this, row](int column)
        { return qobject_cast<QLineEdit *>(ui->resultTable->cellWidget(row, column))->text(); };
        result.mainResult = qobject_cast<CusCheckBox *>(ui->resultTable->cellWidget(row, 0))->isChecked();
        result.name = text(2);
        result.decimals =
            qobject_cast<QComboBox *>(ui->resultTable->cellWidget(row, 3))->currentText().toInt();
        const auto numericValue = [this, row, &text](int column, double stored)
        {
            auto *edit = qobject_cast<QLineEdit *>(ui->resultTable->cellWidget(row, column));
            return m_originalProfile && edit->text() == edit->property("originalText").toString() ? stored
                                                                                               : text(column).toDouble();
        };
        result.testLow = numericValue(4, result.testLow);
        result.testHigh = numericValue(5, result.testHigh);
        result.normalLow = numericValue(6, result.normalLow);
        result.normalHigh = numericValue(7, result.normalHigh);
        result.lisCode = text(8);
        if (auto *unit = qobject_cast<QComboBox *>(ui->resultTable->cellWidget(row, 9)))
        {
            result.unit = unit->currentText();
        }
    }
    return project;
}
void ProjectPage::markDirty()
{
    if (!m_populating && !m_busy && ProjectsStore::canEdit(m_session))
    {
        m_dirty = true;
        updateActions();
    }
}
void ProjectPage::cancel()
{
    if (!m_ready || m_busy || m_refreshRequired || m_formatDialog || m_auxiliaryDialog || m_promptOpen) return;
    m_advancedSourcePath.clear(); // 取消必须从提交快照重建，不能保留未提交的高级输入。
    if (m_creating)
    {
        m_creating = false;
        m_selectedId = m_beforeCreateId;
    }
    showProject(m_selectedId);
    report(tr("已恢复已提交配置；未保存的修改未写入。"));
}
void ProjectPage::updateResultSelection(const QString &type, bool used)
{
    if (m_populating || !canStartOperation() || (m_selectedId <= 0 && !m_creating)) return;
    for (int row = 0; row < m_draftProject.results.size(); ++row)
    {
        auto &result = m_draftProject.results[row];
        if (result.type != type) continue;
        if (!result.selectionKnown)
        {
            QMessageBox::StandardButton answer;
            {
                QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
                updateActions();
                answer = QMessageBox::question(this, tr("确认结果配置"),
                    tr("旧库没有确认过此结果的显示状态。是否同时将 %1 配置为显示？\n是：显示；否：不显示；取消：保留未知。仅保存配置，未执行计算。").arg(type),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Cancel);
            }
            updateActions();
            if (answer == QMessageBox::Cancel || !canStartOperation())
            {
                CusCheckBox *checkbox = type == QStringLiteral("Calibration") ? ui->ckxCalibrationUsed :
                                       type == QStringLiteral("Rate") ? ui->ckxRUsed : ui->ckxINRUsed;
                checkbox->setCheckState(Qt::Unchecked);
                return;
            }
            result.selectionKnown = true;
            result.sourceResultId = sourceResultId(type);
            result.display = answer == QMessageBox::Yes;
        }
        result.used = used;
        CusCheckBox *checkbox = type == QStringLiteral("Calibration") ? ui->ckxCalibrationUsed :
                               type == QStringLiteral("Rate") ? ui->ckxRUsed : ui->ckxINRUsed;
        checkbox->setText(resultSwitchText(type));
        checkbox->setToolTip(tr("只保存此结果的启用配置；不代表完整算法已执行。"));
        ui->resultTable->setRowHidden(row, !used);
        markDirty();
        return;
    }
}
void ProjectPage::openProjectMenu(const QPoint &position)
{
    if (!m_store || m_busy || m_formatDialog || m_auxiliaryDialog || m_promptOpen) return;
    const bool writable = canStartOperation();
    QMenu menu(this);
    auto *create = menu.addAction(tr("新建项目…"));
    create->setObjectName(QStringLiteral("createProject"));
    create->setEnabled(writable && !m_originalProfile);
    if (m_originalProfile) create->setToolTip(tr("原库新增请复制完整项目或导入原DB，保留全部原表和参数。"));
    auto *import = menu.addAction(tr("导入项目配置…"));
    import->setObjectName(QStringLiteral("importProjectConfiguration"));
    import->setEnabled(writable && !m_originalProfile);
    auto *units = menu.addAction(tr("单位维护…"));
    units->setObjectName(QStringLiteral("manageProjectUnits"));
    units->setEnabled(m_ready && !m_refreshRequired);
    menu.addSeparator();
    auto *openOriginal = menu.addAction(tr("加载原配置目录…"));
    openOriginal->setObjectName(QStringLiteral("openOriginalProjectDirectory"));
    openOriginal->setEnabled(ProjectsStore::canEdit(m_session));
    auto *importOriginal = menu.addAction(tr("导入原项目DB…"));
    importOriginal->setObjectName(QStringLiteral("importOriginalProjectDatabase"));
    importOriginal->setEnabled(writable && m_originalProfile);
    auto *exportOriginal = menu.addAction(tr("导出当前原项目DB及关联资料…"));
    exportOriginal->setObjectName(QStringLiteral("exportOriginalProjectDatabase"));
    exportOriginal->setEnabled(writable && m_originalProfile && m_selectedId > 0 && !m_creating);
    auto *exportAll = menu.addAction(tr("导出整组原项目配置…"));
    exportAll->setObjectName(QStringLiteral("exportOriginalProjectConfiguration"));
    exportAll->setEnabled(writable && m_originalProfile);
    auto *parameters = menu.addAction(tr("查看完整原项目参数…"));
    parameters->setObjectName(QStringLiteral("viewOriginalProjectParameters"));
    parameters->setEnabled(m_ready && m_originalProfile && m_selectedId > 0 && !m_refreshRequired);
    menu.addSeparator();
    auto *refresh = menu.addAction(tr("重新读取"));
    refresh->setObjectName(QStringLiteral("reloadProjects"));
    refresh->setEnabled(m_ready);
    QAction *chosen;
    {
        QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
        updateActions();
        chosen = menu.exec(position);
    }
    updateActions();
    if (chosen == create) createProject();
    else if (chosen == import) importProjects();
    else if (chosen == units) manageUnits();
    else if (chosen == openOriginal) openOriginalDirectory();
    else if (chosen == importOriginal) importOriginalProject();
    else if (chosen == exportOriginal) exportOriginalProjects(false);
    else if (chosen == exportAll) exportOriginalProjects(true);
    else if (chosen == parameters) openOriginalParameters();
    else if (chosen == refresh) reload();
}
void ProjectPage::createProject()
{
    if (!canStartOperation() || !prepareToLeave()) return;
    if (m_originalProfile)
    {
        report(tr("原库新增请使用复制或导入原项目DB，以保留完整原表和参数。"));
        return;
    }
    if (m_units.isEmpty())
    {
        report(tr("请先在项目管理的单位维护中新增有效单位，再新建项目。"));
        QMessageBox::information(this, tr("新建项目"), property("lastStatus").toString());
        manageUnits();
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("createProjectDialog"));
    dialog.setWindowTitle(tr("新建项目"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("newProjectName"));
    name->setMaxLength(16);
    auto *fullName = new QLineEdit(&dialog);
    fullName->setObjectName(QStringLiteral("newProjectFullName"));
    fullName->setMaxLength(16);
    auto *instrumentId = new QLineEdit(&dialog);
    instrumentId->setObjectName(QStringLiteral("newInstrumentProjectId"));
    instrumentId->setMaxLength(64);
    form->addRow(tr("项目名称："), name);
    form->addRow(tr("项目全称："), fullName);
    form->addRow(tr("仪器项目编号："), instrumentId);
    layout->addLayout(form);
    auto *notice = new QLabel(tr("按已确认资料填写编号和各结果单位。进入编辑后仍须点击原页面的保存。\n这里只维护配置，不发送检测或仪器动作。"), &dialog);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *results = new QTableWidget(4, 4, &dialog);
    results->setObjectName(QStringLiteral("newProjectResults"));
    results->setHorizontalHeaderLabels({tr("结果类型"), tr("单位"), tr("启用"), tr("显示")});
    results->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    const QStringList types = {QStringLiteral("Original"), QStringLiteral("Calibration"),
                              QStringLiteral("Rate"), QStringLiteral("INR")};
    for (int row = 0; row < types.size(); ++row)
    {
        results->setItem(row, 0, new QTableWidgetItem(types[row]));
        auto *unit = new QComboBox(results);
        for (const auto &value : m_units) unit->addItem(value.name);
        unit->setCurrentIndex(-1);
        results->setCellWidget(row, 1, unit);
        for (int column = 2; column < 4; ++column)
        {
            auto *check = new QCheckBox(results);
            check->setChecked(row == 0);
            results->setCellWidget(row, column, check);
        }
    }
    layout->addWidget(results);
    auto *message = new QLabel(&dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("进入编辑"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("editNewProject"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    ProjectRecord draft;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&, this]()
    {
        if (!m_ready || !ProjectsStore::canEdit(m_session))
        { message->setText(tr("当前账户或项目库不可编辑，请关闭后重新登录或读取。")); return; }
        draft = ProjectRecord();
        draft.name = name->text();
        draft.fullName = fullName->text();
        draft.instrumentProjectId = instrumentId->text();
        if (draft.instrumentProjectId.isEmpty() || draft.instrumentProjectId != draft.instrumentProjectId.trimmed())
        { message->setText(tr("请明确填写仪器项目编号，不能留空或带首尾空格。")); return; }
        for (int row = 0; row < types.size(); ++row)
        {
            ProjectResult result;
            result.type = types[row];
            result.name = types[row];
            result.unit = qobject_cast<QComboBox *>(results->cellWidget(row, 1))->currentText();
            result.mainResult = row == 0;
            result.selectionKnown = true;
            result.sourceResultId = row + 1;
            result.used = qobject_cast<QCheckBox *>(results->cellWidget(row, 2))->isChecked();
            result.display = qobject_cast<QCheckBox *>(results->cellWidget(row, 3))->isChecked();
            draft.results.append(result);
        }
        const QString error = ProjectsStore::validate(draft, m_units);
        if (!error.isEmpty()) { message->setText(error); return; }
        dialog.accept();
    });
    m_auxiliaryDialog = &dialog;
    updateActions();
    const int answer = dialog.exec();
    m_auxiliaryDialog.clear();
    updateActions();
    if (answer != QDialog::Accepted || !canStartOperation()) return;
    m_beforeCreateId = m_selectedId;
    m_creating = true;
    m_draftProject = draft;
    showProject(0);
    m_dirty = true;
    ui->stkPanel->setCurrentIndex(0);
    ui->btnBaseInfo->setChecked(true);
    report(tr("新项目尚未写入。请核对结果范围、算法参数与加样方案，点击保存；取消可放弃。"));
    updateActions();
}
void ProjectPage::importProjects()
{
    if (!canStartOperation() || !prepareToLeave()) return;
    if (m_originalProfile) { importOriginalProject(); return; }
    QFileDialog dialog(this, tr("导入已确认的项目配置"));
    dialog.setObjectName(QStringLiteral("projectImportFileDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("项目配置 (*.json)"));
    m_auxiliaryDialog = &dialog;
    updateActions();
    const int answer = dialog.exec();
    const QString path = answer == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
    m_auxiliaryDialog.clear();
    updateActions();
    if (path.isEmpty() || !canStartOperation()) return;
    const qint64 request = beginOperation(QStringLiteral("importFile"));
    QMetaObject::invokeMethod(m_store, "importFile", Qt::QueuedConnection, Q_ARG(qint64, request), Q_ARG(QString, path));
}

void ProjectPage::openOriginalDirectory()
{
    if (!m_store || m_busy || m_promptOpen || m_auxiliaryDialog || m_formatDialog ||
        !ProjectsStore::canEdit(m_session)) return;
    if (m_ready && !prepareToLeave()) return;
    QFileDialog dialog(this, tr("加载原配置工作目录（bin对应根目录）"));
    dialog.setObjectName(QStringLiteral("originalProjectDirectoryDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    if (!m_originalRoot.isEmpty()) dialog.setDirectory(m_originalRoot);
    m_auxiliaryDialog = &dialog;
    updateActions();
    const int answer = dialog.exec();
    const QString root = answer == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
    m_auxiliaryDialog.clear();
    updateActions();
    if (root.isEmpty() || !m_store || m_busy || !ProjectsStore::canEdit(m_session)) return;
    const qint64 request = beginOperation(QStringLiteral("openOriginalDirectory"));
    report(tr("正在后台检查原配置目录；保持db/systemInfo、db/userSet、project和result原组织。"));
    QMetaObject::invokeMethod(m_store, "openOriginalDirectory", Qt::QueuedConnection,
                              Q_ARG(qint64, request), Q_ARG(QString, root));
}

void ProjectPage::importOriginalProject()
{
    if (!m_originalProfile || !canStartOperation() || !prepareToLeave()) return;
    QFileDialog dialog(this, tr("导入原项目DB（保留project目录及原结构）"));
    dialog.setObjectName(QStringLiteral("originalProjectImportDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(tr("原项目数据库 (*.db *.DB)"));
    dialog.setDirectory(QDir(m_originalRoot).filePath(QStringLiteral("project")));
    m_auxiliaryDialog = &dialog;
    updateActions();
    const int answer = dialog.exec();
    const QString file = answer == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
    m_auxiliaryDialog.clear();
    updateActions();
    if (file.isEmpty() || !canStartOperation()) return;
    const qint64 request = beginOperation(QStringLiteral("importOriginalFile"));
    report(tr("正在检查并导入原项目DB；同名文件不会被覆盖，不转换成新库。"));
    QMetaObject::invokeMethod(m_store, "importOriginalFile", Qt::QueuedConnection,
                              Q_ARG(qint64, request), Q_ARG(QString, file));
}

void ProjectPage::exportOriginalProjects(bool allProjects)
{
    if (!m_originalProfile || !canStartOperation(!allProjects)) return;
    const qint64 projectId = allProjects ? 0 : m_selectedId;
    QFileDialog dialog(this, tr("选择新的原配置导出目录"));
    dialog.setObjectName(QStringLiteral("originalProjectExportDialog"));
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(tr("原配置目录 (*)"));
    dialog.setLabelText(QFileDialog::FileName, tr("新目录名称："));
    dialog.setLabelText(QFileDialog::Accept, tr("导出到此新目录"));
    dialog.selectFile(allProjects ? QStringLiteral("SN100_project_configuration") : QStringLiteral("SN100_project_database"));
    m_auxiliaryDialog = &dialog;
    updateActions();
    const int answer = dialog.exec();
    const QString directory = answer == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
    m_auxiliaryDialog.clear();
    updateActions();
    if (directory.isEmpty() || !canStartOperation(!allProjects)) return;
    const qint64 request = beginOperation(QStringLiteral("exportOriginalBundle"));
    report(tr("正在后台导出已提交的DB副本；未保存草稿保留在页面，导出保持原相对目录。"));
    QMetaObject::invokeMethod(m_store, "exportOriginalBundle", Qt::QueuedConnection,
                              Q_ARG(qint64, request), Q_ARG(qint64, projectId), Q_ARG(QString, directory));
}

void ProjectPage::openOriginalParameters()
{
    if (!m_originalProfile || !m_ready || m_busy || m_refreshRequired || m_promptOpen ||
        m_auxiliaryDialog || m_formatDialog || m_selectedId <= 0) return;
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("originalProjectParametersDialog"));
    dialog.setWindowTitle(tr("完整原项目参数：%1").arg(m_draftProject.originalFileName));
    dialog.resize(1000, 700);
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(tr("这里展示已读取的完整原值。常用参数仍在原页面编辑并保存；未确认编辑规则的参数只读保留，不能把查看当成算法已经运行。"), &dialog);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *filter = new QLineEdit(&dialog);
    filter->setObjectName(QStringLiteral("originalParameterFilter"));
    filter->setPlaceholderText(tr("按原字段名称或值筛选"));
    layout->addWidget(filter);
    auto *tabs = new QTabWidget(&dialog);
    layout->addWidget(tabs);
    QList<QTableWidget *> tables;
    const auto addParameters = [&](const QMap<QString, QString> &values, const QString &name,
                                   const QString &objectName)
    {
        auto *table = new QTableWidget(values.size(), 2, tabs);
        table->setObjectName(objectName);
        table->setHorizontalHeaderLabels({tr("原字段"), tr("已读取的原值")});
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        int row = 0;
        for (auto field = values.cbegin(); field != values.cend(); ++field, ++row)
        {
            table->setItem(row, 0, new QTableWidgetItem(field.key()));
            table->setItem(row, 1, new QTableWidgetItem(field.value()));
        }
        tabs->addTab(table, tr("%1（%2项）").arg(name).arg(values.size()));
        tables.append(table);
    };
    addParameters(m_draftProject.originalBaseValues, tr("项目基础参数"), QStringLiteral("originalProjectBaseTable"));
    addParameters(m_draftProject.originalAlgorithmValues, tr("算法参数AlgParam"), QStringLiteral("originalProjectAlgorithmTable"));
    connect(filter, &QLineEdit::textChanged, &dialog, [tables](const QString &text)
    {
        for (auto *table : tables)
            for (int row = 0; row < table->rowCount(); ++row)
                table->setRowHidden(row, !table->item(row, 0)->text().contains(text, Qt::CaseInsensitive) &&
                                         !table->item(row, 1)->text().contains(text, Qt::CaseInsensitive));
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    m_auxiliaryDialog = &dialog;
    updateActions();
    dialog.exec();
    m_auxiliaryDialog.clear();
    updateActions();
}
void ProjectPage::manageUnits()
{
    if (!m_ready || !m_store || m_busy || m_refreshRequired || m_formatDialog || m_auxiliaryDialog ||
        m_promptOpen || !prepareToLeave()) return;
    ProjectUnitDialog dialog(this);
    dialog.setObjectName(QStringLiteral("projectUnitDialog"));
    dialog.setWindowTitle(tr("单位维护"));
    dialog.resize(620, 560);
    dialog.mayClose = [this]()
    {
        if (!m_busy) return true;
        report(tr("单位操作尚未收到最终结果，请稍后关闭。"));
        return false;
    };
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(m_originalProfile ?
        tr("直接维护原option_Unit.db；倍率须为正数，类型沿用原库分类。被项目或业务记录引用的单位不能修改或删除。") :
        tr("倍率用于已确认的结果显示换算，须为正整数。被项目或业务记录引用的单位不能修改或删除。"), &dialog);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *table = new QTableWidget(0, m_originalProfile ? 3 : 2, &dialog);
    table->setObjectName(QStringLiteral("projectUnitsTable"));
    table->setHorizontalHeaderLabels(m_originalProfile ? QStringList{tr("单位"), tr("倍率"), tr("原类型")} :
                                                         QStringList{tr("单位"), tr("倍率")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table);
    auto *form = new QFormLayout;
    auto *name = new QLineEdit(&dialog);
    name->setObjectName(QStringLiteral("projectUnitName"));
    name->setMaxLength(64);
    auto *ratio = new QSpinBox(&dialog);
    ratio->setObjectName(QStringLiteral("projectUnitRatio"));
    ratio->setRange(1, 2147483647);
    auto *originalRatio = new QLineEdit(&dialog);
    originalRatio->setObjectName(QStringLiteral("projectOriginalUnitRatio"));
    originalRatio->setText(QStringLiteral("1"));
    auto *originalType = new QComboBox(&dialog);
    originalType->setObjectName(QStringLiteral("projectOriginalUnitType"));
    for (const ProjectUnit &unit : m_units)
        if (originalType->findText(unit.originalType) < 0) originalType->addItem(unit.originalType);
    originalType->setCurrentIndex(-1);
    originalRatio->setVisible(m_originalProfile);
    originalType->setVisible(m_originalProfile);
    ratio->setVisible(!m_originalProfile);
    form->addRow(tr("单位名称："), name);
    form->addRow(tr("倍率："), m_originalProfile ? static_cast<QWidget *>(originalRatio) : static_cast<QWidget *>(ratio));
    if (m_originalProfile) form->addRow(tr("原单位类型："), originalType);
    layout->addLayout(form);
    auto *operations = new QDialogButtonBox(&dialog);
    auto *add = operations->addButton(tr("新增"), QDialogButtonBox::ActionRole);
    add->setObjectName(QStringLiteral("addProjectUnit"));
    auto *update = operations->addButton(tr("保存所选单位"), QDialogButtonBox::ActionRole);
    update->setObjectName(QStringLiteral("updateProjectUnit"));
    auto *remove = operations->addButton(tr("删除所选单位"), QDialogButtonBox::ActionRole);
    remove->setObjectName(QStringLiteral("removeProjectUnit"));
    auto *refresh = operations->addButton(tr("重新读取"), QDialogButtonBox::ActionRole);
    refresh->setObjectName(QStringLiteral("reloadProjectUnits"));
    auto *close = operations->addButton(QDialogButtonBox::Close);
    layout->addWidget(operations);
    auto *message = new QLabel(&dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    const auto writable = [this]()
    { return m_ready && m_store && !m_busy && !m_refreshRequired && !m_promptOpen && ProjectsStore::canEdit(m_session); };
    const auto updateControls = [=]()
    {
        add->setEnabled(writable());
        update->setEnabled(writable() && table->currentRow() >= 0);
        remove->setEnabled(writable() && table->currentRow() >= 0);
        name->setReadOnly(!writable());
        ratio->setEnabled(writable());
        originalRatio->setReadOnly(!writable());
        originalType->setEnabled(writable());
        table->setEnabled(!m_busy);
        close->setEnabled(!m_busy);
        refresh->setEnabled(m_ready && m_store && !m_busy);
    };
    const auto populate = [=]()
    {
        QString selected;
        if (table->currentRow() >= 0 && table->item(table->currentRow(), 0))
            selected = table->item(table->currentRow(), 0)->text();
        QSignalBlocker blocker(table);
        table->setRowCount(m_units.size());
        for (int row = 0; row < m_units.size(); ++row)
        {
            table->setItem(row, 0, new QTableWidgetItem(m_units[row].name));
            table->setItem(row, 1, new QTableWidgetItem(QString::number(m_units[row].ratio, m_originalProfile ? 'g' : 'f',
                                                                                     m_originalProfile ? 17 : 0)));
            if (m_originalProfile) table->setItem(row, 2, new QTableWidgetItem(m_units[row].originalType));
            if (m_units[row].name == selected) table->selectRow(row);
        }
        updateControls();
    };
    connect(this, &ProjectPage::snapshotChanged, &dialog, [=](ProjectRecords, ProjectUnits) { populate(); });
    connect(this, &ProjectPage::busyChanged, &dialog, [=](bool) { updateControls(); });
    connect(this, &ProjectPage::statusChanged, message, &QLabel::setText);
    connect(table, &QTableWidget::itemSelectionChanged, &dialog, [=]()
    {
        if (table->currentRow() >= 0 && table->item(table->currentRow(), 0))
        {
            name->setText(table->item(table->currentRow(), 0)->text());
            ratio->setValue(table->item(table->currentRow(), 1)->text().toInt());
            originalRatio->setText(table->item(table->currentRow(), 1)->text());
            if (m_originalProfile) originalType->setCurrentText(table->item(table->currentRow(), 2)->text());
        }
        updateControls();
    });
    connect(add, &QPushButton::clicked, &dialog, [=]()
    {
        if (!writable()) return;
        bool validRatio = true;
        const double value = m_originalProfile ? originalRatio->text().toDouble(&validRatio) : double(ratio->value());
        if (!validRatio || (m_originalProfile && originalType->currentIndex() < 0))
        { message->setText(tr("请填写有效倍率，并明确选择原单位类型。")); return; }
        const ProjectUnit unit{name->text(), value, m_originalProfile ? originalType->currentText() : QString(), QString()};
        const qint64 request = beginOperation(QStringLiteral("addUnit"));
        QMetaObject::invokeMethod(m_store, "addUnit", Qt::QueuedConnection, Q_ARG(qint64, request), Q_ARG(ProjectUnit, unit));
    });
    connect(update, &QPushButton::clicked, &dialog, [=]()
    {
        if (!writable() || table->currentRow() < 0) return;
        const QString originalName = table->item(table->currentRow(), 0)->text();
        bool validRatio = true;
        const double value = m_originalProfile ? originalRatio->text().toDouble(&validRatio) : double(ratio->value());
        if (!validRatio || (m_originalProfile && originalType->currentIndex() < 0))
        { message->setText(tr("请填写有效倍率，并明确选择原单位类型。")); return; }
        ProjectUnit unit{name->text(), value, m_originalProfile ? originalType->currentText() : QString(), QString()};
        if (m_originalProfile)
            for (const auto &snapshot : m_units)
                if (snapshot.name == originalName) unit.originalFingerprint = snapshot.originalFingerprint;
        const qint64 request = beginOperation(QStringLiteral("updateUnit"));
        QMetaObject::invokeMethod(m_store, "updateUnit", Qt::QueuedConnection, Q_ARG(qint64, request),
                                  Q_ARG(QString, originalName), Q_ARG(ProjectUnit, unit));
    });
    connect(remove, &QPushButton::clicked, &dialog, [=, &dialog]()
    {
        if (!writable() || table->currentRow() < 0) return;
        const QString originalName = table->item(table->currentRow(), 0)->text();
        QMessageBox::StandardButton answer;
        {
            QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
            updateControls();
            answer = QMessageBox::question(&dialog, tr("删除单位"), tr("是否删除单位“%1”？引用中的单位会被拒绝。").arg(originalName),
                                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        }
        updateControls();
        if (answer != QMessageBox::Yes || !writable()) return;
        const QString operation = m_originalProfile ? QStringLiteral("removeOriginalUnit") : QStringLiteral("removeUnit");
        const qint64 request = beginOperation(operation);
        if (m_originalProfile)
        {
            QString fingerprint;
            for (const auto &snapshot : m_units)
                if (snapshot.name == originalName) fingerprint = snapshot.originalFingerprint;
            QMetaObject::invokeMethod(m_store, "removeOriginalUnit", Qt::QueuedConnection, Q_ARG(qint64, request),
                                      Q_ARG(QString, originalName), Q_ARG(QString, fingerprint));
        }
        else
            QMetaObject::invokeMethod(m_store, "removeUnit", Qt::QueuedConnection, Q_ARG(qint64, request), Q_ARG(QString, originalName));
    });
    connect(refresh, &QPushButton::clicked, &dialog, [this]()
    { if (m_ready && m_store && !m_busy) requestLoad(); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    m_auxiliaryDialog = &dialog;
    populate();
    updateActions();
    dialog.exec();
    m_auxiliaryDialog.clear();
    updateActions();
}
void ProjectPage::save()
{
    if (!canStartOperation() || (m_selectedId <= 0 && !m_creating))
    {
        return;
    }
    if (!m_dirty && !m_creating)
    {
        report(tr("当前没有新的修改，未重复写入数据库。"));
        return;
    }
    // 输入框允许暂留空白草稿，提交前拒绝不完整值，避免转换时静默写成0。
    for (int row = 0; row < ui->resultTable->rowCount(); ++row)
        for (int column = 4; column <= 7; ++column)
        {
            auto *edit = qobject_cast<QLineEdit *>(ui->resultTable->cellWidget(row, column));
            if (m_originalProfile && edit && edit->text() == edit->property("originalText").toString()) continue;
            if (!edit || !edit->hasAcceptableInput())
            {
                report(tr("第%1行结果范围输入无效或为空；若该结果已隐藏，请先启用后修正。").arg(row + 1));
                QMessageBox::warning(this, tr("项目设置"), property("lastStatus").toString());
                return;
            }
        }
    const bool hasCoefficient = !ui->xishu_a->text().isEmpty() || !ui->xishu_b->text().isEmpty() ||
                                !ui->sample_warmup_length->text().isEmpty();
    for (QLineEdit *edit : {ui->xishu_a, ui->xishu_b, ui->sample_warmup_length})
        if (hasCoefficient && !(m_originalProfile && edit->text() == edit->property("originalText").toString()) &&
            !edit->hasAcceptableInput())
        {
            report(tr("系数A、B、C须一起填写，范围-9999至9999，最多4位小数；全部留空表示未配置。"));
            QMessageBox::warning(this, tr("项目设置"), property("lastStatus").toString());
            return;
        }
    const QStringList rateLabels = m_advanced->sampleRateLabels();
    const QMap<QString, bool> rateOptions = m_advanced->sampleSelections();
    bool rateSelectionsChanged = !m_originalProfile || rateLabels.size() != m_draftProject.sampleRates.size();
    for (int index = 0; !rateSelectionsChanged && index < rateLabels.size(); ++index)
    {
        const ProjectSampleRate &old = m_draftProject.sampleRates[index];
        rateSelectionsChanged = rateLabels[index] != old.sampleRate ||
            rateOptions.value(rateLabels[index]) != old.sampleOption ||
            (rateLabels[index] == m_advanced->defaultSampleLabel()) != old.defaultOption;
    }
    const QString rateError = !rateSelectionsChanged || rateLabels.isEmpty() ? QString() :
                              m_advanced->validateSampleSelections();
    if (!rateError.isEmpty())
    {
        report(rateError);
        QMessageBox::warning(this, tr("项目设置"), rateError);
        return;
    }
    const ProjectRecord project = editedProject();
    const QString message = ProjectsStore::validate(project, m_units);
    if (!message.isEmpty())
    {
        report(message);
        QMessageBox::warning(this, tr("项目设置"), message);
        return;
    }
    const QString operation = m_creating ? QStringLiteral("create") : QStringLiteral("save");
    const qint64 request = beginOperation(operation);
    QMetaObject::invokeMethod(m_store, m_creating ? "create" : "save", Qt::QueuedConnection, Q_ARG(qint64, request),
                              Q_ARG(ProjectRecord, project));
}
void ProjectPage::copy()
{
    if (!canStartOperation(true) || !prepareToLeave())
    {
        return;
    }
    bool accepted = false;
    QString name;
    {
        QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
        updateActions();
        name = QInputDialog::getText(this, tr("复制项目"), tr("新项目名称（1至16字符）："), QLineEdit::Normal,
                                    ui->project_en_name->text().left(11) + QStringLiteral("_copy"), &accepted);
    }
    updateActions();
    if (!accepted || !canStartOperation(true))
    {
        return;
    }
    const qint64 request = beginOperation(QStringLiteral("copy"));
    QMetaObject::invokeMethod(m_store, "copy", Qt::QueuedConnection, Q_ARG(qint64, request),
                              Q_ARG(qint64, m_selectedId), Q_ARG(QString, name));
}
void ProjectPage::remove()
{
    if (!canStartOperation(true) || !prepareToLeave())
    {
        return;
    }
    if (m_draftProject.fixed)
    {
        report(tr("内置固定项目不能删除。"));
        return;
    }
    QMessageBox::StandardButton answer;
    {
        QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
        updateActions();
        answer = QMessageBox::question(this, tr("删除项目"), tr("是否删除当前项目？已被试剂、样本或其他记录引用的项目不能删除。"),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    }
    updateActions();
    if (answer != QMessageBox::Yes || !canStartOperation(true))
    {
        return;
    }
    const qint64 request = beginOperation(QStringLiteral("remove"));
    QMetaObject::invokeMethod(m_store, "remove", Qt::QueuedConnection, Q_ARG(qint64, request),
                              Q_ARG(qint64, m_selectedId));
}
bool ProjectPage::prepareToLeave()
{
    if (m_auxiliaryDialog || m_promptOpen)
    {
        if (m_auxiliaryDialog) m_auxiliaryDialog->raise();
        report(tr("请先完成或关闭当前项目弹窗。"));
        return false;
    }
    if (m_formatDialog)
    {
        m_formatDialog->reject();
        if (m_formatDialog && m_formatDialog->isVisible()) return false;
    }
    if (m_busy)
    {
        report(tr("项目操作尚未完成，请稍后。"));
        return false;
    }
    if (!m_dirty)
    {
        return true;
    }
    int answer;
    {
        QScopedValueRollback<bool> promptGuard(m_promptOpen, true);
        updateActions();
        QMessageBox dialog(QMessageBox::Question, tr("项目设置"), tr("当前项目有未保存修改，是否放弃？"),
                           QMessageBox::Discard | QMessageBox::Cancel, this);
        dialog.setObjectName(QStringLiteral("projectDiscardDialog"));
        dialog.setDefaultButton(QMessageBox::Cancel);
        answer = dialog.exec();
    }
    updateActions();
    if (answer != QMessageBox::Discard)
    {
        return false;
    }
    cancel();
    return true;
}
void ProjectPage::closeEvent(QCloseEvent *event)
{
    if (prepareToLeave())
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}
void ProjectPage::report(const QString &message)
{
    setProperty("lastStatus", message);
    emit statusChanged(message);
}

bool ProjectPage::hasUnsavedChanges() const
{
    return m_dirty || m_auxiliaryDialog || m_promptOpen || (m_formatDialog && m_formatDialog->hasDraft());
}
bool ProjectPage::bindCurrentDisplayState(HistoryCurrentDisplayState *state)
{
    if (!state)
    {
        report(tr("当前结果显示缓存不存在，未建立绑定。"));
        return false;
    }
    if (state->thread() != thread())
    {
        report(tr("当前显示缓存须在界面线程绑定，未建立跨线程直接访问。"));
        return false;
    }
    // This optional cross-page hook uses the real QObject slot at runtime so
    // the independent project target need not link the history implementation.
    const QMetaObject::Connection connection = QObject::connect(this,
        SIGNAL(currentFormatInvalidated(QString)), state,
        SLOT(invalidateConfiguration(QString)), Qt::UniqueConnection);
    if (!connection)
    {
        report(tr("当前结果显示缓存连接失败；请核对是否重复绑定以及真实槽是否可用。"));
        return false;
    }
    return true;
}
void ProjectPage::openResultFormat()
{
    if (m_originalProfile) { openOriginalResultFormat(); return; }
    if (m_formatDialog) { m_formatDialog->raise(); m_formatDialog->activateWindow(); return; }
    if (!m_ready || !m_store || m_busy || m_creating || m_refreshRequired || m_promptOpen ||
        m_auxiliaryDialog || m_selectedId <= 0)
    { report(tr("项目未就绪或正在处理，请稍后再配置结果显示。")); return; }
    if (m_dirty)
    { report(tr("当前项目有基础或高级参数草稿，请先自行保存或放弃，再配置结果显示。")); return; }
    if (m_catalogDatabasePath.isEmpty())
    { report(tr("当前项目资料源路径不可确认，未打开编辑器。")); return; }
    QPointer<ProjectPage> owner(this);
    auto *dialog = new ProjectFormatDialog(m_store, m_selectedId, m_catalogDatabasePath, m_session,
        [owner]() -> qint64 { return owner ? ++owner->m_request : 0; }, this);
    m_formatDialog = dialog; m_formatRefreshAfterClose = false;
    connect(dialog, &ProjectFormatDialog::operationBusyChanged, this, &ProjectPage::setBusy);
    connect(dialog, &ProjectFormatDialog::draftChanged, this, [this]() { emit busyChanged(m_busy); });
    connect(dialog, &ProjectFormatDialog::configurationInvalidated, this, [this](const QString &reason)
    { emit currentFormatInvalidated(reason); report(reason); });
    connect(dialog, &ProjectFormatDialog::committed, this, [this](qint64 id)
    {
        emit currentFormatInvalidated(tr("结果显示配置已实际提交，请重新查询。"));
        m_formatRefreshAfterClose = true;
        report(tr("结果显示配置已保存，其它项目参数未由此操作修改。"));
        emit operationFinished(QStringLiteral("replaceCurrentResultFormat"), true);
        Q_UNUSED(id);
    });
    connect(dialog, &QDialog::finished, this, [this]()
    {
        m_formatDialog.clear(); setBusy(false); emit busyChanged(m_busy);
        const bool refresh = m_formatRefreshAfterClose; m_formatRefreshAfterClose = false;
        if (refresh && m_ready) QTimer::singleShot(0, this, &ProjectPage::reload);
    });
    updateActions(); dialog->open(); dialog->startRead();
}

void ProjectPage::openOriginalResultFormat()
{
    if (!m_ready || m_busy || m_refreshRequired || m_promptOpen || m_auxiliaryDialog ||
        m_formatDialog || m_selectedId <= 0 || m_creating) return;
    const qint64 selectedId = m_selectedId;
    const QString sourceRoot = m_originalRoot;
    const QString fingerprint = m_draftProject.originalFingerprint;
    ProjectResults selections = m_draftProject.results;
    for (int row = 0; row < selections.size(); ++row)
        if (auto *main = qobject_cast<CusCheckBox *>(ui->resultTable->cellWidget(row, 0)))
            selections[row].mainResult = main->isChecked();
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("originalProjectResultsDialog"));
    dialog.setWindowTitle(tr("原库结果显示配置：%1").arg(m_draftProject.name));
    dialog.resize(1060, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *notice = new QLabel(tr("保留原project_results全部结果行。这里仅修改已确认类型的启用、显示和主结果草稿；MDA及未知类型只读保留。点“应用到草稿”后，仍需点原页面保存才写库。"), &dialog);
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto *table = new QTableWidget(selections.size(), 7, &dialog);
    table->setObjectName(QStringLiteral("originalProjectResultsTable"));
    table->setHorizontalHeaderLabels({tr("结果类型"), tr("结果名称"), tr("单位"), tr("启用"), tr("显示"), tr("主结果"), tr("编辑范围")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table);
    const bool writable = canStartOperation(true);
    for (int row = 0; row < selections.size(); ++row)
    {
        const ProjectResult &result = selections[row];
        const int expected = sourceResultId(result.type);
        const bool known = result.selectionKnown && expected > 0 && result.sourceResultId == expected;
        table->setItem(row, 0, new QTableWidgetItem(result.type));
        table->setItem(row, 1, new QTableWidgetItem(result.name));
        table->setItem(row, 2, new QTableWidgetItem(result.unit));
        table->setItem(row, 6, new QTableWidgetItem(known ? tr("已确认类型") : tr("原值只读保留")));
        const bool values[] = {result.used, result.display, result.mainResult};
        for (int column = 3; column <= 5; ++column)
        {
            auto *check = new QCheckBox(table);
            check->setObjectName(QStringLiteral("originalResult_%1_%2").arg(row).arg(column));
            check->setChecked(values[column - 3]);
            // 原页面没有关闭原始结果计算的入口，不能在辅助窗中额外解禁。
            check->setEnabled(writable && known && !(column == 3 && result.type == QStringLiteral("Original")));
            table->setCellWidget(row, column, check);
        }
    }
    auto *message = new QLabel(&dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("应用到草稿"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("applyOriginalResultDraft"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(writable);
    layout->addWidget(buttons);
    bool changed = false;
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&]()
    {
        if (!m_ready || m_busy || !ProjectsStore::canEdit(m_session) || selectedId != m_selectedId ||
            sourceRoot != m_originalRoot || fingerprint != m_draftProject.originalFingerprint)
        {
            message->setText(tr("当前账户或资料源已变化，未应用旧草稿，请关闭后重新进入。"));
            return;
        }
        ProjectResults next = selections;
        bool mainChanged = false;
        int mainCount = 0;
        for (int row = 0; row < next.size(); ++row)
        {
            const auto value = [table, row](int column)
            { return qobject_cast<QCheckBox *>(table->cellWidget(row, column))->isChecked(); };
            next[row].used = value(3);
            next[row].display = value(4);
            next[row].mainResult = value(5);
            mainCount += next[row].mainResult ? 1 : 0;
            mainChanged = mainChanged || next[row].mainResult != selections[row].mainResult;
            changed = changed || next[row].used != selections[row].used ||
                      next[row].display != selections[row].display || next[row].mainResult != selections[row].mainResult;
        }
        if (mainChanged && mainCount != 1)
        {
            message->setText(tr("修改主结果时须明确保留且只选择一个主结果；只读行不能被悄悄取消。"));
            changed = false;
            return;
        }
        selections = next;
        dialog.accept();
    });
    m_auxiliaryDialog = &dialog;
    updateActions();
    const int answer = dialog.exec();
    m_auxiliaryDialog.clear();
    updateActions();
    if (answer != QDialog::Accepted || !changed || !canStartOperation(true) || selectedId != m_selectedId ||
        sourceRoot != m_originalRoot || fingerprint != m_draftProject.originalFingerprint) return;
    for (int row = 0; row < selections.size(); ++row)
    {
        // 只应用选择状态，不重建表格，保留其它控件中尚未保存的输入原文。
        m_draftProject.results[row].used = selections[row].used;
        m_draftProject.results[row].display = selections[row].display;
        m_draftProject.results[row].mainResult = selections[row].mainResult;
        if (auto *main = qobject_cast<CusCheckBox *>(ui->resultTable->cellWidget(row, 0)))
            main->setCheckState(selections[row].mainResult ? Qt::Checked : Qt::Unchecked);
        ui->resultTable->setRowHidden(row, selections[row].selectionKnown && !selections[row].used);
    }
    const QList<QPair<CusCheckBox *, QString>> switches = {
        {ui->ckxCalibrationUsed, QStringLiteral("Calibration")},
        {ui->ckxRUsed, QStringLiteral("Rate")}, {ui->ckxINRUsed, QStringLiteral("INR")}};
    for (const auto &resultSwitch : switches)
        for (const auto &result : selections)
            if (result.type == resultSwitch.second)
                resultSwitch.first->setCheckState(result.used ? Qt::Checked : Qt::Unchecked);
    markDirty();
    report(tr("原库结果选择草稿已更新；点击原页面保存后才写回原DB。"));
}
