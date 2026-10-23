#include "report_controller.h"
#include "report_render.h"
#include "qtrpt.h"
#include <QCoreApplication>
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
    : QObject(parent), m_parent(parent), m_process(new QProcess(this)), m_timeout(new QTimer(this))
{
    m_renderer = QCoreApplication::applicationDirPath() + QStringLiteral("/sn100_report_renderer");
#ifdef Q_OS_WIN
    m_renderer += QStringLiteral(".exe");
#endif
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(30000);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        m_stopReason = tr("PDF渲染超过30秒，已停止本次导出，可重试。");
        m_process->kill();
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_processOutput += QString::fromUtf8(m_process->readAllStandardError()).left(4096);
        m_processOutput = m_processOutput.right(4096);
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { m_process->readAllStandardOutput(); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishExport(false, tr("无法启动PDF渲染器：") + m_process->errorString());
    });
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus state) {
        if (!m_exporting) return;
        if (!m_stopReason.isEmpty()) { finishExport(false, m_stopReason); return; }
        if (state != QProcess::NormalExit || code != 0)
        {
            finishExport(false, tr("PDF渲染失败（退出码%1）：%2").arg(code).arg(m_processOutput));
            return;
        }
        QFile pdf(m_job->filePath(QStringLiteral("render.pdf")));
        if (!pdf.open(QIODevice::ReadOnly) || pdf.size() < 100 || pdf.size() > 32 * 1024 * 1024 || pdf.read(5) != "%PDF-")
        {
            finishExport(false, tr("渲染器没有生成有效PDF。"));
            return;
        }
        pdf.seek(qMax<qint64>(0, pdf.size() - 1024));
        const bool complete = pdf.readAll().contains("%%EOF");
        pdf.close();
        QString error;
        // Check again at publication time; a file created during rendering is
        // never overwritten, and QFile::rename also refuses an existing target.
        if (!complete || !validateDestination(m_destination, &error)
            || !QFile::rename(pdf.fileName(), m_destination))
        {
            finishExport(false, error.isEmpty() ? tr("PDF发布失败；未覆盖目标文件。") : error);
            return;
        }
        finishExport(true, QString());
    });
}

HistoryReportController::~HistoryReportController()
{
    m_timeout->stop();
    if (m_process->state() != QProcess::NotRunning)
    {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
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
    const bool busy = m_engine || m_chooser || m_exporting;
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

bool HistoryReportController::validateDestination(const QString &path, QString *error) const
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
        || output.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) != 0
        || output.fileName().startsWith('.') || QFileInfo::exists(path))
    {
        *error = tr("请选择匿名隔离目录exports下新的.pdf文件；不会覆盖已有文件。");
        return false;
    }
    return true;
}

void HistoryReportController::preview(HistoryReport report)
{
    if (m_busy) { emit failed(QStringLiteral("preview"), tr("报告预览/导出尚未结束。")); return; }
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
    connect(engine, &QtRPT::renderError, this, [this](const QString &message) { emit failed(QStringLiteral("preview"), message); });
    connect(engine, &QtRPT::previewOpened, this, [this](QPrintPreviewDialog *dialog) {
        m_preview = dialog;
        dialog->setObjectName(QStringLiteral("historyPrintPreview"));
        emit previewReady(dialog);
    });
    connect(engine, &QtRPT::offlineExportRequested, this, [this, document](const QString &format) {
        if (format == QStringLiteral("pdf")) choosePdf(document);
    });
    connect(engine, &QtRPT::previewDestroyed, this, [this, engine]() {
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
    if (m_exporting || m_chooser) return;
    const QString path = QDir(m_directory).filePath(QStringLiteral("exports/report_%1.pdf")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"))));
    auto *dialog = new QFileDialog(m_preview ? static_cast<QWidget *>(m_preview.data()) : m_parent.data(),
                                   tr("导出PDF"), path, tr("PDF文件 (*.pdf)"));
    m_chooser = dialog;
    dialog->setObjectName(QStringLiteral("historyPdfFileDialog"));
    dialog->setOption(QFileDialog::DontUseNativeDialog);
    dialog->setOption(QFileDialog::DontConfirmOverwrite);
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setDefaultSuffix(QStringLiteral("pdf"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QFileDialog::fileSelected, this, [this, document](const QString &selected) { startExport(document, selected); });
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
    if (m_exporting) { emit failed(QStringLiteral("pdf"), tr("已有PDF导出正在执行。")); return; }
    HistoryPrintDocument document;
    if (documentFor(report, &document)) startExport(document, path);
}

void HistoryReportController::startExport(const HistoryPrintDocument &document, const QString &path)
{
    QString error;
    if (m_exporting || !validateDestination(path, &error))
    {
        emit failed(QStringLiteral("pdf"), error.isEmpty() ? tr("已有PDF导出正在执行。") : error);
        return;
    }
    if (!QDir::isAbsolutePath(m_renderer) || !QFileInfo(m_renderer).isExecutable())
    {
        emit failed(QStringLiteral("pdf"), tr("PDF渲染器缺失或不可执行：") + m_renderer);
        return;
    }
    m_job.reset(new QTemporaryDir(QDir(m_directory).filePath(QStringLiteral("exports/.report-job-XXXXXX"))));
    if (!m_job->isValid()) { m_job.reset(); emit failed(QStringLiteral("pdf"), tr("不能创建PDF临时任务目录。")); return; }
    const QByteArray json = QJsonDocument(HistoryReportDocument::toJson(document)).toJson(QJsonDocument::Compact);
    QFile snapshot(m_job->filePath(QStringLiteral("snapshot.json")));
    if (json.size() > 1024 * 1024 || !snapshot.open(QIODevice::WriteOnly | QIODevice::NewOnly) || snapshot.write(json) != json.size())
    {
        snapshot.close();
        m_job.reset();
        emit failed(QStringLiteral("pdf"), tr("不能保存完整的报告快照。"));
        return;
    }
    snapshot.close();
    m_destination = QDir::cleanPath(path);
    m_rows = document.rows.size();
    m_processOutput.clear();
    m_stopReason.clear();
    m_exporting = true;
    updateBusy();
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    m_process->setProcessEnvironment(environment);
    m_process->setWorkingDirectory(m_job->path());
    m_process->start(m_renderer, {snapshot.fileName(), m_job->filePath(QStringLiteral("render.pdf"))});
    m_timeout->start();
}

void HistoryReportController::finishExport(bool success, const QString &message)
{
    if (!m_exporting) return;
    m_timeout->stop();
    m_exporting = false;
    const QString path = m_destination;
    const int rows = m_rows;
    m_job.reset();
    updateBusy();
    if (success) emit pdfExported(path, rows);
    else emit failed(QStringLiteral("pdf"), message);
}
