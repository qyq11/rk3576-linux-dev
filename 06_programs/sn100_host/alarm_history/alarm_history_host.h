#ifndef SN100_PRIVATE_ALARM_HISTORY_HOST_H
#define SN100_PRIVATE_ALARM_HISTORY_HOST_H
#include "alarm_history_page.h"
#include <QObject>
#include <QPointer>

// 同一原对话框承载当前表和历史表；SQL及线程仍由调用方持有。
class AlarmHistoryHost : public QObject
{
    Q_OBJECT
public:
    AlarmHistoryHost(AlarmHistory::Store *store,QWidget *owner,QObject *parent=nullptr);
    bool showExistingHistory(QString path,QDate today);
    bool enableCurrentAlarms(QString historyPath);
    bool showCurrent();
    void showCurrentObservation(AlarmHistory::CurrentAlarmObservation observation);
    // 只显示“收到的新事件”；不声明写库，也不将它当当前全量查询。
    void showReceivedEvents(QVector<AlarmHistory::CurrentAlarmRow> rows,QString message);
    void setCurrentStatus(QString message,bool error);
    bool requestLeave(QString action);
    bool isBusy() const { return page_&&page_->isBusy(); }
    bool isOpen() const { return !page_.isNull(); }
    AlarmHistoryPage *page() const { return page_; }
public slots:
    void originalEventsCommitted(AlarmHistory::Rows rows);
signals:
    void statusChanged(QString text,bool error);
    void currentDisplayRejected(QString reason);
    void busyChanged(bool busy);
    void storageClosed();
    void viewClosed();
private:
    QPointer<AlarmHistory::Store> store_;
    QPointer<QWidget> owner_;
    QPointer<AlarmHistoryPage> page_;
    QString path_,currentHistoryPath_,currentMessage_;
    bool currentEnabled_=false,currentError_=false;
    AlarmHistoryPage *createPage(QString path);
    void deliverRows(QVector<AlarmHistory::CurrentAlarmRow> rows,QString message,bool error=false);
};
#endif
