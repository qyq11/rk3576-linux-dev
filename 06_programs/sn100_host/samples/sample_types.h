#ifndef SN100_SAMPLE_TYPES_H
#define SN100_SAMPLE_TYPES_H
#include <QList>
#include <QMetaType>
#include <QString>
#include <QDateTime>
#include "sample_task_reset.h"
#include "sample_qc_protocol.h"

// 孔内患者暂存值，对照原ReportItemData；这些id只是原item值，不表示本片已占号。
struct SamplePatientItem
{
    qint32 id = 0, rowId = 0, sampleNumber = 0, statue = 0, age = 0;
    int reportType = 0, sex = -1;
    bool uploaded = false, isAutoId = false, visible = false;
    QDateTime dateTime;
    QString sampleHole = QStringLiteral(""), barcode = QStringLiteral("");
    QString name = QStringLiteral(""), department = QStringLiteral("");
    QString patientType = QStringLiteral(""), applicationSerial = QStringLiteral("");
    QString bedNumber = QStringLiteral(""), jinayan = QStringLiteral("");
    QString checker = QStringLiteral(""), auditor = QStringLiteral("");
    QString remark = QStringLiteral(""), state = QStringLiteral("");
};

// 原LIS等待阶段，与孔任务状态和设备已执行的含义分开。
enum class SampleLisPhase { None = 0, T1 = 1, R1 = 2, T2 = 3, Max = 4 };
enum class SampleTaskSource { ManualPreset, OriginalLisDownload };

// 本地预设及LIS值，没有本片生成的设备taskID/报告号。
struct SampleTask
{
    qint64 projectId = 0;
    QString rate;
    int repeats = 1;
    bool prolonged = false;
    QList<SampleTaskState> states; // 空列表兼容旧本地预设：每次重复均为Idle，不代表设备状态。
    int calibrationPoint = 0; // 校准点从1开始；普通/质控为0，不是报告行号。
    QString calibrationConcentration; // 原单位换算后的浓度文字；不是测量值或计算结果。
    SampleTaskSource source = SampleTaskSource::ManualPreset;
    QString lisProjectName, lisProjectFile, lisProjectFingerprint;
    bool lisProjectInformationFound = false; // 原未找到资料的id0/空稀释方案也要保留。
};
struct SampleCalibrationPreset
{
    qint64 projectId = 0;
    QString groupToken; // 只连接同一份校准草稿的多个孔；原校准编号在任务占号时取得。
    QString calibratorBatch;
    QString reagentBatchGroup = QStringLiteral("Default");
    bool autoDilution = false;
    int pointCount = 0;
    int repeatCount = 0;
    QString baselineConcentration;
    QStringList rates;
    QStringList concentrations;
    QList<int> holes;
};
struct SampleHole
{
    int number = 1;
    QString sampleId;
    // 原S_SampleHoleInfo的孔位标志，独立于patient.isAutoId；LIS清患者不清此值。
    bool isAutoId = true;
    int container = 1; // Original Tube=0 / MicroCup=1; source default is MicroCup.
    QList<SampleTask> tasks;
    bool revise = true;
    int sampleType = 0; // 原普通=0、校准=1、质控=2；校准仅用于原资料模式。
    SampleQcIdentity quality;
    SampleCalibrationPreset calibration;
    SamplePatientItem patient;
    QString sampleBarcode = QStringLiteral(""); // 孔扫码码，不能与内部sampleId或patient.barcode合并。
    int needleType = 1; // 原MicroNeedle=1；与原容器默认选择无关。
    QString lisControlId = QStringLiteral("");
    SampleLisPhase lisPhase = SampleLisPhase::None;
    QDateTime lisStartedAt = QDateTime::currentDateTime();
    int lisIntervalMsecs = 60000;
};
inline QList<SampleTaskState> sampleTaskStates(const SampleTask &task)
{
    if (!task.states.isEmpty())
    {
        return task.states;
    }
    QList<SampleTaskState> states;
    for (int i = 0; i < task.repeats; ++i)
    {
        states.append(SampleTaskState::Idle);
    }
    return states;
}
inline QList<SampleTaskState> sampleHoleTaskStates(const SampleHole &hole)
{
    QList<SampleTaskState> states;
    for (const SampleTask &task : hole.tasks)
    {
        states.append(sampleTaskStates(task));
    }
    return states;
}
struct SampleRack
{
    qint64 id = 0;
    QString name;
    bool emergency = false;
    QList<SampleHole> holes;
    int remainingSeconds = -1; // TB实际上报的秒数；-1表示未上报，不能当作0。
    qint64 remainingAtMs = 0; // 同一系统单调时钟，不受校时影响。
};
enum class SampleCombinationMode
{
    Ordinary = 0,
    MultiReagentBottle = 1 // 原“多瓶质控”按试剂瓶展开，不表示高低水平质控品。
};
struct SampleCombination
{
    qint64 id = 0;
    QString name;
    QList<qint64> orderedProjectIds; // Repetitions are meaningful; never deduplicate.
    SampleCombinationMode mode = SampleCombinationMode::Ordinary;
};
struct SampleSnapshot
{
    QList<SampleRack> racks;
    QList<SampleCombination> combinations;
};
Q_DECLARE_METATYPE(SampleTask)
Q_DECLARE_METATYPE(SampleHole)
Q_DECLARE_METATYPE(SampleRack)
Q_DECLARE_METATYPE(SampleCombination)
Q_DECLARE_METATYPE(SampleSnapshot)
#endif
