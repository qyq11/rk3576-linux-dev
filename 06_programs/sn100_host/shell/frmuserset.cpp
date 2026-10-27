#include "frmuserset.h"
#include "ui_frmuserset.h"
#include "ui/frmhospinfosetting.h"
#include "ui/frmaccountsetting.h"
#include "ui/frmprintersetting.h"
#include "ui/frmlissetting.h"
#include "ui/language_setting_page.h"

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QVBoxLayout>

frmUserSet::frmUserSet(HospitalStore *store, QWidget *parent, AccountStore *accounts) :
    QWidget(parent),
    ui(new Ui::frmUserSet),
    hospital_(nullptr),
    account_(nullptr)
{
    ui->setupUi(this);
    ui->stkUserCenter->setStyleSheet(QStringLiteral(
        ".QPushButton { background-color: rgba(42,118,242,255); color: #FFFFFF;"
        " border-radius: 4px; padding: 2px; }"
        ".QPushButton:hover { background-color: rgba(42,118,242,170); }"
        ".QPushButton:pressed { background-color: rgba(135,206,250,255); }"
        "QWidget#widget_2 { border: 1px solid #E4E8EB; border-radius: 10px; }"));

    // 有来源的原视觉键固定到本切片，不读取旧配置、会话或数据库。
    ui->usersetFunMenu->setBackgroundColor(QColor(28,28,28));
    QLinearGradient gradient(0, 0, 0, ui->usersetFunMenu->height());
    gradient.setColorAt(0, QColor(255,255,255,0));
    gradient.setColorAt(1, QColor(255,255,255,25));
    gradient.setSpread(QGradient::PadSpread);
    ui->usersetFunMenu->setGraBackgroundColor(gradient);
    ui->usersetFunMenu->setShadowEffect(Qt::AlignLeft);

    const QList<QtMaterialFlatButton *> buttons = {
        ui->btnAccount, ui->btnHospInfo, ui->btnLanguage, ui->btnPrinter, ui->btnLis
    };
    const QStringList names = {
        tr("账户设置"), tr("医院信息"), tr("语言设置"), tr("打印机设置"), tr("LIS设置")
    };
    for (int index = 0; index < buttons.size(); ++index)
    {
        QtMaterialFlatButton *button = buttons.at(index);
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setBackgroundColor(QColor(127,127,127));
        button->setUseThemeColors(false);
        button->setForegroundColor(QColor(255,255,255));
        // 原按钮不设 checkable；保留原灰底、白字和 Material 交互动画。
        connect(button, &QPushButton::clicked, this, [this, index]()
        {
            selectUserPage(index);
        });
    }
    ui->wgtUserSetBg->setRadius(10);
    ui->wgtUserSetBg->setBackgroundColor(Qt::white);

    // 使用 UI 原有的五个页位，避免 insertWidget 遗留重复空白页。
    for (int index = 0; index < 5; ++index)
    {
        QWidget *page = ui->stkUserCenter->widget(index);
        if (index == 0 && accounts)
        {
            account_ = new frmAccountSetting(accounts, ui->stkUserCenter);
            account_->setEmbeddedInShell();
            ui->stkUserCenter->removeWidget(page);
            delete page;
            ui->stkUserCenter->insertWidget(index, account_);
        }
        else if (index == 1)
        {
            hospital_ = new frmHospInfoSetting(store, ui->stkUserCenter);
            hospital_->setEmbeddedInShell();
            ui->stkUserCenter->removeWidget(page);
            delete page;
            ui->stkUserCenter->insertWidget(index, hospital_);
        }
        else
        {
            page->setObjectName(QStringLiteral("offlineUserPage%1").arg(index));
            auto *layout = new QVBoxLayout(page);
            auto *label = new QLabel(tr("%1：尚未接入（脱机）").arg(names.at(index)), page);
            label->setObjectName(QStringLiteral("offlinePlaceholderLabel"));
            label->setTextFormat(Qt::PlainText);
            label->setAlignment(Qt::AlignCenter);
            label->setEnabled(false);
            layout->addWidget(label);
            page->setProperty("offlineUnavailable", true);
        }
    }
    ui->stkUserCenter->setCurrentIndex(accounts ? 0 : 1);

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now)
    {
        if (now && hospital_->isAncestorOf(now))
        {
            lastHospitalFocus_ = now;
        }
    });
}

frmUserSet::~frmUserSet()
{
    delete ui;
}

void frmUserSet::attachPrinterPage(frmPrinterSetting *page)
{
    if (!page || printer_) return;
    const int previous = currentUserPage();
    QWidget *placeholder = ui->stkUserCenter->widget(3);
    ui->stkUserCenter->removeWidget(placeholder);
    delete placeholder;
    printer_ = page;
    ui->stkUserCenter->insertWidget(3, page);
    ui->stkUserCenter->setCurrentIndex(previous);
}

void frmUserSet::attachLisPage(frmLisSetting *page)
{
    if (!page || lis_) return;
    const int previous = currentUserPage();
    QWidget *placeholder = ui->stkUserCenter->widget(4);
    ui->stkUserCenter->removeWidget(placeholder);
    delete placeholder;
    lis_ = page;
    ui->stkUserCenter->insertWidget(4, page);
    ui->stkUserCenter->setCurrentIndex(previous);
}

void frmUserSet::attachLanguagePage(LanguageSettingPage *page)
{
    if (!page || language_) return;
    const int previous = currentUserPage();
    QWidget *placeholder = ui->stkUserCenter->widget(2);
    ui->stkUserCenter->removeWidget(placeholder);
    delete placeholder;
    language_ = page;
    ui->stkUserCenter->insertWidget(2, page);
    ui->stkUserCenter->setCurrentIndex(previous);
}

int frmUserSet::currentUserPage() const
{
    return ui->stkUserCenter->currentIndex();
}

void frmUserSet::rememberHospitalFocus()
{
    QWidget *focus = QApplication::focusWidget();
    if (focus && hospital_->isAncestorOf(focus))
    {
        lastHospitalFocus_ = focus;
    }
}

bool frmUserSet::requestLeave(const QString &action)
{
    if (language_) language_->dismissFeedback();
    rememberHospitalFocus();
    // 先检查全部隐藏页busy，再允许任何草稿放弃。
    if (lis_ && lis_->isBusy())
    {
        lis_->requestLeave(action);
        emit navigationRejected(tr("LIS设置正在处理，请稍后再%1。").arg(action));
        return false;
    }
    if (printer_ && printer_->isBusy())
    {
        printer_->requestLeave(action);
        emit navigationRejected(tr("打印设置正在处理，请稍后再%1。").arg(action));
        return false;
    }
    // Check both pages before asking to discard a hospital draft: a hidden
    // account worker must not let a rejected navigation discard that draft.
    if (account_ && account_->isBusy())
    {
        account_->requestLeave(action);
        emit navigationRejected(tr("账户数据正在处理，请稍后再%1。").arg(action));
        return false;
    }
    const bool accepted = hospital_->requestLeave(action);
    if (!accepted)
    {
        emit navigationRejected(hospital_->isBusy()
            ? tr("医院信息正在处理，请稍后再%1。").arg(action)
            : tr("已继续编辑，未%1。").arg(action));
        restoreHospitalFocus();
        return false;
    }
    if (account_ && !account_->requestLeave(action)) return false;
    if (printer_ && !printer_->requestLeave(action)) return false;
    return !lis_ || lis_->requestLeave(action);
}

bool frmUserSet::selectUserPage(int index)
{
    if (navigationLocked_)
    {
        emit navigationRejected(tr("正在注销匿名会话，请稍后再切换页面。"));
        return false;
    }
    if (index < 0 || index >= ui->stkUserCenter->count())
    {
        emit navigationRejected(tr("用户设置页面索引无效：%1").arg(index));
        return false;
    }
    if (index == currentUserPage())
    {
        return true;
    }
    if (!requestLeave(tr("切换页面")))
    {
        return false;
    }
    ui->stkUserCenter->setCurrentIndex(index);
    if (index == 1)
    {
        restoreHospitalFocus();
    }
    emit pageChanged(index);
    return true;
}

void frmUserSet::restoreHospitalFocus()
{
    // 切栈完成后恢复原编辑位置；放弃草稿重建表格时 QPointer 自动失效。
    QTimer::singleShot(0, this, [this]()
    {
        if (currentUserPage() != 1 || !hospital_->isVisible())
        {
            return;
        }
        QWidget *focus = lastHospitalFocus_.data();
        if (!focus || !focus->isVisible() || !focus->isEnabled())
        {
            focus = hospital_->findChild<QLineEdit *>(QStringLiteral("ldtHospName"));
        }
        if (focus && focus->isEnabled())
        {
            focus->setFocus(Qt::OtherFocusReason);
        }
    });
}
