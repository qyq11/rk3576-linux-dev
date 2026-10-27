#include <QtTest>
#include <QContextMenuEvent>
#include <QDataStream>
#include <QDialog>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QThread>
#include <memory>
#include "history_page.h"
#include "report_dictionary_dialog.h"

namespace
{
// 匿名测试库的逻辑全表快照。测试可同步读取；正式GUI没有这个SQL入口。
bool databaseImage(const QString &path, QByteArray *image, qint64 selectedPatientId = 0)
{
    const QString connection = QUuid::createUuid().toString();
    bool okay = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (database.open() && database.transaction())
        {
            QDataStream stream(image, QIODevice::WriteOnly);
            QSqlQuery query(database);
            okay = query.exec(QStringLiteral(
                "SELECT name,sql FROM sqlite_master WHERE type='table' ORDER BY name"));
            QStringList tables;
            while (okay && query.next())
            {
                tables.append(query.value(0).toString());
                stream << query.value(0) << query.value(1);
            }
            okay = okay && !query.lastError().isValid();
            query.finish();
            for (const QString &table : tables)
            {
                QString escaped = table;
                escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
                okay = okay && query.exec(QStringLiteral("SELECT * FROM \"%1\" ORDER BY rowid").arg(escaped));
                while (okay && query.next())
                {
                    QVariantList values;
                    for (int column = 0; column < query.record().count(); ++column)
                    {
                        const QString name = query.record().fieldName(column);
                        const QStringList changedColumns = {"department", "patient_type", "jianyan", "checker", "auditor"};
                        const bool expectedChange = table == QStringLiteral("report_data")
                                && selectedPatientId > 0 && query.value("id").toLongLong() == selectedPatientId
                                && changedColumns.contains(name);
                        values.append(expectedChange ? QVariant() : query.value(column));
                    }
                    stream << values;
                }
                okay = okay && !query.lastError().isValid();
                query.finish();
            }
            okay = okay && database.commit();
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connection);
    return okay;
}

class PageSession
{
public:
    PageSession()
    {
        HistoryStore::registerTypes();
        registerHospitalDictionaryTypes();
        store = new HistoryStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        page.reset(new HistoryPage(store));
        const QString directory = QDir(temp.path()).filePath(QStringLiteral("history"));
        databasePath = QDir(directory).filePath(QStringLiteral("history.sqlite"));
        QObject::connect(&thread, &QThread::started, store, [this, directory]()
        {
            store->initialize(directory);
        });
        page->show();
        thread.start();
    }

    ~PageSession()
    {
        page.reset();
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit();
        if (!thread.wait(5000)) qFatal("HistoryStore did not close within test teardown budget");
    }

    void queryFixtureDates()
    {
        page->findChild<QPushButton *>("btnQuery")->click();
        auto *dialog = page->findChild<QDialog *>("MaskDlgQueryReport");
        dialog->findChild<QLineEdit *>("ldtStartDate")->setText(QStringLiteral("2026/10/22"));
        dialog->findChild<QLineEdit *>("ldtEndDate")->setText(QStringLiteral("2026/10/24"));
        dialog->findChild<QPushButton *>("btnSave")->click();
    }

    QTemporaryDir temp;
    QThread thread;
    HistoryStore *store = nullptr;
    std::unique_ptr<HistoryPage> page;
    QString databasePath;
};

ReportDictionaryDialog *rightClick(HistoryPage *page, QLineEdit *edit)
{
    // 走真实Qt contextMenuEvent；仍属合成事件，不代替WSLg物理右键/屏幕验收。
    const QPoint position = edit->rect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, edit->mapToGlobal(position));
    QApplication::sendEvent(edit, &event);
    for (auto *dialog : page->findChildren<ReportDictionaryDialog *>())
    {
        if (dialog->isVisible()) return dialog;
    }
    return nullptr;
}

HospitalDictionaryRequest requestAt(const QSignalSpy &spy, int index)
{
    return qvariant_cast<HospitalDictionaryRequest>(spy.at(index).at(0));
}

HospitalDictionaryReply replyFor(const HospitalDictionaryRequest &request, const QString &name)
{
    HospitalDictionaryReply reply;
    reply.request = request;
    reply.succeeded = true;
    reply.entries = {{41, name, QStringLiteral("anonymous-mnemonic")}};
    return reply;
}
}

class ReportDictionaryPageTest : public QObject
{
    Q_OBJECT
private slots:
    void fiveDraftFieldsOnlyOriginalSaveCommits_data()
    {
        QTest::addColumn<int>("role");
        QTest::newRow("Guest-keeps-existing-patient-save") << 0;
        QTest::newRow("local-role-keeps-existing-patient-save") << 3;
    }

    void fiveDraftFieldsOnlyOriginalSaveCommits()
    {
        QFETCH(int, role);
        PageSession session;
        auto *page = session.page.get();
        QTRY_VERIFY(page->isReady() && !page->isBusy());
        AccountSession identity;
        identity.role = role;
        identity.authenticated = role != 0;
        identity.source = role == 0 ? AccountSessionSource::Guest : AccountSessionSource::Simulated;
        page->setSession(identity);
        page->setHospitalDictionaryAvailable(true);
        session.queryFixtureDates();
        QTRY_VERIFY(!page->isBusy());
        auto *reports = page->findChild<QTableWidget *>("tableResultReportOne");
        QVERIFY(reports->rowCount() > 1);
        page->findChild<QPushButton *>("btnDetail")->click();
        QTRY_VERIFY(!page->isBusy());
        auto *detail = page->findChild<QDialog *>("MaskDlgReportDetail");
        QVERIFY(detail && detail->isVisible());
        detail->findChild<QPushButton *>("btnMenuInfo")->click();
        const QStringList fields = {"ldtKeShi", "ldtBingRenLeiXin", "ldtJianYan", "ldtSongJian", "ldtShenHe"};
        const QStringList types = {"keshi", "bingren", "jianyan", "songjian", "shenhe"};
        const QStringList names = {QStringLiteral("科室"), QStringLiteral("中"), QStringLiteral("A"),
            QStringLiteral("完整送检名称超过九字符").repeated(4), QStringLiteral("O'Brien审核")};
        // 模拟历史已存在长名，验证查询33容量不能变成选名/患者字段限制。
        QVERIFY(names.at(3).size() > kHospitalDictionaryFilterLimit);
        QSignalSpy requests(page, &HistoryPage::requestHospitalDictionary);
        QSignalSpy writes(page, &HistoryPage::requestSavePatient);
        QSignalSpy saved(session.store, &HistoryStore::patientSaved);
        QSignalSpy detailRequests(page, &HistoryPage::requestDetail);
        // 通过真实读取拿到选中报告编号，再保护所有非目标字段与其他表。
        QVERIFY(detail->close());
        page->findChild<QPushButton *>("btnDetail")->click();
        QTRY_VERIFY(!page->isBusy());
        QCOMPARE(detailRequests.size(), 1);
        const qint64 selectedId = detailRequests.at(0).at(1).toLongLong();
        const bool canHide = page->findChild<QPushButton *>("btnDel")->isEnabled();
        const bool canDisplay = page->findChild<QPushButton *>("btnDisplay")->isEnabled();
        detail->findChild<QPushButton *>("btnMenuInfo")->click();
        QByteArray before;
        QVERIFY(databaseImage(session.databasePath, &before));
        QByteArray protectedBefore;
        QVERIFY(databaseImage(session.databasePath, &protectedBefore, selectedId));
        for (int index = 0; index < fields.size(); ++index)
        {
            QStringList otherTexts;
            for (const QString &field : fields) otherTexts.append(detail->findChild<QLineEdit *>(field)->text());
            auto *edit = detail->findChild<QLineEdit *>(fields.at(index));
            QVERIFY(!edit->hasAcceptableInput() || edit->text().size() == 9);
            auto *picker = rightClick(page, edit);
            QVERIFY(picker);
            QCOMPARE(requests.size(), index + 1);
            QCOMPARE(requestAt(requests, index).type, types.at(index));
            QVERIFY(page->isBusy());
            QVERIFY(!page->requestLeave(QStringLiteral("切换报告")));
            page->receiveHospitalDictionary(replyFor(requestAt(requests, index), names.at(index)));
            picker->findChild<QPushButton *>("btnSave")->click();
            QCOMPARE(edit->text(), names.at(index));
            QVERIFY(!page->isBusy());
            for (int other = 0; other < fields.size(); ++other)
            {
                if (other != index) QCOMPARE(detail->findChild<QLineEdit *>(fields.at(other))->text(), otherTexts.at(other));
            }
            QCOMPARE(writes.size(), 0);
            QByteArray draft;
            QVERIFY(databaseImage(session.databasePath, &draft));
            QCOMPARE(draft, before);
        }
        QVERIFY(page->hasUnsavedChanges());
        detail->findChild<QPushButton *>("btnSave")->click();
        QCOMPARE(writes.size(), 1);
        QTRY_COMPARE(saved.size(), 1);
        QTRY_VERIFY(!page->isBusy());
        const auto patient = qvariant_cast<HistoryPatient>(saved.at(0).at(1));
        QCOMPARE(patient.department, names.at(0));
        QCOMPARE(patient.patientType, names.at(1));
        QCOMPARE(patient.examiner, names.at(2));
        QCOMPARE(patient.sender, names.at(3));
        QCOMPARE(patient.auditor, names.at(4));
        const HistoryPatient requested = qvariant_cast<HistoryPatient>(writes.at(0).at(1));
        QCOMPARE(requested.name, patient.name);
        QCOMPARE(requested.applicationSerial, patient.applicationSerial);
        QCOMPARE(requested.bedNumber, patient.bedNumber);
        QVERIFY(!page->hasUnsavedChanges());
        QByteArray protectedAfter;
        QVERIFY(databaseImage(session.databasePath, &protectedAfter, selectedId));
        QCOMPARE(protectedAfter, protectedBefore);
        QCOMPARE(page->findChild<QPushButton *>("btnDel")->isEnabled(), canHide);
        QCOMPARE(page->findChild<QPushButton *>("btnDisplay")->isEnabled(), canDisplay);
        QVERIFY(detail->close());
        page->findChild<QPushButton *>("btnDetail")->click();
        QTRY_VERIFY(!page->isBusy());
        for (int index = 0; index < fields.size(); ++index)
            QCOMPARE(detail->findChild<QLineEdit *>(fields.at(index))->text(), names.at(index));
        QVERIFY(detail->close());
    }

    void failuresEmptyAndUnrepresentableNamesPreserveExistingDraft()
    {
        PageSession session;
        auto *page = session.page.get();
        QTRY_VERIFY(page->isReady() && !page->isBusy());
        session.queryFixtureDates();
        QTRY_VERIFY(!page->isBusy());
        page->findChild<QPushButton *>("btnDetail")->click();
        QTRY_VERIFY(!page->isBusy());
        auto *detail = page->findChild<QDialog *>("MaskDlgReportDetail");
        detail->findChild<QPushButton *>("btnMenuInfo")->click();
        auto *target = detail->findChild<QLineEdit *>("ldtKeShi");
        auto *draft = detail->findChild<QLineEdit *>("ldtName");
        draft->setText(QStringLiteral("NameDraft"));
        const QString original = target->text();
        QSignalSpy requests(page, &HistoryPage::requestHospitalDictionary);
        QSignalSpy writes(page, &HistoryPage::requestSavePatient);
        QVERIFY(!rightClick(page, target)); // 未接线时明确不可用，不能造空成功。
        QCOMPARE(requests.size(), 0);
        QVERIFY(page->property("lastStatus").toString().contains(QStringLiteral("尚未就绪")));
        page->setHospitalDictionaryAvailable(true);
        QByteArray before;
        QVERIFY(databaseImage(session.databasePath, &before));
        auto *picker = rightClick(page, target);
        QVERIFY(picker);
        HospitalDictionaryReply failed;
        failed.request = requestAt(requests, 0);
        failed.error = QStringLiteral("匿名读取失败");
        page->receiveHospitalDictionary(failed);
        QVERIFY(!picker->findChild<QPushButton *>("btnSave")->isEnabled());
        picker->reject();
        picker = rightClick(page, target);
        QVERIFY(picker);
        HospitalDictionaryReply empty;
        empty.request = requestAt(requests, 1);
        empty.succeeded = true;
        page->receiveHospitalDictionary(empty);
        picker->findChild<QPushButton *>("btnSave")->click();
        picker = rightClick(page, target);
        QVERIFY(picker);
        page->receiveHospitalDictionary(replyFor(requestAt(requests, 2), QString(32768, QLatin1Char('A'))));
        picker->findChild<QPushButton *>("btnSave")->click();
        QVERIFY(page->property("lastStatus").toString().contains(QStringLiteral("未截断")));
        QCOMPARE(target->text(), original);
        QCOMPARE(draft->text(), QStringLiteral("NameDraft"));
        QCOMPARE(writes.size(), 0);
        QVERIFY(page->hasUnsavedChanges());
        QByteArray after;
        QVERIFY(databaseImage(session.databasePath, &after));
        QCOMPARE(after, before);
        picker = rightClick(page, target);
        QVERIFY(picker);
        const auto last = requestAt(requests, 3);
        page->setHospitalDictionaryAvailable(false);
        QVERIFY(!page->isBusy());
        page->receiveHospitalDictionary(replyFor(last, QStringLiteral("不能回填")));
        QCOMPARE(target->text(), original);
        QCOMPARE(draft->text(), QStringLiteral("NameDraft"));
    }

    void cancelledFlightCoalescesAndCannotFillReopenedField()
    {
        PageSession session;
        auto *page = session.page.get();
        QTRY_VERIFY(page->isReady() && !page->isBusy());
        page->setHospitalDictionaryAvailable(true);
        session.queryFixtureDates();
        QTRY_VERIFY(!page->isBusy());
        page->findChild<QPushButton *>("btnDetail")->click();
        QTRY_VERIFY(!page->isBusy());
        auto *detail = page->findChild<QDialog *>("MaskDlgReportDetail");
        detail->findChild<QPushButton *>("btnMenuInfo")->click();
        auto *department = detail->findChild<QLineEdit *>("ldtKeShi");
        auto *auditor = detail->findChild<QLineEdit *>("ldtShenHe");
        const QString oldDepartment = department->text();
        const QString oldAuditor = auditor->text();
        QSignalSpy requests(page, &HistoryPage::requestHospitalDictionary);
        QSignalSpy writes(page, &HistoryPage::requestSavePatient);
        auto *first = rightClick(page, department);
        QVERIFY(first);
        QCOMPARE(requests.size(), 1);
        const auto stale = requestAt(requests, 0);
        auto *filter = first->findChild<QLineEdit *>("ldtConditionSelect");
        filter->setText(QStringLiteral("丢弃一"));
        filter->setText(QStringLiteral("丢弃二"));
        QCOMPARE(requests.size(), 1);
        first->reject();
        QVERIFY(!page->isBusy());
        auto *second = rightClick(page, auditor);
        QVERIFY(second);
        auto *nextFilter = second->findChild<QLineEdit *>("ldtConditionSelect");
        nextFilter->setText(QStringLiteral("仅保留最新"));
        QCOMPARE(requests.size(), 1);
        page->receiveHospitalDictionary(replyFor(stale, QStringLiteral("不能填入")));
        QCOMPARE(requests.size(), 2);
        const auto current = requestAt(requests, 1);
        QCOMPARE(current.type, QStringLiteral("shenhe"));
        QCOMPARE(current.filter, QStringLiteral("仅保留最新"));
        QVERIFY(!second->findChild<QPushButton *>("btnSave")->isEnabled());
        QCOMPARE(department->text(), oldDepartment);
        QCOMPARE(auditor->text(), oldAuditor);
        page->receiveHospitalDictionary(replyFor(stale, QStringLiteral("重复迟到")));
        second->reject();
        page->receiveHospitalDictionary(replyFor(current, QStringLiteral("关闭后迟到")));
        QCOMPARE(writes.size(), 0);
        QCOMPARE(department->text(), oldDepartment);
        QCOMPARE(auditor->text(), oldAuditor);
        QVERIFY(!page->hasUnsavedChanges());
        QVERIFY(detail->close());
    }
};

QTEST_MAIN(ReportDictionaryPageTest)
#include "report_dictionary_page_test.moc"
