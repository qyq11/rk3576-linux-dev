#ifndef SN100_ORIGINAL_RESULT_CODEC_H
#define SN100_ORIGINAL_RESULT_CODEC_H
#include <QByteArray>
#include <QMap>
#include <QString>

namespace OriginalTasks
{
struct ReceivedResult
{
    qint64 taskId = 0;
    QMap<QString, QString> fields; // 沿用原pcTaskResult的键，不执行算法或填充检测值。
    QByteArray payload;
};
struct ReceivedProgress
{
    qint64 taskId = 0;
    QString rack, hole, state, type, project;
};
bool parseResult(const QByteArray &payload, ReceivedResult *result, QString *message);
bool parseProgress(const QByteArray &payload, ReceivedProgress *progress, QString *message);
}
#endif
