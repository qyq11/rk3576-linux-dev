#ifndef SN100_REAGENT_PANEL_H
#define SN100_REAGENT_PANEL_H
#include "reagent_types.h"
#include <QWidget>
namespace Ui
{
class wgtReagentPanel;
}
class wgtReagentPanel : public QWidget
{
    Q_OBJECT
public:
    explicit wgtReagentPanel(QWidget* parent = nullptr);
    ~wgtReagentPanel() override;
    void setSnapshot(const ReagentSnapshot& snapshot);
    void selectHole(const QString& name);
signals:
    void holeSelected(QString name);
    void holeEditRequested(QString name);

private:
    Ui::wgtReagentPanel* ui;
};
#endif
