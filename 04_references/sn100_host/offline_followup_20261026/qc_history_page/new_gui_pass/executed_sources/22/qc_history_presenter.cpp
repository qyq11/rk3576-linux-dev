#include "qc_history_presenter.h"
#include "cus_quality_chart.h"
#include <QAtomicInteger>
#include <QListWidget>
#include <QThread>
#include <QSet>
#include <algorithm>

namespace { QAtomicInteger<qint64> historySequence(1000000); }

QcHistoryPresenter::QcHistoryPresenter(QListWidget *list, QObject *parent) : QObject(parent), list_(list)
{
    if (list) connect(list,&QObject::destroyed,this,[this]()
    {
        const bool pending=requestId_!=0;
        requestId_=0;controlId_=0;associationId_=0;cards_.clear();setBusy(false);
        if (pending)
        {
            emit statusChanged(tr("历史图宿主已关闭，已撤销本次展示等待；没有修改数据。"),false);
            emit finished(false);
        }
    });
}
QcHistoryPresenter::~QcHistoryPresenter() { clearCards(); }
void QcHistoryPresenter::setBusy(bool busy)
{
    if (busy_ == busy) return;
    busy_ = busy; emit busyChanged(busy);
}
void QcHistoryPresenter::clearCards()
{
    for (const auto &card : cards_) if (card) card->disconnect(this);
    cards_.clear();
    if (list_) list_->clear();
}
void QcHistoryPresenter::selectAssociation(qint64 controlId, qint64 associationId)
{
    Q_ASSERT(QThread::currentThread() == thread());
    controlId_ = controlId; associationId_ = associationId;
    requestId_ = 0; clearCards();
    if (!list_ || controlId <= 0 || associationId <= 0)
    { setBusy(false); return; }
    requestId_ = historySequence.fetchAndAddRelaxed(1) + 1;
    setBusy(true);
    emit statusChanged(tr("正在读取所选项目的当前及前批历史结果。"), false);
    emit requestHistory(requestId_, associationId_);
}
void QcHistoryPresenter::accept(qint64 requestId, QcHistoryReply reply)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!requestId_ || requestId != requestId_ || reply.associationId() != associationId_ ||
        reply.controlId() != controlId_ || !list_) return;
    if (!reply.complete())
    { reject(requestId, associationId_, QStringLiteral("incomplete_reply"), tr("历史回复不完整，未展示截断数据。")); return; }
    requestId_ = 0; clearCards();
    QcHistorySeriesList series = reply.series();
    std::sort(series.begin(), series.end(), [](const QcHistorySeries &a,const QcHistorySeries &b)
    {return int(a.result().type) < int(b.result().type);});
    int invalid = 0, unitUnknown = 0;
    QSet<QString> excludedReasons;
    for (const QcHistorySeries &entry : series)
    {
        const auto result = entry.result();
        const auto selection = entry.selection();
        for (const auto &excluded:selection.excluded) excludedReasons.insert(excluded.reason);
        if (!result.used || !selection.legacyCardVisible) continue;
        auto *card = new CusQualityChart;
        card->setFixedSize(list_->width(), 235); // 原frmQuality的图卡高度/排列。
        card->setData(reply.controlName(), reply.currentBatch().name, result, selection, entry.summary());
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(list_->width(),235));
        list_->addItem(item); list_->setItemWidget(item,card); cards_.append(card);
        invalid += selection.invalidResultCount;
        if (!card->usesHistoricalDisplayUnits() && entry.validRawCount()) ++unitUnknown;
        connect(card,&CusQualityChart::signalCheckdChanged,this,[this,card]()
        {for (const auto &other:cards_) if(other && other!=card) other->setChecked(false);});
    }
    if (!cards_.isEmpty() && cards_.first()) cards_.first()->clicked();
    setBusy(false);
    QString status;
    if (reply.status() == QcHistoryStatus::NoCurrentBatch) status = tr("未设置当前批次；未猜测默认批次。");
    else if (reply.status() == QcHistoryStatus::NoEnabledResults) status = tr("当前项目没有启用的标准结果图卡。");
    else if (cards_.isEmpty()) status = tr("当前批次没有可展示记录；保持原版不单独展示前批图卡。");
    else status = tr("已读历史图卡%1；无效结果%2；日期/身份等排除%3条；临床规则未启用。")
        .arg(cards_.size()).arg(invalid).arg(reply.excludedRowCount());
    if (cards_.isEmpty() && reply.sourceRowCount()>0)
        status+=tr(" 已读取%1条，日期/身份等排除%2条。").arg(reply.sourceRowCount()).arg(reply.excludedRowCount());
    if (excludedReasons.contains(QStringLiteral("invalid_date"))) status+=tr(" 存在日期无效或缺失的历史记录。");
    if (excludedReasons.contains(QStringLiteral("invalid_time"))) status+=tr(" 存在时间无效或缺失的历史记录。");
    if (unitUnknown) status += tr(" %1张图为原存储数值，历史单位未核实，相关统计不可用。").arg(unitUnknown);
    emit statusChanged(status,invalid>0 || reply.excludedRowCount()>0);
    emit finished(true);
}
void QcHistoryPresenter::reject(qint64 requestId,qint64 associationId,QString code,QString message)
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (!requestId_ || requestId != requestId_ || associationId != associationId_) return;
    requestId_ = 0; clearCards(); setBusy(false);
    emit statusChanged(tr("历史结果读取失败（%1）：%2；未改变元数据或草稿。").arg(code,message),true);
    emit finished(false);
}
