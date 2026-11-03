#ifndef SN100_ORIGINAL_RESULT_CODEC_H
#define SN100_ORIGINAL_RESULT_CODEC_H
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QList>

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
struct FailedTask
{
    QString rack;
    int hole = 0;
    qint64 taskId = 0;
};
struct ReceivedTaskError
{
    int reason = 0; // 原0无错误/复位，1错误，2至7为缺样本、清洗液、稀释液、试剂及耗材。
    QList<FailedTask> tasks;
};
bool parseTaskError(const QByteArray &payload, ReceivedTaskError *error, QString *message);
bool parseRackTimes(const QByteArray &payload, QMap<QString, int> *seconds, QString *message);
bool parseResult(const QByteArray &payload, ReceivedResult *result, QString *message);
bool parseProgress(const QByteArray &payload, ReceivedProgress *progress, QString *message);
}
#endif
