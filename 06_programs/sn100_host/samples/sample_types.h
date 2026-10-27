#ifndef SN100_SAMPLE_TYPES_H
#define SN100_SAMPLE_TYPES_H
#include <QList>
#include <QMetaType>
#include <QString>
#include "sample_task_reset.h"
#include "sample_qc_protocol.h"

// These are local presets. There is deliberately no instrument task/report ID.
struct SampleTask
{
    qint64 projectId = 0;
    QString rate;
    int repeats = 1;
    bool prolonged = false;
    QList<SampleTaskState> states; // 空列表兼容旧本地预设：每次重复均为Idle，不代表设备状态。
};
struct SampleHole
{
    int number = 1;
    QString sampleId;
    int container = 1; // Original Tube=0 / MicroCup=1; source default is MicroCup.
    QList<SampleTask> tasks;
    bool revise = true;
    int sampleType = 0; // 原TypeSample=0/TypeQuality=2；不在此开放校准样本。
    SampleQcIdentity quality;
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
