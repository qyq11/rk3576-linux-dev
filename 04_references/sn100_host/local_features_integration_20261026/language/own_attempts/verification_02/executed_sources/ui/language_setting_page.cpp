#include "language_setting_page.h"
#include "cusmessagebox.h"
#include "ui_frmlanguagesetting.h"

#include <QCloseEvent>
#include <QFont>
#include <QHideEvent>
#include <QListWidget>
#include <QPushButton>

LanguageSettingPage::LanguageSettingPage(QWidget *parent) :
    QWidget(parent), ui_(new Ui::frmLanguageSetting)
{
    ui_->setupUi(this);
    setFixedSize(1451, 853);
    QString fontFamily = QStringLiteral("苹方 粗体");
#ifdef Q_OS_LINUX
    // 只替换缺失字体族；原表单的字号、粗细、坐标及边框保持。
    fontFamily = QStringLiteral("Noto Sans CJK SC");
    for (QWidget *control : QList<QWidget *>{ui_->lblHoleinfo_7, ui_->ldtLanguageSelect,
                                           ui_->btnApply, ui_->lstProject})
    {
        QFont font = control->font();
        font.setFamily(fontFamily);
        control->setFont(font);
    }
    QString editStyle = ui_->ldtLanguageSelect->styleSheet();
    editStyle.replace(QStringLiteral("苹方 粗体"), fontFamily);
    ui_->ldtLanguageSelect->setStyleSheet(editStyle);
#endif
    // 原父页按钮主题及theme/kListWidgetRowSelected的已核值；不读取旧配置。
    setStyleSheet(QStringLiteral(
        "QPushButton#btnApply { background-color: rgba(42,118,242,255); color: #FFFFFF;"
        " border-radius: 4px; padding: 2px; }"
        "QPushButton#btnApply:hover { background-color: rgba(42,118,242,170); }"
        "QPushButton#btnApply:pressed { background-color: rgba(135,206,250,255); }"));
    ui_->lstProject->setStyleSheet(QStringLiteral(
        "QListWidget::item:selected { background: rgba(183,215,255,255); color: white; }"
        "QListWidget::item { color: #444444; }"
        "QListWidget { font: bold 18px \"%1\"; background-color: transparent; }").arg(fontFamily));
    const QString chinese = QStringLiteral("Cn 中文简体");
    ui_->ldtLanguageSelect->setText(chinese);
    auto *item = new QListWidgetItem(chinese, ui_->lstProject);
    item->setSizeHint(QSize(ui_->lstProject->width() - 5, 40));
    connect(ui_->btnApply, &QPushButton::clicked, this, &LanguageSettingPage::showUnavailableMessage);
}

LanguageSettingPage::~LanguageSettingPage()
{
    // 先回收遮罩和动画，避免页面销毁后仍有提示窗口或宿主事件过滤器。
    delete feedback_.data();
    delete ui_;
}

void LanguageSettingPage::showUnavailableMessage()
{
    if (!isVisible())
    {
        return;
    }
    if (feedback_)
    {
        // 包括关闭动画/待删除期间，最多保留一个提示，不重复打开遮罩。
        if (feedback_->isVisible())
        {
            feedback_->raise();
            feedback_->activateWindow();
        }
        return;
    }
    const QString message = QStringLiteral("当前仅支持简体中文，暂不提供语言切换。");
    feedback_ = new CusMessageBox(this);
    feedback_->setObjectName(QStringLiteral("languageSettingUnavailableMessage"));
    connect(feedback_.data(), &QDialog::finished, feedback_.data(), &QObject::deleteLater);
    setProperty("lastStatus", message);
    feedback_->openWarningBox(message);
    emit statusChanged(message);
}

void LanguageSettingPage::dismissFeedback()
{
    if (feedback_ && feedback_->isVisible())
    {
        // 切页不等待关闭动画，立即隐藏模态卡；finished排队销毁原卡及动画。
        feedback_->done(QDialog::Rejected);
    }
}

void LanguageSettingPage::hideEvent(QHideEvent *event)
{
    dismissFeedback();
    QWidget::hideEvent(event);
}

void LanguageSettingPage::closeEvent(QCloseEvent *event)
{
    dismissFeedback();
    QWidget::closeEvent(event);
}
