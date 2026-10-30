#include "frmmain.h"
#include "ui_frmmain.h"
#include "projects/project_page.h"
#include "reagents/reagents_page.h"
#include "samples/sample_page.h"
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>

namespace
{
ProjectRecords localCatalogProjects(const ProjectRecords &records)
{
    ProjectRecords local;
    for (const auto &record : records)
        if (record.originalFileName.isEmpty()) local.append(record);
    return local;
}
}

// Pages own their edit buffers; the entry point owns their SQL workers. Only
// committed value snapshots cross from project settings into reagent choices.
void frmMain::attachCatalogPages(ProjectPage *projects, ReagentsPage *reagents)
{
    if (!projects || !reagents || projects_ || reagents_)
    {
        qWarning() << "Catalog pages must be attached once before startup";
        return;
    }
    projects_ = projects;reagents_ = reagents;
    const int previous = currentMainPage();
    const auto replace = [this](int index, QWidget *page, const QString &name)
    {
        auto *viewport = new QScrollArea(ui->stkCenterAdptive);
        viewport->setObjectName(name);
        viewport->setFrameShape(QFrame::NoFrame);
        viewport->setWidgetResizable(false);
        // Keep the original 1751x968 canvas. The content stack is a few pixels
        // shorter, so internal scrolling preserves controls at their old positions.
        page->setFixedSize(1751,968);
        viewport->setWidget(page);
        QWidget *placeholder = ui->stkCenterAdptive->widget(index);
        ui->stkCenterAdptive->removeWidget(placeholder);delete placeholder;
        ui->stkCenterAdptive->insertWidget(index,viewport);
        return viewport;
    };
    projectViewport_ = replace(5,projects,QStringLiteral("projectPageViewport"));
    reagentViewport_ = replace(0,reagents,QStringLiteral("reagentPageViewport"));
    ui->stkCenterAdptive->setCurrentIndex(previous);
    projects->setSession(session_);reagents->setSession(session_);
    connect(this,&frmMain::sessionChanged,projects,&ProjectPage::setSession);
    connect(this,&frmMain::sessionChanged,reagents,&ReagentsPage::setSession);
    connect(projects,&ProjectPage::snapshotChanged,reagents,
            [reagents](const ProjectRecords &records, const ProjectUnits &)
    {
        ReagentProjects choices;
        for (const ProjectRecord &record : records)
        {
            // 原项目尚未接入这个模块的独立catalog，不能把原编号当作其外键。
            if (!record.originalFileName.isEmpty()) continue;
            // A disabled project can still be referenced by a saved batch.
            // Preserve its name here; sample task creation filters enabled state.
            ReagentProject choice;choice.id=record.id;choice.name=record.name;choice.enabled=record.enabled;
            for (const ProjectResult &result : record.results)
                if (result.mainResult) choice.unit=result.unit;
            choices.append(choice);
        }
        reagents->setProjects(choices);
    });
    connect(projects,&ProjectPage::statusChanged,this,[this](const QString &message)
    { projectStatus_=message;updateCatalogStatus(); });
    connect(projects,&ProjectPage::operationFinished,this,[this](const QString &,bool success)
    { projectStatusError_=!success;updateCatalogStatus();updateAccountStatus(); });
    connect(projects,&ProjectPage::busyChanged,this,[this](bool)
    { updateCatalogStatus();updateAccountStatus(); });
    connect(reagents,&ReagentsPage::operationMessage,this,[this](const QString &operation,const QString &message)
    { reagentStatus_=operation+QStringLiteral("：")+message;updateCatalogStatus(); });
    connect(reagents,&ReagentsPage::operationFinished,this,[this](const QString &operation,bool success)
    {
        reagentStatusError_=!success;
        // 试剂名称/类型决定高级项目步骤的候选项；提交后再读，不能覆盖项目草稿。
        if (success && (operation==QStringLiteral("保存试剂资料") || operation==QStringLiteral("删除试剂资料"))
            && projects_->isReady() && !projects_->isBusy() && !projects_->hasUnsavedChanges()) projects_->reload();
        updateCatalogStatus();updateAccountStatus();
    });
    connect(reagents,&ReagentsPage::busyChanged,this,[this](bool)
    { updateCatalogStatus();updateAccountStatus(); });
    connect(reagents,&ReagentsPage::readyChanged,this,[this](bool)
    { updateCatalogStatus();updateAccountStatus(); });
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget *,QWidget *now)
    {
        if (now && projects_->isAncestorOf(now)) lastProjectFocus_=now;
        if (now && reagents_->isAncestorOf(now)) lastReagentFocus_=now;
    });
    updateAccountStatus();
}

bool frmMain::catalogPagesIdle() const
{
    return (!projects_ || !projects_->isBusy()) && (!reagents_ || !reagents_->isBusy())
        && (!samples_ || !samples_->isBusy());
}

void frmMain::attachSamplePage(SamplePage *samples)
{
    if (!samples || samples_ || !projects_ || !reagents_)
    {
        qWarning() << "Sample page requires attached catalog pages and must be attached once";
        return;
    }
    samples_=samples;
    if (deviceControl_) attachDeviceTaskStore();
    const int previous=currentMainPage();
    sampleViewport_=new QScrollArea(ui->stkCenterAdptive);
    sampleViewport_->setObjectName(QStringLiteral("samplePageViewport"));
    sampleViewport_->setFrameShape(QFrame::NoFrame);
    sampleViewport_->setWidgetResizable(false);
    samples->setFixedSize(1751,968);
    sampleViewport_->setWidget(samples);
    QWidget *placeholder=ui->stkCenterAdptive->widget(1);
    ui->stkCenterAdptive->removeWidget(placeholder);delete placeholder;
    ui->stkCenterAdptive->insertWidget(1,sampleViewport_);
    ui->stkCenterAdptive->setCurrentIndex(previous);
    samples->setSession(session_);
    if (!samples->originalProfile()) samples->setProjects(localCatalogProjects(projects_->projects()));
    samples->setReagents(reagents_->snapshot());
    connect(this,&frmMain::sessionChanged,samples,&SamplePage::setSession);
    connect(projects_,&ProjectPage::snapshotChanged,samples,
            [samples](ProjectRecords records,ProjectUnits) { if (!samples->originalProfile()) samples->setProjects(localCatalogProjects(records)); });
    connect(reagents_,&ReagentsPage::snapshotChanged,samples,&SamplePage::setReagents);
    connect(samples,&SamplePage::statusChanged,this,[this](const QString &message)
    { sampleStatus_=message;updateCatalogStatus(); });
    connect(samples,&SamplePage::operationFinished,this,[this](const QString &,bool success)
    { sampleStatusError_=!success;updateCatalogStatus();updateAccountStatus(); });
    connect(samples,&SamplePage::busyChanged,this,[this](bool)
    { updateCatalogStatus();updateAccountStatus(); });
    connect(qApp,&QApplication::focusChanged,this,[this](QWidget *,QWidget *now)
    { if (now && samples_->isAncestorOf(now)) lastSampleFocus_=now; });
    updateAccountStatus();
}

bool frmMain::requestCatalogLeave(const QString &action, bool currentOnly)
{
    if (projects_ && (!currentOnly || currentMainPage()==5) && !projects_->prepareToLeave())
    {
        emit navigationRejected(tr("项目/单位：请先完成操作或处理草稿，再%1。").arg(action));
        return false;
    }
    if (reagents_ && (!currentOnly || currentMainPage()==0) && !reagents_->prepareToLeave())
    {
        emit navigationRejected(tr("试剂：请先完成操作或处理草稿，再%1。").arg(action));
        return false;
    }
    if (samples_ && (!currentOnly || currentMainPage()==1) && !samples_->prepareToLeave())
    {
        emit navigationRejected(tr("样本/任务：请先完成操作或处理草稿，再%1。").arg(action));
        return false;
    }
    return true;
}

void frmMain::restoreCatalogFocus()
{
    const int index=currentMainPage();
    if (index==5 && projects_ && projects_->isReady() && !projects_->isBusy()
        && !projects_->hasUnsavedChanges()) projects_->reload();
    QPointer<QWidget> target=index==5 ? lastProjectFocus_ : index==1 ? lastSampleFocus_ : lastReagentFocus_;
    QScrollArea *viewport=index==5 ? projectViewport_ : index==1 ? sampleViewport_ : reagentViewport_;
    QWidget *page=index==5 ? static_cast<QWidget *>(projects_) : index==1 ? static_cast<QWidget *>(samples_)
        : static_cast<QWidget *>(reagents_);
    if ((index!=0 && index!=1 && index!=5) || !viewport || !page) return;
    QTimer::singleShot(0,this,[viewport,page,target]()
    {
        QWidget *focus=target && target->isVisible() && target->isEnabled() ? target.data() : page;
        viewport->ensureWidgetVisible(focus,10,10);
        focus->setFocus(Qt::OtherFocusReason);
    });
}

void frmMain::updateCatalogStatus()
{
    const int index=currentMainPage();
    const bool project=index==5 && projects_;
    const bool reagent=index==0 && reagents_;
    const bool sample=index==1 && samples_;
    if (!project && !reagent && !sample) return;
    const bool busy=project ? projects_->isBusy() : sample ? samples_->isBusy() : reagents_->isBusy();
    const bool ready=project ? projects_->isReady() : sample ? samples_->isReady() : reagents_->isReady();
    const bool dirty=project ? projects_->hasUnsavedChanges() : sample ? samples_->hasUnsavedChanges() : reagents_->hasUnsavedChanges();
    const bool error=project ? projectStatusError_ : sample ? sampleStatusError_ : reagentStatusError_;
    const QString state=error ? tr("脱机／失败") : busy ? tr("脱机／处理中")
        : dirty ? tr("脱机／未保存") : ready ? tr("脱机／就绪") : tr("脱机／未知");
    const QString detail=project ? projectStatus_ : sample ? sampleStatus_ : reagentStatus_;
    ui->lblDivState->setText(ui->lblDivState->fontMetrics().elidedText(state,Qt::ElideRight,ui->lblDivState->width()));
    ui->lblDivState->setStyleSheet(error ? QStringLiteral("QLabel{color:#E83323;}")
                                                      : QStringLiteral("QLabel{color:rgb(170,170,170);}"));
    ui->lblDivState->setToolTip(detail);
    ui->statusArea->setToolTip(tr("脱机；仪器状态未知。%1：%2").arg(project ? tr("项目/单位") : sample ? tr("样本/任务") : tr("试剂"),detail));
}
