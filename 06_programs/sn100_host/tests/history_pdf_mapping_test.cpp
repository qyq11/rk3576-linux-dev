// Independently mutate a real anonymous QtPDF fixture, then require safe
// rejection. Only new files in the explicitly supplied test directory are used.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include "report_pdf_mapping.h"

namespace
{
bool readFile(const QString &path, QByteArray *bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    *bytes = file.readAll();
    return true;
}

bool newFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        && file.write(bytes) == bytes.size() && file.flush();
}

// Fixture tooling accepts only the known classic-xref QtPDF. Rebuild offsets
// after changing a compressed page stream. An identity control below proves
// that this rewrite itself is accepted before marker-specific failures run.
QByteArray mutatePage(const QByteArray &input, const QString &operation)
{
    const QString latin = QString::fromLatin1(input.constData(), input.size());
    auto start = QRegularExpression(QStringLiteral("startxref\\s+(\\d+)\\s+%%EOF\\s*$")).match(latin);
    if (!start.hasMatch()) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    const int xrefOffset = start.captured(1).toInt();
    const QByteArray xref = input.mid(xrefOffset);
    auto countMatch = QRegularExpression(QStringLiteral("^xref\\s+0 (\\d+)\\s*")).match(QString::fromLatin1(xref));
    if (!countMatch.hasMatch()) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    const int count = countMatch.captured(1).toInt();
    const QList<QByteArray> lines = xref.mid(countMatch.capturedEnd()).split('\n');
    if (lines.size() < count) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    QMap<int, int> offsets;
    for (int id = 1; id < count; ++id) offsets.insert(lines.at(id).left(10).toInt(), id);
    QMap<int, QByteArray> objects;
    const QList<int> positions = offsets.keys();
    for (int index = 0; index < positions.size(); ++index)
    {
        const int position = positions.at(index);
        const int end = index + 1 < positions.size() ? positions.at(index + 1) : xrefOffset;
        objects.insert(offsets.value(position), input.mid(position, end - position));
    }
    int contentId = 0;
    for (const QByteArray &object : objects)
    {
        const QString source = QString::fromLatin1(object);
        if (!QRegularExpression(QStringLiteral("/Type /Page\\s")).match(source).hasMatch()) continue;
        const auto contents = QRegularExpression(QStringLiteral("/Contents (\\d+) 0 R")).match(source);
        if (contents.hasMatch()) { contentId = contents.captured(1).toInt(); break; }
    }
    if (!contentId || !objects.contains(contentId)) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    QByteArray object = objects.value(contentId);
    const int streamStart = object.indexOf("stream\n") + 7;
    const int streamEnd = object.lastIndexOf("\nendstream");
    if (streamStart < 7 || streamEnd <= streamStart) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    const auto length = QRegularExpression(QStringLiteral("/Length (\\d+) 0 R")).match(QString::fromLatin1(object.left(streamStart)));
    if (!length.hasMatch()) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    QByteArray packed(4, '\0');
    packed.append(object.mid(streamStart, streamEnd - streamStart));
    // qUncompress's leading four bytes are only an allocation hint.
    QByteArray content = qUncompress(packed);
    if (content.isEmpty() || !content.contains("-20001 -20000 0.125000000 0.250000000 re"))
    { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    if (operation == QStringLiteral("missing_marker"))
        content.replace("-20001 -20000 0.125000000 0.250000000 re", "-20001 -20002 0.125000000 0.250000000 re");
    else if (operation == QStringLiteral("duplicate_marker"))
    {
        content.replace("-20002 -20000 0.125000000 0.250000000 re", "-20001 -20000 0.125000000 0.250000000 re");
        content.replace("-20002 -20001 0.125000000 0.250000000 re", "-20001 -20001 0.125000000 0.250000000 re");
    }
    else if (operation == QStringLiteral("marker_coordinate_overflow"))
        content.replace("-20001 -20000 0.125000000 0.250000000 re", "-99999999999999 -20000 0.125000000 0.250000000 re");
    const QByteArray compressed = qCompress(content, 9).mid(4);
    objects[contentId] = object.left(streamStart) + compressed + object.mid(streamEnd);
    const int lengthId = length.captured(1).toInt();
    objects[lengthId] = QByteArray::number(lengthId) + " 0 obj\n" + QByteArray::number(compressed.size()) + "\nendobj\n";
    if (operation == QStringLiteral("length_cycle"))
        objects[lengthId] = QByteArray::number(lengthId) + " 0 obj\n<< /Length "
            + QByteArray::number(contentId) + " 0 R >>\nstream\nendstream\nendobj\n";
    QByteArray result = input.left(positions.first());
    QMap<int, int> newOffsets;
    for (int position : positions)
    {
        const int id = offsets.value(position);
        newOffsets.insert(id, result.size());
        result.append(objects.value(id));
    }
    const int newXref = result.size();
    result.append("xref\n0 " + QByteArray::number(count) + "\n0000000000 65535 f \n");
    for (int id = 1; id < count; ++id)
        result.append(QByteArray::number(newOffsets.value(id)).rightJustified(10, '0') + " 00000 n \n");
    const int trailer = xref.indexOf("trailer");
    if (trailer < 0) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    QByteArray tail = xref.mid(trailer);
    const int startInTail = tail.lastIndexOf("startxref");
    if (startInTail < 0) { qWarning() << "fixture_mutation_failed" << __LINE__; return {}; }
    result.append(tail.left(startInTail) + "startxref\n" + QByteArray::number(newXref) + "\n%%EOF\n");
    return result;
}
}

class HistoryPdfMappingTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void positiveControls();
    void reject_data();
    void reject();
private:
    QString m_root;
    QString m_fixture;
    QByteArray m_original;
    QList<HistoryPdfTextSpan> m_spans;
};

void HistoryPdfMappingTest::initTestCase()
{
    m_root = qEnvironmentVariable("SN100_PDF_MAPPING_TEST_DIR");
    m_fixture = qEnvironmentVariable("SN100_PDF_MAPPING_FIXTURE");
    QVERIFY(QDir::isAbsolutePath(m_root) && !QFileInfo::exists(m_root));
    QVERIFY(QDir().mkpath(m_root));
    QVERIFY(readFile(m_fixture, &m_original));
    QVERIFY(m_original.startsWith("%PDF-") && m_original.size() > 1024);
    QByteArray trace;
    QVERIFY(readFile(m_fixture + QStringLiteral(".spans.json"), &trace));
    const auto array = QJsonDocument::fromJson(trace).array();
    QVERIFY(!array.isEmpty());
    for (const auto &value : array)
    {
        const auto item = value.toObject();
        HistoryPdfTextSpan span;
        span.id = item.value(QStringLiteral("id")).toInt();
        span.text = item.value(QStringLiteral("text")).toString();
        QVERIFY(span.font.fromString(item.value(QStringLiteral("font")).toString()));
        m_spans.append(span);
    }
}

void HistoryPdfMappingTest::positiveControls()
{
    QString error;
    const QString originalOutput = QDir(m_root).filePath(QStringLiteral("positive.pdf"));
    QVERIFY2(rewriteHistoryPdfTextMapping(m_fixture, originalOutput, m_spans, &error), qPrintable(error));
    const QByteArray identity = mutatePage(m_original, QStringLiteral("identity"));
    QVERIFY(!identity.isEmpty());
    const QString identityPath = QDir(m_root).filePath(QStringLiteral("identity.pdf"));
    QVERIFY(newFile(identityPath, identity));
    QVERIFY2(rewriteHistoryPdfTextMapping(identityPath, QDir(m_root).filePath(QStringLiteral("identity_mapped.pdf")),
                                         m_spans, &error), qPrintable(error));
}

void HistoryPdfMappingTest::reject_data()
{
    QTest::addColumn<QString>("operation");
    for (const QString &operation : {QStringLiteral("bad_pdf"), QStringLiteral("truncated_pdf"),
            QStringLiteral("xref_out_of_range"), QStringLiteral("missing_marker"), QStringLiteral("duplicate_marker"),
            QStringLiteral("missing_span"), QStringLiteral("duplicate_span"), QStringLiteral("wrong_source"),
            QStringLiteral("all_free_xref"), QStringLiteral("xref_count_overflow"),
            QStringLiteral("length_cycle"), QStringLiteral("marker_coordinate_overflow"), QStringLiteral("existing_output")})
        QTest::newRow(qPrintable(operation)) << operation;
}

void HistoryPdfMappingTest::reject()
{
    QFETCH(QString, operation);
    auto spans = m_spans;
    QByteArray source = m_original;
    if (operation == QStringLiteral("bad_pdf")) source = "anonymous invalid PDF fixture\n";
    else if (operation == QStringLiteral("truncated_pdf")) source.truncate(source.size() / 2);
    else if (operation == QStringLiteral("xref_out_of_range"))
    {
        const int start = source.lastIndexOf("startxref");
        QVERIFY(start > 0);
        source = source.left(start) + "startxref\n999999999\n%%EOF\n";
    }
    else if (operation == QStringLiteral("all_free_xref"))
    {
        const int start = source.lastIndexOf("\nxref\n") + 1;
        QVERIFY(start > 0);
        QByteArray table = source.mid(start);
        table.replace(" 00000 n ", " 00000 f ");
        source = source.left(start) + table;
    }
    else if (operation == QStringLiteral("xref_count_overflow"))
    {
        const int start = source.lastIndexOf("\nxref\n") + 1;
        QVERIFY(start > 0);
        const int entries = source.indexOf('\n', start + 5);
        QVERIFY(entries > start);
        source = source.left(start + 5) + "2147483647 2147483647" + source.mid(entries);
    }
    else if (operation.endsWith(QStringLiteral("_marker"))
             || operation == QStringLiteral("length_cycle") || operation == QStringLiteral("marker_coordinate_overflow"))
    {
        source = mutatePage(m_original, operation);
        QVERIFY(!source.isEmpty());
    }
    else if (operation == QStringLiteral("missing_span")) spans.removeFirst();
    else if (operation == QStringLiteral("duplicate_span")) spans.append(spans.first());
    else if (operation == QStringLiteral("wrong_source")) spans.first().text[0] = QChar(0x516c); // 公 replaces 匿.
    const QString input = QDir(m_root).filePath(operation + QStringLiteral("_input.pdf"));
    const QString output = QDir(m_root).filePath(operation + QStringLiteral("_output.pdf"));
    QVERIFY(newFile(input, source));
    const QByteArray sentinel = QByteArrayLiteral("owned by earlier caller; never overwrite");
    if (operation == QStringLiteral("existing_output")) QVERIFY(newFile(output, sentinel));
    QString error;
    QVERIFY2(!rewriteHistoryPdfTextMapping(input, output, spans, &error), "Invalid mapping unexpectedly succeeded");
    QVERIFY2(!error.isEmpty(), "Rejected mapping must explain the failure");
    if (operation == QStringLiteral("existing_output"))
    {
        QByteArray actual;
        QVERIFY(readFile(output, &actual));
        QCOMPARE(actual, sentinel);
    }
    else QVERIFY2(!QFileInfo::exists(output), "Failed mapping must not leave any output file");
    const QString errorPath = QDir(m_root).filePath(operation + QStringLiteral("_error.txt"));
    QVERIFY(newFile(errorPath, error.toUtf8()));
    QByteArray retainedInput;
    QVERIFY(readFile(input, &retainedInput));
    QCOMPARE(retainedInput, source);
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QGuiApplication application(argc, argv);
    HistoryPdfMappingTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "history_pdf_mapping_test.moc"
