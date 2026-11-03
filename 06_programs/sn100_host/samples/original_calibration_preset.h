#ifndef SN100_ORIGINAL_CALIBRATION_PRESET_H
#define SN100_ORIGINAL_CALIBRATION_PRESET_H
#include "sample_types.h"
#include "projects/projects_store.h"

namespace OriginalCalibrationPreset
{
bool available(const ProjectRecord &project);
bool defaults(const ProjectRecord &project, int firstHole, int holeCount,
              SampleCalibrationPreset *preset, QString *message);
// 原界面输入转为原存储单位，只准备任务，不拟合、不反算。
bool setConcentrations(const ProjectRecord &project, const ProjectUnits &units,
                       const QStringList &displayValues, SampleCalibrationPreset *preset, QString *message);
bool validate(const ProjectRecord &project, const SampleCalibrationPreset &preset,
              int holeCount, QString *message);
bool apply(const ProjectRecord &project, const SampleCalibrationPreset &preset,
           SampleRack *rack, QString *message);
}
#endif
