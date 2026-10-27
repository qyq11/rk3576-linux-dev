#include "sample_task_reset.h"
#include <QtTest>

class SampleTaskResetTest : public QObject
{
    Q_OBJECT
  private slots:
    void errorResetPreservesFinishedAndIdle()
    {
        SampleTaskResetDraft input;
        input.revise = false;
        input.taskStates = {SampleTaskState::Error, SampleTaskState::Finished,
                            SampleTaskState::Idle, SampleTaskState::Reset};
        const auto result = prepareSampleTaskReset(input, SampleTaskResetAction::ResetErrors);
        QVERIFY(result.accepted);
        QVERIFY(result.draft.revise);
        QCOMPARE(result.changedTests, 2);
        QVERIFY(result.draft.taskStates == QList<SampleTaskState>({SampleTaskState::Idle,
            SampleTaskState::Finished, SampleTaskState::Idle, SampleTaskState::Idle}));
        QVERIFY(!input.revise);
        QVERIFY(input.taskStates.first() == SampleTaskState::Error);
    }
    void retestResetsAllOrdinaryStates()
    {
        SampleTaskResetDraft input;
        input.revise = false;
        input.taskStates = {SampleTaskState::Finished, SampleTaskState::Error,
                            SampleTaskState::Reset, SampleTaskState::Idle};
        const auto result = prepareSampleTaskReset(input, SampleTaskResetAction::RetestSample);
        QVERIFY(result.accepted);
        QVERIFY(result.draft.revise);
        QCOMPARE(result.changedTests, 3);
        QVERIFY(result.draft.taskStates == QList<SampleTaskState>({SampleTaskState::Idle,
            SampleTaskState::Idle, SampleTaskState::Idle, SampleTaskState::Idle}));
        QVERIFY(input.taskStates.first() == SampleTaskState::Finished);
    }
    void mixedTestingRefusesWholeHoleForBothActions()
    {
        for (auto action : {SampleTaskResetAction::ResetErrors, SampleTaskResetAction::RetestSample})
        {
            SampleTaskResetDraft input;
            input.revise = false;
            input.taskStates = {SampleTaskState::Error, SampleTaskState::Testing, SampleTaskState::Finished};
            const auto result = prepareSampleTaskReset(input, action);
            QVERIFY(!result.accepted);
            QVERIFY(result.draft.taskStates == input.taskStates);
            QVERIFY(!result.draft.revise);
            QCOMPARE(result.changedTests, 0);
            QVERIFY(result.message.contains(QStringLiteral("测试状态")));
        }
    }
    void calibrationAndUnknownStatesPreserveInput()
    {
        SampleTaskResetDraft input;
        input.revise = false;
        input.taskStates = {SampleTaskState::Error};
        input.hasCalibrationTests = true;
        for (auto action : {SampleTaskResetAction::ResetErrors, SampleTaskResetAction::RetestSample})
        {
            const auto result = prepareSampleTaskReset(input, action);
            QVERIFY(!result.accepted);
            QVERIFY(result.draft.hasCalibrationTests);
            QVERIFY(!result.draft.revise);
            QVERIFY(result.draft.taskStates == input.taskStates);
        }
        input.hasCalibrationTests = false;
        input.taskStates = {static_cast<SampleTaskState>(-1), SampleTaskState::Error};
        auto result = prepareSampleTaskReset(input, SampleTaskResetAction::ResetErrors);
        QVERIFY(!result.accepted);
        QVERIFY(result.draft.taskStates == input.taskStates);
        QVERIFY(!result.draft.revise);
        input.taskStates = {SampleTaskState::Error};
        result = prepareSampleTaskReset(input, static_cast<SampleTaskResetAction>(9));
        QVERIFY(!result.accepted);
        QVERIFY(result.draft.taskStates == input.taskStates);
    }
    void holePriorityMatchesOriginalSn100()
    {
        QVERIFY(sampleHoleState({}) == SampleHoleState::None);
        QVERIFY(sampleHoleState({SampleTaskState::Reset}) == SampleHoleState::Ready);
        QVERIFY(sampleHoleState({SampleTaskState::Reset, SampleTaskState::Finished}) == SampleHoleState::Finished);
        QVERIFY(sampleHoleState({SampleTaskState::Finished, SampleTaskState::Error}) == SampleHoleState::Error);
        QVERIFY(sampleHoleState({SampleTaskState::Error, SampleTaskState::Idle}) == SampleHoleState::Ready);
        QVERIFY(sampleHoleState({SampleTaskState::Idle, SampleTaskState::Testing}) == SampleHoleState::Testing);
    }
    void everyThreeRepetitionCombinationHonorsContract()
    {
        // 每次重复测试独立；5^3组混合状态同时验证“有一个Testing就整孔拒绝”。
        for (int encoded = 0; encoded < 125; ++encoded)
        {
            SampleTaskResetDraft input;
            input.revise = false;
            int value = encoded;
            for (int i = 0; i < 3; ++i)
            {
                input.taskStates.append(static_cast<SampleTaskState>(value % 5));
                value /= 5;
            }
            for (auto action : {SampleTaskResetAction::ResetErrors, SampleTaskResetAction::RetestSample})
            {
                const auto result = prepareSampleTaskReset(input, action);
                if (input.taskStates.contains(SampleTaskState::Testing))
                {
                    QVERIFY(!result.accepted);
                    QVERIFY(result.draft.taskStates == input.taskStates);
                    QVERIFY(!result.draft.revise);
                    continue;
                }
                QVERIFY(result.accepted);
                QVERIFY(result.draft.revise);
                QCOMPARE(result.draft.taskStates.size(), input.taskStates.size());
                for (int i = 0; i < input.taskStates.size(); ++i)
                {
                    const auto before = input.taskStates[i];
                    if (action == SampleTaskResetAction::ResetErrors && before == SampleTaskState::Finished)
                    {
                        QVERIFY(result.draft.taskStates[i] == SampleTaskState::Finished);
                    }
                    else
                    {
                        QVERIFY(result.draft.taskStates[i] == SampleTaskState::Idle);
                    }
                }
            }
        }
    }
    void emptyAndAlreadyIdleStillPrepareRevise()
    {
        SampleTaskResetDraft input;
        input.revise = false;
        const auto empty = prepareSampleTaskReset(input, SampleTaskResetAction::ResetErrors);
        QVERIFY(empty.accepted && empty.draft.revise && empty.draft.taskStates.isEmpty());
        QCOMPARE(empty.changedTests, 0);
        input.taskStates = {SampleTaskState::Idle};
        const auto idle = prepareSampleTaskReset(input, SampleTaskResetAction::RetestSample);
        QVERIFY(idle.accepted && idle.draft.revise);
        QCOMPARE(idle.changedTests, 0);
    }
};
QTEST_APPLESS_MAIN(SampleTaskResetTest)
#include "sample_task_reset_test.moc"
