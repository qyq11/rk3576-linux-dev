#include "read_only_messages.h"
#include "frame_codec.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace Sn100Protocol;

namespace
{
// 独立字面量由原源码的字节/长度/XOR规则核算，非仪器捕获，非被测编码器输出。
const QByteArray kChannelFrame = QByteArray::fromHex(
    "060700190000000151715b325d5b315d5b3132335d5b325d5b3435365d110809");
const QByteArray kAlarmFrame = QByteArray::fromHex(
    "0607001600000001516b5b315d5b53494d2d45315d5b417c425d7d0809");
const QByteArray kAbnormalFrame = QByteArray::fromHex(
    "0607003500000001514b5b315d5b53494d2d45315d5b73696d756c61746564206661756c745d"
    "5b73696d756c61746564206d6561737572655d360809");

bool parse(const QByteArray &payload, ReadOnlyMessage *message, QString *error)
{
    return parseReadOnlyMessage(payload, message, error);
}

bool matchesExpectedSubset(const QJsonObject &actual, const QJsonObject &expected)
{
    for (auto field = expected.begin(); field != expected.end(); ++field)
    {
        if (!actual.contains(field.key()) || actual.value(field.key()) != field.value())
        {
            return false;
        }
    }
    return true;
}
}

class ReadOnlyMessagesTest : public QObject
{
    Q_OBJECT

private slots:
    void queryWhitelist();
    void literalFrames_data();
    void literalFrames();
    void channelStringKeysAndFirstValue();
    void rawAdPreserved();
    void alarmDuplicatesAndCase_data();
    void alarmDuplicatesAndCase();
    void consumablesTwoMapsAndLastValue();
    void emptyCollections_data();
    void emptyCollections();
    void validUtf8AndNoActions();
    void invalidMessages_data();
    void invalidMessages();
    void fieldByteBudget();
    void collectionBudget();
    void aggregateFieldBudget();
    void atomicOutputAndStatusDelegation();
    void fixtureExpectedOutputs();
};

void ReadOnlyMessagesTest::queryWhitelist()
{
    QCOMPARE(queryPayload(ReadOnlyQuery::Status), QByteArray("Qj"));
    QCOMPARE(queryPayload(ReadOnlyQuery::Alarm), QByteArray("Qk"));
    QCOMPARE(queryPayload(ReadOnlyQuery::Consumables), QByteArray("QH"));
    QVERIFY(queryPayload(static_cast<ReadOnlyQuery>(999)).isEmpty());

    QString error;
    const auto alarm = encodeMessage(queryPayload(ReadOnlyQuery::Alarm), Direction::HostToDevice, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(alarm.size(), 1);
    QCOMPARE(alarm.first(), QByteArray::fromHex("0203000600000001516b3b0405"));
    const auto consumables = encodeMessage(queryPayload(ReadOnlyQuery::Consumables), Direction::HostToDevice, &error);
    QCOMPARE(consumables.size(), 1);
    QCOMPARE(consumables.first(), QByteArray::fromHex("02030006000000015148180405"));
}

void ReadOnlyMessagesTest::literalFrames_data()
{
    QTest::addColumn<QByteArray>("frame");
    QTest::addColumn<QByteArray>("payload");
    QTest::newRow("channel") << kChannelFrame << QByteArray("Qq[2][1][123][2][456]");
    QTest::newRow("old-alarm") << kAlarmFrame << QByteArray("Qk[1][SIM-E1][A|B]");
    QTest::newRow("new-abnormal") << kAbnormalFrame
                                  << QByteArray("QK[1][SIM-E1][simulated fault][simulated measure]");
}

void ReadOnlyMessagesTest::literalFrames()
{
    QFETCH(QByteArray, frame);
    QFETCH(QByteArray, payload);
    // 每个TCP切点均走真实帧层和新消息解析；黄金输入不经encodeMessage构造。
    for (int split = 0; split <= frame.size(); ++split)
    {
        FrameDecoder decoder;
        const DecodeBatch first = decoder.feed(frame.left(split), 0);
        const DecodeBatch last = decoder.feed(frame.mid(split), 1);
        QVERIFY(first.errors.isEmpty());
        QVERIFY(last.errors.isEmpty());
        QList<QByteArray> decoded = first.messages;
        decoded.append(last.messages);
        QCOMPARE(decoded.size(), 1);
        QCOMPARE(decoded.first(), payload);
        ReadOnlyMessage message;
        QString error;
        QVERIFY2(parse(decoded.first(), &message, &error), qPrintable(error));
        QCOMPARE(message.command, payload.left(2));
        QCOMPARE(message.data.value("evidence").toString(), QString("simulated-source-derived"));
        QCOMPARE(message.data.value("source_encoding").toString(), QString("utf-8-offline-profile"));
    }
}

void ReadOnlyMessagesTest::channelStringKeysAndFirstValue()
{
    ReadOnlyMessage message;
    QString error;
    QVERIFY2(parse("Qq[4][Base][10 20 30 40][1][123][01][00123][1][999]", &message, &error), qPrintable(error));
    const QJsonObject channels = message.data.value("channels").toObject();
    QCOMPARE(channels.keys(), QStringList({"01", "1", "Base"}));
    QCOMPARE(channels.value("1").toString(), QString("123"));
    QCOMPARE(channels.value("01").toString(), QString("00123"));
    QCOMPARE(channels.value("Base").toString(), QString("10 20 30 40"));
    QCOMPARE(message.data.value("declared_count").toInt(), 4);
    QCOMPARE(message.data.value("duplicate_channels").toInt(), 1);
}

void ReadOnlyMessagesTest::rawAdPreserved()
{
    ReadOnlyMessage message;
    QString error;
    const QByteArray payload("Qq[3][Base][  -1 NaN 1.2300  raw  ][base][][A][not-a-number]");
    QVERIFY2(parse(payload, &message, &error), qPrintable(error));
    const auto channels = message.data.value("channels").toObject();
    QCOMPARE(channels.value("Base").toString(), QString("  -1 NaN 1.2300  raw  "));
    QCOMPARE(channels.value("base").toString(), QString(""));
    QCOMPARE(channels.value("A").toString(), QString("not-a-number"));
}

void ReadOnlyMessagesTest::alarmDuplicatesAndCase_data()
{
    QTest::addColumn<QByteArray>("command");
    QTest::addColumn<bool>("board");
    QTest::addColumn<bool>("notification");
    QTest::newRow("Qk-current-legacy") << QByteArray("Qk") << false << false;
    QTest::newRow("Ql-new-legacy") << QByteArray("Ql") << false << true;
    QTest::newRow("QK-current-board") << QByteArray("QK") << true << false;
    QTest::newRow("QL-new-board") << QByteArray("QL") << true << true;
}

void ReadOnlyMessagesTest::alarmDuplicatesAndCase()
{
    QFETCH(QByteArray, command);
    QFETCH(bool, board);
    QFETCH(bool, notification);
    const QByteArray payload = command + (board ? "[2][E][first][measure1][E][second][measure2]"
                                               : "[2][E][A||B|][E][second]");
    ReadOnlyMessage message;
    QString error;
    QVERIFY2(parse(payload, &message, &error), qPrintable(error));
    const QJsonArray alarms = message.data.value("alarms").toArray();
    QCOMPARE(alarms.size(), 2);
    QCOMPARE(alarms.at(0).toObject().value("error_code").toString(), QString("E"));
    QCOMPARE(alarms.at(1).toObject().value("error_code").toString(), QString("E"));
    QCOMPARE(message.data.value("from_board").toBool(), board);
    QCOMPARE(message.data.value("notification").toBool(), notification);
    if (board)
    {
        QCOMPARE(alarms.at(0).toObject().value("description").toString(), QString("first"));
        QCOMPARE(alarms.at(1).toObject().value("measure").toString(), QString("measure2"));
        QVERIFY(!alarms.at(0).toObject().contains("raw_parameters"));
    }
    else
    {
        QCOMPARE(alarms.at(0).toObject().value("raw_parameters").toString(), QString("A||B|"));
        QCOMPARE(alarms.at(1).toObject().value("raw_parameters").toString(), QString("second"));
        QVERIFY(!alarms.at(0).toObject().contains("description"));
    }
}

void ReadOnlyMessagesTest::consumablesTwoMapsAndLastValue()
{
    ReadOnlyMessage message;
    QString error;
    QVERIFY2(parse("QH[3][PT][10][APTT][unknown][PT][11][2][PT][3][PT][004]", &message, &error), qPrintable(error));
    const QJsonObject has = message.data.value("has").toObject();
    const QJsonObject used = message.data.value("used").toObject();
    QCOMPARE(has.keys(), QStringList({"APTT", "PT"}));
    QCOMPARE(has.value("APTT").toString(), QString("unknown"));
    QCOMPARE(has.value("PT").toString(), QString("11"));
    QCOMPARE(used.value("PT").toString(), QString("004"));
    QCOMPARE(message.data.value("declared_has_count").toInt(), 3);
    QCOMPARE(message.data.value("declared_used_count").toInt(), 2);
}

void ReadOnlyMessagesTest::emptyCollections_data()
{
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QString>("field");
    QTest::newRow("AD-empty") << QByteArray("Qq[0]") << QString("channels");
    QTest::newRow("alarm-empty") << QByteArray("Qk[0]") << QString("alarms");
    QTest::newRow("new-alarm-empty") << QByteArray("Ql[0]") << QString("alarms");
    QTest::newRow("abnormal-empty") << QByteArray("QK[0]") << QString("alarms");
    QTest::newRow("new-abnormal-empty") << QByteArray("QL[0]") << QString("alarms");
    QTest::newRow("consumables-both-empty") << QByteArray("QH[0][0]") << QString("has");
}

void ReadOnlyMessagesTest::emptyCollections()
{
    QFETCH(QByteArray, payload);
    QFETCH(QString, field);
    ReadOnlyMessage message;
    QString error;
    QVERIFY2(parse(payload, &message, &error), qPrintable(error));
    QVERIFY(message.data.contains(field));
    if (field == "alarms") QVERIFY(message.data.value(field).toArray().isEmpty());
    else QVERIFY(message.data.value(field).toObject().isEmpty());
    if (payload.startsWith("QH")) QVERIFY(message.data.value("used").toObject().isEmpty());
}

void ReadOnlyMessagesTest::validUtf8AndNoActions()
{
    ReadOnlyMessage message;
    QString error;
    const QString description = QString::fromUtf8("模拟异常：无设备");
    const QString measure = QString::fromUtf8("仅文本：不要执行复位");
    const QByteArray payload = "QK[1][SIM-中文][" + description.toUtf8() + "][" + measure.toUtf8() + "]";
    QVERIFY2(parse(payload, &message, &error), qPrintable(error));
    const auto alarm = message.data.value("alarms").toArray().first().toObject();
    QCOMPARE(alarm.value("description").toString(), description);
    QCOMPARE(alarm.value("measure").toString(), measure);
    QCOMPARE(alarm.value("error_code").toString(), QString::fromUtf8("SIM-中文"));
    QVERIFY(!message.data.contains("actions"));
    QVERIFY(!message.data.contains("transmitted"));
}

void ReadOnlyMessagesTest::invalidMessages_data()
{
    QTest::addColumn<QByteArray>("payload");
    const QList<QPair<QByteArray, QByteArray>> cases = {
        {"empty", ""}, {"one-byte", "Q"}, {"unknown-command", "Qh[0]"},
        {"receive-only-not-query", "Qq"}, {"missing-count", "Qk"},
        {"negative-count", "Qq[-1]"}, {"empty-count", "Qq[]"},
        {"signed-count", "Qq[+1][1][2]"}, {"fraction-count", "Qq[1.0][1][2]"},
        {"space-count", "Qq[ 1][1][2]"}, {"overflow-count", "Qq[4294967296]"},
        {"long-overflow", "Qq[99999999999999999999999999999]"},
        {"too-few-pairs", "Qq[2][1][123]"}, {"extra-pair", "Qq[0][1][123]"},
        {"old-alarm-wrong-width", "Qk[1][E][desc][measure]"},
        {"new-alarm-wrong-width", "QK[1][E][description]"},
        {"old-alarm-partial", "Ql[1][E]"}, {"new-alarm-extra", "QL[0][E]"},
        {"consumables-no-used-count", "QH[0]"},
        {"consumables-truncated-has", "QH[1][PT]"},
        {"consumables-truncated-used", "QH[0][1][PT]"},
        {"consumables-extra", "QH[0][0][extra]"},
        {"consumables-negative-used", "QH[0][-1]"},
        {"nested-bracket", "Qk[1][E][[nested]]"}, {"missing-close", "Qk[0"},
        {"stray-close", "Qk[0]]"}, {"prefix-noise", "XQk[0]"},
        {"suffix-noise", "Qk[0]x"}, {"whitespace-between", "Qk [0]"},
        {"control-newline", "Qk[1][E][a\nb]"}, {"control-tab", "Qk[1][E][a\tb]"}
    };
    for (const auto &entry : cases) QTest::newRow(entry.first.constData()) << entry.second;
    QTest::newRow("utf8-invalid-continuation") << (QByteArray("QK[1][E][") + QByteArray::fromHex("c328") + "][M]");
    QTest::newRow("utf8-truncated") << (QByteArray("QK[1][E][") + QByteArray::fromHex("e4b8"));
    QTest::newRow("utf8-overlong-nul") << (QByteArray("QK[1][E][") + QByteArray::fromHex("c080") + "][M]");
    QTest::newRow("utf8-surrogate") << (QByteArray("QK[1][E][") + QByteArray::fromHex("eda080") + "][M]");
    QTest::newRow("unicode-control") << (QByteArray("QK[1][E][") + QByteArray::fromHex("c285") + "][M]");
    QTest::newRow("embedded-nul") << (QByteArray("Qk[1][E][") + QByteArray(1, '\0') + "]");
    QTest::newRow("message-budget") << (QByteArray("Qq") + QByteArray(kMaxMessageBytes, 'x'));
}

void ReadOnlyMessagesTest::invalidMessages()
{
    QFETCH(QByteArray, payload);
    ReadOnlyMessage message;
    message.command = "unchanged";
    message.data = {{QStringLiteral("untouched"), true}};
    QString error;
    QVERIFY(!parse(payload, &message, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(message.command, QByteArray("unchanged"));
    QCOMPARE(message.data, QJsonObject({{QStringLiteral("untouched"), true}}));
}

void ReadOnlyMessagesTest::fieldByteBudget()
{
    ReadOnlyMessage message;
    QString error;
    QVERIFY2(parse("Qq[1][Base][" + QByteArray(4096, 'x') + "]", &message, &error), qPrintable(error));
    QVERIFY(!parse("Qq[1][Base][" + QByteArray(4097, 'x') + "]", &message, &error));
    // 1365个三字节汉字=4095字节；按字节预算而非UTF-16字符数量。
    const QByteArray chinese = QString(1365, QChar(0x4e2d)).toUtf8();
    QVERIFY2(parse("Qq[1][Base][" + chinese + "]", &message, &error), qPrintable(error));
    QVERIFY(!parse("Qq[1][Base][" + chinese + QString(QChar(0x4e2d)).toUtf8() + "]", &message, &error));
}

void ReadOnlyMessagesTest::collectionBudget()
{
    ReadOnlyMessage message;
    QString error;
    QByteArray valid("Qq[1024]");
    for (int index = 0; index < 1024; ++index) valid += "[" + QByteArray::number(index) + "][raw]";
    QVERIFY2(parse(valid, &message, &error), qPrintable(error));
    QCOMPARE(message.data.value("channels").toObject().size(), 1024);
    QVERIFY(!parse("Qq[1025]", &message, &error));
    QVERIFY(!parse("QH[0][1025]", &message, &error));
}

void ReadOnlyMessagesTest::aggregateFieldBudget()
{
    QByteArray valid("QH[1023]");
    for (int index = 0; index < 1023; ++index) valid += "[P][has]";
    valid += "[1024]";
    for (int index = 0; index < 1024; ++index) valid += "[P][used]";
    ReadOnlyMessage message;
    QString error;
    QVERIFY2(parse(valid, &message, &error), qPrintable(error)); // 恰好4096字段。
    QCOMPARE(message.data.value("has").toObject().value("P").toString(), QString("has"));
    QVERIFY(!parse(valid + "[]", &message, &error));
    QVERIFY(error.startsWith("message.limit:"));
}

void ReadOnlyMessagesTest::atomicOutputAndStatusDelegation()
{
    ReadOnlyMessage message;
    QString error = "previous-error";
    QVERIFY2(parse("Qj[4][SIMULATED][2][1][0][0]", &message, &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(message.data.value("main_status").toInt(), 2);
    const auto original = message.data;
    QVERIFY(!parse("Qj[4][SIMULATED][99][1][0][0]", &message, &error));
    QCOMPARE(message.data, original);
    QCOMPARE(message.command, QByteArray("Qj"));
    QVERIFY(!parseReadOnlyMessage("Qk[0]", nullptr, &error));
    QVERIFY(parseReadOnlyMessage("Qk[0]", &message, nullptr));
}

void ReadOnlyMessagesTest::fixtureExpectedOutputs()
{
    const QString fixturePath = QFINDTESTDATA("fixtures/protocol_diagnostics.json");
    QVERIFY(!fixturePath.isEmpty());
    QFile file(fixturePath);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &jsonError);
    QCOMPARE(jsonError.error, QJsonParseError::NoError);
    const QJsonObject root = document.object();
    QCOMPARE(root.value("schema").toString(), QString("sn100-protocol-diagnostics-v1"));
    QCOMPARE(root.value("evidence").toString(), QString("simulated-source-derived"));
    const QJsonArray cases = root.value("cases").toArray();
    QVERIFY(cases.size() >= 8);
    for (const QJsonValue &value : cases)
    {
        const QJsonObject entry = value.toObject();
        const QByteArray payload = QByteArray::fromHex(entry.value("payload_hex").toString().toLatin1());
        ReadOnlyMessage message;
        QString error;
        const bool accepted = parse(payload, &message, &error);
        QCOMPARE(accepted, entry.value("valid").toBool());
        if (accepted)
        {
            QCOMPARE(message.command, entry.value("command").toString().toLatin1());
            QVERIFY2(matchesExpectedSubset(message.data, entry.value("expected").toObject()),
                     qPrintable(entry.value("name").toString()));
        }
        else
        {
            QVERIFY(!error.isEmpty());
        }
    }
}

QTEST_GUILESS_MAIN(ReadOnlyMessagesTest)
#include "read_only_messages_test.moc"
