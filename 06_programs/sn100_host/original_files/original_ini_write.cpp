#include "original_ini_write.h"
#include "original_business_database.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextCodec>
#include <algorithm>

namespace
{
bool fail(QString *message, const QString &text)
{
    *message = text;
    return false;
}

bool safeFile(const QString &root, const QString &relative, QString *path, QString *message)
{
    if (!OriginalBusiness::workingRoot(root, message)) return false;
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\') ||
        relative.split('/').contains("..") || relative.split('/').contains(".") ||
        !relative.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive))
        return fail(message, QStringLiteral("原INI必须是当前工作根内明确的相对INI路径。"));
    *path = QDir(root).filePath(relative);
    for (QFileInfo entry(*path); ; entry.setFile(entry.absolutePath()))
    {
        if (entry.isSymLink()) return fail(message, QStringLiteral("原INI路径含链接，未读取或修改。"));
        if (entry.absoluteFilePath() == entry.absolutePath()) break;
    }
    if (!QFileInfo(*path).isFile() || !QFileInfo(*path).canonicalFilePath().startsWith(QFileInfo(root).canonicalFilePath() + '/'))
        return fail(message, QStringLiteral("原INI不存在或不在当前工作根，未创建替代文件。"));
    return true;
}

bool bytesAt(const QString &path, QByteArray *bytes, QString *message)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return fail(message, QStringLiteral("无法完整读取原INI，或文件超过1MiB限制。"));
    *bytes = file.readAll();
    if (file.error() != QFile::NoError)
        return fail(message, QStringLiteral("原INI读取未完成。"));
    QTextCodec::ConverterState state;
    QTextCodec::codecForName("UTF-8")->toUnicode(bytes->constData(), bytes->size(), &state);
    if (state.invalidChars || state.remainingChars || bytes->contains('\0'))
        return fail(message, QStringLiteral("原INI含无效UTF-8或空字符，未猜测替换。"));
    return true;
}

QString version(const QString &path, const QByteArray &bytes)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QFileInfo(path).canonicalFilePath().toUtf8());
    hash.addData("\0", 1);
    hash.addData(bytes);
    return QString::fromLatin1(hash.result().toHex());
}

bool encodedValue(const QVariant &value, QByteArray *encoded, QString *message)
{
    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/sn100-ini-value-XXXXXX.ini"));
    if (!file.open() || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner))
        return fail(message, QStringLiteral("无法创建私有INI编码副本。"));
    file.close();
    {
        QSettings settings(file.fileName(), QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        settings.setValue(QStringLiteral("owned/value"), value);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return fail(message, QStringLiteral("本次INI键值编码失败，原文件未变。"));
    }
    QFile input(file.fileName());
    if (!input.open(QIODevice::ReadOnly)) return fail(message, QStringLiteral("无法回读本次INI编码值。"));
    for (QByteArray line : input.readAll().split('\n'))
    {
        if (line.endsWith('\r')) line.chop(1);
        if (line.startsWith("value="))
        {
            *encoded = line.mid(6);
            return true;
        }
    }
    return fail(message, QStringLiteral("本次INI编码结果缺少目标值。"));
}

struct Line
{
    int start = 0;
    int bodyEnd = 0;
    int end = 0;
};
struct Position
{
    int begin = 0;
    int length = 0;
};
struct Edit
{
    int begin = 0;
    int length = 0;
    QByteArray replacement;
};

// 原文件中未知的引号/续行只穿过，不重新序列化。目标键为续行时明确拒绝猜测。
bool positions(const QByteArray &bytes, const QStringList &targets, QMap<QString, Position> *values,
               QMap<QString, int> *sectionEnds, QMap<QString, int> *sectionCounts, QString *message)
{
    QString section;
    bool inQuotes = false, continued = false;
    int previousSectionStart = -1;
    for (int start = 0; start < bytes.size(); )
    {
        const int newline = bytes.indexOf('\n', start);
        Line line{start, newline < 0 ? bytes.size() : newline, newline < 0 ? bytes.size() : newline + 1};
        if (line.bodyEnd > start && bytes[line.bodyEnd - 1] == '\r') --line.bodyEnd;
        int first = start;
        if (!start && bytes.startsWith("\xef\xbb\xbf")) first += 3;
        while (first < line.bodyEnd && (bytes[first] == ' ' || bytes[first] == '\t')) ++first;
        const bool fresh = !inQuotes && !continued;
        if (fresh && (first == line.bodyEnd || bytes[first] == '#' || bytes[first] == ';'))
        {
            start = line.end;
            continue;
        }
        QByteArray trimmed = bytes.mid(first, line.bodyEnd - first).trimmed();
        const int closing = trimmed.indexOf(']');
        if (fresh && trimmed.startsWith('[') && closing > 0)
        {
            if (previousSectionStart >= 0) sectionEnds->insert(section, line.start);
            section = QString::fromUtf8(trimmed.mid(1, closing - 1).trimmed());
            (*sectionCounts)[section] += 1;
            previousSectionStart = start;
            start = line.end;
            continue;
        }
        const int equal = fresh ? bytes.indexOf('=', first) : -1;
        const bool assignment = equal >= first && equal < line.bodyEnd;
        const QString key = assignment ? section + '/' + QString::fromUtf8(bytes.mid(first, equal - first).trimmed()) : QString();
        const int valueStart = assignment ? equal + 1 : first;
        int comment = -1;
        for (int index = valueStart; index < line.bodyEnd; ++index)
        {
            if (bytes[index] == '\\') { ++index; continue; }
            if (bytes[index] == '"') inQuotes = !inQuotes;
            if (!inQuotes && bytes[index] == ';') { comment = index; break; }
        }
        int slashes = 0;
        if (comment < 0)
            for (int index = line.bodyEnd - 1; index >= valueStart && bytes[index] == '\\'; --index) ++slashes;
        continued = slashes % 2 != 0;
        if (assignment && targets.contains(key))
        {
            if (values->contains(key)) return fail(message, QStringLiteral("原INI目标键重复，未猜测覆盖其中一份。"));
            if (continued || inQuotes) return fail(message, QStringLiteral("目标INI键采用多行值，未将它误改成单行。"));
            int begin = valueStart, end = comment < 0 ? line.bodyEnd : comment;
            while (begin < end && (bytes[begin] == ' ' || bytes[begin] == '\t')) ++begin;
            while (end > begin && (bytes[end - 1] == ' ' || bytes[end - 1] == '\t')) --end;
            values->insert(key, {begin, end - begin});
        }
        start = line.end;
    }
    if (previousSectionStart >= 0) sectionEnds->insert(section, bytes.size());
    return true;
}

bool rewrite(const QByteArray &before, const QStringList &existingKeys,
             const QMap<QString, QVariant> &changes, QByteArray *after, QString *message)
{
    QMap<QString, Position> values;
    QMap<QString, int> sectionEnds, sectionCounts;
    if (!positions(before, changes.keys(), &values, &sectionEnds, &sectionCounts, message)) return false;
    QList<Edit> edits;
    QMap<int, QByteArray> insertions;
    QMap<QString, QByteArray> missingSections;
    const QByteArray newline = before.contains("\r\n") ? QByteArray("\r\n") : QByteArray("\n");
    for (auto it = changes.cbegin(); it != changes.cend(); ++it)
    {
        QByteArray encoded;
        if (!encodedValue(it.value(), &encoded, message)) return false;
        if (values.contains(it.key()))
        {
            const Position position = values.value(it.key());
            edits.append({position.begin, position.length, encoded});
            continue;
        }
        if (existingKeys.contains(it.key()))
            return fail(message, QStringLiteral("原INI键含未能明确定位的转义或多行格式，未新增重复键。"));
        const int slash = it.key().indexOf('/');
        if (slash <= 0 || slash != it.key().lastIndexOf('/'))
            return fail(message, QStringLiteral("新INI键必须明确提供原分组与键名。"));
        QString section = it.key().left(slash);
        QString existingSection;
        for (const QString &candidate : sectionCounts.keys())
        {
            if (candidate.compare(section, Qt::CaseInsensitive) != 0) continue;
            if (!existingSection.isEmpty())
                return fail(message, QStringLiteral("原INI分组有多个大小写变体，未猜测新键保存位置。"));
            existingSection = candidate;
        }
        if (!existingSection.isEmpty()) section = existingSection;
        const QByteArray line = it.key().mid(slash + 1).toUtf8() + '=' + encoded + newline;
        if (sectionCounts.value(section) > 1)
            return fail(message, QStringLiteral("原INI分组重复，未猜测新键插入位置。"));
        if (!sectionCounts.contains(section))
            for (const QString &existing : existingKeys)
                if (existing.startsWith(section + '/'))
                    return fail(message, QStringLiteral("原INI分组采用未能定位的转义名称，未重复新增分组。"));
        if (sectionEnds.contains(section)) insertions[sectionEnds.value(section)] += line;
        else missingSections[section] += line;
    }
    for (auto it = missingSections.cbegin(); it != missingSections.cend(); ++it)
        insertions[before.size()] += '[' + it.key().toUtf8() + ']' + newline + it.value();
    for (auto it = insertions.cbegin(); it != insertions.cend(); ++it)
    {
        QByteArray value = it.value();
        if (it.key() == before.size() && !before.isEmpty() && !before.endsWith('\n')) value.prepend(newline);
        edits.append({it.key(), 0, value});
    }
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.begin > b.begin; });
    *after = before;
    for (const Edit &edit : edits) after->replace(edit.begin, edit.length, edit.replacement);
    return true;
}
}

bool OriginalIni::readVersioned(const QString &root, const QString &relativeFile, Snapshot *snapshot, QString *message)
{
    if (!snapshot || !message) return false;
    *snapshot = Snapshot();
    QString path;
    if (!safeFile(root, relativeFile, &path, message) || !bytesAt(path, &snapshot->bytes, message) ||
        !read(path, &snapshot->parsed, message)) return false;
    QByteArray after;
    if (!bytesAt(path, &after, message) || after != snapshot->bytes)
        return fail(message, QStringLiteral("读取原INI期间文件发生变化，请重试。"));
    snapshot->fingerprint = version(path, snapshot->bytes);
    return true;
}

bool OriginalIni::patch(const QString &root, const QString &relativeFile, const QString &expectedFingerprint,
                        const QMap<QString, QVariant> &requested, const QStringList &ownedKeys,
                        Snapshot *written, QString *message)
{
    if (!written || !message) return false;
    *written = Snapshot();
    QString path;
    if (!safeFile(root, relativeFile, &path, message)) return false;
    QLockFile lock(path + QStringLiteral(".sn100-original-config.lock"));
    if (!lock.tryLock(0)) return fail(message, QStringLiteral("原配置正由其他操作维护，请保留草稿后重试。"));
    Snapshot before;
    if (!readVersioned(root, relativeFile, &before, message)) return false;
    if (expectedFingerprint.isEmpty() || before.fingerprint != expectedFingerprint)
        return fail(message, QStringLiteral("原INI读取版本已变化，未覆盖其他操作；请重新读取核对。"));
    QMap<QString, QVariant> changes;
    for (auto it = requested.cbegin(); it != requested.cend(); ++it)
    {
        if (!ownedKeys.contains(it.key())) return fail(message, QStringLiteral("本次写入包含不属于该设置页的键，未保存。"));
        QString actualKey;
        if (!resolveKey(before.parsed, it.key(), &actualKey, message)) return false;
        if (actualKey.isEmpty()) actualKey = it.key();
        QVariant value = it.value();
        if (before.parsed.values.contains(actualKey))
        {
            const QVariant old = before.parsed.values.value(actualKey);
            if (old.userType() != value.userType() && !value.convert(old.userType()))
                return fail(message, QStringLiteral("本次值不能保持原INI键类型，未保存。"));
            if (old.userType() == value.userType() && old == value) continue;
        }
        changes.insert(actualKey, value);
    }
    if (changes.isEmpty()) { *written = before; return true; }
    QByteArray rewritten;
    if (!rewrite(before.bytes, before.parsed.values.keys(), changes, &rewritten, message)) return false;
    QTemporaryFile verify(QDir::tempPath() + QStringLiteral("/sn100-ini-patch-XXXXXX.ini"));
    if (!verify.open() || !verify.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        verify.write(rewritten) != rewritten.size() || !verify.flush())
        return fail(message, QStringLiteral("无法准备原INI保存前的私有回读副本。"));
    Settings parsed;
    if (!read(verify.fileName(), &parsed, message)) return false;
    auto expected = before.parsed.values;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) expected.insert(it.key(), it.value());
    if (expected != parsed.values)
        return fail(message, QStringLiteral("保存前回读与原键类型或值不一致，未改原文件。"));
    QByteArray current;
    if (!safeFile(root, relativeFile, &path, message) || !bytesAt(path, &current, message) || current != before.bytes)
        return fail(message, QStringLiteral("原INI提交前发生变化，未覆盖。"));
    const auto permissions = QFileInfo(path).permissions();
    QSaveFile destination(path);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly) || !destination.setPermissions(permissions) ||
        destination.write(rewritten) != rewritten.size())
        return fail(message, QStringLiteral("原INI临时提交失败，原文件与草稿保留。"));
    if (!bytesAt(path, &current, message) || current != before.bytes)
    {
        destination.cancelWriting();
        return fail(message, QStringLiteral("原INI原子替换前发生变化，未覆盖。"));
    }
    if (!destination.commit()) return fail(message, QStringLiteral("原INI原子提交失败，原文件与草稿保留。"));
    if (!readVersioned(root, relativeFile, written, message) || written->bytes != rewritten || written->parsed.values != expected)
        return fail(message, QStringLiteral("原INI已提交，但提交后回读出现差异，请重新读取确认；不能推定已撤销。"));
    return true;
}
