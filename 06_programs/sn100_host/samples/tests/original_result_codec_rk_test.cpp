#include "original_result_codec.h"
#include <QtTest>
using namespace OriginalTasks;
class OriginalResultCodecRkTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("只允许在RK ARM64运行。");
#endif
    }
    void resultKeepsTaskIdentityAndEveryRawSample()
    {
        const QByteArray payload("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][1][1][37.1][r_1][1/2][100][1][s_1][][][][405nm-1,2,-3,4|570nm-5,6,7,8]");
        ReceivedResult result;
        QString message;
        QVERIFY2(parseResult(payload, &result, &message),qPrintable(message));
        QCOMPARE(result.taskId,qint64(41));
        QCOMPARE(result.fields.value(QStringLiteral("project_name")),QStringLiteral("PT"));
        QCOMPARE(result.fields.value(QStringLiteral("sample_id")),QStringLiteral("BAR99"));
        QCOMPARE(result.fields.value(QStringLiteral("needle_temp")),QStringLiteral("37.1"));
        QCOMPARE(result.fields.value(QStringLiteral("spectrum_405nm")),QStringLiteral("1,2,-3,4"));
        QCOMPARE(result.fields.value(QStringLiteral("spectrum_570nm")),QStringLiteral("5,6,7,8"));
        QCOMPARE(result.payload,payload);
        QVERIFY(!result.fields.contains(QStringLiteral("result_original")));
    }
    void missingAndExtraFieldsCannotShiftTheTaskNumber()
    {
        ReceivedResult result;
        QString message;
        QVERIFY(!parseResult("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][1][1]",&result,&message));
        QVERIFY(!parseResult("Tr[SAM][S01][BAR99][BAD][9][PT][Default][W1][T2][0][1/2][100][1][s_1][][][][405nm-1,2]",&result,&message));
        QVERIFY(!parseResult("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][0][1/2][100][1][s_1][][][][405nm-1,2][EXTRA]",&result,&message));
        QVERIFY(!parseResult("Tr[SAM][S01][BAR99][41][9][PT][Default][W1][T2][0][1/2][100][1][s_1][][][][405nm-1,2|405nm-3,4]",&result,&message));
    }
    void supportsBothOriginalProgressMessages()
    {
        ReceivedProgress progress;
        QString message;
        QVERIFY(parseProgress("TD[S01][1][41]",&progress,&message));
        QCOMPARE(progress.taskId,qint64(41));
        QVERIFY(parseProgress("TD[S01][1][41][2][][SAM][PT]",&progress,&message));
        QCOMPARE(progress.state,QStringLiteral("2"));
        QCOMPARE(progress.project,QStringLiteral("PT"));
        QVERIFY(!parseProgress("TD[S01][1]",&progress,&message));
    }
};
QTEST_GUILESS_MAIN(OriginalResultCodecRkTest)
#include "original_result_codec_rk_test.moc"
