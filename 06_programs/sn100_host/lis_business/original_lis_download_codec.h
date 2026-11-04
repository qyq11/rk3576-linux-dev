#ifndef SN100_ORIGINAL_LIS_DOWNLOAD_CODEC_H
#define SN100_ORIGINAL_LIS_DOWNLOAD_CODEC_H

#include "original_lis_download_types.h"

namespace Sn100Lis
{
// 本地解析预算，不是原协议字段上限；超限明确拒绝，不截断后报解码成功。
constexpr int OriginalLisMaximumBodyBytes = 1048576;
constexpr int OriginalLisMaximumTextCodeUnits = 1048576;
constexpr int OriginalLisMaximumSegments = 64;
constexpr int OriginalLisMaximumFieldsPerSegment = 128;
constexpr int OriginalLisMaximumProjectItems = 4096;

// 输入必须来自现有传输的完整ReceivedMessage，text已经按原配置解码、去MLLP边界。
// 原业务先核消息头，再定位孔/判状态/置R1，最后才读DSR正文；两个阶段不能合成保存成功bool。
// 本函数不选择当前连接，sequenceNumberSnapshot必须由调用方按原配置身份提供。
OriginalLisReplyHeader decodeOriginalLisReplyHeader(const ReceivedMessage &message,
                                                   const QString &sequenceNumberSnapshot);
OriginalLisDsrFields decodeOriginalLisDsrFields(const OriginalLisReplyHeader &header);
}
#endif
