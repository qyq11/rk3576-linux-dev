#ifndef HISTORY_REPORT_CONTROLLER_H
#define HISTORY_REPORT_CONTROLLER_H

#include "report_document.h"
#include <QObject>
#include <QPointer>
#include <memory>

class QWidget;
class QPrintPreviewDialog;
class QFileDialog;
class QProcess;
class QTemporaryDir;
class QTimer;
class QtRPT;

// Owns one bounded preview/export operation. The page supplies value snapshots;
// rendering never reads the live database or changes patient information.
class HistoryReportController : public QObject
{
    Q_OBJECT
public:
    explicit HistoryReportController(QWidget *parent = nullptr);
    ~HistoryReportController() override;
    void setDataDirectory(const QString &directory);
    void setRendererPath(const QString &path);
    // Optional committed configuration snapshot supplied by the unified shell.
    // Independent history keeps its existing anonymous fixture by default.
    void setPrintOptions(const HistoryPrintOptions &options);

public slots:
    void preview(HistoryReport report);
    void exportPdf(HistoryReport report, QString path);
    void exportHtml(HistoryReport report, QString path);

signals:
    void previewReady(QPrintPreviewDialog *preview);
    void previewClosed();
    void pdfExported(QString path, int rowCount);
    void htmlExported(QString path, int rowCount);
    void failed(QString operation, QString message);
    void busyChanged(bool busy);

private:
    bool documentFor(const HistoryReport &report, HistoryPrintDocument *document);
    bool validateDestination(const QString &path, QString *error, const QString &format = QStringLiteral("pdf")) const;
    void choosePdf(const HistoryPrintDocument &document);
    void chooseExport(const HistoryPrintDocument &document, const QString &format);
    void startExport(const HistoryPrintDocument &document, const QString &path, const QString &format = QStringLiteral("pdf"));
    void finishExport(bool success, const QString &message);
    void updateBusy();

    QPointer<QWidget> m_parent;
    QPointer<QtRPT> m_engine;
    QPointer<QPrintPreviewDialog> m_preview;
    QPointer<QFileDialog> m_chooser;
    QProcess *m_process;
    QTimer *m_timeout;
    std::unique_ptr<QTemporaryDir> m_job;
    QString m_directory;
    QString m_renderer;
    QString m_destination;
    QString m_format = QStringLiteral("pdf");
    QString m_processOutput;
    QString m_stopReason;
    int m_rows = 0;
    bool m_exporting = false;
    bool m_busy = false;
    HistoryPrintOptions m_options;
    bool m_hasOptions = false;
};
#endif
