#ifndef SN100_ORIGINAL_LIS_APPLICATION_PLAN_H
#define SN100_ORIGINAL_LIS_APPLICATION_PLAN_H

#include "original_lis_download_types.h"
#include <QDateTime>
#include <QMap>

namespace Sn100Lis
{
// 新本地资源预算，不是原协议/临床范围，也不是进程RSS上限。
constexpr int OriginalLisMaximumMappingProjects = 4096;
constexpr int OriginalLisMaximumMappingItems = 16384; // 结果代码和稀释项合计。
constexpr int OriginalLisMaximumPlannedTasks = 4096;
constexpr qint64 OriginalLisMaximumPlanInputBytes = 4 * 1048576;
constexpr qint64 OriginalLisMaximumPlanOutputBytes = 4 * 1048576;

enum class OriginalLisPlanState { Rejected, Prepared };
enum class OriginalLisHoleTaskState { None, Ready, Testing, Finished, Error };

// 仅包含原DSR接收器会写的患者字段；其他报告字段由拥有者原样保留。
struct OriginalLisPatientValue
{
    QString bedNumber, name;
    int sex = -1; // 原Unknown=-1、Man=0、Woman=1。
    qint32 age = 0, sampleNumber = 0;
    QString barcode;
    QDateTime registrationTime; // 原无格式fromString结果，可无效，不补当前时间。
    QString remark, checker, department;
};

struct OriginalLisHoleValue
{
    QString originalRoot, rackInstance, rack;
    int hole = 0;
    OriginalLisPatientValue patient;
    QString internalSampleNumber, barcode; // 与patient中的sn/条码不是同一赋值阶段。
    int sampleType = 0; // 原普通0、校准1、质控2。
    int needleType = 1; // 原FullNeedle=0、MicroNeedle=1。
    int bottleType = 1; // 原Tube=0、MicroCup=1；调用方传旧实际值，不采用此默认作来源。
    OriginalLisHoleTaskState taskState = OriginalLisHoleTaskState::None;
};

struct OriginalLisMappingRate
{
    QString rate;
    int defaultOption = 0; // 保留原toInt后是否非零，不按启用状态改选。
};

// 调用方按原项目遍历顺序提供完整映射集合，不预滤enabled。
struct OriginalLisMappingProject
{
    qint64 projectId = 0;
    QString name; // 原project_name，供DSR名称/代码映射。
    QString informationName; // 原projectBase[project_en_name]，供随后getProjectInformation首查。
    QString originalFile, originalFingerprint;
    QStringList resultLisCodes;
    QList<OriginalLisMappingRate> sampleRates;
};
using OriginalLisMappingProjects = QList<OriginalLisMappingProject>;

struct OriginalLisPatientChanges
{
    bool bedNumber = false, name = false, sex = false, age = false, sampleNumber = false;
    bool barcode = false, registrationTime = false, remark = false, checker = false, department = false;
};

struct OriginalLisHoleChanges
{
    bool sampleType = false, needleType = false, bottleType = false;
    bool internalSampleNumber = false, barcode = false, taskState = false;
};

struct OriginalLisPlannedTask
{
    qint64 projectId = 0;
    QString projectName, originalFile, originalFingerprint, defaultRate;
    bool projectInformationFound = false; // 原按名字查不到会用空ProjectInformation，不改为拒绝。
    int repeats = 1; // 单个标量；原OBR-9转换，不截99，不据此分配列表或任务号。
    // 原每次测试均Idle；实际展开和资源准入仍归任务拥有者。
};

struct OriginalLisApplicationPlan
{
    OriginalLisPlanState state = OriginalLisPlanState::Rejected;
    ConfigurationIdentity configuration;
    ReceiveProfilePtr profile;
    quint64 generation = 0, peer = 0;
    QString controlId;
    OriginalLisHoleValue after;
    OriginalLisPatientChanges patientChanged;
    OriginalLisHoleChanges holeAssigned, holeChanged;
    // Prepared时始终替换这些患者字段及整份任务；空任务也必须替换，最终None。
    bool assignPatientFields = false, replaceAllTasks = false;
    // 只描述未来原顺序，不证明内存已应用/MAX已发生，也不授权生成/发送ACK。
    bool describeMaxAfterMemory = false, describeAckRequestAfterMax = false;
    QMap<QString, OriginalLisPlannedTask> tasks; // 原sampleTest按QString项目名排序。
    int ignoredUnknownProjects = 0, ignoredDuplicateProjects = 0;
    qint64 scalarRepeatTotal = 0;
    qint64 inputTextBytes = 0, outputTextBytes = 0;
    OriginalLisFieldError error;
};

// 输入须为冻结codec未经改写的Ready/FieldsReady及旧孔/有序项目值。
// 本函数只准备变化计划，不核实时首匹配/R1/claim、连接、SQL或应用资格。
OriginalLisApplicationPlan buildOriginalLisApplicationPlan(const OriginalLisDsrFields &decoded,
    const OriginalLisHoleValue &oldHole, const OriginalLisMappingProjects &projects);
}

#endif
