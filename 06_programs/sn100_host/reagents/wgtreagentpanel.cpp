#include "wgtreagentpanel.h"
#include "cusreagenthole.h"
#include "ui_wgtreagentpanel.h"
#include "ui_wgtreagentpanel_42.h"
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
void wgtReagentPanel::configureLayout(int count)
{
    if (count!=42 || panel42_) return;
    // 沿用原42位面板的坐标、底图和孔名，不按新偏好重新排布。
    for (QWidget *child : findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly)) child->hide();
    panel42_=new QWidget(this);
    Ui::wgtReagentPanel_6600 form;
    form.setupUi(panel42_);
    panel42_->setGeometry(0,0,1111,941);
    panel42_->show();
    for (cusReagentHole *hole : panel42_->findChildren<cusReagentHole*>())
    {
        connect(hole,&cusReagentHole::clicked,this,&wgtReagentPanel::holeSelected);
        connect(hole,&cusReagentHole::doubleClicked,this,&wgtReagentPanel::holeEditRequested);
    }
    for (const auto &name : {QStringLiteral("btnDisplayAmount"),QStringLiteral("btnDisplayTime"),QStringLiteral("btnDisplayKeepTime")})
        if (auto *button=panel42_->findChild<QPushButton*>(name))
            connect(button,&QPushButton::clicked,this,[this,name]()
            {
                const int mode=name=="btnDisplayAmount" ? 0 : name=="btnDisplayTime" ? 1 : 2;
                for (cusReagentHole *hole : panel42_->findChildren<cusReagentHole*>()) hole->setDisplayMode(mode);
            });
    setProperty("configuredReagentPositions",count);
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
