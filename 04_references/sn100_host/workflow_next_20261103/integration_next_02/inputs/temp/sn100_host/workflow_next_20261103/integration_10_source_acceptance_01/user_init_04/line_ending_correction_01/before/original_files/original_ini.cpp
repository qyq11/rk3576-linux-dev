#include "original_ini.h"
#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextCodec>

namespace
{
bool readBytes(const QString &path, QByteArray *result, QString *message)
{
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
    *result = bytes;
    return true;
}

bool parseBytes(QByteArray bytes, OriginalIni::Settings *settings, QString *message)
{
    QTextCodec::ConverterState state;
    QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (state.invalidChars || state.remainingChars || bytes.contains('\0'))
    {
        *message = QStringLiteral("原INI不是有效UTF-8文本，未猜测或替换原字符。");
        return false;
    }
    // Qt5对带BOM且紧接首分组的文件也可能报格式错误；这里只去掉编码标记。
    if (bytes.startsWith("\xef\xbb\xbf")) bytes.remove(0, 3);
    int comments = 0;
    bool continued = false;
    bool quoted = false;
    for (int start = 0; start < bytes.size(); )
    {
        const int newline = bytes.indexOf('\n', start);
        const int end = newline < 0 ? bytes.size() : newline;
        int first = start;
        while (first < end && (bytes[first] == ' ' || bytes[first] == '\t')) ++first;
        const bool comment = !continued && !quoted && first < end
            && (bytes[first] == '#' || bytes[first] == ';');
        if (comment && bytes[first] == '#')
        {
            bytes[first] = ';';
            ++comments;
        }
        // 多行双引号值中的行首#属于值本身；只在真正的整行注释处做Qt兼容替换。
        if (!comment)
        {
            int valueStart = start;
            if (!continued && !quoted)
            {
                const int equals = bytes.indexOf('=',start);
                valueStart = equals >= start && equals < end ? equals+1 : end;
            }
            bool escaped = false;
            for (int offset=valueStart; offset<end; ++offset)
            {
                const char ch=bytes[offset];
                if (escaped) { escaped=false; continue; }
                if (ch=='\\') { escaped=true; continue; }
                if (ch=='"') quoted=!quoted;
                if (ch==';' && !quoted) break;
            }
        }
        int tail = end;
        if (tail > start && bytes[tail - 1] == '\r') --tail;
        int backslashes = 0;
        while (tail > start && bytes[--tail] == '\\') ++backslashes;
        continued = !comment && backslashes % 2;
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
}

bool OriginalIni::read(const QString &path, Settings *settings, QString *message)
{
    if (!settings || !message) return false;
    *settings = Settings();
    message->clear();
    QByteArray bytes;
    return readBytes(path,&bytes,message) && parseBytes(bytes,settings,message);
}

bool OriginalIni::readWithFingerprint(const QString &path, Settings *settings, QString *digest, QString *message)
{
    if (!settings || !digest || !message) return false;
    *settings = Settings();
    digest->clear();
    message->clear();
    QByteArray bytes;
    if (!readBytes(path,&bytes,message) || !parseBytes(bytes,settings,message)) return false;
    *digest = QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());
    return true;
}

QString OriginalIni::fingerprint(const QString &path, QString *message)
{
    if (!message) return QString();
    message->clear();
    QByteArray bytes;
    return readBytes(path,&bytes,message)
        ? QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex()) : QString();
}

OriginalIni::InitializationSourcePtr OriginalIni::InitializationSource::capture(const QString &root, QString *message)
{
    if (!message) return {};
    auto captured = std::shared_ptr<InitializationSource>(new InitializationSource);
    if (!QDir::isAbsolutePath(root) || !QFileInfo(root).isDir())
    { *message=QStringLiteral("初始化配置必须来自现有资料根的绝对路径。"); return {}; }
    captured->root_=QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    captured->canonicalRoot_=QFileInfo(root).canonicalFilePath();
    if (captured->canonicalRoot_.isEmpty())
    { *message=QStringLiteral("初始化配置的实际资料根无法确认。"); return {}; }
    const QStringList files{QStringLiteral("config.ini"),QStringLiteral("config_Debug.ini"),QStringLiteral("theme_config.ini")};
    Settings *settings[]{&captured->normal_,&captured->debug_,&captured->theme_};
    for (int index=0;index<files.size();++index)
    {
        QString digest;
        if (!readWithFingerprint(QDir(root).filePath(files[index]),settings[index],&digest,message)) return {};
        captured->versions_.insert(files[index],digest);
    }
    // 三文件不能原子读取；捕获后和安装前分别重核，绝不把后来版本贴到早先解析值上。
    if (!captured->current(root,message)) return {};
    return captured;
}

bool OriginalIni::InitializationSource::matchesRoot(const QString &root, QString *message) const
{
    if (!message) return false;
    if (!QDir::isAbsolutePath(root) || QDir::cleanPath(QFileInfo(root).absoluteFilePath())!=root_ ||
        QFileInfo(root).canonicalFilePath()!=canonicalRoot_ || !QFileInfo(root).isDir())
    { *message=QStringLiteral("初始化配置的资料根或实际路径已改变，未沿用旧来源。"); return false; }
    return true;
}

bool OriginalIni::InitializationSource::current(const QString &root, QString *message) const
{
    if (!matchesRoot(root,message)) return false;
    for (auto version=versions_.cbegin();version!=versions_.cend();++version)
    {
        const QString actual=fingerprint(QDir(root).filePath(version.key()),message);
        if (actual.isEmpty()) return false;
        if (actual!=version.value())
        { *message=QStringLiteral("初始化期间原配置已改变，请重新加载资料；未发布混用版本的缓存。"); return false; }
    }
    return matchesRoot(root,message);
}

bool OriginalIni::InitializationSource::defaultContainerIsTube(bool *tube, QString *message) const
{
    if (!tube || !message) return false;
    // 与main的OriginalRuntime::IniReader::flag相同：准确键优先，否则仅接受唯一大小写匹配。
    const QString key=QStringLiteral("userOption/isContainerTube");
    QString actual=key;
    if (!normal_.values.contains(key) && !resolveKey(normal_,key,&actual,message)) return false;
    const QString value=normal_.values.value(actual,false).toString().trimmed().toLower();
    if (value==QStringLiteral("true") || value==QStringLiteral("1")) { *tube=true; return true; }
    if (value==QStringLiteral("false") || value==QStringLiteral("0") || value.isEmpty()) { *tube=false; return true; }
    *message=QStringLiteral("原新孔容器配置不是有效布尔值，未按默认值接续。");
    return false;
}

bool OriginalIni::resolveKey(const Settings &settings, const QString &key, QString *actualKey, QString *message)
{
    if (!actualKey || !message) return false;
    actualKey->clear();
    for (auto it = settings.values.cbegin(); it != settings.values.cend(); ++it)
    {
        if (it.key().compare(key, Qt::CaseInsensitive) != 0) continue;
        if (!actualKey->isEmpty())
        {
            *message = QStringLiteral("原INI有多个仅大小写不同的同名键，未猜测读取或覆盖：") + key;
            return false;
        }
        *actualKey = it.key();
    }
    return true;
}
