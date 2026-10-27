#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QComboBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include "data/lis_settings_store.h"
#include "ui/frmlissetting.h"

class LisResponseTest : public QObject
{
    Q_OBJECT
private slots:
    void localSaveResponse();
};

void LisResponseTest::localSaveResponse()
{
    const QString runtime = qEnvironmentVariable("SN100_LIS_RESPONSE_ROOT");
    const QString output = qEnvironmentVariable("SN100_LIS_RESPONSE_JSON");
    QVERIFY(QDir::isAbsolutePath(runtime) && QDir::isAbsolutePath(output));
    QVERIFY(!QFileInfo::exists(runtime));
    QVERIFY(QDir().mkpath(runtime));
    const QStringList names = {QStringLiteral("ANON_PORT_A"),QStringLiteral("ANON_PORT_B")};
    QThread worker;
    auto *store = new LisSettingsStore([names]() { return names; });
    store->moveToThread(&worker);
    connect(&worker,&QThread::finished,store,&QObject::deleteLater);
    frmLisSetting page(store);
    page.resize(1451,853);
    page.show();
    connect(&worker,&QThread::started,store,[=]() { store->initialize(runtime); });
    QSignalSpy operations(&page,&frmLisSetting::operationFinished);
    worker.start();
    struct StopWorker
    {
        QThread &thread;
        LisSettingsStore *store;
        ~StopWorker()
        {
            QMetaObject::invokeMethod(store,"close",Qt::BlockingQueuedConnection);
            thread.quit();thread.wait();
        }
    } stop{worker,store};
    QTRY_VERIFY(page.isReady());
    auto *list=page.findChild<QComboBox *>(QStringLiteral("cbxCom"));
    auto *apply=page.findChild<QPushButton *>(QStringLiteral("btnSave"));
    QVERIFY(list && apply);
    QVector<double> completed,busyFeedback,heartbeatGaps;
    QElapsedTimer elapsed,heartbeat;
    bool measuring=false,success=true;
    connect(&page,&frmLisSetting::busyChanged,this,[&](bool busy)
    { if (measuring && busy) busyFeedback.append(elapsed.nsecsElapsed()/1e6); });
    connect(&page,&frmLisSetting::operationFinished,this,[&](const QString &operation,bool ok)
    {
        if (measuring && operation==QStringLiteral("save"))
        {
            completed.append(elapsed.nsecsElapsed()/1e6);
            success=success && ok;
            measuring=false;
        }
    });
    QTimer timer;
    heartbeat.start();
    connect(&timer,&QTimer::timeout,this,[&]()
    {
        heartbeatGaps.append(heartbeat.nsecsElapsed()/1e6);
        heartbeat.restart();
    });
    timer.start(5);
    for (int index=0;index<40;++index)
    {
        list->setCurrentText(names.at(index%2));
        QVERIFY(page.isDirty() && apply->isEnabled());
        elapsed.start();measuring=true;
        QVERIFY(QMetaObject::invokeMethod(apply,"click",Qt::QueuedConnection));
        QTRY_COMPARE(completed.size(),index+1);
        QVERIFY(success && !page.isBusy() && !page.isDirty());
        QCOMPARE(page.committedSettings().commName,names.at(index%2));
    }
    timer.stop();
    QCOMPARE(busyFeedback.size(),40);
    const auto summarize=[](QVector<double> values)
    {
        std::sort(values.begin(),values.end());
        const auto percentile=[&](double fraction)
        { return values.at(qMax(0,int(std::ceil(values.size()*fraction))-1)); };
        return QJsonObject{{"count",values.size()},{"p95_ms",percentile(.95)},
            {"p99_ms",percentile(.99)},{"max_ms",values.last()}};
    };
    const auto samples=[](const QVector<double> &values)
    { QJsonArray result;for (double value:values) result.append(value);return result; };
    QVERIFY(!heartbeatGaps.isEmpty());
    QJsonObject result{{"scope",QStringLiteral("Queued GUI click to busy signal and saved GUI data state;40 alternating anonymous INI writes, fixed two-name enumerator with no synthetic delay")},
        {"limits",QStringLiteral("Excludes OS input, paint completion, platform metadata enumeration / physical connection, Windows/RK and any approved budget; heartbeat is only an event-loop proxy")},
        {"enqueue_to_busy",summarize(busyFeedback)},{"enqueue_to_gui_saved",summarize(completed)},
        {"heartbeat_5ms",summarize(heartbeatGaps)},{"busy_samples_ms",samples(busyFeedback)},
        {"save_samples_ms",samples(completed)},{"heartbeat_samples_ms",samples(heartbeatGaps)}};
    QFile file(output);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray bytes=QJsonDocument(result).toJson(QJsonDocument::Indented);
    QCOMPARE(file.write(bytes),qint64(bytes.size()));
    QVERIFY(file.flush());
}

QTEST_MAIN(LisResponseTest)
#include "lis_response_test.moc"
