#ifndef SN100_LIS_INBOX_PAGE_HOST_H
#define SN100_LIS_INBOX_PAGE_HOST_H

#include "data/account_store.h"
#include <QObject>
#include <QPointer>

class QWidget;
class QThread;
class LisInboxPage;
class LisInboxPageWorker;

// Self-owned until the worker has acknowledged close and its thread has finished.
// Call requestClose and await closed before quitting the application. Never delete
// this host directly or stop its thread externally. No SQL or file reads in Host.
class LisInboxPageHost : public QObject
{
    Q_OBJECT
public:
    static LisInboxPageHost *open(QWidget *parent, QString absoluteInboxDirectory, AccountSession initialSession);
public slots:
    void setSession(AccountSession session);
    void requestClose();
signals:
    void statusChanged(QString message, bool error);
    void closed(bool storeCloseAcknowledged);
    void initializeWorker(QString directory, AccountSession session);
    void sessionToWorker(AccountSession session);
    void importToWorker(qint64 request, QString path, QString identity, QString expected);
    void discardToWorker(qint64 request);
    void queryWorker(qint64 request, int offset);
    void detailWorker(qint64 request, qint64 id);
    void closeWorker();
private:
    explicit LisInboxPageHost(QWidget *parent, QString directory, AccountSession session);
    ~LisInboxPageHost() override;
    enum class Pending { None, Initialize, Import, Discard, List, Detail };
    bool onGui();
    bool mayImport() const;
    bool acceptRequest(bool requiresReady = true);
    qint64 begin(Pending operation);
    void updateView();
    void report(QString message, bool error = false);
    void replaceDraft(QString newPath);
    void importDraft(QString expected);
    void loadPage(int offset);
    void loadDetail(qint64 id);
    void reopen();
    void finishThreadStop();
    void handleError(qint64 request, QString operation, QString message);
    QThread *thread_;
    LisInboxPageWorker *worker_;
    QPointer<LisInboxPage> page_;
    AccountSession session_;
    QString directory_, draftPath_, identity_, expectedControl_, replacementPath_;
    qint64 serial_ = 0, activeRequest_ = 0;
    int requestedOffset_ = 0;
    Pending pending_ = Pending::Initialize;
    bool ready_ = false, closing_ = false, storeClosed_ = false, committed_ = false;
};
#endif
