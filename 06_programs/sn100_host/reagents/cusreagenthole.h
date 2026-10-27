#ifndef SN100_REAGENT_HOLE_WIDGET_H
#define SN100_REAGENT_HOLE_WIDGET_H
#include "reagent_types.h"
#include <QWidget>
class cusReagentHole : public QWidget
{
    Q_OBJECT
public:
    explicit cusReagentHole(QWidget* parent = nullptr);
    void setInformation(const ReagentHole& hole, const ReagentRecord& reagent, const ReagentBottle& bottle);
    void setSelected(bool selected);
    void setDisplayMode(int mode);
signals:
    void clicked(QString name);
    void doubleClicked(QString name);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseDoubleClickEvent(QMouseEvent*) override;

private:
    ReagentHole hole_;
    ReagentRecord reagent_;
    ReagentBottle bottle_;
    bool selected_ = false;
    int displayMode_ = 0;
};
#endif
