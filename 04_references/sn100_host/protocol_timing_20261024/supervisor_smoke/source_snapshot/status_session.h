#ifndef SN100_STATUS_SESSION_H
#define SN100_STATUS_SESSION_H

#include "frame_codec.h"
#include <QJsonObject>

namespace Sn100Protocol
{
struct SubsystemStatus
{
    int id = 0;
    QStringList fields; // 保留原始子系统字段，不把未核实的机型映射当作 SN100 事实。
};

struct StatusSnapshot
{
    QString model;
    int mainStatus = -1;
    int ledStatus = -1;
    int errorWorkType = -1;
    QStringList generalFields;
    QList<SubsystemStatus> subsystems;
    QJsonObject toJson() const;
};

// 依据参考工程 Qj 的 [主字段数][主字段...][子系统数][id][字段数][字段...]。
// 只解析值；原接收器中的自动校时、登录、配置/试剂请求均不在本对象中。
bool parseStatus(const QByteArray &message, StatusSnapshot *status, QString *error);

struct SessionEvents
{
    QList<QByteArray> transmitted;
    QList<StatusSnapshot> statuses;
    QStringList notices;
};

// 单连接、一个只读 Qj 在途请求，无设备/网络依赖。
// 所有时间由同一单调时钟提供；模拟重试策略不构成真实仪器重发授权。
class StatusSession
{
public:
    explicit StatusSession(int requestTimeoutMs = 1000, int maximumRetries = 1);
    SessionEvents open(qint64 nowMs);
    SessionEvents queryStatus(qint64 nowMs);
    SessionEvents receive(const QByteArray &bytes, qint64 nowMs);
    SessionEvents advance(qint64 nowMs);
    SessionEvents disconnect(qint64 nowMs);
    QString linkState() const;
    bool hasFreshStatus() const;
    int bufferedBytes() const;
    int assemblyBytes() const;

private:
    bool acceptTime(qint64 nowMs, SessionEvents *events);
    void sendQuery(qint64 nowMs, SessionEvents *events);
    void clearConnection();

    FrameDecoder m_decoder;
    int m_requestTimeoutMs;
    int m_maximumRetries;
    int m_retries = 0;
    qint64 m_now = -1;
    qint64 m_connectedAt = -1;
    qint64 m_lastValidStatusAt = -1;
    qint64 m_deadline = -1;
    bool m_connected = false;
    bool m_pending = false;
    bool m_fresh = false;
    bool m_timedOut = false;
    QByteArray m_previousStatus;
};
}

#endif
