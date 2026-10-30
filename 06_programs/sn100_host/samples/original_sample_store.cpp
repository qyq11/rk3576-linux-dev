#include "sample_store.h"
#include "original_sample_repository.h"
#include "original_detection_session.h"
#include "original_device_sync.h"
#include <QThread>
#include <QTimer>

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
    std::unique_ptr<OriginalDeviceSync> synchronization(new OriginalDeviceSync);
    if (!synchronization->initialize(root,&message))
    { emit error(0,QStringLiteral("initializeOriginal"),message); return; }
    originalSamples_ = std::move(repository);
    originalDetection_ = std::move(detection);
    originalDeviceSync_=std::move(synchronization);
    if (!originalConfigurationTimeout_)
    {
        originalConfigurationTimeout_=new QTimer(this);
        originalConfigurationTimeout_->setSingleShot(true);
        connect(originalConfigurationTimeout_,&QTimer::timeout,this,[this]()
        {
            if (!originalDeviceSync_ || !originalDeviceSync_->waiting()) return;
            const quint64 connection=originalDeviceSync_->connection();
            originalDeviceSync_->invalidate();
            emit originalConfigurationState(connection,false,QStringLiteral("设备配置10秒未确认，未重发或发送开始；请核对连接和回包后重新同步。"));
        });
    }
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
    if (!originalDeviceSync_ || !originalDeviceSync_->canStart(linuxNew,&message))
    { emit deviceTasksPrepared(request,false,message.isEmpty() ? QStringLiteral("原配置同步未就绪。") : message); return; }
    const bool success = originalDetection_->prepare(linuxNew, &message);
    emit deviceTasksPrepared(request, success, success ? QStringLiteral("已按当前原数据库核对样本任务，等待原开始命令。") : message);
}

void SampleStore::cancelDeviceTaskPreparation()
{
    if (QThread::currentThread() == thread() && originalDetection_) originalDetection_->cancelUnsent();
}

void SampleStore::answerDeviceRackRequest(QByteArray payload, quint64 connection)
{
    if (QThread::currentThread() != thread() || !originalDetection_ || !originalDeviceSync_ ||
        !connection || originalDeviceSync_->connection()!=connection)
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
    else if (!originalDeviceSync_ || !connection || originalDeviceSync_->connection()!=connection)
        message = QStringLiteral("该数据不属于当前已核对的设备连接，未写入或下发任务。");
    else if (payload.startsWith("Tt"))
    {
        QByteArray response;
        success = originalDetection_->answerRackRequest(payload,&response,&message);
        if (success) emit deviceRackTasksPrepared(response,connection);
    }
    else if (payload=="Tu")
    {
        QByteArray response;
        success=originalDetection_->reagentConfiguration(&response,&message);
        if (success) emit originalConfigurationPacket(response,connection);
    }
    else if (payload.startsWith("UP"))
    {
        OriginalConfigurationStep step;
        success=originalDeviceSync_ && originalDeviceSync_->projectReply(payload,connection,&step,&message);
        if (success)
        {
            SampleSnapshot snapshot;
            success=originalSamples_->load(&snapshot,&message);
            if (success)
            {
                emit originalProjectsReady(originalSamples_->projects());
                emit originalProjectsChanged(); emit deviceTaskStateChanged();
                sendOriginalConfigurationStep(step);
            }
        }
        if (!success && originalDeviceSync_)
        {
            originalDeviceSync_->invalidate();
            if (originalConfigurationTimeout_) originalConfigurationTimeout_->stop();
            emit originalConfigurationState(connection,false,message);
        }
    }
    else
    {
        success = originalDetection_->receiveDevicePayload(payload,&message);
        if (success && (payload.startsWith("TD") || payload.startsWith("Tr"))) emit deviceTaskStateChanged();
        if (success && payload.startsWith("Tr")) emit originalResultsChanged();
        if (success && payload.startsWith("Ur")) emit originalReagentsChanged();
        if (payload.startsWith("Ur") && originalDeviceSync_ && originalDeviceSync_->waitingForReagents())
        {
            OriginalConfigurationStep step;
            if (success) success=originalDeviceSync_->reagentReplySaved(connection,&step,&message);
            if (success) sendOriginalConfigurationStep(step);
            else
            {
                originalDeviceSync_->invalidate();
                if (originalConfigurationTimeout_) originalConfigurationTimeout_->stop();
                emit originalConfigurationState(connection,false,message);
            }
        }
    }
    emit devicePacketProcessed(packet,connection,payload.left(2),success,message);
}

void SampleStore::beginOriginalDeviceSync(quint64 connection, bool linuxNew)
{
    if (QThread::currentThread()!=thread() || !ready_ || !originalDeviceSync_ || !canEdit(session_))
    {
        emit originalConfigurationState(connection,false,QStringLiteral("当前会话或原配置未就绪，未发送设备参数。"));
        return;
    }
    OriginalConfigurationStep step; QString message;
    if (!originalDeviceSync_->begin(connection,linuxNew,&step,&message))
    { emit originalConfigurationState(connection,false,message); return; }
    sendOriginalConfigurationStep(step);
}

void SampleStore::invalidateOriginalDeviceSync()
{
    if (QThread::currentThread()!=thread()) return;
    if (originalConfigurationTimeout_) originalConfigurationTimeout_->stop();
    if (!originalDeviceSync_) return;
    const quint64 connection=originalDeviceSync_->connection();
    originalDeviceSync_->invalidate();
    emit originalConfigurationState(connection,false,QStringLiteral("设备连接或项目配置已变化，需重新核对同步状态。"));
}

void SampleStore::sendOriginalConfigurationStep(const OriginalConfigurationStep &step)
{
    const quint64 connection=originalDeviceSync_->connection();
    if (originalConfigurationTimeout_)
    {
        originalConfigurationTimeout_->stop();
        if (originalDeviceSync_->waiting()) originalConfigurationTimeout_->start(10000);
    }
    emit originalConfigurationState(connection,step.complete,step.message);
    for (const auto &packet : step.packets) emit originalConfigurationPacket(packet,connection);
}

void SampleStore::prepareOriginalReagentUpdate(QString holeName, bool updateAmount, quint64 connection)
{
    if (QThread::currentThread()!=thread() || !ready_ || !originalSamples_ || !originalDeviceSync_ ||
        !connection || originalDeviceSync_->connection()!=connection || !canEdit(session_))
    { emit deviceTaskError(QStringLiteral("原试剂更新的会话或设备连接已变化，未下发。")); return; }
    // 原单孔设置只在仪器声明LinuxNew时发送Us；完整协议由Tu按需读取本地保存。
    if (!originalDeviceSync_->usesSimplifiedProtocol()) return;
    OriginalReagentRepository repository;
    OriginalReagentState state;
    QByteArray payload; QString message;
    if (!repository.initialize(originalSamples_->root(),&message) || !repository.load(&state,&message) ||
        !repository.singleReagentSettingPayload(state,holeName,updateAmount,&payload,&message))
    { emit deviceTaskError(message); return; }
    emit originalConfigurationPacket(payload,connection);
}
