#include "original_reagent_usage.h"
#include "ui_maskdlgreagentusage.h"
#include "cusdialogmask.h"
#include "samples/material/qtmaterialscrollbar.h"
#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QHeaderView>
#include <QPushButton>
#include <QScroller>
#include <QTableWidget>
#include <QTextEdit>

OriginalReagentUsageDialog::OriginalReagentUsageDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MaskDlgReagentUsage)
{
    ui->setupUi(this);
    for (QWidget *control : findChildren<QWidget*>())
        control->setProperty("reagentBaseEnabled",control->isEnabled());
    // 原ui默认显示耗材添加页，但项目页签被选中；保留这个实际初始状态。
    ui->btnOtherUsagePage->hide();
    for (auto *table : {ui->tblProjReagentGroup, ui->tblOtherGroup})
    {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setFocusPolicy(Qt::NoFocus);
        table->setRowCount(0);
    }
    auto *scrollbar=new QtMaterialScrollBar(ui->tblProjReagentGroup);
    scrollbar->setSliderColor(QColor(34,34,34,50));
    scrollbar->setBackgroundColor(Qt::transparent);
    scrollbar->setCanvasColor(Qt::transparent);
    scrollbar->setHideOnMouseOut(true);
    ui->tblProjReagentGroup->setVerticalScrollBar(scrollbar);
    QScroller::scroller(ui->tblProjReagentGroup)->grabGesture(ui->tblProjReagentGroup,QScroller::LeftMouseButtonGesture);
    ui->tblProjReagentGroup->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    connect(ui->btnClose,&QPushButton::clicked,this,&OriginalReagentUsageDialog::reject);
    connect(ui->mask,&CusDialogMask::closeFinished,this,[this]()
    {
        closingFinished_=true;
        QDialog::reject();
    });
    connect(ui->btnProjUsagePage,&QPushButton::clicked,this,[this]() { ui->stkUsage->setCurrentIndex(0); });
    connect(ui->btnOtherUsagePage,&QPushButton::clicked,this,[this]() { ui->stkUsage->setCurrentIndex(1); });
    connect(ui->btnUsageAddPage,&QPushButton::clicked,this,[this]() { ui->stkUsage->setCurrentIndex(2); });
    connect(ui->btnGetKey,&QPushButton::clicked,this,[this]() { emit commandRequested(QByteArrayLiteral("QI")); });
    connect(ui->btnUpdateCode,&QPushButton::clicked,this,[this]()
    {
        // 原getConsumeUpdateCommand为顶层J；空白、换行和原输入均按UTF-8保留。
        emit commandRequested(QByteArrayLiteral("J[")+ui->txtConsumeCode->toPlainText().toUtf8()+']');
    });
    // 原导入文件、清历史、导出和复制按钮没有处理函数，不按按钮名称新增动作。
    setThemeStyle(QString(),QColor(232,51,35));
}

OriginalReagentUsageDialog::~OriginalReagentUsageDialog()
{
    delete ui;
}

void OriginalReagentUsageDialog::setThemeStyle(const QString &sheet, const QColor &matchColor)
{
    setStyleSheet(sheet);
    ui->wgtTabs->setStyleSheet(QStringLiteral(
        ".QPushButton{color:#AAAAAA;border:none;background-color:transparent;border-top:5px solid transparent;}"
        ".QPushButton:checked{color:#444444;border-top:5px solid rgba(%1,%2,%3,%4);}")
        .arg(matchColor.red()).arg(matchColor.green()).arg(matchColor.blue()).arg(matchColor.alpha()));
}

void OriginalReagentUsageDialog::prepareAppearance()
{
    if (appearancePrepared_) return;
    appearancePrepared_=true;
    QWidget *host=parentWidget() ? parentWidget()->window() : QApplication::activeWindow();
    if (host && host!=this)
    {
        const QSize available=host->size()-QSize(40,40);
        scale_=qMin(1.0,qMin(double(qMax(1,available.width()))/ui->view_frame->width(),
                            double(qMax(1,available.height()))/ui->view_frame->height()));
    }
    struct OriginalControl
    {
        QWidget *widget;
        QRect rectangle;
        QSize minimum;
        QFont font;
        QString style;
    };
    QList<OriginalControl> controls;
    for (QWidget *control : ui->view_frame->findChildren<QWidget*>())
        controls.append({control,control->geometry(),control->minimumSize(),control->font(),control->styleSheet()});
    for (const auto &before : controls)
    {
        QWidget *control=before.widget;
        QFont font=before.font;
        if (font.family().contains(QStringLiteral("苹方"))) font.setFamily(QStringLiteral("Noto Sans CJK SC"));
        if (font.pixelSize()>0) font.setPixelSize(qMax(1,qRound(font.pixelSize()*scale_)));
        else if (font.pointSizeF()>0) font.setPointSizeF(font.pointSizeF()*scale_);
        control->setFont(font);
        QString style=before.style;
        style.replace(QStringLiteral("苹方 粗体"),QStringLiteral("Noto Sans CJK SC"));
        style.replace(QStringLiteral("font:18px"),QStringLiteral("font:%1px").arg(qMax(1,qRound(18*scale_))));
        control->setStyleSheet(style);
        if (scale_<1.0)
        {
            const QRect rectangle=before.rectangle;
            control->setMinimumSize(qRound(before.minimum.width()*scale_),qRound(before.minimum.height()*scale_));
            control->setGeometry(qRound(rectangle.x()*scale_),qRound(rectangle.y()*scale_),
                                 qRound(rectangle.width()*scale_),qRound(rectangle.height()*scale_));
        }
    }
    if (scale_<1.0)
        ui->view_frame->resize(qRound(ui->view_frame->width()*scale_),qRound(ui->view_frame->height()*scale_));
    for (auto *table : {ui->tblProjReagentGroup, ui->tblOtherGroup})
        table->horizontalHeader()->setDefaultSectionSize(qMax(1,qRound(143*scale_)));
    setProperty("reagentDialogScale",scale_);
    ui->mask->setupForm();
}

void OriginalReagentUsageDialog::openUsage()
{
    if (isVisible()) { raise(); activateWindow(); return; }
    prepareAppearance();
    closingFinished_=false;
    projectHas_.clear(); projectUsed_.clear(); fromDevice_=false;
    refreshProjectTable();
    // 不重置页签、添加码或申请码，沿用原常驻弹窗关闭后再开的行为。
    QDialog::open();
    ui->mask->showAnimation();
    // 宿主应先连接信号并核当前连接；不能在构造器中发出后丢失这次查询。
    emit commandRequested(QByteArrayLiteral("QH"));
}

void OriginalReagentUsageDialog::setProjects(QStringList projects)
{
    projects_=projects;
    refreshProjectTable();
}

void OriginalReagentUsageDialog::invalidateDeviceData()
{
    projectHas_.clear(); projectUsed_.clear(); fromDevice_=false;
    ui->txtDivKey->clear();
    refreshProjectTable();
    // 保留使用者尚未提交的耗材添加码；不把旧设备申请码带到新连接。
}

void OriginalReagentUsageDialog::setUsage(QHash<QString,QString> projectHas, QHash<QString,QString> projectUsed)
{
    projectHas_=projectHas;
    projectUsed_=projectUsed;
    fromDevice_=true;
    refreshProjectTable();
}

void OriginalReagentUsageDialog::refreshProjectTable()
{
    ui->tblProjReagentGroup->clearContents();
    ui->tblProjReagentGroup->setRowCount(0);
    QFont font(QStringLiteral("Noto Sans CJK SC"));
    font.setPointSizeF(13*scale_);
    for (const QString &project : projects_)
    {
        const int row=ui->tblProjReagentGroup->rowCount();
        ui->tblProjReagentGroup->insertRow(row);
        const QString missing=fromDevice_ ? QStringLiteral("0") : QStringLiteral("?");
        const QStringList values{project,projectUsed_.value(project,missing)+QStringLiteral("次"),
                                 projectHas_.value(project,missing)+QStringLiteral("次")};
        for (int column=0;column<values.size();++column)
        {
            auto *item=new QTableWidgetItem(values[column]);
            item->setFont(font);
            item->setTextAlignment(Qt::AlignCenter);
            ui->tblProjReagentGroup->setItem(row,column,item);
        }
        ui->tblProjReagentGroup->setRowHeight(row,qMax(1,qRound(41*scale_)));
    }
}

void OriginalReagentUsageDialog::setDeviceKey(QString key)
{
    ui->txtDivKey->clear();
    ui->txtDivKey->append(key);
}

void OriginalReagentUsageDialog::setImportResult(bool success)
{
    emit feedbackRequested(success ? tr("下载耗材成功!") : tr("下载耗材失败!"),!success);
}

void OriginalReagentUsageDialog::reject()
{
    if (closingFinished_ || !isVisible()) QDialog::reject();
    else ui->mask->closeAnimation();
}

void OriginalReagentUsageDialog::closeEvent(QCloseEvent *event)
{
    if (closingFinished_) QDialog::closeEvent(event);
    else { event->ignore(); reject(); }
}
