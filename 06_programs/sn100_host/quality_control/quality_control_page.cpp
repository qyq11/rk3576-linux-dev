#include "quality_control_page.h"
#include "qc_history_presenter.h"
#include "cusmessagebox.h"
#include "ui_frm_quality.h"
#include "ui_mask_dlg_add_control.h"
#include "ui_mask_dlg_control_batch_setting.h"
#include "ui_quality_control_add_wgt.h"
#include "ui_quality_control_item_add_project_wgt.h"
#include <QAtomicInteger>
#include <QAction>
#include <QCalendarWidget>
#include <QCloseEvent>
#include <QDate>
#include <QDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>
#include <memory>

namespace
{
QAtomicInteger<qint64> qcRequestSequence(1000);
qint64 nextRequest() { return qcRequestSequence.fetchAndAddRelaxed(1) + 1; }
QString qcStyle()
{
    // 原public/stylecolor.cpp button/list/table2/combobox公开主题值；
    // 同本工程已迁项目页。原Windows实际活动主题仍单独待对照。
    return QStringLiteral(
        ".QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
        ".QPushButton:hover{background-color:rgba(42,118,242,170);}"
        ".QPushButton:pressed{background-color:rgba(135,206,250,255);}"
        "QListWidget{background:transparent;color:#444444;border:1px solid #E4E8EB;border-radius:5px;}"
        "QListWidget::item{height:40px;color:#444444;}"
        "QListWidget::item:selected{background:rgba(183,215,255,255);color:white;}"
        "QTableWidget{background:transparent;border:1px solid #E4E8EB;border-radius:5px;}"
        "QTableWidget::item{border:0;border-bottom:1px solid #DDDDDD;}"
        "QTableView::item:selected{background:rgba(183,215,255,255);}"
        "QHeaderView::section{color:#444444;height:40px;background:transparent;border:1px solid #DDDDDD;}"
        "QComboBox{border:1px solid #E4E8EB;border-radius:5px;padding:2px 4px;background:transparent;color:#444444;}"
        "QComboBox:disabled{background:#E4E4E4;}"
        "QComboBox QAbstractItemView{border:none;color:#444444;}"
        "QComboBox QAbstractItemView::item{height:50px;}"
        "QComboBox QAbstractItemView::item:selected{background:rgba(183,215,255,255);}"
        "QComboBox::drop-down{width:20px;border:none;}"
        "QComboBox::down-arrow{width:25px;height:25px;image:url(:/image/arrow_drop_down.svg);}");
}
class QcDialog : public QDialog
{
public:
    explicit QcDialog(QWidget *parent) : QDialog(parent) {}
    std::function<bool()> mayClose;
    bool forceClose = false;
    void reject() override
    {
        if (forceClose || !mayClose || mayClose()) QDialog::reject();
    }
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (!forceClose && mayClose && !mayClose()) event->ignore();
        else QDialog::closeEvent(event);
    }
};
void prepareDialog(QcDialog *dialog, CusDialogMask *mask)
{
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setStyleSheet(qcStyle());
#ifdef Q_OS_LINUX
    for (QWidget *widget : dialog->findChildren<QWidget *>())
    {
        QFont font = widget->font();
        if (font.family().contains(QStringLiteral("苹方")))
        {
            font.setFamily(QStringLiteral("Noto Sans CJK SC"));
            widget->setFont(font);
        }
    }
#endif
    mask->setupForm();
    dialog->show();
    mask->showAnimation();
}
void configureTable(QTableWidget *table)
{
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->hideColumn(0);
}
void put(QTableWidget *table, int row, int column, const QString &text, qint64 id = 0)
{
    auto *item = new QTableWidgetItem(text);
    item->setData(Qt::UserRole, id);
    item->setTextAlignment(Qt::AlignCenter);
    table->setItem(row, column, item);
    table->setRowHeight(row, 45);
}
bool sameRule(const QcRule &a, const QcRule &b)
{
    return a.id == b.id && a.used == b.used && a.upperLimit == b.upperLimit &&
        a.lowerLimit == b.lowerLimit && a.upperBound == b.upperBound &&
        a.lowerBound == b.lowerBound && a.sdValue == b.sdValue &&
        a.target == b.target && a.strategies == b.strategies;
}
}

struct QualityControlPage::DialogState
{
    QPointer<QcDialog> manager;
    QPointer<QcDialog> add;
    QPointer<QcDialog> chooser;
    QPointer<QcDialog> batch;
    QPointer<CusMessageBox> warning;
    std::unique_ptr<Ui::MaskDlgAddControl> managerUi;
    std::unique_ptr<Ui::QualityControlAdd> addUi;
    std::unique_ptr<Ui::QualityControlItemAddProject> chooserUi;
    std::unique_ptr<Ui::MaskDlgControlBatchSetting> batchUi;
    QcControl draft;
    qint64 associationId = 0;
    qint64 ruleId = 0;
    qint64 batchControlId = 0;
    qint64 batchEditId = 0;
    bool managerRendering = false;
    bool batchRendering = false;
};

QualityControlPage::QualityControlPage(QcStore *store, QWidget *parent) :
    QWidget(parent), ui(new Ui::frmQuality), dialogs_(new DialogState), store_(store)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("QualityControlPage"));
    setFixedSize(1751, 968);
    setStyleSheet(qcStyle());
    ui->wgtControlTableBg->setRadius(10);
    ui->wgtChartBg->setRadius(10);
    ui->wgtControlTableBg->setBackgroundColor(Qt::white);
    ui->wgtChartBg->setBackgroundColor(Qt::white);
    ui->lstControl->setFrameShape(QFrame::NoFrame);
    ui->lstControl->setStyleSheet(QStringLiteral(
        "QListWidget{font:bold 18px 'Noto Sans CJK SC';background:transparent;border:none;}"
        "QListWidget::item{color:#444444;height:50px;}"
        "QListWidget::item:selected{background:rgba(183,215,255,255);color:white;}"));
    ui->lstProject->setFrameShape(QFrame::NoFrame);
    ui->lstProject->setSelectionMode(QAbstractItemView::NoSelection);
    ui->qualityChartList->setSelectionMode(QAbstractItemView::NoSelection);
    ui->qualityChartList->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->qualityChartList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->qualityChartList->setToolTip(tr("当前及前批历史结果只读；单位未知不换算、不进行临床失控判定。"));
    connect(ui->qualityChartList->verticalScrollBar(), &QScrollBar::rangeChanged,
            ui->qualityChartListScrollbar, &QScrollBar::setRange);
    connect(ui->qualityChartListScrollbar, &QScrollBar::valueChanged,
            ui->qualityChartList->verticalScrollBar(), &QScrollBar::setValue);
    ui->qualityChartListScrollbar->setSliderColor(QColor(34,34,34,50));
    ui->qualityChartListScrollbar->setBackgroundColor(Qt::transparent);
    ui->qualityChartListScrollbar->setCanvasColor(Qt::transparent);
    ui->qualityFunMenu->setBackgroundColor(QColor(28,28,28));
    QLinearGradient gradient(0,0,0,ui->qualityFunMenu->height());
    gradient.setColorAt(0,QColor(255,255,255,0));
    gradient.setColorAt(1,QColor(255,255,255,25));
    ui->qualityFunMenu->setGraBackgroundColor(gradient);
    ui->qualityFunMenu->setShadowEffect(Qt::AlignLeft);
    for (QtMaterialFlatButton *button : {ui->btnAddControl,ui->btnEditControl,ui->btnPrint})
    {
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setBackgroundColor(QColor(127,127,127));
        button->setForegroundColor(Qt::white);
        button->setUseThemeColors(false);
    }
    ui->btnPrint->hide(); // 原init_component也隐藏，不能因移植恢复实体打印。
    history_=new QcHistoryPresenter(ui->qualityChartList,this);
    connect(history_,&QcHistoryPresenter::requestHistory,store,&QcStore::loadHistory,Qt::QueuedConnection);
    connect(store,&QcStore::historyReady,history_,&QcHistoryPresenter::accept,Qt::QueuedConnection);
    connect(store,&QcStore::historyError,history_,&QcHistoryPresenter::reject,Qt::QueuedConnection);
    connect(history_,&QcHistoryPresenter::busyChanged,this,[this](bool busy)
    {historyBusy_=busy;updateEnabledState();});
    connect(history_,&QcHistoryPresenter::statusChanged,this,&QualityControlPage::report);
    connect(history_,&QcHistoryPresenter::finished,this,[this](bool success)
    {emit operationFinished(QStringLiteral("loadHistory"),success);});
    connect(ui->btnAddControl,&QPushButton::clicked,this,&QualityControlPage::openManager);
    connect(ui->btnEditControl,&QPushButton::clicked,this,[this](){ openBatches(); });
    connect(ui->lstControl,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item)
    {
        if (rendering_ || !item) return;
        selectedId_ = item->data(Qt::UserRole).toLongLong();
        renderProjects();
    });
    connect(this,&QualityControlPage::requestLoad,store,&QcStore::load,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestCatalog,store,&QcStore::setCatalog,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestSaveControl,store,&QcStore::saveControl,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestRemoveControl,store,&QcStore::removeControl,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestProjects,store,&QcStore::setProjects,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestSaveBatch,store,&QcStore::saveBatch,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestRemoveBatch,store,&QcStore::removeBatch,Qt::QueuedConnection);
    connect(this,&QualityControlPage::requestSwitchBatch,store,&QcStore::switchBatch,Qt::QueuedConnection);
    connect(store,&QcStore::ready,this,[this](const QString &){ ready_=true; reload(); });
    connect(store,&QcStore::snapshotReady,this,[this](qint64 request,QcControls controls)
    {
        if (request != requestId_) return;
        controls_ = controls;
        if (selectAfterLoad_) selectedId_ = selectAfterLoad_;
        selectAfterLoad_ = 0;
        if (!control(selectedId_)) selectedId_ = controls_.isEmpty() ? 0 : controls_.first().id;
        render();
        setBusy(false);
        const QString completed = operation_.isEmpty() ? QStringLiteral("load") : operation_;
        operation_.clear();
        report(tr("质控元数据已读取；所选项目历史另行只读加载，临床规则未启用。"));
        emit operationFinished(completed,true);
    });
    connect(store,&QcStore::succeeded,this,[this](qint64 request,const QString &operation,qint64 id)
    {
        if (request != requestId_) return;
        if (operation == QStringLiteral("saveControl"))
        {
            selectAfterLoad_ = id;
            dialogs_->draft = QcControl(); // 仅提交成功后放弃旧草稿。
            if (dialogs_->add) { dialogs_->add->forceClose=true; dialogs_->add->close(); }
        }
        if (dialogs_->chooser && operation == QStringLiteral("setProjects"))
        { dialogs_->chooser->forceClose=true; dialogs_->chooser->close(); }
        if (dialogs_->batch && (operation == QStringLiteral("saveBatch") || operation == QStringLiteral("removeBatch")))
        {
            dialogs_->batchEditId=0;
            dialogs_->batchUi->ldtBatch->clear();
            dialogs_->batchUi->ldtOutDate->clear();
            dialogs_->batchUi->btn_add_item->setText(tr("添加"));
        }
        reload();
    });
    connect(store,&QcStore::error,this,[this](qint64 request,const QString &operation,const QString &message)
    {
        if (catalogRequestId_>0 && request == catalogRequestId_)
        {
            report(tr("项目目录读取失败：%1；项目选择暂不可用。").arg(message),true);
            projects_.clear();
            showWarning(status_);
            return;
        }
        if (request != requestId_ && request != 0) return;
        setBusy(false);
        renderBatches(); // 失败回显持久快照，不把未提交默认值当成功。
        if (dialogs_->manager)
        {
            const QSignalBlocker blocker(dialogs_->managerUi->cbx_pihao);
            const QcControl *saved=control(dialogs_->draft.id);
            int index=-1;
            if (saved) for (const QcBatch &batch:saved->batches)
                if (batch.current) index=dialogs_->managerUi->cbx_pihao->findData(batch.id);
            dialogs_->managerUi->cbx_pihao->setCurrentIndex(index);
        }
        report(tr("%1失败：%2；输入保留，可修正或重试。").arg(operation,message),true);
        showWarning(status_);
        emit operationFinished(operation,false);
    });
    connect(store,&QcStore::warning,this,[this](const QString &message)
    {
        // A local delete can succeed while conservative catalog protection is
        // retained. Ordinary list reloads must not disguise that recovery work.
        referenceWarning_=message;
        report(message,true);
    });
    setBusy(true);
}
QualityControlPage::~QualityControlPage()
{
    delete history_; // 先断开历史卡回调，再由原ui销毁子控件。
    history_=nullptr;
    // 子窗口destroyed回调仍会访问DialogState，先销毁窗口再释放该状态。
    if (dialogs_->warning) {dialogs_->warning->disconnect(this);delete dialogs_->warning.data();}
    for (QcDialog *dialog:{dialogs_->chooser.data(),dialogs_->add.data(),dialogs_->batch.data(),dialogs_->manager.data()})
        if (dialog) {dialog->disconnect(this);delete dialog;}
    delete dialogs_;
    delete ui;
}
const QcControl *QualityControlPage::control(qint64 id) const
{
    for (const QcControl &entry:controls_) if (entry.id==id) return &entry;
    return nullptr;
}
void QualityControlPage::report(const QString &message,bool error)
{
    status_=message;
    if (!referenceWarning_.isEmpty() && !message.contains(referenceWarning_))
        status_+=tr(" 待核对项目引用：%1。重新打开后须通过引用核对；勿删除保护登记。").arg(referenceWarning_);
    setToolTip(status_);
    emit statusChanged(status_,error || !referenceWarning_.isEmpty());
}
bool QualityControlPage::hasWarning() const { return !dialogs_->warning.isNull(); }
bool QualityControlPage::isBusy() const { return busy_ || historyBusy_ || hasWarning(); }
void QualityControlPage::showWarning(const QString &message)
{
    if (hasWarning()) return;
    QWidget *parent=nullptr;
    for (QcDialog *dialog:{dialogs_->add.data(),dialogs_->chooser.data(),dialogs_->batch.data(),dialogs_->manager.data()})
        if (dialog) {parent=dialog;break;}
    // 初始化失败没有业务弹窗时由启动入口处理；不在后台打开额外遮罩。
    if (!parent) return;
    auto *warning=new CusMessageBox(parent);
    dialogs_->warning=warning;
    warning->setObjectName(QStringLiteral("QcWarningMessage"));
    warning->setAttribute(Qt::WA_DeleteOnClose);
    connect(warning,&QDialog::finished,this,[this]()
    {
        dialogs_->warning.clear();
        emit busyChanged(isBusy());
    });
    warning->openWarningBox(message);
    emit busyChanged(true);
}
void QualityControlPage::setBusy(bool busy)
{
    busy_=busy;
    updateEnabledState();
}
void QualityControlPage::updateEnabledState()
{
    const bool blocked=busy_||historyBusy_;
    ui->btnAddControl->setEnabled(ready_&&!blocked);
    ui->btnEditControl->setEnabled(ready_&&!blocked&&selectedId_>0);
    ui->lstControl->setEnabled(!blocked);
    ui->lstProject->setEnabled(!blocked);
    for (QcDialog *dialog:{dialogs_->manager.data(),dialogs_->add.data(),dialogs_->chooser.data(),dialogs_->batch.data()})
        if (dialog) dialog->setEnabled(!blocked);
    emit busyChanged(isBusy());
}
qint64 QualityControlPage::begin(const QString &operation)
{
    operation_=operation;
    requestId_=nextRequest();
    setBusy(true);
    return requestId_;
}
void QualityControlPage::reload()
{
    requestId_=nextRequest();
    setBusy(true);
    emit requestLoad(requestId_);
}
void QualityControlPage::setProjects(QcProjectRefs projects)
{
    projects_=projects;
    catalogRequestId_=nextRequest();
    emit requestCatalog(catalogRequestId_,projects_);
}
void QualityControlPage::render()
{
    rendering_=true;
    ui->lstControl->clear();
    int selectedRow=-1;
    for (const QcControl &entry:controls_)
    {
        if (!entry.used) continue;
        auto *item=new QListWidgetItem(entry.name,ui->lstControl);
        item->setData(Qt::UserRole,entry.id); // 过滤is_used后仍以稳定ID查数据。
        item->setSizeHint(QSize(250,50));
        if (entry.id==selectedId_) selectedRow=ui->lstControl->count()-1;
    }
    if (selectedRow<0 && ui->lstControl->count()) selectedRow=0;
    ui->lstControl->setCurrentRow(selectedRow);
    selectedId_=selectedRow<0?0:ui->lstControl->item(selectedRow)->data(Qt::UserRole).toLongLong();
    rendering_=false;
    renderProjects();
    renderManager();
    renderBatches();
}
void QualityControlPage::renderProjects()
{
    ui->lstProject->clear();
    const QcControl *entry=control(selectedId_);
    if (!entry)
    {selectedAssociationId_=0;history_->selectAssociation(0,0);updateEnabledState();return;}
    QPushButton *selectedButton=nullptr;
    qint64 firstAssociation=0;
    for (const QcAssociation &association:entry->projects)
    {
        auto *item=new QListWidgetItem(ui->lstProject);
        item->setData(Qt::UserRole,association.id);
        item->setSizeHint(QSize(124,54));
        auto *button=new QPushButton(association.projectName,ui->lstProject);
        button->setFixedSize(120,50);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setStyleSheet(QStringLiteral("QPushButton{background:#FFFFFF;color:#444444;border-radius:5px;border:1px solid #DDDDDD;}QPushButton:checked{color:#FFFFFF;background:rgba(42,118,242,255);}"));
        button->setToolTip(tr("关联ID：%1；点击只读当前及前批历史结果，临床判定未启用。").arg(association.id));
        button->setProperty("qcAssociationId",association.id);
        ui->lstProject->setItemWidget(item,button);
        if (!firstAssociation) {firstAssociation=association.id;selectedButton=button;}
        if (association.id==selectedAssociationId_) selectedButton=button;
        connect(button,&QPushButton::clicked,this,[this,id=association.id]()
        {
            if (isBusy()) return;
            selectedAssociationId_=id;
            history_->selectAssociation(selectedId_,id);
        });
    }
    if (selectedButton)
    {
        selectedButton->setChecked(true);
        selectedAssociationId_=selectedButton->property("qcAssociationId").toLongLong();
    }
    else selectedAssociationId_=0;
    history_->selectAssociation(selectedId_,selectedAssociationId_);
    updateEnabledState();
}

void QualityControlPage::rememberRuleDraft()
{
    if (!dialogs_->manager || dialogs_->managerRendering || !dialogs_->ruleId) return;
    auto *form=dialogs_->managerUi.get();
    for (QcAssociation &association:dialogs_->draft.projects)
        for (QcRule &rule:association.rules) if (rule.id==dialogs_->ruleId)
        {
            rule.upperLimit=form->ldt_upper_limit->text();
            rule.lowerLimit=form->ldt_loweer_limit->text();
            rule.upperBound=form->ldt_upper_bound->text();
            rule.lowerBound=form->ldt_lower_bound->text();
            rule.sdValue=form->ldt_sd_value->text();
            rule.target=form->ldt_target_value->text();
            QList<QComboBox*> combos={form->cbx_r1_2s,form->cbx_r1_3s,form->cbx_r2_2s,form->cbx_r4_1s,form->cbx_r5_05s,form->cbx_rR_4s,form->cbx_r7t,form->cbx_r10x,form->cbx_limit_control};
            for (int index=0;index<combos.size();++index) rule.strategies[index]=combos[index]->currentIndex();
        }
}
bool QualityControlPage::isDirty() const
{
    if (dialogs_->add && (!dialogs_->addUi->ldt_factor->text().isEmpty() || !dialogs_->addUi->ldt_control_name->text().isEmpty())) return true;
    if (dialogs_->batch)
    {
        QString savedName,savedDate;
        const QcControl *savedControl=control(dialogs_->batchControlId);
        if (savedControl) for (const QcBatch &batch:savedControl->batches)
            if (batch.id==dialogs_->batchEditId) {savedName=batch.name;savedDate=batch.expirationDate;}
        if (dialogs_->batchUi->ldtBatch->text()!=savedName || dialogs_->batchUi->ldtOutDate->text()!=savedDate) return true;
    }
    if (dialogs_->chooser)
    {
        const QcControl *saved=control(dialogs_->draft.id);
        QSet<qint64> before,after;
        if (saved) for (const QcAssociation &association:saved->projects) before.insert(association.projectId);
        for (QListWidgetItem *item:dialogs_->chooserUi->listProjectWidget->selectedItems()) after.insert(item->data(Qt::UserRole).toLongLong());
        if (before!=after) return true;
    }
    if (!dialogs_->manager) return false;
    const QcControl *saved=control(dialogs_->draft.id);
    if (!saved) return false;
    auto *form=dialogs_->managerUi.get();
    if (form->ldtControlName->text()!=saved->name || form->ldtFactor->text()!=saved->manufacturer) return true;
    const_cast<QualityControlPage*>(this)->rememberRuleDraft();
    for (const QcAssociation &association:dialogs_->draft.projects)
        for (const QcRule &rule:association.rules)
            for (const QcAssociation &oldAssociation:saved->projects)
                for (const QcRule &old:oldAssociation.rules)
                    if (old.id==rule.id && !sameRule(old,rule)) return true;
    return false;
}
bool QualityControlPage::discardDraft(const QString &action)
{
    if (isBusy()) { report(hasWarning()?tr("请先关闭质控警告，再%1。").arg(action):tr("质控数据正在读写，请完成后再%1。").arg(action),true); return false; }
    if (!isDirty()) return true;
    const auto answer=QMessageBox::question(this,tr("未保存的质控修改"),tr("质控窗口有未保存的输入。放弃输入并%1？").arg(action),QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel);
    return answer==QMessageBox::Discard;
}
bool QualityControlPage::requestLeave(const QString &action)
{
    if (!discardDraft(action)) return false;
    for (QcDialog *dialog:{dialogs_->chooser.data(),dialogs_->add.data(),dialogs_->batch.data(),dialogs_->manager.data()})
        if (dialog) {dialog->forceClose=true;dialog->close();}
    return true;
}
void QualityControlPage::closeEvent(QCloseEvent *event)
{
    if (!requestLeave(tr("关闭页面"))) event->ignore();
    else QWidget::closeEvent(event);
}

void QualityControlPage::openManager()
{
    if (!ready_ || isBusy() || dialogs_->manager) return;
    auto *dialog=new QcDialog(this);
    dialogs_->manager=dialog;
    dialogs_->managerUi.reset(new Ui::MaskDlgAddControl);
    auto *form=dialogs_->managerUi.get();
    form->setupUi(dialog);
    dialog->setObjectName(QStringLiteral("QcControlManager"));
    // 原btnClose x1610超出1471宽白卡；仅移到白卡右上，保证1600屏可点。
    form->btnClose->move(form->view_frame->width()-51,0);
    form->ckxValid->setText(tr("是否有效"));
    form->ckxValid->setEnabled(false);
    form->ckxValid->setToolTip(tr("原保存流程未写入此状态；本切片只回显，不擅自改变有效性。"));
    form->ldtControlName->setMaxLength(16);
    form->ldtFactor->setMaxLength(16);
    // 原.ui为只读且输入过滤器跳过只读框；按本轮修改业务要求开放原输入框。
    for (QLineEdit *edit : {form->ldtControlName, form->ldtFactor, form->ldt_upper_limit,
         form->ldt_loweer_limit, form->ldt_upper_bound, form->ldt_lower_bound,
         form->ldt_sd_value, form->ldt_target_value}) edit->setReadOnly(false);
    configureTable(form->tab_qualitied_projs);
    configureTable(form->tab_quality_result_type);
    form->widget_2->setToolTip(tr("仅保存原规则元数据；这些设置尚未通过真实质控/临床效力验证。"));
    dialog->mayClose=[this](){return discardDraft(tr("关闭质控品管理"));};
    connect(form->btnClose,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(form->btn_cancel,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(form->btn_add_control_item,&QPushButton::clicked,this,&QualityControlPage::showAddControl);
    connect(form->btn_add_control_project,&QPushButton::clicked,this,&QualityControlPage::showProjectChooser);
    connect(form->btn_save,&QPushButton::clicked,this,&QualityControlPage::saveManager);
    connect(form->btn_delete,&QPushButton::clicked,this,[this]()
    {
        if (!busy_ && dialogs_->draft.id) emit requestRemoveControl(begin(QStringLiteral("removeControl")),dialogs_->draft.id);
    });
    connect(form->lstControl,&QListWidget::currentItemChanged,dialog,[this](QListWidgetItem *item)
    {
        if (dialogs_->managerRendering || !item || busy_) return;
        const qint64 id=item->data(Qt::UserRole).toLongLong();
        if (id==dialogs_->draft.id) return;
        if (!discardDraft(tr("切换质控品"))) {renderManager();return;}
        dialogs_->draft=QcControl();
        dialogs_->draft.id=id;
        dialogs_->associationId=0;
        dialogs_->ruleId=0;
        renderManager();
    });
    connect(form->tab_qualitied_projs,&QTableWidget::itemSelectionChanged,dialog,[this]()
    {
        if (dialogs_->managerRendering || busy_) return;
        rememberRuleDraft();
        auto *table=dialogs_->managerUi->tab_qualitied_projs;
        dialogs_->associationId=table->currentRow()<0?0:table->item(table->currentRow(),0)->data(Qt::UserRole).toLongLong();
        dialogs_->ruleId=0;
        showRuleDraft();
    });
    connect(form->tab_quality_result_type,&QTableWidget::itemSelectionChanged,dialog,[this]()
    {
        if (dialogs_->managerRendering || busy_) return;
        rememberRuleDraft();
        auto *table=dialogs_->managerUi->tab_quality_result_type;
        dialogs_->ruleId=table->currentRow()<0?0:table->item(table->currentRow(),0)->data(Qt::UserRole).toLongLong();
        showRuleDraft();
    });
    connect(form->cbx_pihao,QOverload<int>::of(&QComboBox::activated),dialog,[this](int index)
    {
        if (!busy_ && index>=0) emit requestSwitchBatch(begin(QStringLiteral("switchBatch")),dialogs_->draft.id,dialogs_->managerUi->cbx_pihao->itemData(index).toLongLong());
    });
    connect(dialog,&QObject::destroyed,this,[this](){dialogs_->draft=QcControl();dialogs_->associationId=0;dialogs_->ruleId=0;});
    renderManager();
    prepareDialog(dialog,form->mask);
}
void QualityControlPage::renderManager()
{
    if (!dialogs_->manager) return;
    auto *form=dialogs_->managerUi.get();
    rememberRuleDraft();
    qint64 id=dialogs_->draft.id?dialogs_->draft.id:selectedId_;
    const QcControl *saved=control(id);
    if (!saved && !controls_.isEmpty()) saved=&controls_.first();
    QcControl oldDraft=dialogs_->draft;
    const QString oldName=form->ldtControlName->text(),oldManufacturer=form->ldtFactor->text();
    dialogs_->managerRendering=true;
    form->lstControl->clear();
    int row=-1;
    for (const QcControl &entry:controls_)
    {
        auto *item=new QListWidgetItem(entry.name,form->lstControl);
        item->setData(Qt::UserRole,entry.id);
        if (saved&&entry.id==saved->id) row=form->lstControl->count()-1;
    }
    form->lstControl->setCurrentRow(row);
    dialogs_->draft=saved?*saved:QcControl();
    if (saved && oldDraft.id==saved->id && !oldDraft.name.isEmpty())
    {
        dialogs_->draft.name=oldName;
        dialogs_->draft.manufacturer=oldManufacturer;
        for (QcAssociation &association:dialogs_->draft.projects)
            for (QcRule &rule:association.rules)
                for (const QcAssociation &oldAssociation:oldDraft.projects)
                    for (const QcRule &oldRule:oldAssociation.rules)
                        if (oldRule.id==rule.id)
                        {
                            QcRule preserved=oldRule;
                            // 切批或改关联后，规则原字段未变时接续新版本；保留页面输入。
                            // 任何原字段变化仍保留旧令牌，由后台拒绝覆盖外部更新。
                            if(!rule.originalRoot.isEmpty()&&oldRule.originalRoot==rule.originalRoot&&
                               oldRule.originalValues==rule.originalValues)
                                preserved.originalFingerprint=rule.originalFingerprint;
                            rule=preserved;
                        }
    }
    form->ldtControlName->setText(dialogs_->draft.name);
    form->ldtFactor->setText(dialogs_->draft.manufacturer);
    form->ckxValid->setCheckState(dialogs_->draft.used?Qt::Checked:Qt::Unchecked);
    form->tab_qualitied_projs->setRowCount(dialogs_->draft.projects.size());
    int selectedAssociationRow=-1;
    row=0;
    for (const QcAssociation &association:dialogs_->draft.projects)
    {
        put(form->tab_qualitied_projs,row,0,QString::number(association.id),association.id);
        put(form->tab_qualitied_projs,row,1,association.projectName,association.projectId);
        if (association.id==dialogs_->associationId) selectedAssociationRow=row;
        ++row;
    }
    if (selectedAssociationRow<0 && row) selectedAssociationRow=0;
    if (selectedAssociationRow>=0) form->tab_qualitied_projs->selectRow(selectedAssociationRow);
    dialogs_->associationId=selectedAssociationRow<0?0:dialogs_->draft.projects[selectedAssociationRow].id;
    {
        const QSignalBlocker blocker(form->cbx_pihao);
        form->cbx_pihao->clear();
        int current=-1;
        for (const QcBatch &batch:dialogs_->draft.batches)
        {
            form->cbx_pihao->addItem(batch.name,batch.id);
            if (batch.current) current=form->cbx_pihao->count()-1;
        }
        form->cbx_pihao->setCurrentIndex(current); // 没有current时不猜默认、不写库。
    }
    dialogs_->managerRendering=false;
    showRuleDraft();
}
void QualityControlPage::showRuleDraft()
{
    if (!dialogs_->manager) return;
    auto *form=dialogs_->managerUi.get();
    dialogs_->managerRendering=true;
    const QcAssociation *selected=nullptr;
    for (const QcAssociation &association:dialogs_->draft.projects) if (association.id==dialogs_->associationId) selected=&association;
    form->tab_quality_result_type->setRowCount(selected?selected->rules.size():0);
    const QcRule *chosen=nullptr;
    int row=0;
    if (selected) for (const QcRule &rule:selected->rules)
    {
        put(form->tab_quality_result_type,row,0,QString::number(rule.id),rule.id);
        auto *checkbox=new CusCheckBox(form->tab_quality_result_type);
        checkbox->setText(tr("选用"));
        checkbox->setCheckState(rule.used?Qt::Checked:Qt::Unchecked);
        const qint64 ruleId=rule.id;
        connect(checkbox,QOverload<bool>::of(&CusCheckBox::signalClicked),this,[this,ruleId](bool used)
        {
            for (QcAssociation &association:dialogs_->draft.projects)
                for (QcRule &entry:association.rules) if (entry.id==ruleId) entry.used=used;
        });
        form->tab_quality_result_type->setCellWidget(row,1,checkbox);
        put(form->tab_quality_result_type,row,2,rule.name);
        if (rule.id==dialogs_->ruleId || (!dialogs_->ruleId && row==0))
        {chosen=&rule; form->tab_quality_result_type->selectRow(row);}
        ++row;
    }
    dialogs_->ruleId=chosen?chosen->id:0;
    form->ldt_upper_limit->setText(chosen?chosen->upperLimit:QString());
    form->ldt_loweer_limit->setText(chosen?chosen->lowerLimit:QString());
    form->ldt_upper_bound->setText(chosen?chosen->upperBound:QString());
    form->ldt_lower_bound->setText(chosen?chosen->lowerBound:QString());
    form->ldt_sd_value->setText(chosen?chosen->sdValue:QString());
    form->ldt_target_value->setText(chosen?chosen->target:QString());
    QList<QComboBox*> combos={form->cbx_r1_2s,form->cbx_r1_3s,form->cbx_r2_2s,form->cbx_r4_1s,form->cbx_r5_05s,form->cbx_rR_4s,form->cbx_r7t,form->cbx_r10x,form->cbx_limit_control};
    for (int index=0;index<combos.size();++index) combos[index]->setCurrentIndex(chosen?chosen->strategies.value(index):0);
    form->widget_2->setEnabled(chosen);
    dialogs_->managerRendering=false;
}
void QualityControlPage::saveManager()
{
    if (busy_ || !dialogs_->manager || !dialogs_->draft.id) return;
    rememberRuleDraft();
    dialogs_->draft.name=dialogs_->managerUi->ldtControlName->text();
    dialogs_->draft.manufacturer=dialogs_->managerUi->ldtFactor->text();
    QString message=QcStore::validateControl(dialogs_->draft);
    for (const QcAssociation &association:dialogs_->draft.projects)
        if (message.isEmpty()) message=QcStore::validateRules(association.rules);
    if (!message.isEmpty()) {report(message,true);showWarning(message);emit operationFinished(QStringLiteral("saveControl"),false);return;}
    emit requestSaveControl(begin(QStringLiteral("saveControl")),dialogs_->draft);
}
void QualityControlPage::showAddControl()
{
    if (busy_ || dialogs_->add) return;
    // 新增成功会切换管理器选择，先让操作者决定旧质控品草稿的去留。
    if (!discardDraft(tr("新增质控品"))) return;
    if (dialogs_->manager)
    {
        const qint64 selected=dialogs_->draft.id;
        dialogs_->draft=QcControl();
        dialogs_->draft.id=selected;
        renderManager();
    }
    QWidget *parent=dialogs_->manager?static_cast<QWidget*>(dialogs_->manager.data()):this;
    auto *dialog=new QcDialog(parent);
    dialogs_->add=dialog;
    dialogs_->addUi.reset(new Ui::QualityControlAdd);
    auto *form=dialogs_->addUi.get();
    form->setupUi(dialog);
    dialog->setObjectName(QStringLiteral("QcControlAdd"));
    form->ldt_factor->setMaxLength(16);
    form->ldt_control_name->setMaxLength(16);
    dialog->mayClose=[this](){return discardDraft(tr("关闭新增质控品"));};
    connect(form->btnClose,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(form->btnSave,&QPushButton::clicked,dialog,[this,form]()
    {
        if (busy_) return;
        QcControl entry;
        entry.name=form->ldt_control_name->text();
        entry.manufacturer=form->ldt_factor->text();
        const QString message=QcStore::validateControl(entry);
        if (!message.isEmpty()) {report(message,true);showWarning(message);emit operationFinished(QStringLiteral("saveControl"),false);return;}
        emit requestSaveControl(begin(QStringLiteral("saveControl")),entry);
    });
    prepareDialog(dialog,form->mask);
}
void QualityControlPage::showProjectChooser()
{
    if (busy_ || dialogs_->chooser || !dialogs_->draft.id) return;
    auto *dialog=new QcDialog(dialogs_->manager.data());
    dialogs_->chooser=dialog;
    dialogs_->chooserUi.reset(new Ui::QualityControlItemAddProject);
    auto *form=dialogs_->chooserUi.get();
    form->setupUi(dialog);
    dialog->setObjectName(QStringLiteral("QcProjectChooser"));
    form->listProjectWidget->setSelectionMode(QAbstractItemView::MultiSelection);
    form->listProjectWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    form->listProjectWidget->setStyleSheet(QStringLiteral("QListWidget::item{height:50px;border-bottom:1px solid lightgray;background:transparent;}QListWidget::item:selected{color:black;background:#b7d7ff;}"));
    for (const QcProjectRef &project:projects_)
    {
        auto *item=new QListWidgetItem(project.name,form->listProjectWidget);
        item->setData(Qt::UserRole,project.id);
        item->setTextAlignment(Qt::AlignCenter);
        for (const QcAssociation &association:dialogs_->draft.projects)
            if (association.projectId==project.id) item->setSelected(true);
    }
    // 顶X、Cancel、Esc均reject，不提交空集合；只有明确Confirm才执行集合差异。
    dialog->mayClose=[this](){return !isBusy();};
    connect(form->btnClose,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(form->btn_close,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(form->btnSave,&QPushButton::clicked,dialog,[this,form]()
    {
        if (busy_) return;
        QcProjectRefs selected;
        for (QListWidgetItem *item:form->listProjectWidget->selectedItems())
            for (const QcProjectRef &project:projects_)
                if (project.id==item->data(Qt::UserRole).toLongLong()) selected.append(project);
        emit requestProjects(begin(QStringLiteral("setProjects")),dialogs_->draft.id,selected);
    });
    prepareDialog(dialog,form->mask);
}
void QualityControlPage::openBatches(qint64 controlId)
{
    if (!ready_ || isBusy() || dialogs_->batch) return;
    if (!controlId) controlId=selectedId_;
    if (!control(controlId)) return;
    auto *dialog=new QcDialog(this);
    dialogs_->batch=dialog;
    dialogs_->batchControlId=controlId;
    dialogs_->batchEditId=0;
    dialogs_->batchUi.reset(new Ui::MaskDlgControlBatchSetting);
    auto *form=dialogs_->batchUi.get();
    form->setupUi(dialog);
    dialog->setObjectName(QStringLiteral("QcBatchManager"));
    configureTable(form->tab_quality_control_sns);
    form->ldtControlName->setReadOnly(true);
    form->ldtBatch->setMaxLength(32);
    form->ldtOutDate->setReadOnly(false);
    form->ldtOutDate->setPlaceholderText(QStringLiteral("yyyy/MM/dd"));
    form->ldtBatch->setToolTip(tr("选择已有批号后可编辑并保存修改；表格右键“新增批号”或Ctrl+N清空输入后添加。当前/上次和历史引用受保护。"));
    dialog->mayClose=[this](){return discardDraft(tr("关闭批次管理"));};
    connect(form->btnClose,&QPushButton::clicked,dialog,&QDialog::reject);
    connect(form->btn_add_item,&QPushButton::clicked,dialog,[this,form]()
    {
        if (busy_) return;
        QcBatch batch;
        if(const QcControl *saved=control(dialogs_->batchControlId))
            for(const QcBatch &existing:saved->batches)if(existing.id==dialogs_->batchEditId)batch=existing;
        batch.id=dialogs_->batchEditId;
        batch.controlId=dialogs_->batchControlId;
        batch.name=form->ldtBatch->text();
        batch.expirationDate=form->ldtOutDate->text();
        const QString message=QcStore::validateBatch(batch);
        if (!message.isEmpty()) {report(message,true);showWarning(message);emit operationFinished(QStringLiteral("saveBatch"),false);return;}
        emit requestSaveBatch(begin(QStringLiteral("saveBatch")),batch);
    });
    connect(form->btn_delete,&QPushButton::clicked,dialog,[this,form]()
    {
        const int row=form->tab_quality_control_sns->currentRow();
        if (busy_ || row<0) return;
        emit requestRemoveBatch(begin(QStringLiteral("removeBatch")),form->tab_quality_control_sns->item(row,0)->data(Qt::UserRole).toLongLong());
    });
    connect(form->tab_quality_control_sns,&QTableWidget::itemSelectionChanged,dialog,[this,form]()
    {
        if (busy_ || dialogs_->batchRendering) return;
        const int row=form->tab_quality_control_sns->currentRow();
        if (row<0) return;
        const qint64 id=form->tab_quality_control_sns->item(row,0)->data(Qt::UserRole).toLongLong();
        if (id==dialogs_->batchEditId) return;
        if (!discardDraft(tr("切换批号"))) {renderBatches();return;}
        dialogs_->batchEditId=id;
        form->ldtBatch->setText(form->tab_quality_control_sns->item(row,1)->text());
        form->ldtOutDate->setText(form->tab_quality_control_sns->item(row,2)->text());
        form->btn_add_item->setText(tr("保存修改"));
    });
    auto *newBatch=new QAction(tr("新增批号"),dialog);
    newBatch->setObjectName(QStringLiteral("qcNewBatchAction"));
    newBatch->setShortcut(QKeySequence(QStringLiteral("Ctrl+N")));
    dialog->addAction(newBatch);
    form->tab_quality_control_sns->addAction(newBatch);
    form->tab_quality_control_sns->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(newBatch,&QAction::triggered,dialog,[this,form]()
    {
        if (!discardDraft(tr("新增批号"))) return;
        // 清空选择会同步发选择信号，必须整组切换，避免把旧字段当新草稿再弹确认。
        const QSignalBlocker blocker(form->tab_quality_control_sns);
        dialogs_->batchRendering=true;
        dialogs_->batchEditId=0;
        form->tab_quality_control_sns->clearSelection();
        form->tab_quality_control_sns->setCurrentItem(nullptr);
        form->ldtBatch->clear();
        form->ldtOutDate->clear();
        form->btn_add_item->setText(tr("添加"));
        dialogs_->batchRendering=false;
    });
    connect(form->cbx_default_batch,QOverload<int>::of(&QComboBox::activated),dialog,[this,form](int index)
    {
        if (!busy_&&!dialogs_->batchRendering&&index>=0)
            emit requestSwitchBatch(begin(QStringLiteral("switchBatch")),dialogs_->batchControlId,form->cbx_default_batch->itemData(index).toLongLong());
    });
    connect(form->btn_calendar_set,&QPushButton::clicked,dialog,[this,form,dialog]()
    {
        auto *calendarDialog=new QDialog(dialog);
        calendarDialog->setAttribute(Qt::WA_DeleteOnClose);
        calendarDialog->setWindowTitle(tr("选择有效期"));
        auto *layout=new QVBoxLayout(calendarDialog);
        auto *calendar=new QCalendarWidget(calendarDialog);
        const QDate value=QDate::fromString(form->ldtOutDate->text(),QStringLiteral("yyyy/MM/dd"));
        calendar->setSelectedDate(value.isValid()?value:QDate::currentDate());
        layout->addWidget(calendar);
        connect(calendar,&QCalendarWidget::activated,calendarDialog,[calendarDialog,form](QDate date)
        { form->ldtOutDate->setText(date.toString(QStringLiteral("yyyy/MM/dd")));calendarDialog->accept(); });
        calendarDialog->open();
    });
    renderBatches();
    prepareDialog(dialog,form->mask);
}
void QualityControlPage::renderBatches()
{
    if (!dialogs_->batch) return;
    const QcControl *saved=control(dialogs_->batchControlId);
    auto *form=dialogs_->batchUi.get();
    dialogs_->batchRendering=true;
    form->ldtControlName->setText(saved?saved->name:QString());
    form->tab_quality_control_sns->setRowCount(saved?saved->batches.size():0);
    form->cbx_default_batch->clear();
    int row=0,current=-1;
    if (saved) for (const QcBatch &batch:saved->batches)
    {
        put(form->tab_quality_control_sns,row,0,QString::number(batch.id),batch.id);
        put(form->tab_quality_control_sns,row,1,batch.name,batch.id);
        put(form->tab_quality_control_sns,row,2,batch.expirationDate,batch.id);
        form->cbx_default_batch->addItem(batch.name,batch.id);
        if (batch.current) current=row;
        if (batch.id==dialogs_->batchEditId) form->tab_quality_control_sns->selectRow(row);
        ++row;
    }
    form->cbx_default_batch->setCurrentIndex(current);
    dialogs_->batchRendering=false;
}
