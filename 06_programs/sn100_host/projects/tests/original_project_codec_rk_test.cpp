#include "original_project_codec.h"
#include <QtTest>
using namespace OriginalProjectWire;

class OriginalProjectCodecRkTest : public QObject
{
    Q_OBJECT
    QByteArray deviceReply() const
    {
        // 独立写出原slotReceiveProjectInfo的完整顺序；样本和试剂故意用不同数字防错位。
        return "UP[t][PT][9][1][5][120][0][110][none][7][10][405][3][1][20][0][0][IBS][4][Default][30][0][1][0][C1][1][PT]"
               "[1][1][1/2][3][10][20][2][4][6][8][9][10][11][12][1][0][1][0][2][3][Cleaner][AfterS]"
               "[1][1][4][R1][8][1][2][50][3][4][1][5][6][Cleaner][AfterR]"
               "[1][1][Original][Time][2][s][0][999][0][999][1][1][1][PT][2]";
    }
private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("原项目命令检查仅允许在RK ARM64执行。");
#endif
    }
    void parsesDeviceSpecificExtraFieldsWithoutShiftingActions()
    {
        Project project; QString message;
        QVERIFY2(parseDeviceProject(deviceReply(),&project,&message),qPrintable(message));
        QCOMPARE(project.base.value("project_id"),QStringLiteral("9"));
        QCOMPARE(project.base.value("xuhao"),QStringLiteral("7"));
        QCOMPARE(project.base.value("collect_rate_ms"),QStringLiteral("10"));
        QCOMPARE(project.base.value("first_light"),QStringLiteral("405"));
        QCOMPARE(project.samples.size(),1);
        QCOMPARE(project.samples[0].value("sample_add_length"),QStringLiteral("3"));
        QCOMPARE(project.samples[0].value("suck_release_count"),QStringLiteral("6"));
        QCOMPARE(project.samples[0].value("after_clean_amount_ul"),QStringLiteral("12"));
        QCOMPARE(project.samples[0].value("after_clean_name"),QStringLiteral("AfterS"));
        QCOMPARE(project.reagents.size(),1);
        QCOMPARE(project.reagents[0].value("reagent_add_length"),QStringLiteral("4"));
        QCOMPARE(project.reagents[0].value("reagent_amount_ul"),QStringLiteral("50"));
        QCOMPARE(project.reagents[0].value("after_clean_name"),QStringLiteral("AfterR"));
        QCOMPARE(project.results.size(),1);
        QCOMPARE(project.results[0].value("unit_name"),QStringLiteral("s"));
        QCOMPARE(project.coagulationEndMode,QStringLiteral("2"));
    }
    void sendsExactOriginalUpFieldsAndNeedleConversion()
    {
        Project project; QString message; QByteArray payload;
        QVERIFY(parseDeviceProject(deviceReply(),&project,&message));
        project.samples[0].insert("needle_select","1");
        project.reagents[0].insert("needle_select",QStringLiteral("左试剂针"));
        QVERIFY2(updatePayload(project,&payload,&message),qPrintable(message));
        const QByteArray expected =
            "Up[t][PT][1][5][120][0][110][none][405][3][1][20][0][0][IBS][4][Default][30][0][1][0][C1][1][PT]"
            "[1][1][1/2][10][20][2][4][1][8][9][10][11][1][0][1][0][2][3][Cleaner]"
            "[1][1][R1][8][1][2][50][3][4][1][5][Cleaner]"
            "[1][1][Original][Time][2][s][0][999][0][999][1][1][1][PT]";
        QCOMPARE(payload,expected);
        project.reagents[0].insert("needle_select",QStringLiteral("1"));
        QVERIFY(updatePayload(project,&payload,&message));
        QCOMPARE(payload,expected);
        project.reagents[0].insert("needle_select",QStringLiteral("右试剂针"));
        QVERIFY(updatePayload(project,&payload,&message));
        QVERIFY(payload.contains("[R1][8][1][2][50][3][4][2][5][Cleaner]"));
    }
    void rejectsIncompleteExtraOrOversizedRepliesWithoutChangingTheProject()
    {
        Project project; project.base.insert("project_en_name","KEEP"); QString message;
        const QList<QByteArray> invalid{
            deviceReply().chopped(3),deviceReply()+"[extra]","Up[PT]",QByteArray("UP[")+QByteArray(4097,'a')+']',
            QByteArray("UP")+QByteArray(128001,'a'),QByteArray("UP[bad[nested]")};
        for (const auto &reply : invalid)
        {
            QVERIFY(!parseDeviceProject(reply,&project,&message));
            QVERIFY(!message.isEmpty());
            QCOMPARE(project.base.value("project_en_name"),QStringLiteral("KEEP"));
        }
        auto impossibleCount=deviceReply();
        QVERIFY(impossibleCount.replace("[PT][1][1][1/2]","[PT][257][1][1/2]")!=deviceReply());
        QVERIFY(!parseDeviceProject(impossibleCount,&project,&message));
    }
    void rejectsUnrepresentableFieldsWithoutPartialCommand()
    {
        Project project; QString message; QByteArray payload("KEEP");
        QVERIFY(parseDeviceProject(deviceReply(),&project,&message));
        for (const auto &name : {QStringLiteral("bad[name"),QStringLiteral("含中文"),QStringLiteral("bad\nname")})
        {
            project.base.insert("project_en_name",name);
            QVERIFY(!updatePayload(project,&payload,&message));
            QCOMPARE(payload,QByteArray("KEEP"));
        }
    }
};
QTEST_GUILESS_MAIN(OriginalProjectCodecRkTest)
#include "original_project_codec_rk_test.moc"
