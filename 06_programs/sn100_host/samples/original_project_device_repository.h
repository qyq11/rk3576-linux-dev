#ifndef SN100_ORIGINAL_PROJECT_DEVICE_REPOSITORY_H
#define SN100_ORIGINAL_PROJECT_DEVICE_REPOSITORY_H
#include "projects/original_project_codec.h"
#include "projects/original_project_repository.h"

struct OriginalDeviceProject
{
    ProjectRecord record;
    OriginalProjectWire::Project wire;
};
// 在SampleStore工作线程读取原项目全列及保存原UP返回；不使用页面精简显示值。
class OriginalProjectDeviceRepository
{
public:
    bool initialize(const QString &root, QString *message);
    bool load(QList<OriginalDeviceProject> *projects, QString *message);
    bool applyDeviceProject(const QByteArray &payload, const QString &expectedProject,
                            const QString &expectedFingerprint, QString *message);
    static bool matchesSentParameters(const OriginalDeviceProject &sent,
                                      const OriginalProjectWire::Project &received, QString *message);
private:
    QString root_;
};
#endif
