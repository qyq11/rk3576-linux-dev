#ifndef SN100_PROJECTS_STORE_H
#define SN100_PROJECTS_STORE_H

#include "../data/account_store.h"
#include "project_advanced.h"
#include "project_current_format.h"
#include <QList>
#include <QMap>
#include <QObject>
#include <QSqlDatabase>
#include <memory>

class OriginalProjectRepository;

struct ProjectUnit
{
    QString name;
    double ratio = 1.0; // 原option_unit.ratio；这里只保存倍率，不执行临床计算。
    QString originalType; // 原单位类型原样保留；新库模式不使用此字段。
    QString originalFingerprint; // 读入单位库时的指纹，防止旧页面覆盖外部修改。
    QString originalRatio; // 原报告按QString::toInt换算；保留原字符串，不能用格式化double猜它。
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
    // 原 result_id/is_used/display；未知旧值保持未知，不根据结果名称猜测。
    bool selectionKnown = false;
    int sourceResultId = 0;
    bool used = false;
    bool display = false;
};
using ProjectResults = QList<ProjectResult>;

// 稀释方案的名称、选用和默认；具体加样量保存在advanced的同名行中。
struct ProjectSampleRate
{
    QString sampleRate;
    bool sampleOption = true;
    bool defaultOption = false;
    int order = 1;
};
using ProjectSampleRates = QList<ProjectSampleRate>;

struct ProjectAlgorithm
{
    bool configured = false;
    double coefficientA = 0;
    double coefficientB = 0;
    double coefficientC = 0; // 原 sample_warmup_length 在该页实际表示系数C。
    bool autoApply = false;
};

struct ProjectRecord
{
    qint64 id = 0;               // 稳定的本地编号，不根据显示名称生成。
    QString instrumentProjectId; // 原project_base.project_id；由使用者明确提供。
    QString name;
    QString fullName;
    QString lisCode;
    bool enabled = true;
    bool fixed = false;
    QString changedAt;
    ProjectResults results;
    ProjectSampleRates sampleRates;
    ProjectAdvanced advanced; // 可选测试方案；保存它不会发送设备命令。
    ProjectAlgorithm algorithm; // 仅保存参数，不代表完整计算算法已实现或已应用。
    QString originalFileName; // 原库模式的相对文件名；空值表示既有catalog模式。
    QString originalRoot; // 与草稿绑定的原工作根，拒绝迟到请求写入另一个目录。
    QMap<QString, QString> originalBaseValues; // 全部原project_base键值，未编辑项不丢失。
    QMap<QString, QString> originalAlgorithmValues; // 全部原AlgParam键值，未知项仍保留。
    QString originalFingerprint; // 读入时的文件指纹，拒绝旧草稿覆盖外部修改。
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
Q_DECLARE_METATYPE(ProjectAlgorithm)

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
    void initializeOriginal(QString root, QString referenceCatalogDirectory = QString());
    void openOriginalDirectory(qint64 requestId, QString root);
    void importOriginalFile(qint64 requestId, QString filePath);
    void exportOriginalBundle(qint64 requestId, qint64 projectId, QString targetDirectory);
    void setSession(AccountSession session);
    void load(qint64 requestId);
    // Read-only, explicitly marked source profile; never creates its metadata.
    void loadCurrentResultFormat(qint64 requestId);
    void loadCurrentProjectFormat(qint64 requestId, qint64 projectId);
    // Explicit whole-result configuration replacement, never an implicit Time
    // alias or read-time migration. Existing canEdit permission is reused.
    void replaceCurrentResultFormat(qint64 requestId, qint64 projectId, QString expectedFingerprint,
                                    ProjectResults configurations, ProjectCurrentFormatSelections selections);
    void save(qint64 requestId, ProjectRecord project);
    // 新增和文件导入只追加，不覆盖已有项目或单位；失败整批回滚。
    void create(qint64 requestId, ProjectRecord project);
    void importFile(qint64 requestId, QString filePath);
    void addUnit(qint64 requestId, ProjectUnit unit);
    void updateUnit(qint64 requestId, QString originalName, ProjectUnit unit);
    void removeUnit(qint64 requestId, QString name);
    void removeOriginalUnit(qint64 requestId, QString name, QString expectedFingerprint);
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
    void sourceProfileChanged(QString root, bool original);
    void snapshotReady(qint64 requestId, ProjectRecords projects, ProjectUnits units);
    void currentResultFormatReady(qint64 requestId, ProjectCurrentFormatSnapshot snapshot);
    void currentProjectFormatReady(qint64 requestId, ProjectRecord project, ProjectCurrentFormatSnapshot snapshot);
    void reagentOptionsReady(QStringList reagentNames, QStringList cleanerNames);
    void diluentOptionsReady(QStringList diluentNames);
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
    bool beginWrite(qint64 requestId, const QString &operation);
    void rollbackWrite(qint64 requestId, const QString &operation, QString message);
    bool writeUnit(const ProjectUnit &unit, QString *message);
    bool unitReferenced(const QString &name, bool *referenced, QString *message);
    bool readCurrentResultFormat(ProjectCurrentFormatSnapshot *snapshot, QString *message);
    bool checkOriginalRequest(qint64 requestId, const QString &operation, bool write);
    bool readOriginalFormat(ProjectCurrentFormatSnapshot *snapshot, ProjectRecords *projects,
                            ProjectUnits *units, QString *message);
    bool openOriginal(QString root, QString referenceCatalogDirectory, QString *message);
    void fail(qint64 requestId, const QString &operation, const QString &message);
    void release();
    QSqlDatabase m_database;
    QString m_connectionName;
    AccountSession m_session;
    bool m_ready = false;
    quint64 m_formatGeneration = 0;
    std::unique_ptr<OriginalProjectRepository> m_original;
    qint64 m_lastOriginalWriteRequest = 0;
    QString m_referenceCatalogDirectory;
    bool m_preparingAuxiliary = false;
};
#endif
