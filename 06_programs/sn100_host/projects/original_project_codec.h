#ifndef SN100_ORIGINAL_PROJECT_CODEC_H
#define SN100_ORIGINAL_PROJECT_CODEC_H
#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>

namespace OriginalProjectWire
{
using Fields = QMap<QString, QString>;
// 上下行字段不同：Up用于保存到仪器，UP用于仪器返回的原项目资料。
struct Project
{
    Fields base;
    QList<Fields> samples, reagents, results;
    QString coagulationEndMode;
};
bool updatePayload(const Project &project, QByteArray *payload, QString *message);
Project parametersInUpdate(const Project &project);
bool parseDeviceProject(const QByteArray &payload, Project *project, QString *message);
}
#endif
