#ifndef SN100_PRINTER_REPORT_SHELL_H
#define SN100_PRINTER_REPORT_SHELL_H
#include <QObject>
#include <QPointer>

class PrinterSettingsStore;
class HistoryReportController;

// 在GUI线程先于Store启动创建，缓存真实提交信号；Controller稍后创建也能接收。
class PrinterReportShell : public QObject
{
    Q_OBJECT
public:
    explicit PrinterReportShell(PrinterSettingsStore *store, QObject *parent = nullptr);
    ~PrinterReportShell() override;
    void setReportController(HistoryReportController *controller);
signals:
    void applicationError(QString message);
private:
    QPointer<HistoryReportController> reports_;
    QString committed_;
    bool ready_ = false;
    void receiveCommitted(QString name);
    void invalidate();
};
#endif
