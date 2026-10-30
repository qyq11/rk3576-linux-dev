#ifndef SN100_ORIGINAL_DEVICE_SYNC_H
#define SN100_ORIGINAL_DEVICE_SYNC_H
#include "original_project_device_repository.h"

struct OriginalConfigurationStep
{
    QList<QByteArray> packets;
    bool complete = false;
    QString message;
};

// 一个连接一次同步：先读实际试剂，再逐项目发送Up并以UP回读核对；超时不盲目重发。
class OriginalDeviceSync
{
public:
    bool initialize(const QString &root, QString *message);
    bool begin(quint64 connection, bool linuxNew, OriginalConfigurationStep *step, QString *message);
    bool reagentReplySaved(quint64 connection, OriginalConfigurationStep *step, QString *message);
    bool projectReply(const QByteArray &payload, quint64 connection, OriginalConfigurationStep *step, QString *message);
    bool canStart(bool linuxNew, QString *message) const;
    bool waiting() const { return waiting_; }
    bool waitingForReagents() const { return waiting_ && waitingForReagents_; }
    quint64 connection() const { return connection_; }
    bool usesSimplifiedProtocol() const { return connection_ && linuxNew_; }
    void invalidate();
private:
    QString root_;
    OriginalProjectDeviceRepository repository_;
    QList<OriginalDeviceProject> projects_;
    int projectIndex_ = 0;
    quint64 connection_ = 0;
    bool linuxNew_ = false, ready_ = false, waiting_ = false, waitingForReagents_ = false;
    bool nextProject(OriginalConfigurationStep *step, QString *message);
};
#endif
