#include "calibration_fixture.h"
#include "calibration/calibrationchart.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>

using namespace CalibrationGuiFixture;

namespace
{
QTableWidget *table(CalibrationPage *page)
{
    return page->findChild<QTableWidget *>(QStringLiteral("tbCalibrationResult"));
}
CalibrationChart *chart(CalibrationPage *page)
{
    return page->findChild<CalibrationChart *>(QStringLiteral("cuplotCalibration"));
}
QPushButton *button(CalibrationPage *page, const char *name)
{
    return page->findChild<QPushButton *>(QString::fromLatin1(name));
}
void answerQuestion(CalibrationPage *page, QMessageBox::StandardButton response)
{
    QTimer::singleShot(0, page, [page, response]()
    {
        auto *box = page->findChild<QMessageBox *>();
        if (!box || !box->button(response)) qFatal("Expected reviewable QMessageBox/button missing");
        box->button(response)->click();
    });
}
bool newDraft(CalibrationPage *page, bool accept = true)
{
    bool visited = false;
    QTimer::singleShot(0, page, [page, accept, &visited]()
    {
        auto *dialog = page->findChild<QDialog *>(QStringLiteral("calibrationNewDialog"));
        if (!dialog) qFatal("Expected local calibration input dialog missing");
        visited = true;
        if (!accept) { dialog->reject(); return; }
        dialog->findChild<QLineEdit *>(QStringLiteral("localBloodBatch"))->setText(QStringLiteral("ANON-CAL-20261025"));
        dialog->findChild<QSpinBox *>(QStringLiteral("localPointCount"))->setValue(3);
        dialog->findChild<QSpinBox *>(QStringLiteral("localRepeatCount"))->setValue(2);
        dialog->findChild<QComboBox *>(QStringLiteral("localBatch"))->setCurrentIndex(1);
        dialog->accept();
    });
    page->startNewRecord();
    return visited;
}
void fillPoints(CalibrationPage *page)
{
    const QStringList concentrations = {QStringLiteral("100.125"), QStringLiteral("50"), QStringLiteral("25")};
    const QStringList left = {QStringLiteral("12.25"), QStringLiteral("18"), QStringLiteral("30")};
    const QStringList right = {QStringLiteral("12.75"), QStringLiteral("20"), QStringLiteral("32")};
    for (int row = 0; row < 3; ++row)
    {
        // These are explicit anonymous input labels, not guessed clinical dilution parameters.
        table(page)->item(row, 1)->setText(QStringLiteral("OFFLINE_RATE_%1").arg(row + 1));
        table(page)->item(row, 2)->setText(concentrations.at(row));
        table(page)->item(row, 3)->setText(left.at(row));
        table(page)->item(row, 4)->setText(right.at(row));
    }
}
bool saveDraft(CalibrationPage *page)
{
    answerQuestion(page, QMessageBox::Save);
    page->saveCurrent();
    return waitUntil([page]() { return !page->isBusy() && !page->hasUnsavedChanges() && page->currentRecord().id > 0; });
}
}

class CalibrationPageTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        CalibrationStore::registerTypes();
        qApp->setStyle(QStringLiteral("Fusion"));
        qApp->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    }
    void normalUiSaveReopenAndShutdown()
    {
        Harness fixture;
        QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page));
        QCOMPARE(table(fixture.page)->rowCount(), 3);
        QVERIFY(table(fixture.page)->item(0, 1)->text().isEmpty());
        QVERIFY(table(fixture.page)->item(0, 2)->text().isEmpty());
        QVERIFY(chart(fixture.page)->rawPoints().isEmpty());
        fillPoints(fixture.page);
        QCOMPARE(chart(fixture.page)->rawPoints().size(), 3);
        QCOMPARE(chart(fixture.page)->rawPoints().first(), QPointF(100.125, 12.5));
        // Drive an actual cell editor using keyboard input, then commit it.
        QTableWidget *points = table(fixture.page);
        points->setCurrentCell(1, 3); points->editItem(points->item(1, 3));
        QVERIFY(waitUntil([points]() { return points->findChild<QLineEdit *>() != nullptr; }));
        auto *editor = points->findChild<QLineEdit *>();
        QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(editor, "18.5"); QTest::keyClick(editor, Qt::Key_Return);
        // QAbstractItemDelegate posts commit/close after Return. Wait for the
        // real model update rather than checking before its queued event runs.
        QTRY_COMPARE(points->item(1, 3)->text(), QStringLiteral("18.5"));
        QVERIFY(saveDraft(fixture.page));
        const qint64 id = fixture.page->currentRecord().id;
        QCOMPARE(fixture.page->records().size(), 1);
        QCOMPARE(fixture.page->currentRecord().points.at(1).measurements.first(), 18.5);
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 1);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(), 3);
        QCOMPARE(local.scalar("SELECT is_checked+is_applied FROM calibration_records").toInt(), 0);
        QVERIFY(fixture.reopenPage());
        QCOMPARE(fixture.page->currentRecord().id, id);
        QCOMPARE(table(fixture.page)->item(1, 3)->text(), QStringLiteral("18.5"));
        QCOMPARE(chart(fixture.page)->rawPoints().first(), QPointF(100.125, 12.5));
        QSignalSpy closed(fixture.store, &CalibrationStore::closed);
        fixture.shutdown();
        QCOMPARE(closed.count(), 1);
        QVERIFY(!fixture.thread.isRunning());
        qInfo() << "INPUT concentration mg/dL=[100.125,50,25], original Sec=[12.25,12.75]; OUTPUT raw first=(100.125,12.5), 3 SQL points, checked/applied=0, reopened id=" << id;
    }
    void emptyCatalogAndUnavailableClinicalActions()
    {
        Harness fixture;
        QVERIFY(fixture.initialize(false));
        QCOMPARE(fixture.page->findChild<QListWidget *>("lstProject")->count(), 0);
        QCOMPARE(table(fixture.page)->rowCount(), 0);
        QVERIFY(chart(fixture.page)->rawPoints().isEmpty());
        fixture.page->startNewRecord();
        QVERIFY(!fixture.page->hasUnsavedChanges());
        for (const char *name : {"btnCheck", "btnReCalculate", "btnShowChart", "btnCalculateIDSetting", "btnApply", "btn_use_curve_2"})
        {
            QVERIFY2(button(fixture.page, name), name);
            QVERIFY2(!button(fixture.page, name)->isEnabled(), name);
        }
        QVERIFY(!button(fixture.page, "btnSave")->isEnabled());
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 0);
    }
    void cancellationCopyAndSoftDelete()
    {
        Harness fixture;
        QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page, false));
        QVERIFY(!fixture.page->hasUnsavedChanges());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page);
        answerQuestion(fixture.page, QMessageBox::Cancel); fixture.page->saveCurrent();
        QVERIFY(fixture.page->hasUnsavedChanges());
        answerQuestion(fixture.page, QMessageBox::Cancel); QVERIFY(!fixture.page->prepareToLeave());
        QVERIFY(fixture.page->hasUnsavedChanges());
        answerQuestion(fixture.page, QMessageBox::Discard); QVERIFY(fixture.page->prepareToLeave());
        QVERIFY(!fixture.page->hasUnsavedChanges());
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 0);
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page); QVERIFY(saveDraft(fixture.page));
        const qint64 originalId = fixture.page->currentRecord().id;
        answerQuestion(fixture.page, QMessageBox::Cancel); fixture.page->copyCurrent();
        QCOMPARE(fixture.page->records().size(), 1);
        answerQuestion(fixture.page, QMessageBox::Yes); fixture.page->copyCurrent();
        QVERIFY(waitUntil([&fixture]() { return !fixture.page->isBusy() && fixture.page->records().size() == 2; }));
        QVERIFY(fixture.page->currentRecord().id != originalId);
        QCOMPARE(fixture.page->currentRecord().sourceId, originalId);
        QCOMPARE(fixture.page->currentRecord().points.first().measurements, QList<double>({12.25,12.75}));
        answerQuestion(fixture.page, QMessageBox::Cancel); fixture.page->deleteCurrent();
        QCOMPARE(fixture.page->records().size(), 2);
        answerQuestion(fixture.page, QMessageBox::Yes); fixture.page->deleteCurrent();
        QVERIFY(waitUntil([&fixture]() { return !fixture.page->isBusy() && fixture.page->records().size() == 1; }));
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 2);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_points").toInt(), 6);
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records WHERE visible=0").toInt(), 1);
        QCOMPARE(local.scalar("SELECT sum(is_checked+is_applied) FROM calibration_records").toInt(), 0);
        QVERIFY(fixture.reopenPage()); QCOMPARE(fixture.page->records().size(), 1);
    }
    void illegalInputClearsPlot_data()
    {
        QTest::addColumn<int>("row"); QTest::addColumn<int>("column"); QTest::addColumn<QString>("input");
        QTest::newRow("empty-concentration") << 0 << 2 << QString();
        QTest::newRow("NaN") << 0 << 3 << QStringLiteral("nan");
        QTest::newRow("Inf") << 0 << 3 << QStringLiteral("inf");
        QTest::newRow("negative") << 0 << 2 << QStringLiteral("-1");
        QTest::newRow("too-large") << 0 << 3 << QStringLiteral("100000");
        QTest::newRow("over-precision") << 0 << 2 << QStringLiteral("1.12345");
        QTest::newRow("duplicate") << 1 << 2 << QStringLiteral("100.125");
        QTest::newRow("empty-repeat") << 0 << 4 << QString();
        QTest::newRow("empty-dilution-label") << 0 << 1 << QString();
    }
    void illegalInputClearsPlot()
    {
        QFETCH(int, row); QFETCH(int, column); QFETCH(QString, input);
        Harness fixture; QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page);
        QCOMPARE(chart(fixture.page)->rawPoints().size(), 3);
        table(fixture.page)->item(row, column)->setText(input);
        QVERIFY(chart(fixture.page)->rawPoints().isEmpty());
        QSignalSpy operations(fixture.page, &CalibrationPage::operationFinished);
        fixture.page->saveCurrent();
        QCOMPARE(operations.count(), 1);
        QCOMPARE(operations.first().at(0).toString(), QStringLiteral("save"));
        QCOMPARE(operations.first().at(1).toBool(), false);
        QVERIFY(fixture.page->hasUnsavedChanges());
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT count(*) FROM calibration_records").toInt(), 0);
        fillPoints(fixture.page); QCOMPARE(chart(fixture.page)->rawPoints().size(), 3);
    }
    void lockFailureKeepsDraftBusyGuardAndRetry()
    {
        Harness fixture; QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page); QVERIFY(saveDraft(fixture.page));
        Database local(fixture.localPath());
        QVERIFY(local.exec("BEGIN IMMEDIATE"));
        table(fixture.page)->item(0, 3)->setText(QStringLiteral("40"));
        QSignalSpy errors(fixture.store, &CalibrationStore::error);
        answerQuestion(fixture.page, QMessageBox::Save); fixture.page->saveCurrent();
        QVERIFY(fixture.page->isBusy());
        QVERIFY(!fixture.page->prepareToLeave());
        QVERIFY(!button(fixture.page, "btnSave")->isEnabled());
        fixture.page->saveCurrent(); fixture.page->copyCurrent(); fixture.page->deleteCurrent(); fixture.page->reload();
        QVERIFY(waitUntil([&errors, &fixture]() { return errors.count() == 1 && !fixture.page->isBusy(); }));
        QVERIFY(fixture.page->hasUnsavedChanges());
        QCOMPARE(table(fixture.page)->item(0, 3)->text(), QStringLiteral("40"));
        QCOMPARE(local.scalar("SELECT revision FROM calibration_records").toLongLong(), qint64(1));
        const auto oldValues = QJsonDocument::fromJson(local.scalar("SELECT measurements FROM calibration_points WHERE point_index=1").toByteArray()).array();
        QCOMPARE(oldValues.first().toDouble(), 12.25);
        QVERIFY(local.exec("ROLLBACK"));
        QVERIFY(saveDraft(fixture.page));
        QCOMPARE(fixture.page->currentRecord().revision, qint64(2));
        QCOMPARE(fixture.page->currentRecord().points.first().measurements.first(), 40.0);
        QCOMPARE(errors.count(), 1);
        qInfo() << "INPUT competing SQL BEGIN IMMEDIATE; OUTPUT one real save error, draft40 retained, original12.25 unchanged; ROLLBACK then explicit retry revision2";
    }
    void guestCannotEditAndRestoredSessionCan()
    {
        Harness fixture; QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page); QVERIFY(saveDraft(fixture.page));
        AccountSession guest; fixture.page->setSession(guest);
        QVERIFY(!button(fixture.page, "btnSave")->isEnabled());
        QVERIFY(!button(fixture.page, "btnCopy")->isEnabled());
        QVERIFY(!button(fixture.page, "btnDelLine")->isEnabled());
        QCOMPARE(table(fixture.page)->editTriggers(), QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
        QVERIFY(!fixture.page->findChild<QAction *>("calibrationNewRecord")->isEnabled());
        fixture.page->startNewRecord(); fixture.page->copyCurrent(); fixture.page->deleteCurrent(); fixture.page->saveCurrent();
        QCOMPARE(fixture.page->records().size(), 1); QVERIFY(!fixture.page->hasUnsavedChanges());
        AccountSession blocked = engineer(); blocked.actionReady = false; fixture.page->setSession(blocked);
        QVERIFY(!button(fixture.page, "btnSave")->isEnabled());
        fixture.page->setSession(engineer());
        QVERIFY(button(fixture.page, "btnSave")->isEnabled());
    }
    void staleSnapshotCannotReplaceCurrentRecord()
    {
        Harness fixture; QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page); QVERIFY(saveDraft(fixture.page));
        const qint64 selectedId = fixture.page->currentRecord().id;
        QSignalSpy snapshots(fixture.page, &CalibrationPage::snapshotChanged);
        QMetaObject::invokeMethod(fixture.store, "snapshotReady", Qt::QueuedConnection,
            Q_ARG(qint64, qint64(-900)), Q_ARG(CalibrationRecords, CalibrationRecords()));
        QMetaObject::invokeMethod(fixture.store, "catalogReady", Qt::QueuedConnection,
            Q_ARG(qint64, qint64(-901)), Q_ARG(ProjectRecords, ProjectRecords()),
            Q_ARG(ProjectUnits, ProjectUnits()), Q_ARG(ReagentSnapshot, ReagentSnapshot()));
        QTest::qWait(80);
        QCOMPARE(snapshots.count(), 0); QCOMPARE(fixture.page->records().size(), 1);
        QCOMPARE(fixture.page->currentRecord().id, selectedId);
        QCOMPARE(chart(fixture.page)->rawPoints().size(), 3);
    }
    void externalCatalogChangePreservesDraft_data()
    {
        QTest::addColumn<bool>("removeProject");
        QTest::newRow("rename-project") << false;
        QTest::newRow("delete-project") << true;
    }
    void externalCatalogChangePreservesDraft()
    {
        QFETCH(bool, removeProject);
        Harness fixture; QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page); QVERIFY(saveDraft(fixture.page));
        const qint64 selectedId = fixture.page->currentRecord().id;
        table(fixture.page)->item(0, 3)->setText(QStringLiteral("44"));
        QVERIFY(fixture.catalog.exec(removeProject ? QStringLiteral("DELETE FROM project_base WHERE id=1")
            : QStringLiteral("UPDATE project_base SET project_en_name='RENAMED' WHERE id=1")));
        fixture.page->setProjects(removeProject ? ProjectRecords() : projects(QStringLiteral("RENAMED")), units());
        auto *list = fixture.page->findChild<QListWidget *>("lstProject");
        QVERIFY(list->currentItem()); QCOMPARE(list->currentItem()->data(Qt::UserRole).toLongLong(), qint64(1));
        QCOMPARE(fixture.page->currentRecord().id, selectedId);
        QCOMPARE(table(fixture.page)->item(0, 3)->text(), QStringLiteral("44"));
        QVERIFY(fixture.page->hasUnsavedChanges());
        QSignalSpy errors(fixture.store, &CalibrationStore::error);
        answerQuestion(fixture.page, QMessageBox::Save); fixture.page->saveCurrent();
        QVERIFY(waitUntil([&errors, &fixture]() { return errors.count() == 1 && !fixture.page->isBusy(); }));
        QVERIFY(fixture.page->hasUnsavedChanges());
        QCOMPARE(table(fixture.page)->item(0, 3)->text(), QStringLiteral("44"));
        Database local(fixture.localPath());
        QCOMPARE(local.scalar("SELECT revision FROM calibration_records").toLongLong(), qint64(1));
        QCOMPARE(local.scalar("SELECT project_name FROM calibration_records").toString(), QStringLiteral("LOCAL_CAL"));
    }
    void catalogChangeDuringNewDialogRejectsStaleDraft()
    {
        Harness fixture; QVERIFY(fixture.initialize());
        bool dialogVisited = false, modalBusy = false, leaveDenied = false;
        QTimer::singleShot(0, fixture.page, [&fixture, &dialogVisited, &modalBusy, &leaveDenied]()
        {
            auto *dialog = fixture.page->findChild<QDialog *>("calibrationNewDialog");
            if (!dialog) qFatal("Expected new dialog missing");
            dialogVisited = true;
            modalBusy = fixture.page->isBusy(); leaveDenied = !fixture.page->prepareToLeave();
            fixture.page->setProjects({}, units());
            dialog->findChild<QLineEdit *>("localBloodBatch")->setText(QStringLiteral("ANON-CAL-STALE"));
            dialog->accept();
        });
        fixture.page->startNewRecord();
        QVERIFY(dialogVisited); QVERIFY(modalBusy); QVERIFY(leaveDenied);
        QVERIFY(!fixture.page->hasUnsavedChanges());
        QVERIFY(!fixture.page->isBusy()); QCOMPARE(fixture.page->currentRecord().id, qint64(0));
        QCOMPARE(table(fixture.page)->rowCount(), 0);
    }
    void screenshotsAndScrollReachability()
    {
        const QString output = qEnvironmentVariable("CALIBRATION_SCREENSHOT_DIR");
        if (output.isEmpty()) QSKIP("Set CALIBRATION_SCREENSHOT_DIR for serial GUI screenshot evidence");
        QVERIFY(QDir().mkpath(output));
        Harness fixture; QVERIFY(fixture.initialize());
        QVERIFY(newDraft(fixture.page)); fillPoints(fixture.page); QVERIFY(saveDraft(fixture.page));
        fixture.host.setWindowTitle(QStringLiteral("SN100 校准本地记录：匿名原始点，未审核／未应用"));
        fixture.host.resize(1920, 1080); QTest::qWait(60);
        QCOMPARE(fixture.page->size(), QSize(1751, 968));
        QVERIFY(fixture.host.grab().save(QDir(output).filePath(QStringLiteral("calibration_1920.png"))));
        fixture.host.resize(1600, 900); fixture.host.horizontalScrollBar()->setValue(0);
        fixture.host.verticalScrollBar()->setValue(0); QTest::qWait(60);
        QVERIFY(fixture.host.grab().save(QDir(output).filePath(QStringLiteral("calibration_1600_top.png"))));
        QVERIFY(fixture.host.horizontalScrollBar()->maximum() > 0);
        QVERIFY(fixture.host.verticalScrollBar()->maximum() > 0);
        fixture.host.horizontalScrollBar()->setValue(fixture.host.horizontalScrollBar()->maximum());
        QTest::qWait(30);
        QVERIFY(fixture.host.grab().save(QDir(output).filePath(QStringLiteral("calibration_1600_right_top.png"))));
        fixture.host.verticalScrollBar()->setValue(fixture.host.verticalScrollBar()->maximum()); QTest::qWait(30);
        for (const char *name : {"btnSave", "btnCancel"})
        {
            QWidget *control = button(fixture.page, name);
            const QRect target(control->mapTo(fixture.host.viewport(), QPoint()), control->size());
            QVERIFY2(fixture.host.viewport()->rect().contains(target), name);
        }
        QVERIFY(fixture.host.grab().save(QDir(output).filePath(QStringLiteral("calibration_1600_right_bottom.png"))));
        qInfo() << "SCREEN_REACHABILITY logical1751x968; hosts1920x1080/1600x900; right+bottom scrolling reaches entire Save/Cancel buttons; QPA=" << qEnvironmentVariable("QT_QPA_PLATFORM");
    }
};

QTEST_MAIN(CalibrationPageTest)
#include "calibration_page_test.moc"
