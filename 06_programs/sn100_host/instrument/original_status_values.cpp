#include "original_status_values.h"
#include <QHash>
#include <QVariant>
#include <cmath>
#include <initializer_list>

namespace OriginalInstrument
{
namespace
{
enum class FieldType { Boolean, Number, Float, NumericBoolean, Text };
struct Field { const char *name; FieldType type; };

class Values
{
public:
    void read(const QStringList &input, std::initializer_list<Field> fields)
    {
        int index = 0;
        for (const Field &field : fields)
        {
            const QString name = QString::fromLatin1(field.name);
            if (index >= input.size()) { values_.remove(name); ++index; continue; }
            const QString text = input.at(index++);
            bool ok = true;
            QVariant value;
            if (field.type == FieldType::Text) value = text;
            else if (field.type == FieldType::Boolean) value = text.toInt(&ok) > 0;
            else
            {
                const double number = field.type == FieldType::Float ? double(text.toFloat(&ok)) : text.toDouble(&ok);
                ok = ok && std::isfinite(number);
                value = field.type == FieldType::NumericBoolean ? QVariant(number != 0) : QVariant(number);
            }
            if (ok) values_.insert(name, value);
            else values_.remove(name);
        }
    }
    bool has(const char *name) const { return values_.contains(QString::fromLatin1(name)); }
    bool flag(const char *name) const { return values_.value(QString::fromLatin1(name)).toBool(); }
    QString text(const char *name) const
    {
        return has(name) ? values_.value(QString::fromLatin1(name)).toString() : QStringLiteral("未上报");
    }
    QString booleanText(const char *name, const char *positive, const char *negative) const
    {
        return has(name) ? QString::fromUtf8(flag(name) ? positive : negative) : QStringLiteral("未上报");
    }
    QString number(const char *name, char format = 'g', int precision = 6) const
    {
        return has(name) ? QString::number(values_.value(QString::fromLatin1(name)).toDouble(), format, precision)
                         : QStringLiteral("未上报");
    }
private:
    QHash<QString, QVariant> values_;
};

Values readValues(const Sn100Protocol::StatusSnapshot &snapshot)
{
    Values values;
    // 主字段从0起，顺序与原pcQueryStatus逐项一致；后续扩展字段不挤占已有位置。
    values.read(snapshot.generalFields, {
        {"driverModel", FieldType::Text}, {"mainstatus", FieldType::Number},
        {"ledStatus", FieldType::Number}, {"errorWorkType", FieldType::Number},
        {"errCodeList", FieldType::Text}, {"doorNormal", FieldType::Boolean},
        {"doorOpen_SS", FieldType::Boolean}, {"normalTempture", FieldType::Boolean},
        {"driverVersions", FieldType::Text}, {"driverNumber", FieldType::Text},
        {"EmgcStopKey_SS", FieldType::Boolean}, {"noWaterTest", FieldType::Boolean},
        {"noCupTest", FieldType::Boolean}, {"versionCheck", FieldType::Text},
        {"StartKey_SS", FieldType::Boolean}, {"PlateKey_SS", FieldType::Boolean},
        {"SampleScannerStatus", FieldType::Boolean}, {"ReagentScannerStatus", FieldType::Boolean},
        {"MainPeriodMs", FieldType::Text}, {"protocolMarker", FieldType::Text},
        {"reagentPanelChannelCount", FieldType::Number}, {"channelCount", FieldType::Number},
        {"emergencyHoleCountTemp", FieldType::Number}, {"sampleHoleCountTemp", FieldType::Number},
        {"is2ReagentNeedleWorking", FieldType::Boolean}
    });
    // 子系统号、字段顺序、toInt()>0及浮点精度类型均来自原pcQueryStatus。
    // 每块独立检查长度，缺字段或坏数值显示未上报，不借用相邻块或默认值。
    for (const auto &block : snapshot.subsystems)
    {
        switch (block.id)
        {
        case 16: // SubSys_JYZC
            values.read(block.fields, {
                {"isRackError", FieldType::Boolean},
                {"isRackInGe", FieldType::Boolean},
                {"isRackInTe", FieldType::Boolean},
                {"isRackAddGe", FieldType::Boolean},
                {"isRackOutGe", FieldType::Boolean},
                {"ScanerCur", FieldType::Boolean},
                {"scanerReagent", FieldType::Number},
                {"scanerSample", FieldType::Number},
                {"isRackInEmpty", FieldType::Boolean},
                {"isRackInArrive", FieldType::Boolean},
                {"isRackAdd1in", FieldType::Boolean},
                {"isRackAdd2out", FieldType::Boolean},
                {"isRackOutArrive", FieldType::Boolean},
                {"isRackOutFull", FieldType::Boolean},
                {"isRackInEmpty_SS", FieldType::Boolean},
                {"isRackOutFull_SS", FieldType::Boolean},
                {"isScanerOpen", FieldType::Boolean},
                {"hasSampleCup", FieldType::Boolean},
            });
            break;
        case 17: // SubSys_XSJZJ
            values.read(block.fields, {
                {"isDilpSataError", FieldType::Boolean},
                {"dilpGe", FieldType::Boolean},
                {"sataGe", FieldType::Boolean},
                {"doorIBSNormal", FieldType::Boolean},
                {"doorEmergencyNormal", FieldType::Boolean},
                {"normalCleanBufferUp", FieldType::Boolean},
                {"normalCleanBufferLow", FieldType::Boolean},
                {"doorIBSOpen_SS", FieldType::Boolean},
                {"doorEmergencyOpen_SS", FieldType::Boolean},
                {"normalCleanBufferUp_SS", FieldType::Boolean},
                {"normalCleanBufferLow_SS", FieldType::Boolean},
                {"isPressBengWorking", FieldType::Boolean},
                {"isCleanBengWorking", FieldType::Boolean},
                {"pressure", FieldType::Number},
            });
            break;
        case 3: // SubSys_XBQ
            values.read(block.fields, {
                {"isSelectError", FieldType::Boolean},
                {"selecterGe", FieldType::Boolean},
                {"selecterTe", FieldType::Boolean},
                {"newcupGe", FieldType::Boolean},
                {"normalCupUp", FieldType::Boolean},
                {"normalCupLow", FieldType::Boolean},
                {"normalCupBig", FieldType::Boolean},
                {"normalCupSamllUp", FieldType::Boolean},
                {"normalCupSamllLow", FieldType::Boolean},
                {"normalCupUp_SS", FieldType::Boolean},
                {"normalCupLow_SS", FieldType::Boolean},
                {"normalCupBig_SS", FieldType::Boolean},
                {"normalCupSamllUp_SS", FieldType::Boolean},
                {"normalCupSamllLow_SS", FieldType::Boolean},
                {"normalCupCheck_SS", FieldType::Boolean},
                {"temperatureEnvir", FieldType::Number},
                {"isMotorWorking", FieldType::Boolean},
                {"motorGe", FieldType::Boolean},
                {"motorSpeed", FieldType::Number},
                {"motorSpeedBase", FieldType::Number},
                {"isSelectAutoWork", FieldType::Boolean},
            });
            break;
        case 6: // SubSys_SJP
            values.read(block.fields, {
                {"isPlateError", FieldType::Boolean},
                {"plateGe", FieldType::Boolean},
                {"doorRPanelNormal", FieldType::Boolean},
                {"doorRPanelOpen_SS", FieldType::Boolean},
                {"normalColdWaterUp", FieldType::Boolean},
                {"normalColdWaterLow", FieldType::Boolean},
                {"normalColdWaterUp_SS", FieldType::Boolean},
                {"normalColdWaterLow_SS", FieldType::Boolean},
                {"temperatureRPanel", FieldType::Number},
                {"isBengWorking", FieldType::Boolean},
                {"flow", FieldType::Number},
                {"flowBase", FieldType::Number},
                {"flowGe", FieldType::Boolean},
                {"isCoolWorking", FieldType::Boolean},
                {"isRNeedleDanger", FieldType::Boolean},
                {"buttonStatus", FieldType::Boolean},
                {"isShutDown", FieldType::Boolean},
            });
            break;
        case 11: // SubSys_YBZ
            values.read(block.fields, {
                {"isSampleError", FieldType::Boolean},
                {"SneedleRGe", FieldType::Boolean},
                {"SneedleZGe", FieldType::Boolean},
                {"SpumpGe", FieldType::Boolean},
                {"SzzGe", FieldType::Boolean},
                {"SzzAlarm", FieldType::Boolean},
                {"isSBlenderWorking", FieldType::Boolean},
                {"isSFaWorking", FieldType::Boolean},
                {"isSYWbit", FieldType::Boolean},
                {"SampleNFreq", FieldType::Number},
                {"SampleBaseFreq", FieldType::Number},
                {"SYWsensor", FieldType::Boolean},
                {"SDangerOut", FieldType::Boolean},
            });
            break;
        case 14: // SubSys_SSJZ
            values.read(block.fields, {
                {"isReagentError", FieldType::Boolean},
                {"RDangerOut", FieldType::Boolean},
                {"RneedleRGe", FieldType::Boolean},
                {"RneedleZGe", FieldType::Boolean},
                {"RpumpGe", FieldType::Boolean},
                {"RzzGe", FieldType::Boolean},
                {"RzzAlarm", FieldType::Boolean},
                {"isRBlenderWorking", FieldType::Boolean},
                {"is3FaStatus", FieldType::Boolean},
                {"isRFaWorking", FieldType::Boolean},
                {"isR1YWbit", FieldType::Boolean},
                {"isR2YWbit", FieldType::Boolean},
                {"normalWasteCupArrive", FieldType::Boolean},
                {"normalWasteCupArrive_SS", FieldType::Boolean},
                {"Reagent1Freq", FieldType::Number},
                {"Reagent2Freq", FieldType::Number},
                {"RYWsensor", FieldType::Boolean},
                {"RNeedleNum", FieldType::Boolean},
                {"temperatureRNeedleLeft", FieldType::Number},
                {"isLtempWorking", FieldType::Boolean},
                {"temperatureRNeedleRight", FieldType::Number},
                {"isRtempWorking", FieldType::Boolean},
                {"normalCleanWater", FieldType::Boolean},
                {"normalWasteWater", FieldType::Boolean},
                {"normalCleanWater_SS", FieldType::Boolean},
                {"normalWasteWater_SS", FieldType::Boolean},
            });
            break;
        case 12: // SubSys_YBZS
            values.read(block.fields, {
                {"isSamCatcherError", FieldType::Boolean},
                {"SC_RGe", FieldType::Boolean},
                {"SC_ZGe", FieldType::Boolean},
                {"SC_ZTe", FieldType::Boolean},
                {"isSWasteBengWorking", FieldType::Boolean},
                {"normalWasteWaterBufferS", FieldType::Boolean},
                {"normalWasteWaterBufferS_SS", FieldType::Boolean},
                {"normalWasteCup", FieldType::Boolean},
                {"normalWasteCup_SS", FieldType::Boolean},
            });
            break;
        case 15: // SubSys_CSZS
            values.read(block.fields, {
                {"isTestCatcherError", FieldType::Boolean},
                {"TC_RGe", FieldType::Boolean},
                {"TC_ZGe", FieldType::Boolean},
                {"TC_ZTe", FieldType::Boolean},
                {"TC_CGe", FieldType::Boolean},
                {"TC_CTe", FieldType::Boolean},
                {"isRWasteBengWorking", FieldType::Boolean},
                {"normalWasteWaterBufferR", FieldType::Boolean},
                {"normalWasteWaterBufferR_SS", FieldType::Boolean},
                {"temperatureTPanel", FieldType::Number},
                {"temperatureTestWare", FieldType::Number},
                {"isWareWorking", FieldType::Boolean},
                {"isTempReady", FieldType::Boolean},
            });
            break;
        case 20: // SubSys_Injecter
            values.read(block.fields, {
                {"rack_6_error", FieldType::Boolean},
                {"rack_6_inGe", FieldType::Boolean},
                {"rack_6_addGe", FieldType::Boolean},
                {"rack_6_outGe", FieldType::Boolean},
                {"isRackInEmpty_SS", FieldType::Boolean},
                {"rack_6_rackReady_SS", FieldType::Boolean},
                {"isRackInArrive", FieldType::Boolean},
                {"rack_6_rightBeam_SS", FieldType::Boolean},
                {"rack_6_leftBeam_SS", FieldType::Boolean},
                {"hasSampleCup", FieldType::Boolean},
                {"rack_6_reservedState", FieldType::Boolean},
                {"rack_6_outReady_SS", FieldType::Boolean},
                {"isRackOutFull_SS", FieldType::Boolean},
                {"isRackInEmpty", FieldType::Boolean},
                {"isRackAdd1in", FieldType::Boolean},
                {"isRackAdd2out", FieldType::Boolean},
                {"isRackOutArrive", FieldType::Boolean},
                {"isRackOutFull", FieldType::Boolean},
                {"hasSampleCup", FieldType::Boolean},
            });
            break;
        case 24: // SubSys_IBS_STAT
            values.read(block.fields, {
                {"rail_6_error", FieldType::Boolean},
                {"rail_6_Ge", FieldType::Boolean},
                {"rail_6_plateGe", FieldType::Boolean},
            });
            break;
        case 26: // SubSys_Selecter
            values.read(block.fields, {
                {"select_6_error", FieldType::Boolean},
                {"select_6_HGe", FieldType::Boolean},
                {"select_6_HFlagBit", FieldType::Boolean},
                {"select_6_VGe", FieldType::Boolean},
                {"select_6_SGe", FieldType::Boolean},
                {"select_6_SFlagBit", FieldType::Boolean},
                {"temperatureEnvir", FieldType::Float},
                {"normalCupBig_SS", FieldType::Boolean},
                {"select_6_leftBeam", FieldType::Boolean},
                {"select_6_fallPosBeam", FieldType::Boolean},
                {"normalCupUp", FieldType::Boolean},
                {"normalCupLow", FieldType::Boolean},
                {"normalCupBig", FieldType::Boolean},
                {"normalCupSamllUp", FieldType::Boolean},
                {"normalCupSamllLow", FieldType::Boolean},
            });
            break;
        case 32: // SubSys_CoolPlate
            values.read(block.fields, {
                {"plate_6_error", FieldType::Boolean},
                {"plate_6_Ge", FieldType::Boolean},
                {"temperatureRPanel", FieldType::Float},
                {"flow", FieldType::Float},
                {"isBengWorking", FieldType::Boolean},
                {"isCoolWorking", FieldType::Boolean},
                {"buttonStatus", FieldType::Boolean},
                {"isShutDown", FieldType::Boolean},
                {"normalColdWaterUp_SS", FieldType::Boolean},
                {"normalColdWaterLow_SS", FieldType::Boolean},
                {"plate_6_valveControl", FieldType::Boolean},
                {"plate_6_beltControl", FieldType::Boolean},
                {"doorRPanelNormal", FieldType::Boolean},
                {"normalCleanBufferUp", FieldType::Boolean},
                {"normalCleanBufferLow", FieldType::Boolean},
                {"doorRPanelOpen_SS", FieldType::Boolean},
                {"flowBase", FieldType::Number},
            });
            break;
        case 21: // SubSys_SampleNeedle
            values.read(block.fields, {
                {"sNeedle_6_error", FieldType::Boolean},
                {"sNeedle_6_HGe", FieldType::Boolean},
                {"sNeedle_6_VGe", FieldType::Boolean},
                {"sNeedle_6_pumpGe", FieldType::Boolean},
                {"sNeedle_6_1pumpPresure", FieldType::Float},
                {"sNeedle_6_reservedSignal", FieldType::Float},
                {"sNeedle_6_liquidSignal", FieldType::Boolean},
                {"sNeedle_6_pinSignal", FieldType::Boolean},
                {"sNeedle_6_1pumpState", FieldType::Boolean},
                {"sNeedle_6_minusValveState", FieldType::Boolean},
                {"sNeedle_6_supplyValveState", FieldType::Boolean},
                {"sNeedle_6_liquidSensor", FieldType::Boolean},
                {"sNeedle_6_solenoidValve", FieldType::Boolean},
                {"sNeedle_6_threeWayValve", FieldType::Boolean},
                {"sNeedle_6_pressureReliefValve", FieldType::Boolean},
            });
            break;
        case 31: // SubSys_ReagentNeedle2
            values.read(block.fields, {
                {"rNeedle2_6_error", FieldType::Boolean},
                {"rNeedle2_6_HGe", FieldType::Boolean},
                {"rNeedle2_6_VGe", FieldType::Boolean},
                {"rNeedle2_6_pumpGe", FieldType::Boolean},
                {"rNeedle2_6_minusPumpPressure", FieldType::Float},
                {"rNeedle2_6_ballState", FieldType::Boolean},
                {"rNeedle2_6_liquidSignal", FieldType::Boolean},
                {"rNeedle2_6_pinState", FieldType::Boolean},
                {"rNeedle2_6_minusPumpControl", FieldType::Boolean},
                {"rNeedle2_6_waterMinusValveState", FieldType::Boolean},
                {"rNeedle2_6_supplyValveState", FieldType::Boolean},
                {"rNeedle2_6_liquidSensor", FieldType::Boolean},
                {"rNeedle2_6_solenoidValve", FieldType::Boolean},
                {"rNeedle2_6_cleaningMinusValve", FieldType::Boolean},
                {"rNeedle2_6_cleaningPumpValve", FieldType::Boolean},
                {"temperatureRNeedleRight", FieldType::Number},
                {"normalWasteWater", FieldType::NumericBoolean},
                {"normalWasteCupArrive", FieldType::Boolean},
                {"normalWasteCupArrive_SS", FieldType::Boolean},
            });
            break;
        case 22: // SubSys_Transfer
            values.read(block.fields, {
                {"sCatcher_6_error", FieldType::Boolean},
                {"sCatcher_6_HGe", FieldType::Boolean},
                {"sCatcher_6_VGe", FieldType::Boolean},
                {"sCatcher_6_cupDetect", FieldType::Boolean},
                {"sCatcher_6_blenderState", FieldType::Boolean},
                {"warmPlate_6_Ge", FieldType::Boolean},
                {"warmPlate_6_temp", FieldType::Float},
                {"warmPlate_6_steadyState", FieldType::Boolean},
                {"sCatcher_6_blenderV", FieldType::Float},
                {"normalWasteCup", FieldType::Boolean},
                {"normalWasteCup_SS", FieldType::Boolean},
            });
            break;
        case 29: // SubSys_TestCatcher
            values.read(block.fields, {
                {"lCatcher_6_error", FieldType::Boolean},
                {"lCatcher_6_xGe", FieldType::Boolean},
                {"lCatcher_6_yGe", FieldType::Boolean},
                {"lCatcher_6_VGe", FieldType::Boolean},
                {"lCatcher_6_cupDetect", FieldType::Boolean},
                {"lCatcher_6_blenderState", FieldType::Boolean},
                {"temperatureTestWare", FieldType::Float},
                {"temperatureTPanel", FieldType::Float},
                {"isTempReady", FieldType::Boolean},
                {"lCatcher_blenderV", FieldType::Float},
            });
            break;
        case 28: // SubSys_ReagentNeedle1
            values.read(block.fields, {
                {"rNeedle1_6_error", FieldType::Boolean},
                {"rNeedle1_6_HGe", FieldType::Boolean},
                {"rNeedle1_6_VGe", FieldType::Boolean},
                {"rNeedle1_6_pumpGe", FieldType::Boolean},
                {"rNeedle1_6_pumpPressure", FieldType::Float},
                {"rNeedle1_6_ballState", FieldType::Boolean},
                {"rNeedle1_6_liquidSignal", FieldType::Boolean},
                {"rNeedle1_6_pinState", FieldType::Boolean},
                {"rNeedle1_6_pumpControl", FieldType::Boolean},
                {"rNeedle1_6_waterMinusValveState", FieldType::Boolean},
                {"rNeedle1_6_supplyValveState", FieldType::Boolean},
                {"rNeedle1_6_liquidSensor", FieldType::Boolean},
                {"rNeedle1_6_solenoidValve", FieldType::Boolean},
                {"rNeedle1_6_cleaningMinusValve", FieldType::Boolean},
                {"rNeedle1_6_cleaningPumpValve", FieldType::Boolean},
                {"temperatureRNeedleLeft", FieldType::Number},
            });
            break;
        default: break; // 原解析器未定义的子系统不推测其字段。
        }
    }
    return values;
}

void commonLabels(QMap<QString, QString> &labels, const Values &values)
{
    labels.insert(QStringLiteral("lbl6Main_reagentScaner_5"), values.text("versionCheck"));
    labels.insert(QStringLiteral("lbl6Main_machineLight_3"), values.text("versionCheck"));
    labels.insert(QStringLiteral("lblMain_mainPeriod"), values.text("MainPeriodMs"));
    labels.insert(QStringLiteral("lbl6Main_mainPeriod"), values.text("MainPeriodMs"));
    labels.insert(QStringLiteral("label_2ReagentWork"), values.booleanText("is2ReagentNeedleWorking", "是", "否"));
    labels.insert(QStringLiteral("lbl6Main2ReagentWork"), values.booleanText("is2ReagentNeedleWorking", "是", "否"));
    labels.insert(QStringLiteral("lbl6Main_emergencyCover_5"), values.text("driverModel"));
    labels.insert(QStringLiteral("lbl6Main_reagentScaner_6"), values.text("driverModel"));
    labels.insert(QStringLiteral("lbl6Main_subSystemCount_3"), values.number("SubSystemCount"));
    labels.insert(QStringLiteral("lbl6Main_subSystemCount"), values.number("SubSystemCount"));
    labels.insert(QStringLiteral("lbl6Main_reagentScaner_7"), values.booleanText("noWaterTest", "是", "否"));
    labels.insert(QStringLiteral("lbl6Main_reagentScaner_8"), values.booleanText("noCupTest", "是", "否"));
    labels.insert(QStringLiteral("lblReagentOpen"), values.booleanText("doorRPanelNormal", "关闭", "开启"));
    labels.insert(QStringLiteral("lblDuilentOpen"), values.booleanText("doorIBSNormal", "关闭", "开启"));
    labels.insert(QStringLiteral("lblEmergencyOpen"), values.booleanText("doorEmergencyNormal", "关闭", "开启"));
    labels.insert(QStringLiteral("lblEnvirTemp"), values.number("temperatureEnvir", 'f', 1));
    labels.insert(QStringLiteral("lblLeftNeedleTemp"), values.number("temperatureRNeedleLeft", 'f', 1));
    labels.insert(QStringLiteral("lblRightNeedleTemp"), values.number("temperatureRNeedleRight", 'f', 1));
    labels.insert(QStringLiteral("lblRPlateTemp"), values.number("temperatureRPanel", 'f', 1));
    labels.insert(QStringLiteral("lblTPlateTemp"), values.number("temperatureTPanel", 'f', 2));
    labels.insert(QStringLiteral("lblWPlateTemp"), values.number("warmPlate_6_temp", 'f', 2));
}

void sn450Labels(QMap<QString, QString> &labels, const Values &values)
{
    labels.insert(QStringLiteral("lblRack_Error"), values.booleanText("isRackError", "异常", "正常"));
    labels.insert(QStringLiteral("lblRackIn_Ge"), values.booleanText("isRackInGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRackIn_Te"), values.booleanText("isRackInTe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRackIn_rackEmpty"), values.booleanText("isRackInEmpty_SS", "无架", "有架"));
    labels.insert(QStringLiteral("lblRackIn_Arrive"), values.booleanText("isRackInArrive", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRackAdd_Ge"), values.booleanText("isRackAddGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRackAdd_DS1"), values.booleanText("isRackAdd1in", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblRackAdd_DS2"), values.booleanText("isRackAdd2out", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblRackAdd_Arrive"), values.booleanText("isRackOutArrive", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRackOut_Ge"), values.booleanText("isRackOutGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRackOut_rackFull"), values.booleanText("isRackOutFull_SS", "架满", "正常"));
    labels.insert(QStringLiteral("lblRack_ScanerCur"), values.booleanText("ScanerCur", "试剂舱", "样本架"));
    labels.insert(QStringLiteral("lblRack_ScanerR"), values.number("scanerReagent", 'f', 0));
    labels.insert(QStringLiteral("lblRack_ScanerS"), values.number("scanerSample", 'f', 0));
    labels.insert(QStringLiteral("lblScaner_Status"), values.booleanText("isScanerOpen", "开启", "关闭"));
    labels.insert(QStringLiteral("lblSampleCup_Status"), values.booleanText("hasSampleCup", "有杯", "无杯"));
    labels.insert(QStringLiteral("lblSample_Error"), values.booleanText("isSampleError", "异常", "正常"));
    labels.insert(QStringLiteral("lblSamR_Ge"), values.booleanText("SneedleRGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSamZ_Ge"), values.booleanText("SneedleZGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSamP_Ge"), values.booleanText("SpumpGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSam_Zz"), values.booleanText("SzzGe", "撞针", "未撞针"));
    labels.insert(QStringLiteral("lblSam_ZzAlarm"), values.booleanText("SzzAlarm", "异常", "正常"));
    labels.insert(QStringLiteral("lblSam_danger"), values.booleanText("SDangerOut", "输出", "无输出"));
    labels.insert(QStringLiteral("lblSam_Blender"), values.booleanText("isSBlenderWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblSam_Fa"), values.booleanText("isSFaWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblSamYW_base"), values.number("SampleBaseFreq", 'f', 0));
    labels.insert(QStringLiteral("lblSamYW_Cur"), values.number("SampleNFreq", 'f', 0));
    labels.insert(QStringLiteral("lblSamYW_bit"), values.booleanText("isSYWbit", "1", "0"));
    labels.insert(QStringLiteral("lblSamYW_sensor"), values.booleanText("SYWsensor", "1", "0"));
    labels.insert(QStringLiteral("lblSCatcher_Error"), values.booleanText("isSamCatcherError", "异常", "正常"));
    labels.insert(QStringLiteral("lblSCatcherR_Ge"), values.booleanText("SC_RGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSCatcherZ_Ge"), values.booleanText("SC_ZGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSCatcherZ_Te"), values.booleanText("SC_ZTe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSamWaster_Beng"), values.booleanText("isSWasteBengWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblSamWaster_s"), values.booleanText("normalWasteWaterBufferS_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblWasterCup"), values.booleanText("normalWasteCup_SS", "未满", "满"));
    labels.insert(QStringLiteral("lblSelect_Error"), values.booleanText("isSelectError", "异常", "正常"));
    labels.insert(QStringLiteral("lblSelectS_Ge"), values.booleanText("selecterGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSelectS_Te"), values.booleanText("selecterTe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSelectN_Ge"), values.booleanText("newcupGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSelectNewCup"), values.booleanText("normalCupCheck_SS", "无杯", "有杯"));
    labels.insert(QStringLiteral("lblS_EnvirTemp"), values.number("temperatureEnvir", 'f', 1));
    labels.insert(QStringLiteral("lblSelectMoter_Ge"), values.booleanText("motorGe", "1", "0"));
    labels.insert(QStringLiteral("lblSelectMoter_Speed"), values.number("motorSpeed", 'f', 0));
    labels.insert(QStringLiteral("lblSelectMoter_Base"), values.number("motorSpeedBase", 'f', 0));
    labels.insert(QStringLiteral("lblSelectMoter_work"), values.booleanText("isMotorWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblSelectSamll_low"), values.booleanText("normalCupSamllLow_SS", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblSelectSamll_up"), values.booleanText("normalCupSamllUp_SS", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblSelect_low"), values.booleanText("normalCupLow_SS", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblSelect_up"), values.booleanText("normalCupUp_SS", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblSelect_big"), values.booleanText("normalCupBig_SS", "挡住", "不挡住"));
    labels.insert(QStringLiteral("lblSelect_auto"), values.booleanText("isSelectAutoWork", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblPlate_Error"), values.booleanText("isPlateError", "异常", "正常"));
    labels.insert(QStringLiteral("lblPlate_Ge"), values.booleanText("plateGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblPlate_door"), values.booleanText("doorRPanelOpen_SS", "开启", "盖上"));
    labels.insert(QStringLiteral("lblPlateCool_flow"), values.number("flow", 'f', 0));
    labels.insert(QStringLiteral("lblPlateCool_flowGe"), values.number("flowBase", 'f', 0));
    labels.insert(QStringLiteral("lblPlateCool_low"), values.booleanText("normalColdWaterLow_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblPlateCool_up"), values.booleanText("normalColdWaterUp_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblPlateCool_beng"), values.booleanText("isBengWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblPlateCool_work"), values.booleanText("isCoolWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblPlate_Temp"), values.number("temperatureRPanel", 'f', 1));
    labels.insert(QStringLiteral("lblPlate_danger"), values.booleanText("isRNeedleDanger", "有危险", "无危险"));
    labels.insert(QStringLiteral("lblPlate_button"), values.booleanText("buttonStatus", "按下", "弹起"));
    labels.insert(QStringLiteral("lblPlate_shutdown"), values.booleanText("isShutDown", "开", "关"));
    labels.insert(QStringLiteral("lblTestCatcher_Error"), values.booleanText("isTestCatcherError", "异常", "正常"));
    labels.insert(QStringLiteral("lblTestCatcherR_Ge"), values.booleanText("TC_RGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblTestCatcherZ_Ge"), values.booleanText("TC_ZGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblTestCatcherZ_Te"), values.booleanText("TC_ZTe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblTestCatcherS_Ge"), values.booleanText("TC_CGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblTestCatcherS_Te"), values.booleanText("TC_CTe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblTestHeat_work"), values.booleanText("isWareWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblTestHeat_ready"), values.booleanText("isTempReady", "稳定", "不稳定"));
    labels.insert(QStringLiteral("lblTestHeat_temp"), values.number("temperatureTPanel", 'f', 2));
    labels.insert(QStringLiteral("lblTestHeat_temp2"), values.number("temperatureTestWare", 'f', 2));
    labels.insert(QStringLiteral("lblTestWaste_s"), values.booleanText("normalWasteWaterBufferR_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblTestWaste_Beng"), values.booleanText("isRWasteBengWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblReagent_Error"), values.booleanText("isReagentError", "异常", "正常"));
    labels.insert(QStringLiteral("lblReagentR_Ge"), values.booleanText("RneedleRGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblReagentZ_Ge"), values.booleanText("RneedleZGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblReagentP_Ge"), values.booleanText("RpumpGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblReagent_Zz"), values.booleanText("RzzGe", "撞针", "未撞针"));
    labels.insert(QStringLiteral("lblReagent_ZzAlarm"), values.booleanText("RzzAlarm", "异常", "正常"));
    labels.insert(QStringLiteral("lblReagent_Blender"), values.booleanText("isRBlenderWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblReagent_Fa"), values.booleanText("isRFaWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblReagent_3Fa"), values.booleanText("is3FaStatus", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblReagent_danger"), values.booleanText("RDangerOut", "输出", "未输出"));
    labels.insert(QStringLiteral("lblReagent_Needel"), values.booleanText("RNeedleNum", "右针", "左针"));
    labels.insert(QStringLiteral("lblReagentYW_LBit"), values.booleanText("isR1YWbit", "1", "0"));
    labels.insert(QStringLiteral("lblReagentYW_LFre"), values.number("Reagent1Freq", 'f', 0));
    labels.insert(QStringLiteral("lblReagentYW_RBit"), values.booleanText("isR2YWbit", "1", "0"));
    labels.insert(QStringLiteral("lblReagentYW_RFre"), values.number("Reagent2Freq", 'f', 0));
    labels.insert(QStringLiteral("lblReagentYW_sensor"), values.booleanText("RYWsensor", "1", "0"));
    labels.insert(QStringLiteral("lblReagent_LTemp"), values.number("temperatureRNeedleLeft", 'f', 1));
    labels.insert(QStringLiteral("lblReagent_RTemp"), values.number("temperatureRNeedleRight", 'f', 1));
    labels.insert(QStringLiteral("lblReagent_Waste"), values.booleanText("normalWasteWater_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblReagent_Clean"), values.booleanText("normalCleanWater_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblWasterCup_s"), values.booleanText("normalWasteCupArrive_SS", "在位", "不在位"));
    labels.insert(QStringLiteral("lblDilp_Sata_Error"), values.booleanText("isDilpSataError", "异常", "正常"));
    labels.insert(QStringLiteral("lblDilp_Ge"), values.booleanText("dilpGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblSata_Ge"), values.booleanText("sataGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblDilp_door"), values.booleanText("doorIBSOpen_SS", "开启", "盖上"));
    labels.insert(QStringLiteral("lblSata_door"), values.booleanText("doorEmergencyOpen_SS", "开启", "盖上"));
    labels.insert(QStringLiteral("lblCleaner_low"), values.booleanText("normalCleanBufferLow_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblCleaner_up"), values.booleanText("normalCleanBufferUp_SS", "不在位", "在位"));
    labels.insert(QStringLiteral("lblCleaner_Beng"), values.booleanText("isCleanBengWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblPressure_Beng"), values.booleanText("isPressBengWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblPressure_Beng_2"), values.number("pressure"));
}

void otherModelLabels(QMap<QString, QString> &labels, const Values &values)
{
    labels.insert(QStringLiteral("lbl6Main_subSystemCount"), values.number("SubSystemCount"));
    labels.insert(QStringLiteral("lbl6Plate_Error"), values.booleanText("plate_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6Plate_Ge"), values.booleanText("plate_6_Ge", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6Plate_Temp"), values.number("temperatureRPanel", 'f', 1));
    labels.insert(QStringLiteral("lbl6PlateCool_flow"), values.number("flow", 'f', 1));
    labels.insert(QStringLiteral("lbl6PlateCool_beng"), values.booleanText("isBengWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6PlateCool_work"), values.booleanText("isCoolWorking", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6Plate_button"), values.booleanText("buttonStatus", "按下", "弹起"));
    labels.insert(QStringLiteral("lbl6Plate_shutdown"), values.booleanText("isShutDown", "关机", "开机"));
    labels.insert(QStringLiteral("lbl6Plate_UpBall"), values.booleanText("normalColdWaterUp_SS", "水满", "正常"));
    labels.insert(QStringLiteral("lbl6Plate_DownBall"), values.booleanText("normalColdWaterLow_SS", "缺水", "正常"));
    labels.insert(QStringLiteral("lbl6Plate_door"), values.booleanText("plate_6_valveControl", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6PlateCool_Belt"), values.booleanText("plate_6_beltControl", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6PlateDoor"), values.booleanText("doorRPanelOpen_SS", "开启", "盖上"));
    labels.insert(QStringLiteral("lbl6PlateCool_flowGe"), values.number("flowBase", 'f', 0));
    labels.insert(QStringLiteral("lbl6Rack_Error"), values.booleanText("rack_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6RackIn_Ge"), values.booleanText("rack_6_inGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6RackAdd_Ge"), values.booleanText("rack_6_addGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6RackOut_Ge"), values.booleanText("rack_6_outGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6RackIn_rackArea"), values.booleanText("isRackInEmpty_SS", "有架", "无架"));
    labels.insert(QStringLiteral("lbl6Rack_ready"), values.booleanText("rack_6_rackReady_SS", "有架", "无架"));
    labels.insert(QStringLiteral("lbl6Rack_rightBeam"), values.booleanText("rack_6_rightBeam_SS", "有架", "无架"));
    labels.insert(QStringLiteral("lbl6Rack_LeftBeam"), values.booleanText("rack_6_leftBeam_SS", "有架", "无架"));
    labels.insert(QStringLiteral("lbl6Rack_sampleDetect"), values.booleanText("hasSampleCup", "有样本", "无样本"));
    labels.insert(QStringLiteral("lbl6Rack_reservedSignal"), values.booleanText("rack_6_reservedState", "1", "0"));
    labels.insert(QStringLiteral("lbl6RackOut_ready"), values.booleanText("rack_6_outReady_SS", "有架", "无架"));
    labels.insert(QStringLiteral("lbl6RackOut_rackArea"), values.booleanText("isRackOutFull_SS", "已满", "无架"));
    labels.insert(QStringLiteral("lbl6Select_Error"), values.booleanText("select_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6SelectH_Ge"), values.booleanText("select_6_HGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SelectH_flagBit"), values.booleanText("select_6_HFlagBit", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SelectV_Ge"), values.booleanText("select_6_VGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SelectS_Ge"), values.booleanText("select_6_SGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SelectS_flagBit"), values.booleanText("select_6_SFlagBit", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6Select_EnvirTemp"), values.number("temperatureEnvir", 'f', 1));
    labels.insert(QStringLiteral("lbl6Select_hopperState"), values.booleanText("normalCupBig_SS", "缺杯", "正常"));
    labels.insert(QStringLiteral("lbl6Select_leftBeam"), values.booleanText("select_6_leftBeam", "有杯", "无杯"));
    labels.insert(QStringLiteral("lbl6Select_bottleFallBeam"), values.booleanText("select_6_fallPosBeam", "有杯", "无杯"));
    labels.insert(QStringLiteral("lbl6SNeedle_Error"), values.booleanText("sNeedle_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6SNeedleH_Ge"), values.booleanText("sNeedle_6_HGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SNeedleV_Ge"), values.booleanText("sNeedle_6_VGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SamP_Ge"), values.booleanText("sNeedle_6_pumpGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6Sam_pumpPressure"), values.number("sNeedle_6_1pumpPresure"));
    labels.insert(QStringLiteral("lbl6Sam_signal"), values.booleanText("sNeedle_6_reservedSignal", "1", "0"));
    labels.insert(QStringLiteral("lbl6Sam_liquidSignal"), values.booleanText("sNeedle_6_liquidSignal", "1", "0"));
    labels.insert(QStringLiteral("lbl6Sam_pinSignal"), values.booleanText("sNeedle_6_pinSignal", "撞针", "正常"));
    labels.insert(QStringLiteral("lbl6Sam_waterPumpState"), values.booleanText("sNeedle_6_1pumpState", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6Sam_supplyValveControl"), values.booleanText("sNeedle_6_supplyValveState", "开启", "关闭"));
    labels.insert(QStringLiteral("lbl6SNeedle_sensor"), values.booleanText("sNeedle_6_liquidSensor", "1", "0"));
    labels.insert(QStringLiteral("lbl6SNeedle_solenoidValve"), values.booleanText("sNeedle_6_solenoidValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lbl6SNeedle_threeWayValve"), values.booleanText("sNeedle_6_threeWayValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lbl6SNeedle_pressureRelifValve"), values.booleanText("sNeedle_6_pressureReliefValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lbl6SCatcher_Error"), values.booleanText("sCatcher_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6SCatcher_HGe"), values.booleanText("sCatcher_6_HGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SCatcher_VGe"), values.booleanText("sCatcher_6_VGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6SCatcher_cupDetect"), values.booleanText("sCatcher_6_cupDetect", "有杯", "无杯"));
    labels.insert(QStringLiteral("lbl6SCatcher_blenderState"), values.booleanText("sCatcher_6_blenderState", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6SCatcher_wastecup"), values.booleanText("normalWasteCup_SS", "未满", "满"));
    labels.insert(QStringLiteral("lbl6WarmPlate_Ge"), values.booleanText("warmPlate_6_Ge", "在位", "不在位"));
    labels.insert(QStringLiteral("lbll6WarmPlate_temp"), values.number("warmPlate_6_temp", 'f', 1));
    labels.insert(QStringLiteral("lbll6WarmPlate_steadyState"), values.booleanText("warmPlate_6_steadyState", "不稳定", "稳定"));
    labels.insert(QStringLiteral("lbl6SCatcher_blenderV"), values.number("sCatcher_6_blenderV", 'f', 2));
    labels.insert(QStringLiteral("lbl6LCatcher_Error"), values.booleanText("lCatcher_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6LCatcher_XGe"), values.booleanText("lCatcher_6_xGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6LCatcher_YGe"), values.booleanText("lCatcher_6_yGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6LCatcher_VGe"), values.booleanText("lCatcher_6_VGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6LCatcher_cupDetect"), values.booleanText("lCatcher_6_cupDetect", "有杯", "无杯"));
    labels.insert(QStringLiteral("lbl6LCatcher_blenderState"), values.booleanText("lCatcher_6_blenderState", "工作中", "空闲"));
    labels.insert(QStringLiteral("lbl6LCatcher_heatPatchTemp"), values.number("temperatureTestWare", 'f', 1));
    labels.insert(QStringLiteral("lbl6LCatcher_testPlateTemp"), values.number("temperatureTPanel", 'f', 1));
    labels.insert(QStringLiteral("lbl6LCatcher_steadyState"), values.booleanText("isTempReady", "不稳定", "稳定"));
    labels.insert(QStringLiteral("lbl6LCatcher_blenderV"), values.number("lCatcher_blenderV", 'f', 2));
    labels.insert(QStringLiteral("lblRNeedle1_Error"), values.booleanText("rNeedle1_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lblRNeedle1_HGe"), values.booleanText("rNeedle1_6_HGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRNeedle1_VGe"), values.booleanText("rNeedle1_6_VGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRNeedle1_PGe"), values.booleanText("rNeedle1_6_pumpGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRNeedle1_pumpPressure"), values.number("rNeedle1_6_pumpPressure", 'f', 1));
    labels.insert(QStringLiteral("lblRNeedle1_ballState"), values.booleanText("rNeedle1_6_ballState", "水满", "正常"));
    labels.insert(QStringLiteral("lblRNeedle1_liquidSignal"), values.booleanText("rNeedle1_6_liquidSignal", "1", "0"));
    labels.insert(QStringLiteral("lblRNeedle1_pinSignal"), values.booleanText("rNeedle1_6_pinState", "撞针", "正常"));
    labels.insert(QStringLiteral("lblRNeedle1_pumpControl"), values.booleanText("rNeedle1_6_pumpControl", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblRNeedle1_sensor"), values.booleanText("rNeedle1_6_liquidSensor", "1", "0"));
    labels.insert(QStringLiteral("lblRNeedle1_solenoidValve"), values.booleanText("rNeedle1_6_solenoidValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lblRNeedle1_cleaningMinusValve"), values.booleanText("rNeedle1_6_cleaningMinusValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lblRNeedle1_cleaningPumpValve"), values.booleanText("rNeedle1_6_cleaningPumpValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lblLNeedle1_Ltemp"), values.number("temperatureRNeedleLeft", 'f', 1));
    labels.insert(QStringLiteral("lblRNeedle2_Error"), values.booleanText("rNeedle2_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lblRNeedle2_HGe"), values.booleanText("rNeedle2_6_HGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRNeedle2_VGe"), values.booleanText("rNeedle2_6_VGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRNeedle2_pumpGe"), values.booleanText("rNeedle2_6_pumpGe", "在位", "不在位"));
    labels.insert(QStringLiteral("lblRNeedle2_minusPumpPressure"), values.number("rNeedle2_6_minusPumpPressure", 'f', 1));
    labels.insert(QStringLiteral("lblRNeedle2_ballState"), values.booleanText("rNeedle2_6_ballState", "水满", "正常"));
    labels.insert(QStringLiteral("lblRNeedle2_liquidSignal"), values.booleanText("rNeedle2_6_liquidSignal", "1", "0"));
    labels.insert(QStringLiteral("lblRNeedle2_pinSignal"), values.booleanText("rNeedle2_6_pinState", "撞针", "正常"));
    labels.insert(QStringLiteral("lblRNeedle2_minusPumpControl"), values.booleanText("rNeedle2_6_minusPumpControl", "工作中", "空闲"));
    labels.insert(QStringLiteral("lblRNeedle2_sensor"), values.booleanText("rNeedle2_6_liquidSensor", "1", "0"));
    labels.insert(QStringLiteral("lblRNeedle2_solenoidValve"), values.booleanText("rNeedle2_6_solenoidValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lblRNeedle2_cleaningMinusValve"), values.booleanText("rNeedle2_6_cleaningMinusValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lblRNeedle2_cleaningPumpValve"), values.booleanText("rNeedle2_6_cleaningPumpValve", "开启", "关闭"));
    labels.insert(QStringLiteral("lblRNeedle2_Rtemp"), values.number("temperatureRNeedleRight", 'f', 1));
    labels.insert(QStringLiteral("lbl6SCatcher_wasteBox"), values.booleanText("normalWasteCupArrive_SS", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6Rail_Error"), values.booleanText("rail_6_error", "异常", "正常"));
    labels.insert(QStringLiteral("lbl6Rail_Ge"), values.booleanText("rail_6_Ge", "在位", "不在位"));
    labels.insert(QStringLiteral("lbl6Rail_plateGe"), values.booleanText("rail_6_plateGe", "在位", "不在位"));
}

QString stateText(const QStringList &names, int state)
{
    return state >= 0 && state < names.size() ? names.at(state) : QStringLiteral("未知");
}

QVector<SensorDisplay> sensorValues(const Values &values, bool sn600, bool engineer)
{
    QVector<SensorDisplay> sensors;
    const auto append = [&sensors, &values](const char *name, const char *field,
            const char *positive, const char *negative, bool normalWhenTrue)
    {
        const bool known = values.has(field);
        sensors.append({QString::fromUtf8(name), values.booleanText(field, positive, negative),
                        known, known && values.flag(field) == normalWhenTrue});
    };
    const auto appendLiquid = [&sensors, &values](const char *name, const char *upper, const char *lower)
    {
        const bool known = values.has(upper) && values.has(lower);
        const bool up = values.flag(upper), low = values.flag(lower);
        const QString text = !known ? QStringLiteral("未上报") : up && low ? QStringLiteral("正常")
            : !up && low ? QStringLiteral("溢出") : up && !low ? QStringLiteral("缺少") : QStringLiteral("异常");
        sensors.append({QString::fromUtf8(name), text, known, known && up && low});
    };
    // 正常/异常颜色的判断沿用原slotStatusChanged，不把实时光耦当作滤波后的状态。
    if (sn600) appendLiquid("清洗液桶:", "normalCleanBufferUp", "normalCleanBufferLow");
    else append("清洗液桶:", "normalCleanWater", "充足", "缺少", true);
    append("废液桶:", "normalWasteWater", "未满", "已满", true);
    append("反应杯舱:", "normalCupBig", "充足", "缺杯", true);
    append("废杯状态:", "normalWasteCup", "未满", "已满", true);
    append("丢杯桶:", "normalWasteCupArrive", "已放置", "未放置", true);
    appendLiquid("制冷液:", "normalColdWaterUp", "normalColdWaterLow");
    if (!sn600) appendLiquid("清洗液瓶:", "normalCleanBufferUp", "normalCleanBufferLow");
    append("样本容器检测:", "hasSampleCup", "有杯", "无杯", false);
    if (engineer)
    {
        append("进架区域:", "isRackInEmpty", "无架", "有架", false);
        append("出架区域:", "isRackOutFull", "已满", "未满", false);
        append("进架就位:", "isRackInArrive", "就位", "未就位", false);
        append("加样架对射右:", "isRackAdd1in", "遮挡", "未遮挡", false);
        append("加样架对射左:", "isRackAdd2out", "遮挡", "未遮挡", false);
        append("退架就位:", "isRackOutArrive", "就位", "未就位", false);
        if (sn600)
        {
            append("导轨左反射:", "normalCupUp", "遮挡", "未遮挡", true);
            append("导轨落杯位:", "normalCupLow", "有杯", "缺杯", true);
        }
        else
        {
            append("样本针废液浮球:", "normalWasteWaterBufferS", "正常", "异常", true);
            append("试剂针废液浮球:", "normalWasteWaterBufferR", "正常", "异常", true);
            append("供杯导轨上光耦:", "normalCupUp", "遮挡", "未遮挡", true);
            append("导轨杯状态:", "normalCupLow", "有杯", "缺杯", true);
            append("选杯料斗上光耦:", "normalCupSamllUp", "遮挡", "未遮挡", true);
            append("选杯料斗下光耦:", "normalCupSamllLow", "遮挡", "未遮挡", true);
            append("新杯位状态:", "normalCupCheck_SS", "无杯", "有杯", false);
        }
        append("开始按钮状态:", "StartKey_SS", "按下", "弹起", false);
        append("试剂盘按钮状态:", "PlateKey_SS", "按下", "弹起", false);
    }
    if (sn600) append("急诊架按钮状态:", "EmgcStopKey_SS", "按下", "弹起", false);
    return sensors;
}
}

StatusDisplay statusDisplay(const Sn100Protocol::StatusSnapshot &snapshot, int role)
{
    const Values values = readValues(snapshot);
    StatusDisplay display;
    display.sn600 = snapshot.model == QStringLiteral("SN600");
    auto &labels = display.labels;
    commonLabels(labels, values);
    // 原BoradStatus_flush仅SN450进入第一分支；SN500同其余机型进入第二分支。
    if (snapshot.model == QStringLiteral("SN450")) sn450Labels(labels, values);
    else otherModelLabels(labels, values);
    const QString main = stateText({"空闲", "育温中", "准备就绪", "初始化中", "初始化完成", "测试中", "暂停"}, snapshot.mainStatus);
    const QString led = stateText({"未连接", "未测试", "未测试仪器动作", "未测试异常", "测试全部正常", "测试终止", "测试终止试剂", "测试异常终止"}, snapshot.ledStatus);
    const QString error = stateText({"无错误", "警告", "进架暂停", "进架立即停止", "暂停加样", "加样立即停止", "停止加试剂", "停止所有"}, snapshot.errorWorkType);
    for (const char *name : {"lbl6Main_emergencyCover_2", "lbl6Main_emergencyCover_7"}) labels.insert(QString::fromLatin1(name), main);
    for (const char *name : {"lbl6Main_startButton_3", "lbl6Main_startButton_5"}) labels.insert(QString::fromLatin1(name), led);
    for (const char *name : {"lbl6Main_plateButton_5", "lbl6Main_plateButton_9"}) labels.insert(QString::fromLatin1(name), error);
    for (const char *name : {"lbl6Main_startButton", "lbl6Main_sampleScaner_3"})
        labels.insert(QString::fromLatin1(name), values.booleanText("StartKey_SS", "按下", "弹起"));
    for (const char *name : {"lbl6Main_plateButton", "lbl6Main_plateButton_6"})
        labels.insert(QString::fromLatin1(name), values.booleanText("PlateKey_SS", "按下", "弹起"));
    for (const char *name : {"lbl6Main_plateButton_4", "lbl6Main_plateButton_7"})
        labels.insert(QString::fromLatin1(name), values.booleanText("EmgcStopKey_SS", "按下", "弹起"));
    const QString newProtocol = !values.has("protocolMarker") ? QStringLiteral("未上报")
        : values.text("protocolMarker") == QStringLiteral("LinuxNew") ? QStringLiteral("是") : QStringLiteral("否");
    for (const char *name : {"label_isNew", "label_isNew_2"}) labels.insert(QString::fromLatin1(name), newProtocol);
    for (const char *name : {"lbl6Main_subSystemCount", "lbl6Main_subSystemCount_3"})
        labels.insert(QString::fromLatin1(name), QString::number(snapshot.subsystems.size()));
    labels.insert(QStringLiteral("lblDriverModel"), snapshot.model);
    labels.insert(QStringLiteral("lblDriverNo"), values.text("driverNumber"));
    const QStringList versions = values.text("driverVersions").split('|');
    labels.insert(QStringLiteral("lblVersionZk"), role >= 5 ? versions.join(' ') : versions.value(0));
    if (values.has("doorRPanelNormal")) display.coverNormal.insert(QStringLiteral("lblReagentOpen"), values.flag("doorRPanelNormal"));
    if (values.has("doorIBSNormal")) display.coverNormal.insert(QStringLiteral("lblDuilentOpen"), values.flag("doorIBSNormal"));
    if (values.has("doorEmergencyNormal")) display.coverNormal.insert(QStringLiteral("lblEmergencyOpen"), values.flag("doorEmergencyNormal"));
    display.sensors = sensorValues(values, display.sn600, role >= 4);
    return display;
}
}

