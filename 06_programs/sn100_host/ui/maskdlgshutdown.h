#ifndef SN100_MASKDLGSHUTDOWN_H
#define SN100_MASKDLGSHUTDOWN_H

#include <QDialog>

class QCloseEvent;
namespace Ui { class MaskDlgShutDown; }

// 原版关机卡：确认退出软件，原版隐藏的仪器关机选项继续保持隐藏。
class MaskDlgShutDown : public QDialog
{
    Q_OBJECT
public:
    explicit MaskDlgShutDown(QWidget *parent = nullptr);
    ~MaskDlgShutDown() override;
    void showDialog();
    bool isReadyForInput() const;
    void reject() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::MaskDlgShutDown *ui;
    bool closing_ = false;
    bool finishing_ = false;
    int result_ = QDialog::Rejected;
    void finishAnimated(int result);
};

#endif
