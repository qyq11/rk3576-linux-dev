#ifndef SN100_SAMPLE_TYPES_H
#define SN100_SAMPLE_TYPES_H
#include <QList>
#include <QMetaType>
#include <QString>
#include "sample_task_reset.h"

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
struct SampleCombination
{
    qint64 id = 0;
    QString name;
    QList<qint64> orderedProjectIds; // Repetitions are meaningful; never deduplicate.
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
