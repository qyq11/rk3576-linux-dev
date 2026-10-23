#ifndef SN100_SHELL_FRMUSERSET_H
#define SN100_SHELL_FRMUSERSET_H

#include <QPointer>
#include <QWidget>

class HospitalStore;
class AccountStore;
class frmAccountSetting;
class frmHospInfoSetting;

namespace Ui
{
class frmUserSet;
}

// 原用户设置容器；页面归容器，两个存储及工作线程归入口。
class frmUserSet : public QWidget
{
    Q_OBJECT

public:
    explicit frmUserSet(HospitalStore *store, QWidget *parent = nullptr,
                        AccountStore *accounts = nullptr);
    ~frmUserSet() override;

    frmHospInfoSetting *hospitalPage() const { return hospital_; }
    frmAccountSetting *accountPage() const { return account_; }
    int currentUserPage() const;
    bool selectUserPage(int index);
    bool requestLeave(const QString &action);
    void setNavigationLocked(bool locked) { navigationLocked_ = locked; }
    void restoreHospitalFocus();

signals:
    void pageChanged(int index);
    void navigationRejected(QString reason);

private:
    Ui::frmUserSet *ui;
    frmHospInfoSetting *hospital_;
    frmAccountSetting *account_;
    QPointer<QWidget> lastHospitalFocus_;
    bool navigationLocked_ = false;

    void rememberHospitalFocus();
};

#endif
