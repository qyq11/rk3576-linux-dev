#include "alarm_history_page.h"
#include "ui_maskdlgabnormal.h"
#include "ui_cusmaskcalendar.h"
#include "calendar/cuscalendar.h"
#include "ui/cusdialogmask.h"
#include "ui/cusmessagebox.h"
#include "qtmaterialscrollbar.h"
#include <QAtomicInteger>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScroller>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <algorithm>

using namespace AlarmHistory;
namespace
{
QAtomicInteger<quint64> sequence(0);
bool validDate(QDate date)
{
    return date.isValid()&&date.year()>=1&&date.year()<=9999;
}
QDate readDate(QLineEdit *edit)
{
    const QDate date=QDate::fromString(edit->text(),QStringLiteral("yyyy/MM/dd"));
    return validDate(date)&&date.toString(QStringLiteral("yyyy/MM/dd"))==edit->text()?date:QDate();
}
void fonts(QWidget *root)
{
#ifdef Q_OS_LINUX
    const auto children=root->findChildren<QWidget*>();
    for(QWidget *widget:children)
    {
        QFont font=widget->font();
        if(font.family().contains(QStringLiteral("苹方")))
        {font.setFamily(QStringLiteral("Noto Sans CJK SC"));widget->setFont(font);}
        QString style=widget->styleSheet();
        if(style.contains(QStringLiteral("苹方 粗体")))
        {style.replace(QStringLiteral("苹方 粗体"),QStringLiteral("Noto Sans CJK SC"));widget->setStyleSheet(style);}
    }
#else
    Q_UNUSED(root);
#endif
}
QString originalButtons()
{
    // Same already-migrated theme keys; no old config/theme file is opened.
    return QStringLiteral(".QPushButton{background:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
        ".QPushButton:hover{background:rgba(42,118,242,170);}"
        ".QPushButton:pressed{background:rgba(135,206,250,255);}");
}
class CalendarPopup : public QDialog
{
public:
    explicit CalendarPopup(QWidget *parent,QDate selected):QDialog(parent),ui(new Ui::CusMaskCalendar)
    {
        ui->setupUi(this);setObjectName(QStringLiteral("AlarmHistoryCalendar"));
        setAttribute(Qt::WA_DeleteOnClose);fonts(this);ui->widget->updateCalendar(selected);
        connect(ui->widget,&CusCalendar::signalCloseDialog,this,&CalendarPopup::reject);
        connect(ui->mask,&CusDialogMask::closeFinished,this,[this](){force_=true;QDialog::reject();});
        ui->mask->setupForm();
    }
    ~CalendarPopup() override {delete ui;}
    CusCalendar *calendar()const{return ui->widget;}
    void openAsync(){show();ui->mask->showAnimation();}
    void reject()override{if(force_)QDialog::reject();else ui->mask->closeAnimation();}
protected:
    void closeEvent(QCloseEvent *event)override
    {if(force_)QDialog::closeEvent(event);else{event->ignore();reject();}}
private:
    Ui::CusMaskCalendar *ui;bool force_=false;
};
qint64 visibleBytes(const Row &row)
{
    return row.errorCode.toUtf8().size()+qint64(row.dateTime.toUtf8().size())+
        row.user.toUtf8().size()+row.description.toUtf8().size();
}
}

AlarmHistoryPage::AlarmHistoryPage(Store *store,QWidget *parent)
    : QDialog(parent),ui(new Ui::MaskDlgAbnormal),store_(store),state_(nullptr)
{
    Store::registerTypes();ui->setupUi(this);setObjectName(QStringLiteral("AlarmHistoryPage"));
    setAttribute(Qt::WA_DeleteOnClose);setStyleSheet(originalButtons());fonts(this);
    // Root approved one plain status line in the original30px unused top strip.
    state_=new QLabel(ui->view_frame);state_->setObjectName(QStringLiteral("alarmHistoryStatus"));
    state_->setGeometry(14,1,925,28);state_->setFont(ui->lbl2->font());
    state_->setTextFormat(Qt::PlainText);state_->setAttribute(Qt::WA_TransparentForMouseEvents);
    state_->setStyleSheet(QStringLiteral("color:#444444;background:transparent;border:none;"));
    ui->stackedWidget->setCurrentIndex(1);
    connect(ui->stackedWidget,&QStackedWidget::currentChanged,this,[this](int index)
    {if(index!=1){QSignalBlocker blocker(ui->stackedWidget);ui->stackedWidget->setCurrentIndex(1);}});
    const QString unknown=tr("只读历史；当前告警、严重度、设备处置和互锁未知，功能未接入。");
    for(QWidget *control:QList<QWidget*>{ui->tblAbnormal,ui->txtTestInfo,ui->btnReset,ui->btnReturn,ui->btnErrorLog})
    {control->setEnabled(false);control->setToolTip(unknown);}
    ui->tblAbnormal->setRowCount(0);ui->txtTestInfo->setReadOnly(true);ui->txtTestInfo->setPlainText(unknown);
    auto *table=ui->tlbAbnormallog;
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);table->setFrameShape(QFrame::NoFrame);
    QScroller::scroller(table)->grabGesture(table,QScroller::LeftMouseButtonGesture);
    auto *scrollbar=new QtMaterialScrollBar(table);scrollbar->setSliderColor(QColor(34,34,34,50));
    scrollbar->setBackgroundColor(Qt::transparent);scrollbar->setCanvasColor(Qt::transparent);scrollbar->setHideOnMouseOut(true);
    table->setVerticalScrollBar(scrollbar);
    connect(ui->btnClose,&QPushButton::clicked,this,[this](){requestClose();});
    connect(ui->btnQuery,&QPushButton::clicked,this,&AlarmHistoryPage::queryInterval);
    connect(ui->btnCalendarSetStart,&QPushButton::clicked,this,[this](){calendar(true);});
    connect(ui->btnCalendarSetEnd,&QPushButton::clicked,this,[this](){calendar(false);});
    connect(ui->mask,&CusDialogMask::closeFinished,this,[this]()
    {forceClose_=true;closingDialog_=false;updateControls();QDialog::reject();});
    ui->mask->setupForm();
    if(store)
    {
        connect(this,&AlarmHistoryPage::requestOpen,store,&Store::openExisting,Qt::QueuedConnection);
        connect(this,&AlarmHistoryPage::requestBegin,store,&Store::begin,Qt::QueuedConnection);
        connect(this,&AlarmHistoryPage::requestAdvance,store,&Store::advance,Qt::QueuedConnection);
        connect(this,&AlarmHistoryPage::requestCancel,store,&Store::cancel,Qt::QueuedConnection);
        connect(this,&AlarmHistoryPage::requestSourceClose,store,&Store::close,Qt::QueuedConnection);
        connect(store,&Store::opened,this,[this](QString token,QString path)
        {
            if(closingDialog_)return;
            if(!opening_||token.isEmpty()||path!=QFileInfo(sourcePath_).canonicalFilePath())
            {stopWithError(tr("历史来源回执与显式打开路径不符，未展示。"),true);return;}
            opening_=false;sourceReady_=true;sourceToken_=token;closedSignal_=false;beginQuery(nextOpenScope_);
        },Qt::QueuedConnection);
        connect(store,&Store::openFailed,this,[this](Error,QString message)
        {
            if(closingDialog_)return;
            opening_=false;sourceReady_=false;sourceToken_.clear();clearDisplay();
            report(tr("历史来源打开失败：%1；没有创建空库或空历史成功。").arg(message),true);
            showWarning(status_);updateControls();emit finishedView(false,0,0);
        },Qt::QueuedConnection);
        connect(store,&Store::continuationNeeded,this,&AlarmHistoryPage::receivedContinuation,Qt::QueuedConnection);
        connect(store,&Store::batchReady,this,&AlarmHistoryPage::receivedBatch,Qt::QueuedConnection);
        connect(store,&Store::finished,this,&AlarmHistoryPage::receivedFinished,Qt::QueuedConnection);
        connect(store,&Store::failed,this,&AlarmHistoryPage::receivedFailed,Qt::QueuedConnection);
        connect(store,&Store::cancelled,this,&AlarmHistoryPage::receivedCancelled,Qt::QueuedConnection);
        connect(store,&Store::requestRejected,this,[this](Key key,Error,QString message)
        {
            if(!active_||!(key==request_.key)||cancelling_||closingDialog_)return;
            stopWithError(tr("历史请求被拒绝：%1；未保留部分结果。").arg(message),true);
        },Qt::QueuedConnection);
        connect(store,&Store::closed,this,&AlarmHistoryPage::sourceClosed,Qt::QueuedConnection);
        connect(store,&QObject::destroyed,this,[this]()
        {
            store_=nullptr;
            if(closedSignal_)return; // Expected root-owned deletion after actual closed.
            opening_=active_=cancelling_=sourceClosing_=false;sourceReady_=false;clearDisplay();
            report(tr("历史worker已销毁；未据此声明正常SQL关闭。"),true);
            if(closingDialog_){forceClose_=true;closingDialog_=false;QDialog::reject();}
            updateControls();
        });
    }
    report(tr("未绑定历史来源；当前实时告警未知。"));updateControls();
}
AlarmHistoryPage::~AlarmHistoryPage()
{
    if(calendar_){calendar_->disconnect(this);delete calendar_.data();}
    if(warning_){warning_->disconnect(this);delete warning_.data();}
    // Normal owner path waits storageClosed. This is only external-destruction
    // cleanup; it does not prove normal close/join and never blocks the GUI.
    if(store_&&!closedSignal_)QMetaObject::invokeMethod(store_,"close",Qt::QueuedConnection);
    delete ui;
}
bool AlarmHistoryPage::isBusy()const
{
    return opening_||active_||sourceClosing_||closingDialog_||!pending_.isEmpty()||calendar_||warning_;
}
bool AlarmHistoryPage::setDisplayBudget(DisplayBudget budget)
{
    if(isBusy()||budget.rows<1||budget.rows>10000||budget.textBytes<1||budget.textBytes>16*1024*1024||
        budget.rowsPerTurn<1||budget.rowsPerTurn>32)return false;
    budget_=budget;
    if(ui->tlbAbnormallog->rowCount()>budget.rows||retainedBytes_>budget.textBytes)
    {clearDisplay();report(tr("显示候选容量已改变，请重新查询；未保留超出新候选的旧结果。"));}
    return true;
}
bool AlarmHistoryPage::openExistingHistory(QString path,QDate today)
{
    if(isBusy()||sourceReady_)return false;
    show();ui->mask->showAnimation();
    if(!store_||!QFileInfo(path).isAbsolute()||path.contains(QChar::Null)||!validDate(today))
    {report(tr("请显式提供既有历史库绝对路径和合法当天日期；未打开或造库。"),true);showWarning(status_);return false;}
    sourcePath_=path;today_=today;nextOpenScope_=Scope::LatestToday20;closedSignal_=false;
    ui->ldtStartDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
    ui->ldtEndDate->setText(today.toString(QStringLiteral("yyyy/MM/dd")));
    clearDisplay();opening_=true;report(tr("正在只读打开历史来源；当前实时告警未知。"));updateControls();emit requestOpen(path);return true;
}
void AlarmHistoryPage::report(QString text,bool error)
{
    status_=text;state_->setText(state_->fontMetrics().elidedText(text,Qt::ElideRight,state_->width()));
    state_->setToolTip(text);state_->setAccessibleName(text);ui->tlbAbnormallog->setToolTip(text);
    setWindowTitle(text);emit statusChanged(text,error);
}
void AlarmHistoryPage::updateControls()
{
    const bool choosing=!calendar_.isNull(),warning=!warning_.isNull();
    const bool stationary=!opening_&&!active_&&!sourceClosing_&&!closingDialog_&&!choosing&&!warning;
    ui->btnQuery->setEnabled((stationary&&!sourcePath_.isEmpty())||(active_&&!cancelling_&&!localAbort_&&!closingDialog_&&!warning));
    ui->btnQuery->setText(active_?tr("取消查询"):tr("查询"));
    ui->btnCalendarSetStart->setEnabled(stationary&&!sourcePath_.isEmpty());
    ui->btnCalendarSetEnd->setEnabled(stationary&&!sourcePath_.isEmpty());
    ui->btnClose->setEnabled(!closingDialog_&&!warning);
    const bool busy=isBusy();if(busy!=lastBusy_){lastBusy_=busy;emit busyChanged(busy);}
}
void AlarmHistoryPage::showWarning(QString message)
{
    if(warning_||closingDialog_)return;
    auto *warning=new CusMessageBox(this);warning_=warning;warning->setObjectName(QStringLiteral("AlarmHistoryWarning"));
    warning->setAttribute(Qt::WA_DeleteOnClose);
    connect(warning,&QObject::destroyed,this,[this](){warning_=nullptr;updateControls();});
    warning->openWarningBox(message);updateControls();
}
void AlarmHistoryPage::clearDisplay()
{
    pending_.clear();pendingIndex_=0;pendingBytes_=retainedBytes_=0;credit_=0;terminalPending_=false;complete_=false;
    QSignalBlocker blocker(ui->tlbAbnormallog);ui->tlbAbnormallog->setRowCount(0);
}
void AlarmHistoryPage::queryInterval()
{
    if(active_){cancelCurrent();return;}
    if(isBusy())return;
    if(!validDate(readDate(ui->ldtStartDate))||!validDate(readDate(ui->ldtEndDate))||readDate(ui->ldtStartDate)>readDate(ui->ldtEndDate))
    {stopWithError(tr("请选择合法起止日期，起始日期不能晚于终止日期；未查询。"),false);return;}
    if(!sourceReady_)
    {
        if(sourcePath_.isEmpty())return;
        nextOpenScope_=Scope::DateInterval;opening_=true;closedSignal_=false;clearDisplay();
        report(tr("按本次操作重新只读打开显式历史来源；未自动造库。"));updateControls();emit requestOpen(sourcePath_);return;
    }
    beginQuery(Scope::DateInterval);
}
void AlarmHistoryPage::beginQuery(Scope scope)
{
    clearDisplay();++generation_;request_=Request();request_.key={sequence.fetchAndAddRelaxed(1)+1,generation_,sourceToken_};
    request_.scope=scope;request_.first=readDate(ui->ldtStartDate);request_.last=readDate(ui->ldtEndDate);
    request_.today=today_;request_.batchSize=128;lastCredit_=0;active_=true;cancelling_=localAbort_=false;
    report(scope==Scope::LatestToday20?tr("正在校验并读取当天最近20条历史；尚未完成，实时告警未知。"):
        tr("正在校验并读取完整日期区间；尚未完成，实时告警未知。"));updateControls();emit requestBegin(request_);
}
bool AlarmHistoryPage::sameRequest(const Request &reply)const
{
    return reply.key==request_.key&&reply.scope==request_.scope&&reply.first==request_.first&&reply.last==request_.last&&
        reply.today==request_.today&&reply.batchSize==request_.batchSize;
}
bool AlarmHistoryPage::inflightRequest(const Request &reply)const{return active_&&sameRequest(reply);}
bool AlarmHistoryPage::visibleRequest(const Request &reply)const
{
    return inflightRequest(reply)&&!cancelling_&&!closingDialog_&&!localAbort_&&reply.key.viewGeneration==generation_&&
        reply.key.sourceToken==sourceToken_&&reply.first==readDate(ui->ldtStartDate)&&reply.last==readDate(ui->ldtEndDate);
}
void AlarmHistoryPage::receivedContinuation(Request reply,qint64 validated,quint64 step)
{
    if(!visibleRequest(reply))return;
    if(!pending_.isEmpty()||credit_||step==0||step<=lastCredit_)
    {stopWithError(tr("历史续取回执次序异常；未展示部分结果。"),true);return;}
    lastCredit_=credit_=step;report(tr("正在分批校验历史：已校验%1条，尚未完整；实时告警未知。").arg(validated));advanceLater();
}
void AlarmHistoryPage::advanceLater()
{
    const Request captured=request_;const quint64 step=credit_;
    QTimer::singleShot(0,this,[this,captured,step]()
    {if(!visibleRequest(captured)||!pending_.isEmpty()||!step||credit_!=step)return;
        credit_=0;emit requestAdvance(captured.key,step);});
}
void AlarmHistoryPage::receivedBatch(Request reply,Rows rows,bool more,quint64 step)
{
    if(!visibleRequest(reply))return;
    if(!pending_.isEmpty()||credit_||rows.isEmpty()||rows.size()>reply.batchSize||
        (more?(step==0||step<=lastCredit_):step!=0))
    {stopWithError(tr("历史数据批/续取凭据异常；未标为完整。"),true);return;}
    qint64 bytes=0;for(const Row &row:rows)bytes+=visibleBytes(row);
    if(ui->tlbAbnormallog->rowCount()+rows.size()>budget_.rows||retainedBytes_+bytes>budget_.textBytes)
    {stopWithError(tr("历史超过显示候选容量%1行/%2字节；已清空并取消，不能把部分结果当作完整。请缩小日期范围。")
        .arg(budget_.rows).arg(budget_.textBytes),false);return;}
    pending_=rows;pendingIndex_=0;pendingBytes_=bytes;
    if(more)lastCredit_=credit_=step;
    appendLater();
}
void AlarmHistoryPage::appendLater()
{
    const Request captured=request_;
    QTimer::singleShot(0,this,[this,captured](){if(visibleRequest(captured))appendTurn();});
}
void AlarmHistoryPage::appendTurn()
{
    if(!visibleRequest(request_)||pending_.isEmpty())return;
    auto *table=ui->tlbAbnormallog;const QSignalBlocker blocker(table);int added=0;
    while(pendingIndex_<pending_.size()&&added<budget_.rowsPerTurn)
    {
        const Row &row=pending_.at(pendingIndex_++);const int index=table->rowCount();table->insertRow(index);
        const QStringList texts={row.errorCode,row.dateTime,row.user,row.description};
        for(int column=0;column<4;++column)
        {
            auto *item=new QTableWidgetItem(texts.at(column));item->setToolTip(texts.at(column));
            item->setTextAlignment((column==3?Qt::AlignLeft:Qt::AlignHCenter)|Qt::AlignVCenter);
            if(column==0)item->setData(Qt::UserRole,row.id);table->setItem(index,column,item);
        }
        table->setRowHeight(index,41);table->resizeRowToContents(index);++added;
    }
    emit appendTurnApplied(added);report(tr("历史已追加%1条，仍在处理，尚未完整；实时告警未知。").arg(table->rowCount()));
    if(pendingIndex_<pending_.size()){appendLater();return;}
    retainedBytes_+=pendingBytes_;pendingBytes_=0;pending_.clear();pendingIndex_=0;
    if(terminalPending_)tryComplete();else if(credit_)advanceLater();
}
void AlarmHistoryPage::receivedFinished(Request reply,Completion completion,qint64 delivered,qint64 matching)
{
    if(!inflightRequest(reply))return;
    if(cancelling_||localAbort_||closingDialog_)
    {
        active_=false;cancelling_=false;clearDisplay();
        if(!localAbort_&&!closingDialog_)report(tr("历史查询已取消；未保留部分行，实时告警未知。"));
        updateControls();emit finishedView(false,0,matching);return;
    }
    if(!visibleRequest(reply))return;
    terminalPending_=true;terminalCompletion_=completion;terminalDelivered_=delivered;terminalMatching_=matching;tryComplete();
}
void AlarmHistoryPage::tryComplete()
{
    if(!terminalPending_||!pending_.isEmpty()||!visibleRequest(request_))return;
    const bool recent=request_.scope==Scope::LatestToday20;
    const qint64 expected=recent?std::min<qint64>(20,terminalMatching_):terminalMatching_;
    if(terminalMatching_<0||terminalDelivered_!=expected||ui->tlbAbnormallog->rowCount()!=terminalDelivered_||
        terminalCompletion_!=(recent?Completion::RecentTodayComplete:Completion::IntervalComplete))
    {stopWithError(tr("历史完整回执与已显示行数/范围不符；已清空，不标完整成功。"),true);return;}
    active_=false;complete_=true;terminalPending_=false;credit_=0;
    report(recent?tr("当天最近20条已完成：显示%1条，共匹配%2条；不是完整区间，实时告警未知。")
        .arg(terminalDelivered_).arg(terminalMatching_):tr("日期区间完整读取：%1条；实时告警未知。")
        .arg(terminalDelivered_));updateControls();emit finishedView(true,terminalDelivered_,terminalMatching_);
}
void AlarmHistoryPage::cancelCurrent()
{
    if(!active_||cancelling_||closingDialog_)return;
    // SQL may already have delivered its terminal receipt while bounded GUI
    // append is still active. That receipt ends SQL ownership; waiting for a
    // second terminal after cancelling an idle Store would leave this view busy.
    const bool sqlFinished=terminalPending_;
    cancelling_=true;++generation_;clearDisplay();
    if(sqlFinished)
    {
        active_=cancelling_=false;
        report(tr("已取消剩余历史显示；SQL终态已收到，未保留部分行或声明完整结果。"));
        updateControls();emit finishedView(false,0,0);return;
    }
    report(tr("正在取消历史查询并清表；长SQL返回后才能处理，不保证硬取消时延。"));
    updateControls();emit requestCancel(request_.key);
}
void AlarmHistoryPage::receivedCancelled(Request reply,qint64)
{
    if(!inflightRequest(reply))return;
    active_=false;cancelling_=false;clearDisplay();
    if(!localAbort_&&!closingDialog_)report(tr("历史查询已取消；没有完整结果，实时告警未知。"));
    updateControls();emit finishedView(false,0,0);
}
void AlarmHistoryPage::receivedFailed(Request reply,Error,QString message,qint64)
{
    if(!inflightRequest(reply))return;
    if(closingDialog_){active_=false;clearDisplay();return;}
    stopWithError((localAbort_?status_+QStringLiteral(" "):QString())+tr("历史查询失败：%1；已清表，未完整。").arg(message),true);
}
void AlarmHistoryPage::stopWithError(QString message,bool closeSource)
{
    localAbort_=true;++generation_;clearDisplay();report(message,true);showWarning(message);
    if(closeSource)
    {
        opening_=active_=cancelling_=false;sourceReady_=false;sourceToken_.clear();sourceClosing_=true;
        emit requestSourceClose();emit finishedView(false,0,0);
    }
    else if(active_){cancelling_=true;emit requestCancel(request_.key);}
    else emit finishedView(false,0,0);
    updateControls();
}
void AlarmHistoryPage::calendar(bool first)
{
    if(isBusy())return;
    QDate date=readDate(first?ui->ldtStartDate:ui->ldtEndDate);if(!validDate(date))date=today_;
    if(!validDate(date))return;
    auto *popup=new CalendarPopup(this,date);calendar_=popup;const quint64 captured=generation_;
    connect(popup->calendar(),&CusCalendar::signalSelectDateChanged,this,[this,popup,first,captured](QString text)
    {
        const QDate date=QDate::fromString(text,QStringLiteral("yyyy/MM/dd"));
        if(captured==generation_&&!active_&&!opening_&&!sourceClosing_&&!closingDialog_&&validDate(date))
        {
            auto *edit=first?ui->ldtStartDate:ui->ldtEndDate;
            const QString selected=date.toString(QStringLiteral("yyyy/MM/dd"));
            if(edit->text()!=selected)
            {edit->setText(selected);++generation_;clearDisplay();report(tr("日期已变更，尚未查询；先前日期结果已清空，实时告警未知。"));}
        }
        popup->reject();
    });
    connect(popup,&QObject::destroyed,this,[this]()
    {calendar_=nullptr;updateControls();if(closingDialog_&&closedSignal_)ui->mask->closeAnimation();});
    popup->openAsync();updateControls();
}
bool AlarmHistoryPage::requestClose()
{
    if(forceClose_)return true;
    if(warning_)return false;
    if(closingDialog_)return false;
    closingDialog_=true;setProperty("closingHistory",true);++generation_;clearDisplay();
    if(calendar_)calendar_->reject();
    report(tr("正在关闭只读历史来源；等待SQL返回/释放，不能声明硬取消或设备动作完成。"));
    if(store_&&!sourceClosing_){sourceClosing_=true;emit requestSourceClose();}
    else if(!store_){forceClose_=true;closingDialog_=false;QDialog::reject();}
    updateControls();return false;
}
void AlarmHistoryPage::sourceClosed()
{
    const bool hadPendingView=active_;
    ++generation_;clearDisplay();
    sourceReady_=opening_=active_=cancelling_=sourceClosing_=false;sourceToken_.clear();closedSignal_=true;
    if(!closingDialog_&&!localAbort_)report(tr("历史来源已关闭并清表；没有当前查询结果，实时告警未知。"));
    if(hadPendingView)emit finishedView(false,0,0);
    updateControls();emit storageClosed();
    if(closingDialog_&&!calendar_)ui->mask->closeAnimation();
}
void AlarmHistoryPage::reject(){if(forceClose_)QDialog::reject();else requestClose();}
void AlarmHistoryPage::closeEvent(QCloseEvent *event)
{if(forceClose_)QDialog::closeEvent(event);else{event->ignore();requestClose();}}
