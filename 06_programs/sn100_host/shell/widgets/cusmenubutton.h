#ifndef CUSMENUBUTTON_H
#define CUSMENUBUTTON_H

#include <QPushButton>
#include <QLabel>
#include <QColor>

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
    void setOriginalColours(const QColor &base, const QColor &font, const QColor &active);
    void setOriginalActionColours(const QColor &base, const QColor &font);

signals:

private:
    QLabel *lblIco;
    QLabel *lblInfo;
    QString disableIco;
    QString normalIco;
    QColor normalTextColour = Qt::white;

};

#endif // CUSMENUBUTTON_H
