#ifndef SN100_ORIGINAL_LIS_DOWNLOAD_TYPES_H
#define SN100_ORIGINAL_LIS_DOWNLOAD_TYPES_H

#include "lis_transport/lis_connection_values.h"
#include <QList>
#include <QStringList>

namespace Sn100Lis
{
// 这里只保存原消息的值，不是患者记录、孔位、任务或成功保存回执。
enum class OriginalLisReplyKind { Other, Ack, Dsr };
enum class OriginalLisHeaderState { Rejected, Ready };
enum class OriginalLisDsrState { HeaderNotReady, NotDsr, NegativeResponse, Rejected, FieldsReady };

struct OriginalLisFieldError
{
    QString field;
    QString detail;
};

struct OriginalLisReplyHeader
{
    OriginalLisHeaderState state = OriginalLisHeaderState::Rejected;
    OriginalLisReplyKind kind = OriginalLisReplyKind::Other;
    ReceivedMessage source; // 原配置版本/令牌/指纹、generation、peer、body和text，不重贴新身份。
    QString sequenceNumberSnapshot; // 调用方须提供与source.configuration对应的原配置值。
    QStringList segments; // 原Message按CR/LF拆分并跳过空段后的文字。
    QString sendingApplication; // MSH-3。
    QString messageType; // MSH-9；原规则先contains(ACK)，再contains(DSR)。
    QString controlId; // MSH-10；原定位依据，不是全局唯一ID。
    QString version; // MSH-12。
    QString applicationAcknowledgmentType; // MSH-16；保留原自定义样本标记，不改成标准推断。
    QString responseCode; // MSA-1；DSR的OK/NF与上传AA不是同一阶段。
    QString responseControlId; // MSA-2；原接收器读了它，但定位不用它。
    OriginalLisFieldError error;
};

struct OriginalLisPatientFields
{
    QString patientIdentifier; // PID-3，仅原解析，不在DSR接收器保存。
    QString bedNumber; // PID-4。
    QString name; // PID-5。
    QString wardAndRoom; // PID-6原串。
    QString ward, room; // 恰好两个^分项才赋值；原接收器未保存。
    QString sex; // PID-8，M/F/其他到枚举的转换留给后续原业务。
    QString ageAndUnit; // 实际PID-30；原getPatientAge(31)内部减1。
    QString age, ageUnit; // 恰好两个^分项才赋值；本片不做atoi或猜年龄单位。
};

struct OriginalLisOrderFields
{
    QString barcode; // OBR-2，不能与内部样本序号合并。
    QString sampleNumber; // OBR-3；原Win32 toULong→int32转换尚不在本片执行。
    QString urgent; // OBR-5，只解析，原接收器未采用。
    QString registrationTime; // OBR-7；原无格式fromString，不在这里替换或补当前时间。
    QString repeatTimes; // OBR-9原串；后续原业务toInt后小于1取1，没有99上限。
    QString rackAndHole; // OBR-10原串，仅用于原日志，不能改成定位依据。
    QString rack, hole; // 恰好两个^分项才赋值。
    QString dilution; // OBR-12，只解析，原接收器未采用。
    QString remark; // OBR-13。
    QString specimenReceivedTime; // OBR-14，只解析，原接收器未采用。
    QString specimenType; // OBR-15，只解析，原接收器未采用。
    QString checker; // OBR-16。
    QString department; // OBR-17。
};

struct OriginalLisDspProject
{
    QString code; // 未映射的原代码；不能直接当projectId或基础project_lis_code。
    QString countText; // 最后一次成功stoi的原串，诊断保留，不改写为整数文字。
    int count = 0; // 原DSP解析值；接收器实际建任务时不用它，而用OBR-9。
};

struct OriginalLisDsrFields
{
    OriginalLisDsrState state = OriginalLisDsrState::HeaderNotReady;
    OriginalLisReplyHeader header;
    OriginalLisPatientFields patient;
    OriginalLisOrderFields order;
    QString declaredProjectCount; // DSP-2原串，原读到但不核项目数量。
    QString projectText; // DSP-3原串。
    QList<OriginalLisDspProject> projects; // 原std::map<string>字节顺序，重复码取最后成功值。
    int ignoredMalformedItems = 0;
    int ignoredInvalidCounts = 0;
    OriginalLisFieldError error;
};
}

Q_DECLARE_METATYPE(Sn100Lis::OriginalLisReplyHeader)
Q_DECLARE_METATYPE(Sn100Lis::OriginalLisDsrFields)
#endif
