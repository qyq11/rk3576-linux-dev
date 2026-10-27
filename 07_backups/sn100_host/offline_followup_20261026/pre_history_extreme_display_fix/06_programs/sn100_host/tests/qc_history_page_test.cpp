#include "cus_quality_chart.h"
#include "qc_history_presenter.h"
#include "qualitychart.h"
#include "qc_history_gui_fixture.h"
#include <QtTest>
#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSemaphore>
#include <QSharedPointer>
#include <QAtomicInt>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWindow>
#include <cmath>

using namespace Sn100QcHistory;

namespace
{
struct Values
{
    QcHistoryResult result;
    Selection selected;
    Summary summary;
};
Values values(bool knownUnits, bool invalid = false, bool constant = false)
{
    Values data;
    data.result.type=ResultType::Original;data.result.name="ANON_RESULT";data.result.used=true;
    SelectionRequest request;
    request.controlName="QC_DEMO";request.projectName="PROJ_QC_DEMO";
    request.currentBatch="LOT_CUR";request.previousBatch="LOT_PREV";
    QList<HistoryRow> rows;
    for (int index=0;index<3;++index)
    {
        HistoryRow row;
        row.id=101+index;row.controlName=request.controlName;row.projectName=request.projectName;
        row.batchName=index?request.currentBatch:request.previousBatch;
        row.date=QString("2026/10/%1").arg(23+index);row.time="10:00:00";
        const QString number=invalid&&!index?QString("****"):QString::number(constant?7:10+2*index);
        row.resultValue="[PROJ_QC_DEMO|0|=Original-"+number+":1/1-101]";
        rows.append(row);
        if (knownUnits)
        {
            UnitEvidence unit;unit.rowId=row.id;unit.type=ResultType::Original;
            unit.source=UnitSource::DeclaredAnonymousFixture;unit.unit="ANON_UNIT";
            unit.provenance="anonymous-per-row-card-test";unit.ratio=0.5;unit.decimals=1;
            request.historicalUnits.append(unit);
        }
    }
    data.selected=selectHistory(rows,request);data.summary=summarize(data.selected);
    return data;
}
template<class T> T *widget(QObject *parent,const char *name) {return parent->findChild<T*>(name);}
}

class QcHistoryPageTest : public QObject
{
    Q_OBJECT
    QString runtime_,evidence_;
private slots:
    void initTestCase()
    {
        QApplication::setStyle("Fusion");QApplication::setFont(QFont("Noto Sans CJK SC",11));
        runtime_=qEnvironmentVariable("SN100_QC_HISTORY_PAGE_ROOT");
        evidence_=qEnvironmentVariable("SN100_QC_HISTORY_PAGE_EVIDENCE");
        QVERIFY(QDir::isAbsolutePath(runtime_)&&QDir::isAbsolutePath(evidence_));
        QVERIFY(!QFileInfo::exists(runtime_)&&!QFileInfo::exists(evidence_));
        QVERIFY(QDir().mkpath(runtime_)&&QDir().mkpath(evidence_));
    }
    void originalCardNamesUnknownUnitsAndLegacyTrace()
    {
        const Values data=values(false);
        QVERIFY(data.selected.ok);QCOMPARE(data.selected.plotOrder.size(),3);
        CusQualityChart card;card.resize(1061,235);
        card.setData("QC_DEMO","LOT_CUR",data.result,data.selected,data.summary);card.show();
        auto *plot=widget<QualityChart>(&card,"wgtChart");QVERIFY(plot);
        auto *left=widget<QPushButton>(&card,"btnLeft");auto *right=widget<QPushButton>(&card,"btnRight");
        QVERIFY(left&&right);QVERIFY(!widget<QPushButton>(&card,"btnAddCode")->isVisible());
        QCOMPARE(widget<QLabel>(&card,"lblCount")->text(),QString("3"));
        QCOMPARE(widget<QLabel>(&card,"lblMean")->text(),QStringLiteral("—"));
        QCOMPARE(widget<QLabel>(&card,"lblCV")->text(),QStringLiteral("—"));
        QCOMPARE(widget<QLabel>(&card,"lblState")->text(),QStringLiteral("—"));
        QVERIFY(!card.usesHistoricalDisplayUnits());
        QVERIFY(widget<QLabel>(&card,"lblResultVal")->toolTip().contains(QStringLiteral("单位未知")));
        QCOMPARE(plot->points().size(),3);QCOMPARE(plot->points().first().value,10.0);
        QVERIFY(plot->points().first().previous);QVERIFY(!plot->points().at(1).previous);
        QCOMPARE(plot->tracedIndex(),0);QCOMPARE(card.traceCounter(),2);
        QCOMPARE(widget<QLabel>(&card,"lblDate")->text(),QString("2026/10/23"));
        QVERIFY(!left->isEnabled());QVERIFY(right->isEnabled());QVERIFY(!right->icon().isNull());
        QTest::mouseClick(right,Qt::LeftButton);
        QCOMPARE(plot->tracedIndex(),1);QCOMPARE(card.traceCounter(),1);
        QCOMPARE(widget<QLabel>(&card,"lblDate")->text(),QString("2026/10/25"));
        QVERIFY(widget<QLabel>(&card,"lblResultVal")->toolTip().startsWith("14"));
        QTest::mouseClick(left,Qt::LeftButton);QCOMPARE(plot->tracedIndex(),0);
    }
    void knownPerPointFixtureUsesFractionalRatioAndDescriptiveSummary()
    {
        const Values data=values(true);QVERIFY(data.summary.ok);
        CusQualityChart card;card.resize(1061,235);
        card.setData("QC_DEMO","LOT_CUR",data.result,data.selected,data.summary);card.show();
        QVERIFY(card.usesHistoricalDisplayUnits());
        QCOMPARE(widget<QLabel>(&card,"lblMean")->text(),QString("6.0"));
        QCOMPARE(widget<QLabel>(&card,"lblSD")->text(),QString("1.0"));
        QCOMPARE(widget<QLabel>(&card,"lblCV")->text(),QString("0.2"));
        QVERIFY(widget<QLabel>(&card,"lblCV")->toolTip().contains(QStringLiteral("不是百分比")));
        QCOMPARE(widget<QualityChart>(&card,"wgtChart")->points().first().value,5.0);
        QVERIFY(widget<QLabel>(&card,"lblResultVal")->toolTip().startsWith("5.0 ANON_UNIT"));
        QCOMPARE(widget<QLabel>(&card,"lblState")->text(),QStringLiteral("—"));
    }
    void invalidValueStaysGapAndConstantRangeRemainsFinite()
    {
        Values data=values(false,true);QVERIFY(data.selected.invalidResultCount==1);
        CusQualityChart card;card.resize(1061,235);
        card.setData("QC_DEMO","LOT_CUR",data.result,data.selected,data.summary);card.show();
        auto *plot=widget<QualityChart>(&card,"wgtChart");QVERIFY(plot);
        QVERIFY(!plot->points().first().valid);QVERIFY(std::isnan(plot->points().first().value));
        QCOMPARE(widget<QLabel>(&card,"lblCount")->text(),QString("2"));
        QCOMPARE(widget<QLabel>(&card,"lblResultVal")->text(),QStringLiteral("无效"));
        data=values(true,false,true);
        card.setData("QC_DEMO","LOT_CUR",data.result,data.selected,data.summary);
        QVERIFY(std::isfinite(plot->ySpan())&&plot->ySpan()>0);
        QCOMPARE(widget<QLabel>(&card,"lblSD")->text(),QString("0.0"));
    }
    void requestIdentityErrorsAndDestroyedHostDoNotLeaveBusyState()
    {
        QListWidget list;
        QcHistoryPresenter presenter(&list);
        QSignalSpy requests(&presenter,&QcHistoryPresenter::requestHistory);
        QSignalSpy finished(&presenter,&QcHistoryPresenter::finished);
        presenter.selectAssociation(10,20);QCOMPARE(requests.size(),1);QVERIFY(presenter.isBusy());
        const qint64 first=presenter.requestId();
        presenter.accept(first-1,QcHistoryReply());QVERIFY(presenter.isBusy());QCOMPARE(finished.size(),0);
        presenter.selectAssociation(10,21);const qint64 second=presenter.requestId();QVERIFY(second!=first);
        presenter.reject(first,20,"stale","stale error");QVERIFY(presenter.isBusy());
        presenter.reject(second,20,"wrong-association","wrong association");QVERIFY(presenter.isBusy());
        presenter.reject(second,21,"fixture-read-failure","anonymous failure");
        QVERIFY(!presenter.isBusy());QCOMPARE(finished.size(),1);QVERIFY(!finished.first().first().toBool());
        presenter.selectAssociation(0,0);QVERIFY(!presenter.isBusy());QCOMPARE(list.count(),0);
        auto *temporary=new QListWidget;
        QcHistoryPresenter detached(temporary);detached.selectAssociation(1,2);
        const qint64 pending=detached.requestId();delete temporary;
        QVERIFY(!detached.isBusy());QCOMPARE(detached.requestId(),qint64(0));
        detached.accept(pending,QcHistoryReply());QVERIFY(!detached.isBusy());
    }
    void realWorkerPageHistorySelectionBusyAndShutdown()
    {
        QcHistoryGui::Session session(runtime_);QVERIFY(session.initialize());
        auto *list=widget<QListWidget>(session.page,"qualityChartList");QVERIFY(list);
        auto *projects=widget<QListWidget>(session.page,"lstProject");QVERIFY(projects);
        auto *presenter=session.page->findChild<QcHistoryPresenter*>();QVERIFY(presenter);
        auto showWidget=[&](QWidget *target)
        {
            session.host.raise();session.host.activateWindow();session.host.ensureWidgetVisible(target);
            return QcHistoryGui::waitFor([&]()
            {
                QApplication::sync();
                const QPoint center=target->mapTo(session.host.viewport(),target->rect().center());
                return session.host.isActiveWindow()&&session.host.windowHandle()&&
                    session.host.windowHandle()->isExposed()&&target->isVisible()&&target->isEnabled()&&
                    target->visibleRegion().contains(target->rect().center())&&session.host.viewport()->rect().contains(center);
            });
        };
        auto card=[&]()->CusQualityChart*
        {return list->count()==1?qobject_cast<CusQualityChart*>(list->itemWidget(list->item(0))):nullptr;};
        QVERIFY(card());QVERIFY(!session.page->isBusy());
        QCOMPARE(card()->property("qcResultType").toInt(),2);
        QCOMPARE(widget<QLabel>(card(),"lblCount")->text(),QString("3"));
        QCOMPARE(widget<QLabel>(card(),"lblMean")->text(),QStringLiteral("—"));
        QCOMPARE(widget<QLabel>(card(),"lblState")->text(),QStringLiteral("—"));
        QVERIFY(widget<QLabel>(card(),"lblResultVal")->toolTip().contains(QStringLiteral("10 单位未知")));
        QCOMPARE(widget<QLabel>(card(),"lblDate")->text(),QString("2026/10/23"));
        const auto original=session.history.last();
        QCOMPARE(original.second.associationId(),session.associationA);
        QVERIFY(original.second.complete());QCOMPARE(original.second.sourceRowCount(),3);
        auto *right=widget<QPushButton>(card(),"btnRight");QVERIFY(right);QVERIFY(showWidget(right));
        QTest::mouseClick(right,Qt::LeftButton);
        QCOMPARE(widget<QLabel>(card(),"lblDate")->text(),QString("2026/10/25"));
        QVERIFY(widget<QLabel>(card(),"lblResultVal")->toolTip().startsWith("14"));
        for(const QSize size:{QSize(1920,1080),QSize(1600,900)})
        {
            session.host.resize(size);QVERIFY(showWidget(right));
            QVERIFY(session.host.grab().save(QDir(evidence_).filePath(QString("qc_history_%1.png").arg(size.width()))));
        }
        QPushButton *projectB=nullptr;
        for(int row=0;row<projects->count();++row)
        {
            auto *button=qobject_cast<QPushButton*>(projects->itemWidget(projects->item(row)));
            if(button&&button->property("qcAssociationId").toLongLong()==session.associationB)projectB=button;
        }
        QVERIFY(projectB);QVERIFY(showWidget(projectB));
        auto entered=QSharedPointer<QSemaphore>::create();auto release=QSharedPointer<QSemaphore>::create();
        auto timedOut=QSharedPointer<QAtomicInt>::create(0);
        struct ReleaseOnExit{QSharedPointer<QSemaphore> gate;~ReleaseOnExit(){gate->release();}} releaseGuard{release};
        QMetaObject::invokeMethod(session.store,[entered,release,timedOut]()
        {entered->release();if(!release->tryAcquire(1,3000))timedOut->storeRelease(1);},Qt::QueuedConnection);
        QVERIFY(QcHistoryGui::waitFor([&](){return entered->available()>0;}));
        QTest::mouseClick(projectB,Qt::LeftButton);QVERIFY(session.page->isBusy());
        QCOMPARE(list->count(),0);QVERIFY(!session.page->requestLeave(QStringLiteral("历史读取期间离页")));
        // 旧回复来自刚完成的真实worker，不使用private访问或构造成功DTO。
        presenter->accept(original.first,original.second);
        QCOMPARE(list->count(),0);QVERIFY(session.page->isBusy());
        release->release();
        QVERIFY(QcHistoryGui::waitFor([&](){return !session.page->isBusy()&&card();}));
        QCOMPARE(timedOut->loadAcquire(),0);
        QCOMPARE(session.history.last().second.associationId(),session.associationB);
        QVERIFY(widget<QLabel>(card(),"lblResultVal")->toolTip().startsWith("100"));
        QCOMPARE(widget<QLabel>(card(),"lblCount")->text(),QString("3"));
        QVERIFY(showWidget(projectB));QTest::mouseClick(projectB,Qt::LeftButton);
        QVERIFY(QcHistoryGui::waitFor([&](){return !session.page->isBusy()&&card();}));
        QCOMPARE(session.history.last().second.associationId(),session.associationB);
        QVERIFY(session.unchangedHistory());QVERIFY(!session.page->isDirty());
        QcHistoryGui::Database database(session.qcPath);
        const int rows=database.scalar("SELECT count(*) FROM quality_data").toInt();QCOMPARE(rows,6);
        QVERIFY(session.page->requestLeave(QStringLiteral("只读历史正常离页")));QVERIFY(session.page->close());
        QVERIFY(session.shutdown());
        const QJsonObject receipt{{"source","real ProjectsStore catalog + QcStore metadata/SQL history; anonymous rows only"},
            {"history_rows",rows},{"history_unchanged",true},{"unknown_units_not_converted",true},
            {"stale_real_reply_ignored",true},{"closed_signals",session.qcClosed&&session.projectsClosed},
            {"thread_stopped",session.stopped},{"clinical_rule_evaluation",false}};
        QFile record(QDir(evidence_).filePath("qc_history_page_runtime.json"));
        QVERIFY(record.open(QIODevice::WriteOnly|QIODevice::NewOnly));
        QVERIFY(record.write(QJsonDocument(receipt).toJson())>0);
    }
    void realWorkerInvalidDatesAreExplainedRatherThanOrdinaryEmpty()
    {
        QcHistoryGui::Session session(runtime_);QVERIFY(session.initialize(true));
        auto *projects=widget<QListWidget>(session.page,"lstProject");QVERIFY(projects);
        auto *list=widget<QListWidget>(session.page,"qualityChartList");QVERIFY(list);
        QPushButton *projectB=nullptr;
        for(int row=0;row<projects->count();++row)
        {
            auto *button=qobject_cast<QPushButton*>(projects->itemWidget(projects->item(row)));
            if(button&&button->property("qcAssociationId").toLongLong()==session.associationB)projectB=button;
        }
        QVERIFY(projectB);session.host.raise();session.host.activateWindow();session.host.ensureWidgetVisible(projectB);
        QApplication::sync();QVERIFY(QTest::qWaitForWindowExposed(&session.host));
        QTest::mouseClick(projectB,Qt::LeftButton);
        QVERIFY(QcHistoryGui::waitFor([&](){return !session.page->isBusy()&&session.history.last().second.associationId()==session.associationB;}));
        QCOMPARE(list->count(),0);
        QVERIFY(session.page->statusText().contains(QStringLiteral("日期无效或缺失")));
        QVERIFY(session.page->statusText().contains(QStringLiteral("已读取3条")));
        QCOMPARE(session.history.last().second.excludedRowCount(),3);
        QVERIFY(session.unchangedHistory());QVERIFY(session.shutdown());
    }
};
QTEST_MAIN(QcHistoryPageTest)
#include "qc_history_page_test.moc"
