#ifndef SN100_PRIVATE_ALARM_HISTORY_HOST_H
#define SN100_PRIVATE_ALARM_HISTORY_HOST_H
#include "alarm_history_page.h"
#include <QObject>
#include <QPointer>

// Thin proposed shell adapter. Root owns top badge enablement and worker thread.
// This QObject neither discovers/creates a DB nor maps history to current alarms.
class AlarmHistoryHost : public QObject
{
    Q_OBJECT
public:
    AlarmHistoryHost(AlarmHistory::Store *store,QWidget *owner,QObject *parent=nullptr);
    bool showExistingHistory(QString path,QDate today);
    bool requestLeave(QString action);
    bool isBusy() const { return page_&&page_->isBusy(); }
    bool isOpen() const { return !page_.isNull(); }
    AlarmHistoryPage *page() const { return page_; }
signals:
    void statusChanged(QString text,bool error);
    void busyChanged(bool busy);
    void storageClosed();
    void viewClosed();
private:
    QPointer<AlarmHistory::Store> store_;
    QPointer<QWidget> owner_;
    QPointer<AlarmHistoryPage> page_;
    QString path_;
};
#endif
