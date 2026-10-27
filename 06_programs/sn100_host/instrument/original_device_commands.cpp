#include "original_device_commands.h"

const QList<OriginalDeviceButton> &originalDeviceButtons()
{
    static const QList<OriginalDeviceButton> buttons = {
        {QStringLiteral("pushButton_start"), "Ts"},
        {QStringLiteral("pushButton_pause"), "Tp[0]"},
        {QStringLiteral("pushButton_queryStatus"), "Qj"},
        {QStringLiteral("pushButton_queryAlarm"), "Qk"},
        {QStringLiteral("pushButton_queryConsume"), "QH"},
        {QStringLiteral("btn_StopTest"), "MP"},
        {QStringLiteral("pushButton_requestReagent"), "Ur"},
        {QStringLiteral("pushButton_requestChannel"), "Uh"},
        {QStringLiteral("btnOpenBen_E"), "ME"},
        {QStringLiteral("btnCloseBen_f"), "Mf"},
        {QStringLiteral("btnSampleWater_A1"), "MA[1]"},
        {QStringLiteral("btnSampleWater_A0"), "MA[0]"},
        {QStringLiteral("btnSampleWaster_c1"), "Mc[1]"},
        {QStringLiteral("btnSampleWaster_c0"), "Mc[0]"},
        {QStringLiteral("btnReagent3Fa_F0"), "MF[1]"},
        {QStringLiteral("btnReagent3Fa_F1"), "MF[0]"},
        {QStringLiteral("btnReagentWater_B1"), "MB[1]"},
        {QStringLiteral("btnReagentWater_B0"), "MB[0]"},
        {QStringLiteral("btnReagentWaster_e1"), "Me[1]"},
        {QStringLiteral("btnReagentWaster_e0"), "Me[0]"},
        {QStringLiteral("btnClearCup_C"), "MC"},
        {QStringLiteral("btnServosSample_M"), "MM"},
        {QStringLiteral("btnServosReagent_N"), "MN"}};
    return buttons;
}

bool isOriginalFixedCommand(const QByteArray &payload)
{
    if (payload == "Mz") return true;
    for (const auto &button : originalDeviceButtons())
        if (button.payload == payload) return true;
    return false;
}
