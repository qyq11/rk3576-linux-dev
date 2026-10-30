from pathlib import Path
import shutil

task = Path(__file__).resolve().parent
root = task.parents[3]
program = root / '06_programs/sn100_host'
output = task / 'integrated'

def changed(name, replacements):
    source = (program / name).read_text(encoding='utf-8')
    for old, new in replacements:
        assert source.count(old) == 1, (name, old[:120], source.count(old))
        source = source.replace(old, new, 1)
    target = output / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(source, encoding='utf-8', newline='\n')

changed('samples/sample_store.h', [
    ('#include <QSet>', '#include <QSet>\n#include <memory>\n#include "projects/projects_store.h"\nclass OriginalSampleRepository;\nclass OriginalDetectionSession;'),
    ('    void initialize(QString catalogDir);', '    void initialize(QString catalogDir);\n    void initializeOriginal(QString root);\n    void prepareDeviceTasks(quint64 request, bool linuxNew);\n    void cancelDeviceTaskPreparation();\n    void answerDeviceRackRequest(QByteArray payload, quint64 connection);\n    void receiveDevicePacket(QByteArray payload, quint64 connection, quint64 packet);'),
    ('    void ready(QString databasePath);', '    void ready(QString databasePath);\n    void originalProfileReady(QString root, int ordinaryHoles, int emergencyHoles);\n    void originalProjectsReady(ProjectRecords projects);\n    void deviceTasksPrepared(quint64 request, bool success, QString message);\n    void deviceRackTasksPrepared(QByteArray payload, quint64 connection);\n    void deviceTaskError(QString message);\n    void deviceTaskStateChanged();\n    void devicePacketProcessed(quint64 packet, quint64 connection, QByteArray command, bool success, QString message);'),
    ('    QSqlDatabase database_;', '    std::unique_ptr<OriginalSampleRepository> originalSamples_;\n    std::unique_ptr<OriginalDetectionSession> originalDetection_;\n    bool originalRackChange(qint64 request, const QString &operation, const SampleRack &rack, qint64 id);\n    QSqlDatabase database_;'),
])
changed('samples/sample_store.cpp', [
    ('#include "sample_store.h"', '#include "sample_store.h"\n#include "original_sample_repository.h"\n#include "original_detection_session.h"'),
    ('    qRegisterMetaType<SampleSnapshot>();', '    qRegisterMetaType<SampleSnapshot>();\n    qRegisterMetaType<ProjectRecords>();'),
    ('    SampleSnapshot snapshot;\n    QString message;\n    if (!database_.transaction())', '''    SampleSnapshot snapshot;
    QString message;
    if (originalSamples_)
    {
        if (originalSamples_->load(&snapshot, &message))
        {
            emit originalProjectsReady(originalSamples_->projects());
            emit snapshotReady(request, snapshot);
        }
        else emit error(request, QStringLiteral("load"), message);
        return;
    }
    if (!database_.transaction())'''),
    ('void SampleStore::saveCombination(qint64 request, SampleCombination combination)\n{', '''void SampleStore::saveCombination(qint64 request, SampleCombination combination)
{
    if (originalSamples_)
    {
        if (!check(request, QStringLiteral("saveCombination"), true)) return;
        QString message; qint64 saved = 0;
        if (originalSamples_->saveCombination(combination, &saved, &message)) emit succeeded(request, QStringLiteral("saveCombination"), saved);
        else emit error(request, QStringLiteral("saveCombination"), message);
        return;
    }'''),
    ('void SampleStore::removeCombination(qint64 request, qint64 id)\n{', '''void SampleStore::removeCombination(qint64 request, qint64 id)
{
    if (originalSamples_)
    {
        if (!check(request, QStringLiteral("removeCombination"), true)) return;
        QString message;
        if (originalSamples_->removeCombination(id, &message)) emit succeeded(request, QStringLiteral("removeCombination"), id);
        else emit error(request, QStringLiteral("removeCombination"), message);
        return;
    }'''),
    ('void SampleStore::release()\n{', 'void SampleStore::release()\n{\n    originalDetection_.reset();\n    originalSamples_.reset();'),
])
changed('samples/sample_qc_storage.cpp', [
    ('void SampleStore::saveRack(qint64 request, SampleRack rack)\n{', 'void SampleStore::saveRack(qint64 request, SampleRack rack)\n{\n    if (originalRackChange(request, QStringLiteral("saveRack"), rack, rack.id)) return;'),
    ('void SampleStore::removeRack(qint64 request, qint64 id)\n{', 'void SampleStore::removeRack(qint64 request, qint64 id)\n{\n    if (originalRackChange(request, QStringLiteral("removeRack"), SampleRack(), id)) return;'),
    ('void SampleStore::clearOrdinaryRacks(qint64 request)\n{', 'void SampleStore::clearOrdinaryRacks(qint64 request)\n{\n    if (originalRackChange(request, QStringLiteral("clearOrdinaryRacks"), SampleRack(), 0)) return;'),
])
changed('samples/sample_page.h', [
    ('    SampleSnapshot snapshot() const', '    SampleStore *taskStore() const { return store_; }\n    bool originalProfile() const { return originalProfile_; }\n    SampleSnapshot snapshot() const'),
    ('    SampleSnapshot snapshot_;', '    SampleSnapshot snapshot_;\n    bool originalProfile_ = false, pendingDeviceReload_ = false;\n    int ordinaryHoles_ = 8, emergencyHoles_ = 3;'),
])
changed('samples/sample_page.cpp', [
    ('    connect(store, &SampleStore::ready, this,', '''    connect(store, &SampleStore::originalProfileReady, this, [this](const QString &root, int ordinary, int emergency)
    {
        originalProfile_ = true;
        ordinaryHoles_ = ordinary; emergencyHoles_ = emergency;
        setProperty("originalProfile", true);
        setProperty("originalRoot", root);
    });
    connect(store, &SampleStore::originalProjectsReady, this, &SamplePage::setProjects);
    connect(store, &SampleStore::deviceTaskStateChanged, this, [this]()
    {
        if (busy_ || dirty_ || dialogDirty_) { pendingDeviceReload_ = true; return; }
        reload();
    });
    connect(store, &SampleStore::ready, this,'''),
    ('    for (int hole = 1; hole <= 8; ++hole)', '    for (int hole = 1; hole <= ordinaryHoles_; ++hole)'),
    ('report(tr("本地预设已读取；未下发，真实状态和足料校验待验。")+qualityWarning_);', 'report((originalProfile_ ? tr("已读取当前原项目和原组合；样本架按原版在本次运行内保存，尚未下发。") : tr("本地预设已读取；未下发，真实状态和足料校验待验。"))+qualityWarning_);'),
    ('                emit snapshotChanged(snapshot_);', '''                if (pendingDeviceReload_ && !dirty_ && !dialogDirty_)
                {
                    pendingDeviceReload_ = false;
                    QTimer::singleShot(0, this, &SamplePage::reload);
                }
                emit snapshotChanged(snapshot_);'''),
])

for name, destination in {
    'original_business_database':'original_files',
    'original_sample_repository':'samples',
    'original_task_codec':'samples',
    'original_result_codec':'samples',
    'original_detection_session':'samples',
    'original_reagent_repository':'reagents',
    'original_sample_store':'samples',
}.items():
    for extension in ('.h', '.cpp'):
        source = task / (name + extension)
        if not source.exists(): continue
        target = output / destination / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        text = source.read_text(encoding='utf-8')
        text = text.replace('#include "original_business_database.h"', '#include "original_files/original_business_database.h"')
        text = text.replace('#include "original_reagent_repository.h"', '#include "reagents/original_reagent_repository.h"')
        if destination == 'reagents': text = text.replace('#include "original_task_codec.h"', '#include "samples/original_task_codec.h"')
        target.write_text(text, encoding='utf-8', newline='\n')
print('STAGED_SAMPLE_SOURCES=' + str(len(list(output.rglob('*.*')))))

# 此段仅生成下一批整合副本，不改当前冻结的正式主窗或构建清单。
changed('shell/frmmain.h', [
    ('#include <QPointer>', '#include <QPointer>\n#include <QMap>'),
    ('    QString detectionStartBlocker() const;', '    QString detectionStartBlocker() const;\n    void attachDeviceTaskStore();\n    bool deviceTaskStoreAttached_ = false, deviceTaskPreparing_ = false;\n    quint64 deviceTaskRequest_ = 0, deviceTaskConnection_ = 0, devicePacket_ = 0;\n    QMap<quint64, qint64> devicePackets_;\n    qint64 devicePacketBytes_ = 0;'),
])
changed('shell/catalog_shell.cpp', [
    ('    samples_=samples;', '    samples_=samples;\n    if (deviceControl_) attachDeviceTaskStore();'),
    ('samples->setProjects(localCatalogProjects(projects_->projects()));', 'if (!samples->originalProfile()) samples->setProjects(localCatalogProjects(projects_->projects()));'),
    ('[samples](ProjectRecords records,ProjectUnits) { samples->setProjects(localCatalogProjects(records)); }', '[samples](ProjectRecords records,ProjectUnits) { if (!samples->originalProfile()) samples->setProjects(localCatalogProjects(records)); }'),
])
changed('shell/device_control_shell.cpp', [
    ('#include <QFontMetrics>', '#include <QFontMetrics>\n#include <QTimer>\n#include <QDebug>'),
    ('    updateDeviceControlActions();\n    return true;', '    attachDeviceTaskStore();\n    updateDeviceControlActions();\n    return true;'),
    ('        if (question.getClickButton() == QMessageBox::Yes)\n            deviceControl_->start(detectionStartBlocker());', '''        if (question.getClickButton() != QMessageBox::Yes || deviceTaskPreparing_) return;
        deviceTaskPreparing_ = true;
        const quint64 request = ++deviceTaskRequest_;
        deviceTaskConnection_ = deviceControl_->connectionGeneration();
        const bool linuxNew = deviceControl_->state().status.generalFields.value(19) == QStringLiteral("LinuxNew");
        updateDeviceControlActions();
        QMetaObject::invokeMethod(samples_->taskStore(), "prepareDeviceTasks", Qt::QueuedConnection,
            Q_ARG(quint64, request), Q_ARG(bool, linuxNew));
        QTimer::singleShot(10000, this, [this, request]()
        {
            if (!deviceTaskPreparing_ || request != deviceTaskRequest_) return;
            deviceTaskPreparing_ = false;
            QMetaObject::invokeMethod(samples_->taskStore(), "cancelDeviceTaskPreparation", Qt::QueuedConnection);
            updateDeviceControlActions();
            CusMessageBox message(this);
            message.showWarningBox(tr("原样本任务准备超过10秒，未发送开始命令，请核对数据读取结果。"));
        });'''),
    ('    const bool available = session_.authenticated && state.connected && state.fresh &&', '    const bool available = !deviceTaskPreparing_ && session_.authenticated && state.connected && state.fresh &&'),
    ('    return tr("样本任务尚未生成带稳定任务编号的设备下发记录，请先完成任务准备。");', '    if (!samples_->originalProfile()) return tr("样本页尚未使用当前原数据库，请核对启动的数据根。 ");\n    return QString();'),
])
control = output / 'shell/device_control_shell.cpp'
control.write_text(control.read_text(encoding='utf-8') + '''
void frmMain::attachDeviceTaskStore()
{
    if (deviceTaskStoreAttached_ || !deviceControl_ || !samples_) return;
    auto *store = samples_->taskStore();
    if (!store) return;
    deviceTaskStoreAttached_ = true;
    connect(store, &SampleStore::deviceTasksPrepared, this, [this, store](quint64 request, bool success, const QString &detail)
    {
        if (!deviceTaskPreparing_ || request != deviceTaskRequest_) return;
        deviceTaskPreparing_ = false;
        const QString blocker = deviceTaskConnection_ != deviceControl_->connectionGeneration()
            ? tr("准备样本任务期间连接已改变，未向新连接发送开始命令。") : detectionStartBlocker();
        if (success && blocker.isEmpty())
        {
            if (!deviceControl_->start()) QMetaObject::invokeMethod(store, "cancelDeviceTaskPreparation", Qt::QueuedConnection);
        }
        else
        {
            QMetaObject::invokeMethod(store, "cancelDeviceTaskPreparation", Qt::QueuedConnection);
            CusMessageBox message(this);
            message.showWarningBox(success ? blocker : detail);
        }
        updateDeviceControlActions();
    });
    connect(deviceControl_, &DeviceControl::startAccepted, store, [store](bool accepted)
    { if (!accepted) store->cancelDeviceTaskPreparation(); }, Qt::QueuedConnection);
    connect(deviceControl_, &DeviceControl::payloadReceived, this, [this, store](const QByteArray &payload)
    {
        const QByteArray command = payload.left(2);
        if (command != "Tt" && command != "TD" && command != "Tr" && command != "Ur") return;
        // 本地最多32包/1MiB在后台排队；满时明确断开，不让设备数据无限占用GUI内存。
        if (devicePackets_.size() >= 32 || devicePacketBytes_ + payload.size() > 1048576)
        {
            const QString detail = tr("设备业务数据处理队列已满，本包尚未保存，连接已关闭。已排队数据继续处理，请核对仪器与原始记录后恢复连接。");
            deviceControl_->close();
            setProperty("deviceTaskError", detail);
            ui->lblDivState->setText(tr("设备数据积压"));
            ui->lblDivState->setToolTip(detail);
            qWarning().noquote() << "DEVICE_TASK_BACKPRESSURE" << detail;
            return;
        }
        const quint64 packet = ++devicePacket_;
        devicePackets_.insert(packet,payload.size());
        devicePacketBytes_ += payload.size();
        QMetaObject::invokeMethod(store, "receiveDevicePacket", Qt::QueuedConnection,
            Q_ARG(QByteArray, payload), Q_ARG(quint64, deviceControl_->connectionGeneration()), Q_ARG(quint64, packet));
    });
    connect(store, &SampleStore::devicePacketProcessed, this,
        [this](quint64 packet, quint64 connection, const QByteArray &, bool success, const QString &detail)
    {
        if (devicePackets_.contains(packet)) devicePacketBytes_ -= devicePackets_.take(packet);
        if (success || connection != deviceControl_->connectionGeneration()) return;
        setProperty("deviceTaskError", detail);
        ui->lblDivState->setText(tr("任务处理失败"));
        ui->lblDivState->setToolTip(detail);
        qWarning().noquote() << "DEVICE_PACKET_ERROR" << detail;
    });
    connect(store, &SampleStore::deviceRackTasksPrepared, this, [this](const QByteArray &payload, quint64 connection)
    {
        if (connection != deviceControl_->connectionGeneration() || !deviceControl_->sendTaskReply(payload))
        {
            const QString detail = tr("任务编号已保存，但未能进入设备发送队列。原任务保留，请核对连接和收发记录，不要重复开始。");
            setProperty("deviceTaskError", detail);
            ui->lblDivState->setText(tr("任务下发失败"));
            ui->lblDivState->setToolTip(detail);
            qWarning().noquote() << "DEVICE_TASK_QUEUE_ERROR" << detail;
        }
    });
    connect(store, &SampleStore::deviceTaskError, this, [this](const QString &detail)
    {
        setProperty("deviceTaskError", detail);
        ui->lblDivState->setText(tr("任务处理失败"));
        ui->lblDivState->setToolTip(detail);
        qWarning().noquote() << "DEVICE_TASK_ERROR" << detail;
    });
}
''', encoding='utf-8', newline='\n')
changed('instrument/device_control.h', [
    ('    bool silenceAlarm();', '    bool silenceAlarm();\n    bool sendTaskReply(const QByteArray &payload);'),
    ('    const DeviceControlState &state() const { return state_; }', '    const DeviceControlState &state() const { return state_; }\n    quint64 connectionGeneration() const { return transport_.generation(); }'),
    ('    explicit DeviceTransport(QObject *parent) : ReadOnlyTcpTransport(parent) {}', '''    explicit DeviceTransport(QObject *parent) : ReadOnlyTcpTransport(parent)
    {
        // 一条原任务报文最多128000字节，另留分片封包开销；预算只在连接启动前设置。
        const bool configured = configureMessageQueue(131072);
        Q_ASSERT(configured);
        Q_UNUSED(configured);
    }'''),
])
changed('instrument/device_control.cpp', [
    ('bool DeviceControl::silenceAlarm()\n{', '''bool DeviceControl::sendTaskReply(const QByteArray &payload)
{
    if (!payload.startsWith("Tt[")) return reject(tr("不是已经准备完成的原任务回应。"));
    return send(payload);
}

bool DeviceControl::silenceAlarm()
{'''),
])
print('STAGED_WITH_SHELL=' + str(len(list(output.rglob('*.*')))))

changed('protocol/tcp_transport.h', [
    ('    bool queueMessage(const QByteArray &payload, QString *error);', '    bool queueMessage(const QByteArray &payload, QString *error);\n    bool configureMessageQueue(int maximumBytes);'),
])
changed('protocol/tcp_transport.cpp', [
    ('void ReadOnlyTcpTransport::setTimingEnabled(bool enabled)', '''bool ReadOnlyTcpTransport::configureMessageQueue(int maximumBytes)
{
    // 旧只读接口的构造预算保持原规则；设备派生对象可在空闲时预留完整任务报文空间。
    if (m_active || !m_frames.isEmpty() || pendingWriteBytes() != 0 || maximumBytes < 1 || maximumBytes > 131072) return false;
    m_maxQueuedBytes = maximumBytes;
    return true;
}

void ReadOnlyTcpTransport::setTimingEnabled(bool enabled)'''),
])
print('STAGED_WITH_TASK_QUEUE_BUDGET=' + str(len(list(output.rglob('*.*')))))
