#ifndef SN100_ORIGINAL_REAGENT_CAPACITY_H
#define SN100_ORIGINAL_REAGENT_CAPACITY_H

#include <QString>
#include <limits>

namespace OriginalRuntime
{
// ZeroHeight仅说明原始高度为0；原矩阵仍计算，不在这里把其容量结果强制改为0。
enum class ReagentCapacitySource { Unavailable, Matrix, Geometry, ZeroHeight };
enum class ReagentCapacityFileState { Unknown, Missing, Present };
enum class ReagentCapacityMatrixState { NotChecked, Available, CoverageMissing, Invalid };

// 孔位身份来自当前孔；底径和角度来自该孔/区域的原瓶型表，不能填顶层N中的几何值。
struct ReagentCapacityConfiguredBottle
{
    QString hole;
    QString name;
    QString area; // 原本地孔位区域：r_area或s_area。
    double bottomDiameterMm = std::numeric_limits<double>::quiet_NaN();
    double angleDegrees = std::numeric_limits<double>::quiet_NaN();
    double maximumMicrolitres = std::numeric_limits<double>::quiet_NaN(); // 独立取原hole->getMaxVol()快照。
};

struct ReagentCapacityInput
{
    QString hole;
    int needle = 0; // 已由调用者成功解析的整数；原1→N1、2→N2、其余→S1，不把坏文本默认成0。
    QString area; // 原报文0/s_area对应样本区，1/r_area对应试剂区。
    QString reportedBottle; // 原报文瓶型名或1至5的数字别名。
    double rawHeightMm = std::numeric_limits<double>::quiet_NaN();
    ReagentCapacityConfiguredBottle configuredBottle;
};

struct ReagentCapacityResult
{
    bool available = false;
    ReagentCapacitySource source = ReagentCapacitySource::Unavailable;
    ReagentCapacityFileState fileState = ReagentCapacityFileState::Unknown;
    ReagentCapacityMatrixState matrixState = ReagentCapacityMatrixState::NotChecked;
    QString actualModel;
    QString relativeFile;
    QString fingerprint; // 实际读到的原文件字节SHA-256；Missing时为空。
    QString matrixFailureReason;
    QString failureReason;
    double rawHeightMm = std::numeric_limits<double>::quiet_NaN();
    int quantizedHeightHundredths = 0; // 原int(rawHeightMm * 100)，向0截断。
    double quantizedHeightMm = std::numeric_limits<double>::quiet_NaN();
    bool adjustedHeightAvailable = false;
    double adjustedHeightMm = std::numeric_limits<double>::quiet_NaN();
    int matrixMaximumMicrolitres = 0; // 原矩阵函数的int最大量。
    double matrixVolumeMicrolitres = std::numeric_limits<double>::quiet_NaN();
    double volumeMicrolitres = std::numeric_limits<double>::quiet_NaN(); // 原dectVol，几何结果不先限幅。
    double displayVolumeMicrolitres = std::numeric_limits<double>::quiet_NaN(); // 按本孔double最大量另算显示值。
};

// actualModel必须由调用者核实；精确SN450读450，其余非空原机型读600，不互换资料。
// 仅在Linux/RK读取原工作根及原容量文件；沿现workingRoot检查读取已存在的许可标记，
// 不创建文件、不改数据库/容量资料，不发设备命令。失败时available=false且容量不是默认0。
bool calculateReagentCapacity(const QString &root, const QString &actualModel,
                              const ReagentCapacityInput &input, ReagentCapacityResult *result,
                              QString *message);
// 仅加载实际机型容量文件的存在状态/原字节指纹，不造高度或执行换算。
// 返回true不表示容量可用，result.available仍为false；供到达时版本绑定使用。
bool readReagentCapacityVersion(const QString &root, const QString &actualModel,
                                ReagentCapacityResult *result, QString *message);
}

#endif
