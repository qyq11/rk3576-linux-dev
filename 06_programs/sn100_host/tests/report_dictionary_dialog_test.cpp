#include <QtTest>
#include <QFrame>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QValidator>
#include "report_dictionary_dialog.h"

namespace
{
HospitalDictionaryRequest requestAt(const QSignalSpy &spy, int index)
{
    return qvariant_cast<HospitalDictionaryRequest>(spy.at(index).at(0));
}

HospitalDictionaryReply success(const HospitalDictionaryRequest &request)
{
    HospitalDictionaryReply reply;
    reply.request = request;
    reply.succeeded = true;
    reply.entries = {{91, QStringLiteral("中文短名"), QStringLiteral("zw")},
                     {103, QStringLiteral("完整名称超过原九字符"), QStringLiteral("full")}};
    return reply;
}
}

class ReportDictionaryDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void originalInnerGeometryAndNameOnly()
    {
        ReportDictionaryDialog dialog(QStringLiteral("keshi"));
        QSignalSpy requests(&dialog, &ReportDictionaryDialog::queryRequested);
        QSignalSpy names(&dialog, &ReportDictionaryDialog::nameSelected);
        dialog.openForSelection();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(dialog.findChild<QFrame *>("view_frame")->size(), QSize(491, 751));
        QCOMPARE(dialog.findChild<QLineEdit *>("ldtConditionSelect")->geometry(), QRect(20, 90, 451, 61));
        auto *table = dialog.findChild<QTableWidget *>("lstSelect");
        QCOMPARE(table->geometry(), QRect(20, 190, 451, 451));
        auto *confirm = dialog.findChild<QPushButton *>("btnSave");
        QCOMPARE(confirm->geometry(), QRect(20, 670, 451, 61));
        QVERIFY(!confirm->isEnabled());
        dialog.receiveReply(success(requestAt(requests, 0)));
        QCOMPARE(table->columnCount(), 3);
        QCOMPARE(table->currentRow(), 0);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("1"));
        QCOMPARE(table->item(0, 1)->text(), QStringLiteral("中文短名"));
        QCOMPARE(table->item(0, 2)->text(), QStringLiteral("zw"));
        table->selectRow(1);
        confirm->click();
        QCOMPARE(names.size(), 1);
        QCOMPARE(names.at(0).at(0).toString(), QStringLiteral("完整名称超过原九字符"));
        QVERIFY(!dialog.isVisible());
    }

    void staleRequestTypeAndFilterCannotReplaceCurrentRows()
    {
        ReportDictionaryDialog dialog(QStringLiteral("bingren"));
        QSignalSpy requests(&dialog, &ReportDictionaryDialog::queryRequested);
        dialog.openForSelection();
        auto *edit = dialog.findChild<QLineEdit *>("ldtConditionSelect");
        QVERIFY(edit->validator());
        edit->setText(QStringLiteral("中文%'_"));
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requestAt(requests, 1).filter, QStringLiteral("中文%'_"));
        QVERIFY(requestAt(requests, 0).requestId != requestAt(requests, 1).requestId);
        auto *confirm = dialog.findChild<QPushButton *>("btnSave");
        dialog.receiveReply(success(requestAt(requests, 0)));
        QVERIFY(!confirm->isEnabled());
        auto wrongType = success(requestAt(requests, 1));
        wrongType.request.type = QStringLiteral("keshi");
        dialog.receiveReply(wrongType);
        QVERIFY(!confirm->isEnabled());
        auto wrongFilter = success(requestAt(requests, 1));
        wrongFilter.request.filter = QStringLiteral("另一次");
        dialog.receiveReply(wrongFilter);
        QVERIFY(!confirm->isEnabled());
        dialog.receiveReply(success(requestAt(requests, 1)));
        QVERIFY(confirm->isEnabled());
        dialog.reject();
    }

    void filterLimitRejectsWholeInputAndDoesNotLimitSelectedName()
    {
        ReportDictionaryDialog dialog(QStringLiteral("keshi"));
        QSignalSpy requests(&dialog, &ReportDictionaryDialog::queryRequested);
        QSignalSpy names(&dialog, &ReportDictionaryDialog::nameSelected);
        dialog.openForSelection();
        auto *edit = dialog.findChild<QLineEdit *>("ldtConditionSelect");
        auto *confirm = dialog.findChild<QPushButton *>("btnSave");
        QVERIFY(edit->validator());
        QString input;
        int cursor = 0;
        QCOMPARE(edit->validator()->validate(input, cursor), QValidator::Acceptable);
        input = QStringLiteral("中文");
        QCOMPARE(edit->validator()->validate(input, cursor), QValidator::Acceptable);
        input = QString(kHospitalDictionaryFilterLimit, QChar(0x4e2d));
        QCOMPARE(edit->validator()->validate(input, cursor), QValidator::Acceptable);
        input.append(QChar(0x4e2d));
        QCOMPARE(edit->validator()->validate(input, cursor), QValidator::Invalid);

        const QString atLimit(kHospitalDictionaryFilterLimit, QLatin1Char('%'));
        edit->setText(atLimit);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requestAt(requests, 1).filter, atLimit);
        edit->setCursorPosition(edit->text().size());
        edit->insert(QStringLiteral("X"));
        QCOMPARE(edit->text(), atLimit);
        QCOMPARE(requests.size(), 2);

        const QString overLimit = atLimit + QStringLiteral("X");
        edit->setText(overLimit); // 程序赋值绕过validator，仍不可发送截断/超限查询。
        QCOMPARE(edit->text(), overLimit);
        QCOMPARE(requests.size(), 2);
        QVERIFY(!confirm->isEnabled());
        QVERIFY(dialog.findChild<QTableWidget *>("lstSelect")->item(0, 0)->text()
                .contains(QStringLiteral("未查询，未截断")));
        dialog.receiveReply(success(requestAt(requests, 1)));
        QVERIFY(!confirm->isEnabled());
        edit->setText(QStringLiteral("中文"));
        QCOMPARE(requests.size(), 3);
        QCOMPARE(requestAt(requests, 2).filter, QStringLiteral("中文"));
        auto reply = success(requestAt(requests, 2));
        const QString fullName = QStringLiteral("完整名称").repeated(12);
        QVERIFY(fullName.size() > kHospitalDictionaryFilterLimit);
        reply.entries = {{19, fullName, QStringLiteral("full")}};
        dialog.receiveReply(reply);
        confirm->click();
        QCOMPARE(names.size(), 1);
        QCOMPARE(names.at(0).at(0).toString(), fullName);
    }

    void errorIsNotEmptyAndRetryUsesFreshRequest()
    {
        ReportDictionaryDialog dialog(QStringLiteral("jianyan"));
        QSignalSpy requests(&dialog, &ReportDictionaryDialog::queryRequested);
        QSignalSpy names(&dialog, &ReportDictionaryDialog::nameSelected);
        dialog.openForSelection();
        HospitalDictionaryReply failed;
        failed.request = requestAt(requests, 0);
        failed.error = QStringLiteral("匿名锁定错误");
        failed.entries = success(failed.request).entries;
        dialog.receiveReply(failed);
        auto *confirm = dialog.findChild<QPushButton *>("btnSave");
        auto *table = dialog.findChild<QTableWidget *>("lstSelect");
        QVERIFY(!confirm->isEnabled());
        QVERIFY(table->item(0, 0)->text().contains(QStringLiteral("读取失败")));
        QVERIFY(table->item(0, 0)->text().contains(failed.error));
        confirm->click();
        QCOMPARE(names.size(), 0);
        QVERIFY(dialog.isVisible());
        dialog.findChild<QLineEdit *>("ldtConditionSelect")->setText(QStringLiteral("无匹配"));
        HospitalDictionaryReply empty;
        empty.request = requestAt(requests, 1);
        empty.succeeded = true;
        dialog.receiveReply(empty);
        QCOMPARE(table->rowCount(), 0);
        QCOMPARE(table->currentRow(), -1);
        QVERIFY(confirm->isEnabled());
        confirm->click();
        QCOMPARE(names.size(), 0);
        QVERIFY(!dialog.isVisible());
    }

    void closeWithoutSelection_data()
    {
        QTest::addColumn<int>("method");
        QTest::newRow("X") << 0;
        QTest::newRow("Escape") << 1;
        QTest::newRow("window-close") << 2;
    }

    void closeWithoutSelection()
    {
        QFETCH(int, method);
        ReportDictionaryDialog dialog(QStringLiteral("shenhe"));
        QSignalSpy requests(&dialog, &ReportDictionaryDialog::queryRequested);
        QSignalSpy names(&dialog, &ReportDictionaryDialog::nameSelected);
        dialog.openForSelection();
        if (method == 0) dialog.findChild<QPushButton *>("btnClose")->click();
        if (method == 1) QTest::keyClick(&dialog, Qt::Key_Escape);
        if (method == 2) QVERIFY(dialog.close());
        QVERIFY(!dialog.isVisible());
        dialog.receiveReply(success(requestAt(requests, 0)));
        dialog.findChild<QPushButton *>("btnSave")->click();
        QCOMPARE(names.size(), 0);
    }
};

QTEST_MAIN(ReportDictionaryDialogTest)
#include "report_dictionary_dialog_test.moc"
