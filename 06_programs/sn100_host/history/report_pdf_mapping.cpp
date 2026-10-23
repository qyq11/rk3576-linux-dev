#include "report_pdf_mapping.h"

#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QGlyphRun>
#include <QMap>
#include <QRawFont>
#include <QRegularExpression>
#include <QSet>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace
{
const int maximumPdfBytes = 32 * 1024 * 1024;
const int maximumObjects = 20000;
const int maximumSpans = 4096;

struct Failure { QString message; };
void require(bool condition, const QString &message)
{
    if (!condition) throw Failure{message};
}
QString message(const char *detail)
{
    // Diagnostics deliberately exclude patient text and file contents.
    return QStringLiteral("PDF中文映射校验失败：") + QString::fromLatin1(detail);
}

bool white(char c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
bool delimiter(char c)
{
    return white(c) || QByteArrayLiteral("()<>[]{}/%").contains(c);
}

struct Value
{
    enum Kind { Number, Name, String, Hex, Array, Dictionary, Reference, Word } kind = Word;
    QByteArray raw;
    QList<Value> items;
    QMap<QByteArray, Value> entries;
    int reference = -1;
    int begin = 0;
    int end = 0;
    int integer() const
    {
        bool ok = false;
        const int result = raw.toInt(&ok);
        require(kind == Number && ok, message("expected bounded integer"));
        return result;
    }
    double number() const
    {
        bool ok = false;
        const double result = raw.toDouble(&ok);
        require(kind == Number && ok && std::isfinite(result), message("expected finite number"));
        return result;
    }
    bool named(const char *name) const { return kind == Name && raw == QByteArray(name); }
};

Value number(int value) { Value v; v.kind = Value::Number; v.raw = QByteArray::number(value); return v; }
Value reference(int id) { Value v; v.kind = Value::Reference; v.reference = id; return v; }

class Parser
{
public:
    explicit Parser(const QByteArray &bytes, int position = 0) : bytes(bytes), position(position) {}
    const QByteArray &bytes;
    int position;
    void spaces()
    {
        while (position < bytes.size())
        {
            if (white(bytes.at(position))) ++position;
            else if (bytes.at(position) == '%')
                while (position < bytes.size() && bytes.at(position) != '\r' && bytes.at(position) != '\n') ++position;
            else break;
        }
    }
    QByteArray word()
    {
        spaces();
        const int start = position;
        while (position < bytes.size() && !delimiter(bytes.at(position))) ++position;
        require(position > start, message("missing token"));
        return bytes.mid(start, position - start);
    }
    Value value(bool references = true, int depth = 0)
    {
        require(depth < 16, message("object nesting limit"));
        spaces();
        require(position < bytes.size(), message("unexpected end of object"));
        Value v;
        v.begin = position;
        const char c = bytes.at(position);
        if (c == '/' )
        {
            v.kind = Value::Name;
            ++position;
            const int start = position;
            while (position < bytes.size() && !delimiter(bytes.at(position))) ++position;
            v.raw = bytes.mid(start, position - start);
            require(!v.raw.contains('#'), message("escaped PDF name outside Qt dialect"));
        }
        else if (c == '(')
        {
            v.kind = Value::String;
            int nesting = 1;
            ++position;
            while (position < bytes.size() && nesting)
            {
                const char part = bytes.at(position++);
                if (part == '\\')
                {
                    require(position < bytes.size(), message("truncated literal escape"));
                    ++position;
                }
                else if (part == '(') ++nesting;
                else if (part == ')') --nesting;
            }
            require(!nesting, message("truncated literal string"));
            v.raw = bytes.mid(v.begin, position - v.begin);
        }
        else if (c == '<' && position + 1 < bytes.size() && bytes.at(position + 1) == '<')
        {
            v.kind = Value::Dictionary;
            position += 2;
            for (;;)
            {
                spaces();
                require(position + 1 < bytes.size(), message("truncated dictionary"));
                if (bytes.mid(position, 2) == ">>") { position += 2; break; }
                const Value key = value(false, depth + 1);
                require(key.kind == Value::Name && !v.entries.contains(key.raw), message("invalid dictionary key"));
                v.entries.insert(key.raw, value(references, depth + 1));
                require(v.entries.size() < 1024, message("dictionary capacity"));
            }
        }
        else if (c == '<')
        {
            v.kind = Value::Hex;
            const int start = ++position;
            while (position < bytes.size() && bytes.at(position) != '>') ++position;
            require(position < bytes.size(), message("truncated hex string"));
            QByteArray hex;
            for (int i = start; i < position; ++i)
            {
                const char h = bytes.at(i);
                if (white(h)) continue;
                require((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f') || (h >= 'A' && h <= 'F'),
                        message("invalid hexadecimal digit"));
                hex += h;
            }
            require(hex.size() % 2 == 0, message("odd hex string outside Qt dialect"));
            v.raw = QByteArray::fromHex(hex);
            ++position;
        }
        else if (c == '[')
        {
            v.kind = Value::Array;
            ++position;
            for (;;)
            {
                spaces();
                require(position < bytes.size(), message("truncated array"));
                if (bytes.at(position) == ']') { ++position; break; }
                v.items.append(value(references, depth + 1));
                require(v.items.size() <= 65536, message("array capacity"));
            }
        }
        else
        {
            v.raw = word();
            bool validNumber = false;
            v.raw.toDouble(&validNumber);
            v.kind = validNumber ? Value::Number : Value::Word;
            if (references && v.kind == Value::Number)
            {
                const int saved = position;
                spaces();
                if (position < bytes.size() && bytes.at(position) >= '0' && bytes.at(position) <= '9')
                {
                    const QByteArray generation = word();
                    spaces();
                    if (position < bytes.size() && bytes.at(position) == 'R'
                        && (position + 1 == bytes.size() || delimiter(bytes.at(position + 1))))
                    {
                        require(generation == "0", message("nonzero object generation"));
                        v.reference = v.integer();
                        v.kind = Value::Reference;
                        ++position;
                    }
                    else position = saved;
                }
                else position = saved;
            }
        }
        v.end = position;
        return v;
    }
};

QByteArray serialize(const Value &v)
{
    if (v.kind == Value::Dictionary)
    {
        QByteArray out("<<\n");
        for (auto it = v.entries.cbegin(); it != v.entries.cend(); ++it)
            out += '/' + it.key() + ' ' + serialize(it.value()) + '\n';
        return out + ">>";
    }
    if (v.kind == Value::Array)
    {
        QByteArray out("[");
        for (const Value &item : v.items) out += serialize(item) + ' ';
        return out + ']';
    }
    if (v.kind == Value::Reference) return QByteArray::number(v.reference) + " 0 R";
    if (v.kind == Value::Name) return '/' + v.raw;
    if (v.kind == Value::Hex) return '<' + v.raw.toHex().toUpper() + '>';
    return v.raw;
}

struct Object
{
    Value value;
    QByteArray stream;
    bool hasStream = false;
    bool loaded = false;
    bool loading = false;
};

class Pdf
{
public:
    QByteArray bytes;
    QMap<int, int> offsets;
    QMap<int, Object> objects;
    QMap<int, QByteArray> replacements;
    Value trailer;
    int nextObject = 0;

    void load(const QString &path)
    {
        QFile file(path);
        require(file.open(QIODevice::ReadOnly) && file.size() > 0 && file.size() <= maximumPdfBytes,
                message("PDF input size/read"));
        bytes = file.read(maximumPdfBytes + 1);
        require(bytes.size() <= maximumPdfBytes && bytes.startsWith("%PDF-1."), message("PDF header/size"));
        const int start = bytes.lastIndexOf("startxref");
        require(start > 0 && bytes.mid(start).contains("%%EOF"), message("missing final xref"));
        Parser ending(bytes, start + 9);
        const int offset = ending.value(false).integer();
        require(offset > 0 && offset < start, message("xref offset"));
        Parser xref(bytes, offset);
        require(xref.word() == "xref", message("only classic Qt xref accepted"));
        for (;;)
        {
            xref.spaces();
            if (bytes.mid(xref.position, 7) == "trailer") { xref.position += 7; break; }
            const int first = xref.value(false).integer();
            const int count = xref.value(false).integer();
            require(first >= 0 && first < maximumObjects && count > 0
                    && count <= maximumObjects - first, message("xref object capacity"));
            for (int i = 0; i < count; ++i)
            {
                const int position = xref.value(false).integer();
                const int generation = xref.value(false).integer();
                const QByteArray state = xref.word();
                require(state == "n" || state == "f", message("xref entry type"));
                if (state == "n")
                {
                    require(generation == 0 && first + i > 0 && position > 0 && position < offset
                            && !offsets.contains(first + i), message("xref entry bounds"));
                    offsets.insert(first + i, position);
                }
            }
        }
        trailer = xref.value();
        require(trailer.kind == Value::Dictionary && !trailer.entries.contains("Encrypt")
                && !trailer.entries.contains("Prev") && !trailer.entries.contains("XRefStm")
                && trailer.entries.value("Root").kind == Value::Reference, message("single unencrypted revision required"));
        nextObject = trailer.entries.value("Size").integer();
        require(!offsets.isEmpty() && nextObject > offsets.lastKey() && nextObject < maximumObjects,
                message("trailer object count"));
    }

    Object &object(int id)
    {
        require(offsets.contains(id), message("unresolved object reference"));
        Object &result = objects[id];
        if (result.loaded) return result;
        require(!result.loading, message("cyclic object/stream length reference"));
        result.loading = true;
        Parser parser(bytes, offsets.value(id));
        require(parser.value(false).integer() == id && parser.value(false).integer() == 0
                && parser.word() == "obj", message("object header mismatch"));
        result.value = parser.value();
        parser.spaces();
        if (bytes.mid(parser.position, 6) == "stream")
        {
            require(result.value.kind == Value::Dictionary, message("stream dictionary"));
            parser.position += 6;
            require(parser.position < bytes.size(), message("truncated stream delimiter"));
            if (bytes.mid(parser.position, 2) == "\r\n") parser.position += 2;
            else { require(bytes.at(parser.position) == '\n', message("stream newline")); ++parser.position; }
            Value length = result.value.entries.value("Length");
            if (length.kind == Value::Reference)
            {
                require(length.reference != id, message("recursive stream length"));
                length = object(length.reference).value;
            }
            const int size = length.integer();
            require(size >= 0 && size <= maximumPdfBytes && parser.position + size <= bytes.size(), message("stream length"));
            result.stream = bytes.mid(parser.position, size);
            result.hasStream = true;
            parser.position += size;
            require(parser.word() == "endstream", message("stream length boundary"));
        }
        require(parser.word() == "endobj", message("object end boundary"));
        result.loaded = true;
        result.loading = false;
        return result;
    }

    Value resolved(Value value)
    {
        if (value.kind == Value::Reference) value = object(value.reference).value;
        return value;
    }
    QByteArray decoded(int id)
    {
        const Object &obj = object(id);
        require(obj.hasStream, message("expected PDF stream"));
        const Value filter = obj.value.entries.value("Filter");
        if (filter.raw.isEmpty() && filter.kind == Value::Word) return obj.stream;
        require(filter.named("FlateDecode") && !obj.value.entries.contains("DecodeParms"), message("unexpected stream filter"));
        // Qt's qCompress format is a four-byte big-endian size followed by
        // the same zlib stream used by QtPDF. Inputs are exclusively our own
        // bounded renderer output; this is not an external PDF import API.
        QByteArray packed(4, '\0');
        // This prefix is an allocation hint, not a hard inflation limit.
        // Starting at the 32 MiB file limit needlessly commits that much for
        // every small page. Qt grows this buffer if an internal stream needs
        // more; the decoded-size check below remains mandatory.
        qToBigEndian<quint32>(64 * 1024, reinterpret_cast<uchar *>(packed.data()));
        packed += obj.stream;
        QByteArray result = qUncompress(packed);
        require(!result.isEmpty() && result.size() <= maximumPdfBytes, message("decoded stream capacity"));
        // qUncompress reserves the supplied upper bound. Do not retain that
        // reservation for every page while the independent child maps them.
        result.squeeze();
        return result;
    }
    void replaceStream(int id, Value dictionary, const QByteArray &data)
    {
        require(data.size() <= maximumPdfBytes, message("rewritten stream capacity"));
        dictionary.entries.remove("Filter");
        dictionary.entries.remove("DecodeParms");
        // Keep Qt's ordinary Flate compression after rewriting text metadata;
        // a multi-page report must not retain expanded content on disk.
        const QByteArray compressed = qCompress(data, 6).mid(4);
        const bool useCompressed = !compressed.isEmpty() && compressed.size() + 32 < data.size();
        const QByteArray &stream = useCompressed ? compressed : data;
        if (useCompressed)
        {
            Value filter;
            filter.kind = Value::Name;
            filter.raw = "FlateDecode";
            dictionary.entries.insert("Filter", filter);
        }
        dictionary.entries.insert("Length", number(stream.size()));
        replacements[id] = serialize(dictionary) + "\nstream\n" + stream + "\nendstream";
    }
    int addStream(const QByteArray &data)
    {
        require(nextObject < maximumObjects, message("new object capacity"));
        const int id = nextObject++;
        Value dictionary; dictionary.kind = Value::Dictionary;
        replaceStream(id, dictionary, data);
        return id;
    }
    void save(const QString &path)
    {
        // A complete new revision avoids stale-object text in readers which
        // do not implement incremental updates. Unchanged objects are copied
        // from the original parsed value/stream, without touching font bytes.
        QByteArray out = bytes.left(bytes.indexOf('\n') + 1) + QByteArrayLiteral("%\xE2\xE3\xCF\xD3\n");
        QMap<int, int> newOffsets;
        for (int id = 1; id < nextObject; ++id)
        {
            if (!offsets.contains(id) && !replacements.contains(id)) continue;
            newOffsets.insert(id, out.size());
            out += QByteArray::number(id) + " 0 obj\n";
            if (replacements.contains(id)) out += replacements.value(id);
            else
            {
                const Object &obj = object(id);
                out += serialize(obj.value);
                if (obj.hasStream) out += "\nstream\n" + obj.stream + "\nendstream";
            }
            out += "\nendobj\n";
            require(out.size() <= maximumPdfBytes, message("final PDF capacity"));
        }
        const int xref = out.size();
        out += "xref\n0 " + QByteArray::number(nextObject) + "\n0000000000 65535 f \n";
        for (int id = 1; id < nextObject; ++id)
            out += newOffsets.contains(id)
                ? QByteArray::number(newOffsets.value(id)).rightJustified(10, '0') + " 00000 n \n"
                : QByteArrayLiteral("0000000000 00000 f \n");
        trailer.entries.insert("Size", number(nextObject));
        out += "trailer\n" + serialize(trailer) + "\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
        require(out.size() <= maximumPdfBytes, message("final PDF capacity"));
        QFile target(path);
        require(target.open(QIODevice::WriteOnly | QIODevice::NewOnly), message("output must be a new file"));
        const bool success = target.write(out) == out.size() && target.flush();
        target.close();
        if (!success) target.remove();
        require(success, message("new PDF write failed"));
    }
};

QByteArray utf16Hex(const QString &text)
{
    QByteArray result;
    for (QChar c : text)
    {
        const ushort unit = c.unicode();
        result += char(unit >> 8);
        result += char(unit & 255);
    }
    return result.toHex().toUpper();
}
QString unicode(const QByteArray &bytes)
{
    require(bytes.size() % 2 == 0 && bytes.size() <= 64, message("ToUnicode string capacity"));
    QString result;
    for (int i = 0; i < bytes.size(); i += 2)
        result += QChar((uchar(bytes.at(i)) << 8) | uchar(bytes.at(i + 1)));
    return result;
}
int cid(const Value &v)
{
    require(v.kind == Value::Hex && v.raw.size() == 2, message("two-byte CID required"));
    return (uchar(v.raw.at(0)) << 8) | uchar(v.raw.at(1));
}
QByteArray hexCid(int value) { return QByteArray::number(value, 16).rightJustified(4, '0').toUpper(); }

QMap<int, QString> readCMap(const QByteArray &bytes)
{
    Parser parser(bytes);
    QMap<int, QString> result;
    while (parser.position < bytes.size())
    {
        parser.spaces();
        if (parser.position == bytes.size()) break;
        Value token = parser.value(false);
        if (token.kind != Value::Word || (token.raw != "beginbfchar" && token.raw != "beginbfrange")) continue;
        const bool range = token.raw == "beginbfrange";
        for (;;)
        {
            token = parser.value(false);
            if (token.kind == Value::Word)
            {
                require(token.raw == (range ? "endbfrange" : "endbfchar"), message("CMap record end"));
                break;
            }
            const int first = cid(token);
            const int last = range ? cid(parser.value(false)) : first;
            Value destination = parser.value(false);
            require(last >= first && last - first < 65536, message("CMap range bounds"));
            if (destination.kind == Value::Array)
            {
                require(destination.items.size() == last - first + 1, message("CMap array bounds"));
                for (int index = first; index <= last; ++index)
                {
                    const Value &entry = destination.items.at(index - first);
                    require(entry.kind == Value::Hex, message("CMap array hex required"));
                    result.insert(index, unicode(entry.raw));
                }
            }
            else
            {
                require(destination.kind == Value::Hex, message("CMap destination hex required"));
                if (first == last) result.insert(first, unicode(destination.raw));
                else
                {
                    require(destination.raw.size() == 2, message("linear BMP CMap range required"));
                    const int base = cid(destination);
                    require(base + last - first <= 65535, message("CMap Unicode range bounds"));
                    for (int index = first; index <= last; ++index) result.insert(index, QString(QChar(base + index - first)));
                }
            }
        }
    }
    require(!result.isEmpty(), message("missing ToUnicode records"));
    return result;
}

struct Font
{
    int object = 0;
    int descendant = 0;
    int toUnicode = 0;
    int cidSet = 0;
    int maximumCid = 0;
    int defaultWidth = 1000;
    QMap<int, int> widths;
    QMap<int, QString> oldUnicode;
    QMap<int, int> sourceGlyphs;
    QMap<QPair<int, QString>, int> assigned;
    QMap<int, int> newToOld;
    QMap<int, QString> newUnicode;
};

Font readFont(Pdf &pdf, int id)
{
    Font font;
    font.object = id;
    const Value top = pdf.object(id).value;
    require(top.kind == Value::Dictionary && top.entries.value("Subtype").named("Type0")
            && top.entries.value("Encoding").named("Identity-H"), message("only Qt Type0 Identity-H fonts accepted"));
    const Value descendants = top.entries.value("DescendantFonts");
    require(descendants.kind == Value::Array && descendants.items.size() == 1
            && descendants.items.first().kind == Value::Reference, message("font descendant contract"));
    font.descendant = descendants.items.first().reference;
    const Value desc = pdf.object(font.descendant).value;
    require(desc.kind == Value::Dictionary && desc.entries.value("Subtype").named("CIDFontType2")
            && desc.entries.value("CIDToGIDMap").named("Identity"), message("only Qt identity glyph maps accepted"));
    const Value toUnicode = top.entries.value("ToUnicode");
    require(toUnicode.kind == Value::Reference, message("font ToUnicode reference"));
    font.toUnicode = toUnicode.reference;
    const Value descriptorRef = desc.entries.value("FontDescriptor");
    require(descriptorRef.kind == Value::Reference, message("font descriptor reference"));
    const Value descriptor = pdf.object(descriptorRef.reference).value;
    const Value cidSet = descriptor.entries.value("CIDSet");
    require(cidSet.kind == Value::Reference, message("Qt subset CIDSet reference"));
    font.cidSet = cidSet.reference;
    font.oldUnicode = readCMap(pdf.decoded(font.toUnicode));
    font.maximumCid = font.oldUnicode.lastKey();
    if (desc.entries.contains("DW")) font.defaultWidth = desc.entries.value("DW").integer();
    if (desc.entries.contains("W"))
    {
        const Value widths = desc.entries.value("W");
        require(widths.kind == Value::Array, message("font widths array"));
        int index = 0;
        while (index < widths.items.size())
        {
            int first = widths.items.at(index++).integer();
            require(first >= 0 && first <= 65535 && index < widths.items.size(), message("width CID bounds"));
            const Value &next = widths.items.at(index++);
            if (next.kind == Value::Array)
            {
                require(first + next.items.size() <= 65536, message("width array bounds"));
                for (const Value &width : next.items) font.widths.insert(first++, width.integer());
                font.maximumCid = qMax(font.maximumCid, first - 1);
            }
            else
            {
                const int last = next.integer();
                require(last >= first && last <= 65535 && index < widths.items.size(), message("width range bounds"));
                const int width = widths.items.at(index++).integer();
                for (; first <= last; ++first) font.widths.insert(first, width);
                font.maximumCid = qMax(font.maximumCid, last);
            }
        }
    }
    return font;
}

struct Glyph
{
    int font = 0;
    int oldCid = 0;
    int begin = 0;
    int end = 0;
};
struct Page
{
    int stream = 0;
    QByteArray content;
    QList<Glyph> glyphs;
    QMap<int, QList<int>> spans; // span id -> indices in glyphs
    QMap<int, int> markers; // complete off-page rectangle draw ranges
};

Page readPage(Pdf &pdf, const Value &value, QMap<int, Font> &fonts, int &nextMarker)
{
    Page page;
    const Value resources = pdf.resolved(value.entries.value("Resources"));
    require(resources.kind == Value::Dictionary, message("page resources dictionary"));
    const Value fontNames = pdf.resolved(resources.entries.value("Font"));
    require(fontNames.kind == Value::Dictionary, message("page font resource dictionary"));
    QMap<QByteArray, int> fontIds;
    for (auto it = fontNames.entries.cbegin(); it != fontNames.entries.cend(); ++it)
    {
        require(it.value().kind == Value::Reference, message("font resource reference"));
        const int id = it.value().reference;
        fontIds.insert(it.key(), id);
        if (!fonts.contains(id)) fonts.insert(id, readFont(pdf, id));
    }
    const Value content = value.entries.value("Contents");
    require(content.kind == Value::Reference, message("single Qt page content stream required"));
    page.stream = content.reference;
    page.content = pdf.decoded(page.stream);
    Parser parser(page.content);
    QList<Value> operands;
    int activeSpan = 0;
    int activeFont = 0;
    bool inText = false;
    while (parser.position < page.content.size())
    {
        parser.spaces();
        if (parser.position == page.content.size()) break;
        const Value token = parser.value(false);
        if (token.kind != Value::Word)
        {
            operands.append(token);
            require(operands.size() <= 4096, message("content operand capacity"));
            continue;
        }
        const QByteArray op = token.raw;
        if (op == "re" && operands.size() == 4 && operands.at(0).kind == Value::Number
            && operands.at(0).number() <= -20001 && operands.at(2).kind == Value::Number
            && operands.at(2).number() == 0.125 && operands.at(3).kind == Value::Number
            && operands.at(3).number() == 0.25)
        {
            const double x = operands.at(0).number();
            require(x >= -20000 - maximumSpans, message("marker coordinate capacity"));
            const int id = int(-20000 - x);
            require(x == -20000 - id && id >= 1 && id <= maximumSpans && !inText,
                    message("marker id/position"));
            const Value paint = parser.value(false);
            require(paint.kind == Value::Word
                    && (paint.raw == "S" || paint.raw == "f" || paint.raw == "B"
                        || paint.raw == "f*" || paint.raw == "B*"), message("marker rectangle paint boundary"));
            page.markers.insert(operands.first().begin, paint.end);
            if (operands.at(1).number() == -20000)
            {
                require(!activeSpan && id == nextMarker && !page.spans.contains(id), message("marker start order"));
                activeSpan = id;
                page.spans.insert(id, {});
            }
            else
            {
                require(operands.at(1).number() == -20001 && activeSpan == id, message("marker end order"));
                activeSpan = 0;
                ++nextMarker;
            }
        }
        else if (op == "BT") { require(!inText, message("nested text object")); inText = true; }
        else if (op == "ET") { require(inText, message("unbalanced text object")); inText = false; }
        else if (op == "Tf")
        {
            require(inText && operands.size() == 2 && operands.first().kind == Value::Name
                    && fontIds.contains(operands.first().raw), message("font selection"));
            activeFont = fontIds.value(operands.first().raw);
        }
        else if (op == "Tj")
        {
            require(inText && activeSpan && activeFont && operands.size() == 1,
                    message("every text glyph must have source markers"));
            const int oldCid = cid(operands.first());
            require(oldCid != 0, message("notdef glyph forbidden"));
            Glyph glyph;
            glyph.font = activeFont;
            glyph.oldCid = oldCid;
            glyph.begin = operands.first().begin;
            glyph.end = operands.first().end;
            page.spans[activeSpan].append(page.glyphs.size());
            page.glyphs.append(glyph);
            fonts[activeFont].maximumCid = qMax(fonts[activeFont].maximumCid, oldCid);
        }
        else if (op == "TJ" || op == "'" || op == "\"" || op == "BI" || op == "Do" || op == "BDC" || op == "BMC")
            require(false, message("unsupported text/image/marked-content operator"));
        operands.clear();
        require(page.glyphs.size() <= 250000, message("page glyph capacity"));
    }
    require(!inText && !activeSpan && operands.isEmpty(), message("page ended inside text/span"));
    return page;
}

struct Face
{
    QRawFont raw;
    QSet<quint32> bmpGlyphs;
};
Face sourceFace(const QFont &font)
{
    Face face;
    face.raw = QRawFont::fromFont(font);
    require(face.raw.isValid(), message("source font unavailable"));
    QString bmp;
    bmp.reserve(63488);
    for (int value = 0; value <= 65535; ++value)
        if (value < 0xd800 || value > 0xdfff) bmp += QChar(value);
    const QVector<quint32> glyphs = face.raw.glyphIndexesForString(bmp);
    require(glyphs.size() == bmp.size(), message("font BMP inverse-map cardinality"));
    for (quint32 glyph : glyphs) if (glyph) face.bmpGlyphs.insert(glyph);
    return face;
}

quint32 oneGlyph(const QTextLayout &layout, int start, int length, const QRawFont &raw)
{
    // Shape in the complete label's context. Noto can choose contextual
    // Latin/digit glyphs with no BMP cmap entry (e.g. the 0 in B-09), so
    // shaping each scalar in isolation would reject legitimate output.
    const QList<QGlyphRun> runs = layout.glyphRuns(start, length);
    if (runs.size() != 1 || runs.first().isRightToLeft()) return 0;
    const QGlyphRun &run = runs.first();
    // A range cutting an ffi ligature must not claim it represents one f.
    if (run.flags().testFlag(QGlyphRun::SplitLigature)) return 0;
    if (run.rawFont().familyName() != raw.familyName() || run.rawFont().styleName() != raw.styleName()) return 0;
    const QVector<quint32> indexes = run.glyphIndexes();
    return indexes.size() == 1 ? indexes.first() : 0;
}

void mapSpan(Page &page, const HistoryPdfTextSpan &span, QMap<int, Font> &fonts,
             QMap<QString, Face> &faces, QMap<int, QByteArray> &edits)
{
    require(span.text.size() <= 4096, message("source field text capacity"));
    for (int index = 0; index < span.text.size(); ++index)
    {
        const QChar c = span.text.at(index);
        if (c.isHighSurrogate())
        {
            require(index + 1 < span.text.size() && span.text.at(index + 1).isLowSurrogate(),
                    message("unpaired source UTF16 surrogate"));
            ++index;
            continue;
        }
        require(!c.isLowSurrogate(), message("unpaired source UTF16 surrogate"));
        require(c.direction() != QChar::DirR && c.direction() != QChar::DirAL
                && c.direction() != QChar::DirRLE && c.direction() != QChar::DirRLO
                && c.direction() != QChar::DirLRE && c.direction() != QChar::DirLRO,
                message("bidirectional shaping needs explicit glyph trace"));
    }
    const QString key = span.font.key();
    if (!faces.contains(key)) faces.insert(key, sourceFace(span.font));
    const Face &face = faces[key];
    QTextLayout layout(span.text, span.font);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (!span.text.isEmpty()) require(line.isValid(), message("source shaping failed"));
    if (line.isValid()) line.setLineWidth(1000000);
    layout.endLayout();
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, span.text);
    QList<int> graphemeEnds;
    for (int end = boundaries.toNextBoundary(); end >= 0; end = boundaries.toNextBoundary()) graphemeEnds.append(end);
    int position = 0;
    const QList<int> indices = page.spans.value(span.id);
    for (int glyphIndex : indices)
    {
        const Glyph &glyph = page.glyphs.at(glyphIndex);
        Font &font = fonts[glyph.font];
        const QString oldText = font.oldUnicode.value(glyph.oldCid);
        quint32 expected = 0;
        if (!oldText.isEmpty())
        {
            const QVector<quint32> original = face.raw.glyphIndexesForString(oldText);
            require(original.size() == 1 && original.first(), message("old glyph source cannot be established"));
            expected = original.first();
        }
        if (font.sourceGlyphs.contains(glyph.oldCid))
        {
            const quint32 prior = quint32(font.sourceGlyphs.value(glyph.oldCid));
            require(!expected || expected == prior, message("source font changed for existing CID"));
            expected = prior;
        }

        QString selected;
        quint32 selectedGlyph = 0;
        int selectedEnd = position;
        // Only grapheme boundaries can split semantic text. At most eight
        // UTF-16 units may form one shaped glyph. All ambiguous candidates
        // are rejected rather than selecting a convenient normalization.
        for (;;)
        {
            QList<QPair<QString, quint32>> candidates;
            QList<int> candidateEnds;
            for (int end : graphemeEnds)
            {
                if (end <= position) continue;
                if (end - position > 8) break;
                const QString candidate = span.text.mid(position, end - position);
                const quint32 shaped = oneGlyph(layout, position, end - position, face.raw);
                if (!shaped) continue;
                const bool matches = expected ? shaped == expected : !face.bmpGlyphs.contains(shaped);
                if (matches) { candidates.append(qMakePair(candidate, shaped)); candidateEnds.append(end); }
            }
            if (!candidates.isEmpty())
            {
                require(candidates.size() == 1, message("ambiguous source cluster for glyph"));
                selected = candidates.first().first;
                selectedGlyph = candidates.first().second;
                selectedEnd = candidateEnds.first();
                break;
            }
            // Word wrapping may consume a layout space without drawing it.
            // Do not turn it into an invented glyph or attach it to a letter.
            if (position < span.text.size() && span.text.at(position).isSpace()) { ++position; continue; }
            throw Failure{message("source text and rendered glyph sequence disagree")
                + QStringLiteral(" (span=%1, UTF16-offset=%2, CID=%3, expected-glyph=%4)")
                    .arg(span.id).arg(position).arg(glyph.oldCid).arg(expected)};
        }
        require(selectedGlyph != 0 && selectedEnd > position, message("empty source cluster"));
        font.sourceGlyphs.insert(glyph.oldCid, int(selectedGlyph));
        const QPair<int, QString> assignment(glyph.oldCid, selected);
        int replacement = font.assigned.value(assignment, 0);
        if (!replacement)
        {
            require(font.maximumCid < 65535, message("CID capacity exceeded"));
            replacement = ++font.maximumCid;
            font.assigned.insert(assignment, replacement);
            font.newToOld.insert(replacement, glyph.oldCid);
            font.newUnicode.insert(replacement, selected);
        }
        edits.insert(glyph.begin, '<' + hexCid(replacement) + '>');
        position = selectedEnd;
    }
    while (position < span.text.size() && span.text.at(position).isSpace()) ++position;
    require(position == span.text.size(), message("source field has unmatched text"));
}

void writeFont(Pdf &pdf, const Font &font)
{
    if (font.newToOld.isEmpty()) return;
    Value descendant = pdf.object(font.descendant).value;
    QByteArray gidMap((font.maximumCid + 1) * 2, '\0');
    for (int value = 0; value <= font.maximumCid; ++value)
    {
        const int gid = font.newToOld.value(value, value);
        gidMap[value * 2] = char(gid >> 8);
        gidMap[value * 2 + 1] = char(gid & 255);
    }
    descendant.entries.insert("CIDToGIDMap", reference(pdf.addStream(gidMap)));
    Value widths;
    widths.kind = Value::Array;
    for (auto it = font.newToOld.cbegin(); it != font.newToOld.cend(); ++it)
    {
        widths.items.append(number(it.key()));
        Value one;
        one.kind = Value::Array;
        one.items.append(number(font.widths.value(it.value(), font.defaultWidth)));
        widths.items.append(one);
    }
    const Value originalWidths = descendant.entries.value("W");
    if (originalWidths.kind == Value::Array) widths.items = originalWidths.items + widths.items;
    descendant.entries.insert("W", widths);
    pdf.replacements[font.descendant] = serialize(descendant);

    QByteArray cidSet = pdf.decoded(font.cidSet);
    require(cidSet.size() <= 8192, message("CIDSet capacity"));
    const int oldSetBytes = cidSet.size();
    cidSet.resize((font.maximumCid / 8) + 1);
    // resize does not initialize newly allocated QByteArray bytes.
    for (int index = oldSetBytes; index < cidSet.size(); ++index) cidSet[index] = 0;
    for (auto it = font.newToOld.cbegin(); it != font.newToOld.cend(); ++it)
        cidSet[it.key() / 8] = char(uchar(cidSet.at(it.key() / 8)) | (1 << (7 - it.key() % 8)));
    pdf.replaceStream(font.cidSet, pdf.object(font.cidSet).value, cidSet);

    QMap<int, QString> mappings = font.oldUnicode;
    for (auto it = font.newUnicode.cbegin(); it != font.newUnicode.cend(); ++it) mappings.insert(it.key(), it.value());
    QByteArray cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    auto it = mappings.cbegin();
    while (it != mappings.cend())
    {
        QByteArray entries;
        int count = 0;
        while (it != mappings.cend() && count < 100)
        {
            entries += '<' + hexCid(it.key()) + "> <" + utf16Hex(it.value()) + ">\n";
            ++it;
            ++count;
        }
        cmap += QByteArray::number(count) + " beginbfchar\n" + entries + "endbfchar\n";
    }
    cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    pdf.replaceStream(font.toUnicode, pdf.object(font.toUnicode).value, cmap);
}
}

bool rewriteHistoryPdfTextMapping(const QString &input, const QString &newOutput,
                                 const QList<HistoryPdfTextSpan> &spans, QString *error)
{
    QString ignored;
    if (!error) error = &ignored;
    error->clear();
    try
    {
        require(QFileInfo(input).isAbsolute() && QFileInfo(newOutput).isAbsolute()
                && input != newOutput && !QFileInfo(input).isSymLink() && !QFileInfo(newOutput).exists(),
                message("distinct absolute regular input/new output required"));
        require(!spans.isEmpty() && spans.size() <= maximumSpans, message("source span capacity"));
        qint64 sourceUnits = 0;
        for (int index = 0; index < spans.size(); ++index)
        {
            require(spans.at(index).id == index + 1, message("source ids must be contiguous"));
            sourceUnits += spans.at(index).text.size();
        }
        require(sourceUnits <= 1024 * 1024, message("total source text capacity"));
        Pdf pdf;
        pdf.load(input);
        const Value catalog = pdf.object(pdf.trailer.entries.value("Root").reference).value;
        const Value pagesRef = catalog.entries.value("Pages");
        require(pagesRef.kind == Value::Reference, message("catalog page tree"));
        const Value tree = pdf.object(pagesRef.reference).value;
        const Value kids = tree.entries.value("Kids");
        require(kids.kind == Value::Array && !kids.items.isEmpty() && kids.items.size() <= 128,
                message("flat Qt page tree capacity"));
        QMap<int, Font> fonts;
        QList<Page> pages;
        int nextMarker = 1;
        for (const Value &kid : kids.items)
        {
            require(kid.kind == Value::Reference, message("page reference"));
            const Value value = pdf.object(kid.reference).value;
            require(value.entries.value("Type").named("Page"), message("flat Qt page tree required"));
            pages.append(readPage(pdf, value, fonts, nextMarker));
        }
        require(nextMarker == spans.size() + 1, message("source/marker total differs"));
        QMap<QString, Face> faces;
        for (Page &page : pages)
        {
            QMap<int, QByteArray> edits;
            for (auto it = page.spans.cbegin(); it != page.spans.cend(); ++it)
                mapSpan(page, spans.at(it.key() - 1), fonts, faces, edits);
            QByteArray mapped;
            int start = 0;
            QMap<int, QPair<int, QByteArray>> replacements;
            for (auto it = page.markers.cbegin(); it != page.markers.cend(); ++it)
                replacements.insert(it.key(), qMakePair(it.value(), QByteArray()));
            for (const Glyph &glyph : page.glyphs)
            {
                require(edits.contains(glyph.begin), message("glyph edit coverage"));
                replacements.insert(glyph.begin, qMakePair(glyph.end, edits.value(glyph.begin)));
            }
            for (auto it = replacements.cbegin(); it != replacements.cend(); ++it)
            {
                require(it.key() >= start && it.value().first >= it.key(), message("mapping edit coverage/order"));
                mapped += page.content.mid(start, it.key() - start) + it.value().second;
                start = it.value().first;
            }
            mapped += page.content.mid(start);
            pdf.replaceStream(page.stream, pdf.object(page.stream).value, mapped);
        }
        for (const Font &font : fonts) writeFont(pdf, font);
        pdf.save(newOutput);
        return true;
    }
    catch (const Failure &failure) { *error = failure.message; }
    catch (const std::bad_alloc &) { *error = message("memory capacity exceeded"); }
    return false;
}
