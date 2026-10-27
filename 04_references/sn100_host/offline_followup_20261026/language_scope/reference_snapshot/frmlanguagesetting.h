#ifndef FRMLANGUAGESETTING_H
#define FRMLANGUAGESETTING_H

#include <QWidget>

namespace Ui {
class frmLanguageSetting;
}

class frmLanguageSetting : public QWidget
{
    Q_OBJECT

public:
    explicit frmLanguageSetting(QWidget *parent = nullptr);
    ~frmLanguageSetting();

private slots:
    void on_btnApply_clicked();

private:
    Ui::frmLanguageSetting *ui;
    void refrashLanguageList();
};

#endif // FRMLANGUAGESETTING_H
