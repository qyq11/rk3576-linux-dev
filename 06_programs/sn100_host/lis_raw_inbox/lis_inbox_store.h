#ifndef SN100_LIS_INBOX_STORE_H
#define SN100_LIS_INBOX_STORE_H
#include "lis_dsr_raw.h"
#include "data/account_store.h"
#include <QObject>
#include <QSqlDatabase>

// 独立本地原报文收件箱；构造/移动后再初始化，SQL连接始终在工作线程。
class LisInboxStore : public QObject
{
    Q_OBJECT
public:
    explicit LisInboxStore(QObject *parent = nullptr);
    ~LisInboxStore() override;
    static void registerTypes();
public slots:
    void initialize(QString absoluteDirectory);
    void setSession(AccountSession session);
    void ingestBytes(qint64 requestId, QString receptionIdentity, QString profile,
                     QString expectedControl, QByteArray frame);
    // File must be an explicit regular, non-symlink file beneath this inbox/inputs/.
    void ingestFile(qint64 requestId, QString receptionIdentity, QString profile,
                    QString expectedControl, QString absoluteFile);
    void query(qint64 requestId, int offset = 0, int limit = 20);
    void detail(qint64 requestId, qint64 id);
    void close();
signals:
    void ready(QString databasePath);
    void ingested(qint64 requestId, LisDsrRaw::Message actual, bool duplicate);
    void listReady(qint64 requestId, LisDsrRaw::Summaries rows, qint64 total);
    void detailReady(qint64 requestId, LisDsrRaw::Message actual);
    void error(qint64 requestId, QString operation, QString message);
    void closed();
private:
    bool checkThread(qint64 requestId, const QString &operation);
    bool checkReady(qint64 requestId, const QString &operation);
    bool canIngest(qint64 requestId, const QString &operation);
    bool createSchema(QString *error);
    bool validateSchema(QString *error);
    bool readMessage(qint64 id, LisDsrRaw::Message *message, QString *error);
    void saveFrame(qint64 requestId, const QString &operation, const QString &identity,
                   const QString &profile, const QString &expectedControl, const QByteArray &frame);
    void release();
    QSqlDatabase database_;
    QString connection_, directory_, storeId_;
    AccountSession session_;
    bool ready_ = false;
};
#endif
