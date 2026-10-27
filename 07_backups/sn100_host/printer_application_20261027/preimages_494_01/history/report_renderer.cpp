#include "report_render.h"
#include "report_pdf_mapping.h"
#include "qtrpt.h"
#include "report_remaining_html.h"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QTextCodec>
#include <memory>
#ifdef Q_OS_WIN
#include <QFontDatabase>
#include <QSet>
#include <QStandardPaths>
#endif

namespace
{
const qint64 kMaximumSnapshotBytes = 1024 * 1024;
const qint64 kMaximumPdfBytes = 32 * 1024 * 1024;

void markPdfText(QPainter *painter, int id, bool begin)
{
    // Both signals run outside the label's temporary transform. A tiny path
    // well outside the original page marks the boundary without changing ANY
    // painter state. In Qt5, toggling opacity can change subsequent line
    // stroking even after restore(); whole-page pixel comparison guards this.
    painter->drawRect(QRectF(-20000 - id, begin ? -20000 : -20001, 0.125, 0.25));
}

#ifdef Q_OS_WIN
void registerWindowsReportFonts()
{
    if (!qEnvironmentVariable("QT_QPA_FONTDIR").isEmpty())
    {
        // Retain an explicit Qt offscreen font directory for controlled
        // same-font comparisons; the normal Windows launch needs no env var.
        qInfo() << "HISTORY_REPORT_FONT_SOURCE explicit_qpa_directory";
        return;
    }
    // Qt 5.14's offscreen platform does not discover the Windows font folder.
    // Register the existing template font files for this process only: never
    // install fonts, change the system, or copy Windows font files into Git.
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::FontsLocation);
    const QStringList fileNames{QStringLiteral("simhei.ttf"), QStringLiteral("simsun.ttc")};
    QSet<QString> loadedPaths;
    for (const QString &location : locations)
        for (const QString &name : fileNames)
        {
            const QFileInfo file(QDir(location).filePath(name));
            if (!file.isFile() || loadedPaths.contains(file.canonicalFilePath())) continue;
            loadedPaths.insert(file.canonicalFilePath());
            const int identifier = QFontDatabase::addApplicationFont(file.absoluteFilePath());
            if (identifier < 0)
                qWarning().noquote() << "HISTORY_REPORT_FONT_LOAD_FAILED" << name;
            else
                qInfo().noquote() << "HISTORY_REPORT_FONT_REGISTERED"
                                 << QFontDatabase::applicationFontFamilies(identifier).join(QChar(','));
        }
    // Missing files/faces are handled by prepareHistoryReport's explicit font
    // and per-code-point checks, including any caller-provided Noto font path.
}
#endif

bool readSnapshot(const QString &path, HistoryPrintDocument *document, QString *error)
{
    const QFileInfo information(path);
    if (!QDir::isAbsolutePath(path) || !information.isFile() || information.isSymLink()
        || information.size() <= 0 || information.size() > kMaximumSnapshotBytes)
    {
        *error = QStringLiteral("渲染快照必须是存在的绝对路径普通文件，大小为1至1MiB，不能是符号链接。");
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = QStringLiteral("无法读取报告渲染快照：") + file.errorString();
        return false;
    }
    const QByteArray bytes = file.read(kMaximumSnapshotBytes + 1);
    if (bytes.size() > kMaximumSnapshotBytes || file.error() != QFile::NoError)
    {
        *error = QStringLiteral("报告快照读取失败或读取期间超出1MiB容量。");
        return false;
    }
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject())
    {
        *error = QStringLiteral("报告快照不是有效JSON对象：") + parse.errorString();
        return false;
    }
    return HistoryReportDocument::fromJson(json.object(), document, error);
}

bool validatePdf(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 1024 || file.size() > kMaximumPdfBytes
        || file.read(5) != QByteArrayLiteral("%PDF-"))
    {
        *error = QStringLiteral("报告未生成有效PDF文件头或文件大小无效。");
        return false;
    }
    if (!file.seek(qMax<qint64>(0, file.size() - 1024)))
    {
        *error = QStringLiteral("无法核对PDF文件尾。");
        return false;
    }
    const QByteArray tail = file.readAll();
    const int eof = tail.lastIndexOf("%%EOF");
    if (eof < 0 || !tail.mid(eof + 5).trimmed().isEmpty())
    {
        *error = QStringLiteral("PDF文件未正常结束，拒绝发布不完整报告。");
        return false;
    }
    return true;
}

bool publishPdf(const QString &temporaryPath, const QString &path, QString *error)
{
    QFile source(temporaryPath);
    if (!source.open(QIODevice::ReadOnly))
    {
        *error = QStringLiteral("无法读取已渲染PDF：") + source.errorString();
        return false;
    }
    QFile target(path);
    // NewOnly is the final publication check; a file created while rendering
    // also remains intact. Never remove a target we did not create ourselves.
    if (!target.open(QIODevice::WriteOnly | QIODevice::NewOnly))
    {
        *error = QStringLiteral("PDF输出已存在或不能新建：") + target.errorString();
        return false;
    }
    bool success = true;
    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty() || target.write(chunk) != chunk.size())
        {
            success = false;
            break;
        }
    }
    success = success && source.error() == QFile::NoError && target.flush();
    target.close();
    if (!success)
    {
        *error = QStringLiteral("发布PDF写入失败，已清理本进程新建的残缺文件。");
        target.remove();
    }
    return success;
}

int fail(int code, const QString &message)
{
    qCritical().noquote() << "HISTORY_RENDER_ERROR" << message;
    return code;
}
}

int main(int argc, char **argv)
{
    // No desktop preview, physical printer dialog, SQL connection, network or
    // device operation is created by this independent value-snapshot process.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
#ifdef Q_OS_WIN
    // This child has redirected channels and no console window. Keep its real
    // validation/rendering errors visible to the controller instead of sending
    // them only to the Windows debugger output channel.
    qputenv("QT_FORCE_STDERR_LOGGING", QByteArrayLiteral("1"));
#endif
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
#ifdef Q_OS_WIN
    registerWindowsReportFonts();
    // Qt5 constructs QPrinter in NativeFormat before setOutputFormat(PdfFormat).
    // The Windows printing plugin can block in OpenPrinter on an unavailable
    // default device. This separate, label-only helper needs no external SQL,
    // image or printing plugins; QPA is already loaded. Empty process-local
    // paths make QPrinter use its built-in PDF engine without driver discovery.
    // Never apply this isolation to the main GUI, whose SQL worker needs plugins.
    QCoreApplication::setLibraryPaths(QStringList());
    qInfo() << "HISTORY_REPORT_PLUGIN_ISOLATION builtin_pdf_only"
            << QCoreApplication::libraryPaths();
#endif
    const bool htmlOutput = application.arguments().size() == 4
        && application.arguments().at(1) == QStringLiteral("--html");
    if (application.arguments().size() != 3 && !htmlOutput)
        return fail(2, QStringLiteral("Usage: sn100_report_renderer [--html] <absolute-snapshot.json> <new-absolute-output>"));
    const QString snapshotPath = application.arguments().at(htmlOutput ? 2 : 1);
    const QString outputPath = application.arguments().at(htmlOutput ? 3 : 2);
    const QFileInfo output(outputPath);
    if (!QDir::isAbsolutePath(outputPath) || output.fileName().isEmpty()
        || !QFileInfo(output.absolutePath()).isDir() || output.exists() || output.isSymLink()
        || (htmlOutput && output.suffix().compare(QStringLiteral("html"), Qt::CaseInsensitive) != 0))
        return fail(2, QStringLiteral("PDF输出必须是已有目录下尚不存在的绝对文件路径；不会覆盖已有文件。"));

    QString error;
    HistoryPrintDocument document;
    if (!readSnapshot(snapshotPath, &document, &error)) return fail(3, error);
    if (htmlOutput && !validateHistoryHtmlSource(document, &error)) return fail(4, error);
    // Original printHTML still starts a QPainter on its QPrinter even though
    // labels are emitted to HTML. Supply a private file device explicitly;
    // an unconfigured/default printer is neither required nor contacted.
    std::unique_ptr<QTemporaryDir> htmlPainterDirectory;
    std::unique_ptr<QPrinter> htmlPrinter;
    QtRPT engine;
    QString renderError;
    QObject::connect(&engine, &QtRPT::renderError, &application,
        [&renderError](const QString &message) { renderError = message; });
    QList<HistoryPdfTextSpan> spans;
    bool textOpen = false;
    QObject::connect(&engine, &QtRPT::pdfTextBegin, &application,
        [&](QPainter *painter, const QString &text, const QFont &font) {
        if (textOpen || spans.size() >= 4096)
        {
            renderError = QStringLiteral("PDF文本绘制边界重叠或超出容量，拒绝发布。");
            return;
        }
        HistoryPdfTextSpan span;
        span.id = spans.size() + 1;
        span.text = text;
        span.font = font;
        spans.append(span);
        textOpen = true;
        markPdfText(painter, span.id, true);
    }, Qt::DirectConnection);
    QObject::connect(&engine, &QtRPT::pdfTextEnd, &application, [&](QPainter *painter) {
        if (!textOpen)
        {
            renderError = QStringLiteral("PDF文本绘制边界未配对，拒绝发布。");
            return;
        }
        markPdfText(painter, spans.last().id, false);
        textOpen = false;
    }, Qt::DirectConnection);
    if (!prepareHistoryReport(&engine, document, &error)) return fail(4, error);
    if (htmlOutput)
    {
        // QtRPT uses a default QTextStream codec. Fix it in this isolated
        // process only; UTF-8 in its meta header must match the actual bytes.
        QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
        htmlPainterDirectory.reset(new QTemporaryDir(QDir(output.absolutePath()).filePath(QStringLiteral(".history-html-device-XXXXXX"))));
        if (!htmlPainterDirectory->isValid()) return fail(5, QStringLiteral("不能创建HTML绘制临时目录。"));
        htmlPrinter.reset(new QPrinter(QPrinter::ScreenResolution));
        htmlPrinter->setOutputFormat(QPrinter::PdfFormat);
        htmlPrinter->setOutputFileName(htmlPainterDirectory->filePath(QStringLiteral("drawing.pdf")));
        if (!engine.setPrinter(htmlPrinter.get())) return fail(4, QStringLiteral("不能设置HTML文件绘制设备。"));
        QTemporaryFile html(QDir(output.absolutePath()).filePath(QStringLiteral(".history-render-XXXXXX.html")));
        if (!html.open()) return fail(5, QStringLiteral("不能创建HTML临时文件。"));
        const QString temporaryHtml = html.fileName();
        html.close();
        engine.printHTML(temporaryHtml, false);
        if (!renderError.isEmpty()) return fail(4, renderError);
        if (!html.open() || html.size() > kMaximumPdfBytes
            || !validateHistoryHtml(html.readAll(), &document, &error)) return fail(4, error);
        html.close();
        if (!publishPdf(temporaryHtml, outputPath, &error)) return fail(5, error);
        qInfo().noquote() << "HISTORY_RENDER_HTML_OK rows=" << document.rows.size()
                         << "bytes=" << QFileInfo(outputPath).size();
        return 0;
    }
    QTemporaryFile temporary(QDir(output.absolutePath()).filePath(QStringLiteral(".history-render-XXXXXX.pdf")));
    temporary.setAutoRemove(true);
    if (!temporary.open()) return fail(5, QStringLiteral("无法在输出目录创建本次渲染临时文件。"));
    const QString temporaryPath = temporary.fileName();
    temporary.close();
    engine.printPDF(temporaryPath, false);
    if (!renderError.isEmpty()) return fail(4, renderError);
    if (textOpen || spans.isEmpty()) return fail(4, QStringLiteral("PDF文本绘制记录未完整结束。"));
    if (!validatePdf(temporaryPath, &error)) return fail(4, error);
    QTemporaryDir mappingDirectory(QDir(output.absolutePath()).filePath(QStringLiteral(".history-mapping-XXXXXX")));
    if (!mappingDirectory.isValid()) return fail(5, QStringLiteral("无法创建本次PDF映射临时目录。"));
    const QString mappedPath = QDir(mappingDirectory.path()).filePath(QStringLiteral("mapped.pdf"));
    if (!rewriteHistoryPdfTextMapping(temporaryPath, mappedPath, spans, &error)) return fail(4, error);
    if (!validatePdf(mappedPath, &error)) return fail(4, error);
    if (!publishPdf(mappedPath, outputPath, &error)) return fail(5, error);
    qInfo().noquote() << "HISTORY_RENDER_OK rows=" << document.rows.size()
                     << "text_spans=" << spans.size()
                     << "bytes=" << QFileInfo(outputPath).size();
    return 0;
}
