#include "sample_store.h"
#include "original_sample_repository.h"
#include "original_detection_session.h"
#include <QThread>

void SampleStore::initializeOriginal(QString root)
{
    if (QThread::currentThread() != thread())
    { emit error(0,QStringLiteral("initializeOriginal"),QStringLiteral("原样本初始化必须在数据库工作线程执行。")); return; }
    if (!pendingQuality_.change.changeToken.isEmpty())
    { emit error(0,QStringLiteral("initializeOriginal"),QStringLiteral("原有质控引用操作正在完成，未切换数据源。")); return; }
    release();
    auto repository = std::unique_ptr<OriginalSampleRepository>(new OriginalSampleRepository);
    QString message;
    if (!repository->initialize(root, &message))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    auto detection = std::unique_ptr<OriginalDetectionSession>(new OriginalDetectionSession);
    if (!detection->initialize(repository.get(), &message))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    originalSamples_ = std::move(repository);
    originalDetection_ = std::move(detection);
    ready_ = true;
    emit originalProfileReady(root, originalSamples_->ordinaryHoles(), originalSamples_->emergencyHoles());
    emit originalProjectsReady(originalSamples_->projects());
    emit ready(root);
}

bool SampleStore::originalRackChange(qint64 request, const QString &operation, const SampleRack &rack, qint64 id)
{
    if (!originalSamples_) return false;
    if (!check(request, operation, true)) return true;
    QString message;
    qint64 saved = id;
    bool success = false;
    if (operation == QStringLiteral("saveRack")) success = originalSamples_->saveRack(rack, &saved, &message);
    else if (operation == QStringLiteral("removeRack")) success = originalSamples_->removeRack(id, &message);
    else if (operation == QStringLiteral("clearOrdinaryRacks")) success = originalSamples_->clearOrdinary(&message);
    else message = QStringLiteral("原样本操作类型未识别。 ");
    if (success)
    {
        emit originalProjectsReady(originalSamples_->projects());
        emit succeeded(request, operation, saved);
    }
    else emit error(request, operation, message);
    return true;
}

void SampleStore::prepareDeviceTasks(quint64 request, bool linuxNew)
{
    if (QThread::currentThread() != thread() || !ready_ || !originalDetection_ || !canEdit(session_))
    { emit deviceTasksPrepared(request, false, QStringLiteral("当前账户、原样本库或工作线程未就绪，未准备设备任务。")); return; }
    QString message;
    const bool success = originalDetection_->prepare(linuxNew, &message);
    emit deviceTasksPrepared(request, success, success ? QStringLiteral("已按当前原数据库核对样本任务，等待原开始命令。") : message);
}

void SampleStore::cancelDeviceTaskPreparation()
{
    if (QThread::currentThread() == thread() && originalDetection_) originalDetection_->cancelUnsent();
}

void SampleStore::answerDeviceRackRequest(QByteArray payload, quint64 connection)
{
    if (QThread::currentThread() != thread() || !originalDetection_)
    { emit deviceTaskError(QStringLiteral("原样本任务处理未就绪，不能回应设备。")); return; }
    QByteArray response;
    QString message;
    if (!originalDetection_->answerRackRequest(payload, &response, &message))
    { emit deviceTaskError(message); return; }
    emit deviceRackTasksPrepared(response, connection);
}

void SampleStore::receiveDevicePacket(QByteArray payload, quint64 connection, quint64 packet)
{
    QString message;
    bool success = false;
    if (QThread::currentThread() != thread() || !originalDetection_)
        message = QStringLiteral("原样本/试剂处理未就绪，未保存或回应设备数据。");
    else if (payload.startsWith("Tt"))
    {
        QByteArray response;
        success = originalDetection_->answerRackRequest(payload,&response,&message);
        if (success) emit deviceRackTasksPrepared(response,connection);
    }
    else
    {
        success = originalDetection_->receiveDevicePayload(payload,&message);
        if (success && (payload.startsWith("TD") || payload.startsWith("Tr"))) emit deviceTaskStateChanged();
    }
    emit devicePacketProcessed(packet,connection,payload.left(2),success,message);
}
