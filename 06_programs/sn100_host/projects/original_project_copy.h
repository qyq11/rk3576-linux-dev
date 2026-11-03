#ifndef SN100_ORIGINAL_PROJECT_COPY_H
#define SN100_ORIGINAL_PROJECT_COPY_H

#include <QString>
#include <QStringList>

namespace OriginalProjectCopy
{
// 调用者先持原项目写锁。没有校准绑定时仍沿项目单文件复制，不复制孤立资料。
bool hasCalibrationBindings(const QString &root, const QString &projectName, bool *found, QString *message);
bool rewriteClone(const QString &path, const QString &sourceName, const QString &newName, qint64 newId, QString *message);
// stagedSource是已核完整行身份的源快照；本函数不修改源文件或网络共享。
bool publishLinked(const QString &root, const QString &sourceFile, const QString &sourceFingerprint,
                   const QString &stagedSource, const QString &sourceName, const QString &newName,
                   qint64 newId, bool *committed, QString *message);
// 初始化/重新载入前处理本程序留的清单；不能证明状态时保留现场并拒绝继续。
bool recoverPending(const QString &root, QStringList *uncommittedFiles, QString *message);
bool noPending(const QString &root, QString *message);
}

#endif
