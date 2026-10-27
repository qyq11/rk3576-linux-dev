#ifndef SN100_LIS_DSR_RAW_H
#define SN100_LIS_DSR_RAW_H

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QStringList>

namespace LisDsrRaw
{
// New LOCAL engineering limits, not clinical/vendor protocol maxima.
constexpr int MaximumFrameBytes = 1024 * 1024;
constexpr int MaximumSegments = 64;
constexpr int MaximumFields = 128; // Includes the segment tag at index 0.
constexpr int MaximumFieldBytes = 65536;
constexpr int MaximumControlBytes = 256;
constexpr int MaximumMessages = 1024;
QString profileName();
QString parseStatus();

struct Segment
{
    QString name;
    QString rawText; // Without the segment terminator; no trim or unescape.
    QStringList fields; // Tag at 0; MSH inserts the actual separator at 1.
};
struct Message
{
    qint64 id = 0; // Actual committed inbox SQL ID, never a test/task ID.
    QString receptionIdentity; // Caller-supplied LOCAL UUID, never sent on wire.
    QString profile;
    QString controlId; // Exact received MSH-10, not generated or normalized.
    QString messageType; // Full received DSR event, not replaced with Q03.
    QString version;
    QString status;
    QDateTime receivedAt; // Local receipt time; does not interpret OBR dates.
    QByteArray frame;
    QList<Segment> segments;
    QMap<QString, QString> sourceFields; // Only present raw fields, e.g. PID.31.
};
struct Summary
{
    qint64 id = 0;
    QString receptionIdentity, profile, controlId, messageType, status;
    QDateTime receivedAt;
    int frameBytes = 0;
};
using Summaries = QList<Summary>;

bool decode(const QString &profile, const QString &expectedControl,
            const QByteArray &frame, Message *message, QString *error);
bool samePayload(const Message &left, const Message &right);
QMap<QString, QString> sourceFields(const QList<Segment> &segments);
}
Q_DECLARE_METATYPE(LisDsrRaw::Message)
Q_DECLARE_METATYPE(LisDsrRaw::Summary)
Q_DECLARE_METATYPE(LisDsrRaw::Summaries)
#endif
