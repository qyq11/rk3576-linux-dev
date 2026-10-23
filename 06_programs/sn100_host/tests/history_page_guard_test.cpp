#include <QtTest>
#include <QApplication>
#include <QDialog>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include "history/history_page.h"

namespace
{
HistoryReport anonymousReport()
{
    HistoryReport report;
    report.id = 17;
    report.sampleNumber = 1001;
    report.barcode = QStringLiteral("ANON-1001");
    report.dateTime = QDateTime(QDate(2026, 10, 23), QTime(9, 0));
    report.patient.reportId = report.id;
    report.patient.name = QStringLiteral("Anonymous");
    report.patient.age = 30;
    return report;
}

void disconnectBackend(HistoryPage *page, HistoryStore *store)
{
    // 这些测试只核对页面信号和快照，绝不初始化/读写数据库。
    QObject::disconnect(page, &HistoryPage::requestQuery, store, &HistoryStore::query);
    QObject::disconnect(page, &HistoryPage::requestDetail, store, &HistoryStore::detail);
    QObject::disconnect(page, &HistoryPage::requestSavePatient, store, &HistoryStore::savePatient);
    QObject::disconnect(page, &HistoryPage::requestExportCsv, store, &HistoryStore::exportCsv);
}

bool makeReady(HistoryPage *page, HistoryStore *store)
{
    QSignalSpy queries(page, &HistoryPage::requestQuery);
    store->ready(0, QStringLiteral("anonymous-no-database"));
    if (queries.size() != 1) return false;
    store->queryReady(queries.at(0).at(0).toLongLong(), HistoryFilter(), {anonymousReport()}, 1);
    return page->isReady() && !page->isBusy();
}

QDialog *openDetail(HistoryPage *page, HistoryStore *store)
{
    QSignalSpy details(page, &HistoryPage::requestDetail);
    auto *button = page->findChild<QPushButton *>(QStringLiteral("btnDetail"));
    if (!button) return nullptr;
    button->click();
    if (details.size() != 1) return nullptr;
    store->detailReady(details.at(0).at(0).toLongLong(), anonymousReport());
    return page->findChild<QDialog *>(QStringLiteral("MaskDlgReportDetail"));
}

void chooseDiscard(QMessageBox::StandardButton answer, bool *defaultKept)
{
    QTimer::singleShot(0, qApp, [answer, defaultKept]()
    {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            auto *box = qobject_cast<QMessageBox *>(widget);
            if (!box || box->objectName() != QStringLiteral("historyDiscardDialog")) continue;
            if (defaultKept)
            {
                *defaultKept = box->defaultButton() == box->button(QMessageBox::Cancel)
                        && box->escapeButton() == box->button(QMessageBox::Cancel);
            }
            box->button(answer)->click();
            return;
        }
    });
}
}

class HistoryPageGuardTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        HistoryStore::registerTypes();
    }

    void initializationAndBusyRefuseLeave()
    {
        HistoryStore store;
        HistoryPage page(&store);
        disconnectBackend(&page, &store);
        QCOMPARE(page.size(), QSize(1751, 968));
        QVERIFY(!page.requestLeave(QStringLiteral("切换页面")));
        QSignalSpy queries(&page, &HistoryPage::requestQuery);
        store.ready(0, QStringLiteral("anonymous-no-database"));
        QVERIFY(page.isBusy());
        QVERIFY(!page.requestLeave(QStringLiteral("退出")));
        QCOMPARE(queries.size(), 1);
        store.queryReady(queries.at(0).at(0).toLongLong(), HistoryFilter(), {anonymousReport()}, 1);
        QVERIFY(page.requestLeave(QStringLiteral("切换页面")));
    }

    void initializationFailureIsObservableForHostTeardown()
    {
        HistoryStore store;
        HistoryPage page(&store);
        disconnectBackend(&page, &store);
        QSignalSpy finished(&page, &HistoryPage::operationFinished);
        QSignalSpy statuses(&page, &HistoryPage::statusChanged);
        QSignalSpy queries(&page, &HistoryPage::requestQuery);
        store.error(0, QStringLiteral("initialize"), QStringLiteral("匿名初始化失败"));
        QVERIFY(!page.isReady());
        QVERIFY(!page.isBusy());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.at(0).at(0).toString(), QStringLiteral("initialize"));
        QCOMPARE(finished.at(0).at(1).toBool(), false);
        QCOMPARE(statuses.size(), 1);
        QVERIFY(statuses.at(0).at(0).toString().contains(QStringLiteral("匿名初始化失败")));
        QCOMPARE(queries.size(), 0);
        // 页面不把初始化失败当就绪；宿主须收到上述fatal结果后直接统一关闭
        // worker并销毁页面，不能等待一个永远不会出现的ready再调用离页守卫。
        QVERIFY(!page.requestLeave(QStringLiteral("切换页面")));
    }

    void keepAndDiscardLastReadOrCommittedSnapshot()
    {
        HistoryStore store;
        HistoryPage page(&store);
        disconnectBackend(&page, &store);
        QVERIFY(makeReady(&page, &store));
        QDialog *detail = openDetail(&page, &store);
        QVERIFY(detail);
        auto *name = detail->findChild<QLineEdit *>(QStringLiteral("ldtName"));
        QVERIFY(name);
        QSignalSpy writes(&page, &HistoryPage::requestSavePatient);
        name->setText(QStringLiteral("DraftName"));
        QVERIFY(page.hasUnsavedChanges());
        bool defaultKept = false;
        chooseDiscard(QMessageBox::Cancel, &defaultKept);
        QVERIFY(!page.requestLeave(QStringLiteral("切换页面")));
        QVERIFY(defaultKept);
        QCOMPARE(name->text(), QStringLiteral("DraftName"));
        QVERIFY(page.hasUnsavedChanges());
        chooseDiscard(QMessageBox::Discard, nullptr);
        QVERIFY(page.requestLeave(QStringLiteral("切换页面")));
        QCOMPARE(name->text(), anonymousReport().patient.name);
        QVERIFY(!page.hasUnsavedChanges());
        QCOMPARE(writes.size(), 0);

        QVERIFY(openDetail(&page, &store));
        name->setText(QStringLiteral("SavedName"));
        auto *save = detail->findChild<QPushButton *>(QStringLiteral("btnSave"));
        QVERIFY(save);
        save->click();
        QCOMPARE(writes.size(), 1);
        QVERIFY(page.isBusy());
        QVERIFY(!page.requestLeave(QStringLiteral("退出")));
        const qint64 requestId = writes.at(0).at(0).toLongLong();
        const HistoryPatient committed = qvariant_cast<HistoryPatient>(writes.at(0).at(1));
        store.patientSaved(requestId, committed);
        name->setText(QStringLiteral("NewDraft1"));
        chooseDiscard(QMessageBox::Discard, nullptr);
        QVERIFY(page.requestLeave(QStringLiteral("退出")));
        QCOMPARE(name->text(), QStringLiteral("SavedName"));
        QCOMPARE(writes.size(), 1); // 只有明确保存发出一条请求，放弃未发写请求。
    }

    void titlebarCloseUsesTheSameGuard()
    {
        HistoryStore store;
        HistoryPage page(&store);
        disconnectBackend(&page, &store);
        QVERIFY(makeReady(&page, &store));
        QDialog *detail = openDetail(&page, &store);
        QVERIFY(detail);
        auto *name = detail->findChild<QLineEdit *>(QStringLiteral("ldtName"));
        QVERIFY(name);
        name->setText(QStringLiteral("CloseDraft"));
        chooseDiscard(QMessageBox::Cancel, nullptr);
        QVERIFY(!detail->close());
        QVERIFY(page.hasUnsavedChanges());
        chooseDiscard(QMessageBox::Discard, nullptr);
        QVERIFY(detail->close());
        QCOMPARE(name->text(), anonymousReport().patient.name);
        QVERIFY(!page.hasUnsavedChanges());
    }

    void transientDialogsCannotOutliveThePage()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        HistoryStore store;
        HistoryPage page(&store);
        page.setDataDirectory(directory.path());
        disconnectBackend(&page, &store);
        QVERIFY(makeReady(&page, &store));
        QSignalSpy queries(&page, &HistoryPage::requestQuery);
        QSignalSpy exports(&page, &HistoryPage::requestExportCsv);
        auto *queryButton = page.findChild<QPushButton *>(QStringLiteral("btnQuery"));
        auto *exportButton = page.findChild<QPushButton *>(QStringLiteral("btnExport"));
        QVERIFY(queryButton);
        QVERIFY(exportButton);
        queryButton->click();
        auto *query = page.findChild<QDialog *>(QStringLiteral("MaskDlgQueryReport"));
        QVERIFY(query);
        auto *sample = query->findChild<QLineEdit *>(QStringLiteral("ldtSampleID"));
        auto *calendar = query->findChild<QPushButton *>(QStringLiteral("btnCalendarStart"));
        QVERIFY(sample);
        QVERIFY(calendar);
        sample->setText(QStringLiteral("1002"));
        calendar->click();
        QVERIFY(page.requestLeave(QStringLiteral("切换页面")));
        QVERIFY(!query->isVisible());
        for (QDialog *child : query->findChildren<QDialog *>()) QVERIFY(!child->isVisible());
        QCOMPARE(queries.size(), 0);
        QVERIFY(page.currentFilter().sampleNumber.isEmpty());

        exportButton->click();
        auto *exportDialog = page.findChild<QDialog *>(QStringLiteral("MaskDlgExport"));
        QVERIFY(exportDialog);
        auto *chooseFile = exportDialog->findChild<QPushButton *>(QStringLiteral("btnSave"));
        QVERIFY(chooseFile);
        chooseFile->click();
        auto *chooser = page.findChild<QFileDialog *>(QStringLiteral("historyCsvFileDialog"));
        QVERIFY(chooser);
        QVERIFY(page.requestLeave(QStringLiteral("切换页面")));
        QVERIFY(!chooser->isVisible());
        QCOMPARE(exports.size(), 0);
    }
};

QTEST_MAIN(HistoryPageGuardTest)
#include "history_page_guard_test.moc"
