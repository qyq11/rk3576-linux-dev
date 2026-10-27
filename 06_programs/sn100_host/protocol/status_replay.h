#ifndef SN100_STATUS_REPLAY_H
#define SN100_STATUS_REPLAY_H

#include "status_session.h"

#include <QVector>

namespace Sn100Protocol
{

struct ReplayState
{
    bool loaded = false;
    bool finished = false;
    bool stopped = false;
    bool fresh = false;
    bool hasSnapshot = false;
    int eventIndex = 0; // 已处理数量；0表示尚未处理第一步。
    int eventCount = 0;
    qint64 sourceTimeMs = 0; // 文件记录时间，不是本机经过时间或仪器实时性证据。
    qint64 statusTimeMs = -1;
    quint64 epoch = 0;
    QString action;
    QString linkState = QStringLiteral("disconnected");
    QString error; // 致命文件/合同错误；坏包等恢复型通知留在notices。
    StatusSnapshot snapshot;
    QList<QByteArray> transmitted; // 计划TX，只供回放取证，不发送到任何端点。
    QByteArray received;
    QStringList notices;
};

// 纯离线逐步回放，无QObject、timer、socket或线程。外部驱动每次只调用step一次。
// 沿用StatusSession的观测语义；新观测不是请求ACK，也不改变真实业务权限。
class StatusReplay
{
public:
    explicit StatusReplay(int requestTimeoutMs = 120, int maximumRetries = 1);
    bool loadFile(const QString &path, QString *error = nullptr);
    bool step();
    void stop();
    const ReplayState &current() const;

private:
    struct Event
    {
        qint64 atMs = 0;
        QString action;
        QByteArray bytes;
    };
    void clearSnapshot();
    void finish(bool stopped);
    int m_requestTimeoutMs;
    int m_maximumRetries;
    StatusSession m_session;
    QVector<Event> m_events;
    ReplayState m_state;
};

} // namespace Sn100Protocol

#endif
