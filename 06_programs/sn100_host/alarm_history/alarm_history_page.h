#ifndef SN100_PRIVATE_ALARM_HISTORY_PAGE_H
#define SN100_PRIVATE_ALARM_HISTORY_PAGE_H
#include "alarm_history_store.h"
#include <QDialog>
#include <QPointer>

class QLabel;
class QCloseEvent;
class CusMessageBox;
namespace Ui { class MaskDlgAbnormal; }

// Original historical tab only. Store and its thread belong to the caller.
// No source is opened until explicit openExistingHistory(path,today).
class AlarmHistoryPage : public QDialog
{
    Q_OBJECT
public:
    struct DisplayBudget
    {
        int rows = 10000; // Candidate only, not original or accepted product limit.
        qint64 textBytes = 16*1024*1024;
        int rowsPerTurn = 32;
    };
    explicit AlarmHistoryPage(AlarmHistory::Store *store,QWidget *parent=nullptr);
    ~AlarmHistoryPage() override;
    bool openExistingHistory(QString absolutePath,QDate capturedToday);
    bool requestClose();
    bool isBusy() const;
    bool isClosing() const { return closingDialog_; }
    bool isComplete() const { return complete_; }
    QString statusText() const { return status_; }
    AlarmHistory::Key inflightKey() const { return request_.key; }
    // Explicit private test/measurement candidate; cannot change during a query.
    bool setDisplayBudget(DisplayBudget budget);
signals:
    void requestOpen(QString absolutePath);
    void requestBegin(AlarmHistory::Request request);
    void requestAdvance(AlarmHistory::Key key,quint64 continuationStep);
    void requestCancel(AlarmHistory::Key key);
    void requestSourceClose();
    void statusChanged(QString text,bool error);
    void busyChanged(bool busy);
    void storageClosed();
    void finishedView(bool complete,qint64 shown,qint64 matching);
    void appendTurnApplied(int rows); // Private consumer observability, no SQL/device effect.
public slots:
    void queryInterval();
    void cancelCurrent();
    void reject() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    Ui::MaskDlgAbnormal *ui;
    QPointer<AlarmHistory::Store> store_;
    QLabel *state_;
    QPointer<QDialog> calendar_;
    QPointer<CusMessageBox> warning_;
    DisplayBudget budget_;
    QString sourcePath_,sourceToken_,status_;
    QDate today_;
    AlarmHistory::Request request_;
    AlarmHistory::Rows pending_;
    AlarmHistory::Completion terminalCompletion_ = AlarmHistory::Completion::IntervalComplete;
    quint64 generation_ = 1,credit_ = 0,lastCredit_ = 0;
    int pendingIndex_ = 0;
    qint64 retainedBytes_ = 0,pendingBytes_ = 0,terminalDelivered_ = 0,terminalMatching_ = 0;
    bool sourceReady_ = false,opening_ = false,active_ = false,cancelling_ = false;
    bool sourceClosing_ = false,closingDialog_ = false,forceClose_ = false;
    bool complete_ = false,terminalPending_ = false,localAbort_ = false,lastBusy_ = false,closedSignal_ = false;
    AlarmHistory::Scope nextOpenScope_ = AlarmHistory::Scope::LatestToday20;
    void beginQuery(AlarmHistory::Scope scope);
    void advanceLater();
    void appendLater();
    void appendTurn();
    void tryComplete();
    void clearDisplay();
    void stopWithError(QString message,bool closeSource);
    void showWarning(QString message);
    void report(QString message,bool error=false);
    void updateControls();
    void calendar(bool first);
    bool sameRequest(const AlarmHistory::Request &reply) const;
    bool visibleRequest(const AlarmHistory::Request &reply) const;
    bool inflightRequest(const AlarmHistory::Request &reply) const;
    void receivedContinuation(AlarmHistory::Request reply,qint64 validated,quint64 step);
    void receivedBatch(AlarmHistory::Request reply,AlarmHistory::Rows rows,bool more,quint64 step);
    void receivedFinished(AlarmHistory::Request reply,AlarmHistory::Completion completion,qint64 delivered,qint64 matching);
    void receivedCancelled(AlarmHistory::Request reply,qint64 delivered);
    void receivedFailed(AlarmHistory::Request reply,AlarmHistory::Error error,QString message,qint64 delivered);
    void sourceClosed();
};
#endif
