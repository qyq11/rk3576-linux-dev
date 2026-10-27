#include "sample_task_reset.h"

SampleHoleState sampleHoleState(const QList<SampleTaskState> &taskStates)
{
    if (taskStates.isEmpty())
    {
        return SampleHoleState::None;
    }
    // 原 S_SampleHoleInfo::setStateFromTaskState 的优先顺序；Reset-only 仍是 Ready。
    if (taskStates.contains(SampleTaskState::Testing))
    {
        return SampleHoleState::Testing;
    }
    if (taskStates.contains(SampleTaskState::Idle))
    {
        return SampleHoleState::Ready;
    }
    if (taskStates.contains(SampleTaskState::Error))
    {
        return SampleHoleState::Error;
    }
    if (taskStates.contains(SampleTaskState::Finished))
    {
        return SampleHoleState::Finished;
    }
    return SampleHoleState::Ready;
}

SampleTaskResetResult prepareSampleTaskReset(const SampleTaskResetDraft &draft,
                                           SampleTaskResetAction action)
{
    SampleTaskResetResult result;
    result.draft = draft;
    if (action != SampleTaskResetAction::ResetErrors && action != SampleTaskResetAction::RetestSample)
    {
        result.message = QStringLiteral("样本复位操作不支持，草稿未改动。");
        return result;
    }
    for (SampleTaskState state : draft.taskStates)
    {
        if (state != SampleTaskState::Idle && state != SampleTaskState::Reset &&
            state != SampleTaskState::Testing && state != SampleTaskState::Finished &&
            state != SampleTaskState::Error)
        {
            result.message = QStringLiteral("测试状态未知，草稿保留，请先核对来源。");
            return result;
        }
    }
    if (sampleHoleState(draft.taskStates) == SampleHoleState::Testing)
    {
        result.message = action == SampleTaskResetAction::ResetErrors
                             ? QStringLiteral("无法复位错误状态: 该孔位正处于测试状态!")
                             : QStringLiteral("无法复测: 该孔位正处于测试状态!");
        return result;
    }
    if (draft.hasCalibrationTests)
    {
        result.message = QStringLiteral("校准测试不支持本地样本复位/复测，草稿未改动。");
        return result;
    }
    for (SampleTaskState &state : result.draft.taskStates)
    {
        // 原复测按钮会重置普通样本的全部测试，不只是 Finished；错误复位只处理 Error/Reset。
        const bool reset = action == SampleTaskResetAction::RetestSample ||
                           state == SampleTaskState::Error || state == SampleTaskState::Reset;
        if (reset && state != SampleTaskState::Idle)
        {
            state = SampleTaskState::Idle;
            ++result.changedTests;
        }
    }
    result.draft.revise = true;
    result.accepted = true;
    result.message = QStringLiteral("仅已准备本地修改草稿；保存、设备下发及执行另行处理。");
    return result;
}
