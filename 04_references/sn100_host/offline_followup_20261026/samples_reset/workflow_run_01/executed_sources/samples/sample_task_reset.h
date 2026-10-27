#ifndef SN100_SAMPLE_TASK_RESET_H
#define SN100_SAMPLE_TASK_RESET_H
#include <QList>
#include <QString>

// 原 SN100 每次重复测试各有状态，不能用一个项目状态代替全部重复次数。
// 这里只准备本地草稿；没有仪器 taskID、报告行号、设备命令或数据库副作用。
enum class SampleTaskState
{
    Idle = 0,
    Reset = 1,
    Testing = 2,
    Finished = 3,
    Error = 4
};
enum class SampleHoleState
{
    None,
    Ready,
    Testing,
    Finished,
    Error
};
enum class SampleTaskResetAction
{
    ResetErrors,
    RetestSample
};
struct SampleTaskResetDraft
{
    QList<SampleTaskState> taskStates;
    bool hasCalibrationTests = false;
    bool revise = true; // 原 revise 表示该孔位任务可以修改/再次提交，不能解释成设备已复位。
};
struct SampleTaskResetResult
{
    bool accepted = false;
    SampleTaskResetDraft draft;
    int changedTests = 0;
    QString message;
};

// 调用者仍须完成身份/忙态、当前行及批量选择检查，再自行决定保存或放弃草稿。
// 失败返回未经修改的输入。校准测试不在本切片内，不替其删除或重建任务。
SampleTaskResetResult prepareSampleTaskReset(const SampleTaskResetDraft &draft,
                                           SampleTaskResetAction action);
SampleHoleState sampleHoleState(const QList<SampleTaskState> &taskStates);
#endif
