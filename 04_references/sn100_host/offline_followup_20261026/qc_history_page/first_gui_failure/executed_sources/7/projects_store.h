#ifndef SN100_PROJECTS_STORE_H
#define SN100_PROJECTS_STORE_H

#include "data/account_store.h"
#include "project_advanced.h"
#include <QList>
#include <QObject>
#include <QSqlDatabase>

struct ProjectUnit
{
    QString name;
    double ratio = 1.0; // Original option_unit.ratio; no clinical calculation here.
};
using ProjectUnits = QList<ProjectUnit>;

struct ProjectResult
{
    QString type;
    QString name;
    int decimals = 2;
    double testLow = 0;
    double testHigh = 999;
    double normalLow = 0;
    double normalHigh = 999;
    QString lisCode;
    QString unit;
    bool mainResult = false;
};
using ProjectResults = QList<ProjectResult>;

// Read-only prerequisites for the next offline sample slice. These are labels
// from configured rows, not inferred numeric dilution or instrument actions.
struct ProjectSampleRate
{
    QString sampleRate;
    bool sampleOption = true;
    bool defaultOption = false;
    int order = 1;
};
using ProjectSampleRates = QList<ProjectSampleRate>;

struct ProjectRecord
{
    qint64 id = 0;               // Stable local key; never derived from a display name.
    QString instrumentProjectId; // Original project_base.project_id.
    QString name;
    QString fullName;
    QString lisCode;
    bool enabled = true;
    bool fixed = false;
    QString changedAt;
    ProjectResults results;
    ProjectSampleRates sampleRates;
    ProjectAdvanced advanced; // Optional local test scheme, never an executable device command.
};
using ProjectRecords = QList<ProjectRecord>;
Q_DECLARE_METATYPE(ProjectUnit)
Q_DECLARE_METATYPE(ProjectUnits)
Q_DECLARE_METATYPE(ProjectResult)
Q_DECLARE_METATYPE(ProjectResults)
Q_DECLARE_METATYPE(ProjectRecord)
Q_DECLARE_METATYPE(ProjectRecords)
Q_DECLARE_METATYPE(ProjectSampleRate)
Q_DECLARE_METATYPE(ProjectSampleRates)

// Create without parent and move to its database QThread before initialize.
// Each module has its own connection to the same explicitly isolated catalog.db.
class ProjectsStore : public QObject
{
    Q_OBJECT
  public:
    explicit ProjectsStore(QObject *parent = nullptr);
    ~ProjectsStore() override;
    static void registerTypes();
    static bool canEdit(const AccountSession &session);
    static QString validate(const ProjectRecord &project, const ProjectUnits &units);
  public slots:
    void initialize(QString directory, bool seedDemo = false);
    void setSession(AccountSession session);
    void load(qint64 requestId);
    void save(qint64 requestId, ProjectRecord project);
    void copy(qint64 requestId, qint64 sourceId, QString newName);
    void remove(qint64 requestId, qint64 projectId);
    // Durable holds protect references in another module's database. Reserve
    // before its commit, release only after its deletion/failed-save outcome.
    void holdProjectReference(qint64 requestId, QString ownerModule, QString referenceKey, qint64 projectId);
    void releaseProjectReference(qint64 requestId, QString ownerModule, QString referenceKey);
    void checkProjectReferences(qint64 requestId, qint64 projectId);
    void close();
  signals:
    void ready(QString databasePath);
    void snapshotReady(qint64 requestId, ProjectRecords projects, ProjectUnits units);
    void reagentOptionsReady(QStringList reagentNames, QStringList cleanerNames);
    void succeeded(qint64 requestId, QString operation, qint64 projectId);
    void error(qint64 requestId, QString operation, QString message);
    void closed();
    void projectReferenceSucceeded(qint64 requestId, QString operation);
    void projectReferenceError(qint64 requestId, QString operation, QString message);
    void projectReferencesReady(qint64 requestId, qint64 projectId, bool exists, QStringList references);

  private:
    bool check(qint64 requestId, const QString &operation, bool write = false);
    bool read(ProjectRecords *projects, ProjectUnits *units, QString *message);
    bool writeProject(ProjectRecord project, bool insert, QString *message, qint64 *id);
    void fail(qint64 requestId, const QString &operation, const QString &message);
    void release();
    QSqlDatabase m_database;
    QString m_connectionName;
    AccountSession m_session;
    bool m_ready = false;
};
#endif
