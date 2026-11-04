#ifndef SN100_ORIGINAL_INI_H
#define SN100_ORIGINAL_INI_H

#include <QMap>
#include <QString>
#include <QVariant>
#include <memory>

// 原发行版使用整行#注释，Qt5的INI格式只识别分号注释。
// 只在私有临时副本替换注释标记；原文件、键名和配置值都不改写。
namespace OriginalIni
{
struct Settings
{
    QMap<QString, QVariant> values;
    int commentLines = 0;
};
bool read(const QString &path, Settings *settings, QString *message);
// 解析值和摘要来自同一次原字节读取；摘要计算在去BOM/替换注释之前。
bool readWithFingerprint(const QString &path, Settings *settings, QString *fingerprint, QString *message);
QString fingerprint(const QString &path, QString *message);

class InitializationSource;
using InitializationSourcePtr = std::shared_ptr<const InitializationSource>;

// 仅证明本次样本仓库、内置试剂仓库和Session的初始化来源；不替main或其他页面证明配置来源。
// 创建后只读，不公开原INI字节，也不重新捕获版本给已有缓存续签。
class InitializationSource
{
public:
    static InitializationSourcePtr capture(const QString &root, QString *message);
    bool matchesRoot(const QString &root, QString *message) const;
    bool current(const QString &root, QString *message) const;
    bool defaultContainerIsTube(bool *tube, QString *message) const;
    const Settings &normal() const { return normal_; }
    const Settings &debug() const { return debug_; }
    const Settings &theme() const { return theme_; }
    const QMap<QString,QString> &versions() const { return versions_; }
private:
    InitializationSource() = default;
    QString root_, canonicalRoot_;
    Settings normal_, debug_, theme_;
    QMap<QString,QString> versions_;
};
// 沿原Windows大小写不敏感的查键方式，返回实际键名；未找到时返回空名。
// 若Linux读到多个仅大小写不同的键，拒绝猜测覆盖，原键名不改写。
bool resolveKey(const Settings &settings, const QString &key, QString *actualKey, QString *message);
}

#endif
