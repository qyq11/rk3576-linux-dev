#include "printer_settings_store.h"
#include "original_files/original_ini_write.h"
#include "original_files/original_business_database.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPrinterInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QThread>

namespace
{
const QString schemaKey = QStringLiteral("sn100/offline_printer_schema");
const QString printerKey = QStringLiteral("printer/name");

bool containsSymlink(QString path)
{
    while (true)
    {
        const QFileInfo info(path);
        if (info.isSymLink()) return true;
        const QString parent = info.absolutePath();
        if (parent == path) return false;
        path = parent;
    }
}
}

PrinterSettingsStore::PrinterSettingsStore(PrinterNames names, QObject *parent) :
    QObject(parent), names_(names ? names : []() { return QPrinterInfo::availablePrinterNames(); })
{
}

bool PrinterSettingsStore::checkThread(const QString &operation)
{
    if (QThread::currentThread() == thread()) return true;
    emit error(operation, tr("打印机设置必须在所属工作线程处理。"));
    return false;
}

bool PrinterSettingsStore::enumerate(QStringList *names, QString *message)
{
    *names = names_();
    names->removeDuplicates();
    if (names->size() > 512)
    {
        *message = tr("打印设备列表超过512项，请检查本机打印配置。");
        return false;
    }
    for (const QString &name : *names)
    {
        if (name.trimmed().isEmpty() || name.size() > 1024 || name.contains(QChar::Null))
        {
            *message = tr("本机返回了无效的打印设备名称。");
            return false;
        }
    }
    return true;
}

bool PrinterSettingsStore::readBytes(QByteArray *bytes, QString *message)
{
    if (containsSymlink(path_))
    {
        *message = tr("打印设置路径已变为符号链接，未读取或修改。");
        return false;
    }
    const QFileInfo info(path_);
    if (!info.isFile() || info.isSymLink() || info.size() > 65536)
    {
        *message = tr("打印设置文件不是普通文件或超过64KiB。");
        return false;
    }
    QFile input(path_);
    if (!input.open(QIODevice::ReadOnly))
    {
        *message = tr("无法读取打印设置：%1").arg(input.errorString());
        return false;
    }
    *bytes = input.read(65537);
    if (input.error() != QFile::NoError || bytes->size() > 65536)
    {
        *message = tr("打印设置读取失败或超过64KiB。");
        return false;
    }
    return true;
}

bool PrinterSettingsStore::readStored(QString *name, QByteArray *bytes, QString *message, QString *fingerprint)
{
    if (!originalRoot_.isEmpty())
    {
        OriginalIni::Snapshot snapshot;
        if (!OriginalIni::readVersioned(originalRoot_, QStringLiteral("config.ini"), &snapshot, message)) return false;
        QString actualKey;
        if (!OriginalIni::resolveKey(snapshot.parsed, printerKey, &actualKey, message)) return false;
        *name = snapshot.parsed.values.value(actualKey).toString();
        if (name->size() > 1024 || name->contains(QChar::Null))
        { *message = tr("原打印机名称无效，未替换为其他设备。"); return false; }
        *bytes = snapshot.bytes;
        if (fingerprint) *fingerprint = snapshot.fingerprint;
        return true;
    }
    if (!readBytes(bytes, message)) return false;
    // Qt5目标路径QSettings可能复用相同size/mtime的缓存。解析独一临时
    // 文件中的实际字节，使外部原子替换和同时间戳改动也能被发现。
    QTemporaryFile parsed(QFileInfo(path_).absolutePath() + QStringLiteral("/.printer-read-XXXXXX.ini"));
    if (!parsed.open() || parsed.write(*bytes) != bytes->size() || !parsed.flush())
    {
        *message = tr("无法准备打印设置解析文件。");
        return false;
    }
    parsed.close();
    QSettings settings(parsed.fileName(), QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    settings.sync();
    if (settings.status() != QSettings::NoError || settings.value(schemaKey).toInt() != 1)
    {
        *message = tr("打印设置格式/脱机标记无效；未导入旧配置。");
        return false;
    }
    *name = settings.value(printerKey).toString();
    if (name->size() > 1024 || name->contains(QChar::Null))
    {
        *message = tr("已保存的打印机名称无效。");
        return false;
    }
    return true;
}

bool PrinterSettingsStore::writeStored(const QString &name, const QByteArray &expected,
                                      QByteArray *written, QString *message)
{
    if (!originalRoot_.isEmpty())
    {
        OriginalIni::Snapshot before, after;
        if (!OriginalIni::readVersioned(originalRoot_, QStringLiteral("config.ini"), &before, message)) return false;
        if (before.bytes != expected)
        { *message = tr("原打印配置提交前变化，未覆盖。"); return false; }
        if (!OriginalIni::patch(originalRoot_, QStringLiteral("config.ini"), before.fingerprint,
            {{printerKey, name}}, {printerKey}, &after, message)) return false;
        *written = after.bytes;
        originalFingerprint_ = after.fingerprint;
        return true;
    }
    if (containsSymlink(path_))
    {
        *message = tr("打印设置路径包含符号链接，原文件保留。");
        return false;
    }
    // 在唯一临时INI中编码原键，再原子替换目标；QSettings写失败的缓存
    // 不得成为下一轮已提交值，也不允许QSaveFile退化为覆盖旧文件。
    QTemporaryFile temporary(QFileInfo(path_).absolutePath() + QStringLiteral("/.printer-XXXXXX.ini"));
    if (!temporary.open())
    {
        *message = tr("无法创建打印设置临时文件：%1").arg(temporary.errorString());
        return false;
    }
    // 仅更改本模块拥有的键；保留合法配置中其他模块/后续版本的键。
    if ((!expected.isEmpty() && temporary.write(expected) != expected.size()) || !temporary.flush())
    {
        *message = tr("打印设置原值复制失败，未提交。");
        return false;
    }
    temporary.close();
    {
        QSettings encoded(temporary.fileName(), QSettings::IniFormat);
        encoded.setIniCodec("UTF-8");
        encoded.setValue(schemaKey, 1);
        encoded.setValue(printerKey, name);
        encoded.sync();
        if (encoded.status() != QSettings::NoError)
        {
            *message = tr("打印设置编码/同步失败，原值保留。");
            return false;
        }
    }
    // sync可能原子替换临时路径；按路径打开新文件，不复用旧描述符。
    QFile encodedFile(temporary.fileName());
    if (!encodedFile.open(QIODevice::ReadOnly))
    {
        *message = tr("无法读取打印设置临时文件。");
        return false;
    }
    const qint64 encodedSize = encodedFile.size();
    const QByteArray bytes = encodedFile.read(65537);
    if (encodedFile.error() != QFile::NoError || bytes.isEmpty()
        || bytes.size() > 65536 || bytes.size() != encodedSize)
    {
        *message = tr("打印设置临时编码文件读取失败，未提交。");
        return false;
    }
    encodedFile.close();
    const auto unchanged = [&]()
    {
        if (expected.isEmpty())
            return !QFileInfo::exists(path_) && !QFileInfo(path_).isSymLink();
        QByteArray actual;
        return readBytes(&actual, message) && actual == expected;
    };
    if (!unchanged())
    {
        if (message->isEmpty()) *message = tr("打印设置已改变，未覆盖其他写入方的数据。");
        return false;
    }
    QSaveFile destination(path_);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size())
    {
        *message = tr("打印设置保存失败，原文件与草稿保留：%1").arg(destination.errorString());
        return false;
    }
    if (!unchanged())
    {
        destination.cancelWriting();
        if (message->isEmpty()) *message = tr("打印设置提交前已改变，原文件保留。");
        return false;
    }
    if (!destination.commit())
    {
        *message = tr("打印设置原子提交失败，原文件保留：%1").arg(destination.errorString());
        return false;
    }
    *written = bytes;
    return true;
}

void PrinterSettingsStore::initialize(QString directory)
{
    if (!checkThread(QStringLiteral("initialize"))) return;
    originalRoot_.clear(); originalFingerprint_.clear();
    ready_ = false;
    directory = QDir::cleanPath(directory);
    if (!QDir::isAbsolutePath(directory) || containsSymlink(directory)
        || !QDir().mkpath(directory))
    {
        emit error(QStringLiteral("initialize"), tr("请指定可写、无符号链接的独立打印设置绝对目录。"));
        return;
    }
    path_ = QDir(directory).filePath(QStringLiteral("printer.ini"));
    QString message, name;
    QByteArray bytes;
    QStringList printers;
    // 枚举失败时不创建半初始化的设置文件。
    if (containsSymlink(path_))
    {
        emit error(QStringLiteral("initialize"), tr("打印设置路径包含符号链接，未读取或修改。"));
        return;
    }
    QLockFile lock(QDir(directory).filePath(QStringLiteral(".printer-settings.lock")));
    if (!lock.tryLock(0))
    {
        emit error(QStringLiteral("initialize"), tr("打印设置正由其他进程处理，请稍后重开。"));
        return;
    }
    if (!enumerate(&printers, &message)
        || (!QFileInfo::exists(path_) && !writeStored(QString(), QByteArray(), &bytes, &message))
        || !readStored(&name, &bytes, &message))
    {
        emit error(QStringLiteral("initialize"), message);
        return;
    }
    committed_ = name;
    committedBytes_ = bytes;
    available_ = printers;
    ready_ = true;
    emit loaded(committed_, available_);
}

void PrinterSettingsStore::initializeOriginal(QString root)
{
    const QString operation = QStringLiteral("initialize");
    if (!checkThread(operation)) return;
    ready_ = false;
    QString message;
    if (!OriginalBusiness::workingRoot(root, &message)) { emit error(operation, message); return; }
    originalRoot_ = QFileInfo(root).canonicalFilePath();
    path_ = QDir(originalRoot_).filePath(QStringLiteral("config.ini"));
    QString name, fingerprint;
    QByteArray bytes;
    QStringList printers;
    if (!readStored(&name, &bytes, &message, &fingerprint) || !enumerate(&printers, &message))
    { emit error(operation, message); return; }
    committed_ = name; committedBytes_ = bytes; originalFingerprint_ = fingerprint;
    available_ = printers; ready_ = true;
    emit loaded(committed_, available_);
}

void PrinterSettingsStore::load()
{
    if (!checkThread(QStringLiteral("load"))) return;
    QString message, name;
    QByteArray bytes;
    QString fingerprint;
    QStringList printers;
    if (!ready_ || !readStored(&name, &bytes, &message, &fingerprint) || !enumerate(&printers, &message))
    {
        emit error(QStringLiteral("load"), message.isEmpty() ? tr("打印设置尚未就绪。") : message);
        return;
    }
    committed_ = name;
    committedBytes_ = bytes;
    if (!originalRoot_.isEmpty()) originalFingerprint_ = fingerprint;
    available_ = printers;
    emit loaded(name, printers);
}

void PrinterSettingsStore::save(QString name)
{
    if (!checkThread(QStringLiteral("save"))) return;
    if (!ready_ || containsSymlink(path_))
    {
        emit error(QStringLiteral("save"), tr("打印设置未就绪或路径已变为符号链接，未读取或修改。"));
        return;
    }
    QString message, stored;
    QByteArray bytes;
    QStringList printers;
    QLockFile lock(QFileInfo(path_).absolutePath() + QStringLiteral("/.printer-settings.lock"));
    if (!lock.tryLock(0) || !readStored(&stored, &bytes, &message))
    {
        emit error(QStringLiteral("save"), message.isEmpty() ? tr("打印设置未就绪或正由其他进程处理。") : message);
        return;
    }
    if (stored != committed_ || (originalRoot_.isEmpty() && bytes != committedBytes_))
    {
        emit error(QStringLiteral("save"), tr("打印设置已被其他写入方改变，请重开读取后再修改。"));
        return;
    }
    if (name == committed_)
    {
        emit saved(committed_, available_);
        return;
    }
    if (name.trimmed().isEmpty() || name.size() > 1024 || name.contains(QChar::Null))
    {
        emit error(QStringLiteral("save"), tr("打印机名称不能为空或无效。"));
        return;
    }
    if (!enumerate(&printers, &message) || !printers.contains(name))
    {
        emit error(QStringLiteral("save"), message.isEmpty() ? tr("所选打印设备当前不可用，请重新读取本机列表。") : message);
        return;
    }
    if (!writeStored(name, bytes, &committedBytes_, &message))
    {
        emit error(QStringLiteral("save"), message);
        return;
    }
    committed_ = name;
    available_ = printers;
    emit saved(committed_, available_);
}

void PrinterSettingsStore::close()
{
    if (!checkThread(QStringLiteral("close"))) return;
    ready_ = false;
    originalRoot_.clear(); originalFingerprint_.clear();
    emit closed();
}
