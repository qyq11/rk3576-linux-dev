#ifndef CUSMENUBUTTON_H
#define CUSMENUBUTTON_H

#include <QPushButton>
#include <QLabel>

class CusMenuButton : public QPushButton
{
    Q_OBJECT
public:
    explicit CusMenuButton(QWidget *parent = nullptr);
    ~CusMenuButton();
    void setButtonInfo(QString);
    void setButtonStartInfo(QString);
    void setButtonIco(QString);
    void setDisableIco(QString);
    void setBtnDisabled(bool);

signals:

private:
    QLabel *lblIco;
    QLabel *lblInfo;
    QString disableIco;
    QString normalIco;

};

#endif // CUSMENUBUTTON_H
