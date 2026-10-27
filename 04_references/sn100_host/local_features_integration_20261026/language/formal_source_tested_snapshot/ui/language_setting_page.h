#ifndef SN100_LANGUAGE_SETTING_PAGE_H
#define SN100_LANGUAGE_SETTING_PAGE_H

#include <QPointer>
#include <QWidget>

class CusMessageBox;
class QCloseEvent;
class QHideEvent;
namespace Ui
{
class frmLanguageSetting;
}

// 原语言页只有中文单项；应用按钮解释能力边界，不产生语言设置或保存请求。
class LanguageSettingPage : public QWidget
{
    Q_OBJECT

public:
    explicit LanguageSettingPage(QWidget *parent = nullptr);
    ~LanguageSettingPage() override;

public slots:
    // 宿主切页/注销前可调用；hide/close也会立即解除本页提示的模态状态。
    void dismissFeedback();

signals:
    void statusChanged(QString message);

protected:
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showUnavailableMessage();

private:
    Ui::frmLanguageSetting *ui_;
    QPointer<CusMessageBox> feedback_;
};

#endif
