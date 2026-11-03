#ifndef SN100_LIS_CONNECTION_VALUES_H
#define SN100_LIS_CONNECTION_VALUES_H

#include <QByteArray>
#include <QMetaType>
#include <QString>

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

struct ConnectionState
{
    ConfigurationIdentity configuration;
    quint64 generation = 0;
    int connectedPeers = 0;
    QString state;
    QString detail;
    bool closing = false;
};

struct ReceivedMessage
{
    ConfigurationIdentity configuration;
    quint64 generation = 0;
    quint64 peer = 0;
    QByteArray body;
    QString text;
};

struct SendOutcome
{
    ConfigurationIdentity configuration;
    quint64 request = 0;
    quint64 generation = 0;
    quint64 peer = 0;
    QString outcome;
    QString detail;
};
}

Q_DECLARE_METATYPE(Sn100Lis::ConfigurationIdentity)
Q_DECLARE_METATYPE(Sn100Lis::ConnectionState)
Q_DECLARE_METATYPE(Sn100Lis::ReceivedMessage)
Q_DECLARE_METATYPE(Sn100Lis::SendOutcome)
#endif
