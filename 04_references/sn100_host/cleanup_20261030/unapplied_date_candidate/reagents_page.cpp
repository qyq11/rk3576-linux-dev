#include "reagents_page.h"
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
    connect(ui->btnReagentInfoPage, &QPushButton::clicked, this,
        [this]() { ui->stkReagentInfo->setCurrentIndex(0); });
    connect(ui->btnReagentCountPage, &QPushButton::clicked, this,
        [this]()
        {
            ui->stkReagentInfo->setCurrentIndex(1);
            showOffline(QStringLiteral("剩余测试次数计算：尚缺项目试剂消耗配置"));
        });
    for (QAbstractButton* button : QList<QAbstractButton*> { ui->btnDect, ui->btnDectOne, ui->btnScan,
             ui->btnChange, ui->btnChangeHole, ui->btnUpdatePriority, ui->btn_restore, ui->btn_clearPanel,
             ui->btn_calibrationmode, ui->btnUsaged })
        connect(button, &QAbstractButton::clicked, this, [this, button]() { showOffline(button->text()); });
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
            setBusy(false);
            applyPermissions();
            ui->reagentPanel->setSnapshot(snapshot_);
            selectHole(selectedHole_);
            emit readyChanged(true);
            emit snapshotChanged(snapshot_);
            if (!pendingSavedOperation_.isEmpty())
            {
                const QString operation=pendingSavedOperation_; pendingSavedOperation_.clear();
                emit operationMessage(operation,originalProfile_ ? QStringLiteral("原数据库保存并重新读取完成；设备同步状态另行核对") : QStringLiteral("本地保存并重新读取完成；未同步仪器"));
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
            setBusy(false);
            emit operationMessage(op, message);
            emit operationFinished(op, false);
            QToolTip::showText(mapToGlobal(QPoint(width() / 2, 50)), op + "：" + message, this);
        });
    applyPermissions();
}
ReagentsPage::~ReagentsPage()
{
    if (dialog_)
        delete dialog_;
    delete ui;
}
void ReagentsPage::setProjects(ReagentProjects projects)
{
    if (originalProfile_) return; // 原项目由同一原库读取，不接受早期catalog编号覆盖。
    projects_ = projects;
}
bool ReagentsPage::canEdit() const
{
    return ready_ && !instrumentBusy_ && session_.authenticated && session_.actionReady && session_.role >= 0
        && session_.role <= 5;
}
void ReagentsPage::setSession(AccountSession session)
{
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
void ReagentsPage::showOffline(QString action)
{
    const QString message = action + QStringLiteral("：设备未连接，未发送命令");
    emit operationMessage(action, message);
    QToolTip::showText(QCursor::pos(), message, this);
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
    ui->lblOutDate->setText(hole.expiry);
    ui->lblKeepTime->setText("-");
    ui->lblSetDate->setText(hole.changedAt.section('T', 0, 0));
    ui->lblSetTime->setText(hole.changedAt.section('T', 1, 1));
    ui->lblTests->setText("-");
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
    current->area = selectedHole_.startsWith("R_") ? "r_area" : "s_area";
    for (const auto& h : snapshot_.holes)
        if (h.name == selectedHole_)
            *current = h;
    form->cbxName->addItem("none", 0);
    for (const auto& r : snapshot_.reagents)
        if (r.valid
            && (r.type == "清洗液"
                || (current->area == "r_area" ? r.type == "试剂" : r.type == "缓冲稀释液")))
            form->cbxName->addItem(r.name, r.id);
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
    form->cbx_use_Batch_Group->setEnabled(false);
    form->cbxRBatch->setEditable(true);
    auto refresh = [this, form, current]()
    {
        QSignalBlocker blocker(form->tblGroup);
        form->tblGroup->setRowCount(0);
        int selected = -1;
        for (const auto& b : snapshot_.batches)
            if (b.projectId == current->projectId)
            {
                int row = form->tblGroup->rowCount();
                form->tblGroup->insertRow(row);
                auto* item = new QTableWidgetItem(b.code);
                item->setData(Qt::UserRole, b.id);
                form->tblGroup->setItem(row, 0, item);
                if (b.id == current->id || (!current->id && b.code == current->code))
                {
                    selected = row;
                    *current = b;
                }
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
