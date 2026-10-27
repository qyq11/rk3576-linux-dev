#include <QtTest>
#include <QDialog>
#include <QFile>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlError>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include "history/history_store.h"
#include "history/history_page.h"
#include "history/report_remaining_display_dialog.h"

namespace
{
bool signalArrives(QSignalSpy &spy) { return !spy.isEmpty() || spy.wait(5000); }
AccountSession writer()
{
    AccountSession session;
    session.name = QStringLiteral("Anonymous local test");
    session.role = 4;
    session.authenticated = true;
    session.actionReady = false; // Local metadata is independent of hardware readiness.
    session.source = AccountSessionSource::Simulated;
    return session;
}
HistoryFilter allReports()
{
    HistoryFilter filter;
    filter.start = filter.end = QDateTime(QDate(2026, 10, 23), QTime(0, 0));
    return filter;
}
class Session
{
public:
    explicit Session(QString path) : directory(path)
    {
        store = new HistoryStore;
        store->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        worker.start();
    }
    ~Session()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
    }
    bool initialize()
    {
        QSignalSpy ready(store, &HistoryStore::ready);
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection, Q_ARG(QString, directory));
        if (!signalArrives(ready)) return false;
        QMetaObject::invokeMethod(store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, writer()));
        return true;
    }
    HistoryStore *store;
    QThread worker;
    QString directory;
};
class Inspection
{
public:
    explicit Inspection(const QString &directory)
    {
        name = QUuid::createUuid().toString();
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(QDir(directory).filePath(QStringLiteral("history.sqlite")));
        db.open();
    }
    ~Inspection() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name); }
    bool exec(const QString &sql) { QSqlQuery query(db); return query.exec(sql); }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(db);
        if (!query.exec(sql) || !query.next()) return QVariant();
        return query.value(0);
    }
    QString name;
    QSqlDatabase db;
};
void hide(HistoryStore *store, qint64 id, HistoryIds ids)
{
    QMetaObject::invokeMethod(store, "hideReports", Qt::QueuedConnection,
        Q_ARG(qint64, id), Q_ARG(HistoryIds, ids));
}
void load(HistoryStore *store, qint64 id)
{
    QMetaObject::invokeMethod(store, "loadDisplay", Qt::QueuedConnection, Q_ARG(qint64, id));
}
void save(HistoryStore *store, qint64 id, HistoryDisplayColumns columns)
{
    QMetaObject::invokeMethod(store, "saveDisplay", Qt::QueuedConnection,
        Q_ARG(qint64, id), Q_ARG(HistoryDisplayColumns, columns));
}
void answerBox(const QString &name, QMessageBox::StandardButton answer)
{
    QTimer::singleShot(0, qApp, [name, answer]() {
        auto *box = qApp->activeModalWidget();
        if (box && box->objectName() == name) qobject_cast<QMessageBox *>(box)->button(answer)->click();
    });
}
}

class ReportRemainingTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        HistoryStore::registerTypes();
        root = qEnvironmentVariable("SN100_REPORT_REMAINING_DIR");
        QVERIFY(QDir::isAbsolutePath(root));
        QVERIFY(QDir().mkpath(root));
        qInfo().noquote() << "ANONYMOUS_EVIDENCE" << root << "Qt" << qVersion() << "QPA" << QGuiApplication::platformName();
    }
    void guestEmptyInvalidAndDuplicate()
    {
        Session session(root + "/invalid");
        QVERIFY(session.initialize());
        QSignalSpy errors(session.store, &HistoryStore::error);
        QSignalSpy hidden(session.store, &HistoryStore::reportsHidden);
        QMetaObject::invokeMethod(session.store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, AccountSession()));
        hide(session.store, 1, {2});
        QVERIFY(signalArrives(errors));
        QVERIFY(errors.takeFirst().at(2).toString().contains("Guest"));
        QMetaObject::invokeMethod(session.store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, writer()));
        const QList<HistoryIds> invalid{{}, {0}, {2, 2}, {-1}};
        for (const auto &ids : invalid)
        {
            hide(session.store, 2, ids);
            QVERIFY(signalArrives(errors)); errors.clear();
        }
        QCOMPARE(hidden.size(), 0);
        Inspection db(session.directory);
        QCOMPARE(db.scalar("SELECT COUNT(*) FROM report_data WHERE visible=1").toInt(), 5);
        qInfo() << "Guest, empty, zero, negative, duplicate: no visibility changes";
    }
    void linkedRowsRemainAndBatchRollback()
    {
        Session session(root + "/linked"); QVERIFY(session.initialize());
        QSignalSpy errors(session.store, &HistoryStore::error), hidden(session.store, &HistoryStore::reportsHidden);
        Inspection db(session.directory);
        QVERIFY(db.exec("CREATE TABLE synthetic_report_reference(report_id INTEGER REFERENCES report_data(id))"));
        QVERIFY(db.exec("INSERT INTO synthetic_report_reference VALUES(2)"));
        const int results = db.scalar("SELECT COUNT(*) FROM report_results").toInt();
        const int values = db.scalar("SELECT COUNT(*) FROM report_values").toInt();
        hide(session.store, 1, {2, 999}); QVERIFY(signalArrives(errors)); errors.clear();
        QCOMPARE(db.scalar("SELECT visible FROM report_data WHERE id=2").toInt(), 1);
        hide(session.store, 2, {2}); QVERIFY(signalArrives(hidden));
        QCOMPARE(db.scalar("SELECT visible FROM report_data WHERE id=2").toInt(), 0);
        QCOMPARE(db.scalar("SELECT COUNT(*) FROM report_results").toInt(), results);
        QCOMPARE(db.scalar("SELECT COUNT(*) FROM report_values").toInt(), values);
        QCOMPARE(db.scalar("SELECT COUNT(*) FROM synthetic_report_reference").toInt(), 1);
        hide(session.store, 3, {2}); QVERIFY(signalArrives(errors));
        QCOMPARE(hidden.size(), 1);
        QCOMPARE(db.scalar("PRAGMA integrity_check").toString(), QString("ok"));
        qInfo() << "id=2 hidden; references/results/values unchanged; invalid mixed batch rolled back; repeated deletion rejected";
    }
    void databaseLockAndWriteFailureRecovery()
    {
        Session session(root + "/failure"); QVERIFY(session.initialize());
        Inspection db(session.directory);
        QSignalSpy errors(session.store, &HistoryStore::error), hidden(session.store, &HistoryStore::reportsHidden);
        QVERIFY(db.exec("BEGIN IMMEDIATE"));
        hide(session.store, 1, {2}); QVERIFY(signalArrives(errors)); errors.clear();
        QVERIFY(db.exec("ROLLBACK"));
        QCOMPARE(db.scalar("SELECT visible FROM report_data WHERE id=2").toInt(), 1);
        QVERIFY(db.exec("CREATE TRIGGER injected_report_write_failure BEFORE UPDATE ON report_data WHEN NEW.id=3 AND NEW.visible=0 BEGIN SELECT RAISE(ABORT,'synthetic write failure'); END"));
        hide(session.store, 2, {2, 3}); QVERIFY(signalArrives(errors)); errors.clear();
        QCOMPARE(db.scalar("SELECT COUNT(*) FROM report_data WHERE id IN (2,3) AND visible=1").toInt(), 2);
        QVERIFY(db.exec("DROP TRIGGER injected_report_write_failure"));
        hide(session.store, 3, {2, 3}); QVERIFY(signalArrives(hidden));
        QCOMPARE(db.scalar("SELECT COUNT(*) FROM report_data WHERE id IN (2,3) AND visible=1").toInt(), 0);
        qInfo() << "real SQLite lock and injected SQL write failure rolled back; same store retry hid 2 reports";
    }
    void displayValidationFailureAndRecovery()
    {
        Session session(root + "/display"); QVERIFY(session.initialize());
        QSignalSpy loaded(session.store, &HistoryStore::displayLoaded), saved(session.store, &HistoryStore::displaySaved), errors(session.store, &HistoryStore::error);
        load(session.store, 1); QVERIFY(signalArrives(loaded));
        auto columns = qvariant_cast<HistoryDisplayColumns>(loaded.takeFirst().at(1));
        QCOMPARE(columns.size(), 10);
        auto invalid = columns; invalid[0].key = "unknown";
        save(session.store, 2, invalid); QVERIFY(signalArrives(errors)); errors.clear();
        invalid = columns; invalid[1].key = invalid[0].key;
        save(session.store, 3, invalid); QVERIFY(signalArrives(errors)); errors.clear();
        invalid = columns; invalid[4].visible = false;
        save(session.store, 4, invalid); QVERIFY(signalArrives(errors)); errors.clear();
        invalid = columns; invalid.removeLast();
        save(session.store, 5, invalid); QVERIFY(signalArrives(errors)); errors.clear();
        columns[1].visible = false;
        columns.move(2, 0);
        Inspection db(session.directory);
        QVERIFY(db.exec("BEGIN IMMEDIATE"));
        save(session.store, 6, columns); QVERIFY(signalArrives(errors)); errors.clear();
        QVERIFY(db.exec("ROLLBACK"));
        save(session.store, 7, columns); QVERIFY(signalArrives(saved));
        QVERIFY(db.exec("CREATE TRIGGER injected_display_failure BEFORE INSERT ON report_view_preferences BEGIN SELECT RAISE(ABORT,'synthetic settings failure'); END"));
        auto other = columns; other[2].visible = true;
        save(session.store, 8, other); QVERIFY(signalArrives(errors)); errors.clear();
        QVERIFY(db.exec("DROP TRIGGER injected_display_failure"));
        QCOMPARE(db.scalar("SELECT visible FROM report_view_preferences WHERE column_key='patient:age'").toInt(), 0);
        save(session.store, 9, other); QTRY_COMPARE(saved.size(), 2);
        qInfo() << "10 stable columns; invalid key/duplicate/required/count rejected; lock and INSERT failure restored saved settings";
    }
    void persistenceWrite()
    {
        QVERIFY(!QFileInfo::exists(root + "/persist/history.sqlite"));
        Session session(root + "/persist"); QVERIFY(session.initialize());
        QSignalSpy hidden(session.store, &HistoryStore::reportsHidden), loaded(session.store, &HistoryStore::displayLoaded), saved(session.store, &HistoryStore::displaySaved);
        hide(session.store, 1, {2}); QVERIFY(signalArrives(hidden));
        load(session.store, 2); QVERIFY(signalArrives(loaded));
        auto columns = qvariant_cast<HistoryDisplayColumns>(loaded.first().at(1));
        columns[1].visible = false; columns.move(2, 0);
        save(session.store, 3, columns); QVERIFY(signalArrives(saved));
    }
    void persistenceRead()
    {
        Session session(root + "/persist"); QVERIFY(session.initialize());
        QSignalSpy loaded(session.store, &HistoryStore::displayLoaded), queried(session.store, &HistoryStore::queryReady);
        load(session.store, 1); QVERIFY(signalArrives(loaded));
        auto columns = qvariant_cast<HistoryDisplayColumns>(loaded.first().at(1));
        QCOMPARE(columns.first().key, QString("patient:sex"));
        bool ageHidden = false;
        for (const auto &column : columns) if (column.key == "patient:age") ageHidden = !column.visible;
        QVERIFY(ageHidden);
        QMetaObject::invokeMethod(session.store, "query", Qt::QueuedConnection, Q_ARG(qint64, 2), Q_ARG(HistoryFilter, allReports()));
        QVERIFY(signalArrives(queried));
        QCOMPARE(queried.first().at(3).toLongLong(), qint64(4));
        for (const auto &report : qvariant_cast<HistoryReports>(queried.first().at(2))) QVERIFY(report.id != 2);
        qInfo() << "reopened persisted hidden id=2, patient:sex first, patient:age hidden; 4 reports visible";
    }
    void csvFrozenUnitPrecisionAndTextRegression()
    {
        Session session(root + "/csv"); QVERIFY(session.initialize());
        QSignalSpy csv(session.store, &HistoryStore::csvExported), loaded(session.store, &HistoryStore::displayLoaded), saved(session.store, &HistoryStore::displaySaved);
        const QString before = session.directory + "/exports/before.csv", after = session.directory + "/exports/after.csv";
        const auto exportTo = [&](qint64 request, const QString &path) {
            QMetaObject::invokeMethod(session.store, "exportCsv", Qt::QueuedConnection,
                Q_ARG(qint64, request), Q_ARG(HistoryFilter, allReports()), Q_ARG(HistoryIds, HistoryIds()), Q_ARG(QString, path));
        };
        exportTo(1, before); QVERIFY(signalArrives(csv));
        QCOMPARE(csv.first().at(2).toLongLong(), qint64(5));
        QCOMPARE(csv.first().at(3).toLongLong(), qint64(6));
        load(session.store, 2); QVERIFY(signalArrives(loaded));
        auto columns = qvariant_cast<HistoryDisplayColumns>(loaded.first().at(1));
        columns[0].visible = false; columns.move(5, 4);
        save(session.store, 3, columns); QVERIFY(signalArrives(saved));
        exportTo(4, after); QTRY_COMPARE(csv.size(), 2);
        QFile first(before), second(after); QVERIFY(first.open(QIODevice::ReadOnly)); QVERIFY(second.open(QIODevice::ReadOnly));
        const auto bytes = first.readAll(); QCOMPARE(second.readAll(), bytes);
        const QString text = QTextCodec::codecForName("GBK")->toUnicode(bytes);
        QVERIFY(text.contains("123.456")); QVERIFY(text.contains("12.3"));
        QVERIFY(text.contains(QStringLiteral("匿名,"))); QVERIFY(text.contains(QStringLiteral("待确认")));
        QVERIFY(text.contains("****"));
        qInfo() << "GBK CSV 5 reports/6 rows, Chinese/quotes/newlines, D-D factor1000 ->123.456, PT 1dp; byte-identical after display reorder/hide";
    }
    void dialogCancelRequiredAndBusy()
    {
        HistoryDisplayColumns defaults{{"patient:name", QStringLiteral("姓名"), 1, true, false},
            {"patient:age", QStringLiteral("年龄"), 1, true, false},
            {"result:PT", "PT s", 2, true, true}, {"result:INR", "INR", 2, true, false}};
        auto columns = defaults; columns.move(1, 0);
        ReportRemainingDisplayDialog dialog(columns, defaults);
        QSignalSpy saves(&dialog, &ReportRemainingDisplayDialog::saveRequested);
        dialog.show();
        auto *required = dialog.findChild<QPushButton *>("displayColumn_result:PT"); QVERIFY(required);
        required->click(); QVERIFY(required->isChecked());
        auto *name = dialog.findChild<QPushButton *>("displayColumn_patient:name"); QVERIFY(name);
        name->click(); QVERIFY(dialog.hasUnsavedChanges());
        dialog.findChild<QPushButton *>("btnDefaultSort")->click();
        auto *list = dialog.findChild<QListWidget *>("lstDisplay_patientInfo");
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), QString("patient:name"));
        QVERIFY(!dialog.findChild<QPushButton *>("displayColumn_patient:name")->isChecked());
        dialog.findChild<QPushButton *>("btnSave")->click(); QCOMPARE(saves.size(), 1);
        QVERIFY(!dialog.close());
        QTest::keyClick(&dialog, Qt::Key_Escape); QVERIFY(dialog.isVisible());
        dialog.findChild<QPushButton *>("btnSave")->click(); QCOMPARE(saves.size(), 1);
        dialog.saveFailed(QStringLiteral("synthetic write failure"));
        QVERIFY(dialog.hasUnsavedChanges());
        dialog.findChild<QPushButton *>("btnSave")->click(); QCOMPARE(saves.size(), 2);
        dialog.acceptSaved(); QVERIFY(!dialog.isVisible());
        ReportRemainingDisplayDialog cancel(columns, defaults);
        QSignalSpy cancelledSaves(&cancel, &ReportRemainingDisplayDialog::saveRequested);
        cancel.show(); cancel.findChild<QPushButton *>("btnDefaultSort")->click();
        cancel.findChild<QPushButton *>("btnCancel")->click(); QCOMPARE(cancelledSaves.size(), 0);
        qInfo() << "required cannot uncheck; reset retains visibility; busy close/Escape/duplicate rejected; failure draft retried; cancel emitted no write";
    }
    void actualPageDeleteCancelBusyAndDisplayReopen()
    {
        Session session(root + "/page");
        HistoryPage page(session.store); page.setDataDirectory(session.directory);
        page.setSession(writer());
        QVERIFY(session.initialize());
        QTRY_VERIFY(page.isReady() && !page.isBusy());
        QSignalSpy hideRequests(&page, &HistoryPage::requestHideReports);
        page.findChild<QPushButton *>("btnDel")->click();
        QCOMPARE(hideRequests.size(), 0); // Today's initial query is empty.
        auto *query = page.findChild<QPushButton *>("btnQuery"); query->click();
        auto *dialog = page.findChild<QDialog *>("MaskDlgQueryReport"); QVERIFY(dialog);
        dialog->findChild<QLineEdit *>("ldtStartDate")->setText("2026/10/22");
        dialog->findChild<QLineEdit *>("ldtEndDate")->setText("2026/10/24");
        dialog->findChild<QPushButton *>("btnSave")->click();
        QTRY_VERIFY(!page.isBusy());
        auto *reports = page.findChild<QTableWidget *>("tableResultReportOne");
        QCOMPARE(reports->rowCount(), 5);
        answerBox("historyDeleteDialog", QMessageBox::No);
        page.findChild<QPushButton *>("btnDel")->click(); QCOMPARE(hideRequests.size(), 0);
        page.setReportOperationBusy(true);
        page.findChild<QPushButton *>("btnDel")->click(); QCOMPARE(hideRequests.size(), 0);
        QVERIFY(!page.requestLeave(QStringLiteral("退出")));
        page.setReportOperationBusy(false);
        reports->item(1, 0)->setCheckState(Qt::Checked);
        Inspection inspection(session.directory);
        QVERIFY(inspection.exec("BEGIN IMMEDIATE"));
        answerBox("historyDeleteDialog", QMessageBox::Yes);
        page.findChild<QPushButton *>("btnDel")->click();
        QTRY_VERIFY(!page.isBusy()); QCOMPARE(reports->rowCount(), 5);
        QCOMPARE(hideRequests.size(), 1);
        QVERIFY(reports->item(1, 0)->checkState() == Qt::Checked);
        QVERIFY(inspection.exec("ROLLBACK"));
        answerBox("historyDeleteDialog", QMessageBox::Yes);
        page.findChild<QPushButton *>("btnDel")->click(); QCOMPARE(hideRequests.size(), 2);
        QTRY_VERIFY(!page.isBusy()); QCOMPARE(reports->rowCount(), 4);
        page.findChild<QPushButton *>("btnDisplay")->click();
        QTRY_VERIFY(page.findChild<ReportRemainingDisplayDialog *>() != nullptr);
        auto *display = page.findChild<ReportRemainingDisplayDialog *>();
        display->findChild<QPushButton *>("displayColumn_patient:age")->click();
        QVERIFY(page.hasUnsavedChanges());
        answerBox("historyDisplayDiscardDialog", QMessageBox::Cancel);
        QVERIFY(!page.requestLeave(QStringLiteral("切页")));
        display->findChild<QPushButton *>("btnSave")->click();
        QTRY_VERIFY(!page.isBusy());
        auto *patients = page.findChild<QTableWidget *>("tablePatientInfo");
        QVERIFY(patients->isColumnHidden(1)); QVERIFY(!page.hasUnsavedChanges());
        QTRY_VERIFY(page.findChild<ReportRemainingDisplayDialog *>() == nullptr);
        page.findChild<QPushButton *>("btnDisplay")->click();
        QTRY_VERIFY(page.findChild<ReportRemainingDisplayDialog *>() != nullptr);
        display = page.findChild<ReportRemainingDisplayDialog *>();
        display->findChild<QPushButton *>("displayColumn_patient:name")->click();
        page.setReportOperationBusy(true);
        page.setSession(AccountSession());
        page.setReportOperationBusy(false);
        display->findChild<QPushButton *>("btnSave")->click();
        QVERIFY(display->windowTitle().contains(QStringLiteral("身份已失效")));
        QVERIFY(display->findChild<QPushButton *>("btnCancel")->isEnabled());
        display->findChild<QPushButton *>("btnCancel")->click();
        QVERIFY(!page.findChild<QPushButton *>("btnDel")->isEnabled());
        QVERIFY(!page.findChild<QPushButton *>("btnDisplay")->isEnabled());
        QVERIFY(page.requestLeave(QStringLiteral("退出")));
        QVERIFY(page.close());
        qInfo() << "actual widget+worker+SQLite: empty/cancel unchanged, write lock keeps checked selection, retry succeeds, busy guard, committed age hide, external busy+Guest revoke recovers and normal close";
    }
    void savedColumnOrderAndDeleteLastPage()
    {
        Session session(root + "/pagination");
        HistoryPage page(session.store); page.setDataDirectory(session.directory); page.setSession(writer());
        QVERIFY(session.initialize()); QTRY_VERIFY(page.isReady() && !page.isBusy());
        Inspection inspection(session.directory);
        for (int id = 7; id <= 22; ++id)
        {
            QVERIFY(inspection.exec(QString("INSERT INTO report_data SELECT %1,%2,sn,sample_id||'-extra-%1',data_time,visible,state,name,sex,age,department,patient_type,application_serial,bed_number,jianyan,checker,auditor,remark FROM report_data WHERE id=2").arg(id).arg(100 + id)));
        }
        QSignalSpy loaded(session.store, &HistoryStore::displayLoaded), saved(session.store, &HistoryStore::displaySaved);
        load(session.store, 900); QVERIFY(signalArrives(loaded));
        auto columns = qvariant_cast<HistoryDisplayColumns>(loaded.first().at(1));
        columns.move(2, 0); // Sex before name; logical field IDs stay fixed.
        columns.move(5, 4); // INR before PT within PT, never a unit/value edit.
        save(session.store, 901, columns); QVERIFY(signalArrives(saved));
        page.findChild<QPushButton *>("btnQuery")->click();
        auto *query = page.findChild<QDialog *>("MaskDlgQueryReport"); QVERIFY(query);
        query->findChild<QLineEdit *>("ldtStartDate")->setText("2026/10/22");
        query->findChild<QLineEdit *>("ldtEndDate")->setText("2026/10/24");
        query->findChild<QPushButton *>("btnSave")->click(); QTRY_VERIFY(!page.isBusy());
        auto *patients = page.findChild<QTableWidget *>("tablePatientInfo");
        QCOMPARE(patients->horizontalHeader()->visualIndex(2), 0);
        auto *results = page.findChild<QTableWidget *>("tableResultReport");
        QCOMPARE(results->item(0, 0)->text(), QString("INR"));
        auto *reports = page.findChild<QTableWidget *>("tableResultReportOne"); QCOMPARE(reports->rowCount(), 20);
        page.findChild<QPushButton *>("btnLastPage")->click(); QTRY_VERIFY(!page.isBusy());
        QCOMPARE(reports->rowCount(), 1); QCOMPARE(page.currentFilter().offset, 20);
        answerBox("historyDeleteDialog", QMessageBox::Yes);
        page.findChild<QPushButton *>("btnDel")->click(); QTRY_VERIFY(!page.isBusy());
        QCOMPARE(page.currentFilter().offset, 0); QCOMPARE(reports->rowCount(), 20);
        auto *currentPage = page.findChild<QLabel *>("ldtCurPage"); QVERIFY(currentPage);
        QCOMPARE(currentPage->text(), QString("1"));
        QVERIFY(page.close());
        qInfo() << "saved stable-key patient/result order applied; 21 reports page2 last-row deletion returns page1 with20 rows";
    }
private:
    QString root;
};
QTEST_MAIN(ReportRemainingTest)
#include "report_remaining_test.moc"
