#ifndef SN100_RESULT_PROJECT_REFERENCES_H
#define SN100_RESULT_PROJECT_REFERENCES_H

#include "../projects/projects_store.h"

// Owns a real ProjectsStore/SQL connection in the HistoryStore worker.
// No caller-provided held flag or key is accepted as proof of a catalog hold.
class ResultProjectReferences
{
public:
    explicit ResultProjectReferences(QObject *workerOwner);
    ~ResultProjectReferences();
    bool open(const QString &catalogDirectory, const AccountSession &session, QString *message);
    void setSession(const AccountSession &session);
    bool projects(ProjectRecords *records, QString *message);
    bool has(qint64 projectId, const QString &key, bool *held, QString *message);
    bool hold(qint64 projectId, const QString &key, bool *newlyHeld, QString *message);
    bool release(const QString &key, QString *message);
private:
    bool sameThread(QString *message) const;
    ProjectsStore *store_ = nullptr;
    qint64 serial_ = 0;
};

#endif
