#include "original_task_codec.h"
#include <QtTest>
using namespace OriginalTasks;

class OriginalTaskCodecRkTest : public QObject
{
    Q_OBJECT
    Task task() const
    {
        Task value;
        value.taskId = 81;
        value.reportRow = 42;
        value.rack = QStringLiteral("S01");
        value.hole = 2;
        value.container = QStringLiteral("cup");
        value.sample = QStringLiteral("BAR123");
        value.presetSampleId = QStringLiteral("17");
        value.rate = QStringLiteral("1/2");
        value.batchGroup = QStringLiteral("Default");
        value.project.id = 7;
        value.project.name = QStringLiteral("PT");
        value.project.enabled = true;
        value.project.originalFileName = QStringLiteral("PT.db");
        value.project.advanced.configured = true;
        value.project.originalBaseValues = {{"project_id","9"},{"project_en_name","PT"},{"test_analysis_method",QStringLiteral("凝固法")},
            {"is_related","0"},{"warmup_banding_channel","1"},{"test_banding_channel","2"},
            {"min_test_time","5"},{"max_test_time","120"},{"sample_change_clean","30"},{"first_light","405"},{"second_light","none"},{"IBS_name","IBS"}};
        value.project.sampleRates.append({QStringLiteral("1/2"),true,true,1});
        value.project.advanced.samples.append({{"sample_rate","1/2"},{"sample_option","1"},{"IBS_amount_ul","10"},{"sample_amount_ul","20"},
            {"blender_type","2"},{"sample_add_mode","0"},{"mix_amount_ul","3"},{"second_IBS_amount_ul","4"},{"action_amount_ul","5"},
            {"before_clean_name","Cleaner"},{"before_clean_frequency","1"},{"before_clean_amount_ul","6"},
            {"after_clean_name","Cleaner"},{"after_clean_frequency","2"},{"after_clean_amount_ul","7"}});
        value.project.advanced.reagents.append({{"reagent_name","R1"},{"needle_select",QStringLiteral("左试剂针")},{"reagent_amount_ul","50"},
            {"blender_type","1"},{"before_clean_name","Cleaner"},{"before_clean_frequency","2"},{"before_clean_amount_ul","10"},
            {"after_clean_name","Cleaner"},{"after_clean_frequency","3"},{"after_clean_amount_ul","11"},{"blender_time_sec","4"},{"reagent_warmup_length","8"}});
        value.reagentSlots.append({QStringLiteral("r_1"),QStringLiteral("1")});
        value.sampleAreaSlots.insert(QStringLiteral("IBS"), {QStringLiteral("s_1"),QStringLiteral("1")});
        value.sampleAreaSlots.insert(QStringLiteral("Cleaner"), {QStringLiteral("s_2"),QStringLiteral("2")});
        value.reagentAreaSlots.insert(QStringLiteral("Cleaner"), {QStringLiteral("r_2"),QStringLiteral("2")});
        return value;
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("只允许在RK ARM64运行此驱动。");
#endif
    }
    void parsesActualRackRequestAndRejectsIncompleteFields()
    {
        RackRequest request;
        QString message;
        QVERIFY(parseRackRequest("Tt[S01][2][1][BAR001][2][none][2][81][82]", &request, &message));
        QCOMPARE(request.rack, QStringLiteral("S01"));
        QCOMPARE(request.barcodes.value(2), QStringLiteral("none"));
        QCOMPARE(request.receivedTaskIds, QSet<qint64>({81,82}));
        QVERIFY(!parseRackRequest("Tt[S01][2][1][BAR001]", &request, &message));
        QVERIFY(!parseRackRequest("Tt[S01][2][1][A][1][B][0]", &request, &message));
        QVERIFY(!parseRackRequest("Tt[S01][0][2][1]", &request, &message));
        QVERIFY(parseRackRequest("Tt[][0][0]", &request, &message));
        QVERIFY(request.rack.isEmpty()); // 不在文本解析层猜测架号，交给原扫码配置处理。
    }
    void newControllerPayloadMatchesWindowsGolden()
    {
        const QByteArray expected("Tt[S01][1][SAM][81][2][cup][BAR123][PT][0][0][1/2][Default][s_1][1][0][]");
        QByteArray payload;
        QString message;
        QVERIFY2(rackPayload(QStringLiteral("S01"), {task()}, true, &payload, &message), qPrintable(message));
        QCOMPARE(payload, expected);
        auto multiple = task();
        multiple.multiBottle = true;
        QVERIFY(rackPayload(QStringLiteral("S01"), {multiple}, true, &payload, &message));
        QVERIFY(payload.endsWith("[1][0][r_1]"));
        qInfo().noquote() << "GOLDEN_NEW" << payload << payload.toHex();
    }
    void oldControllerPayloadKeepsEveryPlaceholder()
    {
        const QByteArray expected("Tt[S01][1][SAM][81][S01][2][cup][BAR123][9][PT][0][1][2][5][120][405][none][1][1/2][1/2][Default]"
            "[IBS][10][s_1][1][20][2][0][3][4][5][Cleaner][1][6][s_2][2][Cleaner][2][7][s_2][2]"
            "[1][R1][1][50][r_1][1][1][Cleaner][2][10][r_2][2][Cleaner][3][11][][4][8]");
        QByteArray payload;
        QString message;
        QVERIFY2(rackPayload(QStringLiteral("S01"), {task()}, false, &payload, &message), qPrintable(message));
        QCOMPARE(payload, expected);
        auto fromDatabase = task();
        fromDatabase.project.advanced.reagents[0].insert(QStringLiteral("needle_select"),QStringLiteral("1"));
        QVERIFY(rackPayload(QStringLiteral("S01"), {fromDatabase}, false, &payload, &message));
        QCOMPARE(payload, expected);
        auto prolonged = task();
        prolonged.prolonged = true;
        QVERIFY(rackPayload(QStringLiteral("S01"), {prolonged}, false, &payload, &message));
        QVERIFY(payload.contains("[5][150][405]"));
        qInfo().noquote() << "GOLDEN_OLD" << payload << payload.toHex();
    }
    void invalidRateIdentityAndUnrepresentableNameDoNotSend()
    {
        QByteArray payload;
        QString message;
        auto invalid = task();
        invalid.taskId = 0;
        QVERIFY(!rackPayload(QStringLiteral("S01"), {invalid}, true, &payload, &message));
        invalid = task(); invalid.rate = QStringLiteral("1/99");
        QVERIFY(!rackPayload(QStringLiteral("S01"), {invalid}, true, &payload, &message));
        invalid = task(); invalid.sample = QStringLiteral("样本");
        QVERIFY(!rackPayload(QStringLiteral("S01"), {invalid}, true, &payload, &message));
        invalid = task(); invalid.sample = QStringLiteral("A[B]");
        QVERIFY(!rackPayload(QStringLiteral("S01"), {invalid}, true, &payload, &message));
        QVERIFY(rackPayload(QStringLiteral("S01"), {}, true, &payload, &message));
        QCOMPARE(payload, QByteArray("Tt[S01][0]"));
        QVERIFY(rackPayload(QString(), {}, true, &payload, &message));
        QCOMPARE(payload,QByteArray("Tt[][0]"));
    }
};
QTEST_GUILESS_MAIN(OriginalTaskCodecRkTest)
#include "original_task_codec_rk_test.moc"
