#include "sample_page.h"
#include "cuscheckbox.h"
#include "cuscheckheader.h"
#include "cuscombox_base.h"
#include "cusdialogmask.h"
#include "qtmaterialradiobutton.h"
#include "qtmaterialscrollbar.h"
#include "sample_widgets.h"
#include "ui_frmsamplematerial.h"
#include "ui_frmsamplesettingmaterial.h"
#include "ui_maskdlgcombasetting.h"
#include "ui_maskdlgracknosetting.h"
#include "ui_maskdlgrepeatsetting.h"
#include "ui_maskdlgsampledetail.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QIntValidator>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <functional>
#include <memory>

namespace
{
QString sampleStyle()
{
    return QStringLiteral(
        ".QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
        ".QPushButton:hover{background-color:rgba(42,118,242,170);}.QPushButton:pressed{background-color:"
        "rgba(135,206,250,255);}"
        "QHeaderView{background:transparent;}QHeaderView::section{color:#444444;font:bold 15px 'Noto Sans "
        "CJK SC';height:40px;background:transparent;border:1px solid "
        "#DDDDDD;border-left:none;border-right:none;border-top:none;}"
        "QTableWidget::item{border:0;border-bottom:1px solid "
        "#DDDDDD;}QTableView::item:selected{background:rgba(183,215,255,255);}"
        "QTableWidget{background:transparent;alternate-background-color:#EEEEEE;}"
        "QComboBox{border:1px solid #E4E8EB;border-radius:5px;padding:2px "
        "4px;background:transparent;color:#444444;}"
        "QComboBox::down-arrow{image:url(:/image/arrow_drop_down.svg);width:25px;height:25px;}"
        "QLineEdit{background:transparent;border:0;border-bottom:1px solid #DDDDDD;color:#444444;}"
        "QListWidget{background:transparent;border:none;}QListWidget::item:selected{background:#B7D7FF;}");
}
QString projectStyle()
{
    return QStringLiteral(
        "QPushButton{background:#FFFFFF;color:#444444;border-radius:5px;border:1px solid #DDDDDD;}"
        "QPushButton:checked{background:rgba(42,118,242,255);color:#FFFFFF;}");
}
class SampleEditor : public QDialog
{
  public:
    std::function<bool()> allowClose;
    explicit SampleEditor(QWidget *parent) : QDialog(parent) {}
    void reject() override
    {
        if (bypass_ || !allowClose || allowClose())
        {
            QDialog::reject();
        }
    }

  protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (allowClose && !allowClose())
        {
            event->ignore();
            return;
        }
        bypass_ = true;
        QDialog::closeEvent(event);
        bypass_ = false;
    }

  private:
    bool bypass_ = false;
};
bool confirm(QWidget *parent, const QString &message)
{
    QMessageBox dialog(QMessageBox::Question, QStringLiteral("样本设置"), message,
                       QMessageBox::Discard | QMessageBox::Cancel, parent);
    dialog.setObjectName(QStringLiteral("sampleDiscardDialog"));
    dialog.setDefaultButton(QMessageBox::Cancel);
    return dialog.exec() == QMessageBox::Discard;
}
void maskOpen(SampleEditor *dialog)
{
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setStyleSheet(sampleStyle());
    if (auto *mask = dialog->findChild<CusDialogMask *>("mask"))
    {
        mask->setupForm();
        QObject::connect(mask, &CusDialogMask::closeFinished, dialog, &QDialog::reject);
    }
    dialog->open();
    if (auto *mask = dialog->findChild<CusDialogMask *>("mask"))
    {
        mask->showAnimation();
    }
}
} // namespace
SamplePage::SamplePage(SampleStore *store, QWidget *parent)
    : QWidget(parent), ui(new Ui::frmSampleMaterial), setting_(new Ui::frmSampleSettingMaterial),
      settingWidget_(nullptr), store_(store)
{
    ui->setupUi(this);
    setFixedSize(1751, 968);
    settingWidget_ = ui->wgtSampleSetting;
    const QRect original = settingWidget_->geometry();
    setting_->setupUi(settingWidget_);
    settingWidget_->setGeometry(original);
    // Original constructor removes its designer-only first project stack page.
    QWidget *unused = setting_->stkProjButton->widget(0);
    setting_->stkProjButton->removeWidget(unused);
    delete unused;
    setting_->stkProjButton->setCurrentIndex(0);
    setting_->btnCalProj->setEnabled(false);
    setting_->btnCalProj->setToolTip(tr("校准流程尚未移植。"));
    setting_->wgtSetTableBg->setRadius(15);
    setting_->wgtProjTableBg->setRadius(15);
    auto *header = new CusCheckHeader(0, Qt::Horizontal, setting_->tblSetting);
    setting_->tblSetting->setHorizontalHeader(header);
    style();
    connect(header, &CusCheckHeader::signalCheckStausChange, this,
            [this](Qt::CheckState state)
            {
                for (int row = 0; row < setting_->tblSetting->rowCount(); ++row)
                {
                    if (auto *check = qobject_cast<CusCheckBox *>(setting_->tblSetting->cellWidget(row, 0)))
                    {
                        check->setCheckState(state);
                    }
                }
            });
    auto *tableScroll = new QtMaterialScrollBar(setting_->tblSetting);
    tableScroll->setSliderColor(QColor(34, 34, 34, 50));
    tableScroll->setBackgroundColor(Qt::transparent);
    tableScroll->setCanvasColor(Qt::transparent);
    setting_->tblSetting->setVerticalScrollBar(tableScroll);
    auto *projectScroll = new QtMaterialScrollBar(setting_->projButtonList);
    projectScroll->setSliderColor(QColor(34, 34, 34, 50));
    projectScroll->setBackgroundColor(Qt::transparent);
    projectScroll->setCanvasColor(Qt::transparent);
    setting_->projButtonList->setVerticalScrollBar(projectScroll);
    setting_->tblSetting->setColumnCount(5);
    setting_->tblSetting->setHorizontalHeaderLabels(
        {tr("孔位"), tr("样本号"), tr("样本条码"), tr("容器类型"), tr("任务列表")});
    const int widths[] = {146, 73, 219, 146, 756};
    for (int column = 0; column < 5; ++column)
    {
        setting_->tblSetting->setColumnWidth(column, widths[column]);
    }
    setting_->tblSetting->horizontalHeader()->setStretchLastSection(true);
    setting_->tblSetting->verticalHeader()->hide();
    setting_->tblSetting->setSelectionBehavior(QAbstractItemView::SelectRows);
    setting_->tblSetting->setSelectionMode(QAbstractItemView::SingleSelection);
    setting_->tblSetting->setEditTriggers(QAbstractItemView::DoubleClicked);
    setting_->tblSetting->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setting_->projButtonList->setSelectionMode(QAbstractItemView::NoSelection);
    setting_->projButtonList->setSpacing(3);
    ui->rackList->setSelectionMode(QAbstractItemView::NoSelection);
    ui->rackList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(setting_->tblSetting, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) { selectHole(row); });
    connect(setting_->tblSetting, &QTableWidget::cellChanged, this,
            [this](int row, int column)
            {
                if (populating_ || column != 1 || row < 0 || row >= editing_.holes.size() ||
                    !SampleStore::canEdit(session_))
                {
                    return;
                }
                if (!canEditHole(row))
                {
                    QSignalBlocker blocker(setting_->tblSetting);
                    setting_->tblSetting->item(row, 1)->setText(editing_.holes[row].sampleId);
                    report(tr("该孔位正在测试或尚不可修改，请先核对本地状态。"));
                    return;
                }
                if (editing_.holes[row].sampleType == 2)
                {
                    QSignalBlocker blocker(setting_->tblSetting);
                    setting_->tblSetting->item(row,1)->setText(editing_.holes[row].sampleId);
                    report(tr("质控样本号由质控品和批次生成，请在明细选择。"));
                    return;
                }
                editing_.holes[row].sampleId = setting_->tblSetting->item(row, 1)->text();
                markDirty();
            });
    connect(ui->btnAdd, &CusBadgeButton::clicked, this, &SamplePage::addRack);
    connect(ui->btnMenuSave, &QPushButton::clicked, this, &SamplePage::saveRack);
    connect(ui->btnEditRack, &QPushButton::clicked, this, [this]() { showRack(editing_.id); });
    connect(ui->btnMenuReturn, &QPushButton::clicked, this,
            [this]()
            {
                if (!prepareToLeave())
                {
                    return;
                }
                settingWidget_->hide();
                ui->stkFunMenu->setCurrentIndex(0);
                refreshRackList();
                ui->btnPrvRack->hide();
                ui->btnNextRack->hide();
                ui->btnAdd->show();
            });
    connect(ui->btnHoleDetail, &QPushButton::clicked, this, &SamplePage::detail);
    connect(ui->btnRepeatHoleTask, &QPushButton::clicked, this, &SamplePage::repeat);
    connect(ui->btnDelHoleTask, &QPushButton::clicked, this, &SamplePage::clearHole);
    connect(ui->btnDelRack, &QPushButton::clicked, this, &SamplePage::removeRack);
    connect(ui->btnDelRack_current, &QPushButton::clicked, this, &SamplePage::removeRack);
    connect(ui->btnDelRackAll, &QPushButton::clicked, this, &SamplePage::clearRacks);
    connect(ui->btnDelRackAll_2, &QPushButton::clicked, this, &SamplePage::clearRacks);
    connect(ui->btnCombSetting, &QPushButton::clicked, this, &SamplePage::combinations);
    connect(ui->btnPrvRack, &QPushButton::clicked, this,
            [this]()
            {
                for (int i = 1; i < snapshot_.racks.size(); ++i)
                {
                    if (snapshot_.racks[i].id == editing_.id)
                    {
                        if (prepareToLeave())
                        {
                            showRack(snapshot_.racks[i - 1].id);
                        }
                        break;
                    }
                }
            });
    connect(ui->btnNextRack, &QPushButton::clicked, this,
            [this]()
            {
                for (int i = 0; i + 1 < snapshot_.racks.size(); ++i)
                {
                    if (snapshot_.racks[i].id == editing_.id)
                    {
                        if (prepareToLeave())
                        {
                            showRack(snapshot_.racks[i + 1].id);
                        }
                        break;
                    }
                }
            });
    connect(ui->btnUpdateLis, &QAbstractButton::clicked, this,
            [this]() { report(tr("LIS更新：尚未接入设备流程，未发送命令。")); });
    connect(ui->btnResetErr, &QAbstractButton::clicked, this,
            [this]() { resetTasks(SampleTaskResetAction::ResetErrors); });
    connect(ui->btnResetFin, &QAbstractButton::clicked, this,
            [this]() { resetTasks(SampleTaskResetAction::RetestSample); });
    connect(setting_->repeatComb, QOverload<int>::of(&QComboBox::activated), this,
            [this](int index)
            {
                if (index <= 0)
                {
                    return;
                }
                // This original shortcut is the same batch copy action with auto IDs.
                repeat();
                QSignalBlocker blocker(setting_->repeatComb);
                setting_->repeatComb->setCurrentIndex(0);
            });
    connect(store, &SampleStore::ready, this,
            [this](const QString &)
            {
                initialized_ = true;
                reload();
            });
    connect(store, &SampleStore::snapshotReady, this,
            [this](qint64 request, SampleSnapshot snapshot)
            {
                if (request != request_)
                {
                    return;
                }
                snapshot_ = snapshot;
                ready_ = true;
                qint64 selected = selectAfter_ ? selectAfter_ : editing_.id;
                selectAfter_ = 0;
                bool exists = false;
                for (const auto &rack : snapshot_.racks)
                {
                    if (rack.id == selected)
                    {
                        exists = true;
                    }
                }
                if (!exists)
                {
                    selected = snapshot_.racks.isEmpty() ? 0 : snapshot_.racks.first().id;
                }
                if (!dirty_)
                {
                    showRack(selected);
                }
                else
                {
                    refreshRackList();
                }
                setBusy(false);
                report(tr("本地预设已读取；未下发，真实状态和足料校验待验。")+qualityWarning_);
                emit snapshotChanged(snapshot_);
                emit operationFinished("load", true);
            });
    connect(store, &SampleStore::succeeded, this,
            [this](qint64 request, const QString &operation, qint64 id)
            {
                if (request != request_)
                {
                    return;
                }
                if (operation == "saveRack" || operation == "removeRack" || operation == "clearOrdinaryRacks")
                {
                    dirty_ = false;
                    selectAfter_ = operation == "saveRack" ? id : 0;
                }
                if (operation == "saveCombination" || operation == "removeCombination")
                {
                    dialogDirty_ = false;
                    if (dialog_)
                    {
                        dialog_->accept();
                    }
                }
                report(tr("本地%1成功；未下发/扣量/生成报告。").arg(operation)+qualityWarning_);
                emit operationFinished(operation, true);
                ++request_;
                QMetaObject::invokeMethod(store_, "load", Qt::QueuedConnection, Q_ARG(qint64, request_));
            });
    connect(store, &SampleStore::error, this,
            [this](qint64 request, const QString &operation, const QString &message)
            {
                if (request && request != request_)
                {
                    return;
                }
                setBusy(false);
                report(message + qualityWarning_);
                emit operationFinished(operation, false);
            });
    connect(store,&SampleStore::warning,this,[this](qint64,QString,const QString &message)
    { qualityWarning_=message; report(message); });
    connect(store,&SampleStore::qualityProtectionReady,this,[this](bool ready)
    {
        if (!ready)
        {
            invalidateQualityChoices(tr("质控引用保护未就绪，原草稿保留。"));
            return;
        }
        qualityProtectionBound_ = true;
        qualityWarning_.clear();
        qualityChoices_ = SampleQcPreset::Snapshot();
        ++qualityChoicesRequest_;
        emit qualityChoicesChanged();
        emit requestQualityChoices(qualityChoicesRequest_); // 只刷新选择资料，不覆盖架或明细草稿。
    });
    connect(store, &SampleStore::closed, this,
            [this]()
            {
                initialized_ = false;
                ready_ = false;
                invalidateQualityChoices(tr("样本存储已关闭，质控选择已失效。"));
                setBusy(false);
                report(tr("样本存储已关闭；本地草稿保留，不能继续提交。"));
            });
    actions();
}
SamplePage::~SamplePage()
{
    if (dialog_)
    {
        delete dialog_;
    }
    delete setting_;
    delete ui;
}
void SamplePage::style()
{
    setStyleSheet(sampleStyle());
    ui->sampleFunMenu->setBackgroundColor(QColor(28, 28, 28));
    QLinearGradient gradient(0, 0, 0, ui->sampleFunMenu->height());
    gradient.setColorAt(0, QColor(255, 255, 255, 0));
    gradient.setColorAt(1, QColor(255, 255, 255, 1));
    ui->sampleFunMenu->setGraBackgroundColor(gradient);
    ui->sampleFunMenu->setShadowEffect(Qt::AlignLeft);
    for (auto *button : ui->sampleFunMenu->findChildren<QtMaterialFlatButton *>())
    {
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setUseThemeColors(false);
        button->setBackgroundColor(QColor(127, 127, 127));
        button->setForegroundColor(Qt::white);
    }
    ui->btnSetSample->hide();
    ui->btnAdd->setSize(80);
    ui->btnAdd->setNormalBgImage(":/samples/addrack_normal.png");
    ui->btnAdd->setHoverBgImage(":/samples/addrack_hover.png");
    ui->btnAdd->setPressedBgImage(":/samples/addrack_pressed.png");
    setting_->wgtTabs->setStyleSheet(
        ".QPushButton{color:#AAAAAA;border:none;background:transparent;border-top:5px solid "
        "transparent;}.QPushButton:checked{color:#444444;border-top:5px solid rgba(232,51,35,255);}");
    setting_->tblSetting->horizontalHeader()->setStyleSheet(
        "QHeaderView{background:white;}QHeaderView::section{color:#444444;font:bold 15px 'Noto Sans CJK "
        "SC';height:60px;background:transparent;border:1px solid "
        "#DDDDDD;border-left:none;border-right:none;border-top:none;}");
}
void SamplePage::report(const QString &message)
{
    setProperty("lastStatus", message);
    emit statusChanged(message);
}
void SamplePage::setBusy(bool busy)
{
    busy_ = busy;
    actions();
    emit busyChanged(busy);
}
void SamplePage::actions()
{
    QSignalBlocker tableSignals(setting_->tblSetting);
    const bool edit = ready_ && !busy_ && SampleStore::canEdit(session_);
    for (QAbstractButton *button :
         QList<QAbstractButton *>{ui->btnMenuSave, ui->btnHoleDetail, ui->btnRepeatHoleTask,
                                  ui->btnDelHoleTask, ui->btnCombSetting, ui->btnDelRack,
                                  ui->btnDelRack_current, ui->btnDelRackAll, ui->btnDelRackAll_2,
                                  ui->btnResetErr, ui->btnResetFin})
    {
        button->setEnabled(edit);
    }
    ui->btnAdd->setEnabled(edit);
    setting_->tblSetting->setEnabled(edit);
    const bool ordinary = row_>=0 && row_<editing_.holes.size() && editing_.holes[row_].sampleType==0;
    setting_->projButtonList->setEnabled(canEditHole(row_) && ordinary);
    setting_->repeatComb->setEnabled(canEditHole(row_) && ordinary);
    ui->btnHoleDetail->setEnabled(canEditHole(row_));
    ui->btnRepeatHoleTask->setEnabled(canEditHole(row_) && ordinary);
    for (int row = 0; row < setting_->tblSetting->rowCount(); ++row)
    {
        if (auto *item = setting_->tblSetting->item(row, 1))
        {
            auto flags = item->flags();
            item->setFlags(canEditHole(row) && editing_.holes[row].sampleType==0 ? flags | Qt::ItemIsEditable : flags & ~Qt::ItemIsEditable);
        }
        if (auto *container = setting_->tblSetting->cellWidget(row, 3))
        {
            container->setEnabled(canEditHole(row));
        }
    }
    ui->rackList->setEnabled(!busy_);
    ui->btnMenuReturn->setEnabled(!busy_);
    ui->btnPrvRack->setEnabled(!busy_);
    ui->btnNextRack->setEnabled(!busy_);
    // Keep cancel/close available after permission changes. Store checks again.
    if (dialog_)
    {
        for (auto *button : dialog_->findChildren<QPushButton *>())
        {
            if (button->objectName() == "btnSave" || button->objectName() == "btnDel" ||
                button->objectName() == "btnAdd")
            {
                button->setEnabled(edit);
            }
        }
    }
    if (dialog_)
    {
        for (auto *input : dialog_->findChildren<QLineEdit *>())
        {
            input->setEnabled(edit);
        }
    }
    if (dialog_)
    {
        for (auto *input : dialog_->findChildren<QComboBox *>())
        {
            input->setEnabled(edit);
        }
        if (auto *normal=dialog_->findChild<QtMaterialRadioButton *>("radioBoxSM"))
            normal->setEnabled(edit);
        if (auto *quality=dialog_->findChild<QtMaterialRadioButton *>("radioBoxQC"))
            quality->setEnabled(edit && qualityProtectionBound_ && qualityChoices_.ready);
    }
}
void SamplePage::setProjects(ProjectRecords projects)
{
    projects_ = projects;
    if (!dialog_)
    {
        refreshProjects(); // Never rebuild an in-progress dialog.
    }
    if (dirty_)
    {
        report(tr("项目快照已更新，编辑草稿保留；保存时重新核对引用和稀释候选。"));
    }
}
void SamplePage::setReagents(ReagentSnapshot reagents)
{
    reagents_ = reagents;
    setProperty("reagentSource", QStringLiteral("manual-local; consumption-map-unavailable"));
}
void SamplePage::setSession(AccountSession session)
{
    session_ = session;
    QMetaObject::invokeMethod(store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, session));
    actions();
}
void SamplePage::reload()
{
    if (!initialized_ || busy_)
    {
        return;
    }
    if (hasUnsavedChanges())
    {
        report(tr("未保存草稿仍在编辑，未覆盖刷新。"));
        return;
    }
    setBusy(true);
    ++request_;
    QMetaObject::invokeMethod(store_, "load", Qt::QueuedConnection, Q_ARG(qint64, request_));
}
bool SamplePage::closeEditor()
{
    if (!dialog_)
    {
        return true;
    }
    dialog_->reject();
    return !dialog_ || !dialog_->isVisible();
}
bool SamplePage::prepareToLeave()
{
    if (busy_)
    {
        report(tr("样本操作正在提交，请稍后。"));
        return false;
    }
    if (!closeEditor())
    {
        return false;
    }
    if (dirty_ && !confirm(this, tr("当前样本设置未保存，是否放弃？")))
    {
        return false;
    }
    if (dirty_)
    {
        dirty_ = false;
        showRack(editing_.id);
        report(tr("已放弃未保存更改，已恢复本地已保存设置；未发送设备命令。"));
    }
    return true;
}
void SamplePage::closeEvent(QCloseEvent *event)
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
void SamplePage::markDirty()
{
    if (!populating_ && !busy_ && SampleStore::canEdit(session_))
    {
        dirty_ = true;
    }
}
bool SamplePage::canEditHole(int row) const
{
    return ready_ && !busy_ && SampleStore::canEdit(session_) && row >= 0 && row < editing_.holes.size() &&
           editing_.holes[row].revise &&
           sampleHoleState(sampleHoleTaskStates(editing_.holes[row])) != SampleHoleState::Testing;
}
void SamplePage::resetTasks(SampleTaskResetAction action)
{
    if (!ready_ || busy_ || dialog_ || !SampleStore::canEdit(session_) ||
        row_ < 0 || row_ >= editing_.holes.size())
    {
        return;
    }
    SampleRack candidate = editing_;
    int changed = 0;
    for (int row = 0; row < candidate.holes.size(); ++row)
    {
        auto *check = qobject_cast<CusCheckBox *>(setting_->tblSetting->cellWidget(row, 0));
        if (row != row_ && (!check || !check->isChecked()))
        {
            continue;
        }
        SampleHole &hole = candidate.holes[row];
        for (const SampleTask &task : hole.tasks)
        {
            if (!task.states.isEmpty() && task.states.size() != task.repeats)
            {
                report(tr("孔%1逐次状态数量不匹配，整批草稿未改动。").arg(hole.number));
                return;
            }
        }
        SampleTaskResetDraft draft;
        draft.taskStates = sampleHoleTaskStates(hole);
        draft.revise = hole.revise;
        const auto reset = prepareSampleTaskReset(draft, action);
        if (!reset.accepted)
        {
            // 原版还处理勾选孔；这里先检查全部目标，避免当前孔之外的Testing被部分改写。
            report(tr("孔%1：%2 整批草稿未改动。").arg(hole.number).arg(reset.message));
            return;
        }
        int position = 0;
        for (SampleTask &task : hole.tasks)
        {
            task.states.clear();
            for (int repeat = 0; repeat < task.repeats; ++repeat)
            {
                task.states.append(reset.draft.taskStates[position++]);
            }
        }
        hole.revise = reset.draft.revise;
        changed += reset.changedTests;
    }
    editing_ = candidate;
    markDirty();
    refreshTable();
    refreshProjectButtons();
    if (auto *header = qobject_cast<CusCheckHeader *>(setting_->tblSetting->horizontalHeader()))
    {
        header->setCheckState(Qt::Unchecked);
    }
    report(tr("本地草稿已复位%1次测试；点更新设置后保存，未发送设备命令。").arg(changed));
    emit operationFinished(action == SampleTaskResetAction::ResetErrors ? "resetErrorsDraft" : "retestDraft", true);
}
const ProjectRecord *SamplePage::project(qint64 id) const
{
    for (const auto &value : projects_)
    {
        if (value.id == id)
        {
            return &value;
        }
    }
    return nullptr;
}
QString SamplePage::projectName(qint64 id) const
{
    const auto *value = project(id);
    return value ? value->name : tr("失效项目%1").arg(id);
}
QString SamplePage::defaultRate(qint64 id) const
{
    const auto *value = project(id);
    if (!value)
    {
        return QString();
    }
    QString selected;
    int count = 0;
    for (const auto &rate : value->sampleRates)
    {
        if (rate.defaultOption)
        {
            if (!rate.sampleOption)
            {
                return QString();
            }
            selected = rate.sampleRate;
            ++count;
        }
    }
    return count == 1 ? selected : QString();
}
void SamplePage::refreshRackList()
{
    QSignalBlocker blocker(ui->rackList);
    const bool editorOpen = !settingWidget_->isHidden();
    const bool localDraft = editorOpen && !editing_.id && !editing_.holes.isEmpty();
    auto visibleRacks = snapshot_.racks;
    if (localDraft)
    {
        // A new rack has no persistent ID until saveRack succeeds.
        visibleRacks.append(editing_);
    }
    QListWidgetItem *currentItem = nullptr;
    ui->rackList->clear();
    for (int index = 0; index < visibleRacks.size(); ++index)
    {
        const auto &rack = visibleRacks[index];
        auto *item = new QListWidgetItem(ui->rackList);
        const bool current = rack.id == editing_.id;
        item->setSizeHint(QSize(ui->rackList->width(), current && editorOpen ? height() : 136));
        if (current)
        {
            currentItem = item;
        }
        auto *view = new SampleRackView(rack, index, current, ui->rackList);
        view->setProperty("localDraft", localDraft && current);
        if (localDraft && current)
        {
            view->setToolTip(tr("本地草稿，尚未保存；未下发。"));
        }
        connect(view, &SampleRackView::selected, this,
                [this](qint64 id)
                {
                    if (id != editing_.id && prepareToLeave())
                    {
                        showRack(id);
                    }
                });
        connect(view, &SampleRackView::editRequested, this,
                [this](qint64 id)
                {
                    if ((id != editing_.id || settingWidget_->isHidden()) && prepareToLeave())
                    {
                        showRack(id);
                    }
                });
        connect(view, &SampleRackView::renameRequested, this,
                [this](qint64 id)
                {
                    if (id != editing_.id)
                    {
                        if (!prepareToLeave())
                        {
                            return;
                        }
                        showRack(id);
                    }
                    renameRack();
                });
        ui->rackList->setItemWidget(item, view);
    }
    // Original editRack expands the current row so its card stays above the editor.
    ui->rackList->setScrollEnabled(!editorOpen);
    if (currentItem)
    {
        ui->rackList->setCurrentItem(currentItem);
        ui->rackList->doItemsLayout();
        ui->rackList->scrollToItem(currentItem, editorOpen ? QAbstractItemView::PositionAtTop
                                                           : QAbstractItemView::EnsureVisible);
    }
}
void SamplePage::showRack(qint64 id)
{
    editing_ = SampleRack();
    for (const auto &rack : snapshot_.racks)
    {
        if (rack.id == id)
        {
            editing_ = rack;
        }
    }
    if (!editing_.id && !snapshot_.racks.isEmpty())
    {
        editing_ = snapshot_.racks.first();
    }
    dirty_ = false;
    row_ = 0;
    refreshTable();
    refreshProjects();
    settingWidget_->setVisible(editing_.id > 0);
    settingWidget_->raise();
    refreshRackList();
    ui->stkFunMenu->setCurrentIndex(1);
    ui->btnDelRack->setVisible(!editing_.emergency);
    ui->btnDelRackAll->setVisible(!editing_.emergency);
    ui->btnPrvRack->setVisible(!snapshot_.racks.isEmpty() && editing_.id != snapshot_.racks.first().id);
    ui->btnNextRack->setVisible(!snapshot_.racks.isEmpty() && editing_.id != snapshot_.racks.last().id);
    ui->btnPrvRack->raise();
    ui->btnNextRack->raise();
    actions();
}
void SamplePage::refreshTable()
{
    populating_ = true;
    auto *table = setting_->tblSetting;
    QSignalBlocker blocker(table);
    table->clearContents();
    table->setRowCount(editing_.holes.size());
    for (int row = 0; row < editing_.holes.size(); ++row)
    {
        const auto &hole = editing_.holes[row];
        auto *check = new CusCheckBox(table);
        check->setText(QString::number(hole.number));
        check->setFont(QFont("Noto Sans CJK SC", 12));
        table->setCellWidget(row, 0, check);
        connect(check, QOverload<>::of(&CusCheckBox::signalClicked), this,
                [this, row]()
                {
                    setting_->tblSetting->selectRow(row);
                    selectHole(row);
                });
        auto *sample = new QTableWidgetItem(hole.sampleId);
        sample->setToolTip(hole.sampleId); // 原窄列保留；完整派生名可回看，不截断存储值。
        sample->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, 1, sample);
        auto *code = new QTableWidgetItem;
        code->setFlags(code->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 2, code);
        auto *container = new CusCombox_Base(table);
        container->setSourceData({tr("试管"), tr("生化杯")});
        container->setCurrentIndex(hole.container);
        table->setCellWidget(row, 3, container);
        connect(container, &CusCombox_Base::itemSelected, this,
                [this, row](int index, const QString &)
                {
                    if (canEditHole(row))
                    {
                        editing_.holes[row].container = index;
                        markDirty();
                    }
                });
        QStringList names;
        QList<SampleTaskState> states;
        for (const auto &task : hole.tasks)
        {
            for (int i = 0; i < task.repeats; ++i)
            {
                names.append(projectName(task.projectId));
            }
            states.append(sampleTaskStates(task));
        }
        auto *view = new SampleTaskView(table);
        view->resize(756, 36);
        view->setNames(names, states);
        table->setCellWidget(row, 4, view);
        table->setRowHeight(row, qMax(51, view->minimumHeight()));
    }
    if (table->rowCount())
    {
        table->selectRow(qBound(0, row_, table->rowCount() - 1));
    }
    setting_->repeatComb->clear();
    setting_->repeatComb->addItem("0");
    for (int i = 1; i <= editing_.holes.size(); ++i)
    {
        setting_->repeatComb->addItem(QString::number(i));
    }
    populating_ = false;
}
void SamplePage::selectHole(int row)
{
    if (populating_ || row < 0 || row >= editing_.holes.size())
    {
        return;
    }
    row_ = row;
    refreshProjectButtons();
}
void SamplePage::refreshProjects()
{
    auto *list = setting_->projButtonList;
    list->clear();
    if (row_ < 0 || row_ >= editing_.holes.size())
    {
        return;
    }
    for (const auto &value : projects_)
    {
        if (!value.enabled)
        {
            continue;
        }
        auto *button = new QPushButton(value.name, list);
        button->setObjectName("sampleProject_" + QString::number(value.id));
        button->setFixedSize(80, 84);
        button->setCheckable(true);
        button->setStyleSheet(projectStyle());
        button->setFocusPolicy(Qt::NoFocus);
        auto *number = new QComboBox(list);
        number->setObjectName("sampleCount_" + QString::number(value.id));
        number->addItems({"1", "2", "3", "5", "10", "18", "24"});
        number->setFixedSize(80, 84);
        number->setView(new QListView);
        auto *widget = new QWidget(list);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(button);
        layout->addWidget(number);
        auto *item = new QListWidgetItem(list);
        item->setSizeHint(QSize(160, 84));
        list->setItemWidget(item, widget);
        connect(button, &QPushButton::clicked, this,
                [this, id = value.id](bool checked)
                {
                    if (!canEditHole(row_) || editing_.holes[row_].sampleType!=0)
                    {
                        return;
                    }
                    auto &tasks = editing_.holes[row_].tasks;
                    for (int i = 0; i < tasks.size(); ++i)
                    {
                        if (tasks[i].projectId == id)
                        {
                            tasks.removeAt(i);
                            markDirty();
                            refreshTable();
                            refreshProjectButtons();
                            return;
                        }
                    }
                    if (checked)
                    {
                        tasks.append({id, defaultRate(id), 1, false});
                    }
                    markDirty();
                    refreshTable();
                    refreshProjectButtons();
                });
        connect(number, QOverload<int>::of(&QComboBox::activated), this,
                [this, id = value.id, number]()
                {
                    if (!canEditHole(row_) || editing_.holes[row_].sampleType!=0)
                    {
                        return;
                    }
                    auto &tasks = editing_.holes[row_].tasks;
                    bool found = false;
                    for (auto &task : tasks)
                    {
                        if (task.projectId == id)
                        {
                            if (task.repeats != number->currentText().toInt())
                            {
                                task.states.clear();
                            }
                            task.repeats = number->currentText().toInt();
                            found = true;
                        }
                    }
                    if (!found)
                    {
                        tasks.append({id, defaultRate(id), number->currentText().toInt(), false});
                    }
                    markDirty();
                    refreshTable();
                    refreshProjectButtons();
                });
    }
    for (const auto &combination : snapshot_.combinations)
    {
        auto *button = new QPushButton(combination.name, list);
        button->setFixedSize(160, 84);
        button->setStyleSheet(projectStyle());
        auto *item = new QListWidgetItem(list);
        item->setSizeHint(QSize(160, 84));
        list->setItemWidget(item, button);
        connect(button, &QPushButton::clicked, this,
                [this, combination]() { applyCombination(combination); });
    }
    refreshProjectButtons();
    actions();
}
void SamplePage::refreshProjectButtons()
{
    if (row_ < 0 || row_ >= editing_.holes.size())
    {
        return;
    }
    for (const auto &value : projects_)
    {
        bool selected = false;
        int count = 1;
        for (const auto &task : editing_.holes[row_].tasks)
        {
            if (task.projectId == value.id)
            {
                selected = true;
                count = task.repeats;
            }
        }
        // QListWidget retires previous item widgets with deleteLater. Search
        // only its current itemWidget tree, never a detached old control.
        QPushButton *button = nullptr;
        QComboBox *number = nullptr;
        for (int index = 0; index < setting_->projButtonList->count(); ++index)
        {
            QWidget *container = setting_->projButtonList->itemWidget(setting_->projButtonList->item(index));
            if (!container)
            {
                continue;
            }
            button = container->findChild<QPushButton *>("sampleProject_" + QString::number(value.id));
            if (button)
            {
                number = container->findChild<QComboBox *>("sampleCount_" + QString::number(value.id));
                break;
            }
        }
        if (button)
        {
            QSignalBlocker blocker(button);
            button->setChecked(selected);
        }
        if (number)
        {
            QSignalBlocker blocker(number);
            if (number->findText(QString::number(count)) < 0)
            {
                number->addItem(QString::number(count));
            }
            number->setCurrentText(QString::number(count));
        }
    }
    actions();
}
void SamplePage::addRack()
{
    if (busy_ || !SampleStore::canEdit(session_) || !prepareToLeave())
    {
        return;
    }
    SampleRack rack;
    int number = 1;
    for (; number < 99; ++number)
    {
        const QString name = QString("S%1").arg(number, 2, 10, QChar('0'));
        bool found = false;
        for (const auto &existing : snapshot_.racks)
        {
            if (existing.name == name)
            {
                found = true;
            }
        }
        if (!found)
        {
            rack.name = name;
            break;
        }
    }
    if (number == 99)
    {
        report(tr("普通架已达98个本地预算。"));
        return;
    }
    for (int hole = 1; hole <= 8; ++hole)
    {
        SampleHole value;
        value.number = hole;
        rack.holes.append(value);
    }
    editing_ = rack;
    dirty_ = true;
    row_ = 0;
    refreshTable();
    refreshProjects();
    settingWidget_->show();
    settingWidget_->raise();
    refreshRackList();
    ui->btnPrvRack->hide();
    ui->btnNextRack->hide();
    ui->stkFunMenu->setCurrentIndex(1);
    report(tr("新预设仅在编辑中，点更新设置后保存本地；不会下发。"));
}
void SamplePage::saveRack()
{
    if (busy_ || !SampleStore::canEdit(session_) || (!dirty_ && editing_.id))
    {
        return;
    }
    setBusy(true);
    ++request_;
    QMetaObject::invokeMethod(store_, "saveRack", Qt::QueuedConnection, Q_ARG(qint64, request_),
                              Q_ARG(SampleRack, editing_));
}
void SamplePage::removeRack()
{
    if (busy_ || !SampleStore::canEdit(session_) || editing_.emergency)
    {
        return;
    }
    if (!editing_.id)
    {
        if (prepareToLeave())
        {
            showRack(snapshot_.racks.first().id);
        }
        return;
    }
    const qint64 id = editing_.id;
    if (!prepareToLeave() || !confirm(this, tr("删除当前本地预设架？不会取消设备任务。")))
    {
        return;
    }
    setBusy(true);
    ++request_;
    QMetaObject::invokeMethod(store_, "removeRack", Qt::QueuedConnection, Q_ARG(qint64, request_),
                              Q_ARG(qint64, id));
}
void SamplePage::clearRacks()
{
    // All local ordinary presets are removed in one transaction.
    if (busy_ || !SampleStore::canEdit(session_) || !prepareToLeave())
    {
        return;
    }
    if (!confirm(this, tr("清空所有普通本地预设？急诊架保留，不发送设备命令。")))
    {
        return;
    }
    setBusy(true);
    ++request_;
    QMetaObject::invokeMethod(store_, "clearOrdinaryRacks", Qt::QueuedConnection, Q_ARG(qint64, request_));
}
void SamplePage::clearHole()
{
    if (busy_ || !SampleStore::canEdit(session_) || row_ >= editing_.holes.size())
    {
        return;
    }
    QList<int> selected;
    for (int row = 0; row < editing_.holes.size(); ++row)
    {
        if (auto *check = qobject_cast<CusCheckBox *>(setting_->tblSetting->cellWidget(row, 0)))
        {
            if (check->isChecked())
            {
                selected.append(row);
            }
        }
    }
    if (selected.isEmpty())
    {
        selected.append(row_);
    }
    for (int row : selected)
    {
        if (!canEditHole(row))
        {
            report(tr("孔位正在测试或尚不可修改，整批未清空。"));
            return;
        }
    }
    for (int row : selected)
    {
        editing_.holes[row].tasks.clear();
        if (editing_.holes[row].sampleType==2)
        { editing_.holes[row].sampleType=0;editing_.holes[row].quality=SampleQcIdentity();editing_.holes[row].sampleId.clear(); }
    }
    markDirty();
    refreshTable();
    refreshProjectButtons();
}
void SamplePage::renameRack()
{
    if (dialog_ || busy_ || !SampleStore::canEdit(session_) || editing_.emergency)
    {
        return;
    }
    auto *dialog = new SampleEditor(this);
    dialog->setObjectName("sampleRackNumberDialog");
    dialog_ = dialog;
    auto form = std::make_shared<Ui::MaskDlgRackNoSetting>();
    form->setupUi(dialog);
    form->ldtRackID->setReadOnly(true);
    form->ldtRackID->setText(editing_.name);
    form->btnSave->hide();
    form->btnCancel->hide();
    for (auto *button : dialog->findChildren<QPushButton *>())
    {
        if (button->objectName().startsWith("btnRackID_"))
        {
            for (const auto &rack : snapshot_.racks)
            {
                if (rack.name == button->text())
                {
                    button->setStyleSheet("QPushButton{background:grey;}");
                }
            }
            connect(button, &QPushButton::clicked, dialog,
                    [this, dialog, button]()
                    {
                        if (!SampleStore::canEdit(session_) || busy_)
                        {
                            return;
                        }
                        for (const auto &rack : snapshot_.racks)
                        {
                            if (rack.name == button->text())
                            {
                                report(tr("重复的样本架号。"));
                                return;
                            }
                        }
                        editing_.name = button->text();
                        markDirty();
                        if (!editing_.id)
                        {
                            refreshRackList();
                        }
                        dialog->accept();
                    });
        }
    }
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->allowClose = [this]() { return !busy_; };
    connect(dialog, &QDialog::finished, this,
            [this]()
            {
                dialog_.clear();
                dialogDirty_ = false;
                refreshProjects();
            });
    maskOpen(dialog);
    actions();
}
void SamplePage::applyCombination(const SampleCombination &combination)
{
    if (!canEditHole(row_))
    {
        return;
    }
    if (editing_.holes[row_].sampleType==2)
    { report(tr("质控与其关联项目绑定，请在明细修改或先删除任务。")); return; }
    QList<SampleTask> tasks;
    for (qint64 id : combination.orderedProjectIds)
    {
        const auto *value = project(id);
        if (!value || !value->enabled)
        {
            report(tr("组合引用项目已失效/禁用，未覆盖草稿。"));
            return;
        }
        bool found = false;
        for (auto &task : tasks)
        {
            if (task.projectId == id)
            {
                ++task.repeats;
                found = true;
            }
        }
        if (!found)
        {
            tasks.append({id, defaultRate(id), 1, false});
        }
    }
    for (const auto &task : tasks)
    {
        if (task.repeats > 99)
        {
            report(tr("组合单项目超过99次，未覆盖草稿。"));
            return;
        }
    }
    editing_.holes[row_].tasks = tasks;
    markDirty();
    refreshTable();
    refreshProjectButtons();
}

void SamplePage::detail()
{
    if (dialog_ || !canEditHole(row_))
    {
        return;
    }
    auto *dialog = new SampleEditor(this);
    dialog->setObjectName("sampleDetailDialog");
    dialog_ = dialog;
    auto form = std::make_shared<Ui::MaskDlgSampleDetail>();
    form->setupUi(dialog);
    auto hole = std::make_shared<SampleHole>(editing_.holes[row_]);
    const int editedRow = row_;
    const qint64 editedRackId = editing_.id;
    form->radioBoxSM->setText(tr("样本测试"));
    form->radioBoxSM->setChecked(hole->sampleType==0);
    form->radioBoxQC->setText(tr("质控测试"));
    form->radioBoxQC->setChecked(hole->sampleType==2);
    form->radioBoxQC->setEnabled(qualityProtectionBound_ && qualityChoices_.ready);
    form->radioBoxQC->setToolTip(tr("仅准备本地质控预设；未下发或判断临床可用性。"));
    form->widget->setEnabled(hole->sampleType==0);
    form->widget_2->setEnabled(hole->sampleType==2 && qualityProtectionBound_ && qualityChoices_.ready);
    form->btnClose->hide();
    form->tblSetting->setColumnHidden(4, true);
    form->tblSetting->setColumnHidden(5, true);
    form->tblSetting->verticalHeader()->hide();
    form->tblSetting->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *ordinaryValidator = new QRegularExpressionValidator(QRegularExpression("^[A-Za-z0-9]{0,4}$"),dialog);
    form->ldtSampleID->setMaxLength(hole->sampleType==2?32767:4);
    form->ldtSampleID->setValidator(hole->sampleType==2?nullptr:ordinaryValidator);
    form->ldtSampleID->setText(hole->sampleId);
    form->ldtSampleID->setReadOnly(hole->sampleType==2);
    const QString ordinaryId = hole->sampleType==0?hole->sampleId:QString();
    auto qualityValid=std::make_shared<bool>(hole->sampleType==0);
    auto idMode=[form,ordinaryValidator](bool quality)
    {
        form->ldtSampleID->setMaxLength(quality?32767:4);
        form->ldtSampleID->setValidator(quality?nullptr:ordinaryValidator);
        form->ldtSampleID->setReadOnly(quality);
        form->widget->setEnabled(!quality);
    };
    auto populate = std::make_shared<std::function<void()>>();
    *populate = [this, form, hole, dialog]()
    {
        form->tblSetting->setRowCount(hole->tasks.size());
        for (int row = 0; row < hole->tasks.size(); ++row)
        {
            const auto task = hole->tasks[row];
            const auto *value = project(task.projectId);
            auto *name = new QTableWidgetItem(projectName(task.projectId));
            name->setData(Qt::UserRole, task.projectId);
            form->tblSetting->setItem(row, 0, name);
            auto *count = new QComboBox(form->tblSetting);
            count->setObjectName("detailCount_" + QString::number(task.projectId));
            count->addItems({"1", "2", "3", "5", "10", "18", "24"});
            count->setEditable(true);
            count->lineEdit()->setValidator(new QIntValidator(0, 99, count));
            count->setCurrentText(QString::number(task.repeats));
            count->setView(new QListView);
            form->tblSetting->setCellWidget(row, 1, count);
            auto *longTest = new QCheckBox(form->tblSetting);
            longTest->setStyleSheet(
                "QCheckBox::indicator{width:30px;height:30px;}QCheckBox{padding-left:40px;}");
            longTest->setChecked(task.prolonged);
            form->tblSetting->setCellWidget(row, 2, longTest);
            auto *rate = new QComboBox(form->tblSetting);
            rate->setObjectName("detailRate_" + QString::number(task.projectId));
            rate->setView(new QListView);
            if (value)
            {
                for (const auto &option : value->sampleRates)
                {
                    if (option.sampleOption)
                    {
                        rate->addItem(option.sampleRate);
                    }
                }
            }
            if (rate->findText(task.rate) >= 0)
            {
                rate->setCurrentText(task.rate);
            }
            else
            {
                rate->setCurrentIndex(-1);
            }
            rate->setToolTip(tr("已配置的比例标签；匿名OFFLINE_RATE不表示物理倍数。"));
            form->tblSetting->setCellWidget(row, 3, rate);
            form->tblSetting->setRowHeight(row, 50);
            connect(count, &QComboBox::currentTextChanged, dialog,
                    [this, hole, id = task.projectId](const QString &text)
                    {
                        bool ok = false;
                        const int number = text.toInt(&ok);
                        if (ok && number >= 0 && number <= 99)
                        {
                            for (auto &task : hole->tasks)
                            {
                                if (task.projectId == id)
                                {
                                    if (task.repeats != number)
                                    {
                                        task.states.clear();
                                    }
                                    task.repeats = number;
                                }
                            }
                        }
                        dialogDirty_ = true;
                    });
            connect(rate, QOverload<int>::of(&QComboBox::activated), dialog,
                    [this, hole, id = task.projectId, rate]()
                    {
                        for (auto &task : hole->tasks)
                        {
                            if (task.projectId == id)
                            {
                                if (task.rate != rate->currentText())
                                {
                                    task.states.clear();
                                }
                                task.rate = rate->currentText();
                            }
                        }
                        dialogDirty_ = true;
                    });
            connect(longTest, &QCheckBox::toggled, dialog,
                    [this, hole, id = task.projectId](bool checked)
                    {
                        for (auto &task : hole->tasks)
                        {
                            if (task.projectId == id)
                            {
                                if (task.prolonged != checked)
                                {
                                    task.states.clear();
                                }
                                task.prolonged = checked;
                            }
                        }
                        dialogDirty_ = true;
                    });
        }
    };
    for (const auto &value : projects_)
    {
        if (!value.enabled)
        {
            continue;
        }
        auto *button = new QPushButton(value.name, form->projButtonList);
        button->setObjectName("detailProject_" + QString::number(value.id));
        button->setFixedSize(80, 80);
        button->setCheckable(true);
        button->setStyleSheet(projectStyle());
        button->setFocusPolicy(Qt::NoFocus);
        bool found = false;
        for (const auto &task : hole->tasks)
        {
            if (task.projectId == value.id)
            {
                found = true;
            }
        }
        button->setChecked(found);
        button->setEnabled(hole->sampleType==0);
        auto *item = new QListWidgetItem(form->projButtonList);
        item->setSizeHint(QSize(84, 84));
        form->projButtonList->setItemWidget(item, button);
        connect(button, &QPushButton::clicked, dialog,
                [this, hole, populate, id = value.id](bool checked)
                {
                    if (!SampleStore::canEdit(session_) || busy_ || hole->sampleType==2)
                    {
                        return;
                    }
                    for (int i = 0; i < hole->tasks.size(); ++i)
                    {
                        if (hole->tasks[i].projectId == id)
                        {
                            hole->tasks.removeAt(i);
                            dialogDirty_ = true;
                            (*populate)();
                            return;
                        }
                    }
                    if (checked)
                    {
                        hole->tasks.append({id, defaultRate(id), 1, false});
                    }
                    dialogDirty_ = true;
                    (*populate)();
                });
    }
    auto updateProjectChecks=[form,hole]()
    {
        for(auto *button:form->projButtonList->findChildren<QPushButton *>())
        {
            const qint64 id=button->objectName().mid(QStringLiteral("detailProject_").size()).toLongLong();
            bool chosen=false;for(const auto &task:hole->tasks)if(task.projectId==id)chosen=true;
            QSignalBlocker blocker(button);button->setChecked(chosen);button->setEnabled(hole->sampleType==0);
        }
    };
    auto selectQuality=std::make_shared<std::function<void()>>();
    *selectQuality=[this,form,hole,populate,qualityValid,idMode,updateProjectChecks,editedRow,editedRackId]()
    {
        if(editing_.id!=editedRackId || row_!=editedRow || !canEditHole(editedRow) || hole->sampleType!=2)return;
        const auto reply=SampleQcPreset::prepare(qualityChoiceSnapshot(),
            form->cbxControName->currentData().toLongLong(),form->cbxControlBatch->currentData().toLongLong());
        *qualityValid=reply.ok();
        if(!reply.ok()){report(reply.message);return;}
        const bool sameIdentity=hole->quality.selected.datasetId==reply.selection.datasetId &&
            hole->quality.selected.controlId==reply.selection.controlId && hole->quality.selected.batchId==reply.selection.batchId;
        QList<SampleTask> tasks;
        for(const auto &reference:reply.selection.projects)
        {
            SampleTask task{reference.projectId,defaultRate(reference.projectId),1,false};
            for(const auto &old:hole->tasks)if(old.projectId==reference.projectId)task=old;
            if(!sameIdentity)task.states.clear();
            tasks.append(task);
        }
        hole->tasks=tasks;hole->quality=SampleQcIdentity();hole->quality.selected=reply.selection;
        hole->sampleId=reply.selection.sampleId;
        idMode(true);form->ldtSampleID->setText(hole->sampleId);
        form->ldtSampleID->setToolTip(hole->sampleId);
        form->cbxControName->setToolTip(reply.selection.controlName);
        form->cbxControlBatch->setToolTip(reply.selection.batchName);
        dialogDirty_=true;(*populate)();updateProjectChecks();
    };
    auto fillBatches=[this,form,hole]()
    {
        QSignalBlocker block(form->cbxControlBatch);form->cbxControlBatch->clear();
        const qint64 controlId=form->cbxControName->currentData().toLongLong();
        for(const auto &control:qualityChoices_.controls)if(control.id==controlId)
            for(const auto &batch:control.batches)form->cbxControlBatch->addItem(batch.name,batch.id);
        if(hole->sampleType==2 && hole->quality.selected.controlId==controlId)
            form->cbxControlBatch->setCurrentIndex(form->cbxControlBatch->findData(hole->quality.selected.batchId));
        // 新选择沿用原下拉首项；已有ID缺失不按同名或首项重新绑定。
    };
    auto fillControls=[this,form,hole,qualityValid,fillBatches]()
    {
        QSignalBlocker block(form->cbxControName);form->cbxControName->clear();
        for(const auto &control:qualityChoices_.controls)form->cbxControName->addItem(control.name,control.id);
        if(hole->sampleType==2)
            form->cbxControName->setCurrentIndex(form->cbxControName->findData(hole->quality.selected.controlId));
        fillBatches();
        form->radioBoxQC->setEnabled(qualityProtectionBound_ && qualityChoices_.ready && SampleStore::canEdit(session_));
        form->widget_2->setEnabled(hole->sampleType==2 && qualityProtectionBound_ && qualityChoices_.ready);
        if(hole->sampleType==2)
        {
            auto check=SampleQcPreset::recheck(qualityChoiceSnapshot(),hole->quality.selected);
            *qualityValid=check.ok();
            if(!check.ok())report(qualityChoicesError_.isEmpty()?check.message:qualityChoicesError_);
        }
    };
    connect(this,&SamplePage::qualityChoicesChanged,dialog,fillControls);
    connect(form->cbxControName,QOverload<int>::of(&QComboBox::activated),dialog,
        [this,fillBatches,selectQuality](int){if(!SampleStore::canEdit(session_)||busy_)return;fillBatches();(*selectQuality)();});
    connect(form->cbxControlBatch,QOverload<int>::of(&QComboBox::activated),dialog,
        [this,selectQuality](int){if(!SampleStore::canEdit(session_)||busy_)return;(*selectQuality)();});
    connect(form->radioBoxQC,&QtMaterialRadioButton::toggled,dialog,
        [this,hole,form,selectQuality,idMode](bool checked)
        {
            if(!checked||!SampleStore::canEdit(session_)||busy_)return;
            hole->sampleType=2;idMode(true);form->widget_2->setEnabled(qualityProtectionBound_ && qualityChoices_.ready);(*selectQuality)();
        });
    connect(form->radioBoxSM,&QtMaterialRadioButton::toggled,dialog,
        [this,hole,form,populate,qualityValid,idMode,ordinaryId,updateProjectChecks](bool checked)
        {
            if(!checked||!SampleStore::canEdit(session_)||busy_)return;
            hole->sampleType=0;hole->quality=SampleQcIdentity();hole->sampleId=ordinaryId;
            *qualityValid=true;idMode(false);form->ldtSampleID->setText(ordinaryId);form->widget_2->setEnabled(false);
            dialogDirty_=true;(*populate)();updateProjectChecks();
        });
    fillControls();
    ++qualityChoicesRequest_;
    emit requestQualityChoices(qualityChoicesRequest_); // 只读快照，关闭/取消不预约、不写库。

    connect(form->ldtSampleID, &QLineEdit::textEdited, dialog, [this]() { dialogDirty_ = true; });
    connect(form->btnSave, &QPushButton::clicked, dialog,
            [this, form, hole, dialog, editedRow,editedRackId,qualityValid]()
            {
                if (editing_.id!=editedRackId || row_!=editedRow || !canEditHole(editedRow))
                {
                    return;
                }
                if (hole->sampleType==2)
                {
                    auto checked=SampleQcPreset::recheck(qualityChoiceSnapshot(),hole->quality.selected);
                    if (!*qualityValid || !checked.ok())
                    { report(checked.message.isEmpty()?tr("请重新选择质控品和批次。"):checked.message);return; }
                }
                for (int row = 0; row < form->tblSetting->rowCount(); ++row)
                {
                    auto *count = qobject_cast<QComboBox *>(form->tblSetting->cellWidget(row, 1));
                    auto *rate = qobject_cast<QComboBox *>(form->tblSetting->cellWidget(row, 3));
                    if (!count->lineEdit()->hasAcceptableInput() || rate->currentIndex() < 0)
                    {
                        report(tr("次数须为0至99整数，且须选择已配置的稀释标签。"));
                        return;
                    }
                    if (hole->tasks[row].repeats != count->currentText().toInt() ||
                        hole->tasks[row].rate != rate->currentText())
                    {
                        hole->tasks[row].states.clear();
                    }
                    hole->tasks[row].repeats = count->currentText().toInt();
                    hole->tasks[row].rate = rate->currentText();
                }
                hole->sampleId = hole->sampleType==2?hole->quality.selected.sampleId:form->ldtSampleID->text();
                editing_.holes[editedRow] = *hole;
                dialogDirty_ = false;
                dirty_ = true;
                refreshTable();
                refreshProjectButtons();
                dialog->accept();
            });
    connect(form->btnCancle, &QPushButton::clicked, dialog,
            [this, dialog]()
            {
                dialogDirty_ = false;
                dialog->reject();
            });
    dialog->allowClose = [this, dialog]()
    {
        if (busy_)
        {
            return false;
        }
        return !dialogDirty_ || confirm(dialog, tr("放弃未保存的样本测试设置？"));
    };
    connect(dialog, &QDialog::finished, this,
            [this]()
            {
                dialog_.clear();
                dialogDirty_ = false;
                refreshProjects();
            });
    (*populate)();
    dialogDirty_ = false;
    maskOpen(dialog);
    actions();
}
void SamplePage::repeat()
{
    if (dialog_ || !canEditHole(row_))
    {
        return;
    }
    const SampleHole source = editing_.holes[row_];
    if (source.sampleType != 0 || source.sampleId.isEmpty() || source.tasks.isEmpty())
    {
        report(tr("批量样本测试须先设置样本号和普通项目任务。"));
        return;
    }
    auto *dialog = new SampleEditor(this);
    dialog->setObjectName("sampleRepeatDialog");
    dialog_ = dialog;
    auto form = std::make_shared<Ui::MaskDlgRepeatSetting>();
    form->setupUi(dialog);
    form->ldtTimes->setText("1");
    form->ckxAuto->setText(tr("自动编号"));
    form->ckxAuto->setCheckState(Qt::Checked);
    const int limit = editing_.holes.size();
    for (int number = 1; number <= 10; ++number)
    {
        if (auto *button = dialog->findChild<QPushButton *>(QString("btnNum_%1").arg(number)))
        {
            button->setEnabled(number <= limit);
            if (number > limit)
            {
                button->setText("/");
                button->setStyleSheet("QPushButton{background:lightgray;}");
            }
            else
            {
                connect(button, &QPushButton::clicked, dialog,
                        [this, form, number]()
                        {
                            form->ldtTimes->setText(QString::number(number));
                            dialogDirty_ = true;
                        });
            }
        }
    }
    connect(form->ckxAuto, QOverload<>::of(&CusCheckBox::signalClicked), dialog,
            [this]() { dialogDirty_ = true; });
    const int start = row_;
    connect(form->btnSave, &QPushButton::clicked, dialog,
            [this, dialog, form, source, start]()
            {
                if (!canEditHole(start))
                {
                    return;
                }
                bool numeric = false;
                const int sourceId = source.sampleId.toInt(&numeric);
                const int count = form->ldtTimes->text().toInt();
                if (form->ckxAuto->isChecked() &&
                    (!numeric || sourceId < 0 ||
                     sourceId + qMin(count, editing_.holes.size() - start) - 1 > 9999))
                {
                    report(tr("自动编号需数字样本号且结果不超过4位；可关闭自动编号仅复制任务。"));
                    return;
                }
                for (int row = start + 1; row < qMin(start + count, editing_.holes.size()); ++row)
                {
                    if (!canEditHole(row) || editing_.holes[row].sampleType!=0)
                    {
                        report(tr("批量目标含质控、正在测试或不可修改孔位，草稿未改动。"));
                        return;
                    }
                }
                for (int row = start + 1; row < qMin(start + count, editing_.holes.size()); ++row)
                {
                    auto &hole = editing_.holes[row];
                    hole.tasks = source.tasks;
                    if (form->ckxAuto->isChecked())
                    {
                        const int number = hole.number;
                        hole = source;
                        hole.number = number;
                        hole.sampleId = QString::number(sourceId + row - start);
                    }
                    hole.revise = true;
                    for (SampleTask &task : hole.tasks)
                    {
                        task.states.clear(); // 新复制预设不能继承已完成/错误的执行观测。
                    }
                }
                dirty_ = true;
                dialogDirty_ = false;
                refreshTable();
                refreshProjectButtons();
                dialog->accept();
            });
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->allowClose = [this, dialog]()
    { return !busy_ && (!dialogDirty_ || confirm(dialog, tr("放弃批量设置？"))); };
    connect(dialog, &QDialog::finished, this,
            [this]()
            {
                dialog_.clear();
                dialogDirty_ = false;
                refreshProjects();
            });
    dialogDirty_ = false;
    maskOpen(dialog);
    actions();
}
void SamplePage::combinations()
{
    if (dialog_ || busy_ || !SampleStore::canEdit(session_))
    {
        return;
    }
    auto *dialog = new SampleEditor(this);
    dialog->setObjectName("sampleCombinationDialog");
    dialog_ = dialog;
    auto form = std::make_shared<Ui::MaskDlgCombaSetting>();
    form->setupUi(dialog);
    auto current = std::make_shared<SampleCombination>();
    form->cbk_multiCtrl->setEnabled(false);
    form->cbk_multiCtrl->setToolTip(tr("多瓶质控依赖项目试剂映射，尚未移植。"));
    form->ldtCombaName->setMaxLength(64);
    auto display = std::make_shared<std::function<void()>>();
    *display = [this, form, current, dialog]()
    {
        form->ldtCombaName->setText(current->name);
        form->lstDisplay->clear();
        for (const auto &value : projects_)
        {
            if (!value.enabled)
            {
                continue;
            }
            auto *button = new QPushButton(value.name, form->lstDisplay);
            button->setFixedSize(140, 40);
            button->setCheckable(true);
            button->setEnabled(false);
            button->setStyleSheet(projectStyle());
            int count = 0;
            for (qint64 id : current->orderedProjectIds)
            {
                if (id == value.id)
                {
                    ++count;
                }
            }
            button->setChecked(count > 0);
            auto *item = new QListWidgetItem(form->lstDisplay);
            item->setSizeHint(QSize(144, 44));
            form->lstDisplay->setItemWidget(item, button);
            auto *number = new QComboBox(form->lstDisplay);
            number->setObjectName("combinationCount_" + QString::number(value.id));
            number->setProperty("projectId", value.id);
            number->addItems({"0", "1", "2", "3", "10", "18", "54", "90"});
            number->setEditable(true);
            number->lineEdit()->setValidator(new QIntValidator(0, 99, number));
            number->setCurrentText(QString::number(count));
            number->setFixedSize(140, 40);
            number->setView(new QListView);
            auto *numberItem = new QListWidgetItem(form->lstDisplay);
            numberItem->setSizeHint(QSize(144, 44));
            form->lstDisplay->setItemWidget(numberItem, number);
            connect(number, &QComboBox::currentTextChanged, dialog, [this]() { dialogDirty_ = true; });
        }
    };
    auto list = [this, form, current]()
    {
        QSignalBlocker blocker(form->listComba);
        form->listComba->clear();
        for (const auto &combination : snapshot_.combinations)
        {
            auto *item = new QListWidgetItem(combination.name, form->listComba);
            item->setData(Qt::UserRole, combination.id);
            if (combination.id == current->id)
            {
                form->listComba->setCurrentItem(item);
            }
        }
    };
    connect(form->listComba, &QListWidget::currentItemChanged, dialog,
            [this, form, current, display](QListWidgetItem *item, QListWidgetItem *previous)
            {
                if (!item)
                {
                    return;
                }
                if (dialogDirty_)
                {
                    QSignalBlocker blocker(form->listComba);
                    form->listComba->setCurrentItem(previous);
                    report(tr("组合草稿未保存，请保存或取消后切换。"));
                    return;
                }
                for (const auto &combination : snapshot_.combinations)
                {
                    if (combination.id == item->data(Qt::UserRole).toLongLong())
                    {
                        *current = combination;
                        (*display)();
                        break;
                    }
                }
            });
    connect(form->btnAdd, &QPushButton::clicked, dialog,
            [this, current, display]()
            {
                if (dialogDirty_)
                {
                    report(tr("当前组合有未保存修改。"));
                    return;
                }
                *current = SampleCombination();
                (*display)();
                dialogDirty_ = true;
            });
    connect(form->ldtCombaName, &QLineEdit::textEdited, dialog, [this]() { dialogDirty_ = true; });
    connect(form->btnSave, &QPushButton::clicked, dialog,
            [this, current, form]()
            {
                if (busy_ || !SampleStore::canEdit(session_))
                {
                    return;
                }
                SampleCombination combination = *current;
                combination.name = form->ldtCombaName->text();
                combination.orderedProjectIds.clear();
                for (auto *number : form->lstDisplay->findChildren<QComboBox *>())
                {
                    if (!number->lineEdit()->hasAcceptableInput())
                    {
                        report(tr("组合次数须为0至99整数。"));
                        return;
                    }
                    for (int count = 0; count < number->currentText().toInt(); ++count)
                    {
                        combination.orderedProjectIds.append(number->property("projectId").toLongLong());
                    }
                }
                setBusy(true);
                ++request_;
                QMetaObject::invokeMethod(store_, "saveCombination", Qt::QueuedConnection,
                                          Q_ARG(qint64, request_), Q_ARG(SampleCombination, combination));
            });
    connect(form->btnDel, &QPushButton::clicked, dialog,
            [this, current, dialog]()
            {
                if (busy_ || !SampleStore::canEdit(session_) || !current->id)
                {
                    return;
                }
                if (!confirm(dialog, tr("删除当前本地组合？")))
                {
                    return;
                }
                setBusy(true);
                ++request_;
                QMetaObject::invokeMethod(store_, "removeCombination", Qt::QueuedConnection,
                                          Q_ARG(qint64, request_), Q_ARG(qint64, current->id));
            });
    connect(form->btnCancel, &QPushButton::clicked, dialog,
            [this, dialog]()
            {
                dialogDirty_ = false;
                dialog->reject();
            });
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::reject);
    dialog->allowClose = [this, dialog]()
    { return !busy_ && (!dialogDirty_ || confirm(dialog, tr("放弃未保存的组合设置？"))); };
    connect(dialog, &QDialog::finished, this,
            [this]()
            {
                dialog_.clear();
                dialogDirty_ = false;
                refreshProjects();
            });
    if (!snapshot_.combinations.isEmpty())
    {
        *current = snapshot_.combinations.first();
    }
    list();
    (*display)();
    dialogDirty_ = false;
    maskOpen(dialog);
    actions();
}

SampleQcPreset::Snapshot SamplePage::qualityChoiceSnapshot() const
{
    auto snapshot=qualityChoices_;
    snapshot.ready = snapshot.ready && qualityProtectionBound_;
    snapshot.projects.clear();
    for(const ProjectRecord &project:projects_)
        snapshot.projects.append({project.id,project.name,project.enabled});
    return snapshot;
}
void SamplePage::setQualityChoices(qint64 request, SampleQcPreset::Snapshot choices, QString message)
{
    if(request!=qualityChoicesRequest_ || !qualityProtectionBound_)return;
    qualityChoices_=choices;qualityChoicesError_=message;
    if(!choices.ready)report(message.isEmpty()?tr("质控选择资料读取失败，原草稿保留。"):message);
    emit qualityChoicesChanged(); // dialog接收者关闭即断开，不更新其他孔。
}

void SamplePage::invalidateQualityChoices(QString message)
{
    ++qualityChoicesRequest_; // 已在途的旧选择回执永久失效。
    qualityProtectionBound_ = false;
    qualityChoices_ = SampleQcPreset::Snapshot();
    qualityChoicesError_ = message;
    emit qualityChoicesChanged();
    actions();
    if (!message.isEmpty()) report(message);
}
