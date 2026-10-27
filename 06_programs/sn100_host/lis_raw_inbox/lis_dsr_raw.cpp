#include "lis_dsr_raw.h"
#include <QRegularExpression>

namespace LisDsrRaw
{
QString profileName() { return QStringLiteral("source-derived-utf8-dsr-raw-v1"); }
QString parseStatus() { return QStringLiteral("raw-unmapped-not-applied"); }

QMap<QString, QString> sourceFields(const QList<Segment> &segments)
{
    QMap<QString, QString> values;
    // Original getRecString accessors; compound values remain whole strings.
    const QList<QList<int>> positions{{3, 5, 9, 10, 12, 16, 18}, {1, 2},
        {3, 4, 5, 6, 8, 31}, {2, 3, 7, 9, 10, 12, 13, 14, 15, 16, 17}, {2, 3}};
    for (int segment = 0; segment < positions.size() && segment < segments.size(); ++segment)
        for (int field : positions.at(segment))
            if (field < segments.at(segment).fields.size())
                values.insert(segments.at(segment).name + QChar('.') + QString::number(field),
                              segments.at(segment).fields.at(field));
    return values;
}

bool decode(const QString &profile, const QString &expectedControl,
            const QByteArray &frame, Message *message, QString *error)
{
    auto reject = [&](const QString &reason) { *error = reason; return false; };
    if (profile != profileName()) return reject(QStringLiteral("只支持明确命名的source-derived UTF8原始DSR profile。"));
    if (expectedControl.isEmpty() || expectedControl.toUtf8().size() > MaximumControlBytes
            || expectedControl.contains(QChar('|')))
        return reject(QStringLiteral("调用方必须给非空、有界的预期MSH-10；不会生成或替换控制号。"));
    for (QChar character : expectedControl)
        if (character.unicode() < 32 || character.unicode() == 127)
            return reject(QStringLiteral("预期控制号含控制字符。"));
    if (frame.size() > MaximumFrameBytes || frame.size() < 4)
        return reject(QStringLiteral("本地整帧上限为1MiB，且必须有完整MLLP边界。"));
    if (frame.startsWith(QByteArray("\x0b\x00", 2)) || frame.startsWith(QByteArray("\x00\x0b", 2))
            || frame.startsWith(QByteArray("\xff\xfe", 2))
            || frame.startsWith(QByteArray("\xfe\xff", 2)))
        return reject(QStringLiteral("此profile不支持UTF16；不猜测编码或进行转码。"));
    if (frame.at(0) != '\x0b' || !frame.endsWith(QByteArray("\x1c\x0d", 2)))
        return reject(QStringLiteral("需要UTF8 MLLP的0B开头和1C0D结尾。"));
    const QByteArray body = frame.mid(1, frame.size() - 3);
    if (body.contains('\x0b') || body.contains('\x1c'))
        return reject(QStringLiteral("单次只接收恰好一帧，不拼接多消息或丢弃尾部。"));
    for (char value : body)
    {
        const unsigned char byte = static_cast<unsigned char>(value);
        if ((byte < 32 && byte != 13 && byte != 10) || byte == 127)
            return reject(QStringLiteral("消息含不支持的控制字节，UTF16/NUL输入不会当UTF8接收。"));
    }
    const QString text = QString::fromUtf8(body.constData(), body.size());
    if (text.toUtf8() != body || text.startsWith(QChar(0xfeff)))
        return reject(QStringLiteral("消息不是严格UTF8，或使用了本profile不支持的BOM。"));
    QStringList lines;
    int start = 0;
    for (int index = 0; index < text.size(); ++index)
    {
        if (text.at(index) != QChar('\r') && text.at(index) != QChar('\n')) continue;
        if (index == start) return reject(QStringLiteral("不接收空段；原帧会整体拒绝，不静默忽略。"));
        lines.append(text.mid(start, index - start));
        if (text.at(index) == QChar('\r') && index + 1 < text.size() && text.at(index + 1) == QChar('\n')) ++index;
        start = index + 1;
        if (lines.size() > MaximumSegments) return reject(QStringLiteral("段数超过本地64段上限。"));
    }
    if (start < text.size()) lines.append(text.mid(start));
    if (lines.size() < 5 || lines.size() > MaximumSegments)
        return reject(QStringLiteral("当前原始订单profile需要MSH/MSA/PID/OBR/DSP五个核心段，最多64段。"));
    Message parsed;
    parsed.profile = profile;
    parsed.frame = frame;
    parsed.status = parseStatus();
    const QStringList required{QStringLiteral("MSH"), QStringLiteral("MSA"), QStringLiteral("PID"),
                               QStringLiteral("OBR"), QStringLiteral("DSP")};
    static const QRegularExpression tag(QStringLiteral("^[A-Z0-9]{3}$"));
    for (int index = 0; index < lines.size(); ++index)
    {
        Segment segment;
        segment.rawText = lines.at(index);
        if (segment.rawText.count(QChar('|')) + 1 + (index == 0 ? 1 : 0) > MaximumFields)
            return reject(QStringLiteral("单段字段数超过本地128字段上限。"));
        segment.fields = segment.rawText.split(QChar('|'), Qt::KeepEmptyParts);
        segment.name = segment.fields.first();
        if (!tag.match(segment.name).hasMatch() || (index < required.size() && segment.name != required.at(index))
                || (index != 0 && segment.name == QStringLiteral("MSH")))
            return reject(QStringLiteral("段名称/顺序不符合本地profile，或一帧中包含第二条消息。"));
        if (index == 0) segment.fields.insert(1, QStringLiteral("|"));
        if (segment.fields.size() > MaximumFields)
            return reject(QStringLiteral("单段字段数超过本地128字段上限。"));
        for (const QString &field : segment.fields)
            if (field.toUtf8().size() > MaximumFieldBytes)
                return reject(QStringLiteral("单字段超过本地64KiB字节上限，不截断。"));
        parsed.segments.append(segment);
    }
    const auto &msh = parsed.segments.first().fields;
    if (msh.size() <= 12 || msh.at(2) != QStringLiteral("^~\\&"))
        return reject(QStringLiteral("MSH字段缺失或编码分隔符不符合源码默认profile。"));
    parsed.messageType = msh.at(9);
    parsed.controlId = msh.at(10);
    parsed.version = msh.at(12);
    if (parsed.messageType.section(QChar('^'), 0, 0) != QStringLiteral("DSR")
            || parsed.version != QStringLiteral("2.3.1") || parsed.controlId != expectedControl)
        return reject(QStringLiteral("必须是DSR、版本2.3.1且MSH-10与调用方预期逐字相同。"));
    if (parsed.segments.at(1).fields.size() <= 1 || parsed.segments.at(1).fields.at(1).isEmpty())
        return reject(QStringLiteral("MSA状态字段缺失；不推断为OK或AA。"));
    parsed.sourceFields = sourceFields(parsed.segments);
    *message = parsed;
    return true;
}

bool samePayload(const Message &left, const Message &right)
{
    if (left.profile != right.profile || left.controlId != right.controlId || left.frame != right.frame
            || left.messageType != right.messageType || left.version != right.version || left.status != right.status
            || left.segments.size() != right.segments.size() || left.sourceFields != right.sourceFields) return false;
    for (int index = 0; index < left.segments.size(); ++index)
    {
        const auto &a = left.segments.at(index); const auto &b = right.segments.at(index);
        if (a.name != b.name || a.rawText != b.rawText || a.fields != b.fields) return false;
    }
    return true;
}
}
