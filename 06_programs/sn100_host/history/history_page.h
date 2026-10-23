#ifndef HISTORY_PAGE_H
#define HISTORY_PAGE_H

#include "history_store.h"
#include <QPointer>
#include <QWidget>

class QDialog;
namespace Ui { class frmReportMaterial; class MaskDlgReportDetail; }

class HistoryPage : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryPage(HistoryStore *store, QWidget *parent = nullptr);
    ~HistoryPage() override;
    void setDataDirectory(const QString &directory);
    void setAverageEnabled(bool enabled);
    bool averageEnabled() const { return m_averageEnabled; }
    void setReportPreviewEnabled(bool enabled);
    void setReportOperationBusy(bool busy);
    // 先拒绝初始化/在途操作；未保存默认保留，确认放弃只恢复内存快照。
    bool requestLeave(const QString &action);
    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_busy || m_reportBusy; }
    bool hasUnsavedChanges() const { return m_dirty; }
    HistoryFilter currentFilter() const { return m_filter; }

public slots:
    void refresh();
    void exportCsv(QString path, bool all = true);

signals:
    void requestQuery(qint64 requestId, HistoryFilter filter);
    void requestDetail(qint64 requestId, qint64 reportId);
    void requestSavePatient(qint64 requestId, HistoryPatient patient);
    void requestExportCsv(qint64 requestId, HistoryFilter filter,
                          HistoryIds selectedIds, QString path);
    void operationFinished(QString operation, bool success);
    void statusChanged(QString message);
    void reportPreviewRequested(HistoryReport report);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void queryPage(int page);
    void displayReports();
    void displaySelectedResults(int row);
    void openQuery();
    void openDetail();
    void showDetail(const HistoryReport &report);
    void fillDetail(const HistoryReport &report);
    void savePatient();
    void openExport();
    void selectRow(int row);
    void moveRow(int delta);
    void setBusy(bool busy);
    void status(const QString &message);
    bool hasCurrentRow() const;
    void finish(const QString &operation, bool success);
    bool confirmLeave(const QString &action);
    void printReport(bool detail);

    Ui::frmReportMaterial *ui;
    Ui::MaskDlgReportDetail *m_detailUi = nullptr;
    QPointer<QDialog> m_detailDialog;
    QPointer<QDialog> m_queryDialog;
    QPointer<QDialog> m_exportDialog;
    QPointer<QDialog> m_exportFileDialog;
    HistoryFilter m_filter;
    HistoryReports m_reports;
    HistoryReport m_detailReport;
    QString m_directory;
    qint64 m_total = 0;
    qint64 m_serial = 0;
    qint64 m_pending = 0;
    QString m_operation;
    bool m_ready = false;
    bool m_busy = false;
    bool m_reportBusy = false;
    bool m_reportPreviewEnabled = false;
    bool m_dirty = false;
    bool m_filling = false;
    // Original SystemCommon::isOpenAverage defaults to true. The independent
    // slice does not read the reference application's runtime configuration.
    bool m_averageEnabled = true;
};
#endif
