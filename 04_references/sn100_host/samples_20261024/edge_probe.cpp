#define main existingSuiteEntryNotCalled
#include "../../../06_programs/sn100_host/samples/tests/samples_test.cpp"
#undef main
#include "cusbadgebutton.h"
#include "cuscombox_base.h"
#include <stdexcept>
static void need(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static bool waitFor(std::function<bool()> predicate)
{
    QElapsedTimer clock;clock.start();while(!predicate()&&clock.elapsed()<5000)QTest::qWait(10);return predicate();
}
int main(int argc,char **argv)
{
    QApplication app(argc,argv);ProjectsStore::registerTypes();SampleStore::registerTypes();
    try
    {
        Harness h;need(h.start(),"initialize");h.actor();need(waitFor([&](){return h.page->isReady()&&!h.page->isBusy();}),"ready");
        auto *add=h.page->findChild<CusBadgeButton *>("btnAdd");need(add,"add control");
        QMetaObject::invokeMethod(add,"clicked",Qt::DirectConnection);
        auto *table=h.page->findChild<QTableWidget *>("tblSetting");need(table&&table->rowCount()==8,"ordinary 8 holes");
        table->selectRow(6);table->item(6,1)->setText("7001");
        h.page->findChild<QPushButton *>("btnHoleDetail")->click();
        QPointer<QDialog> detail=h.page->findChild<QDialog *>("sampleDetailDialog");need(detail,"detail");
        need(waitFor([&](){return detail->findChild<CusDialogMask *>("mask")->isReadyForInput();}),"detail animation");
        detail->findChild<QPushButton *>("detailProject_"+QString::number(h.records.first().id))->click();
        detail->findChild<QPushButton *>("btnSave")->click();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        h.page->findChild<QPushButton *>("btnRepeatHoleTask")->click();
        QPointer<QDialog> repeat=h.page->findChild<QDialog *>("sampleRepeatDialog");need(repeat,"repeat");
        need(waitFor([&](){return repeat->findChild<CusDialogMask *>("mask")->isReadyForInput();}),"repeat animation");
        repeat->findChild<QPushButton *>("btnNum_3")->click();repeat->findChild<QPushButton *>("btnSave")->click();
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        h.page->findChild<QPushButton *>("btnMenuSave")->click();need(waitFor([&](){return !h.page->isBusy();}),"ordinary save");
        auto snapshot=h.page->snapshot();need(snapshot.racks.size()==2,"two local racks");
        auto rack=snapshot.racks[1];need(rack.holes.size()==8&&rack.holes[6].sampleId=="7001"&&rack.holes[7].sampleId=="7002","last-row copy IDs");
        need(rack.holes[6].tasks.size()==1&&rack.holes[7].tasks.size()==1,"last-row tasks");
        table->selectRow(7);
        auto *container=qobject_cast<CusCombox_Base *>(table->cellWidget(7,3));need(container,"container");
        container->setCurrentIndex(0);QMetaObject::invokeMethod(container,"itemSelected",Qt::DirectConnection,Q_ARG(int,0),Q_ARG(QString,QString::fromUtf8("试管")));
        h.page->findChild<QPushButton *>("btnMenuSave")->click();need(waitFor([&](){return !h.page->isBusy();}),"container save");
        need(h.page->snapshot().racks[1].holes[7].container==0,"container persisted");
        SampleCombination combination;combination.name="ANON_DOUBLE";combination.orderedProjectIds={h.records.first().id,h.records.first().id};
        QSignalSpy comboSaved(h.samples,&SampleStore::succeeded);
        QMetaObject::invokeMethod(h.samples,"saveCombination",Qt::BlockingQueuedConnection,Q_ARG(qint64,5000),Q_ARG(SampleCombination,combination));
        need(comboSaved.count()==1,"combination saved");
        h.page->reload();need(waitFor([&](){return !h.page->isBusy();}),"combination refresh");
        table->selectRow(7);
        auto *projects=h.page->findChild<QListWidget *>("projButtonList");QPushButton *apply=nullptr;
        for(int i=0;i<projects->count();++i){auto *button=qobject_cast<QPushButton *>(projects->itemWidget(projects->item(i)));if(button&&button->text()=="ANON_DOUBLE")apply=button;}
        need(apply,"combination button");apply->click();h.page->findChild<QPushButton *>("btnMenuSave")->click();
        need(waitFor([&](){return !h.page->isBusy();}),"apply save");need(h.page->snapshot().racks[1].holes[7].tasks.first().repeats==2,"combination duplicates applied");
        QSignalSpy copied(h.projects,&ProjectsStore::succeeded);
        QMetaObject::invokeMethod(h.projects,"copy",Qt::BlockingQueuedConnection,Q_ARG(qint64,6000),Q_ARG(qint64,h.records.first().id),Q_ARG(QString,QString("LOCAL")));
        need(copied.count()==1,"mutable project copy");const qint64 id=copied.first()[2].toLongLong();
        rack=h.page->snapshot().racks[1];rack.holes[0].tasks={{id,"OFFLINE_RATE_A",1,false}};need(h.save(rack),"task reference save");
        QSignalSpy refused(h.projects,&ProjectsStore::error);
        QMetaObject::invokeMethod(h.projects,"remove",Qt::BlockingQueuedConnection,Q_ARG(qint64,6001),Q_ARG(qint64,id));
        need(refused.count()==1,"task FK refuses project deletion");
        qInfo().noquote()<<QJsonDocument(QJsonObject{{"ordinary_holes",8},{"batch_start_hole",7},{"requested_count",3},{"actual_last_sample","7002"},{"container_saved",0},{"combination_repeats",2},{"task_project_delete_refused",true},{"hardware_actions",0},{"passed",true}}).toJson(QJsonDocument::Compact);
        return 0;
    }
    catch(const std::exception &error){qCritical()<<error.what();return 1;}
}

