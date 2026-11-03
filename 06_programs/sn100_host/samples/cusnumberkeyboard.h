#ifndef CUSNUMBERKEYBOARD_H
#define CUSNUMBERKEYBOARD_H

#include "cusview.h"

namespace Ui {
class CusNumberKeyboard;
}

class CusNumberKeyboard : public CusView
{
    Q_OBJECT

public:
    explicit CusNumberKeyboard(QWidget *parent = nullptr);
    ~CusNumberKeyboard();
    void hidePointButton();
    void showPointButton();
private slots:
    void slotNumberBtnClick();
private:
    Ui::CusNumberKeyboard *ui;
    QWidget *parentWidget;
};

#endif // CUSNUMBERKEYBOARD_H
