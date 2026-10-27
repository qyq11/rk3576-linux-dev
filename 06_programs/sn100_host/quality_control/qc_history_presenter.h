#ifndef SN100_QC_HISTORY_PRESENTER_H
#define SN100_QC_HISTORY_PRESENTER_H
#include "qc_history_reply.h"
#include <QObject>
#include <QPointer>

class QListWidget;
class CusQualityChart;

// 主线程只消费worker完成的值快照，不在GUI重复解析/查询/统计。
class QcHistoryPresenter : public QObject
{
    Q_OBJECT
public:
    explicit QcHistoryPresenter(QListWidget *list, QObject *parent = nullptr);
    ~QcHistoryPresenter() override;
    bool isBusy() const { return busy_; }
    qint64 requestId() const { return requestId_; }
    void selectAssociation(qint64 controlId, qint64 associationId);
signals:
    void requestHistory(qint64 requestId, qint64 associationId);
    void busyChanged(bool busy);
    void statusChanged(QString message, bool error);
    void finished(bool success);
public slots:
    void accept(qint64 requestId, QcHistoryReply reply);
    void reject(qint64 requestId, qint64 associationId, QString code, QString message);
private:
    QPointer<QListWidget> list_;
    QList<QPointer<CusQualityChart>> cards_;
    qint64 controlId_ = 0, associationId_ = 0, requestId_ = 0;
    bool busy_ = false;
    void setBusy(bool busy);
    void clearCards();
};
#endif
