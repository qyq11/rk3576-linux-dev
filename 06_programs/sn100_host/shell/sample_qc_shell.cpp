#include "sample_qc_shell.h"
#include "samples/sample_page.h"
#include "quality_control/qc_store.h"
#include <QSharedPointer>
#include <QThread>

bool connectSampleQuality(SampleStore *samples, QcStore *quality,
                          SamplePage *samplePage, QObject *context)
{
    if (!samples || !quality || !samplePage || !context
        || QThread::currentThread() != context->thread()
        || samplePage->thread() != context->thread()
        || samplePage->property("_sn100SampleQualityConnected").toBool()) return false;
    samplePage->setProperty("_sn100SampleQualityConnected", true);
    // 只传已提交值和稳定身份；预约、样本提交及保护回收各由所属worker完成。
    // 启动顺序不同不能丢掉绑定请求，也不能把QC资料可读当作引用保护已就绪。
    struct SampleQualityBinding
    {
        QString ownerId;
        QString catalogPath;
        bool catalogReady = false;
    };
    const auto binding = QSharedPointer<SampleQualityBinding>::create();
    const auto requestBinding = [quality, binding]()
    {
        if (!binding->catalogReady || binding->ownerId.isEmpty()
            || binding->catalogPath.isEmpty()) return;
        const QString ownerId = binding->ownerId;
        const QString catalogPath = binding->catalogPath;
        QMetaObject::invokeMethod(quality, [quality, ownerId, catalogPath]()
        { quality->reconcileSampleOwner(ownerId, catalogPath); }, Qt::QueuedConnection);
    };
    QObject::connect(samples, &SampleStore::qualityOwnerBindingRequired, context,
                     [binding, requestBinding](QString ownerId, QString catalogPath)
    {
        binding->ownerId = ownerId;
        binding->catalogPath = catalogPath;
        requestBinding();
    });
    QObject::connect(quality, &QcStore::catalogBound, context,
                     [binding, requestBinding](QString)
    {
        binding->catalogReady = true;
        requestBinding();
    });
    QObject::connect(quality, &QcStore::sampleOwnerReconciled,
                     samples, &SampleStore::qualityOwnerReconciled, Qt::QueuedConnection);
    QObject::connect(samples, &SampleStore::reserveQualityChange,
                     quality, &QcStore::reserveSampleChange, Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::sampleChangeReserved,
                     samples, &SampleStore::qualityChangeReserved, Qt::QueuedConnection);
    QObject::connect(samples, &SampleStore::finishQualityChange,
                     quality, &QcStore::finishSampleChange, Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::sampleChangeFinished,
                     samples, &SampleStore::qualityChangeFinished, Qt::QueuedConnection);
    QObject::connect(samplePage, &SamplePage::requestQualityChoices,
                     quality, &QcStore::loadSampleChoices, Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::sampleChoicesReady, samplePage,
                     [samplePage](qint64 requestId, SampleQcPreset::Snapshot choices)
    { samplePage->setQualityChoices(requestId, choices, QString()); }, Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::sampleChoicesError, samplePage,
                     [samplePage](qint64 requestId, QString message)
    { samplePage->setQualityChoices(requestId, SampleQcPreset::Snapshot(), message); },
                     Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::closed,
                     samples, &SampleStore::invalidateQualityBinding, Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::closed, samplePage, [samplePage]()
    { samplePage->invalidateQualityChoices(QObject::tr("质控资料已关闭，原样本草稿保留。")); },
                     Qt::QueuedConnection);
    QObject::connect(quality, &QcStore::closed, context, [binding]()
    { binding->catalogReady = false; });
    return true;
}
