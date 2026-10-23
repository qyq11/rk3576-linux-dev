// Headless, anonymous end-to-end PDF text checks. This target never opens the
// history UI, a database, an instrument connection or a physical print dialog.
#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>

namespace
{
QString pathForPython(const QString &path, const QString &python)
{
#ifdef Q_OS_LINUX
    if (python.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
        && path.startsWith(QStringLiteral("/mnt/")) && path.size() > 7)
        return path.mid(5, 1).toUpper() + QStringLiteral(":/") + path.mid(7);
#endif
    return path;
}

QJsonObject snapshot(int rows)
{
    // Each alias pair is intentional source text. The renderer must preserve
    // both Unicode values even when their visible glyph IDs are shared.
    const QStringList names{QStringLiteral("页⻚"), QStringLiteral("生⽣"),
        QStringLiteral("日⽇"), QStringLiteral("目⽬"), QStringLiteral("女⼥"),
        QStringLiteral("人⼈"), QStringLiteral("血⾎"), QStringLiteral("二⼆"),
        QStringLiteral("ASCII\"Q\"'x'"), QStringLiteral("𠀋“引号”")};
    QJsonObject headers{{"hospital_name", QStringLiteral("匿名中文验证医院")},
        {"name", QStringLiteral("匿名分页")}, {"sex", QStringLiteral("女")}, {"age", "23"},
        {"bed_number", "B-09"}, {"department", QStringLiteral("血液科")},
        {"sample_provider", QStringLiteral("测试医生甲")}, {"sample_sn", "987"},
        {"patient_type", QStringLiteral("匿名病人")}, {"report_date", "2026-10-23 12:34:56"},
        {"mark", QStringLiteral("二号'单引号'\"双引号\"")},
        {"checker", QStringLiteral("检验医生乙")}, {"reviewer", QStringLiteral("审核医生丙")}};
    QJsonArray values;
    for (int row = 0; row < rows; ++row)
        values.append(QJsonObject{{"project", QStringLiteral("P%1").arg(row + 1, 2, 10, QChar('0'))},
            {"project_name", names.at(row % names.size())},
            {"project_result", QString::number(9001 + row) + QStringLiteral(".125")},
            {"reference_range", QString()}, {"unit", "s"}});
    return {{"schema", "sn100-render-snapshot-v1"}, {"headers", headers}, {"rows", values}};
}
}

class HistoryPdfTextTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void exactText_data();
    void exactText();
    void complexShape_data();
    void complexShape();
private:
    void verifyDocument(const QString &name, const QJsonObject &document, int pages,
                        bool allowSafeRejection = false);
    QString m_root;
    QString m_renderer;
    QString m_python;
    QString m_validator;
};

void HistoryPdfTextTest::initTestCase()
{
    m_root = qEnvironmentVariable("SN100_PDF_TEXT_TEST_DIR");
    m_renderer = qEnvironmentVariable("SN100_REPORT_RENDERER");
    m_python = qEnvironmentVariable("SN100_REPORT_PDF_PYTHON");
    m_validator = qEnvironmentVariable("SN100_PDF_TEXT_VALIDATOR");
    QVERIFY2(QDir::isAbsolutePath(m_root) && !QFileInfo::exists(m_root),
             "SN100_PDF_TEXT_TEST_DIR must be a new absolute test directory");
    QVERIFY2(QFileInfo(m_renderer).isExecutable(), "SN100_REPORT_RENDERER must select the new renderer");
    QVERIFY2(QFileInfo(m_python).isExecutable(), "SN100_REPORT_PDF_PYTHON must contain pypdf and pdfplumber");
    QVERIFY2(QFileInfo(m_validator).isFile(), "SN100_PDF_TEXT_VALIDATOR must select verify_history_pdf_text.py");
    QVERIFY(QDir().mkpath(m_root));
}

void HistoryPdfTextTest::exactText_data()
{
    QTest::addColumn<int>("rows");
    QTest::addColumn<int>("pages");
    QTest::newRow("one_page_alias_pairs_quotes_nonbmp") << 10 << 1;
    QTest::newRow("five_pages_64_rows") << 64 << 5;
}

void HistoryPdfTextTest::exactText()
{
    QFETCH(int, rows);
    QFETCH(int, pages);
    verifyDocument(QString::fromLatin1(QTest::currentDataTag()), snapshot(rows), pages);
}

void HistoryPdfTextTest::complexShape_data()
{
    QTest::addColumn<QString>("source");
    QTest::newRow("latin_ligatures") << QStringLiteral("Office office ffi fi ﬃ ﬁ");
    QTest::newRow("decomposed_acute") << QStringLiteral("e\u0301");
    QTest::newRow("precomposed_acute") << QStringLiteral("é");
    QTest::newRow("both_acute_forms") << QStringLiteral("e\u0301 é");
}

void HistoryPdfTextTest::complexShape()
{
    QFETCH(QString, source);
    auto document = snapshot(1);
    auto rows = document.value(QStringLiteral("rows")).toArray();
    auto row = rows.first().toObject();
    row.insert(QStringLiteral("project_name"), source);
    rows[0] = row;
    document.insert(QStringLiteral("rows"), rows);
    // A shape whose source-to-glyph association is unproven must be refused
    // explicitly. Publishing text that only looks right is never acceptable.
    verifyDocument(QString::fromLatin1(QTest::currentDataTag()), document, 1, true);
}

void HistoryPdfTextTest::verifyDocument(const QString &name, const QJsonObject &document,
                                      int pages, bool allowSafeRejection)
{
    const QDir directory(m_root);
    const QString input = directory.filePath(name + QStringLiteral(".json"));
    const QString output = directory.filePath(name + QStringLiteral(".pdf"));
    const QString inspection = directory.filePath(name + QStringLiteral("_text.json"));
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    const QByteArray bytes = QJsonDocument(document).toJson(QJsonDocument::Indented);
    QCOMPARE(file.write(bytes), qint64(bytes.size()));
    file.close();
    QProcess renderer;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    renderer.setProcessEnvironment(environment);
    renderer.start(m_renderer, {input, output});
    QVERIFY2(renderer.waitForFinished(30000), qPrintable(renderer.errorString()));
    const QByteArray rendererLog = renderer.readAllStandardOutput() + renderer.readAllStandardError();
    QFile renderLog(directory.filePath(name + QStringLiteral("_renderer.txt")));
    QVERIFY(renderLog.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    renderLog.write(rendererLog);
    renderLog.close();
    QCOMPARE(renderer.exitStatus(), QProcess::NormalExit);
    if (allowSafeRejection && renderer.exitCode() != 0)
    {
        QCOMPARE(renderer.exitCode(), 4); // Explicit prepare/render validation failure.
        QVERIFY2(rendererLog.contains("HISTORY_RENDER_ERROR"), rendererLog.constData());
        QVERIFY2(!QFileInfo::exists(output), "Rejected complex text must never publish a PDF");
        QVERIFY2(directory.entryList({QStringLiteral(".history-render-*")}, QDir::Files | QDir::Hidden).isEmpty(),
                 "Failed render must remove its owned temporary file");
        QVERIFY2(directory.entryList({QStringLiteral(".history-mapping-*")},
                 QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
                 "Failed mapping must remove its owned temporary directory");
        QFile status(directory.filePath(name + QStringLiteral("_support.json")));
        QVERIFY(status.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        status.write(QJsonDocument(QJsonObject{{"source", document.value("rows").toArray().first().toObject().value("project_name")},
            {"supported", false}, {"safely_rejected", true}, {"error", QString::fromUtf8(rendererLog)}}).toJson());
        return;
    }
    QVERIFY2(renderer.exitCode() == 0, rendererLog.constData());
    QVERIFY(QFileInfo(output).isFile());
    QProcess parser;
    parser.start(m_python, {pathForPython(m_validator, m_python),
        QStringLiteral("--pdf"), pathForPython(output, m_python),
        QStringLiteral("--snapshot"), pathForPython(input, m_python),
        QStringLiteral("--expected-pages"), QString::number(pages),
        QStringLiteral("--output"), pathForPython(inspection, m_python)});
    QVERIFY2(parser.waitForFinished(30000), qPrintable(parser.errorString()));
    const QByteArray parserLog = parser.readAllStandardOutput() + parser.readAllStandardError();
    QFile parseLog(directory.filePath(name + QStringLiteral("_parsers.txt")));
    QVERIFY(parseLog.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    parseLog.write(parserLog);
    parseLog.close();
    QCOMPARE(parser.exitStatus(), QProcess::NormalExit);
    QVERIFY2(parser.exitCode() == 0, parserLog.constData());
    QFile check(inspection);
    QVERIFY(check.open(QIODevice::ReadOnly));
    const QJsonObject result = QJsonDocument::fromJson(check.readAll()).object();
    QVERIFY(result.value(QStringLiteral("passed")).toBool());
    QCOMPARE(result.value(QStringLiteral("parsers")).toArray().size(), 2);
    if (allowSafeRejection)
    {
        QFile status(directory.filePath(name + QStringLiteral("_support.json")));
        QVERIFY(status.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        status.write(QJsonDocument(QJsonObject{{"source", document.value("rows").toArray().first().toObject().value("project_name")},
            {"supported", true}, {"safely_rejected", false}, {"exact_unicode_both_parsers", true}}).toJson());
    }
}

QTEST_GUILESS_MAIN(HistoryPdfTextTest)
#include "history_pdf_text_test.moc"
