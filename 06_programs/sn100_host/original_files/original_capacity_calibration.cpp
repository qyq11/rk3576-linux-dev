#include "original_capacity_calibration.h"
#include "original_business_database.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <cmath>

namespace OriginalRuntime
{
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
QJsonValue valueAt(const QJsonObject &object, const QStringList &path)
{
    QJsonValue result(object);
    for (const QString &part : path)
    {
        if (!result.isObject() || !result.toObject().contains(part)) return QJsonValue(QJsonValue::Undefined);
        result = result.toObject().value(part);
    }
    return result;
}
bool replaceAt(QJsonObject *object, const QStringList &path, const QJsonValue &value)
{
    if (path.isEmpty() || !object->contains(path.first())) return false;
    if (path.size() == 1) { object->insert(path.first(), value); return true; }
    if (!object->value(path.first()).isObject()) return false;
    QJsonObject child = object->value(path.first()).toObject();
    if (!replaceAt(&child, path.mid(1), value)) return false;
    object->insert(path.first(), child);
    return true;
}
bool validFile(const QString &root, const QString &relative, QString *path, QString *message)
{
    if (!OriginalBusiness::workingRoot(root, message)) return false;
    if (relative != "calibrationFile/ReagentAmount_450.json" && relative != "calibrationFile/ReagentAmount_450 - TJ.json")
        return fail(message, QStringLiteral("请选择已提供的原容量校准文件；不根据文件名猜测实际机型。"));
    *path = QDir(root).filePath(relative);
    for (QFileInfo current(*path); ; current.setFile(current.absolutePath()))
    {
        if (current.isSymLink()) return fail(message, QStringLiteral("容量校准路径不能经过符号链接。"));
        if (current.absoluteFilePath() == current.absolutePath()) break;
    }
    if (!QFileInfo(*path).isFile()) return fail(message, QStringLiteral("原容量校准文件不存在。"));
    return true;
}
bool readObject(const QString &path, QJsonObject *result, QString *fingerprint, QString *message)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024)
        return fail(message, QStringLiteral("原容量校准文件无法读取或超过8MB。"));
    const QByteArray bytes = file.readAll();
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("CONFIG").isObject())
        return fail(message, QStringLiteral("原容量校准JSON结构无效，未采用不完整参数。"));
    *result = document.object();
    *fingerprint = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return true;
}
bool numeric(const QJsonValue &value, double *result)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    *result = value.toDouble();
    return true;
}
QString bottleName(const QString &value)
{
    static const QMap<QString, QString> aliases{{"1", "7mL"}, {"2", "10mL"}, {"3", "15mL"}, {"4", "30mL"}, {"5", "50mL"}};
    return aliases.value(value, value);
}
QString areaName(const QString &value)
{
    if (value == "0" || value == "s_area" || value == "Area_S1") return "Area_S1";
    if (value == "1" || value == "r_area" || value == "Area_R1") return "Area_R1";
    return QString();
}
bool updateBaseAndNeedles(QJsonObject *document, const CapacityMeasurement &point, QString *message)
{
    const QStringList prefix{point.area, point.subArea};
    const auto face = valueAt(*document, prefix + QStringList{"FACE", point.bottle}).toObject();
    const auto volume = valueAt(*document, prefix + QStringList{"VOL", point.bottle}).toObject();
    if (face.isEmpty() || volume.isEmpty()) return fail(message, QStringLiteral("面型或容量原校准点不齐，不能更新BASE。"));
    QStringList holes, volumes;
    for (const auto &matrix : {face, volume})
        for (auto hole = matrix.begin(); hole != matrix.end(); ++hole)
        {
            if (!hole.value().isObject()) return fail(message, QStringLiteral("校准点矩阵不是对象。"));
            if (!holes.contains(hole.key())) holes << hole.key();
            const auto points = hole.value().toObject();
            for (auto amount = points.begin(); amount != points.end(); ++amount)
                if (!volumes.contains(amount.key())) volumes << amount.key();
        }
    QJsonObject base;
    for (const auto &hole : holes)
    {
        QJsonObject row;
        const QJsonObject currentFace = face.value(hole).toObject();
        if (currentFace.isEmpty()) return fail(message, QStringLiteral("容量校准引用的孔位没有原面型点。"));
        for (const auto &amount : volumes)
        {
            QString baseHole;
            for (auto it = volume.begin(); it != volume.end(); ++it)
                if (it.value().toObject().contains(amount)) { baseHole = it.key(); break; }
            double height = 0;
            if (baseHole.isEmpty())
            {
                if (!numeric(currentFace.value(amount), &height))
                    return fail(message, QStringLiteral("原面型高度不完整，未写入猜测的0。"));
            }
            else
            {
                const auto baseFace = face.value(baseHole).toObject();
                double baseHeight = 0, baseVolumeHeight = 0, currentHeight = 0;
                if (baseFace.isEmpty() || !numeric(baseFace.begin().value(), &baseHeight) ||
                    !numeric(volume.value(baseHole).toObject().value(amount), &baseVolumeHeight) ||
                    !numeric(currentFace.begin().value(), &currentHeight))
                    return fail(message, QStringLiteral("原面型/容量点不是有限高度。"));
                height = currentHeight + baseVolumeHeight - baseHeight;
            }
            row.insert(amount, std::round(height * 100.0) / 100.0);
        }
        base.insert(hole, row);
    }
    if (!replaceAt(document, prefix + QStringList{"BASE", point.bottle}, base))
        return fail(message, QStringLiteral("原BASE节点不存在，未擅自新建结构。"));
    const auto area = valueAt(*document, prefix).toObject();
    for (auto mode = area.begin(); mode != area.end(); ++mode)
    {
        if (mode.key() == "FACE" || mode.key() == "VOL" || mode.key() == "BASE") continue;
        const auto bottle = mode.value().toObject().value(point.bottle).toObject();
        if (bottle.isEmpty()) continue;
        const auto hole = bottle.begin();
        const auto mark = hole.value().toObject();
        if (mark.isEmpty()) return fail(message, QStringLiteral("原针补偿点不完整。"));
        const auto measured = base.value(hole.key()).toObject().value(mark.begin().key());
        double number = 0;
        if (!numeric(measured, &number) ||
            !replaceAt(document, prefix + QStringList{mode.key(), point.bottle, hole.key(), mark.begin().key()}, number))
            return fail(message, QStringLiteral("原针补偿引用的BASE点不存在。"));
    }
    return true;
}
}

bool loadCapacityCalibration(const QString &root, const QString &relativeFile, CapacityCalibration *result, QString *message)
{
    if (!result) return fail(message, QStringLiteral("容量校准输出为空。"));
    QString path;
    CapacityCalibration candidate;
    if (!validFile(root, relativeFile, &path, message) ||
        !readObject(path, &candidate.original, &candidate.fingerprint, message)) return false;
    candidate.root = QFileInfo(root).canonicalFilePath();
    candidate.relativeFile = relativeFile;
    *result = candidate;
    return true;
}
QVector<CapacityMeasurement> CapacityCalibration::configuredMeasurements() const
{
    QVector<CapacityMeasurement> result;
    const auto configuration = original.value("CONFIG").toObject();
    for (auto mode = configuration.begin(); mode != configuration.end(); ++mode)
    {
        const auto bottles = mode.value().toObject();
        for (auto bottle = bottles.begin(); bottle != bottles.end(); ++bottle)
        {
            const auto areas = bottle.value().toObject();
            for (auto area = areas.begin(); area != areas.end(); ++area)
            {
                const QString subArea = area.value().toString();
                const auto holes = valueAt(original, {area.key(), subArea, mode.key(), bottle.key()}).toObject();
                for (auto hole = holes.begin(); hole != holes.end(); ++hole)
                {
                    const auto marks = hole.value().toObject();
                    for (auto mark = marks.begin(); mark != marks.end(); ++mark)
                    {
                        double height = 0;
                        if (numeric(mark.value(), &height))
                            result.append({area.key(), subArea, mode.key(), bottle.key(), hole.key(), mark.key(), height});
                    }
                }
            }
        }
    }
    return result;
}
bool CapacityCalibration::volumeForHeight(const QString &hole, int needle, const QString &area,
                                         const QString &bottle, double maximum, int measured,
                                         double *result, QString *message) const
{
    if (!result || measured < 0 || measured >= 0xff00 || !std::isfinite(maximum) || maximum <= 0)
        return fail(message, QStringLiteral("探测高度或最大容量无效。"));
    const QString region = areaName(area), container = bottleName(bottle);
    const QString needleName = needle == 1 ? QStringLiteral("N1") : needle == 2 ? QStringLiteral("N2") : needle == 3 ? QStringLiteral("S1") : QString();
    if (region.isEmpty() || needleName.isEmpty()) return fail(message, QStringLiteral("原针号或探测区域无效，未猜测其他区域。"));
    const auto subAreas = original.value(region).toObject();
    QString subArea;
    QJsonObject matrix;
    for (auto it = subAreas.begin(); it != subAreas.end(); ++it)
    {
        const auto candidate = valueAt(original, {region, it.key(), "BASE", container, hole});
        if (candidate.isObject()) { subArea = it.key(); matrix = candidate.toObject(); break; }
    }
    if (subArea.isEmpty() || matrix.isEmpty()) return fail(message, QStringLiteral("此孔位/瓶型没有原容量矩阵。"));
    const auto needleOffsets = valueAt(original, {region, subArea, needleName, container}).toObject();
    if (needleOffsets.isEmpty() || needleOffsets.begin().value().toObject().isEmpty())
        return fail(message, QStringLiteral("此针没有原校准偏移点。"));
    const auto needleHole = needleOffsets.begin();
    const auto needleMark = needleHole.value().toObject();
    double offsetNeedle = 0, offsetBase = 0;
    if (!numeric(needleMark.begin().value(), &offsetNeedle) ||
        !numeric(valueAt(original, {region, subArea, "BASE", container, needleHole.key(), needleMark.begin().key()}), &offsetBase))
        return fail(message, QStringLiteral("原针校准偏移无法对应BASE点。"));
    double height = measured / 100.0;
    if (height > 0) height += offsetBase - offsetNeedle;
    QVector<QPair<double, double>> points{{0.0, 0.0}};
    for (auto it = matrix.begin(); it != matrix.end(); ++it)
    {
        bool amountOk = false;
        const double amount = it.key().toDouble(&amountOk);
        double level = 0;
        if (!amountOk || !std::isfinite(amount) || !numeric(it.value(), &level))
            return fail(message, QStringLiteral("原容量矩阵含无效数字。"));
        points.append({level, amount});
    }
    std::stable_sort(points.begin(), points.end(), [](const QPair<double, double> &a, const QPair<double, double> &b) { return a.first < b.first; });
    int segment = points.size() - 2;
    if (height < points.first().first) segment = 0;
    else
        for (int index = 0; index + 1 < points.size(); ++index)
            if (height >= points[index].first && height <= points[index + 1].first) { segment = index; break; }
    const auto left = points[segment], right = points[segment + 1];
    // 原manage_reagentCarlibration.cpp按高度排序、分段直线，并保留分母的1e-12。
    const double slope = (right.second - left.second) / (right.first - left.first + 1e-12);
    const double volume = slope * height + left.second - slope * left.first;
    if (!std::isfinite(volume)) return fail(message, QStringLiteral("容量换算结果不是有限数。"));
    *result = qMax(0.0, qMin(maximum, volume));
    return true;
}
bool saveCapacityMeasurement(const CapacityCalibration &snapshot, const CapacityMeasurement &point,
                             const QVector<int> &measurements, bool completeMode, QString *message)
{
    if (measurements.size() != 3) return fail(message, QStringLiteral("原流程每个校准点需要恰好3次有效高度，未保存半份测量。"));
    double total = 0;
    for (int height : measurements)
    {
        if (height <= 0 || height >= 0xff00) return fail(message, QStringLiteral("校准高度不能为0或无效探测值。"));
        total += height / 100.0;
    }
    bool configured = false;
    for (const auto &candidate : snapshot.configuredMeasurements())
        if (candidate.area == point.area && candidate.subArea == point.subArea && candidate.mode == point.mode &&
            candidate.bottle == point.bottle && candidate.hole == point.hole && candidate.markedMicrolitres == point.markedMicrolitres)
            configured = true;
    if (!configured) return fail(message, QStringLiteral("此校准点不在原CONFIG任务中，未新增任意参数。"));
    QString path;
    if (!validFile(snapshot.root, snapshot.relativeFile, &path, message)) return false;
    QLockFile lock(path + ".sn100-write.lock");
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0)) return fail(message, QStringLiteral("容量校准文件正在写入，请重读后再试。"));
    QJsonObject current;
    QString fingerprint;
    if (!readObject(path, &current, &fingerprint, message) || fingerprint != snapshot.fingerprint || current != snapshot.original)
        return fail(message, QStringLiteral("容量校准文件已变化，请重新读取。"));
    QJsonObject changed = current;
    const double average = std::round(total * 100.0 / 3.0) / 100.0;
    if (!replaceAt(&changed, {point.area, point.subArea, point.mode, point.bottle, point.hole, point.markedMicrolitres}, average))
        return fail(message, QStringLiteral("原校准点不存在，未新增结构。"));
    if (completeMode && (point.mode == "FACE" || point.mode == "VOL") && !updateBaseAndNeedles(&changed, point, message)) return false;
    if (changed == current) return true;
    const QByteArray bytes = QJsonDocument(changed).toJson(QJsonDocument::Indented);
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        output.write(bytes) != bytes.size() || !output.commit())
        return fail(message, QStringLiteral("容量校准文件原子保存失败，原文件未替换。"));
    QJsonObject readback;
    QString readbackFingerprint;
    return readObject(path, &readback, &readbackFingerprint, message) && readback == changed;
}
}
