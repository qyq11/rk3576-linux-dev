#ifndef SN100_ORIGINAL_LIS_DOWNLOAD_ACK_H
#define SN100_ORIGINAL_LIS_DOWNLOAD_ACK_H

#include "original_lis_download_types.h"

namespace Sn100Lis
{
constexpr qint64 OriginalLisMaximumAckTextBytes = 4 * 1048576;
enum class OriginalLisAckState { Rejected, Prepared };

struct OriginalLisDownloadAckTimes
{
    // 未来真实内存应用完成、MAX之后的MSH构造时刻：yyyyMMddHHmmss.zzz。
    // 由当次调用点显式提供；这里不取当前时间，也不证明该阶段真实发生。
    QString messageTimestampAfterApplication;
};

struct OriginalLisDownloadAck
{
    OriginalLisAckState state = OriginalLisAckState::Rejected;
    QString body; // 仅原MSH/MSA正文及段CR，无MLLP、无编码或发送。
    OriginalLisFieldError error;
};

// 与ApplicationPlan独立；不能从“计划已准备”推导调用时机或发送许可。
OriginalLisDownloadAck buildOriginalLisDownloadAck(const ConfigurationIdentity &configuration,
    const ReceiveProfilePtr &profile, const QString &receivedControlId,
    const OriginalLisDownloadAckTimes &times);
}

#endif
