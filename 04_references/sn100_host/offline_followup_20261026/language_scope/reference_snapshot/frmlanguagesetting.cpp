#include "frmlanguagesetting.h"
#include "ui_frmlanguagesetting.h"
#include "stylecolor.h"
#include "systemcommon.h"

frmLanguageSetting::frmLanguageSetting(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::frmLanguageSetting)
{
    ui->setupUi(this);
    refrashLanguageList();
    ui->lstProject->setStyleSheet(listWidgetStyle::getStrSheet1());
    // 获取LINEEDIT数量，并初始化
}

frmLanguageSetting::~frmLanguageSetting()
{
    delete ui;
}

void frmLanguageSetting::refrashLanguageList()
{
    ui->ldtLanguageSelect->setText("Cn 中文简体");
    QStringList languageList = QStringList()<<"Cn 中文简体";
    for (QString language:languageList)
    {
        QListWidgetItem *item = new QListWidgetItem;
        item->setText(language);
        item->setSizeHint(QSize(ui->lstProject->width()-5,40));
        ui->lstProject->insertItem(ui->lstProject->count(),item);
    }

}

void frmLanguageSetting::on_btnApply_clicked()
{

}
