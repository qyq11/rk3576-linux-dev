#ifndef SN100_FRMPRINTERSETTING_H
#define SN100_FRMPRINTERSETTING_H

#include <QWidget>
#include <QStringList>

class PrinterSettingsStore;
class QCloseEvent;
namespace Ui { class frmPrinterSetting; }

class frmPrinterSetting : public QWidget
{
    Q_OBJECT
public:
    explicit frmPrinterSetting(PrinterSettingsStore *store, QWidget *parent = nullptr);
    ~frmPrinterSetting() override;
    bool isReady() const { return ready_; }
    bool isBusy() const { return busy_; }
    bool isDirty() const;
    QString committedName() const { return committed_; }
    QString statusText() const { return status_; }
    bool requestLeave(const QString &action);

signals:
    void requestSave(QString name);
    void operationFinished(QString operation, bool success);
    void busyChanged(bool busy);
    void statusChanged(QString message, bool error);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void on_btnApply_clicked();

private:
    Ui::frmPrinterSetting *ui;
    QString committed_;
    QString status_;
    QString operation_ = QStringLiteral("initialize");
    bool ready_ = false;
    bool busy_ = true;
    void setBusy(bool busy);
    void render(const QString &name, const QStringList &available);
    void reportStatus(const QString &message, bool error = false);
};

#endif
