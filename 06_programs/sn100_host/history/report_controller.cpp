#include "report_controller.h"
#include "report_render.h"
#include "qtrpt.h"
#include "report_remaining_html.h"
#include <QAction>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPrintPreviewDialog>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonObject>
#include <QJsonParseError>
#include <QUuid>
#include <QDebug>
#include <QSignalBlocker>

namespace
{
bool hasUnsafeComponent(const QString &path)
{
    QString current = QDir::cleanPath(path);
    while (!current.isEmpty())
    {
        const QFileInfo info(current);
        if (info.isSymLink() || info.fileName().compare(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive) == 0)
            return true;
        const QString parent = info.dir().absolutePath();
        if (parent == current) break;
        current = parent;
    }
    return false;
}
}

HistoryReportController::HistoryReportController(QWidget *parent)
    : QObject(parent), m_parent(parent), m_process(new QProcess(this)), m_timeout(new QTimer(this)),
      m_nativeProcess(new QProcess(this)), m_nativeTimeout(new QTimer(this))
{
    m_renderer = QCoreApplication::applicationDirPath() + QStringLiteral("/sn100_report_renderer");
#ifdef Q_OS_WIN
    m_renderer += QStringLiteral(".exe");
#endif
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(30000);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        m_stopReason = tr("报告渲染超过30秒，已停止本次导出，可重试。");
        m_process->kill();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_processOutput += QString::fromUtf8(m_process->readAllStandardError()).left(4096);
        m_processOutput = m_processOutput.right(4096);
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { m_process->readAllStandardOutput(); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishExport(false, tr("无法启动报告渲染器：") + m_process->errorString());
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus state) {
        if (!m_exporting) return;
        if (!m_stopReason.isEmpty()) { finishExport(false, m_stopReason); return; }
        if (state != QProcess::NormalExit || code != 0)
        {
            finishExport(false, tr("报告渲染失败（退出码%1）：%2").arg(code).arg(m_processOutput));
            return;
        }
        QFile output(m_job->filePath(QStringLiteral("render.") + m_format));
        if (!output.open(QIODevice::ReadOnly) || output.size() < 100 || output.size() > 32 * 1024 * 1024)
        {
            finishExport(false, tr("渲染器没有生成有效报告文件。"));
            return;
        }
        QString error;
        bool complete = false;
        if (m_format == QStringLiteral("html"))
            complete = validateHistoryHtml(output.readAll(), nullptr, &error);
        else
        {
            complete = output.read(5) == "%PDF-";
            output.seek(qMax<qint64>(0, output.size() - 1024));
            complete = complete && output.readAll().contains("%%EOF");
        }
        output.close();
        // Check again at publication time; a file created during rendering is
        // never overwritten, and QFile::rename also refuses an existing target.
        if (!complete || !validateDestination(m_destination, &error, m_format)
            || !QFile::rename(output.fileName(), m_destination))
        {
            finishExport(false, error.isEmpty() ? tr("报告发布失败；未覆盖目标文件。") : error);
            return;
        }
        finishExport(true, QString());
    });
    // 确认等待没有文件导出的30秒时限。只从收到submit_started开始独立计时。
    m_nativeTimeout->setSingleShot(true);
    m_nativeTimeout->setInterval(30000); // 本地开发watchdog，未认可为设备响应预算。
    connect(m_nativeTimeout, &QTimer::timeout, this, [this]() {
        stopNativePrint(tr("打印提交超过本地开发30秒观察时限，结局不明；不会自动重试。"));
    });
    connect(m_nativeProcess, &QProcess::readyReadStandardOutput, this, &HistoryReportController::readNativeMessages);
    connect(m_nativeProcess, &QProcess::readyReadStandardError, this, [this]() {
        const QByteArray bytes = m_nativeProcess->readAllStandardError();
        if (!m_nativePrinting) return;
        m_nativeDiagnostics += QString::fromUtf8(bytes).left(4096);
        m_nativeDiagnostics = m_nativeDiagnostics.right(4096);
    });
    connect(m_nativeProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!m_nativePrinting) return;
        if (error == QProcess::FailedToStart)
        {
            if (!m_nativeStopReason.isEmpty() || m_closing)
                finishNativePrint(QStringLiteral("Unknown"), m_nativeStopReason.isEmpty()
                                  ? tr("任务被强制关闭，结局不明；不会自动重试。") : m_nativeStopReason);
            else
                finishNativePrint(QStringLiteral("Failed"), tr("未能启动独立打印确认，未执行绘制：") + m_nativeProcess->errorString());
            return;
        }
        if (m_nativeStopReason.isEmpty())
            m_nativeStopReason = tr("独立打印进程异常，提交结局不明；不会自动重试：") + m_nativeProcess->errorString();
        m_nativeTimeout->stop();
        // Crashed会随后发finished；不在error中先释放并允许重入启动下一请求。
        if (m_nativeProcess->state() != QProcess::NotRunning) m_nativeProcess->kill();
    });
    connect(m_nativeProcess, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus state) { nativeProcessFinished(code, state == QProcess::NormalExit); });
}

HistoryReportController::~HistoryReportController()
{
    // 正常关闭先调用shutdown并收真实进程结局。析构兜底不能回调已拆除的父UI。
    const QSignalBlocker noLateUiCallbacks(this);
    if (m_nativePrinting) qWarning().noquote() << "NATIVE_PRINT_UNKNOWN_ON_DESTRUCTION" << m_nativeToken;
    shutdown();
    m_timeout->stop();
    if (m_process->state() != QProcess::NotRunning)
    {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    m_nativeProcess->disconnect(this);
    if (m_nativeProcess->state() != QProcess::NotRunning)
    {
        m_nativeProcess->kill();
        m_nativeProcess->waitForFinished(2000);
    }
    if (m_nativePrinting)
        finishNativePrint(QStringLiteral("Unknown"), tr("控制器关闭，打印结局不明；不会自动重试。"));
    if (m_chooser) delete m_chooser.data();
    if (m_preview) delete m_preview.data();
    if (m_engine) delete m_engine.data();
}

void HistoryReportController::setDataDirectory(const QString &directory) { m_directory = directory; }
void HistoryReportController::setRendererPath(const QString &path) { m_renderer = path; }

void HistoryReportController::setPrintOptions(const HistoryPrintOptions &options)
{
    // Existing previews/exports already own a HistoryPrintDocument copy.
    // Updating configuration affects only the next requested report.
    m_options = options;
    m_hasOptions = true;
}

void HistoryReportController::updateBusy()
{
    const bool busy = m_engine || m_chooser || m_exporting || m_nativePrinting;
    if (busy == m_busy) return;
    m_busy = busy;
    emit busyChanged(busy);
}

bool HistoryReportController::documentFor(const HistoryReport &report, HistoryPrintDocument *document)
{
    HistoryPrintOptions options;
    QString error;
    if (m_hasOptions) options = m_options;
    else if (!HistoryReportDocument::anonymousOptions(&options, &error))
    {
        emit failed(QStringLiteral("report"), error);
        return false;
    }
    if (options.hospitalName.trimmed().isEmpty())
    {
        emit failed(QStringLiteral("report"), tr("请先在医院信息设置中保存医院名称，再预览报告。"));
        return false;
    }
    if (!HistoryReportDocument::build(report, options, document, &error))
    {
        emit failed(QStringLiteral("report"), error);
        return false;
    }
    return true;
}

bool HistoryReportController::validateDestination(const QString &path, QString *error, const QString &format) const
{
    const QDir directory(QDir::cleanPath(m_directory));
    QFile marker(directory.filePath(QStringLiteral(".sn100-history-offline")));
    const QString exportPath = directory.filePath(QStringLiteral("exports"));
    const QFileInfo output(path);
    if (!QDir::isAbsolutePath(m_directory) || hasUnsafeComponent(m_directory)
        || !marker.open(QIODevice::ReadOnly) || marker.readAll() != "SN100 O04 ANONYMOUS OFFLINE DATA v1\n"
        || QFileInfo(marker.fileName()).isSymLink() || hasUnsafeComponent(exportPath)
        || !QFileInfo(exportPath).isDir() || !QDir::isAbsolutePath(path) || hasUnsafeComponent(path)
        || output.dir().canonicalPath() != QFileInfo(exportPath).canonicalFilePath()
        || (format != QStringLiteral("pdf") && format != QStringLiteral("html"))
        || output.suffix().compare(format, Qt::CaseInsensitive) != 0
        || output.fileName().startsWith('.') || QFileInfo::exists(path))
    {
        *error = tr("请选择匿名隔离目录exports下新的.%1文件；不会覆盖已有文件。").arg(format);
        return false;
    }
    return true;
}

void HistoryReportController::preview(HistoryReport report)
{
    if (m_closing || m_busy) { emit failed(QStringLiteral("preview"), tr("报告尚未结束或正在关闭。")); return; }
    HistoryPrintDocument document;
    if (!documentFor(report, &document)) return;
    auto *engine = new QtRPT(m_parent);
    m_engine = engine;
    QString error;
    if (!prepareHistoryReport(engine, document, &error))
    {
        m_engine.clear();
        delete engine;
        emit failed(QStringLiteral("preview"), error);
        return;
    }
    engine->setOfflinePreview(true);
    const QString reportPrinter = m_committedPrinter;
    const quint64 printerGeneration = m_printerGeneration;
    m_previewPrinterGeneration = printerGeneration;
    m_previewPrinterBound = m_printerBound;
    engine->setControlledNativePrintEnabled(m_previewPrinterBound);
    connect(engine, &QtRPT::controlledNativePrintRequested, this, [this, document, reportPrinter, printerGeneration]() {
        startNativePrint(document, reportPrinter, printerGeneration);
    });
    connect(engine, &QtRPT::renderError, this, [this](const QString &message) { emit failed(QStringLiteral("preview"), message); });
    connect(engine, &QtRPT::previewOpened, this, [this, document](QPrintPreviewDialog *dialog) {
        m_preview = dialog;
        dialog->setObjectName(QStringLiteral("historyPrintPreview"));
        // 接管原工具栏动作：保留图标/位置，由相同快照的受控helper导出，
        // 不走原exportTo的同步写入或自动打开浏览器。
        if (auto *html = dialog->findChild<QAction *>(QStringLiteral("actExpToHtml")))
        {
            QObject::disconnect(html, nullptr, m_engine.data(), nullptr);
            html->setEnabled(true);
            html->setToolTip(tr("按原模板导出UTF-8 HTML文件"));
            connect(html, &QAction::triggered, this, [this, document]() { chooseExport(document, QStringLiteral("html")); });
        }
        emit previewReady(dialog);
    });
    connect(engine, &QtRPT::offlineExportRequested, this, [this, document](const QString &format) {
        if (format == QStringLiteral("pdf")) choosePdf(document);
    });
    connect(engine, &QtRPT::previewDestroyed, this, [this, engine]() {
        if (m_nativePrinting) stopNativePrint(tr("报告预览已关闭，独立打印任务已停止，结局不明；不会自动重试。"));
        m_preview.clear();
        m_engine.clear();
        engine->deleteLater();
        updateBusy();
        emit previewClosed();
    });
    updateBusy();
    engine->printExec(true, false);
}

void HistoryReportController::choosePdf(const HistoryPrintDocument &document)
{
    chooseExport(document, QStringLiteral("pdf"));
}

void HistoryReportController::chooseExport(const HistoryPrintDocument &document, const QString &format)
{
    if (m_closing || m_exporting || m_nativePrinting || m_chooser) return;
    const QString path = QDir(m_directory).filePath(QStringLiteral("exports/report_%1.%2")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")), format));
    auto *dialog = new QFileDialog(m_preview ? static_cast<QWidget *>(m_preview.data()) : m_parent.data(),
                                   tr("导出%1").arg(format.toUpper()), path,
                                   format == QStringLiteral("html") ? tr("HTML文件 (*.html)") : tr("PDF文件 (*.pdf)"));
    m_chooser = dialog;
    dialog->setObjectName(format == QStringLiteral("html") ? QStringLiteral("historyHtmlFileDialog") : QStringLiteral("historyPdfFileDialog"));
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setOption(QFileDialog::DontConfirmOverwrite);
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setDefaultSuffix(format);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QFileDialog::fileSelected, this, [this, document, format](const QString &selected) { startExport(document, selected, format); });
    // Release the active chooser when it finishes, before deferred deletion;
    // an immediate retry after Cancel must be able to open a new chooser.
    const auto releaseChooser = [this, dialog]() {
        if (m_chooser == dialog) m_chooser.clear();
        updateBusy();
    };
    connect(dialog, &QDialog::finished, this, [releaseChooser](int) { releaseChooser(); });
    connect(dialog, &QObject::destroyed, this, releaseChooser);
    updateBusy();
    dialog->open();
}

void HistoryReportController::exportPdf(HistoryReport report, QString path)
{
    if (m_closing || m_nativePrinting || m_exporting) { emit failed(QStringLiteral("pdf"), tr("报告任务未结束或正在关闭。")); return; }
    HistoryPrintDocument document;
    if (documentFor(report, &document)) startExport(document, path);
}

void HistoryReportController::exportHtml(HistoryReport report, QString path)
{
    if (m_closing || m_nativePrinting || m_exporting) { emit failed(QStringLiteral("html"), tr("报告任务未结束或正在关闭。")); return; }
    HistoryPrintDocument document;
    if (documentFor(report, &document)) startExport(document, path, QStringLiteral("html"));
}

void HistoryReportController::startExport(const HistoryPrintDocument &document, const QString &path, const QString &format)
{
    QString error;
    if (m_closing || m_nativePrinting || m_exporting || !validateDestination(path, &error, format))
    {
        emit failed(format, error.isEmpty() ? tr("已有报告导出正在执行。") : error);
        return;
    }
    if (!QDir::isAbsolutePath(m_renderer) || !QFileInfo(m_renderer).isExecutable())
    {
        emit failed(format, tr("报告渲染器缺失或不可执行：") + m_renderer);
        return;
    }
    m_job.reset(new QTemporaryDir(QDir(m_directory).filePath(QStringLiteral("exports/.report-job-XXXXXX"))));
    if (!m_job->isValid()) { m_job.reset(); emit failed(format, tr("不能创建报告临时任务目录。")); return; }
    const QByteArray json = QJsonDocument(HistoryReportDocument::toJson(document)).toJson(QJsonDocument::Compact);
    QFile snapshot(m_job->filePath(QStringLiteral("snapshot.json")));
    if (json.size() > 1024 * 1024 || !snapshot.open(QIODevice::WriteOnly | QIODevice::NewOnly) || snapshot.write(json) != json.size())
    {
        snapshot.close();
        m_job.reset();
        emit failed(format, tr("不能保存完整的报告快照。"));
        return;
    }
    snapshot.close();
    m_destination = QDir::cleanPath(path);
    m_format = format;
    m_rows = document.rows.size();
    m_processOutput.clear();
    m_stopReason.clear();
    m_exporting = true;
    updateBusy();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    m_process->setProcessEnvironment(environment);
    m_process->setWorkingDirectory(m_job->path());
    QStringList arguments{snapshot.fileName(), m_job->filePath(QStringLiteral("render.") + format)};
    if (format == QStringLiteral("html")) arguments.prepend(QStringLiteral("--html"));
    m_process->start(m_renderer, arguments);
    m_timeout->start();
}

void HistoryReportController::finishExport(bool success, const QString &message)
{
    if (!m_exporting) return;
    m_timeout->stop();
    m_exporting = false;
    const QString path = m_destination;
    const int rows = m_rows;
    const QString format = m_format;
    m_job.reset();
    updateBusy();
    if (success)
    {
        if (format == QStringLiteral("html")) emit htmlExported(path, rows);
        else emit pdfExported(path, rows);
    }
    else emit failed(format, message);
}

void HistoryReportController::setCommittedPrinter(QString name)
{
    if (m_closing) return;
    m_committedPrinter = name;
    m_printerBound = true;
    // 不修改已打开预览的捕获值；普通saved也不改变其binding代次。
}

void HistoryReportController::invalidatePrinterBinding()
{
    m_printerBound = false;
    m_committedPrinter.clear();
    ++m_printerGeneration;
    if (m_engine) m_engine->setControlledNativePrintEnabled(false);
    if (m_nativePrinting) stopNativePrint(tr("打印设置绑定失效，已停止当前独立任务；结局不明。"));
}

void HistoryReportController::shutdown()
{
    if (m_closing) return;
    m_closing = true;
    invalidatePrinterBinding();
    if (m_chooser) m_chooser->close();
    if (m_preview) m_preview->close();
    if (m_exporting && m_process->state() != QProcess::NotRunning)
    {
        m_stopReason = tr("控制器关闭，本次文件导出已停止。");
        m_process->kill();
    }
}

void HistoryReportController::startNativePrint(const HistoryPrintDocument &document, const QString &printerName,
                                               quint64 generation)
{
    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto reject = [this, token, printerName](const QString &message) {
        emit nativePrintFinished(token, QStringLiteral("Failed"), printerName, message);
        emit failed(QStringLiteral("native_print"), message);
    };
    if (m_closing || m_nativePrinting || m_exporting || m_chooser)
    { reject(tr("报告任务繁忙或正在关闭，未启动打印。")); return; }
    if (!m_printerBound || !m_previewPrinterBound || generation != m_printerGeneration)
    { reject(tr("本份报告的打印设置绑定已失效，请重新打开报告。")); return; }
    if (printerName.trimmed().isEmpty() || printerName.size() > 1024 || printerName.contains(QChar::Null))
    { reject(tr("本份报告没有有效的已保存打印机名称，未使用系统默认设备。")); return; }
    QString error;
    // 复用已有匿名运行根检查，不创建或发布这个哨兵PDF文件。
    const QString sentinel = QDir(m_directory).filePath("exports/native_" + token + ".pdf");
    if (!validateDestination(sentinel, &error) || !QDir::isAbsolutePath(m_renderer) || !QFileInfo(m_renderer).isExecutable())
    { reject(error.isEmpty() ? tr("独立报告任务入口缺失或不可执行。") : error); return; }
    m_nativeJob.reset(new QTemporaryDir(QDir(m_directory).filePath(QStringLiteral("exports/.native-print-XXXXXX"))));
    if (!m_nativeJob->isValid())
    { m_nativeJob.reset(); reject(tr("无法创建本次独立打印目录。")); return; }
    const QByteArray bytes = QJsonDocument(HistoryReportDocument::toJson(document)).toJson(QJsonDocument::Compact);
    QFile snapshot(m_nativeJob->filePath(QStringLiteral("snapshot.json")));
    if (bytes.size() > 1024 * 1024 || !snapshot.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
        snapshot.write(bytes) != bytes.size() || !snapshot.flush())
    { snapshot.close(); m_nativeJob.reset(); reject(tr("无法保存完整报告值快照，未启动打印。")); return; }
    snapshot.close();
    m_nativeToken = token;
    m_nativeRequestedPrinter = printerName;
    m_nativeActualPrinter = printerName;
    m_nativeBuffer.clear(); m_nativeBytes = 0;
    m_nativeConfirming = false; m_nativeSubmitStarted = false;
    m_nativeHasTerminal = false; m_nativeProtocolInvalid = false;
    m_nativeTerminal.clear(); m_nativeMessage.clear(); m_nativeStopReason.clear(); m_nativeDiagnostics.clear();
    m_nativePrinting = true;
    if (m_engine) m_engine->setControlledNativePrintEnabled(false);
    updateBusy();
    // Native确认需要正常GUI/PrintSupport环境；不复制PDF任务强制offscreen设置。
    QProcessEnvironment nativeEnvironment = QProcessEnvironment::systemEnvironment();
    if (nativeEnvironment.value(QStringLiteral("QT_QPA_PLATFORM")).isEmpty())
        nativeEnvironment.insert(QStringLiteral("QT_QPA_PLATFORM"), QGuiApplication::platformName());
    m_nativeProcess->setProcessEnvironment(nativeEnvironment);
    m_nativeProcess->setWorkingDirectory(m_nativeJob->path());
    // 防止合法设备名恰为Qt保留命令行选项时被QApplication吞掉；只是UTF-8传值。
    const QString encodedName = QStringLiteral("name64:") + QString::fromLatin1(printerName.toUtf8().toBase64());
    m_nativeProcess->start(m_renderer, {QStringLiteral("--native-print"), token, encodedName, snapshot.fileName()});
}

void HistoryReportController::readNativeMessages()
{
    const QByteArray fresh = m_nativeProcess->readAllStandardOutput();
    if (!m_nativePrinting) return;
    m_nativeBytes += fresh.size();
    m_nativeBuffer += fresh;
    const auto invalid = [this]() {
        m_nativeProtocolInvalid = true;
        stopNativePrint(tr("独立打印消息不完整或不匹配，提交结局不明；不会自动重试。"));
    };
    if (m_nativeBytes > 65536 || m_nativeBuffer.size() > 65536) { invalid(); return; }
    int end = -1;
    while ((end = m_nativeBuffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = m_nativeBuffer.left(end);
        m_nativeBuffer.remove(0, end + 1);
        if (line.isEmpty() || line.size() > 16384 || m_nativeHasTerminal) { invalid(); return; }
        QJsonParseError parse;
        const QJsonDocument json = QJsonDocument::fromJson(line, &parse);
        if (parse.error != QJsonParseError::NoError || !json.isObject()) { invalid(); return; }
        const QJsonObject object = json.object();
        const QStringList expectedKeys{QStringLiteral("protocol"), QStringLiteral("token"), QStringLiteral("kind"),
            QStringLiteral("value"), QStringLiteral("printer"), QStringLiteral("message")};
        if (object.size() != expectedKeys.size()) { invalid(); return; }
        for (const QString &key : expectedKeys)
            if (!object.contains(key) || !object.value(key).isString()) { invalid(); return; }
        const QString kind = object.value("kind").toString(), value = object.value("value").toString();
        const QString device = object.value("printer").toString(), message = object.value("message").toString();
        if (object.size() != 6 || object.value("protocol").toString() != "sn100-native-print-v1" ||
            object.value("token").toString() != m_nativeToken || device.size() > 1024 || device.contains(QChar::Null) ||
            message.size() > 4096 || message.contains(QChar::Null)) { invalid(); return; }
        if (kind == "phase" && value == "confirming" && !m_nativeConfirming && !m_nativeSubmitStarted &&
            device == m_nativeRequestedPrinter)
            m_nativeConfirming = true;
        else if (kind == "phase" && value == "submit_started" && m_nativeConfirming && !m_nativeSubmitStarted &&
                 !device.trimmed().isEmpty())
        {
            m_nativeSubmitStarted = true;
            m_nativeActualPrinter = device;
            m_nativeTimeout->start();
        }
        else if (kind == "terminal")
        {
            const bool allowed = value == "Unknown" ||
                (value == "Failed" && !m_nativeSubmitStarted) ||
                (value == "Cancelled" && m_nativeConfirming && !m_nativeSubmitStarted && device == m_nativeRequestedPrinter) ||
                (value == "Submitted" && m_nativeSubmitStarted && device == m_nativeActualPrinter);
            if (!allowed || (m_nativeSubmitStarted && device != m_nativeActualPrinter)) { invalid(); return; }
            m_nativeHasTerminal = true; m_nativeTerminal = value; m_nativeMessage = message;
            m_nativeActualPrinter = device;
            // 等实际退出；即使先收到Submitted，也不能取消提交阶段watchdog后冒成功。
        }
        else { invalid(); return; }
    }
}

void HistoryReportController::nativeProcessFinished(int code, bool normalExit)
{
    if (!m_nativePrinting) return;
    readNativeMessages();
    if (!m_nativePrinting) return;
    m_nativeTimeout->stop();
    int expected = -1;
    if (m_nativeTerminal == "Submitted") expected = 0;
    else if (m_nativeTerminal == "Cancelled") expected = 20;
    else if (m_nativeTerminal == "Failed") expected = 21;
    else if (m_nativeTerminal == "Unknown") expected = 22;
    if (!normalExit || m_nativeProtocolInvalid || !m_nativeStopReason.isEmpty() ||
        !m_nativeBuffer.isEmpty() || !m_nativeHasTerminal || code != expected)
    {
        finishNativePrint(QStringLiteral("Unknown"), m_nativeStopReason.isEmpty()
            ? tr("独立任务崩溃、退出或终态不一致，打印结局不明；不会自动重试。") : m_nativeStopReason);
        return;
    }
    finishNativePrint(m_nativeTerminal, m_nativeMessage);
}

void HistoryReportController::stopNativePrint(const QString &reason)
{
    if (!m_nativePrinting) return;
    if (m_nativeStopReason.isEmpty()) m_nativeStopReason = reason;
    m_nativeTimeout->stop();
    if (m_nativeProcess->state() == QProcess::NotRunning)
        finishNativePrint(QStringLiteral("Unknown"), reason);
    else
        m_nativeProcess->kill();
}

void HistoryReportController::finishNativePrint(const QString &outcome, const QString &message)
{
    if (!m_nativePrinting) return;
    m_nativeTimeout->stop();
    const QString token = m_nativeToken, device = m_nativeActualPrinter;
    const QString result = outcome; // 参数可能引用下面清理的本对象字段。
    QString detail = message;
    if ((result == "Unknown" || result == "Failed") && !m_nativeDiagnostics.trimmed().isEmpty())
        detail += tr("；任务诊断：") + m_nativeDiagnostics.right(1024);
    m_nativePrinting = false;
    m_nativeJob.reset();
    m_nativeToken.clear(); m_nativeBuffer.clear();
    m_nativeRequestedPrinter.clear(); m_nativeActualPrinter.clear();
    m_nativeTerminal.clear(); m_nativeMessage.clear(); m_nativeStopReason.clear(); m_nativeDiagnostics.clear();
    m_nativeHasTerminal = false; m_nativeConfirming = false; m_nativeSubmitStarted = false;
    m_nativeProtocolInvalid = false; m_nativeBytes = 0;
    if (m_engine) m_engine->setControlledNativePrintEnabled(!m_closing && m_printerBound && m_previewPrinterBound &&
                                                           m_previewPrinterGeneration == m_printerGeneration);
    updateBusy();
    emit nativePrintFinished(token, result, device, detail);
    if (result == "Failed" || result == "Unknown") emit failed(QStringLiteral("native_print"), detail);
}
