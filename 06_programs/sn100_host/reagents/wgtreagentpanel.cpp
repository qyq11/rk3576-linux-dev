#include "wgtreagentpanel.h"
#include "cusreagenthole.h"
#include "ui_wgtreagentpanel.h"
wgtReagentPanel::wgtReagentPanel(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::wgtReagentPanel)
{
    ui->setupUi(this);
    auto changeMode = [this](int mode)
    {
        for (cusReagentHole* hole : findChildren<cusReagentHole*>())
            hole->setDisplayMode(mode);
    };
    connect(ui->btnDisplayAmount, &QPushButton::clicked, this, [changeMode]() { changeMode(0); });
    connect(ui->btnDisplayTime, &QPushButton::clicked, this, [changeMode]() { changeMode(1); });
    connect(ui->btnDisplayKeepTime, &QPushButton::clicked, this, [changeMode]() { changeMode(2); });
    for (cusReagentHole* hole : findChildren<cusReagentHole*>())
    {
        connect(hole, &cusReagentHole::clicked, this, &wgtReagentPanel::holeSelected);
        connect(hole, &cusReagentHole::doubleClicked, this, &wgtReagentPanel::holeEditRequested);
    }
}
wgtReagentPanel::~wgtReagentPanel()
{
    delete ui;
}
void wgtReagentPanel::setSnapshot(const ReagentSnapshot& snapshot)
{
    for (cusReagentHole* widget : findChildren<cusReagentHole*>())
    {
        ReagentHole hole;
        ReagentRecord reagent;
        ReagentBottle bottle;
        for (const auto& h : snapshot.holes)
            if (h.name == widget->objectName())
                hole = h;
        for (const auto& r : snapshot.reagents)
            if (r.id == hole.reagentId)
                reagent = r;
        for (const auto& b : snapshot.bottles)
            if (b.id == hole.bottleId)
                bottle = b;
        widget->setInformation(hole, reagent, bottle);
    }
}
void wgtReagentPanel::selectHole(const QString& name)
{
    for (cusReagentHole* hole : findChildren<cusReagentHole*>())
        hole->setSelected(hole->objectName() == name);
}
