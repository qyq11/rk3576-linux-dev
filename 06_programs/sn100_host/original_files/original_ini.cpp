#include "original_ini.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextCodec>

bool OriginalIni::read(const QString &path, Settings *settings, QString *message)
{
    if (!settings || !message) return false;
    *settings = Settings();
    message->clear();
    for (QFileInfo entry(path); ; entry.setFile(entry.absolutePath()))
    {
        if (!QDir::isAbsolutePath(path) || entry.isSymLink())
        {
            *message = QStringLiteral("原INI必须使用绝对路径，且路径不能经过符号链接。");
            return false;
        }
        if (entry.absoluteFilePath() == entry.absolutePath()) break;
    }
    QFile source(path);
    if (QFileInfo(path).isSymLink() || !source.open(QIODevice::ReadOnly) || source.size() > 1024 * 1024)
    {
        *message = QStringLiteral("无法读取原INI文件，或文件超过1MB限制。");
        return false;
    }
    QByteArray bytes = source.readAll();
    if (source.error() != QFile::NoError)
    {
        *message = QStringLiteral("读取原INI文件未完成。");
        return false;
    }
    QTextCodec::ConverterState state;
    QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars || bytes.contains('\0'))
    {
        *message = QStringLiteral("原INI不是有效UTF-8文本，未猜测或替换原字符。");
        return false;
    }
    // Qt5对带BOM且紧接首分组的文件也可能报格式错误；这里只去掉编码标记。
    if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    int comments = 0;
    bool continued = false;
    for (int start = 0; start < bytes.size(); )
    {
        const int newline = bytes.indexOf('\n', start);
        const int end = newline < 0 ? bytes.size() : newline;
        int first = start;
        while (first < end && (bytes[first] == ' ' || bytes[first] == '\t')) ++first;
        const bool comment = first < end && (bytes[first] == '#' || bytes[first] == ';');
        if (!continued && first < end && bytes[first] == '#')
        {
            bytes[first] = ';';
            ++comments;
        }
        int tail = end;
        if (tail > start && bytes[tail - 1] == '\r') --tail;
        int backslashes = 0;
        while (tail > start && bytes[--tail] == '\\') ++backslashes;
        continued = !(comment && !continued) && backslashes % 2;
        start = end + 1;
    }
    QTemporaryFile compatible(QDir::tempPath() + QStringLiteral("/sn100-ini-XXXXXX.ini"));
    if (!compatible.open() || !compatible.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
        compatible.write(bytes) != bytes.size() || !compatible.flush())
    {
        *message = QStringLiteral("无法创建私有INI读取副本。");
        return false;
    }
    QSettings parsed(compatible.fileName(), QSettings::IniFormat);
    parsed.setIniCodec("UTF-8");
    const QStringList keys = parsed.allKeys();
    if (parsed.status() != QSettings::NoError)
    {
        *message = QStringLiteral("原INI存在其他格式问题，未采用不完整的读取结果。");
        return false;
    }
    for (const QString &key : keys) settings->values.insert(key, parsed.value(key));
    settings->commentLines = comments;
    return true;
}
