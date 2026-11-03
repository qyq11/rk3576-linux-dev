#include "original_reagent_level.h"
#include "protocol/frame_codec.h"
#include <QTextCodec>
#include <cmath>

namespace OriginalLevels
{
namespace
{
bool fail(QString *message, const QString &reason)
{
    if (message) *message=reason;
    return false;
}
}

bool decodePacket(const QByteArray &payload, bool linuxNew, ReceivedPacket *result, QString *message)
{
    if (message) message->clear();
    if (!result) return fail(message,QStringLiteral("N液位返回的输出为空。"));
    *result=ReceivedPacket();
    if (payload.size()<2 || payload.size()>Sn100Protocol::kMaxMessageBytes || payload[0]!='N')
        return fail(message,QStringLiteral("不是完整的顶层N液位返回，或超过原通信本地读取上限。"));
    QTextCodec *utf8=QTextCodec::codecForName("UTF-8");
    if (!utf8) return fail(message,QStringLiteral("UTF-8解码器不可用，未猜测N字段编码。"));
    ReceivedPacket read;
    read.linuxNew=linuxNew;
    read.payload=payload;
    for (int cursor=1;cursor<payload.size();)
    {
        if (payload[cursor++]!='[')
            return fail(message,QStringLiteral("N液位返回字段缺少左括号或含尾随内容。"));
        const int end=payload.indexOf(']',cursor);
        if (end<cursor || end-cursor>4096 || read.rawFields.size()>=4096)
            return fail(message,QStringLiteral("N液位返回字段不完整或超过本地读取上限。"));
        const QByteArray raw=payload.mid(cursor,end-cursor);
        for (unsigned char character : raw)
            if (character<32 || character=='[')
                return fail(message,QStringLiteral("N液位返回含控制字符或未定义的嵌套括号。"));
        QTextCodec::ConverterState conversion;
        const QString text=utf8->toUnicode(raw.constData(),raw.size(),&conversion);
        if (conversion.invalidChars || conversion.remainingChars)
            return fail(message,QStringLiteral("N液位返回字段不是完整有效的原UTF-8文字。"));
        read.rawFields.append(raw);
        read.fields.append(text);
        cursor=end+1;
    }
    if (read.fields.isEmpty()) return fail(message,QStringLiteral("N液位返回没有孔位字段。"));
    read.hole=read.fields[0];
    // 原Sample按首项contains判断，不能把其架号/孔号解释成试剂的9或10字段。
    if (read.hole.contains(QStringLiteral("Sample")))
    {
        if (read.fields.size()<4)
            return fail(message,QStringLiteral("N样本液位返回少于原分支所需的4项，未进入试剂处理。"));
        read.kind=PacketKind::SampleIgnored;
        *result=read;
        return true;
    }
    const int expected=linuxNew ? 10 : 9;
    if (read.fields.size()!=expected)
        return fail(message,QStringLiteral("N试剂液位字段数与已确认的新旧协议不符，未按长度猜测协议。"));
    read.reagent=read.fields[1];
    read.area=read.fields[2];
    read.sendIndex=read.fields[4]; // 原含义未核实，保留原文，不作为设备请求号。
    read.bottle=read.fields[5];
    read.reportedDiameter=read.fields[6];
    read.reportedAngle=read.fields[7];
    if (linuxNew) read.reportedVolume=read.fields[8];
    if (read.hole.isEmpty() || read.reagent.isEmpty() || read.area.isEmpty() || read.bottle.isEmpty())
        return fail(message,QStringLiteral("N试剂液位缺少孔位、试剂、区域或瓶型身份。"));
    bool needleOk=false, heightOk=false;
    read.needle=read.fields[3].toInt(&needleOk);
    read.rawHeightMm=read.fields[linuxNew ? 9 : 8].toDouble(&heightOk);
    // 本接收切片仅开放实际确认的三根针；不改变纯容量函数的1/2/其余映射。
    if (!needleOk || read.needle<1 || read.needle>3)
        return fail(message,QStringLiteral("N针号不是已确认的1至3整数；本接收入口保护性拒绝，未改成默认样本针。"));
    if (!heightOk || !std::isfinite(read.rawHeightMm) || read.rawHeightMm<0 || read.rawHeightMm>=0xff00)
        return fail(message,QStringLiteral("N毫米高度无效或达到原错误高度标记，未改写为0后继续保存。"));
    *result=read;
    return true;
}
}
