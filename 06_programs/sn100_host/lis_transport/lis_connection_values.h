#ifndef SN100_LIS_CONNECTION_VALUES_H
#define SN100_LIS_CONNECTION_VALUES_H

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QSet>
#include <memory>

namespace Sn100Lis
{
// 本地身份不写入LIS报文；配置读取指纹与真正的socket代次分别保留。
struct ConfigurationIdentity
{
    quint64 revision = 0;
    QString effectiveKey;
    QString transportToken;
    QString sourceFingerprint;
};

// 仅控制器能从采用成功的配置建立本值；读者只持const共享所有权。
// 不保存全INI、originalValues或认证正文，也不依赖控制器线程/对象寿命。
class ReceiveProfile final
{
public:
    ~ReceiveProfile() = default;
    const ConfigurationIdentity &configuration() const { return configuration_; }
    const QString &sequenceNumber() const { return sequenceNumber_; }
    // 自动查询设置只供业务选择路线，不是发送、接收或保存许可。
    bool autoGet() const { return autoGet_; }
    // 直接保留设置层已确认的原资料目录；这里不重新查路径或读取文件。
    const QString &originalRoot() const { return originalRoot_; }
    bool matches(const ConfigurationIdentity &identity) const
    {
        return configuration_.revision == identity.revision
            && configuration_.effectiveKey == identity.effectiveKey
            && configuration_.transportToken == identity.transportToken
            && configuration_.sourceFingerprint == identity.sourceFingerprint;
    }
private:
    friend class LisConnectionController;
    ReceiveProfile(const ConfigurationIdentity &identity, const QString &sequenceNumber,
                   bool autoGet, const QString &originalRoot) :
        configuration_(identity), sequenceNumber_(sequenceNumber), autoGet_(autoGet),
        originalRoot_(originalRoot) {}
    ReceiveProfile(const ReceiveProfile &) = delete;
    ReceiveProfile &operator=(const ReceiveProfile &) = delete;
    const ConfigurationIdentity configuration_;
    const QString sequenceNumber_;
    const bool autoGet_;
    const QString originalRoot_;
};
using ReceiveProfilePtr = std::shared_ptr<const ReceiveProfile>;

// 只证明本地传输入队及当次固定目标，不表示写完、对端确认或患者任务保存。
struct QueuedSend
{
    ConfigurationIdentity configuration;
    quint64 request = 0;
    quint64 generation = 0;
    QSet<quint64> peers;
    ReceiveProfilePtr profile;
};

struct ConnectionState
{
    ConfigurationIdentity configuration;
    quint64 generation = 0;
    int connectedPeers = 0;
    QString state;
    QString detail;
    bool closing = false;
    // 仅说明最近发布状态的配置来源；不证明已连接，也不授权查询、保存或ACK。
    ReceiveProfilePtr profile;
};

struct ReceivedMessage
{
    ConfigurationIdentity configuration;
    quint64 generation = 0;
    quint64 peer = 0;
    QByteArray body;
    QString text;
    ReceiveProfilePtr profile; // 同身份的序列号、自动查询开关和原目录；旧值不读新配置。
};

struct SendOutcome
{
    ConfigurationIdentity configuration;
    quint64 request = 0;
    quint64 generation = 0;
    quint64 peer = 0;
    QString outcome;
    QString detail;
    ReceiveProfilePtr profile; // 按pending请求保留，换配置后不贴新来源。
};
}

Q_DECLARE_METATYPE(Sn100Lis::ConfigurationIdentity)
Q_DECLARE_METATYPE(Sn100Lis::ReceiveProfilePtr)
Q_DECLARE_METATYPE(Sn100Lis::QueuedSend)
Q_DECLARE_METATYPE(Sn100Lis::ConnectionState)
Q_DECLARE_METATYPE(Sn100Lis::ReceivedMessage)
Q_DECLARE_METATYPE(Sn100Lis::SendOutcome)
#endif
