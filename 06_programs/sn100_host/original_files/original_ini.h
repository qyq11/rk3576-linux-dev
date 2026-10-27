#ifndef SN100_ORIGINAL_INI_H
#define SN100_ORIGINAL_INI_H

#include <QMap>
#include <QString>
#include <QVariant>

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
}

#endif
