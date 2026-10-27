#ifndef HISTORY_PAGE_H
#define HISTORY_PAGE_H

#include "history_store.h"
#include "../data/hospital_dictionary_query.h"
#include <QPointer>
#include <QWidget>

class QDialog;
class ReportRemainingDisplayDialog;
class ReportDictionaryDialog;
class QLineEdit;
class QShowEvent;
class HistoryCurrentDisplayState;
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
    void setLocalResultsMode(bool enabled);
    // Root binds the project editor to this actual GUI-owned cache; no SQL or
    // replacement state is created by this getter. Legacy/closed pages return null.
    HistoryCurrentDisplayState *currentDisplayState() const;
    void setReportOperationBusy(bool busy);
    // 根窗口在独立字典查询通道就绪/关闭时设置；默认不假设后端已接通。
    void setHospitalDictionaryAvailable(bool available);
    void setSession(AccountSession session);
    // 先拒绝初始化/在途操作；未保存默认保留，确认放弃只恢复内存快照。
    bool requestLeave(const QString &action);
    bool isReady() const { return m_ready; }
    bool isBusy() const { return m_busy || m_reportBusy || m_dictionaryBusy; }
    bool hasUnsavedChanges() const;
    HistoryFilter currentFilter() const { return m_filter; }

public slots:
    void receiveHospitalDictionary(HospitalDictionaryReply reply);
    // Invalidate first; the writer owner calls refresh() after its actual ACK.
    void invalidateCurrentResultFormat(QString reason);
    void refresh();
    void exportCsv(QString path, bool all = true);

signals:
    void requestHospitalDictionary(HospitalDictionaryRequest request);
    void requestQuery(qint64 requestId, HistoryFilter filter);
    void requestDetail(qint64 requestId, qint64 reportId);
    void requestSavePatient(qint64 requestId, HistoryPatient patient);
    void requestSetSession(AccountSession session);
    void requestHideReports(qint64 requestId, HistoryIds reportIds);
    void requestLoadDisplay(qint64 requestId);
    void requestSaveDisplay(qint64 requestId, HistoryDisplayColumns columns);
    void requestExportCsv(qint64 requestId, HistoryFilter filter,
                          HistoryIds selectedIds, QString path);
    void operationFinished(QString operation, bool success);
    void statusChanged(QString message);
    void reportPreviewRequested(HistoryReport report);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

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
    void hideSelectedReports();
    void openDisplay();
    void applyDisplay();
    void bindHospitalDictionary(QLineEdit *edit, const QString &type);
    void openHospitalDictionary(QLineEdit *edit, const QString &type);
    void queueHospitalDictionary(HospitalDictionaryRequest request);
    void sendPendingHospitalDictionary();
    void clearCurrentResultPresentation(const QString &reason);
    bool currentResultPresentationValid(bool detail) const;
    void scheduleCurrentResultRefresh();
    void fillCurrentDetailResults(const HistoryReport &report);

    Ui::frmReportMaterial *ui;
    Ui::MaskDlgReportDetail *m_detailUi = nullptr;
    QPointer<QDialog> m_detailDialog;
    QPointer<QDialog> m_queryDialog;
    QPointer<QDialog> m_exportDialog;
    QPointer<QDialog> m_exportFileDialog;
    QPointer<ReportRemainingDisplayDialog> m_displayDialog;
    QPointer<ReportDictionaryDialog> m_dictionaryDialog;
    HospitalDictionaryRequest m_dictionaryFlight;
    HospitalDictionaryRequest m_dictionaryWaiting;
    bool m_dictionaryAvailable = false;
    bool m_dictionaryBusy = false;
    HistoryDisplayColumns m_displayColumns;
    HistoryDisplayColumns m_displayDefaults;
    bool m_canWriteRemaining = false;
    HistoryFilter m_filter;
    HistoryReports m_reports;
    HistoryReport m_detailReport;
    QPointer<HistoryCurrentDisplayState> m_currentDisplayState;
    HistoryCurrentFormatSource m_currentSource;
    HistoryFilter m_currentPendingFilter;
    HistoryDisplayColumns m_currentPendingColumns;
    HistoryDisplayColumns m_currentPendingDefaults;
    qint64 m_currentColumnsRequest = 0;
    qint64 m_currentDetailRequestReport = 0;
    bool m_currentQueryValid = false;
    bool m_currentDetailValid = false;
    bool m_currentRefreshPending = false;
    bool m_currentRefreshScheduled = false;
    bool m_currentClosing = false;
    bool m_currentLeaving = false;
    QString m_directory;
    qint64 m_total = 0;
    qint64 m_serial = 0;
    qint64 m_pending = 0;
    QString m_operation;
    bool m_ready = false;
    bool m_busy = false;
    bool m_reportBusy = false;
    bool m_reportPreviewEnabled = false;
    bool m_localResultsMode = false;
    bool m_dirty = false;
    bool m_filling = false;
    // Original SystemCommon::isOpenAverage defaults to true. The independent
    // slice does not read the reference application's runtime configuration.
    bool m_averageEnabled = true;
};
#endif
