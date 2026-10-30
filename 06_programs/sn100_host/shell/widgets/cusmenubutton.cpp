#include "cusmenubutton.h"

#include <QDebug>

namespace
{
QString rgba(const QColor &colour)
{
    return QStringLiteral("%1,%2,%3,%4").arg(colour.red()).arg(colour.green()).arg(colour.blue()).arg(colour.alpha());
}
}

CusMenuButton::CusMenuButton(QWidget *parent) : QPushButton(parent)
{
    this->setStyleSheet(QString("QPushButton{background-color:rgba(%1);border:none;color:rgba(%2)}"
                        "QPushButton:hover{background-color:rgba(%3);}"
                        "QPushButton:checked{background-color:rgba(%3);border-image: url(:/shell/main_btn/menu_checked.png);}"
                        "QPushButton:disabled{background-color:transparent;color:#636363;}")
                        .arg("28,28,28,255")
                        .arg("255,255,255,255")
                        .arg("232,51,35,200"));

    lblIco = new QLabel(this);
    lblInfo = new QLabel(this);

}

CusMenuButton::~CusMenuButton()
{
    delete lblIco;
    delete lblInfo;
}

void CusMenuButton::setButtonInfo(QString info)
{
    lblInfo->setStyleSheet(QStringLiteral("QLabel{color:rgba(%1)}").arg(rgba(isEnabled() ? normalTextColour : QColor(99,99,99))));
    lblInfo->setGeometry(0,50,165,20);
    lblInfo->setAlignment(Qt::AlignCenter);
    QFont font1("Noto Sans CJK SC");
    font1.setPixelSize(16);
    lblInfo->setFont(font1);
    lblInfo->setText(info);
    lblInfo->show();
}

void CusMenuButton::setButtonStartInfo(QString text)
{
    // 原位置/字号不变；复用已有标签，让禁用更新也作用于开始/暂停文字。
    setButtonInfo(text);
    if (!isEnabled())
    {
        lblInfo->setStyleSheet("QLabel{color:rgb(99,99,99)}");
    }
}

void CusMenuButton::setButtonIco(QString ico)
{
    normalIco = ico;
    QString style=QString("QLabel{border-image: url(%1);}").arg(ico);
    lblIco->setStyleSheet(style);
    lblIco->setGeometry(165/2-38/2,8,38,38);
    lblIco->show();
}

void CusMenuButton::setDisableIco(QString disableIcoIn)
{
    disableIco = disableIcoIn;
}

void CusMenuButton::setBtnDisabled(bool disabled)
{
    if(disabled)
    {
        lblInfo->setStyleSheet("QLabel{color:#636363}");
        QString style=QString("QLabel{border-image: url(%1);}").arg(disableIco);
        lblIco->setStyleSheet(style);
    }
    else
    {
        lblInfo->setStyleSheet(QStringLiteral("QLabel{color:rgba(%1)}").arg(rgba(normalTextColour)));
        QString style=QString("QLabel{border-image: url(%1);}").arg(normalIco);
        lblIco->setStyleSheet(style);
    }
    this->setDisabled(disabled);
}

void CusMenuButton::setOriginalColours(const QColor &base, const QColor &font, const QColor &active)
{
    normalTextColour = font;
    setStyleSheet(QStringLiteral("QPushButton{background-color:rgba(%1);border:none;color:rgba(%2);}"
        "QPushButton:hover{background-color:rgba(%3);}"
        "QPushButton:checked{background-color:rgba(%3);border-image:url(:/shell/main_btn/menu_checked.png);}"
        "QPushButton:disabled{background-color:transparent;color:#636363;}").arg(rgba(base),rgba(font),rgba(active)));
    lblInfo->setStyleSheet(QStringLiteral("QLabel{color:rgba(%1)}").arg(rgba(isEnabled() ? font : QColor(99,99,99))));
}

void CusMenuButton::setOriginalActionColours(const QColor &base, const QColor &font)
{
    normalTextColour = font;
    // 原开始/暂停的悬停和按下色是固定红色，不能误套菜单选中背景图。
    setStyleSheet(QStringLiteral("QPushButton{border:none;background-color:rgba(%1);color:rgba(%2);}"
        "QPushButton:hover{background-color:rgb(242,89,76);color:rgba(%2);}"
        "QPushButton:pressed{background-color:rgb(211,47,47);color:rgba(%2);}"
        "QPushButton:disabled{background-color:transparent;color:#636363;}").arg(rgba(base),rgba(font)));
    lblInfo->setStyleSheet(QStringLiteral("QLabel{color:rgba(%1)}").arg(rgba(isEnabled() ? font : QColor(99,99,99))));
}
