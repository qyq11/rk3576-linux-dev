#include "frame_codec.h"
#include "status_session.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace Sn100Protocol;

namespace
{
// These byte literals were derived separately from the reference's documented
// constants and XOR rule. They are synthetic input, never captured hardware RX.
const QByteArray kQueryFrame = QByteArray::fromHex("0203000600000001516a3a0405");
const QByteArray kAdFrame = QByteArray::fromHex(
    "060700190000000151715b325d5b315d5b3132335d5b325d5b3435365d110809");
const QByteArray kAdMessage("Qq[2][1][123][2][456]");
const QByteArray kAcknowledgement = QByteArray::fromHex("060700090000000154735b315d110809");
const QByteArray kPartZero = QByteArray::fromHex("0607000c0000000251715b325d5b315d210809");
const QByteArray kPartOne = QByteArray::fromHex("06070011000100025b3132335d5b325d5b3435365d300809");
const QByteArray kStatusMessage(
    "Qj[25][SIMULATED][2][1][0][][1][0][1][sim-v1|sim-v2][SIM-001]"
    "[0][0][0][offline-v1][0][0][1][1][10][LinuxNew][20][18][3][10][0][0]");
const QByteArray kStatusFrame = QByteArray::fromHex(
    "0607008500000001516a5b32355d5b53494d554c415445445d5b325d5b315d5b305d5b5d"
    "5b315d5b305d5b315d5b73696d2d76317c73696d2d76325d5b53494d2d3030315d5b305d"
    "5b305d5b305d5b6f66666c696e652d76315d5b305d5b305d5b315d5b315d5b31305d5b4c"
    "696e75784e65775d5b32305d5b31385d5b335d5b31305d5b305d5b305d4f0809");

void appendBatch(DecodeBatch *all, const DecodeBatch &part)
{
    all->messages.append(part.messages);
    all->errors.append(part.errors);
    all->duplicatePackets += part.duplicatePackets;
}

// Used only to construct malformed semantic headers while retaining a correct
// wire checksum. The golden happy paths above do not depend on this helper.
QByteArray repairChecksum(QByteArray frame)
{
    quint8 check = 0;
    for(int offset = 4; offset < frame.size() - 3; ++offset)
        check ^= static_cast<quint8>(frame.at(offset));
    frame[frame.size() - 3] = static_cast<char>(check);
    return frame;
}
}

class ProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void literalTx();
    void literalRx();
    void literalStatus();
    void everyStreamSplit();
    void bytewiseStream();
    void coalescedMessages();
    void leadingNoiseAndPartialHeader();
    void malformedLength_data();
    void malformedLength();
    void corruptChecksumAndRecovery();
    void corruptTrailerAndRecovery();
    void rejectWrongDirection();
    void multipartLiterals();
    void duplicatePacket();
    void reorderedPacketsAndRecovery();
    void conflictingPacketAndRecovery();
    void inconsistentCountAndRecovery();
    void invalidPacketCount_data();
    void invalidPacketCount();
    void disconnectDropsPartialInput();
    void frameIdleTimeout();
    void assemblyIdleTimeout();
    void encoderLimits();
    void maximumMessageAndBoundedInput();
    void repeatedMessagesDoNotGrow();
    void defaultStatusJsonIsSafe();
    void publicStatusJsonBoundsAreSafe();
    void statusFieldsAndEnums();
    void malformedStatus_data();
    void malformedStatus();
    void unknownSubsystemKeepsRawValues();
    void sessionOnlyAcceptsStatus();
    void sessionDuplicateStatus();
    void sessionRetryAndTimeout();
    void sessionReceiveAtDeadline();
    void invalidStatusNeverRefreshesSession();
    void sessionDisconnectDropsOldBytes();
    void sessionSilenceAndTimeReversal();
    void replayFixture();
};

void ProtocolTest::literalTx()
{
    QString error;
    const auto frames = encodeMessage("Qj", Direction::HostToDevice, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(frames, QList<QByteArray>({kQueryFrame}));
    FrameDecoder peer(Direction::HostToDevice);
    const auto received = peer.feed(kQueryFrame, 0);
    QCOMPARE(received.messages, QList<QByteArray>({QByteArray("Qj")}));
    QVERIFY(received.errors.isEmpty());
    qInfo().noquote() << "SYNTHETIC TX #1 query status HEX=" << frames.first().toHex(' ')
                      << "PAYLOAD=Qj";
}

void ProtocolTest::literalRx()
{
    FrameDecoder decoder;
    const auto result = decoder.feed(kAdFrame, 10);
    QCOMPARE(result.messages, QList<QByteArray>({kAdMessage}));
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join("; ")));
    QCOMPARE(decoder.bufferedBytes(), 0);
    QCOMPARE(decoder.assemblyBytes(), 0);
    qInfo().noquote() << "SYNTHETIC RX #1 AD HEX=" << kAdFrame.toHex(' ')
                      << "PAYLOAD=" << result.messages.first();
}

void ProtocolTest::literalStatus()
{
    FrameDecoder decoder;
    const auto result = decoder.feed(kStatusFrame, 0);
    QCOMPARE(result.messages, QList<QByteArray>({kStatusMessage}));
    QVERIFY(result.errors.isEmpty());
    qInfo().noquote() << "SYNTHETIC RX #2 status HEX=" << kStatusFrame.toHex(' ')
                      << "PAYLOAD=" << result.messages.first();
}

void ProtocolTest::everyStreamSplit()
{
    for(int split = 0; split <= kAdFrame.size(); ++split)
    {
        FrameDecoder decoder;
        DecodeBatch all = decoder.feed(kAdFrame.left(split), 0);
        appendBatch(&all, decoder.feed(kAdFrame.mid(split), 1));
        QCOMPARE(all.messages, QList<QByteArray>({kAdMessage}));
        QVERIFY2(all.errors.isEmpty(), qPrintable(QString("split=%1: %2")
                                                 .arg(split).arg(all.errors.join("; "))));
    }
}

void ProtocolTest::bytewiseStream()
{
    FrameDecoder decoder;
    DecodeBatch all;
    for(int index = 0; index < kStatusFrame.size(); ++index)
        appendBatch(&all, decoder.feed(kStatusFrame.mid(index, 1), index));
    QCOMPARE(all.messages, QList<QByteArray>({kStatusMessage}));
    QVERIFY(all.errors.isEmpty());
}

void ProtocolTest::coalescedMessages()
{
    FrameDecoder decoder;
    const auto result = decoder.feed(kAdFrame + kAcknowledgement + kStatusFrame, 0);
    QCOMPARE(result.messages, QList<QByteArray>({kAdMessage, QByteArray("Ts[1]"), kStatusMessage}));
    QVERIFY(result.errors.isEmpty());
}

void ProtocolTest::leadingNoiseAndPartialHeader()
{
    FrameDecoder decoder;
    DecodeBatch all = decoder.feed(QByteArray::fromHex("ff000606"), 0);
    appendBatch(&all, decoder.feed(kAdFrame.mid(1), 1));
    QCOMPARE(all.messages, QList<QByteArray>({kAdMessage}));
    QCOMPARE(decoder.bufferedBytes(), 0);
    qInfo().noquote() << "NOISE recovery input=ff 00 06 06 +" << kAdFrame.mid(1).toHex(' ')
                      << "OUTPUT=" << all.messages.first();
}

void ProtocolTest::malformedLength_data()
{
    QTest::addColumn<QByteArray>("header");
    QTest::newRow("zero") << QByteArray::fromHex("06070000");
    QTest::newRow("packet-header-only") << QByteArray::fromHex("06070004");
    QTest::newRow("over-local-frame-limit") << QByteArray::fromHex("060707d5");
    QTest::newRow("uint16-maximum") << QByteArray::fromHex("0607ffff");
}

void ProtocolTest::malformedLength()
{
    QFETCH(QByteArray, header);
    FrameDecoder decoder;
    const auto result = decoder.feed(header + kAdFrame, 0);
    QCOMPARE(result.messages, QList<QByteArray>({kAdMessage}));
    QVERIFY2(!result.errors.isEmpty(), "Bad wire length needs an observable error");
    QVERIFY(decoder.bufferedBytes() <= 65536);
    qInfo().noquote() << "REJECT length HEX=" << header.toHex(' ')
                      << "ERROR=" << result.errors.join("; ");
}

void ProtocolTest::corruptChecksumAndRecovery()
{
    QByteArray bad = kAdFrame;
    bad[bad.size() - 3] = static_cast<char>(static_cast<quint8>(bad.at(bad.size() - 3)) ^ 1);
    FrameDecoder decoder;
    const auto result = decoder.feed(bad + kAcknowledgement, 0);
    QCOMPARE(result.messages, QList<QByteArray>({QByteArray("Ts[1]")}));
    QVERIFY(!result.errors.isEmpty());
    qInfo().noquote() << "REJECT XOR RX HEX=" << bad.toHex(' ')
                      << "ERROR=" << result.errors.join("; ")
                      << "RECOVERED=" << result.messages.first();
}

void ProtocolTest::corruptTrailerAndRecovery()
{
    QByteArray bad = kAdFrame;
    bad[bad.size() - 1] = static_cast<char>(0x00);
    FrameDecoder decoder;
    const auto result = decoder.feed(bad + kAcknowledgement, 0);
    QCOMPARE(result.messages, QList<QByteArray>({QByteArray("Ts[1]")}));
    QVERIFY(!result.errors.isEmpty());
}

void ProtocolTest::rejectWrongDirection()
{
    FrameDecoder decoder;
    const auto result = decoder.feed(kQueryFrame + kAdFrame, 0);
    QCOMPARE(result.messages, QList<QByteArray>({kAdMessage}));
    QVERIFY(!result.errors.isEmpty());
}

void ProtocolTest::multipartLiterals()
{
    FrameDecoder decoder;
    const auto first = decoder.feed(kPartZero, 0);
    QVERIFY(first.messages.isEmpty());
    QVERIFY(first.errors.isEmpty());
    QCOMPARE(decoder.assemblyBytes(), 8);
    const auto second = decoder.feed(kPartOne, 1);
    QCOMPARE(second.messages, QList<QByteArray>({kAdMessage}));
    QVERIFY(second.errors.isEmpty());
    QCOMPARE(decoder.assemblyBytes(), 0);
    qInfo().noquote() << "SYNTHETIC RX multipart #0=" << kPartZero.toHex(' ')
                      << "#1=" << kPartOne.toHex(' ') << "ASSEMBLED=" << second.messages.first();
}

void ProtocolTest::duplicatePacket()
{
    FrameDecoder decoder;
    DecodeBatch all = decoder.feed(kPartZero, 0);
    appendBatch(&all, decoder.feed(kPartZero, 1));
    appendBatch(&all, decoder.feed(kPartOne, 2));
    QCOMPARE(all.messages, QList<QByteArray>({kAdMessage}));
    QCOMPARE(all.duplicatePackets, 1);
    QVERIFY(all.errors.isEmpty());
}

void ProtocolTest::reorderedPacketsAndRecovery()
{
    FrameDecoder decoder;
    DecodeBatch all = decoder.feed(kPartOne, 0);
    QVERIFY(all.messages.isEmpty());
    QVERIFY(!all.errors.isEmpty());
    appendBatch(&all, decoder.feed(kPartZero + kPartOne, 1));
    QCOMPARE(all.messages, QList<QByteArray>({kAdMessage}));
}

void ProtocolTest::conflictingPacketAndRecovery()
{
    QByteArray conflict = kPartZero;
    conflict[8] = 'X';
    conflict = repairChecksum(conflict);
    FrameDecoder decoder;
    DecodeBatch all = decoder.feed(kPartZero + conflict, 0);
    QVERIFY(all.messages.isEmpty());
    QVERIFY(!all.errors.isEmpty());
    decoder.reset();
    appendBatch(&all, decoder.feed(kAdFrame, 1));
    QCOMPARE(all.messages, QList<QByteArray>({kAdMessage}));
}

void ProtocolTest::inconsistentCountAndRecovery()
{
    QByteArray conflict = kPartOne;
    conflict[7] = 3;
    conflict = repairChecksum(conflict);
    FrameDecoder decoder;
    DecodeBatch all = decoder.feed(kPartZero + conflict, 0);
    QVERIFY(all.messages.isEmpty());
    QVERIFY(!all.errors.isEmpty());
    appendBatch(&all, decoder.feed(kAdFrame, 1));
    QCOMPARE(all.messages, QList<QByteArray>({kAdMessage}));
}

void ProtocolTest::invalidPacketCount_data()
{
    QTest::addColumn<int>("index");
    QTest::addColumn<int>("count");
    QTest::newRow("zero-total") << 0 << 0;
    QTest::newRow("over-64-total") << 0 << 65;
    QTest::newRow("index-equals-total") << 1 << 1;
}

void ProtocolTest::invalidPacketCount()
{
    QFETCH(int, index);
    QFETCH(int, count);
    QByteArray bad = kAdFrame;
    bad[4] = static_cast<char>(index >> 8);
    bad[5] = static_cast<char>(index & 0xff);
    bad[6] = static_cast<char>(count >> 8);
    bad[7] = static_cast<char>(count & 0xff);
    bad = repairChecksum(bad);
    FrameDecoder decoder;
    const auto result = decoder.feed(bad + kAcknowledgement, 0);
    QCOMPARE(result.messages, QList<QByteArray>({QByteArray("Ts[1]")}));
    QVERIFY(!result.errors.isEmpty());
}

void ProtocolTest::disconnectDropsPartialInput()
{
    FrameDecoder decoder;
    decoder.feed(kAdFrame.left(12), 0);
    QVERIFY(decoder.bufferedBytes() > 0);
    decoder.reset();
    QCOMPARE(decoder.bufferedBytes(), 0);
    QCOMPARE(decoder.assemblyBytes(), 0);
    const auto tail = decoder.feed(kAdFrame.mid(12), 1);
    QVERIFY(tail.messages.isEmpty());
    const auto fresh = decoder.feed(kAdFrame, 2);
    QCOMPARE(fresh.messages, QList<QByteArray>({kAdMessage}));
    decoder.feed(kPartZero, 3);
    QVERIFY(decoder.assemblyBytes() > 0);
    decoder.reset();
    QCOMPARE(decoder.assemblyBytes(), 0);
    QVERIFY(decoder.feed(kPartOne, 4).messages.isEmpty());
}

void ProtocolTest::frameIdleTimeout()
{
    FrameDecoder decoder;
    decoder.feed(kAdFrame.left(12), 100);
    QVERIFY(decoder.expire(10099).errors.isEmpty());
    QVERIFY(decoder.bufferedBytes() > 0);
    const auto expired = decoder.expire(10100);
    QVERIFY(expired.messages.isEmpty());
    QVERIFY(!expired.errors.isEmpty());
    QCOMPARE(decoder.bufferedBytes(), 0);
    QCOMPARE(decoder.feed(kAdFrame, 10101).messages, QList<QByteArray>({kAdMessage}));
}

void ProtocolTest::assemblyIdleTimeout()
{
    FrameDecoder decoder;
    decoder.feed(kPartZero, 100);
    QVERIFY(decoder.expire(10099).errors.isEmpty());
    QVERIFY(decoder.assemblyBytes() > 0);
    const auto expired = decoder.expire(10100);
    QVERIFY(!expired.errors.isEmpty());
    QCOMPARE(decoder.assemblyBytes(), 0);
    QVERIFY(decoder.feed(kPartOne, 10101).messages.isEmpty());
    QCOMPARE(decoder.feed(kAdFrame, 10102).messages, QList<QByteArray>({kAdMessage}));
}

void ProtocolTest::encoderLimits()
{
    QString error;
    QVERIFY(encodeMessage(QByteArray(), Direction::HostToDevice, &error).isEmpty());
    QVERIFY(!error.isEmpty());
    error.clear();
    const auto single = encodeMessage(QByteArray(2000, 'A'), Direction::HostToDevice, &error);
    QCOMPARE(single.size(), 1);
    QCOMPARE(single.first().size(), 2011);
    QCOMPARE(single.first().mid(2, 2), QByteArray::fromHex("07d4"));
    QVERIFY(error.isEmpty());
    const auto two = encodeMessage(QByteArray(2001, 'A'), Direction::HostToDevice, &error);
    QCOMPARE(two.size(), 2);
    QCOMPARE(two.first().mid(4, 4), QByteArray::fromHex("00000002"));
    QCOMPARE(two.last().mid(4, 4), QByteArray::fromHex("00010002"));
    const auto maximum = encodeMessage(QByteArray(128000, 'A'), Direction::HostToDevice, &error);
    QCOMPARE(maximum.size(), 64);
    QVERIFY(error.isEmpty());
    QVERIFY(encodeMessage(QByteArray(128001, 'A'), Direction::HostToDevice, &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void ProtocolTest::maximumMessageAndBoundedInput()
{
    QByteArray payload(128000, 'A');
    payload.replace(0, 2, "Qq");
    const auto frames = encodeMessage(payload, Direction::DeviceToHost);
    FrameDecoder decoder;
    DecodeBatch all;
    for(int index = 0; index < frames.size(); ++index)
    {
        appendBatch(&all, decoder.feed(frames.at(index), index));
        QVERIFY(decoder.bufferedBytes() <= 65536);
        QVERIFY(decoder.assemblyBytes() <= 128000);
    }
    QCOMPARE(all.messages, QList<QByteArray>({payload}));
    QVERIFY(all.errors.isEmpty());
    QCOMPARE(decoder.bufferedBytes(), 0);
    QCOMPARE(decoder.assemblyBytes(), 0);
    const auto oversized = decoder.feed(QByteArray(65537, 'X'), 100);
    QVERIFY(!oversized.errors.isEmpty());
    QVERIFY(decoder.bufferedBytes() <= 65536);
    QCOMPARE(decoder.feed(kAdFrame, 101).messages, QList<QByteArray>({kAdMessage}));
}

void ProtocolTest::repeatedMessagesDoNotGrow()
{
    FrameDecoder decoder;
    QElapsedTimer timer;
    timer.start();
    for(int sequence = 0; sequence < 10000; ++sequence)
    {
        const auto result = decoder.feed(kAdFrame, sequence);
        QCOMPARE(result.messages, QList<QByteArray>({kAdMessage}));
        QVERIFY(result.errors.isEmpty());
        QCOMPARE(decoder.bufferedBytes(), 0);
        QCOMPARE(decoder.assemblyBytes(), 0);
    }
    qInfo() << "SHORT codec repetition: messages=10000 inputBytes=" << 10000 * kAdFrame.size()
            << "elapsedMs=" << timer.elapsed() << "remainingWireBytes=" << decoder.bufferedBytes()
            << "remainingAssemblyBytes=" << decoder.assemblyBytes();
}

void ProtocolTest::defaultStatusJsonIsSafe()
{
    const StatusSnapshot status;
    const QJsonObject json = status.toJson();
    QCOMPARE(json.value("main_status").toInt(), -1);
    QCOMPARE(json.value("main_status_name").toString(), QString("unknown"));
    QCOMPARE(json.value("declared_general_count").toInt(), 0);
    QVERIFY(json.value("general").toObject().isEmpty());
    QVERIFY(json.value("subsystems_raw").toArray().isEmpty());
}

void ProtocolTest::publicStatusJsonBoundsAreSafe()
{
    // This struct is public: callers can construct it without parseStatus.
    StatusSnapshot status;
    status.mainStatus = 999;
    for(int index = 0; index < 30; ++index)
        status.generalFields.append(QString("field-%1").arg(index));
    const QJsonObject json = status.toJson();
    QCOMPARE(json.value("main_status").toInt(), 999);
    QCOMPARE(json.value("main_status_name").toString(), QString("unknown"));
    QCOMPARE(json.value("declared_general_count").toInt(), 30);
    const QJsonObject general = json.value("general").toObject();
    QCOMPARE(general.size(), 25);
    QCOMPARE(general.value("model").toString(), QString("field-0"));
    QCOMPARE(general.value("dual_needle_working").toString(), QString("field-24"));
    for(auto field = general.constBegin(); field != general.constEnd(); ++field)
        QVERIFY(field.value().toString() != QString("field-25"));
}

void ProtocolTest::statusFieldsAndEnums()
{
    StatusSnapshot status;
    QString error;
    QVERIFY2(parseStatus(kStatusMessage, &status, &error), qPrintable(error));
    QCOMPARE(status.model, QString("SIMULATED"));
    QCOMPARE(status.mainStatus, 2);
    QCOMPARE(status.ledStatus, 1);
    QCOMPARE(status.errorWorkType, 0);
    QCOMPARE(status.generalFields.size(), 25);
    QVERIFY(status.subsystems.isEmpty());
    const auto json = status.toJson();
    QCOMPARE(json.value("evidence").toString(), QString("simulated-source-derived"));
    QCOMPARE(json.value("main_status_name").toString(), QString("ready"));
    QCOMPARE(json.value("general").toObject().value("protocol_marker").toString(), QString("LinuxNew"));
    QCOMPARE(json.value("general").toObject().value("reagent_channels").toString(), QString("20"));
    QCOMPARE(json.value("general").toObject().value("test_channels").toString(), QString("18"));
    // Enum positions come from SystemCommon::DMainStatus, not from label order.
    for(int state = 0; state <= 6; ++state)
    {
        const QByteArray payload = QByteArray("Qj[4][SIMULATED][") + QByteArray::number(state) + "][1][0][0]";
        QVERIFY2(parseStatus(payload, &status, &error), qPrintable(error));
        QCOMPARE(status.mainStatus, state);
    }
    qInfo().noquote() << "STATUS fields=" << QJsonDocument(json).toJson(QJsonDocument::Compact);
}

void ProtocolTest::malformedStatus_data()
{
    QTest::addColumn<QByteArray>("message");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("not-status") << kAdMessage;
    QTest::newRow("missing-closing-bracket") << QByteArray("Qj[4][SIMULATED][2][1][0][0");
    QTest::newRow("nested-bracket") << QByteArray("Qj[4][SIM[ULATED][2][1][0][0]");
    QTest::newRow("missing-general-fields") << QByteArray("Qj[25][SIMULATED][2][1][0][0]");
    QTest::newRow("negative-count") << QByteArray("Qj[-4][SIMULATED][2][1][0][0]");
    QTest::newRow("too-many-general-fields") << QByteArray("Qj[26][SIMULATED][2][1][0][0]");
    QTest::newRow("main-status-outside-enum") << QByteArray("Qj[4][SIMULATED][7][1][0][0]");
    QTest::newRow("led-status-outside-enum") << QByteArray("Qj[4][SIMULATED][2][8][0][0]");
    QTest::newRow("error-type-outside-enum") << QByteArray("Qj[4][SIMULATED][2][1][8][0]");
    QTest::newRow("empty-model") << QByteArray("Qj[4][][2][1][0][0]");
    QTest::newRow("nonnumeric-status") << QByteArray("Qj[4][SIMULATED][ready][1][0][0]");
    QTest::newRow("overflow-numeric-status") << QByteArray("Qj[4][SIMULATED][999999999999999][1][0][0]");
    QTest::newRow("missing-subsystem-count") << QByteArray("Qj[4][SIMULATED][2][1][0]");
    QTest::newRow("too-many-subsystems") << QByteArray("Qj[4][SIMULATED][2][1][0][65]");
    QTest::newRow("truncated-subsystem") << QByteArray("Qj[4][SIMULATED][2][1][0][1][999][2][a]");
    QTest::newRow("negative-subsystem-length") << QByteArray("Qj[4][SIMULATED][2][1][0][1][999][-1]");
    QTest::newRow("trailing-unclaimed-field") << QByteArray("Qj[4][SIMULATED][2][1][0][0][extra]");
    QTest::newRow("trailing-unbracketed-byte") << QByteArray("Qj[4][SIMULATED][2][1][0][0]x");
    QByteArray badNumber = kStatusMessage;
    badNumber.replace("[20][18]", "[twenty][18]");
    QTest::newRow("nonnumeric-channel-count") << badNumber;
    QTest::newRow("oversized-field") << (QByteArray("Qj[4][") + QByteArray(1025, 'A') + "][2][1][0][0]");
}

void ProtocolTest::malformedStatus()
{
    QFETCH(QByteArray, message);
    StatusSnapshot status;
    QString error;
    QVERIFY(parseStatus(kStatusMessage, &status, &error));
    const QJsonObject previous = status.toJson();
    QVERIFY(!parseStatus(message, &status, &error));
    QVERIFY(!error.isEmpty());
    // Invalid input must never partially overwrite an existing status snapshot.
    QCOMPARE(status.toJson(), previous);
    qInfo().noquote() << "REJECT status PAYLOAD=" << message.left(200)
                      << "ERROR=" << error;
}

void ProtocolTest::unknownSubsystemKeepsRawValues()
{
    const QByteArray message("Qj[4][SIMULATED][2][1][0][2][999][3][alpha][][37.5][3][0]");
    StatusSnapshot status;
    QString error;
    QVERIFY2(parseStatus(message, &status, &error), qPrintable(error));
    QCOMPARE(status.subsystems.size(), 2);
    QCOMPARE(status.subsystems.first().id, 999);
    QCOMPARE(status.subsystems.first().fields, QStringList({"alpha", "", "37.5"}));
    QCOMPARE(status.subsystems.last().id, 3);
    QVERIFY(status.subsystems.last().fields.isEmpty());
    qInfo().noquote() << "STATUS unknown-subsystem PAYLOAD=" << message
                      << "RAW=" << QJsonDocument(status.toJson()).toJson(QJsonDocument::Compact);
}

void ProtocolTest::sessionOnlyAcceptsStatus()
{
    StatusSession session;
    const auto rejected = session.queryStatus(0);
    QVERIFY(rejected.transmitted.isEmpty());
    QVERIFY(!rejected.notices.isEmpty());
    QCOMPARE(session.linkState(), QString("disconnected"));
    session.open(1);
    QCOMPARE(session.linkState(), QString("connected-status-unknown"));
    const auto query = session.queryStatus(2);
    QCOMPARE(query.transmitted, QList<QByteArray>({kQueryFrame}));
    QCOMPARE(session.linkState(), QString("awaiting-status"));
    const auto duplicateQuery = session.queryStatus(3);
    QVERIFY(duplicateQuery.transmitted.isEmpty());
    QVERIFY(!duplicateQuery.notices.isEmpty());
    const auto unrelated = session.receive(kAcknowledgement + kAdFrame, 4);
    QVERIFY(unrelated.statuses.isEmpty());
    QVERIFY(unrelated.transmitted.isEmpty());
    QVERIFY(!unrelated.notices.isEmpty());
    QVERIFY(!session.hasFreshStatus());
    QCOMPARE(session.linkState(), QString("awaiting-status"));
    const auto valid = session.receive(kStatusFrame, 5);
    QCOMPARE(valid.statuses.size(), 1);
    QVERIFY(valid.transmitted.isEmpty());
    QCOMPARE(session.linkState(), QString("has-simulated-status"));
    QVERIFY(session.hasFreshStatus());
}

void ProtocolTest::sessionDuplicateStatus()
{
    StatusSession session;
    session.open(0);
    QCOMPARE(session.receive(kStatusFrame, 1).statuses.size(), 1);
    const auto repeated = session.receive(kStatusFrame, 2);
    QCOMPARE(repeated.statuses.size(), 1);
    QVERIFY(repeated.transmitted.isEmpty());
    QVERIFY(repeated.notices.join(' ').contains("status.repeated"));
    QVERIFY(repeated.notices.join(' ').contains("status.uncorrelated"));
    QVERIFY(session.hasFreshStatus());
    // Repeated identical complete states are fresh observations, not packet duplicates.
    QCOMPARE(session.advance(200001).notices.size(), 0);
    session.advance(200002);
    QCOMPARE(session.linkState(), QString("disconnected"));
}

void ProtocolTest::sessionRetryAndTimeout()
{
    StatusSession session(1000, 1);
    session.open(0);
    QCOMPARE(session.queryStatus(1).transmitted, QList<QByteArray>({kQueryFrame}));
    QVERIFY(session.advance(1000).transmitted.isEmpty());
    const auto retry = session.advance(1001);
    QCOMPARE(retry.transmitted, QList<QByteArray>({kQueryFrame}));
    QVERIFY(retry.statuses.isEmpty());
    QCOMPARE(session.linkState(), QString("awaiting-status"));
    const auto timeout = session.advance(2001);
    QVERIFY(timeout.transmitted.isEmpty());
    QVERIFY(!timeout.notices.isEmpty());
    QCOMPARE(session.linkState(), QString("status-timeout"));
    QVERIFY(!session.hasFreshStatus());
    QVERIFY(session.advance(3001).transmitted.isEmpty());
    const auto late = session.receive(kStatusFrame, 3002);
    QCOMPARE(late.statuses.size(), 1);
    QVERIFY(late.transmitted.isEmpty());
    QVERIFY(late.notices.join(' ').contains("status.uncorrelated"));
    QCOMPARE(session.linkState(), QString("has-simulated-status"));
    qInfo().noquote() << "SESSION only-read retry TX=" << retry.transmitted.first().toHex(' ')
                      << "RETRY=" << retry.notices.join("; ")
                      << "EXHAUSTED=" << timeout.notices.join("; ");
}

void ProtocolTest::sessionDisconnectDropsOldBytes()
{
    StatusSession session;
    session.open(0);
    session.receive(kStatusFrame.left(20), 1);
    QVERIFY(session.bufferedBytes() > 0);
    session.disconnect(2);
    QCOMPARE(session.bufferedBytes(), 0);
    QVERIFY(!session.hasFreshStatus());
    QVERIFY(session.receive(kStatusFrame.mid(20), 3).statuses.isEmpty());
    session.open(4);
    QVERIFY(session.receive(kStatusFrame.mid(20), 5).statuses.isEmpty());
    QCOMPARE(session.receive(kStatusFrame, 6).statuses.size(), 1);
    session.receive(kPartZero, 7);
    QVERIFY(session.assemblyBytes() > 0);
    session.disconnect(8);
    QCOMPARE(session.assemblyBytes(), 0);
    session.open(9);
    QVERIFY(session.receive(kPartOne, 10).statuses.isEmpty());
    QVERIFY(!session.hasFreshStatus());
}

void ProtocolTest::sessionReceiveAtDeadline()
{
    StatusSession session(1000, 1);
    session.open(0);
    session.queryStatus(1);
    const auto atDeadline = session.receive(kStatusFrame, 1001);
    QCOMPARE(atDeadline.transmitted, QList<QByteArray>({kQueryFrame}));
    QCOMPARE(atDeadline.statuses.size(), 1);
    QCOMPARE(session.linkState(), QString("has-simulated-status"));
    // A Qj arriving at the silence boundary cannot revive a disconnected session.
    const auto afterSilence = session.receive(kStatusFrame, 201001);
    QVERIFY(afterSilence.statuses.isEmpty());
    QCOMPARE(session.linkState(), QString("disconnected"));
    QVERIFY(!session.hasFreshStatus());
}

void ProtocolTest::invalidStatusNeverRefreshesSession()
{
    StatusSession session;
    session.open(0);
    QCOMPARE(session.receive(kStatusFrame, 1).statuses.size(), 1);
    const auto invalid = encodeMessage("Qj[4][SIMULATED][99][1][0][0]", Direction::DeviceToHost);
    QCOMPARE(invalid.size(), 1);
    const auto rejected = session.receive(invalid.first(), 199999);
    QVERIFY(rejected.statuses.isEmpty());
    QVERIFY(!rejected.notices.isEmpty());
    QVERIFY(session.hasFreshStatus());
    session.advance(200001);
    QCOMPARE(session.linkState(), QString("disconnected"));
    QVERIFY(!session.hasFreshStatus());
}

void ProtocolTest::sessionSilenceAndTimeReversal()
{
    StatusSession session;
    session.open(0);
    session.receive(kAdFrame, 199999);
    QVERIFY(!session.hasFreshStatus());
    session.advance(200000);
    QCOMPARE(session.linkState(), QString("disconnected"));
    session.open(200001);
    session.receive(kStatusFrame, 200002);
    QVERIFY(session.hasFreshStatus());
    const auto reversed = session.advance(200001);
    QVERIFY(!reversed.notices.isEmpty());
    QCOMPARE(session.linkState(), QString("disconnected"));
    QVERIFY(!session.hasFreshStatus());
    QCOMPARE(session.bufferedBytes(), 0);
    const auto negative = session.open(-1);
    QVERIFY(!negative.notices.isEmpty());
    QCOMPARE(session.linkState(), QString("disconnected"));
}

void ProtocolTest::replayFixture()
{
    // QFINDTESTDATA resolves from this source location, independent of build CWD.
    const QString path = QFINDTESTDATA("fixtures/protocol_replay.json");
    QVERIFY2(!path.isEmpty(), "The committed synthetic replay fixture must be present");
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(document.isObject());
    const QJsonObject fixture = document.object();
    QCOMPARE(fixture.value("schema").toString(), QString("sn100-protocol-replay-v1"));
    QCOMPARE(fixture.value("evidence").toString(), QString("simulated-source-derived"));
    StatusSession session;
    int receivedStatuses = 0;
    int sentQueries = 0;
    qint64 previousTime = -1;
    const QJsonArray events = fixture.value("events").toArray();
    QVERIFY(!events.isEmpty());
    for(int index = 0; index < events.size(); ++index)
    {
        const QJsonObject event = events.at(index).toObject();
        const qint64 at = qint64(event.value("at_ms").toDouble());
        QVERIFY(at >= previousTime);
        previousTime = at;
        const QString action = event.value("action").toString();
        SessionEvents output;
        if(action == "connect") output = session.open(at);
        else if(action == "query-status") output = session.queryStatus(at);
        else if(action == "rx") output = session.receive(QByteArray::fromHex(event.value("hex").toString().toLatin1()), at);
        else if(action == "advance") output = session.advance(at);
        else if(action == "disconnect") output = session.disconnect(at);
        else QFAIL(qPrintable(QString("Unknown fixture action at event %1: %2").arg(index).arg(action)));
        if(event.contains("expect_link_state"))
            QCOMPARE(session.linkState(), event.value("expect_link_state").toString());
        if(event.contains("expect_status_count"))
            QCOMPARE(output.statuses.size(), event.value("expect_status_count").toInt());
        if(event.contains("expect_tx_count"))
            QCOMPARE(output.transmitted.size(), event.value("expect_tx_count").toInt());
        if(event.contains("expect_main_status"))
        {
            QVERIFY(!output.statuses.isEmpty());
            QCOMPARE(output.statuses.last().mainStatus, event.value("expect_main_status").toInt());
        }
        for(const QByteArray &frame : output.transmitted)
            QCOMPARE(frame, kQueryFrame);
        receivedStatuses += output.statuses.size();
        sentQueries += output.transmitted.size();
        qInfo().noquote() << "REPLAY event=" << index << "at_ms=" << at << "action=" << action
                          << "RX_HEX=" << event.value("hex").toString()
                          << "TX_COUNT=" << output.transmitted.size()
                          << "STATUS_COUNT=" << output.statuses.size() << "STATE=" << session.linkState()
                          << "NOTICES=" << output.notices.join("; ");
    }
    QVERIFY(receivedStatuses >= 6);
    QVERIFY(sentQueries >= 5);
}

QTEST_GUILESS_MAIN(ProtocolTest)
#include "protocol_test.moc"
