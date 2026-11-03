#ifndef SN100_ORIGINAL_PROJECT_REPOSITORY_H
#define SN100_ORIGINAL_PROJECT_REPOSITORY_H

#include "projects_store.h"
#include <QMap>
#include "original_project_save_notice.h"
#include <functional>

// 由ProjectsStore的数据库线程调用；不持有界面对象，不创建通信连接。
// 根目录维持原db/systemInfo、db/userSet、project、result分类。
class OriginalProjectRepository
{
public:
    bool initialize(QString root, QString referenceCatalogDirectory, QString *message);
    void close();
    QString root() const { return m_root; }
    bool copyRecoveryPending() const { return !m_uncommittedCopyFiles.isEmpty(); }
    bool writesAllowed(QString *message) const;
    QString unitsDatabasePath() const;
    bool load(ProjectRecords *projects, ProjectUnits *units, QStringList *reagents,
              QStringList *cleaners, QStringList *diluents, QString *message, bool allowVerifiedPending = true);
    bool save(ProjectRecord project, qint64 *savedId, bool *changed, QString *message,
              OriginalProjectSaveNotice *result = nullptr, const QString &expectedFileName = QString(),
              const std::function<bool()> &claim = {},
              const std::function<void(const OriginalProjectSaveNotice &)> &committed = {});
    bool copy(qint64 sourceId, QString newName, qint64 *newId, QString *message);
    bool remove(qint64 projectId, QString *message);
    bool importFile(QString filePath, qint64 *newId, QString *message);
    bool exportBundle(qint64 projectId, QString targetDirectory, QString *message);
    bool addUnit(ProjectUnit unit, QString *message);
    bool updateUnit(QString originalName, ProjectUnit unit, QString *message);
    bool removeUnit(QString name, QString *message);
    bool removeUnit(QString name, QString expectedFingerprint, QString *message);
    bool references(qint64 projectId, QStringList *references, QString *message);
    static QString validate(const ProjectRecord &project, const ProjectUnits &units);
    static QString databaseFingerprint(QString path, QString *message);

private:
    bool readProjects(ProjectRecords *projects, QString *message, bool allowVerifiedPending = false);
    bool readUnits(ProjectUnits *units, QString *message);
    bool readProject(QString filePath, ProjectRecord *project, QString *message) const;
    bool unitReferenced(QString name, bool *referenced, QString *message);
    bool checkRoot(QString *message) const;
    QString m_root;
    QString m_referenceCatalogDirectory;
    QMap<qint64, QString> m_files;
    QStringList m_uncommittedCopyFiles;
};

#endif
