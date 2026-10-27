#include "serial_port_catalog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#if defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <sys/stat.h>
#endif

QStringList localSerialPortNames(QString *error)
{
    error->clear();
    QStringList names;
#if defined(Q_OS_WIN)
    // Qt5 QSettings NativeFormat可能请求写权限/创建注册表键。本片只用
    // KEY_QUERY_VALUE打开已存在的固定设备映射，不创建键或打开COM设备。
    HKEY mapping = nullptr;
    const LSTATUS opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_QUERY_VALUE, &mapping);
    if (opened == ERROR_FILE_NOT_FOUND) return names;
    if (opened != ERROR_SUCCESS)
    {
        *error = QObject::tr("无法读取本机串口设备映射。");
        return names;
    }
    for (DWORD index = 0; ; ++index)
    {
        wchar_t valueName[16384];
        wchar_t value[1024] = {};
        DWORD nameLength = 16384, dataLength = sizeof(value), type = 0;
        const LSTATUS result = RegEnumValueW(mapping, index, valueName, &nameLength,
            nullptr, &type, reinterpret_cast<BYTE *>(value), &dataLength);
        if (result == ERROR_NO_MORE_ITEMS) break;
        if (result != ERROR_SUCCESS || type != REG_SZ || dataLength % sizeof(wchar_t) != 0 || index >= 512)
        {
            *error = QObject::tr("本机串口设备映射无效或超过容量。");
            names.clear();
            break;
        }
        int characters = int(dataLength / sizeof(wchar_t));
        while (characters > 0 && value[characters - 1] == L'\0') --characters;
        names.append(QString::fromWCharArray(value, characters));
    }
    RegCloseKey(mapping);
#elif defined(Q_OS_LINUX)
    // Qt5 SerialPortInfo的8250探测可能open(O_RDWR)设备。本离线切片
    // 只需供用户选择名称，改读sysfs与/dev的stat元数据，不做探测。
    const QDir entries(QStringLiteral("/sys/class/tty"));
    if (!entries.exists() || !QFileInfo(entries.absolutePath()).isReadable())
    {
        *error = QObject::tr("无法读取本机/sys/class/tty串口目录。");
        return names;
    }
    for (const QString &name : entries.entryList(QDir::Dirs | QDir::System | QDir::NoDotAndDotDot))
    {
        if (!QFileInfo::exists(entries.filePath(name + QStringLiteral("/device")))) continue;
        struct stat metadata;
        const QByteArray device = QFile::encodeName(QStringLiteral("/dev/") + name);
        if (::stat(device.constData(), &metadata) == 0 && S_ISCHR(metadata.st_mode)) names.append(name);
    }
#else
    *error = QObject::tr("此平台尚未验证只读串口名称枚举。");
#endif
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}
