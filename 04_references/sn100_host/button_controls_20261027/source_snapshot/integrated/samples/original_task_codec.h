#ifndef SN100_ORIGINAL_TASK_CODEC_H
#define SN100_ORIGINAL_TASK_CODEC_H

#include "projects/projects_store.h"
#include <QMap>
#include <QSet>

namespace OriginalTasks
{
struct RackRequest
{
    QString rack;
    QMap<int, QString> barcodes;
    QSet<qint64> receivedTaskIds;
};

// 下发前已经在原results.db占好任务号和报告行号；这两个编号不能由界面临时生成。
struct Task
{
    qint64 taskId = 0;
    qint64 reportRow = 0;
    QString rack;
    int hole = 0;
    int repeatIndex = 0;
    QString container; // 原tube/cup。
    QString sample;
    QString presetSampleId; // 原报告sn/结果sample_id保留输入样本号，线上sample字段可使用实际条码。
    QString type = QStringLiteral("SAM");
    bool prolonged = false;
    bool multiBottle = false;
    QString rate;
    QString batchGroup;
    ProjectRecord project;
    QMap<QString, QString> unitRatios; // 检测准备时的原单位倍率，后续修改单位不能改变这次任务。
    // 每个试剂步骤各自对应孔名和发送编号，不能按名称去重后破坏原顺序。
    QList<QPair<QString, QString>> reagentSlots;
    QMap<QString, QPair<QString, QString>> sampleAreaSlots;
    QMap<QString, QPair<QString, QString>> reagentAreaSlots;
};

bool parseRackRequest(const QByteArray &payload, RackRequest *request, QString *message);
bool taskFields(const Task &task, bool linuxNew, QStringList *fields, QString *message);
bool rackPayload(const QString &rack, const QList<Task> &tasks, bool linuxNew,
                 QByteArray *payload, QString *message);
}
#endif
