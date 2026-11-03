#include "cusnumberkeyboard.h"
#include "ui_cusnumberkeyboard.h"

#include <QLineEdit>

CusNumberKeyboard::CusNumberKeyboard(QWidget *parent) :
    CusView(parent),
    ui(new Ui::CusNumberKeyboard)
{
    ui->setupUi(this);
    parentWidget = parent;

    QList<QPushButton *> buttonList = this->findChildren<QPushButton*>();
    for(auto btn:buttonList)
        connect(btn,SIGNAL(clicked()),this,SLOT(slotNumberBtnClick()));
}

CusNumberKeyboard::~CusNumberKeyboard()
{
    delete ui;
}

void CusNumberKeyboard::hidePointButton()
{
    ui->btnNum_Point->hide();
}

void CusNumberKeyboard::showPointButton()
{
    ui->btnNum_Point->show();
}

void CusNumberKeyboard::slotNumberBtnClick()
{
    QPushButton *btnClick = qobject_cast<QPushButton*>(sender());
    if(btnClick && parentWidget)
    {
        QLineEdit *foucLdt = qobject_cast<QLineEdit*>(parentWidget->focusWidget());
        if(foucLdt)
        {
            if(btnClick == ui->btnNum_Del)
            {
                QString text= foucLdt->text();
                text= text.left(text.length()-1);
                foucLdt->setText(text);
            }
            else if(btnClick == ui->btnNum_Point)
            {
                QString text= foucLdt->text();
                if(!text.contains("."))
                {
                    text += ".";
                    foucLdt->setText(text);
                }
            }
            else
            {
                QString text= foucLdt->text();
                text += btnClick->text();
                foucLdt->setText(text);
            }
        }
    }
}
