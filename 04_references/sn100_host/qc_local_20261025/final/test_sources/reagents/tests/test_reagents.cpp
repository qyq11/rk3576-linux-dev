#include "cusmessagebox.h"
#include "projects/projects_store.h"
#include "reagents_page.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWindow>
#include <QtTest>
#include <memory>

class ReagentsTest : public QObject
{
    Q_OBJECT
private:
    QThread thread_;
    ReagentStore* store_ = nullptr;
    std::unique_ptr<QTemporaryDir> directory_;
    QString catalog_;
    ReagentSnapshot latest_;
    AccountSession engineer_;
    int errors_ = 0, saves_ = 0, loads_ = 0;
    QString errorMessage_;
    qint64 projectId_ = 0;
    ReagentRecord reagent_;
    ReagentBottle bottle_;
    ReagentBatch batch_;
    bool waitCount(int* count, int before, int timeout = 4000)
    {
        QElapsedTimer timer;
        timer.start();
        while (*count == before && timer.elapsed() < timeout)
            QTest::qWait(5);
        return *count > before;
    }
    bool saveReagent(ReagentRecord r)
    {
        int before = saves_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, r));
        return waitCount(&saves_, before);
    }
    bool reload()
    {
        int before = loads_;
        QMetaObject::invokeMethod(store_, "load", Qt::QueuedConnection);
        return waitCount(&loads_, before);
    }
private slots:
    void initTestCase()
    {
        ProjectsStore::registerTypes();
        ReagentStore::registerTypes();
        directory_.reset(new QTemporaryDir(QDir::currentPath() + "/reagent-test-XXXXXX"));
        QVERIFY(directory_->isValid());
        catalog_ = directory_->path() + "/catalog";
        ProjectsStore projects;
        projects.initialize(catalog_, true);
        QSignalSpy data(&projects, &ProjectsStore::snapshotReady);
        projects.load(1);
        QCOMPARE(data.count(), 1);
        const ProjectRecords records = qvariant_cast<ProjectRecords>(data.first().at(1));
        QVERIFY(!records.isEmpty());
        projectId_ = records.first().id;
        projects.close();
        engineer_.name = "offline_engineer";
        engineer_.role = 4;
        engineer_.authenticated = true;
        store_ = new ReagentStore;
        store_->moveToThread(&thread_);
        connect(&thread_, &QThread::finished, store_, &QObject::deleteLater);
        connect(store_, &ReagentStore::loaded, this,
            [this](ReagentSnapshot s)
            {
                latest_ = s;
                ++loads_;
            });
        connect(store_, &ReagentStore::succeeded, this, [this](QString) { ++saves_; });
        connect(store_, &ReagentStore::error, this,
            [this](QString, QString e)
            {
                ++errors_;
                errorMessage_ = e;
            });
        thread_.start();
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(waitCount(&loads_, 0));
        QCOMPARE(errors_, 0);
        QMetaObject::invokeMethod(
            store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, engineer_));
    }
    void crudAndReopen()
    {
        reagent_.name = "REAGENT_A";
        reagent_.manufacturer = "anonymous";
        reagent_.warningUl = 500;
        reagent_.outUl = 100;
        QVERIFY(saveReagent(reagent_));
        QVERIFY(reload());
        QCOMPARE(latest_.reagents.size(), 1);
        reagent_ = latest_.reagents.first();
        QVERIFY(reagent_.id > 0);
        reagent_.name = "REAGENT_RENAMED";
        QVERIFY(saveReagent(reagent_));
        QVERIFY(reload());
        QCOMPARE(latest_.reagents.first().name, reagent_.name);
        bottle_.name = "7mL_local";
        bottle_.capacityUl = 7000;
        bottle_.heightMm = 45;
        int before = saves_;
        QMetaObject::invokeMethod(store_, "saveBottle", Qt::QueuedConnection, Q_ARG(ReagentBottle, bottle_));
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        bottle_ = latest_.bottles.first();
        batch_.projectId = projectId_;
        batch_.code = "LOT_20261024";
        batch_.batchGroup = "LOT_20261024";
        before = saves_;
        QMetaObject::invokeMethod(store_, "saveBatch", Qt::QueuedConnection, Q_ARG(ReagentBatch, batch_));
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        batch_ = latest_.batches.first();
        ReagentBarcode code;
        code.reagentId = reagent_.id;
        code.bottleId = bottle_.id;
        code.code = "ANON_BARCODE_001";
        code.expiry = "2027-10-24";
        before = saves_;
        QMetaObject::invokeMethod(store_, "saveBarcode", Qt::QueuedConnection, Q_ARG(ReagentBarcode, code));
        QVERIFY(waitCount(&saves_, before));
        ReagentHole h;
        h.name = "R_1";
        h.area = "r_area";
        h.reagentId = reagent_.id;
        h.bottleId = bottle_.id;
        h.batchId = batch_.id;
        h.expiry = "2027-10-24";
        h.amountUl = 6000;
        h.remainingUl = 4300;
        before = saves_;
        QMetaObject::invokeMethod(store_, "saveHole", Qt::QueuedConnection, Q_ARG(ReagentHole, h));
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        QCOMPARE(latest_.holes.size(), 1);
        QCOMPARE(latest_.holes.first().remainingUl, 4300);
        QMetaObject::invokeMethod(store_, "close", Qt::BlockingQueuedConnection);
        before = loads_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(waitCount(&loads_, before));
        QCOMPARE(latest_.holes.first().remainingUl, 4300);
        QCOMPARE(latest_.barcodes.first().code, code.code);
        QCOMPARE(latest_.batches.first().projectId, projectId_);
    }
    void validationReferencesAndStaleIds()
    {
        ReagentRecord bad = reagent_;
        bad.id = 0;
        bad.name = "";
        int before = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, bad));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(!errorMessage_.isEmpty());
        bad = reagent_;
        bad.id = 0;
        before = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, bad));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(errorMessage_.contains("UNIQUE"));
        bad = reagent_;
        bad.outUl = bad.warningUl + 1;
        before = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, bad));
        QVERIFY(waitCount(&errors_, before));
        bad = reagent_;
        bad.id = 999999;
        before = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, bad));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(errorMessage_.contains(QStringLiteral("不存在")));
        for (QString method : { "removeReagent", "removeBottle", "removeBatch" })
        {
            qint64 id = method == "removeReagent" ? reagent_.id
                : method == "removeBottle"        ? bottle_.id
                                                  : batch_.id;
            before = errors_;
            QMetaObject::invokeMethod(store_, method.toLatin1(), Qt::QueuedConnection, Q_ARG(qint64, id));
            QVERIFY(waitCount(&errors_, before));
            QVERIFY(errorMessage_.contains(QStringLiteral("引用")));
        }
        ReagentBatch b = batch_;
        b.id = 0;
        b.projectId = 987654;
        b.code = "BAD_PROJECT";
        before = errors_;
        QMetaObject::invokeMethod(store_, "saveBatch", Qt::QueuedConnection, Q_ARG(ReagentBatch, b));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(errorMessage_.contains(QStringLiteral("不存在")));
        ReagentHole h = latest_.holes.first();
        h.remainingUl = 7001;
        before = errors_;
        QMetaObject::invokeMethod(store_, "saveHole", Qt::QueuedConnection, Q_ARG(ReagentHole, h));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(reload());
        QCOMPARE(latest_.holes.first().remainingUl, 4300);
    }
    void permissionsAndUiCancel()
    {
        for (int role : { -1, 0, 6 })
        {
            AccountSession guest = engineer_;
            guest.role = role;
            QMetaObject::invokeMethod(
                store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, guest));
            int before = errors_;
            ReagentRecord r = reagent_;
            r.name = "FORBIDDEN";
            QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, r));
            QVERIFY(waitCount(&errors_, before));
        }
        ReagentsPage page(store_);
        page.setSession(engineer_);
        page.show();
        QVERIFY(reload());
        QTRY_VERIFY(page.isReady());
        page.openReagentEditor();
        QTRY_VERIFY(page.findChild<QDialog*>("MaskDlgReagentListSetting"));
        QTest::qWait(350);
        auto* dialog = page.findChild<QDialog*>("MaskDlgReagentListSetting");
        auto* name = dialog->findChild<QLineEdit*>("ldtReagentName");
        QVERIFY(name);
        name->setFocus();
        name->selectAll();
        QTest::keyClicks(name, "CANCELLED_DRAFT");
        QVERIFY(page.hasUnsavedChanges());
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnCancel"), Qt::LeftButton);
        QVERIFY(!page.hasUnsavedChanges());
        QCOMPARE(name->text(), reagent_.name);
        QVERIFY(reload());
        QCOMPARE(latest_.reagents.first().name, reagent_.name);
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnClose"), Qt::LeftButton);
        QTest::qWait(50);
        page.openReagentEditor();
        QTest::qWait(350);
        dialog = page.findChild<QDialog*>("MaskDlgReagentListSetting");
        QVERIFY(dialog);
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnAdd"), Qt::LeftButton);
        name = dialog->findChild<QLineEdit*>("ldtReagentName");
        name->setFocus();
        QTest::keyClicks(name, "UI_CREATED");
        int before = saves_;
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnSave"), Qt::LeftButton);
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        QCOMPARE(latest_.reagents.size(), 2);
        QVERIFY(!page.hasUnsavedChanges());
        QVERIFY(page.prepareToLeave());
        AccountSession guest;
        page.setSession(guest);
        QTest::qWait(10);
        QVERIFY(!page.findChild<QPushButton*>("btnReagentSet")->isEnabled());
        before = errors_;
        ReagentRecord bad = reagent_;
        bad.name = "AFTER_LOGOUT";
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, bad));
        QVERIFY(waitCount(&errors_, before));
        QMetaObject::invokeMethod(
            store_, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, engineer_));
    }
    void draftRefreshSelectionEscapeAndLogout()
    {
        ReagentsPage page(store_);
        page.setSession(engineer_);
        page.setFixedSize(1751, 968);
        page.show();
        QVERIFY(reload());
        QTRY_VERIFY(page.isReady());
        page.openReagentEditor();
        QTest::qWait(350);
        auto* dialog = page.findChild<QDialog*>("MaskDlgReagentListSetting");
        QVERIFY(dialog);
        auto* name = dialog->findChild<QLineEdit*>("ldtReagentName");
        name->setFocus();
        name->selectAll();
        QTest::keyClicks(name, "PRESERVED_DRAFT");
        QVERIFY(page.hasUnsavedChanges());
        QVERIFY(reload());
        QCOMPARE(name->text(), QString("PRESERVED_DRAFT"));
        auto* list = dialog->findChild<QListWidget*>("listReagent");
        list->setCurrentRow(1);
        QCOMPARE(name->text(), QString("PRESERVED_DRAFT"));
        QVERIFY(list->currentRow() != 1);
        bool answered = false;
        QTimer::singleShot(450, &page,
            [&page, &answered]()
            {
                const auto boxes = page.findChildren<CusMessageBox*>();
                if (!boxes.isEmpty())
                {
                    QTest::mouseClick(boxes.last()->findChild<QPushButton*>("btnCancel"), Qt::LeftButton);
                    answered = true;
                }
            });
        QTest::keyClick(dialog, Qt::Key_Escape);
        QVERIFY(answered);
        QVERIFY(dialog->isVisible());
        QVERIFY(page.hasUnsavedChanges());
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnCancel"), Qt::LeftButton);
        QVERIFY(!page.hasUnsavedChanges());
        name->setFocus();
        name->selectAll();
        QTest::keyClicks(name, "LOGOUT_DRAFT");
        AccountSession guest;
        page.setSession(guest);
        QVERIFY(!dialog->findChild<QPushButton*>("btnSave")->isEnabled());
        QVERIFY(dialog->findChild<QPushButton*>("btnCancel")->isEnabled());
        QVERIFY(dialog->findChild<QPushButton*>("btnClose")->isEnabled());
        QTest::mouseClick(dialog->findChild<QPushButton*>("btnCancel"), Qt::LeftButton);
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->isVisible());
        QVERIFY(!page.hasUnsavedChanges());
        page.setSession(engineer_);
        QVERIFY(reload());
        QCOMPARE(latest_.reagents.first().name, reagent_.name);
    }
    void bottleEditorCrudReferenceAndCancel()
    {
        ReagentsPage page(store_);
        page.setFixedSize(1751, 968);
        page.setSession(engineer_);
        page.show();
        QVERIFY(reload());
        page.openBottleEditor();
        QTest::qWait(30);
        auto* dialog = page.findChild<QDialog*>("ReagentBottleEditor");
        QVERIFY(dialog);
        auto* name = dialog->findChild<QLineEdit*>("bottleName");
        auto* area = dialog->findChild<QComboBox*>("bottleArea");
        auto* capacity = dialog->findChild<QSpinBox*>("bottleCapacity");
        auto* height = dialog->findChild<QDoubleSpinBox*>("bottleHeight");
        name->setFocus();
        QTest::keyClicks(name, "UI_BOTTLE");
        area->setCurrentIndex(1);
        capacity->setValue(15000);
        height->setValue(35);
        int before = saves_;
        QTest::mouseClick(
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        QCOMPARE(latest_.bottles.size(), 2);
        qint64 createdId = 0;
        for (const auto& b : latest_.bottles)
            if (b.name == "UI_BOTTLE")
            {
                createdId = b.id;
                QCOMPARE(b.area, QString("s_area"));
                QCOMPARE(b.capacityUl, 15000);
            }
        QVERIFY(createdId > 0);
        QTest::qWait(30);
        page.openBottleEditor();
        QTest::qWait(30);
        dialog = page.findChild<QDialog*>("ReagentBottleEditor");
        QVERIFY(dialog);
        auto* select = dialog->findChild<QComboBox*>("bottleSelect");
        select->setCurrentIndex(select->findData(createdId));
        capacity = dialog->findChild<QSpinBox*>("bottleCapacity");
        capacity->setValue(14000);
        before = saves_;
        QTest::mouseClick(
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        for (const auto& b : latest_.bottles)
            if (b.id == createdId)
                QCOMPARE(b.capacityUl, 14000);
        QTest::qWait(30);
        page.openBottleEditor();
        QTest::qWait(30);
        dialog = page.findChild<QDialog*>("ReagentBottleEditor");
        select = dialog->findChild<QComboBox*>("bottleSelect");
        select->setCurrentIndex(select->findData(bottle_.id));
        capacity = dialog->findChild<QSpinBox*>("bottleCapacity");
        capacity->setValue(8000);
        before = errors_;
        QTest::mouseClick(
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save), Qt::LeftButton);
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(errorMessage_.contains(QStringLiteral("孔位")));
        QVERIFY(page.hasUnsavedChanges());
        QTest::mouseClick(
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        QTest::qWait(30);
        page.openBottleEditor();
        QTest::qWait(30);
        dialog = page.findChild<QDialog*>("ReagentBottleEditor");
        select = dialog->findChild<QComboBox*>("bottleSelect");
        select->setCurrentIndex(select->findData(createdId));
        QPushButton* remove = nullptr;
        for (QPushButton* button : dialog->findChildren<QPushButton*>())
            if (button->text() == QStringLiteral("删除"))
                remove = button;
        QVERIFY(remove);
        before = saves_;
        QTest::mouseClick(remove, Qt::LeftButton);
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        QCOMPARE(latest_.bottles.size(), 1);
        QTest::qWait(30);
        page.openBottleEditor();
        QTest::qWait(30);
        dialog = page.findChild<QDialog*>("ReagentBottleEditor");
        name = dialog->findChild<QLineEdit*>("bottleName");
        name->setFocus();
        QTest::keyClicks(name, "NOT_SAVED");
        QVERIFY(page.hasUnsavedChanges());
        AccountSession guest;
        page.setSession(guest);
        QVERIFY(dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->isEnabled());
        QVERIFY(!dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->isEnabled());
        QTest::mouseClick(
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel), Qt::LeftButton);
        page.setSession(engineer_);
        QVERIFY(reload());
        QCOMPARE(latest_.bottles.size(), 1);
        QVERIFY(!page.hasUnsavedChanges());
    }
    void visualEvidence()
    {
        const QString directory = qEnvironmentVariable("SN100_REAGENT_EVIDENCE");
        if (directory.isEmpty())
            return;
        QVERIFY(QDir().mkpath(directory));
        ReagentsPage page(store_);
        page.setFixedSize(1751, 968);
        page.setSession(engineer_);
        page.setProjects({ { projectId_, "DEMO_A", "s" } });
        page.show();
        QVERIFY(reload());
        QTest::qWait(150);
        auto capture = [](QWidget* widget, const QString& path)
        {
            if (QGuiApplication::platformName() == "xcb" && widget->windowHandle())
            {
                const QPixmap screen = widget->windowHandle()->screen()->grabWindow(widget->winId());
                const QImage pixels = screen.toImage();
                bool hasContent = false;
                for (int y = 0; y < pixels.height() && !hasContent; y += 20)
                    for (int x = 0; x < pixels.width(); x += 20)
                        if (pixels.pixelColor(x, y).lightness() > 10)
                        {
                            hasContent = true;
                            break;
                        }
                // WSLg can return an all-black X11 pixmap for an otherwise
                // painted surface. Keep a valid widget-rendered capture.
                if (hasContent)
                    return screen.save(path);
            }
            return widget->grab().save(path);
        };
        QVERIFY(capture(&page, directory + "/reagent_page_1751x968.png"));
        struct View
        {
            const char* file;
            void (ReagentsPage::*open)();
            const char* object;
        };
        const QList<View> views
            = { { "reagent_information.png", &ReagentsPage::openReagentEditor, "MaskDlgReagentListSetting" },
                  { "reagent_hole.png", &ReagentsPage::openHoleEditor, "MaskDlgReagentSetting" },
                  { "reagent_batch.png", &ReagentsPage::openBatchEditor, "MaskDlgGroupSetting" } };
        for (const auto& view : views)
        {
            (page.*view.open)();
            QTest::qWait(350);
            auto* dialog = page.findChild<QDialog*>(view.object);
            QVERIFY(dialog);
            QVERIFY(capture(dialog, directory + "/" + view.file));
            QVERIFY(page.prepareToLeave());
            QTest::qWait(20);
        }
        page.openBottleEditor();
        QTest::qWait(80);
        const auto dialogs = page.findChildren<QDialog*>();
        QVERIFY(!dialogs.isEmpty());
        QVERIFY(capture(dialogs.last(), directory + "/reagent_bottles.png"));
        QVERIFY(page.prepareToLeave());
    }
    void lockedRecoveryKeepsUiResponsive()
    {
        QSqlDatabase lock = QSqlDatabase::addDatabase("QSQLITE", "reagent_lock");
        lock.setDatabaseName(catalog_ + "/catalog.db");
        QVERIFY(lock.open());
        QSqlQuery q(lock);
        QVERIFY(q.exec("BEGIN EXCLUSIVE"));
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 last = 0, maxGap = 0;
        int samples = 0;
        QTimer heartbeat;
        heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, this,
            [&]()
            {
                qint64 now = elapsed.elapsed();
                maxGap = qMax(maxGap, now - last);
                last = now;
                ++samples;
            });
        heartbeat.start();
        ReagentRecord changed = reagent_;
        changed.manufacturer = "LOCK_RECOVERY";
        int before = errors_;
        QMetaObject::invokeMethod(store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, changed));
        QVERIFY(waitCount(&errors_, before));
        heartbeat.stop();
        QVERIFY(errorMessage_.contains("locked"));
        QVERIFY(samples > 20);
        QVERIFY2(maxGap < 100, qPrintable(QString::number(maxGap)));
        qInfo() << "LOCK_HEARTBEAT samples=" << samples << " max_gap_ms=" << maxGap;
        QVERIFY(q.exec("ROLLBACK"));
        q.finish();
        lock.close();
        lock = QSqlDatabase();
        QSqlDatabase::removeDatabase("reagent_lock");
        QVERIFY(saveReagent(changed));
        QVERIFY(reload());
        QCOMPARE(latest_.reagents.first().manufacturer, changed.manufacturer);
    }
    void pathProtectionAndReopenRecovery()
    {
        const QString unmarked = directory_->path() + "/unmarked";
        QDir().mkpath(unmarked);
        QFile sentinel(unmarked + "/keep.txt");
        QVERIFY(sentinel.open(QIODevice::WriteOnly));
        sentinel.write("preserve");
        sentinel.close();
        int before = errors_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, unmarked));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(!QFileInfo::exists(unmarked + "/catalog.db"));
        before = errors_;
        QMetaObject::invokeMethod(
            store_, "saveReagent", Qt::QueuedConnection, Q_ARG(ReagentRecord, reagent_));
        QVERIFY(waitCount(&errors_, before));
        QVERIFY(errorMessage_.contains(QStringLiteral("未就绪")));
        before = loads_;
        QMetaObject::invokeMethod(store_, "initialize", Qt::QueuedConnection, Q_ARG(QString, catalog_));
        QVERIFY(waitCount(&loads_, before));
        QCOMPARE(latest_.holes.first().remainingUl, 4300);
    }
    void clearAndDelete()
    {
        int before = saves_;
        QMetaObject::invokeMethod(store_, "clearHole", Qt::QueuedConnection, Q_ARG(QString, QString("R_1")));
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        QVERIFY(latest_.holes.isEmpty());
        qint64 barcode = latest_.barcodes.first().id;
        before = saves_;
        QMetaObject::invokeMethod(store_, "removeBarcode", Qt::QueuedConnection, Q_ARG(qint64, barcode));
        QVERIFY(waitCount(&saves_, before));
        before = saves_;
        QMetaObject::invokeMethod(store_, "removeBatch", Qt::QueuedConnection, Q_ARG(qint64, batch_.id));
        QVERIFY(waitCount(&saves_, before));
        before = saves_;
        QMetaObject::invokeMethod(store_, "removeReagent", Qt::QueuedConnection, Q_ARG(qint64, reagent_.id));
        QVERIFY(waitCount(&saves_, before));
        before = saves_;
        QMetaObject::invokeMethod(store_, "removeBottle", Qt::QueuedConnection, Q_ARG(qint64, bottle_.id));
        QVERIFY(waitCount(&saves_, before));
        QVERIFY(reload());
        QVERIFY(latest_.holes.isEmpty());
        QVERIFY(latest_.batches.isEmpty());
        QVERIFY(latest_.barcodes.isEmpty());
        QVERIFY(latest_.bottles.isEmpty());
        QCOMPARE(latest_.reagents.size(), 1);
    }
    void cleanupTestCase()
    {
        QMetaObject::invokeMethod(store_, "close", Qt::BlockingQueuedConnection);
        thread_.quit();
        QVERIFY(thread_.wait(3000));
    }
};
QTEST_MAIN(ReagentsTest)
#include "test_reagents.moc"
