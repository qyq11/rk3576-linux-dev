#ifndef SN100_ORIGINAL_REAGENT_PROBE_H
#define SN100_ORIGINAL_REAGENT_PROBE_H
#include "data/account_store.h"
#include "original_files/original_ini.h"
#include "original_reagent_page_metadata.h"
#include <QMetaType>
#include <chrono>

// 单孔纯Mg的本地来源，不是设备回显、任务到包号、CAL批号许可或SQL提交凭据。
namespace OriginalReagentProbe
{
using Clock = std::chrono::steady_clock;
struct Request
{
    QString token, root, hole;
    quint64 request = 0, connection = 0;
    AccountSession actor;
    Clock::time_point prepareUntil;
};
struct Prepared
{
    Request source;
    bool success = false;
    QString message, table;
    QString sourceLease; // 仅本进程准备来源的随机凭据；不在线上发送，也不持锁或SQL对象。
    QByteArray payload;
    QVariantMap originalHole;
    QMap<QString,QString> versions;
    OriginalIni::InitializationSourcePtr initialization;
    OriginalReagentPageMetadata::View batches;
};
struct NGrant
{
    QString token, root, hole;
    quint64 request = 0, connection = 0, packet = 0;
};
inline bool sameActor(const AccountSession &a, const AccountSession &b)
{
    return a.name==b.name && a.role==b.role && a.authenticated==b.authenticated && a.source==b.source &&
        a.originalPolicySource==b.originalPolicySource && a.grantedPowerIds==b.grantedPowerIds;
}
inline bool sameRequest(const Request &a, const Request &b)
{
    return !a.token.isEmpty() && a.token==b.token && a.request==b.request && a.connection==b.connection &&
        a.root==b.root && a.hole==b.hole && a.prepareUntil==b.prepareUntil && sameActor(a.actor,b.actor);
}
inline bool sameHoleIdentity(const QVariantMap &a, const QVariantMap &b)
{
    for (const auto &key : {"id","hole_name","area","reagent_type","reagent_name","reagent_batch","bottle_type"})
    {
        const QString name=QString::fromLatin1(key);
        if (!a.contains(name) || !b.contains(name)) return false;
        const auto first=a.value(name), second=b.value(name);
        if (first.userType()!=second.userType() || first.isNull()!=second.isNull() || first!=second) return false;
    }
    return true;
}
}
Q_DECLARE_METATYPE(OriginalReagentProbe::Request)
Q_DECLARE_METATYPE(OriginalReagentProbe::Prepared)
#endif
