#ifndef SN100_ORIGINAL_REAGENT_USAGE_H
#define SN100_ORIGINAL_REAGENT_USAGE_H

#include <QByteArray>
#include <QColor>
#include <QDialog>
#include <QHash>
#include <QStringList>

class QCloseEvent;
namespace Ui
{
class MaskDlgReagentUsage;
}

// 原耗材窗口只负责显示和提交意图；实际连接、回包校验及条码写入由主程序负责。
class OriginalReagentUsageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OriginalReagentUsageDialog(QWidget *parent = nullptr);
    ~OriginalReagentUsageDialog() override;
    void setThemeStyle(const QString &styleSheet, const QColor &matchColor);

public slots:
    void openUsage();
    void setProjects(QStringList projects);
    // 仅在后端已完整校验当前连接的真实QH回复后调用；这时缺项按原版显示0次。
    void setUsage(QHash<QString, QString> projectHas, QHash<QString, QString> projectUsed);
    void setDeviceKey(QString key);
    // 后端先处理J回复及条码保存，再通知本窗口；此处不写数据库。
    void setImportResult(bool success);
    void invalidateDeviceData();
    void reject() override;

signals:
    void commandRequested(QByteArray payload);
    // 沿用原成功/失败文案，由宿主使用原消息外观异步显示；不在此处运行嵌套事件循环。
    void feedbackRequested(QString message, bool warning);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ui::MaskDlgReagentUsage *ui;
    QStringList projects_;
    QHash<QString, QString> projectHas_, projectUsed_;
    bool fromDevice_ = false;
    bool appearancePrepared_ = false, closingFinished_ = false;
    double scale_ = 1.0;
    void refreshProjectTable();
    void prepareAppearance();
};

#endif
