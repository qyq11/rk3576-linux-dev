#include "lis_settings_store.h"
#include "serial_port_catalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QTextCodec>
#include <QThread>

namespace
{
const QString schemaKey = QStringLiteral("sn100/offline_lis_schema");
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

bool LisSettings::operator==(const LisSettings &other) const
{
    return type == other.type && ip == other.ip && port == other.port
        && commName == other.commName && baudrate == other.baudrate
        && sampleMode == other.sampleMode && autoGet == other.autoGet
        && autoSend == other.autoSend && link == other.link;
}

LisSettingsStore::LisSettingsStore(PortNames names, QObject *parent) :
    QObject(parent), names_(names)
{
    qRegisterMetaType<LisSettings>("LisSettings");
}

QStringList LisSettingsStore::supportedBaudrates()
{
    return {QStringLiteral("1200"), QStringLiteral("2400"), QStringLiteral("4800"),
        QStringLiteral("9600"), QStringLiteral("19200"), QStringLiteral("38400"),
        QStringLiteral("57600"), QStringLiteral("115200")};
}

QString LisSettingsStore::validate(const LisSettings &settings)
{
    for (const QString &text : {settings.type, settings.ip, settings.port, settings.commName, settings.baudrate})
    {
        for (int index = 0; index < text.size(); ++index)
        {
            const QChar character = text.at(index);
            if (character.category() == QChar::Other_Control)
                return tr("LIS可见字段不能包含控制字符。");
            if (character.isHighSurrogate())
            {
                if (++index >= text.size() || !text.at(index).isLowSurrogate())
                    return tr("LIS可见字段包含不完整Unicode字符。");
            }
            else if (character.isLowSurrogate()) return tr("LIS可见字段包含不完整Unicode字符。");
        }
    }
    if (!settings.type.isEmpty() && settings.type != QStringLiteral("tcp")
        && settings.type != QStringLiteral("serial")) return tr("LIS通讯方式无效。");
    // 保留原页16字符地址/五位0..99999端口输入范围；这只是本地配置，
    // 并非真实TCP端点可连接的保证，未来连接还需确认角色及1..65535端口。
    if (settings.ip.size() > 16 || (!settings.ip.isEmpty() && !QRegularExpression(QStringLiteral("^[A-Za-z0-9][^\\s\\x{0000}]{0,15}$")).match(settings.ip).hasMatch()))
        return tr("主计算机地址需以字母或数字开头、不含空白且不超过16字符。");
    if (!QRegularExpression(QStringLiteral("^[0-9]{0,5}$")).match(settings.port).hasMatch())
        return tr("主计算机端口只能为空或0至99999的整数；未验证真实连接。");
    if (settings.commName.size() > 256 || settings.commName.contains(QChar::Null)
        || settings.baudrate.size() > 16 || settings.baudrate.contains(QChar::Null))
        return tr("串口名称或波特率字段无效。");
    if (settings.sampleMode != 0 && settings.sampleMode != 1) return tr("样本上传模式无效。");
    return QString();
}

bool LisSettingsStore::checkThread(const QString &operation)
{
    if (QThread::currentThread() == thread()) return true;
    emit error(operation, tr("LIS设置必须在所属工作线程处理。"));
    return false;
}

bool LisSettingsStore::enumerate(QStringList *names, QString *message)
{
    *names = names_ ? names_() : localSerialPortNames(message);
    if (!message->isEmpty()) return false;
    names->removeDuplicates();
    if (names->size() > 512)
    {
        *message = tr("本机串口列表超过512项。");
        return false;
    }
    for (const QString &name : *names)
    {
        if (name.trimmed().isEmpty() || name.size() > 256 || name.contains(QChar::Null))
        {
            *message = tr("本机返回了无效的串口名称。");
            return false;
        }
    }
    return true;
}

bool LisSettingsStore::readBytes(QByteArray *bytes, QString *message)
{
    if (containsSymlink(path_))
    {
        *message = tr("LIS设置路径已变为符号链接，未读取或修改。");
        return false;
    }
    const QFileInfo info(path_);
    if (!info.isFile() || info.isSymLink() || info.size() > 65536)
    {
        *message = tr("LIS设置文件不是普通文件或超过64KiB。");
        return false;
    }
    QFile input(path_);
    if (!input.open(QIODevice::ReadOnly))
    {
        *message = tr("无法读取LIS设置：%1").arg(input.errorString());
        return false;
    }
    *bytes = input.read(65537);
    if (input.error() != QFile::NoError || bytes->size() > 65536)
    {
        *message = tr("LIS设置读取失败或超过64KiB。");
        return false;
    }
    QTextCodec::ConverterState decoding;
    QTextCodec::codecForName("UTF-8")->toUnicode(bytes->constData(), bytes->size(), &decoding);
    if (decoding.invalidChars || decoding.remainingChars)
    {
        *message = tr("LIS设置含无效或不完整UTF-8，原字节保留。");
        return false;
    }
    return true;
}

bool LisSettingsStore::readStored(LisSettings *value, QByteArray *bytes, QString *message)
{
    if (!readBytes(bytes, message)) return false;
    // 唯一临时路径解析实际字节，避开QSettings同size/mtime全局缓存。
    QTemporaryFile parsed(QFileInfo(path_).absolutePath() + QStringLiteral("/.lis-read-XXXXXX.ini"));
    if (!parsed.open() || parsed.write(*bytes) != bytes->size() || !parsed.flush())
    {
        *message = tr("无法准备LIS设置解析文件。");
        return false;
    }
    parsed.close();
    QSettings settings(parsed.fileName(), QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    settings.sync();
    if (settings.status() != QSettings::NoError || settings.value(schemaKey).toInt() != 1)
    {
        *message = tr("LIS设置格式/脱机标记无效；未导入旧配置。");
        return false;
    }
    value->type = settings.value(QStringLiteral("lis/type")).toString();
    value->ip = settings.value(QStringLiteral("lis/ip")).toString();
    value->port = settings.value(QStringLiteral("lis/port")).toString();
    value->commName = settings.value(QStringLiteral("lis/commname")).toString();
    value->baudrate = settings.value(QStringLiteral("lis/baudrate")).toString();
    const QString sampleMode = settings.value(QStringLiteral("lis/samMode"), QStringLiteral("0")).toString();
    if (sampleMode != QStringLiteral("0") && sampleMode != QStringLiteral("1"))
    {
        *message = tr("已保存的样本上传模式无效。");
        return false;
    }
    value->sampleMode = sampleMode.toInt();
    value->autoGet = settings.value(QStringLiteral("lis/autoget")).toString() == QStringLiteral("true");
    value->autoSend = settings.value(QStringLiteral("lis/autosend")).toString() == QStringLiteral("true");
    value->link = settings.value(QStringLiteral("lis/link")).toString() == QStringLiteral("true");
    *message = validate(*value);
    return message->isEmpty();
}

bool LisSettingsStore::writeStored(const LisSettings &settings, const QByteArray &expected,
                                      QByteArray *written, QString *message)
{
    if (containsSymlink(path_))
    {
        *message = tr("LIS设置路径包含符号链接，原文件保留。");
        return false;
    }
    // 在唯一临时INI中编码原键，再原子替换目标；QSettings写失败的缓存
    // 不得成为下一轮已提交值，也不允许QSaveFile退化为覆盖旧文件。
    QTemporaryFile temporary(QFileInfo(path_).absolutePath() + QStringLiteral("/.lis-XXXXXX.ini"));
    if (!temporary.open())
    {
        *message = tr("无法创建LIS设置临时文件：%1").arg(temporary.errorString());
        return false;
    }
    // 仅更改本模块拥有的键；保留合法配置中其他模块/后续版本的键。
    if ((!expected.isEmpty() && temporary.write(expected) != expected.size()) || !temporary.flush())
    {
        *message = tr("LIS设置原值复制失败，未提交。");
        return false;
    }
    temporary.close();
    {
        QSettings encoded(temporary.fileName(), QSettings::IniFormat);
        encoded.setIniCodec("UTF-8");
        encoded.setValue(schemaKey, 1);
        encoded.setValue(QStringLiteral("lis/type"), settings.type);
        encoded.setValue(QStringLiteral("lis/ip"), settings.ip);
        encoded.setValue(QStringLiteral("lis/port"), settings.port);
        encoded.setValue(QStringLiteral("lis/commname"), settings.commName);
        encoded.setValue(QStringLiteral("lis/baudrate"), settings.baudrate);
        encoded.setValue(QStringLiteral("lis/samMode"), QString::number(settings.sampleMode));
        encoded.setValue(QStringLiteral("lis/autoget"), settings.autoGet ? "true" : "false");
        encoded.setValue(QStringLiteral("lis/autosend"), settings.autoSend ? "true" : "false");
        encoded.setValue(QStringLiteral("lis/link"), settings.link ? "true" : "false");
        encoded.sync();
        if (encoded.status() != QSettings::NoError)
        {
            *message = tr("LIS设置编码/同步失败，原值保留。");
            return false;
        }
    }
    // sync可能原子替换临时路径；按路径打开新文件，不复用旧描述符。
    QFile encodedFile(temporary.fileName());
    if (!encodedFile.open(QIODevice::ReadOnly))
    {
        *message = tr("无法读取LIS设置临时文件。");
        return false;
    }
    const qint64 encodedSize = encodedFile.size();
    const QByteArray bytes = encodedFile.read(65537);
    if (encodedFile.error() != QFile::NoError || bytes.isEmpty()
        || bytes.size() > 65536 || bytes.size() != encodedSize)
    {
        *message = tr("LIS设置临时编码文件读取失败，未提交。");
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
        if (message->isEmpty()) *message = tr("LIS设置已改变，未覆盖其他写入方的数据。");
        return false;
    }
    QSaveFile destination(path_);
    destination.setDirectWriteFallback(false);
    if (!destination.open(QIODevice::WriteOnly) || destination.write(bytes) != bytes.size())
    {
        *message = tr("LIS设置保存失败，原文件与草稿保留：%1").arg(destination.errorString());
        return false;
    }
    if (!unchanged())
    {
        destination.cancelWriting();
        if (message->isEmpty()) *message = tr("LIS设置提交前已改变，原文件保留。");
        return false;
    }
    if (!destination.commit())
    {
        *message = tr("LIS设置原子提交失败，原文件保留：%1").arg(destination.errorString());
        return false;
    }
    *written = bytes;
    return true;
}

void LisSettingsStore::initialize(QString directory)
{
    if (!checkThread(QStringLiteral("initialize"))) return;
    ready_ = false;
    directory = QDir::cleanPath(directory);
    if (!QDir::isAbsolutePath(directory) || containsSymlink(directory) || !QDir().mkpath(directory))
    {
        emit error(QStringLiteral("initialize"), tr("请指定可写、无符号链接的独立LIS设置绝对目录。"));
        return;
    }
    path_ = QDir(directory).filePath(QStringLiteral("lis.ini"));
    if (containsSymlink(path_))
    {
        emit error(QStringLiteral("initialize"), tr("LIS设置路径包含符号链接，未读取或修改。"));
        return;
    }
    // INI父目录暂时失去写权限时，锁所在私有子目录仍可清理锁文件。
    // Qt5 unlock删除失败后会丢锁所有权状态，不能靠再次unlock立即恢复。
    const QString lockDirectory = QDir(directory).filePath(QStringLiteral(".lis-locks"));
    lockPath_ = QDir(lockDirectory).filePath(QStringLiteral("settings.lock"));
    if (containsSymlink(lockPath_) || !QDir().mkpath(lockDirectory))
    {
        emit error(QStringLiteral("initialize"), tr("LIS私有锁目录无效或不可写，未读取或修改配置。"));
        return;
    }
    QLockFile lock(lockPath_);
    QString message;
    QByteArray bytes;
    LisSettings settings;
    QStringList ports;
    if (!lock.tryLock(0))
    {
        emit error(QStringLiteral("initialize"), tr("LIS设置正由其他进程处理，请稍后重开。"));
        return;
    }
    if (!enumerate(&ports, &message)
        || (!QFileInfo::exists(path_) && !writeStored(settings, QByteArray(), &bytes, &message))
        || !readStored(&settings, &bytes, &message))
    {
        emit error(QStringLiteral("initialize"), message);
        return;
    }
    committed_ = settings;
    committedBytes_ = bytes;
    available_ = ports;
    ready_ = true;
    emit loaded(settings, ports);
}

void LisSettingsStore::load()
{
    if (!checkThread(QStringLiteral("load"))) return;
    LisSettings settings;
    QByteArray bytes;
    QString message;
    QStringList ports;
    if (!ready_ || !readStored(&settings, &bytes, &message) || !enumerate(&ports, &message))
    {
        emit error(QStringLiteral("load"), message.isEmpty() ? tr("LIS设置尚未就绪。") : message);
        return;
    }
    committed_ = settings;
    committedBytes_ = bytes;
    available_ = ports;
    emit loaded(settings, ports);
}

void LisSettingsStore::save(LisSettings settings)
{
    if (!checkThread(QStringLiteral("save"))) return;
    if (!ready_ || containsSymlink(path_) || containsSymlink(lockPath_))
    {
        emit error(QStringLiteral("save"), tr("LIS设置未就绪或路径已变为符号链接，未读取或修改。"));
        return;
    }
    LisSettings stored;
    QByteArray bytes;
    QString message;
    QLockFile lock(lockPath_);
    if (!lock.tryLock(0) || !readStored(&stored, &bytes, &message))
    {
        emit error(QStringLiteral("save"), message.isEmpty() ? tr("LIS设置正由其他进程处理。") : message);
        return;
    }
    if (stored != committed_ || bytes != committedBytes_)
    {
        emit error(QStringLiteral("save"), tr("LIS设置已被其他写入方改变，请重开读取后再修改。"));
        return;
    }
    message = validate(settings);
    if (!message.isEmpty())
    {
        emit error(QStringLiteral("save"), message);
        return;
    }
    QStringList ports;
    if (!enumerate(&ports, &message))
    {
        emit error(QStringLiteral("save"), message);
        return;
    }
    // 不可用的已存端口/波特率可原样保留；新选择必须来自枚举/原固定表。
    if ((!settings.commName.isEmpty() && settings.commName != committed_.commName && !ports.contains(settings.commName))
        || (!settings.baudrate.isEmpty() && settings.baudrate != committed_.baudrate && !supportedBaudrates().contains(settings.baudrate)))
    {
        emit error(QStringLiteral("save"), tr("新串口或波特率不在本地可选列表，未改动原配置。"));
        return;
    }
    if (settings != committed_ && !writeStored(settings, bytes, &committedBytes_, &message))
    {
        emit error(QStringLiteral("save"), message);
        return;
    }
    committed_ = settings;
    available_ = ports;
    emit saved(settings, ports);
}

void LisSettingsStore::close()
{
    if (!checkThread(QStringLiteral("close"))) return;
    ready_ = false;
    emit closed();
}
