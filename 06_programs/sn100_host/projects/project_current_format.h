#ifndef SN100_PROJECT_CURRENT_FORMAT_H
#define SN100_PROJECT_CURRENT_FORMAT_H

#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QString>

// This private source profile describes display metadata, never a calculation.
// Missing original selection fields stay unknown; row existence is no default.
struct ProjectCurrentFormatEntry
{
    qint64 projectId = 0;
    QString instrumentProjectId;
    QString projectName;
    bool projectEnabled = false;
    QString resultTypeName;
    int resultType = 0;
    int sourceResultId = 0;
    QString resultName;
    QString unit;
    int decimals = -1;
    int integerRatio = 0;
    bool used = false;
    bool display = false;
    bool mainResult = false;
    // Selection proof is independent of whether unit/precision can be used.
    bool selectionKnown = false;
    bool known = false;
    QString unknownReason;
};
using ProjectCurrentFormatEntries = QList<ProjectCurrentFormatEntry>;

struct ProjectCurrentFormatUnit
{
    QString name;
    double storedRatio = 0;
};
using ProjectCurrentFormatUnits = QList<ProjectCurrentFormatUnit>;

struct ProjectCurrentFormatProject
{
    qint64 id = 0;
    QString instrumentProjectId;
    QString name;
    bool enabled = false;
};
using ProjectCurrentFormatProjects = QList<ProjectCurrentFormatProject>;

// No default silently means selected: the caller must supply the original
// fields explicitly when saving a new local format profile.
struct ProjectCurrentFormatSelection
{
    QString resultType;
    int resultId = 0;
    int used = -1;
    int display = -1;
};
using ProjectCurrentFormatSelections = QList<ProjectCurrentFormatSelection>;
Q_DECLARE_METATYPE(ProjectCurrentFormatSelections)

// Copies own their values. Only the actual ProjectsStore SQL reader can create
// a valid snapshot; callers cannot pass a bool or a GUI draft as source proof.
class ProjectCurrentFormatSnapshot
{
public:
    bool valid() const { return valid_; }
    const QString &directory() const { return directory_; }
    const QString &databasePath() const { return databasePath_; }
    const QString &catalogVersion() const { return catalogVersion_; }
    const QString &formatVersion() const { return formatVersion_; }
    const QString &fingerprint() const { return fingerprint_; }
    const QDateTime &capturedAtUtc() const { return capturedAtUtc_; }
    quint64 generation() const { return generation_; }
    bool selectionProfilePresent() const { return selectionProfilePresent_; }
    const ProjectCurrentFormatEntries &entries() const { return entries_; }
    const ProjectCurrentFormatUnits &units() const { return units_; }
    const ProjectCurrentFormatProjects &projects() const { return projects_; }
private:
    friend class ProjectsStore;
    bool valid_ = false;
    QString directory_, databasePath_, catalogVersion_, formatVersion_, fingerprint_;
    QDateTime capturedAtUtc_;
    quint64 generation_ = 0;
    bool selectionProfilePresent_ = false;
    ProjectCurrentFormatEntries entries_;
    ProjectCurrentFormatUnits units_;
    ProjectCurrentFormatProjects projects_;
};
Q_DECLARE_METATYPE(ProjectCurrentFormatSnapshot)

#endif
