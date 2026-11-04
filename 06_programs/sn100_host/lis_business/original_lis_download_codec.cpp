#include "original_lis_download_codec.h"

#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace Sn100Lis
{
namespace
{
void fail(OriginalLisFieldError *error, const QString &field, const QString &detail)
{
    error->field = field;
    error->detail = detail;
}

bool splitSegments(const QString &text, QStringList *segments, OriginalLisFieldError *error)
{
    int begin = 0;
    for (int index = 0; index <= text.size(); ++index)
    {
        if (index < text.size() && text.at(index) != QChar('\r') && text.at(index) != QChar('\n')) continue;
        if (index > begin)
        {
            if (segments->size() >= OriginalLisMaximumSegments)
            {
                fail(error, QStringLiteral("message"), QStringLiteral("消息段数超过本地64段预算，未截断处理。"));
                return false;
            }
            segments->append(text.mid(begin, index - begin));
        }
        begin = index + 1;
    }
    return true;
}

bool fieldsAt(const OriginalLisReplyHeader &header, int segment, int lastRequiredField,
              QStringList *fields, OriginalLisFieldError *error)
{
    if (segment >= header.segments.size())
    {
        fail(error, QStringLiteral("segment[%1]").arg(segment), QStringLiteral("原读取位置的消息段缺失。"));
        return false;
    }
    const QString &text = header.segments.at(segment);
    QString field;
    for (QChar character : text)
    {
        if (character == QChar('|'))
        {
            fields->append(field);
            field.clear();
            if (fields->size() > OriginalLisMaximumFieldsPerSegment) break;
        }
        else field.append(character);
    }
    // 原Segment只在尾项非空时再入列，不能用KeepEmptyParts多保留一个尾空项。
    if (!field.isEmpty()) fields->append(field);
    if (!fields->isEmpty() && fields->first() == QStringLiteral("MSH"))
        fields->insert(1, QStringLiteral("|"));
    if (fields->size() > OriginalLisMaximumFieldsPerSegment)
    {
        fail(error, QStringLiteral("segment[%1]").arg(segment), QStringLiteral("消息字段超过本地128项预算，未截断处理。"));
        return false;
    }
    if (fields->size() <= lastRequiredField)
    {
        fail(error, QStringLiteral("segment[%1].field[%2]").arg(segment).arg(lastRequiredField),
             QStringLiteral("原版将越界读取的字段缺失，未补空后当成功。"));
        return false;
    }
    return true;
}

void splitPair(const QString &text, QString *first, QString *second)
{
    // 原splitStringLis保留^尾空项；空串也是一个空项，恰好两项时才采用。
    const int separator = text.indexOf(QChar('^'));
    if (separator >= 0 && text.indexOf(QChar('^'), separator + 1) < 0)
    {
        *first = text.left(separator);
        *second = text.mid(separator + 1);
    }
}
}

OriginalLisReplyHeader decodeOriginalLisReplyHeader(const ReceivedMessage &message,
                                                   const QString &sequenceNumberSnapshot)
{
    OriginalLisReplyHeader result;
    result.source = message;
    result.sequenceNumberSnapshot = sequenceNumberSnapshot;
    if (message.body.size() > OriginalLisMaximumBodyBytes
            || message.text.size() > OriginalLisMaximumTextCodeUnits)
    {
        fail(&result.error, QStringLiteral("message"), QStringLiteral("正文超过本地1MiB字节或1048576个UTF16字值预算，未拆成部分成功。"));
        return result;
    }
    // 原receiver把QString转toStdString后交Message；此处保持同一UTF8往返。
    // 不重新从body选编码，也不用QByteArray重载在本阶段另做NUL截断。
    const QString normalized = QString::fromStdString(message.text.toStdString());
    if (!splitSegments(normalized, &result.segments, &result.error)) return result;
    QStringList msh;
    if (!fieldsAt(result, 0, 16, &msh, &result.error)) return result;
    result.sendingApplication = msh.at(3);
    result.messageType = msh.at(9);
    result.controlId = msh.at(10);
    result.version = msh.at(12);
    result.applicationAcknowledgmentType = msh.at(16);
    if (result.version != QStringLiteral("2.3.1"))
    {
        fail(&result.error, QStringLiteral("MSH.12"), QStringLiteral("原接收规则要求版本逐字为2.3.1。"));
        return result;
    }
    if (!sequenceNumberSnapshot.contains(result.sendingApplication))
    {
        fail(&result.error, QStringLiteral("MSH.3"), QStringLiteral("原配置序列号不包含收到的发送方，未反转或改成相等判断。"));
        return result;
    }
    QStringList msa;
    if (!fieldsAt(result, 1, 2, &msa, &result.error)) return result;
    result.responseCode = msa.at(1);
    result.responseControlId = msa.at(2);
    if (result.messageType.contains(QStringLiteral("ACK"))) result.kind = OriginalLisReplyKind::Ack;
    else if (result.messageType.contains(QStringLiteral("DSR"))) result.kind = OriginalLisReplyKind::Dsr;
    result.state = OriginalLisHeaderState::Ready;
    return result;
}

OriginalLisDsrFields decodeOriginalLisDsrFields(const OriginalLisReplyHeader &header)
{
    OriginalLisDsrFields result;
    result.header = header;
    if (header.state != OriginalLisHeaderState::Ready) return result;
    if (header.kind != OriginalLisReplyKind::Dsr)
    {
        result.state = OriginalLisDsrState::NotDsr;
        return result;
    }
    if (header.responseCode != QStringLiteral("OK"))
    {
        // 原非OK分支不读取患者/项目，也不要求五段；NF是否告警由后续业务处理。
        result.state = OriginalLisDsrState::NegativeResponse;
        return result;
    }
    result.state = OriginalLisDsrState::Rejected;
    QStringList pid, obr, dsp;
    // 按原段位置读，不擅自增加段名校验；OBR原resetIndex(0)，所以用直接字段号。
    if (!fieldsAt(header, 2, 30, &pid, &result.error)
            || !fieldsAt(header, 3, 17, &obr, &result.error)
            || !fieldsAt(header, 4, 3, &dsp, &result.error)) return result;
    result.patient.patientIdentifier = pid.at(3);
    result.patient.bedNumber = pid.at(4);
    result.patient.name = pid.at(5);
    result.patient.wardAndRoom = pid.at(6);
    splitPair(result.patient.wardAndRoom, &result.patient.ward, &result.patient.room);
    result.patient.sex = pid.at(8);
    result.patient.ageAndUnit = pid.at(30);
    splitPair(result.patient.ageAndUnit, &result.patient.age, &result.patient.ageUnit);
    result.order.barcode = obr.at(2);
    result.order.sampleNumber = obr.at(3);
    result.order.urgent = obr.at(5);
    result.order.registrationTime = obr.at(7);
    result.order.repeatTimes = obr.at(9);
    result.order.rackAndHole = obr.at(10);
    splitPair(result.order.rackAndHole, &result.order.rack, &result.order.hole);
    result.order.dilution = obr.at(12);
    result.order.remark = obr.at(13);
    result.order.specimenReceivedTime = obr.at(14);
    result.order.specimenType = obr.at(15);
    result.order.checker = obr.at(16);
    result.order.department = obr.at(17);
    result.declaredProjectCount = dsp.at(2);
    result.projectText = dsp.at(3);
    if (result.projectText.count(QChar('^')) >= OriginalLisMaximumProjectItems)
    {
        fail(&result.error, QStringLiteral("DSP.3"), QStringLiteral("项目分项超过本地4096项预算，未截断或继续建任务。"));
        return result;
    }
    const QStringList items = result.projectText.split(QChar('^'), Qt::KeepEmptyParts);
    std::map<std::string, OriginalLisDspProject> projects;
    static_assert(std::numeric_limits<int>::is_signed && std::numeric_limits<int>::digits == 31,
                  "Original LIS DSP stoi requires a signed 32-bit int");
    for (const QString &item : items)
    {
        const int separator = item.indexOf(QChar('&'));
        if (separator < 0 || item.indexOf(QChar('&'), separator + 1) >= 0)
        {
            ++result.ignoredMalformedItems;
            continue;
        }
        OriginalLisDspProject project;
        // 与原&恰好拆两项相同，先数分隔符，避免异常长串生成大量临时字段。
        project.code = item.left(separator); project.countText = item.mid(separator + 1);
        try
        {
            // 原stoi允许前缀数字；重复码只有后一次成功转换才覆盖。
            project.count = std::stoi(project.countText.toStdString());
            projects[project.code.toStdString()] = project;
        }
        catch (const std::invalid_argument &)
        {
            ++result.ignoredInvalidCounts;
        }
        catch (const std::out_of_range &)
        {
            // 原版未捕获此异常，没有可沿用的成功规则；明确拒绝，不补0或继续建任务。
            fail(&result.error, QStringLiteral("DSP.3"), QStringLiteral("原DSP次数超出32位stoi范围，未把原未处理异常改作正常次数。"));
            return result;
        }
    }
    for (const auto &project : projects) result.projects.append(project.second);
    result.state = OriginalLisDsrState::FieldsReady;
    return result;
}
}
