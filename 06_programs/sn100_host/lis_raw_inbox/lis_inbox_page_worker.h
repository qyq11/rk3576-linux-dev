#ifndef SN100_LIS_INBOX_PAGE_WORKER_H
#define SN100_LIS_INBOX_PAGE_WORKER_H

#include "lis_inbox_store.h"

// Dedicated file bridge. Move this object and its Store child before initialize.
// No SQL here; the unchanged Store owns every transaction and SQL connection.
class LisInboxPageWorker : public QObject
{
    Q_OBJECT
public:
    explicit LisInboxPageWorker(QObject *parent = nullptr);
public slots:
    void initialize(QString directory, AccountSession session);
    void setSession(AccountSession session);
    void importFile(qint64 request, QString path, QString identity, QString expectedControl);
    void discardDraft(qint64 request);
    void query(qint64 request, int offset);
    void detail(qint64 request, qint64 id);
    void close();
signals:
    void ready(QString databasePath);
    void ingested(qint64 request, LisDsrRaw::Message actual, bool duplicate);
    void listReady(qint64 request, LisDsrRaw::Summaries rows, qint64 total);
    void detailReady(qint64 request, LisDsrRaw::Message actual);
    void draftDiscarded(qint64 request);
    void error(qint64 request, QString operation, QString message);
    void closed();
private:
    bool onWorker(qint64 request, const QString &operation);
    bool readExplicitFile(const QString &path, QByteArray *bytes, QString *failure);
    LisInboxStore *store_;
    AccountSession session_;
    QString identity_, path_, expectedControl_;
    QByteArray frame_, fileStamp_;
    bool ready_ = false, closing_ = false, frameCaptured_ = false;
};
#endif
