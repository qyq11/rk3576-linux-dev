#include "reagents_page.h"
#include "original_reagent_level_runtime.h"
#include "original_reagent_usage.h"
#include "cusdialogmask.h"
#include "cusmessagebox.h"
#include "qtmaterialflatbutton.h"
#include "ui_addbatchgroupdialog.h"
#include "ui_frmreagentmaterial.h"
#include "ui_maskdlggroupsetting.h"
#include "ui_maskdlgreagentcode.h"
#include "ui_maskdlgreagentlistsetting.h"
#include "ui_maskdlgreagentsetting.h"
#include "ui_cusmaskcalendar.h"
#include "alarm_history/calendar/cuscalendar.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QIntValidator>
#include <QRegularExpressionValidator>
#include <QMenu>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolTip>
#include <functional>
#include <memory>

class GuardedReagentDialog : public QDialog
{
public:
    explicit GuardedReagentDialog(QWidget* parent)
        : QDialog(parent)
    {
    }
    std::function<bool()> mayClose;
    void reject() override
    {
        if (!mayClose || mayClose())
            QDialog::reject();
    }
};

// 复用原日历和遮罩；不阻塞主线程，关闭孔位页时由父子关系一起释放。
class ReagentCalendarPopup : public QDialog
{
public:
    explicit ReagentCalendarPopup(QWidget *parent, const QDate &selected)
        : QDialog(parent), form_(new Ui::CusMaskCalendar)
    {
        form_->setupUi(this);
        setObjectName(QStringLiteral("reagentExpiryCalendar"));
        setAttribute(Qt::WA_DeleteOnClose);
        for (auto *widget : findChildren<QWidget*>())
        {
            // 日历晚于孔位页创建，登记初始状态，供注销后禁用与重新登录恢复。
            widget->setProperty("reagentBaseEnabled", widget->isEnabled());
            QFont font=widget->font();
            if (font.family().contains(QStringLiteral("苹方")))
            { font.setFamily(QStringLiteral("Noto Sans CJK SC")); widget->setFont(font); }
            QString style=widget->styleSheet();
            style.replace(QStringLiteral("苹方 粗体"),QStringLiteral("Noto Sans CJK SC"));
            widget->setStyleSheet(style);
        }
        form_->widget->updateCalendar(selected);
        connect(form_->widget,&CusCalendar::signalCloseDialog,this,&ReagentCalendarPopup::reject);
        connect(form_->mask,&CusDialogMask::closeFinished,this,[this]()
        { closingFinished_=true; QDialog::reject(); });
        form_->mask->setupForm();
    }
    ~ReagentCalendarPopup() override { delete form_; }
    CusCalendar *calendar() const { return form_->widget; }
    void openAsync() { show(); form_->mask->showAnimation(); }
    void reject() override
    {
        if (closingFinished_) QDialog::reject();
        else form_->mask->closeAnimation();
    }
protected:
    void closeEvent(QCloseEvent *event) override
    {
        if (closingFinished_) QDialog::closeEvent(event);
        else { event->ignore(); reject(); }
    }
private:
    Ui::CusMaskCalendar *form_;
    bool closingFinished_=false;
};

static QString editorStyle()
{
    // Reference buttonStyle/listWidgetStyle/tableStyle2/comboboxStyle with the
    // shipped theme_config visual values; never load reference runtime config.
    return QStringLiteral(
        "QPushButton{background:rgba(42,118,242,255);color:white;border-radius:4px;padding:2px;}"
        "QPushButton:hover{background:rgba(42,118,242,170);}QPushButton:pressed{background:rgba(135,206,250,"
        "255);}"
        "QPushButton:disabled{background:#DDDDDD;color:#888888;}"
        "QListWidget{border:1px solid #DDDDDD;background:transparent;}"
        "QListWidget::item{height:41px;color:#444444;}QListWidget::item:selected{background:#B7D7FF;}"
        "QTableWidget{background:transparent;border:1px solid #E4E8EB;}"
        "QTableWidget::item{height:40px;color:#444444;border-bottom:1px solid #DDDDDD;}"
        "QTableWidget::item:selected{background:#B7D7FF;}"
        "QHeaderView::section{background:white;color:#444444;height:40px;border:0;border-bottom:1px solid "
        "#DDDDDD;}"
        "QComboBox{background:transparent;border:1px solid #DDDDDD;border-radius:4px;padding:2px;}"
        "QLineEdit{color:#444444;}QLabel{color:#444444;}");
}

ReagentsPage::ReagentsPage(ReagentStore* store, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::frmReagentMaterial)
{
    store_=store;
    ui->setupUi(this);
    ui->reagentPanel->setGeometry(70, 39, 984, 843); // Original SN10020 branch.
    ui->reagentInfo->setRadius(10);
    ui->reagentFunMenu->setBackgroundColor(QColor(28, 28, 28));
    QLinearGradient gradient(0, 0, 0, ui->reagentFunMenu->height());
    gradient.setColorAt(0, QColor(255, 255, 255, 0));
    gradient.setColorAt(1, QColor(255, 255, 255, 25));
    ui->reagentFunMenu->setGraBackgroundColor(gradient);
    ui->reagentFunMenu->setShadowEffect(Qt::AlignLeft);
    const auto menu = ui->reagentFunMenu->findChildren<QtMaterialFlatButton*>();
    for (auto button : menu)
    {
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setUseThemeColors(false);
        button->setBackgroundColor(QColor(127, 127, 127));
        button->setForegroundColor(Qt::white);
    }
    const QString red = QStringLiteral("QPushButton{background:rgba(232,51,35,255);color:white;border-radius:"
                                       "4px;padding:2px;}QPushButton:hover{background:rgba(242,89,76,200);}");
    ui->btnUpdatePriority->setStyleSheet(red);
    ui->btnChangeHole->setStyleSheet(red);
    ui->btnDectOne->setStyleSheet(red);
    const QString tabs = QStringLiteral(
        ".QPushButton{color:#AAAAAA;border:none;background:transparent;border-top:5px solid "
        "transparent;}.QPushButton:checked{color:#444444;border-top:5px solid rgba(232,51,35,255);}");
    ui->wgtTabs->setStyleSheet(tabs);
    ui->btnReagentInfoPage->setStyleSheet(tabs);
    ui->btnReagentCountPage->setStyleSheet(tabs);
    ui->lineEdit_13->setStyleSheet(
        "border:none;background:transparent;border-left:10px solid rgba(232,51,35,255);");
    ui->lineEdit_15->setStyleSheet(ui->lineEdit_13->styleSheet());
    ui->btnChange->hide();
    ui->btnScan->setEnabled(false);
    ui->lblAmount->setToolTip(QStringLiteral("双击编辑本地装载量/余量；不是液面实测"));
    ui->lblAmount->installEventFilter(this);
    ui->lblBottleName->setToolTip(QStringLiteral("双击管理本地瓶型"));
    ui->lblBottleName->installEventFilter(this);
    connect(ui->reagentPanel, &wgtReagentPanel::holeSelected, this, &ReagentsPage::selectHole);
    connect(ui->reagentPanel, &wgtReagentPanel::holeEditRequested, this,
        [this](QString name)
        {
            selectHole(name);
            openHoleEditor();
        });
    connect(ui->btnReagentSet, &QPushButton::clicked, this, &ReagentsPage::openReagentEditor);
    connect(ui->btnReagentInfoSet, &QPushButton::clicked, this, &ReagentsPage::openHoleEditor);
    connect(ui->btnGroupSet, &QPushButton::clicked, this, &ReagentsPage::openBatchEditor);
    connect(ui->btnUsaged,&QAbstractButton::clicked,this,&ReagentsPage::openUsageDialog);
    connect(ui->btnReagentInfoPage, &QPushButton::clicked, this,
        [this]() { ui->stkReagentInfo->setCurrentIndex(0); });
    connect(ui->btnReagentCountPage, &QPushButton::clicked, this,
        [this]()
        {
            refreshTestCounts();
            ui->stkReagentInfo->setCurrentIndex(1);
        });
    connect(ui->btnDectOne, &QPushButton::clicked, this, [this]()
    { requestOriginalDeviceOperation(QStringLiteral("detectOne"),QStringLiteral("是否进行试剂探测？")); });
    connect(ui->btnDect, &QAbstractButton::clicked, this, [this]()
    { requestOriginalDeviceOperation(QStringLiteral("detectAll"),QStringLiteral("是否进行试剂探测？")); });
    connect(ui->btnChangeHole, &QPushButton::clicked, this, [this]()
    { requestOriginalDeviceOperation(QStringLiteral("fillHole"),QStringLiteral("是否充满当前选中试剂?")); });
    connect(ui->btnUpdatePriority, &QPushButton::clicked, this, [this]()
    { requestOriginalDeviceOperation(QStringLiteral("prioritizeHole"),QStringLiteral("是否优先使用当前选中试剂?")); });
    connect(ui->btn_clearPanel, &QAbstractButton::clicked, this, [this]()
    { requestOriginalDeviceOperation(QStringLiteral("clearPanel"),QStringLiteral("是否清空所有试剂设置？")); });
    // 参考源码默认禁用扫码、隐藏出口；现有SN450程序可见入口，SN500适用条件另核。
    connect(ui->btnScan, &QAbstractButton::clicked, this, [this]()
    { showOperationMessage(QStringLiteral("扫码"),QStringLiteral("当前机型的扫码启用条件和资料回读仍待核对，未开放此操作。")); });
    connect(ui->btnChange, &QAbstractButton::clicked, this, [this]()
    { showOperationMessage(QStringLiteral("转至出口"),QStringLiteral("当前机型的转至出口条件及完成通知仍待核对，未开放此操作。")); });
    connect(ui->btn_restore, &QAbstractButton::clicked, this, [this]()
    { showOperationMessage(QStringLiteral("恢复"),QStringLiteral("参考实现没有执行恢复操作，当前发行版的恢复行为仍待核对；本次未改资料或发送命令。")); });
    connect(ui->btn_calibrationmode, &QAbstractButton::clicked, this, [this]()
    { showOperationMessage(QStringLiteral("容量标定"),QStringLiteral("原容量标定模式、孔位编排和对应机型高度换算尚未完整接入，未开始标定。")); });
    deviceOperationTimer_=new QTimer(this);
    deviceOperationTimer_->setSingleShot(true);
    connect(deviceOperationTimer_,&QTimer::timeout,this,[this]()
    {
        if (!pendingDeviceRequest_) return;
        const QString operation=pendingDeviceOperation_;
        pendingDeviceRequest_=0; pendingDeviceOperation_.clear(); deviceOperations_.clear();
        store_->setOriginalDeviceOperationContext(0,QStringList());
        setBusy(false);
        showOperationMessage(operation,QStringLiteral("操作在等待时间内未得到完整确认，执行结果待核对；未重发。请先核设备与原资料回读，再重新开放操作。"));
        emit operationFinished(operation,false);
    });
    for (QTableWidget* table : { ui->projectUseageTable, ui->otherReagentUseageTable })
    {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setRowCount(0);
        table->setStyleSheet(editorStyle());
    }
    connect(this, &ReagentsPage::requestLoad, store, &ReagentStore::load, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestSession, store, &ReagentStore::setSession, Qt::QueuedConnection);
    connect(store, &ReagentStore::ready, this, [this]() { emit requestLoad(); });
    connect(store, &ReagentStore::originalProfileReady, this, [this](const QString &root, const ReagentProjects &projects)
    {
        originalProfile_=true; projects_=projects;
        setProperty("originalProfile",true); setProperty("originalRoot",root);
    });
    connect(store,&ReagentStore::closed,this,[this]()
    {
        ready_=false;
        applyPermissions();
        emit readyChanged(false);
    });
    connect(this, &ReagentsPage::requestSaveReagent, store, &ReagentStore::saveReagent, Qt::QueuedConnection);
    connect(
        this, &ReagentsPage::requestRemoveReagent, store, &ReagentStore::removeReagent, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestSaveBottle, store, &ReagentStore::saveBottle, Qt::QueuedConnection);
    connect(
        this, &ReagentsPage::requestRemoveBottle, store, &ReagentStore::removeBottle, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestSaveBatch, store, &ReagentStore::saveBatch, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestSaveOriginalBatchGroupEnabled, store,
            &ReagentStore::saveOriginalBatchGroupEnabled, Qt::QueuedConnection);
    connect(store, &ReagentStore::originalProjectGroupEnableCommitted, this,
            [this](const ReagentProject &project) { pendingOriginalGroupCommit_=project; });
    connect(this, &ReagentsPage::requestRemoveBatch, store, &ReagentStore::removeBatch, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestSaveBarcode, store, &ReagentStore::saveBarcode, Qt::QueuedConnection);
    connect(
        this, &ReagentsPage::requestRemoveBarcode, store, &ReagentStore::removeBarcode, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestSaveHole, store, &ReagentStore::saveHole, Qt::QueuedConnection);
    connect(this, &ReagentsPage::requestClearHole, store, &ReagentStore::clearHole, Qt::QueuedConnection);
    connect(store, &ReagentStore::loaded, this,
        [this](ReagentSnapshot snapshot)
        {
            snapshot_ = snapshot;
            ready_ = true;
            setBusy(pendingDeviceRequest_!=0);
            applyPermissions();
            ui->reagentPanel->setSnapshot(snapshot_);
            selectHole(selectedHole_);
            refreshTestCounts();
            consumeOriginalLevelSnapshot();
            ReagentProject savedGroupProject;
            if (!pendingOriginalGroupCommit_.originalFileName.isEmpty())
                for (const auto &project : projects_)
                    // xuhao可由UP改写；提交后的重读按原工作根和实际文件名定位。
                    if (project.originalRoot==pendingOriginalGroupCommit_.originalRoot &&
                        project.originalFileName==pendingOriginalGroupCommit_.originalFileName &&
                        !project.originalFingerprint.isEmpty())
                    {
                        savedGroupProject=project;
                        pendingOriginalGroupCommit_=ReagentProject();
                        break;
                    }
            emit readyChanged(true);
            emit snapshotChanged(snapshot_);
            if (!savedGroupProject.originalFingerprint.isEmpty()) emit originalProjectGroupEnabledSaved(savedGroupProject);
            if (!pendingSavedOperation_.isEmpty())
            {
                const QString operation=pendingSavedOperation_; pendingSavedOperation_.clear();
                emit operationMessage(operation,operation==QStringLiteral("保存试剂组开关") ?
                    QStringLiteral("试剂组开关已保存并重新读取；未同步仪器，请先完成项目重读并核对设备同步。") :
                    originalProfile_ ? QStringLiteral("原数据库保存并重新读取完成；设备同步状态另行核对") : QStringLiteral("本地保存并重新读取完成；未同步仪器"));
                emit operationFinished(operation,true);
            }
        });
    connect(store, &ReagentStore::succeeded, this,
        [this](QString op)
        {
            dirty_ = false;
            pendingSavedOperation_=op;
        });
    connect(store, &ReagentStore::error, this,
        [this](QString op, QString message)
        {
            if (!pendingSavedOperation_.isEmpty())
            {
                message=QStringLiteral("数据库已保存，但重新读取失败：")+message;
                pendingSavedOperation_.clear();
            }
            setBusy(pendingDeviceRequest_!=0);
            emit operationMessage(op, message);
            emit operationFinished(op, false);
            QToolTip::showText(mapToGlobal(QPoint(width() / 2, 50)), op + "：" + message, this);
        });
    applyPermissions();
    initializeOriginalTaskReagentDisplay();
}
ReagentsPage::~ReagentsPage()
{
    if (dialog_)
        delete dialog_;
    delete ui;
}
void ReagentsPage::setProjects(ReagentProjects projects)
{
    if (originalProfile_)
    {
        // 主窗口的项目快照通知只用于使估算失效；不能用早期catalog编号覆盖原项目。
        for (auto it=snapshot_.holeTestCounts.begin();it!=snapshot_.holeTestCounts.end();++it)
        { it->available=false; it->reason=QStringLiteral("原项目资料已变化，正在重新读取用量。"); }
        for (auto &entry : snapshot_.projectTestCounts)
        { entry.second.available=false; entry.second.reason=QStringLiteral("原项目资料已变化，正在重新读取用量。"); }
        refreshTestCounts(); selectHole(selectedHole_);
        reloadOriginal();
        return;
    }
    projects_ = projects;
}
void ReagentsPage::configureLayout(int count)
{
    ui->reagentPanel->configureLayout(count);
    if (count==42)
    {
        ui->reagentPanel->setGeometry(13,11,1111,941);
        selectedHole_=QStringLiteral("A_1");
    }
    setProperty("configuredReagentPositions",count);
}
bool ReagentsPage::canEdit() const
{
    return ready_ && !instrumentBusy_ && session_.authenticated && session_.actionReady && session_.role >= 0
        && session_.role <= 5;
}
void ReagentsPage::setSession(AccountSession session)
{
    const bool identityChanged=session_.name!=session.name || session_.role!=session.role ||
        session_.authenticated!=session.authenticated || session_.source!=session.source ||
        session_.originalPolicySource!=session.originalPolicySource || session_.grantedPowerIds!=session.grantedPowerIds;
    if (identityChanged) setDeviceOperations(0,QStringList());
    else if (!session.actionReady)
    {
        store_->setOriginalDeviceOperationContext(0,QStringList());
        deviceOperations_.clear();
    }
    session_ = session;
    emit requestSession(session);
    applyPermissions();
    if (dialog_)
        dialog_->setEnabled(!busy_);
}
void ReagentsPage::applyPermissions()
{
    ui->btnReagentSet->setEnabled(canEdit() && !busy_);
    ui->btnGroupSet->setEnabled(canEdit() && !busy_);
    ui->btnReagentInfoSet->setEnabled(canEdit() && !busy_);
    ui->btn_restore->setVisible(session_.role >= 4 && session_.role <= 5);
    ui->btn_clearPanel->setVisible(session_.role >= 4 && session_.role <= 5);
    ui->btn_calibrationmode->setVisible(session_.role >= 4 && session_.role <= 5);
    ui->btnChangeHole->setVisible(session_.role == 5);
    if (dialog_)
    {
        for (QAbstractButton* button : dialog_->findChildren<QAbstractButton*>())
            if (button->objectName() != "btnClose" && button->objectName() != "btnCancel")
                button->setEnabled(button->property("reagentBaseEnabled").toBool() && canEdit() && !busy_);
        for (QLineEdit* edit : dialog_->findChildren<QLineEdit*>())
            edit->setEnabled(edit->property("reagentBaseEnabled").toBool() && canEdit() && !busy_);
        for (QComboBox* box : dialog_->findChildren<QComboBox*>())
            box->setEnabled(box->property("reagentBaseEnabled").toBool() && canEdit() && !busy_);
        for (CusCheckBox* box : dialog_->findChildren<CusCheckBox*>())
            box->setEnabled(box->property("reagentBaseEnabled").toBool() && canEdit() && !busy_);
    }
}
void ReagentsPage::setBusy(bool busy)
{
    busy_ = busy;
    if (dialog_)
        dialog_->setEnabled(!busy);
    applyPermissions();
    emit busyChanged(busy);
    if (!busy_ && !dirty_ && pendingOriginalReload_)
        QTimer::singleShot(0,this,&ReagentsPage::reloadOriginal);
}
void ReagentsPage::showOperationMessage(const QString &operation, const QString &message)
{
    emit operationMessage(operation, message);
    QToolTip::showText(QCursor::pos(), message, this);
}

void ReagentsPage::setDeviceOperations(quint64 connection, QStringList operations)
{
    store_->setOriginalDeviceOperationContext(connection,operations);
    if (usageDialog_ && (!connection || connection!=deviceConnection_)) usageDialog_->invalidateDeviceData();
    if (pendingDeviceRequest_ && connection!=deviceConnection_)
    {
        const QString operation=pendingDeviceOperation_;
        pendingDeviceRequest_=0; pendingDeviceOperation_.clear(); deviceOperationTimer_->stop();
        setBusy(false);
        showOperationMessage(operation,QStringLiteral("连接已变化，原操作执行结果待核对；没有将新连接的回复归给旧操作。"));
        emit operationFinished(operation,false);
    }
    deviceConnection_=connection;
    deviceOperations_=connection ? operations : QStringList();
}

void ReagentsPage::requestOriginalDeviceOperation(const QString &operation, const QString &question)
{
    if (!originalProfile_ || !canEdit() || busy_ || dirty_ || dialog_ || pendingDeviceRequest_)
    {
        showOperationMessage(operation,QStringLiteral("当前账户、仪器占用状态或未保存的试剂资料不允许此操作。"));
        return;
    }
    if ((operation==QStringLiteral("fillHole") && session_.role!=5) ||
        (operation==QStringLiteral("clearPanel") && session_.role<4))
    {
        showOperationMessage(operation,QStringLiteral("当前账户没有此试剂操作权限。"));
        return;
    }
    if (!deviceConnection_ || !deviceOperations_.contains(operation) ||
        !receivers(SIGNAL(deviceOperationRequested(quint64,quint64,QString,QString))))
    {
        const QString blocker=operation==QStringLiteral("prioritizeHole") ? property("originalPriorityBlocker").toString() : QString();
        showOperationMessage(operation,blocker.isEmpty() ?
            QStringLiteral("主窗口尚未开放该操作的设备发送及完整回读流程，未修改资料或发送命令。") : blocker);
        return;
    }
    const quint64 connection=deviceConnection_;
    const QString hole=selectedHole_;
    CusMessageBox confirmation(this);
    confirmation.showQuestionBox(question);
    if (confirmation.getClickButton()!=QMessageBox::Yes) return;
    // 确认框打开期间仍会处理状态消息，确认后再次核对连接和权限。
    if (connection!=deviceConnection_ || !deviceOperations_.contains(operation) || !canEdit() || busy_)
    {
        showOperationMessage(operation,QStringLiteral("确认期间设备连接或操作条件已变化，未执行。"));
        return;
    }
    const quint64 request=++nextDeviceRequest_;
    if (!store_->authorizeOriginalDeviceOperation(request,connection,operation))
    {
        showOperationMessage(operation,QStringLiteral("设备操作许可已经变化，未提交请求。"));
        return;
    }
    pendingDeviceRequest_=request; pendingDeviceOperation_=operation;
    setBusy(true);
    deviceOperationTimer_->start(operation==QStringLiteral("detectOne") || operation==QStringLiteral("detectAll") ||
        operation==QStringLiteral("fillHole") ? 60000 : 10000);
    showOperationMessage(operation,QStringLiteral("正在请求主窗口核对设备条件；发送、执行和资料回读分别等待确认。"));
    emit deviceOperationRequested(pendingDeviceRequest_,connection,operation,hole);
}

void ReagentsPage::deviceOperationProgress(quint64 request, quint64 connection, QString message)
{
    if (!request || request!=pendingDeviceRequest_ || connection!=deviceConnection_) return;
    showOperationMessage(pendingDeviceOperation_,message);
}

void ReagentsPage::renewDeviceOperationWait(quint64 request, quint64 connection)
{
    if (!request || request!=pendingDeviceRequest_ || connection!=deviceConnection_) return;
    // 主窗口仅在当前探测孔的有效N消息、且原灯状态为操作中时调用，与原等待续时一致。
    if (pendingDeviceOperation_==QStringLiteral("detectOne") || pendingDeviceOperation_==QStringLiteral("detectAll") ||
        pendingDeviceOperation_==QStringLiteral("fillHole")) deviceOperationTimer_->start(60000);
}

void ReagentsPage::finishDeviceOperation(quint64 request, quint64 connection, bool success, QString message)
{
    if (!request || request!=pendingDeviceRequest_ || connection!=deviceConnection_) return;
    const QString operation=pendingDeviceOperation_;
    deviceOperationTimer_->stop(); pendingDeviceRequest_=0; pendingDeviceOperation_.clear();
    setBusy(false);
    showOperationMessage(operation,message);
    emit operationFinished(operation,success);
    if (success) reloadOriginal();
}

void ReagentsPage::refreshTestCounts()
{
    ui->projectUseageTable->setRowCount(0);
    // 原refreshUseageTable不显示稀释液区的可测次数，不能用装载量臆算。
    ui->otherReagentUseageTable->setRowCount(0);
    for (const auto &entry : snapshot_.projectTestCounts)
    {
        const int row=ui->projectUseageTable->rowCount();
        ui->projectUseageTable->insertRow(row);
        auto *name=new QTableWidgetItem(entry.first);
        auto *count=new QTableWidgetItem(entry.second.available ? QString::number(entry.second.tests) : QStringLiteral("待核"));
        name->setTextAlignment(Qt::AlignCenter); count->setTextAlignment(Qt::AlignCenter);
        count->setToolTip(entry.second.available ? QStringLiteral("按原已存液量和项目单次用量估算；不代替开始前的选瓶、批组和扣量校验。") : entry.second.reason);
        ui->projectUseageTable->setItem(row,0,name); ui->projectUseageTable->setItem(row,1,count);
        ui->projectUseageTable->setRowHeight(row,41);
    }
}

void ReagentsPage::openUsageDialog()
{
    if (!originalProfile_ || !ready_) return;
    if (dialog_) { dialog_->raise(); return; }
    if (!usageDialog_)
    {
        usageDialog_=new OriginalReagentUsageDialog(this);
        usageDialog_->setThemeStyle(editorStyle(),QColor(232,51,35));
        connect(usageDialog_,&OriginalReagentUsageDialog::feedbackRequested,this,[this](const QString &message,bool)
        { showOperationMessage(QStringLiteral("试剂耗材"),message); });
        connect(usageDialog_,&OriginalReagentUsageDialog::commandRequested,this,[this](const QByteArray &payload)
        {
            const QString operation=payload=="QH" ? QStringLiteral("queryUsage") : payload=="QI" ?
                QStringLiteral("queryDeviceKey") : payload.startsWith("J[") && payload.endsWith(']') ?
                QStringLiteral("updateUsage") : QString();
            if (operation.isEmpty() || payload.size()>128000 || !deviceConnection_ ||
                !deviceOperations_.contains(operation) ||
                !receivers(SIGNAL(usageCommandRequested(quint64,QByteArray))))
            {
                showOperationMessage(QStringLiteral("试剂耗材"),QStringLiteral("设备耗材收发和资料回写尚未开放，未发送；未知次数继续显示问号。"));
                return;
            }
            if (operation==QStringLiteral("updateUsage") && (!canEdit() || busy_))
            {
                showOperationMessage(QStringLiteral("试剂耗材"),QStringLiteral("当前账户或仪器状态不允许添加耗材，原输入保留。"));
                return;
            }
            emit usageCommandRequested(deviceConnection_,payload);
        });
    }
    QStringList names=snapshot_.originalProjectOrder;
    if (names.isEmpty()) for (const auto &project : projects_) names.append(project.name);
    usageDialog_->setProjects(names);
    usageDialog_->openUsage();
}

void ReagentsPage::updateOriginalUsage(quint64 connection,QHash<QString,QString> projectHas,QHash<QString,QString> projectUsed)
{
    if (!connection || connection!=deviceConnection_ || !usageDialog_) return;
    usageDialog_->setUsage(projectHas,projectUsed);
}

void ReagentsPage::updateOriginalDeviceKey(quint64 connection,QString key)
{
    if (!connection || connection!=deviceConnection_ || !usageDialog_) return;
    usageDialog_->setDeviceKey(key);
}

void ReagentsPage::finishOriginalUsageImport(quint64 connection,bool success)
{
    if (!connection || connection!=deviceConnection_ || !usageDialog_) return;
    usageDialog_->setImportResult(success);
}
void ReagentsPage::markDirty()
{
    dirty_ = true;
}
bool ReagentsPage::prepareToLeave()
{
    if (busy_)
        return false;
    if (dirty_)
    {
        CusMessageBox question(this);
        question.showQuestionBox(QStringLiteral("试剂编辑尚未保存，放弃本次修改？"));
        if (question.getClickButton() != QMessageBox::Yes)
            return false;
    }
    dirty_ = false;
    if (dialog_)
        dialog_->close();
    if (pendingOriginalReload_) QTimer::singleShot(0,this,&ReagentsPage::reloadOriginal);
    return true;
}
void ReagentsPage::closeEvent(QCloseEvent* event)
{
    if (prepareToLeave())
        QWidget::closeEvent(event);
    else
        event->ignore();
}
bool ReagentsPage::allowEditor()
{
    if (!canEdit() || busy_)
        return false;
    if (dialog_)
    {
        dialog_->raise();
        return false;
    }
    return true;
}
void ReagentsPage::initializeDialog(QDialog* dialog)
{
    for (QWidget* control : dialog->findChildren<QWidget*>())
        control->setProperty("reagentBaseEnabled", control->isEnabled());
    auto* guard = static_cast<GuardedReagentDialog*>(dialog);
    guard->mayClose = [this]()
    {
        if (busy_)
            return false;
        if (!dirty_)
            return true;
        CusMessageBox question(this);
        question.showQuestionBox(QStringLiteral("放弃未保存的试剂编辑？"));
        if (question.getClickButton() != QMessageBox::Yes)
            return false;
        dirty_ = false;
        return true;
    };
    dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setStyleSheet(editorStyle());
    dialog->installEventFilter(this);
    connect(dialog, &QDialog::finished, this,
        [this]()
        {
            dirty_ = false;
            dialog_.clear();
            if (pendingOriginalReload_ && !busy_)
                QTimer::singleShot(0,this,&ReagentsPage::reloadOriginal);
        });
    if (auto* mask = dialog->findChild<CusDialogMask*>("mask"))
    {
        if (auto* view = dialog->findChild<QFrame*>("view_frame"))
        {
            // Original reagent dialogs reach 995 logical pixels in height.
            // Fit the complete group uniformly to its current host; preserve
            // every child order/relative position and the source .ui geometry.
            const QSize available = window()->size() - QSize(40, 40);
            const double scale = qMin(1.0,
                qMin(double(available.width()) / view->width(), double(available.height()) / view->height()));
            if (scale < 1.0)
            {
                struct OriginalControl
                {
                    QWidget* widget;
                    QRect rectangle;
                    QFont font;
                };
                QList<OriginalControl> controls;
                for (QWidget* control : view->findChildren<QWidget*>())
                    controls.append({ control, control->geometry(), control->font() });
                for (const OriginalControl& control : controls)
                {
                    const QRect r = control.rectangle;
                    control.widget->setGeometry(qRound(r.x() * scale), qRound(r.y() * scale),
                        qRound(r.width() * scale), qRound(r.height() * scale));
                    QFont font = control.font;
                    if (font.pixelSize() > 0)
                        font.setPixelSize(qMax(8, qRound(font.pixelSize() * scale)));
                    else if (font.pointSizeF() > 0)
                        font.setPointSizeF(font.pointSizeF() * scale);
                    control.widget->setFont(font);
                }
                view->resize(qRound(view->width() * scale), qRound(view->height() * scale));
            }
            dialog->setProperty("reagentDialogScale", scale);
        }
        mask->setupForm();
        connect(mask, &CusDialogMask::closeFinished, dialog, &QDialog::reject);
    }
    dialog->open();
    if (auto* mask = dialog->findChild<CusDialogMask*>("mask"))
        mask->showAnimation();
}
void ReagentsPage::selectHole(QString name)
{
    selectedHole_ = name;
    ui->reagentPanel->selectHole(name);
    ReagentHole hole;
    ReagentRecord reagent;
    ReagentBottle bottle;
    ReagentBatch batch;
    for (const auto& h : snapshot_.holes)
        if (h.name == name)
            hole = h;
    for (const auto& r : snapshot_.reagents)
        if (r.id == hole.reagentId)
            reagent = r;
    for (const auto& b : snapshot_.bottles)
        if (b.id == hole.bottleId)
            bottle = b;
    for (const auto& b : snapshot_.batches)
        if (b.id == hole.batchId)
            batch = b;
    ui->lblHoleinfo->setText(name + QStringLiteral("试剂信息"));
    ui->lblName->setText(reagent.id ? reagent.name : QStringLiteral("none"));
    ui->lblBottleName->setText(bottle.id ? bottle.name : QStringLiteral("-"));
    ui->lblBatch->setText(snapshot_.originalProfile ? hole.originalBatch : batch.id ? batch.batchGroup : QStringLiteral("-"));
    ui->lblAmount->setText(hole.id ? QString::number(hole.remainingUl) : QStringLiteral("-"));
    ui->lblAmount->setToolTip(QString());
    ui->lblOutDate->setText(hole.expiry);
    ui->lblKeepTime->setText("-");
    ui->lblSetDate->setText(hole.changedAt.section('T', 0, 0));
    ui->lblSetTime->setText(hole.changedAt.section('T', 1, 1));
    const auto tests=snapshot_.holeTestCounts.value(name);
    ui->lblTests->setText(!tests.available || !tests.tests ? QStringLiteral("-") :
        tests.tests>999 ? QStringLiteral(">999") : QString::number(tests.tests));
    ui->lblTests->setToolTip(tests.available ? QStringLiteral("按原已存液量估算，不表示本次设备液面已实测。") : tests.reason);
    renderOriginalLevelViews();
}
void ReagentsPage::openReagentEditor()
{
    if (!allowEditor())
        return;
    auto* dialog = new GuardedReagentDialog(this);
    auto form = std::make_shared<Ui::MaskDlgReagentListSetting>();
    form->setupUi(dialog);
    auto current = std::make_shared<ReagentRecord>();
    form->ckxValid->setText(QStringLiteral("是否有效"));
    form->ckxCheckDate->setText(QStringLiteral("检查有效期"));
    form->ckxClean->setVisible(originalProfile_);
    form->widget_3->setVisible(originalProfile_);
    form->cbxNeedle->setVisible(originalProfile_);
    form->label_3->setVisible(originalProfile_);
    form->ldtFactor->setVisible(originalProfile_);
    form->label_2->setVisible(originalProfile_);
    if (originalProfile_)
        for (const auto &reagent : snapshot_.reagents)
            if (reagent.type==QStringLiteral("清洗液")) form->cbxCleanReagent->addItem(reagent.name);
    form->ldtReagentName->setMaxLength(16);
    form->ldtWarningAmount->setValidator(new QIntValidator(0, 9999, dialog));
    form->ldtLackAmount->setValidator(new QIntValidator(0, 9999, dialog));
    auto populate = [this, form, current]()
    {
        for (const auto& r : snapshot_.reagents)
                if ((current->id && r.id == current->id) || (!current->id && r.name == current->name))
                {
                    *current = r;
                    break;
                }
        QSignalBlocker block(form->listReagent);
        qint64 selected = current->id;
        form->listReagent->clear();
        for (const auto& r : snapshot_.reagents)
        {
            auto* item = new QListWidgetItem(r.name, form->listReagent);
            item->setData(Qt::UserRole, r.id);
            if (r.id == selected)
                form->listReagent->setCurrentItem(item);
        }
    };
    auto display = [this, form, current]()
    {
        form->ldtReagentName->setText(current->name);
        form->ldtFactor->setText(current->manufacturer);
        form->cbxReagentType->setCurrentText(current->type);
        form->ldtWarningAmount->setText(QString::number(current->warningUl));
        form->ldtLackAmount->setText(QString::number(current->outUl));
        form->ckxValid->setCheckState(current->valid ? Qt::Checked : Qt::Unchecked);
        form->ckxCheckDate->setCheckState(current->checkExpiry ? Qt::Checked : Qt::Unchecked);
        if (originalProfile_)
        {
            form->cbxNeedle->setCurrentIndex(current->originalValues.value("banding_needle").toInt());
            form->ckxClean->setCheckState(current->originalValues.value("dect_clean").toBool() ? Qt::Checked : Qt::Unchecked);
            form->cbxCleanReagent->setCurrentText(current->originalValues.value("cleaner_name").toString());
        }
        form->listReagentCode->clear();
        for (const auto& b : snapshot_.barcodes)
            if (b.reagentId == current->id)
            {
                auto* item = new QListWidgetItem(b.code, form->listReagentCode);
                item->setData(Qt::UserRole, b.id);
            }
        dirty_ = false;
    };
    connect(form->listReagent, &QListWidget::currentItemChanged, dialog,
        [this, form, current, display](QListWidgetItem* item, QListWidgetItem* previous)
        {
            if (!item)
                return;
            if (dirty_)
            {
                QSignalBlocker block(form->listReagent);
                form->listReagent->setCurrentItem(previous);
                return;
            }
            for (const auto& r : snapshot_.reagents)
                if (r.id == item->data(Qt::UserRole).toLongLong())
                {
                    *current = r;
                    display();
                    break;
                }
        });
    connect(form->btnAdd, &QPushButton::clicked, dialog,
        [this, current, display]()
        {
            if (dirty_)
                return;
            *current = ReagentRecord();
            display();
            dirty_ = true;
        });
    connect(form->btnCopy, &QPushButton::clicked, dialog,
        [this, current, display]()
        {
            if (dirty_)
                return;
            current->id = 0;
            current->name = current->name.left(12) + "copy";
            current->fixed = false;
            display();
            dirty_ = true;
        });
    connect(form->btnSave, &QPushButton::clicked, dialog,
        [this, form, current]()
        {
            if (!canEdit() || busy_)
                return;
            current->name = form->ldtReagentName->text();
            current->manufacturer = form->ldtFactor->text();
            current->type = form->cbxReagentType->currentText();
            current->warningUl = form->ldtWarningAmount->text().toInt();
            current->outUl = form->ldtLackAmount->text().toInt();
            current->valid = form->ckxValid->isChecked();
            current->checkExpiry = form->ckxCheckDate->isChecked();
            if (originalProfile_)
            {
                current->originalValues.insert("banding_needle",QString::number(form->cbxNeedle->currentIndex()));
                current->originalValues.insert("dect_clean",form->ckxClean->isChecked() ? "1" : "0");
                current->originalValues.insert("cleaner_name",form->cbxCleanReagent->currentText());
            }
            if (!form->ldtWarningAmount->hasAcceptableInput() || !form->ldtLackAmount->hasAcceptableInput())
            {
                emit operationMessage("保存试剂资料", QStringLiteral("警告量和缺少量须为整数"));
                return;
            }
            setBusy(true);
            emit requestSaveReagent(*current);
        });
    connect(form->btnDel, &QPushButton::clicked, dialog,
        [this, current]()
        {
            if (!current->id || !canEdit() || busy_)
                return;
            CusMessageBox q(this);
            q.showQuestionBox(QStringLiteral("是否删除当前选中试剂？"));
            if (q.getClickButton() == QMessageBox::Yes)
            {
                setBusy(true);
                emit requestRemoveReagent(current->id);
            }
        });
    connect(form->btnCancel, &QPushButton::clicked, dialog,
        [this, current, display]()
        {
            for (const auto& r : snapshot_.reagents)
                if (r.id == current->id)
                {
                    *current = r;
                    display();
                    return;
                }
            *current = ReagentRecord();
            display();
        });
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::close);
    connect(form->btnAddCode, &QPushButton::clicked, dialog,
        [this, current]()
        {
            if (!current->id)
                return;
            const qint64 id = current->id;
            if (!prepareToLeave())
                return;
            QTimer::singleShot(0, this, [this, id]() { openBarcodeEditor(id); });
        });
    connect(form->btnEditCode, &QPushButton::clicked, dialog,
        [this, current, form]()
        {
            if (!form->listReagentCode->currentItem())
                return;
            const qint64 id = current->id,
                         code = form->listReagentCode->currentItem()->data(Qt::UserRole).toLongLong();
            if (!prepareToLeave())
                return;
            QTimer::singleShot(0, this, [this, id, code]() { openBarcodeEditor(id, code); });
        });
    connect(form->btnDelCode, &QPushButton::clicked, dialog,
        [this, form]()
        {
            if (!form->listReagentCode->currentItem() || busy_)
                return;
            setBusy(true);
            emit requestRemoveBarcode(form->listReagentCode->currentItem()->data(Qt::UserRole).toLongLong());
        });
    connect(this, &ReagentsPage::snapshotChanged, dialog,
        [this, populate, display]()
        {
            if (dirty_ || busy_)
                return;
            populate();
            display();
        });
    for (QLineEdit* edit : dialog->findChildren<QLineEdit*>())
        connect(edit, &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(
        form->cbxReagentType, QOverload<int>::of(&QComboBox::activated), dialog, [this]() { markDirty(); });
    connect(form->ckxValid, QOverload<>::of(&CusCheckBox::signalClicked), dialog, [this]() { markDirty(); });
    connect(form->ckxClean,QOverload<>::of(&CusCheckBox::signalClicked),dialog,[this](){ markDirty(); });
    connect(form->cbxNeedle,QOverload<int>::of(&QComboBox::activated),dialog,[this](){ markDirty(); });
    connect(form->cbxCleanReagent,QOverload<int>::of(&QComboBox::activated),dialog,[this](){ markDirty(); });
    connect(
        form->ckxCheckDate, QOverload<>::of(&CusCheckBox::signalClicked), dialog, [this]() { markDirty(); });
    populate();
    if (!snapshot_.reagents.isEmpty())
        *current = snapshot_.reagents.first();
    display();
    initializeDialog(dialog);
}
void ReagentsPage::openHoleEditor()
{
    if (!allowEditor())
        return;
    auto* dialog = new GuardedReagentDialog(this);
    auto form = std::make_shared<Ui::MaskDlgReagentSetting>();
    form->setupUi(dialog);
    auto current = std::make_shared<ReagentHole>();
    current->name = selectedHole_;
    current->area = selectedHole_.startsWith("SD_") ? "s_area" : "r_area";
    for (const auto& h : snapshot_.holes)
        if (h.name == selectedHole_)
            *current = h;
    form->cbxName->addItem("none", 0);
    if (originalProfile_)
    {
        // 原版分两次读取：先清洗液，再本区域试剂；各类内部保持原表顺序。
        const QString areaType = current->area == "r_area" ? QStringLiteral("试剂")
                                                          : QStringLiteral("缓冲稀释液");
        for (const auto& type : { QStringLiteral("清洗液"), areaType })
            for (const auto& reagent : snapshot_.reagents)
                if (reagent.valid && reagent.type == type)
                    form->cbxName->addItem(reagent.name, reagent.id);
    }
    else
    {
    for (const auto& r : snapshot_.reagents)
        if (r.valid
            && (r.type == "清洗液"
                || (current->area == "r_area" ? r.type == "试剂" : r.type == "缓冲稀释液")))
            form->cbxName->addItem(r.name, r.id);
    }
    for (const auto& b : snapshot_.bottles)
        if (b.area == current->area)
            form->cbxBottleName->addItem(b.name, b.id);
    form->cbxBatch->setEditable(false);
    if (!originalProfile_)
    {
        form->cbxBatch->addItem("Default", 0);
        for (const auto& b : snapshot_.batches) form->cbxBatch->addItem(b.batchGroup, b.id);
    }
    form->cbxName->setCurrentIndex(qMax(0, form->cbxName->findData(current->reagentId)));
    form->cbxBottleName->setCurrentIndex(qMax(0, form->cbxBottleName->findData(current->bottleId)));
    form->cbxBatch->setCurrentIndex(qMax(0, form->cbxBatch->findData(current->batchId)));
    if (originalProfile_)
    {
        // 原Windows只允许选择使用该试剂的项目批组。条码不是这里的候选来源；
        // 一个批组可用|包含多个批号，PTFbg为原版排除项，不自行补可输入批号。
        const auto refreshChoices=[this,form,current]()
        {
            const QSignalBlocker bottleSignals(form->cbxBottleName), batchSignals(form->cbxBatch);
            form->cbxBottleName->clear(); form->cbxBatch->clear();
            const qint64 reagentId=form->cbxName->currentData().toLongLong();
            if (!reagentId) return;
            for (const auto &bottle : snapshot_.bottles)
                if (bottle.area==current->area) form->cbxBottleName->addItem(bottle.name,bottle.id);
            form->cbxBottleName->setCurrentIndex(qMax(0,form->cbxBottleName->findData(current->bottleId)));
            const QString reagentName=form->cbxName->currentText();
            for (const auto &project : projects_)
            {
                if (!project.reagentNames.contains(reagentName)) continue;
                for (const auto &batch : snapshot_.batches)
                {
                    if (batch.projectId!=project.id || batch.batchGroup==QStringLiteral("PTFbg")) continue;
                    for (const auto &name : batch.batchGroup.split('|'))
                        if (form->cbxBatch->findText(name)<0) form->cbxBatch->addItem(name);
                }
            }
            const int selected=form->cbxBatch->findText(current->originalBatch);
            if (selected>=0) form->cbxBatch->setCurrentIndex(selected);
        };
        refreshChoices();
        connect(form->cbxName,QOverload<int>::of(&QComboBox::currentIndexChanged),dialog,refreshChoices);
    }
    form->ldtOutDate->setReadOnly(originalProfile_);
    form->ldtOutDate->setText(!originalProfile_ && current->expiry.isEmpty()
            ? QDate::currentDate().addYears(1).toString("yyyy-MM-dd")
            : current->expiry);
    auto calendar=std::make_shared<QPointer<ReagentCalendarPopup>>();
    connect(form->btnCalendarSet, &QPushButton::clicked, dialog,
        [this,form,dialog,calendar]()
        {
            if (!originalProfile_)
            { form->ldtOutDate->setFocus(); form->ldtOutDate->selectAll(); return; }
            if (!canEdit() || busy_) return;
            if (*calendar) { (*calendar)->raise(); return; }
            QDate selected=QDate::fromString(form->ldtOutDate->text(),QStringLiteral("yyyy/MM/dd"));
            if (!selected.isValid()) selected=QDate::currentDate();
            auto *popup=new ReagentCalendarPopup(dialog,selected);
            *calendar=popup;
            connect(popup->calendar(),&CusCalendar::signalSelectDateChanged,this,
                [this,form,popup](const QString &text)
                {
                    if (canEdit() && !busy_ && form->ldtOutDate->text()!=text)
                    {
                        form->ldtOutDate->setText(text);
                        markDirty(); // 日历赋值不会触发textEdited，必须明确保留未保存草稿。
                    }
                    popup->reject();
                });
            popup->openAsync();
        });
    connect(form->btnEditBatch, &QPushButton::clicked, dialog, [this]()
        { emit operationMessage("批号", QStringLiteral("请先保存/取消孔位，再从试剂组设置编辑批号")); });
    connect(form->btnSave, &QPushButton::clicked, dialog,
        [this, form, current]()
        {
            if (!canEdit() || busy_)
                return;
            current->reagentId = form->cbxName->currentData().toLongLong();
            if (!current->reagentId)
            {
                setBusy(true);
                emit requestClearHole(current->name);
                return;
            }
            const qint64 bottle = form->cbxBottleName->currentData().toLongLong();
            // Original setting Save reinitializes the local amount to the
            // bottle dictionary maximum. The separate local-volume editor
            // can subsequently enter an actual known quantity without TX.
            for (const auto& b : snapshot_.bottles)
                if (b.id == bottle)
                    current->amountUl = current->remainingUl = b.capacityUl;
            current->bottleId = bottle;
            current->priority = 99; // 原单孔设置保存会把该瓶优先级置99。
            current->batchId = form->cbxBatch->currentData().toLongLong();
            if (originalProfile_) current->originalBatch=form->cbxBatch->currentText();
            current->expiry = form->ldtOutDate->text();
            setBusy(true);
            emit requestSaveHole(*current);
        });
    connect(form->btnCancel, &QPushButton::clicked, dialog,
        [this, dialog]()
        {
            dirty_ = false;
            dialog->close();
        });
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::close);
    for (QComboBox* box : dialog->findChildren<QComboBox*>())
        connect(box, QOverload<int>::of(&QComboBox::activated), dialog, [this]() { markDirty(); });
    connect(form->ldtOutDate, &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(this, &ReagentsPage::snapshotChanged, dialog,
        [this, dialog]()
        {
            if (!busy_ && !dirty_)
                dialog->close();
        });
    initializeDialog(dialog);
}
void ReagentsPage::openBatchEditor()
{
    if (!allowEditor())
        return;
    auto* dialog = new GuardedReagentDialog(this);
    auto form = std::make_shared<Ui::MaskDlgGroupSetting>();
    form->setupUi(dialog);
    auto current = std::make_shared<ReagentBatch>();
    for (const auto& p : projects_)
    {
        const int row = form->tblProjReagentGroup->rowCount();
        form->tblProjReagentGroup->insertRow(row);
        auto* item = new QTableWidgetItem(p.name);
        item->setData(Qt::UserRole, p.id);
        item->setData(Qt::UserRole+1, p.originalFileName);
        item->setData(Qt::UserRole+2, p.originalRoot);
        form->tblProjReagentGroup->setItem(row, 0, item);
        form->tblProjReagentGroup->setRowHeight(row, 40);
    }
    form->tblProjReagentGroup->setEditTriggers(QAbstractItemView::NoEditTriggers);
    form->tblProjReagentGroup->setSelectionBehavior(QAbstractItemView::SelectRows);
    form->tblProjReagentGroup->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    form->tblGroup->setEditTriggers(QAbstractItemView::NoEditTriggers);
    form->tblGroup->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    const QRegularExpression decimal(QStringLiteral("(0|[1-9][0-9]{0,3})(\\.[0-9]{1,4})?"));
    form->ldtIsiVal->setValidator(new QRegularExpressionValidator(decimal, dialog));
    form->ldtNormalVal->setValidator(new QRegularExpressionValidator(decimal, dialog));
    form->ldtIsiVal->setToolTip(QStringLiteral("原范围0–9999，最多4位小数；本地资料，未用于计算"));
    form->ldtNormalVal->setToolTip(form->ldtIsiVal->toolTip());
    form->btnIsiVal->setEnabled(false);
    form->btnNormal->setEnabled(false);
    form->cbx_use_Batch_Group->setText(tr("是否启用试剂组功能"));
    form->cbx_use_Batch_Group->setEnabled(originalProfile_);
    form->cbxRBatch->setEditable(true);
    auto refresh = [this, form, current]()
    {
        if (dirty_) return; // 失败回执或资料变化不能重填尚未保存的批组草稿。
        QSignalBlocker blocker(form->tblGroup), groupSignals(form->cbx_use_Batch_Group), projectSignals(form->tblProjReagentGroup);
        ReagentProject project;
        const int projectRow=form->tblProjReagentGroup->currentRow();
        auto *selectedProject=projectRow>=0 ? form->tblProjReagentGroup->item(projectRow,0) : nullptr;
        for (const auto &candidate : projects_)
            if (originalProfile_ ? selectedProject && !candidate.originalFileName.isEmpty() &&
                    candidate.originalFileName==selectedProject->data(Qt::UserRole+1).toString() &&
                    candidate.originalRoot==selectedProject->data(Qt::UserRole+2).toString() : candidate.id==current->projectId)
            { project=candidate;break; }
        if (originalProfile_)
        {
            if (!project.originalFileName.isEmpty())
            {
                // 同文件成功读回后采用实际新xuhao；表格旧序号不能把页面困在旧项目上。
                current->projectId=project.id;
                selectedProject->setData(Qt::UserRole,project.id);
                selectedProject->setText(project.name);
            }
            else *current=ReagentBatch();
        }
        const bool committedUnread=!pendingOriginalGroupCommit_.originalFileName.isEmpty() &&
            project.originalRoot==pendingOriginalGroupCommit_.originalRoot &&
            project.originalFileName==pendingOriginalGroupCommit_.originalFileName;
        const bool known=originalProfile_ && !committedUnread && !project.originalFingerprint.isEmpty() &&
            (project.originalGroupEnabled==QStringLiteral("0") || project.originalGroupEnabled==QStringLiteral("1"));
        const bool enabled=project.originalGroupEnabled==QStringLiteral("1");
        form->cbx_use_Batch_Group->setTristate(!known);
        form->cbx_use_Batch_Group->setCheckState(known ? (enabled ? Qt::Checked : Qt::Unchecked) : Qt::PartiallyChecked);
        form->cbx_use_Batch_Group->setProperty("reagentBaseEnabled",known);
        form->cbx_use_Batch_Group->setEnabled(known && canEdit() && !busy_);
        form->tblGroup->setRowCount(0);
        int selected = -1, firstVisible = -1;
        for (const auto& b : snapshot_.batches)
            if (b.projectId == current->projectId)
            {
                int row = form->tblGroup->rowCount();
                form->tblGroup->insertRow(row);
                auto* item = new QTableWidgetItem(b.code);
                item->setData(Qt::UserRole, b.id);
                form->tblGroup->setItem(row, 0, item);
                const bool hidden=originalProfile_ && (b.batchGroup==QStringLiteral("PTFbg") ||
                    (b.batchGroup==QStringLiteral("Default") && (enabled || !project.name.startsWith(QStringLiteral("PT")))));
                form->tblGroup->setRowHidden(row,hidden);
                if (!hidden && firstVisible<0) firstVisible=row;
                if (!hidden && (b.id == current->id || (!current->id && b.code == current->code)))
                {
                    selected = row;
                    *current = b;
                }
            }
        if (originalProfile_ && selected<0 && !dirty_ && firstVisible>=0)
        {
            selected=firstVisible;
            const qint64 id=form->tblGroup->item(selected,0)->data(Qt::UserRole).toLongLong();
            for (const auto &batch : snapshot_.batches) if (batch.id==id) *current=batch;
        }
        form->tblGroup->setCurrentCell(selected, 0);
        if (selected < 0 && current->id)
        {
            const qint64 projectId = current->projectId;
            *current = ReagentBatch();
            current->projectId = projectId;
        }
        form->lstRBatch->clear();
        form->lstRBatch->addItems(current->batchGroup.split('|', Qt::SkipEmptyParts));
        form->cbxRBatch->setCurrentText(current->code);
        form->ldtIsiVal->setText(current->isiValue);
        form->ldtNormalVal->setText(current->normalValue);
    };
    connect(form->cbx_use_Batch_Group, &QCheckBox::clicked, dialog,
        [this, form, current](bool enabled)
        {
            ReagentProject project;
            for (const auto &candidate : projects_) if (candidate.id==current->projectId) project=candidate;
            const QSignalBlocker blocker(form->cbx_use_Batch_Group);
            form->cbx_use_Batch_Group->setChecked(project.originalGroupEnabled==QStringLiteral("1"));
            if (!originalProfile_ || !canEdit() || busy_ || dirty_ || project.originalFingerprint.isEmpty())
            {
                showOperationMessage(QStringLiteral("保存试剂组开关"),QStringLiteral("请先保存或取消批组草稿，并确认账户、仪器状态和原项目资料就绪。"));
                return;
            }
            // 点击即保存；没有调用下面的ISI/正常值保存，也没有发送设备命令。
            setBusy(true);
            emit requestSaveOriginalBatchGroupEnabled(project,enabled);
        });
    connect(this, &ReagentsPage::operationFinished, dialog,
        [this,refresh](const QString &operation,bool success)
        {
            const bool groupReceipt=operation==QStringLiteral("保存试剂组开关") ||
                (!success && operation==QStringLiteral("读取原试剂") && !pendingOriginalGroupCommit_.originalFileName.isEmpty());
            if (groupReceipt && !dirty_) refresh();
        });
    connect(form->tblProjReagentGroup, &QTableWidget::itemSelectionChanged, dialog,
        [this, form, current, refresh]()
        {
            if (dirty_)
            {
                QSignalBlocker block(form->tblProjReagentGroup);
                for (int i = 0; i < form->tblProjReagentGroup->rowCount(); ++i)
                    if (form->tblProjReagentGroup->item(i, 0)->data(Qt::UserRole).toLongLong()
                        == current->projectId)
                        form->tblProjReagentGroup->setCurrentCell(i, 0);
                return;
            }
            int row = form->tblProjReagentGroup->currentRow();
            if (row < 0)
                return;
            *current = ReagentBatch();
            current->projectId = form->tblProjReagentGroup->item(row, 0)->data(Qt::UserRole).toLongLong();
            const bool pt = form->tblProjReagentGroup->item(row, 0)->text().startsWith("PT");
            for (QWidget* widget : QList<QWidget*> {form->ldtIsiVal, form->ldtNormalVal, form->lblIsiVal,
                     form->lblNormal, form->btnIsiVal, form->btnNormal})
                widget->setVisible(pt);
            form->lstRBatch->clear();
            refresh();
        });
    connect(form->tblGroup, &QTableWidget::itemSelectionChanged, dialog,
        [this, form, current]()
        {
            if (dirty_)
            {
                QSignalBlocker block(form->tblGroup);
                if (!current->id)
                    form->tblGroup->setCurrentCell(-1, -1);
                for (int i = 0; i < form->tblGroup->rowCount(); ++i)
                    if (form->tblGroup->item(i, 0)->data(Qt::UserRole).toLongLong() == current->id)
                        form->tblGroup->setCurrentCell(i, 0);
                return;
            }
            int row = form->tblGroup->currentRow();
            if (row < 0)
                return;
            qint64 id = form->tblGroup->item(row, 0)->data(Qt::UserRole).toLongLong();
            for (const auto& b : snapshot_.batches)
                if (b.id == id)
                {
                    *current = b;
                    form->lstRBatch->clear();
                    form->lstRBatch->addItems(b.batchGroup.split('|', Qt::SkipEmptyParts));
                    form->cbxRBatch->setCurrentText(b.code);
                    form->ldtIsiVal->setText(b.isiValue);
                    form->ldtNormalVal->setText(b.normalValue);
                }
        });
    connect(form->btnAdd, &QPushButton::clicked, dialog,
        [this, current, form]()
        {
            if (dirty_)
                return;
            current->id = 0;
            current->code.clear();
            current->batchGroup.clear();
            current->hasMetadata = false;
            current->isiValue.clear();
            current->normalValue.clear();
            current->metadataEdited = false;
            QSignalBlocker blocker(form->tblGroup);
            form->tblGroup->setCurrentCell(-1, -1);
            form->lstRBatch->clear();
            form->cbxRBatch->setCurrentText("");
            form->ldtIsiVal->clear();
            form->ldtNormalVal->clear();
            dirty_ = true;
        });
    connect(form->btnAddRBatch, &QPushButton::clicked, dialog,
        [this, form]()
        {
            const QString value = form->cbxRBatch->currentText().trimmed();
            if (value.isEmpty() || value.contains('|'))
                return;
            if (form->lstRBatch->findItems(value, Qt::MatchExactly).isEmpty())
                form->lstRBatch->addItem(value);
            markDirty();
        });
    connect(form->btnDelRBatch, &QPushButton::clicked, dialog,
        [this, form]()
        {
            delete form->lstRBatch->takeItem(form->lstRBatch->currentRow());
            markDirty();
        });
    connect(form->btnSave, &QPushButton::clicked, dialog,
        [this, form, current]()
        {
            if (!canEdit() || busy_)
                return;
            if (!current->projectId)
            {
                emit operationMessage("保存批号", QStringLiteral("先选择项目"));
                return;
            }
            current->code = form->cbxRBatch->currentText().trimmed();
            QStringList list;
            for (int i = 0; i < form->lstRBatch->count(); ++i)
                list << form->lstRBatch->item(i)->text();
            if (list.isEmpty())
                list << current->code;
            current->batchGroup = list.join('|');
            const int projectRow = form->tblProjReagentGroup->currentRow();
            const bool pt = projectRow >= 0 && form->tblProjReagentGroup->item(projectRow, 0)->text().startsWith("PT");
            current->metadataEdited = pt;
            if (pt)
            {
                current->isiValue = form->ldtIsiVal->text();
                current->normalValue = form->ldtNormalVal->text();
            }
            setBusy(true);
            emit requestSaveBatch(*current);
        });
    connect(form->btnDel, &QPushButton::clicked, dialog,
        [this, current]()
        {
            if (current->id && !busy_)
            {
                setBusy(true);
                emit requestRemoveBatch(current->id);
            }
        });
    connect(form->btnCancel, &QPushButton::clicked, dialog,
        [this, form, current, refresh]()
        {
            dirty_ = false;
            const qint64 project = current->projectId;
            const qint64 id = current->id;
            *current = ReagentBatch();
            current->projectId = project;
            for (const auto& batch : snapshot_.batches)
                if (batch.id == id)
                    *current = batch;
            refresh();
        });
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::close);
    connect(form->cbxRBatch->lineEdit(), &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(form->ldtIsiVal, &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(form->ldtNormalVal, &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(this, &ReagentsPage::snapshotChanged, dialog, [this, refresh]()
    {
        if (!dirty_)
            refresh();
    });
    initializeDialog(dialog);
    if (form->tblProjReagentGroup->rowCount())
        form->tblProjReagentGroup->selectRow(0);
}
void ReagentsPage::openBarcodeEditor(qint64 reagentId, qint64 barcodeId)
{
    if (!allowEditor())
        return;
    auto* dialog = new GuardedReagentDialog(this);
    auto form = std::make_shared<Ui::MaskDlgReagentCode>();
    form->setupUi(dialog);
    auto current = std::make_shared<ReagentBarcode>();
    current->reagentId = reagentId;
    for (const auto& b : snapshot_.barcodes)
        if (b.id == barcodeId)
            *current = b;
    for (const auto& r : snapshot_.reagents)
        if (r.id == reagentId)
            form->lblReagentName->setText(r.name);
    for (const auto& b : snapshot_.bottles)
        form->cbxBottleName->addItem(b.name + " (" + b.area + ")", b.id);
    form->cbxBottleName->setCurrentIndex(qMax(0, form->cbxBottleName->findData(current->bottleId)));
    form->ldtReagentCode->setText(current->code);
    form->ldtOutDate->setReadOnly(false);
    form->ldtOutDate->setText(current->expiry.isEmpty()
            ? QDate::currentDate().addYears(1).toString("yyyy-MM-dd")
            : current->expiry);
    connect(form->btnSave, &QPushButton::clicked, dialog,
        [this, current, form]()
        {
            if (!canEdit() || busy_)
                return;
            current->bottleId = form->cbxBottleName->currentData().toLongLong();
            current->code = form->ldtReagentCode->text();
            current->expiry = form->ldtOutDate->text();
            setBusy(true);
            emit requestSaveBarcode(*current);
        });
    connect(form->btnCancel, &QPushButton::clicked, dialog,
        [this, dialog]()
        {
            dirty_ = false;
            dialog->close();
        });
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::close);
    connect(form->btnCalendarSet, &QPushButton::clicked, dialog, [form]() { form->ldtOutDate->setFocus(); });
    connect(form->btnEditBatch, &QPushButton::clicked, dialog,
        [form]()
        {
            form->ldtReagentCode->setFocus();
            form->ldtReagentCode->selectAll();
        });
    for (QLineEdit* e : dialog->findChildren<QLineEdit*>())
        connect(e, &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(this, &ReagentsPage::snapshotChanged, dialog,
        [this, dialog]()
        {
            if (!busy_ && !dirty_)
                dialog->close();
        });
    initializeDialog(dialog);
    connect(
        form->cbxBottleName, QOverload<int>::of(&QComboBox::activated), dialog, [this]() { markDirty(); });
}
void ReagentsPage::openBottleEditor()
{
    if (!allowEditor())
        return;
    // The reference exposes bottle metadata from its dictionary, without a
    // bottle CRUD page. A separate local dictionary dialog leaves the source
    // business page and its existing controls/layout untouched.
    auto* dialog = new GuardedReagentDialog(this);
    dialog->setObjectName(QStringLiteral("ReagentBottleEditor"));
    dialog->setWindowTitle(QStringLiteral("本地瓶型字典（未同步仪器）"));
    dialog->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 13));
    dialog->setMinimumWidth(420);
    auto* layout = new QFormLayout(dialog);
    auto* select = new QComboBox(dialog);
    select->setObjectName("bottleSelect");
    select->addItem(QStringLiteral("新增"), 0);
    for (const auto& b : snapshot_.bottles)
        select->addItem(b.name + " / " + b.area, b.id);
    auto* name = new QLineEdit(dialog);
    name->setObjectName("bottleName");
    auto* area = new QComboBox(dialog);
    area->setObjectName("bottleArea");
    area->addItem(QStringLiteral("试剂区"), QStringLiteral("r_area"));
    area->addItem(QStringLiteral("缓冲稀释液区"), QStringLiteral("s_area"));
    auto* capacity = new QSpinBox(dialog);
    capacity->setObjectName("bottleCapacity");
    capacity->setRange(1, 100000);
    capacity->setSuffix(" µL");
    auto* height = new QDoubleSpinBox(dialog);
    height->setObjectName("bottleHeight");
    height->setRange(0, 500);
    height->setSuffix(" mm");
    layout->addRow(QStringLiteral("瓶型"), select);
    layout->addRow(QStringLiteral("名称"), name);
    layout->addRow(QStringLiteral("区域"), area);
    layout->addRow(QStringLiteral("最大容量"), capacity);
    layout->addRow(QStringLiteral("高度"), height);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Save)->setText(QStringLiteral("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    buttons->button(QDialogButtonBox::Save)->setObjectName(QStringLiteral("btnSave"));
    buttons->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("btnCancel"));
    auto* remove = buttons->addButton(QStringLiteral("删除"), QDialogButtonBox::DestructiveRole);
    layout->addRow(buttons);
    auto selectedId = std::make_shared<qint64>(0);
    connect(select, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog,
        [this, selectedId, select, name, area, capacity, height]()
        {
            if (dirty_)
            {
                QSignalBlocker block(select);
                select->setCurrentIndex(qMax(0, select->findData(*selectedId)));
                return;
            }
            *selectedId = select->currentData().toLongLong();
            for (const auto& b : snapshot_.bottles)
                if (b.id == *selectedId)
                {
                    name->setText(b.name);
                    area->setCurrentIndex(qMax(0, area->findData(b.area)));
                    capacity->setValue(b.capacityUl);
                    height->setValue(b.heightMm);
                    dirty_ = false;
                    return;
                }
            name->clear();
            dirty_ = false;
        });
    connect(buttons, &QDialogButtonBox::accepted, dialog,
        [this, select, name, area, capacity, height]()
        {
            if (!canEdit() || busy_)
                return;
            ReagentBottle b;
            b.id = select->currentData().toLongLong();
            for (const auto &existing : snapshot_.bottles) if (existing.id==b.id) b=existing;
            b.name = name->text();
            b.area = area->currentData().toString();
            b.capacityUl = capacity->value();
            b.heightMm = height->value();
            setBusy(true);
            emit requestSaveBottle(b);
        });
    connect(remove, &QPushButton::clicked, dialog,
        [this, select]()
        {
            if (!busy_ && select->currentData().toLongLong())
            {
                setBusy(true);
                emit requestRemoveBottle(select->currentData().toLongLong());
            }
        });
    connect(buttons, &QDialogButtonBox::rejected, dialog,
        [this, dialog]()
        {
            dirty_ = false;
            dialog->close();
        });
    connect(name, &QLineEdit::textEdited, dialog, [this]() { markDirty(); });
    connect(capacity, QOverload<int>::of(&QSpinBox::valueChanged), dialog, [this]() { markDirty(); });
    connect(height, QOverload<double>::of(&QDoubleSpinBox::valueChanged), dialog, [this]() { markDirty(); });
    connect(this, &ReagentsPage::snapshotChanged, dialog,
        [this, dialog]()
        {
            if (!busy_ && !dirty_)
                dialog->close();
        });
    initializeDialog(dialog);
    connect(area, QOverload<int>::of(&QComboBox::activated), dialog, [this]() { markDirty(); });
}
void ReagentsPage::reloadOriginal()
{
    if (!originalProfile_) return;
    if (busy_ || dirty_) { pendingOriginalReload_=true; return; }
    pendingOriginalReload_=false;
    setBusy(true);
    emit requestLoad();
}

void ReagentsPage::setInstrumentBusy(bool busy)
{
    if (instrumentBusy_==busy) return;
    instrumentBusy_=busy;
    if (busy)
    {
        store_->setOriginalDeviceOperationContext(0,QStringList());
        deviceOperations_.clear();
    }
    QMetaObject::invokeMethod(store_,"setInstrumentBusy",Qt::QueuedConnection,Q_ARG(bool,busy));
    applyPermissions();
}

bool ReagentsPage::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick && watched == ui->lblBottleName)
    {
        openBottleEditor();
        return true;
    }
    if (event->type() == QEvent::MouseButtonDblClick && watched == ui->lblAmount)
    {
        if (!canEdit() || busy_ || dialog_)
            return true;
        ReagentHole h;
        for (const auto& hole : snapshot_.holes)
            if (hole.name == selectedHole_)
                h = hole;
        if (!h.id)
            return true;
        double capacity = h.amountUl;
        for (const auto& b : snapshot_.bottles)
            if (b.id == h.bottleId)
                capacity = b.capacityUl;
        bool accepted = false;
        double amount = QInputDialog::getDouble(this, QStringLiteral("已知装载量"),
            QStringLiteral("装载量 µL（非液面测量）"), h.amountUl, 0, capacity, 4, &accepted);
        if (!accepted)
            return true;
        double remaining = QInputDialog::getDouble(this, QStringLiteral("已知余量"),
            QStringLiteral("余量 µL（非液面测量）"), qMin(h.remainingUl, amount), 0, amount, 4,
            &accepted);
        if (!accepted)
            return true;
        h.amountUl = amount;
        h.remainingUl = remaining;
        setBusy(true);
        emit requestSaveHole(h);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
