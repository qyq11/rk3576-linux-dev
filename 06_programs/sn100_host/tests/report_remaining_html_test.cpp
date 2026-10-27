#include <QtTest>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QPrintPreviewDialog>
#include <QProcess>
#include <QSignalSpy>
#include <QTextDocument>
#include <QTemporaryDir>
#include "history/report_controller.h"
#include "history/report_remaining_html.h"

namespace
{
HistoryReport anonymousReport(int rows = 3)
{
    HistoryReport report;
    report.id = 41;
    report.sampleNumber = 987;
    report.barcode = QStringLiteral("ANON-HTML-987");
    report.dateTime = QDateTime(QDate(2026, 10, 25), QTime(9, 30, 0));
    report.patient.reportId = report.id;
    report.patient.name = QStringLiteral("匿名中文甲");
    report.patient.age = 23;
    report.patient.sex = 1;
    report.patient.department = QStringLiteral("匿名血液科");
    report.patient.bedNumber = QStringLiteral("测试床9");
    report.patient.sender = QStringLiteral("测试送检甲");
    report.patient.examiner = QStringLiteral("测试检验乙");
    report.patient.auditor = QStringLiteral("测试审核丙");
    report.patient.patientType = QStringLiteral("匿名门诊");
    report.patient.remark = QStringLiteral("中文备注'单引号'和\"双引号\"");
    for (int i = 0; i < rows; ++i)
    {
        HistoryResult task;
        task.taskId = 9001 + i;
        task.project = QStringLiteral("PT");
        task.testedAt = report.dateTime;
        HistoryResultValue value;
        value.resultType = 1;
        value.label = QStringLiteral("PT");
        value.unit = QStringLiteral("s");
        value.decimals = 1;
        value.rawValue = QString::number(9001 + i) + QStringLiteral(".125");
        task.values.append(value);
        report.results.append(task);
    }
    return report;
}

QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size();
}
}

class ReportRemainingHtmlTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qRegisterMetaType<QPrintPreviewDialog *>("QPrintPreviewDialog*");
        m_root = qEnvironmentVariable("SN100_REMAINING_HTML_DIR");
        m_renderer = qEnvironmentVariable("SN100_REPORT_RENDERER");
        QVERIFY(QDir::isAbsolutePath(m_root));
        QVERIFY(!QFileInfo::exists(m_root));
        QVERIFY(QFileInfo(m_renderer).isExecutable());
        QVERIFY(QDir().mkpath(m_root + QStringLiteral("/exports")));
        QVERIFY(write(m_root + QStringLiteral("/.sn100-history-offline"), "SN100 O04 ANONYMOUS OFFLINE DATA v1\n"));
    }

    void htmlContent_data()
    {
        QTest::addColumn<int>("rows");
        QTest::newRow("three_rows") << 3;
        QTest::newRow("sixty_four_rows") << 64;
    }

    void htmlContent()
    {
        QFETCH(int, rows);
        const QString name = QStringLiteral("rows_%1").arg(rows);
        const QString output = m_root + QStringLiteral("/exports/") + name + QStringLiteral(".html");
        HistoryReportController controller;
        controller.setDataDirectory(m_root);
        controller.setRendererPath(m_renderer);
        QSignalSpy complete(&controller, &HistoryReportController::htmlExported);
        QSignalSpy errors(&controller, &HistoryReportController::failed);
        const HistoryReport input = anonymousReport(rows);
        controller.exportHtml(input, output);
        QTRY_VERIFY_WITH_TIMEOUT(!complete.isEmpty() || !errors.isEmpty(), 30000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.last().last().toString()));
        QCOMPARE(complete.size(), 1);
        QCOMPARE(complete.first().at(1).toInt(), rows);
        HistoryPrintOptions options;
        QString error;
        QVERIFY(HistoryReportDocument::anonymousOptions(&options, &error));
        HistoryPrintDocument snapshot;
        QVERIFY(HistoryReportDocument::build(input, options, &snapshot, &error));
        QVERIFY(write(m_root + QStringLiteral("/") + name + QStringLiteral(".json"),
                      QJsonDocument(HistoryReportDocument::toJson(snapshot)).toJson()));
        const QByteArray bytes = read(output);
        QVERIFY2(validateHistoryHtml(bytes, &snapshot, &error), qPrintable(error));
        QTextDocument parsed;
        parsed.setHtml(QString::fromUtf8(bytes));
        const QString text = parsed.toPlainText();
        QVERIFY(text.contains(QStringLiteral("匿名中文甲")));
        for (int i = 0; i < rows; ++i)
            QVERIFY(text.contains(QString::number(9001 + i) + QStringLiteral(".1")));
        QVERIFY(write(m_root + QStringLiteral("/") + name + QStringLiteral("_qt_text.txt"), text.toUtf8()));
        qInfo() << "HTML_REPARSED" << rows << bytes.size() << output;
    }

    void invalidDuplicateAndRecovery()
    {
        HistoryReportController controller;
        controller.setDataDirectory(m_root);
        controller.setRendererPath(m_renderer);
        QSignalSpy done(&controller, &HistoryReportController::htmlExported);
        QSignalSpy errors(&controller, &HistoryReportController::failed);
        const QString base = m_root + QStringLiteral("/exports/");
        controller.exportHtml(anonymousReport(), base + QStringLiteral("wrong.pdf"));
        QCOMPARE(errors.size(), 1);
        controller.exportHtml(HistoryReport(), base + QStringLiteral("empty.html"));
        QCOMPARE(errors.size(), 2);
        controller.exportHtml(anonymousReport(), m_root + QStringLiteral("/outside.html"));
        QCOMPARE(errors.size(), 3);
        const QByteArray before("owned pre-existing export bytes");
        QVERIFY(write(base + QStringLiteral("existing.html"), before));
        controller.exportHtml(anonymousReport(), base + QStringLiteral("existing.html"));
        QCOMPARE(errors.size(), 4);
        QCOMPARE(read(base + QStringLiteral("existing.html")), before);
        HistoryReport ampersand = anonymousReport();
        ampersand.patient.name = QStringLiteral("匿名&amp;甲");
        controller.exportHtml(ampersand, base + QStringLiteral("entity.html"));
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 5, 30000);
        QVERIFY(!QFileInfo::exists(base + QStringLiteral("entity.html")));
        QVERIFY(errors.last().last().toString().contains(QStringLiteral("未转义")));
        controller.exportHtml(anonymousReport(), base + QStringLiteral("recovery.html"));
        controller.exportHtml(anonymousReport(), base + QStringLiteral("duplicate.html"));
        QCOMPARE(errors.size(), 6);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
        QVERIFY(!QFileInfo::exists(base + QStringLiteral("duplicate.html")));
        QVERIFY(QDir(base).entryList({QStringLiteral(".report-job-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
    }

    void publicationFailureAndRecovery()
    {
        HistoryReportController controller;
        controller.setDataDirectory(m_root);
        controller.setRendererPath(m_renderer);
        QSignalSpy done(&controller, &HistoryReportController::htmlExported);
        QSignalSpy errors(&controller, &HistoryReportController::failed);
        const QString exports = m_root + QStringLiteral("/exports");
        const QString collision = exports + QStringLiteral("/publication_collision.html");
        controller.exportHtml(anonymousReport(), collision);
        const QByteArray ownedBytes("test-created destination while renderer is running");
        QVERIFY(write(collision, ownedBytes));
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 30000);
        QCOMPARE(read(collision), ownedBytes);
        QCOMPARE(done.size(), 0);
        controller.setRendererPath(m_root + QStringLiteral("/missing-renderer"));
        controller.exportHtml(anonymousReport(), exports + QStringLiteral("/missing_helper.html"));
        QCOMPARE(errors.size(), 2);
        controller.setRendererPath(m_renderer);
        controller.exportHtml(anonymousReport(), exports + QStringLiteral("/after_publication_failure.html"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 30000);
    }

    void previewCancelBusyAndPdfRegression()
    {
        QWidget host;
        HistoryReportController controller(&host);
        controller.setDataDirectory(m_root);
        controller.setRendererPath(m_renderer);
        QSignalSpy ready(&controller, &HistoryReportController::previewReady);
        QSignalSpy closed(&controller, &HistoryReportController::previewClosed);
        QSignalSpy errors(&controller, &HistoryReportController::failed);
        QSignalSpy htmlDone(&controller, &HistoryReportController::htmlExported);
        QSignalSpy pdfDone(&controller, &HistoryReportController::pdfExported);
        controller.preview(anonymousReport());
        QTRY_COMPARE(ready.size(), 1);
        auto *preview = qvariant_cast<QPrintPreviewDialog *>(ready.first().first());
        QVERIFY(preview);
        auto *htmlAction = preview->findChild<QAction *>(QStringLiteral("actExpToHtml"));
        QVERIFY(htmlAction && htmlAction->isEnabled());
        QVERIFY(preview->findChild<QAction *>(QStringLiteral("actExpToXlsx")) == nullptr);
        htmlAction->trigger();
        auto *chooser = preview->findChild<QFileDialog *>(QStringLiteral("historyHtmlFileDialog"));
        QVERIFY(chooser);
        chooser->reject();
        QCOMPARE(htmlDone.size(), 0);
        htmlAction->trigger();
        QTRY_VERIFY(preview->findChild<QFileDialog *>(QStringLiteral("historyHtmlFileDialog")) != nullptr);
        for (auto *dialog : preview->findChildren<QFileDialog *>()) dialog->reject();
        const QString path = m_root + QStringLiteral("/exports/preview_export.html");
        controller.exportHtml(anonymousReport(), path);
        preview->close();
        QTRY_COMPARE(closed.size(), 1);
        controller.exportPdf(anonymousReport(), m_root + QStringLiteral("/exports/duplicate.pdf"));
        QCOMPARE(errors.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(htmlDone.size(), 1, 30000);
        const QString pdf = m_root + QStringLiteral("/exports/pdf_regression.pdf");
        controller.exportPdf(anonymousReport(), pdf);
        QTRY_COMPARE_WITH_TIMEOUT(pdfDone.size(), 1, 30000);
        QVERIFY(read(pdf).startsWith("%PDF-"));
        QVERIFY(read(pdf).contains("%%EOF"));
        qInfo() << "PDF_REGRESSION" << QFileInfo(pdf).size() << pdf;
    }

private:
    QString m_root;
    QString m_renderer;
};

QTEST_MAIN(ReportRemainingHtmlTest)
#include "report_remaining_html_test.moc"
