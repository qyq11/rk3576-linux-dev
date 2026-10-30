#ifndef SN100_ORIGINAL_INI_WRITE_H
#define SN100_ORIGINAL_INI_WRITE_H

#include "original_ini.h"
#include <QByteArray>
#include <QStringList>

namespace OriginalIni
{
struct Snapshot
{
    Settings parsed;
    QByteArray bytes;
    QString fingerprint;
};
// 读取已部署原INI的实际字节及解析结果；不创建文件，不采用路径缓存。
bool readVersioned(const QString &root, const QString &relativeFile, Snapshot *snapshot, QString *message);
// 只替换明确拥有的键值；其他字节、BOM、注释与换行保持原样。
// 相同原文件使用同一把config.ini.sn100-original-config.lock，兼容医院名称维护。
bool patch(const QString &root, const QString &relativeFile, const QString &expectedFingerprint,
           const QMap<QString, QVariant> &changes, const QStringList &ownedKeys,
           Snapshot *written, QString *message);
}
#endif
