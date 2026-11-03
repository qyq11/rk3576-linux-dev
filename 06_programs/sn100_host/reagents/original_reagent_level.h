#ifndef SN100_ORIGINAL_REAGENT_LEVEL_H
#define SN100_ORIGINAL_REAGENT_LEVEL_H

#include "reagents/original_reagent_repository.h"
#include "projects/projects_store.h"
#include "original_files/original_reagent_capacity.h"
#include <functional>
#include <limits>

namespace OriginalLevels
{
enum class PacketKind { Reagent, SampleIgnored };
enum class MaximumSource { Unknown, InitialBottleByName, ConfirmedUr, ManualHoleSave, FilledHole, ClearedHole };

struct ReceivedPacket
{
    PacketKind kind = PacketKind::Reagent;
    bool linuxNew = false;
    QByteArray payload;
    QList<QByteArray> rawFields;
    QStringList fields;
    QString hole, reagent, area, bottle, sendIndex;
    QString reportedDiameter, reportedAngle, reportedVolume;
    int needle = 0;
    double rawHeightMm = std::numeric_limits<double>::quiet_NaN();
};

// 原控件的独立运行快照，不能从孔库max_ul或整数显示模型冒充。
// 来源、三库版本及revision由提供者核实；Unknown/不可用时不允许N写库。
struct CurrentHoleRuntime
{
    bool available = false;
    QString root, hole, reagent, bottle, area, reagentType, state;
    QString settingFingerprint, bottleFingerprint, informationFingerprint;
    quint64 revision = 0;
    MaximumSource maximumSource = MaximumSource::Unknown;
    double maximumMicrolitres = std::numeric_limits<double>::quiet_NaN();
    double amountMicrolitres = std::numeric_limits<double>::quiet_NaN();
    double usedMicrolitres = std::numeric_limits<double>::quiet_NaN();
};

struct Context
{
    quint64 packetId = 0; // 本地接收序号，不是N.sendIndex或设备请求号。
    quint64 connection = 0; // transport到达时的原代次，不在后台重贴当前代次。
    quint64 permissionRevision = 0;
    QString root, actualModel, configuredModel, themeFingerprint, protocolToken;
    QString capacityRelativeFile, capacityFingerprint;
    OriginalRuntime::ReagentCapacityFileState capacityFileState = OriginalRuntime::ReagentCapacityFileState::Unknown;
    bool linuxNew = false;
    int configuredPositions = 0, reportedPositions = 0, mainStatus = -1;
    bool capacityCalibrationActive = false;
};

struct ProjectSnapshot
{
    bool orderVerified = false;
    ProjectRecords records; // 原全部项目及每个试剂步骤，已按原加载顺序排列。
    QMap<QString,QString> versions; // project/<原文件名>及实际用于排序的config.ini。
};

struct PreparedObservation
{
    Context context;
    ReceivedPacket received;
    CurrentHoleRuntime before, after;
    OriginalReagentState observed;
    ProjectSnapshot projects;
    OriginalRuntime::ReagentCapacityInput capacityInput;
    OriginalRuntime::ReagentCapacityResult capacity;
    qint64 holeId = 0;
    QString formulaText, amountText, previousUsedText, usedText;
    int amountInteger = 0;
    bool updateUsed = false;
    ReagentTestCount selectedHoleTests;
};

bool decodePacket(const QByteArray &payload, bool linuxNew, ReceivedPacket *result, QString *message);
// 沿原项目外层排序与project_reagent无ORDER BY读取；只读，不调用检测计算。
bool readProjectSnapshot(const QString &root, ProjectSnapshot *result, QString *message);
// 只准备换算和原字段/显示变化，不发送或写库；runtime及项目顺序未核实时明确拒绝。
bool prepareObservation(const OriginalReagentState &state, const ReceivedPacket &packet,
                        const Context &context, const CurrentHoleRuntime &runtime,
                        const ProjectSnapshot &projects, PreparedObservation *result, QString *message);
// 只写formula_level/amount_uL，以及非Testing时的used_ul；已提交事实与返回成功分开。
bool saveObservation(const PreparedObservation &prepared, const std::function<bool()> &stillCurrent,
                     bool *committed, QString *savedFingerprint, QString *message);
}

Q_DECLARE_METATYPE(OriginalLevels::Context)
Q_DECLARE_METATYPE(OriginalLevels::CurrentHoleRuntime)

#endif
