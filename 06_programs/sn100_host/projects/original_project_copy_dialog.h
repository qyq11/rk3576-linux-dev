#ifndef SN100_ORIGINAL_PROJECT_COPY_DIALOG_H
#define SN100_ORIGINAL_PROJECT_COPY_DIALOG_H

#include <QDialog>
#include <QStringList>

namespace Ui { class CopyProjectDialog; }

// 原复制卡只让使用者输入后缀，项目主体名称沿用原前缀。
class OriginalProjectCopyDialog : public QDialog
{
public:
    OriginalProjectCopyDialog(const QString &sourceName, const QStringList &existingNames, QWidget *parent = nullptr);
    ~OriginalProjectCopyDialog() override;
    QString projectName() const;
private:
    Ui::CopyProjectDialog *ui;
};

#endif
