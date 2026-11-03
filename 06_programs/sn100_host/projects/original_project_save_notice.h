#ifndef SN100_ORIGINAL_PROJECT_SAVE_NOTICE_H
#define SN100_ORIGINAL_PROJECT_SAVE_NOTICE_H
#include <QMetaType>
#include <QString>
#include <QList>

// 只由明确的本地保存产生；文件名定位，编号只断言该次提交版本。
struct OriginalProjectSaveNotice
{
    QString source, root, originalFileName, projectName;
    qint64 writeRequest = 0, projectId = 0;
    QString beforeFingerprint, committedFingerprint;
    bool commitSeen = false, verifiedSnapshot = false;
    QString message;
};

struct OriginalProjectUpdateState
{
    OriginalProjectSaveNotice notice;
    quint64 intent = 0, connection = 0;
    QString stage, message, confirmedFingerprint;
    qint64 confirmedProjectId = 0;
    bool ready = false;
};
using OriginalProjectUpdateStates = QList<OriginalProjectUpdateState>;
Q_DECLARE_METATYPE(OriginalProjectSaveNotice)
Q_DECLARE_METATYPE(OriginalProjectUpdateState)
#endif
