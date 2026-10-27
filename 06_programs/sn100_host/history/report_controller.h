#ifndef HISTORY_REPORT_CONTROLLER_H
#define HISTORY_REPORT_CONTROLLER_H

#include "report_document.h"
#include <QObject>
#include <QByteArray>
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
    // Store真实loaded/saved值；只作用下一份预览。空名称不替换成默认设备。
    void setCommittedPrinter(QString name);
    void invalidatePrinterBinding();
    void shutdown();

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
    // Submitted仅代表交给本机打印系统；不是纸张完成。所有结果都带本次token。
    void nativePrintFinished(QString token, QString outcome, QString printerName, QString message);

private:
    bool documentFor(const HistoryReport &report, HistoryPrintDocument *document);
    bool validateDestination(const QString &path, QString *error, const QString &format = QStringLiteral("pdf")) const;
    void choosePdf(const HistoryPrintDocument &document);
    void chooseExport(const HistoryPrintDocument &document, const QString &format);
    void startExport(const HistoryPrintDocument &document, const QString &path, const QString &format = QStringLiteral("pdf"));
    void finishExport(bool success, const QString &message);
    void updateBusy();
    void startNativePrint(const HistoryPrintDocument &document, const QString &printerName,
                          quint64 bindingGeneration);
    void readNativeMessages();
    void nativeProcessFinished(int code, bool normalExit);
    void stopNativePrint(const QString &reason);
    void finishNativePrint(const QString &outcome, const QString &message);

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
    QProcess *m_nativeProcess;
    QTimer *m_nativeTimeout;
    std::unique_ptr<QTemporaryDir> m_nativeJob;
    QString m_committedPrinter;
    quint64 m_printerGeneration = 0; // 失效/重绑代次；普通saved不使旧预览改目标。
    bool m_printerBound = false;
    bool m_closing = false;
    bool m_nativePrinting = false;
    bool m_nativeConfirming = false;
    bool m_nativeSubmitStarted = false;
    bool m_nativeProtocolInvalid = false;
    bool m_nativeHasTerminal = false;
    QByteArray m_nativeBuffer;
    qint64 m_nativeBytes = 0;
    QString m_nativeToken;
    QString m_nativeRequestedPrinter;
    QString m_nativeActualPrinter;
    QString m_nativeTerminal;
    QString m_nativeMessage;
    QString m_nativeStopReason;
    QString m_nativeDiagnostics;
    quint64 m_previewPrinterGeneration = 0;
    bool m_previewPrinterBound = false;
};
#endif
