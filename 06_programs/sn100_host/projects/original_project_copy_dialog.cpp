#include "original_project_copy_dialog.h"
#include "ui_copyprojectdialog.h"
#include "../ui/cusmessagebox.h"

#include <QRegExpValidator>

OriginalProjectCopyDialog::OriginalProjectCopyDialog(const QString &sourceName, const QStringList &existingNames, QWidget *parent)
    : QDialog(parent), ui(new Ui::CopyProjectDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window | Qt::CustomizeWindowHint);
    ui->project_label->setText(sourceName.section(QLatin1Char('_'),0,0) + QLatin1Char('_'));
    ui->lineEdit->setMaxLength(4);
    ui->lineEdit->setValidator(new QRegExpValidator(QRegExp(QStringLiteral("^[0-9A-Za-z]{4}$")),ui->lineEdit));
    ui->lineEdit->setToolTip(QStringLiteral("只能输入字母和数字，限制4个字符长度"));
    connect(ui->btnCancel,&QPushButton::clicked,this,&QDialog::reject);
    connect(ui->btnSave,&QPushButton::clicked,this,[this,existingNames] {
        QString warning;
        if (ui->lineEdit->text().isEmpty()) warning=QStringLiteral("输入内容不能为空");
        else if (existingNames.contains(projectName(),Qt::CaseSensitive)) warning=QStringLiteral("该项目已存在");
        // 原保存回调只查非空；验证器允许输入中的1至3位前缀也能点确定。
        if (warning.isEmpty()) accept();
        else { CusMessageBox message(this);message.showWarningBox(warning); }
    });
}

OriginalProjectCopyDialog::~OriginalProjectCopyDialog() { delete ui; }
QString OriginalProjectCopyDialog::projectName() const { return ui->project_label->text()+ui->lineEdit->text(); }
