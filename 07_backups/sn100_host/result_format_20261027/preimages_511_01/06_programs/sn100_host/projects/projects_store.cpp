#include "projects_store.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace
{
const char markerName[] = ".sn100-catalog-offline-v1";
QString sqlError(const QSqlQuery &query)
{
    return query.lastError().text();
}
bool validName(const QString &name, int length)
{
    // Reference helper's ASCII head/exact-length regex conflicts with the
    // Chinese full-name field. This isolated slice accepts a Unicode letter or
    // digit head, at most the stated length, and excludes path characters.
    static const QRegularExpression expression(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N}_() .%+\\-]*$"));
    return !name.isEmpty() && name.size() <= length && name == name.trimmed() &&
           expression.match(name).hasMatch();
}
QJsonObject fieldsJson(const QMap<QString, QString> &fields)
{
    QJsonObject object;
    for (auto it = fields.cbegin(); it != fields.cend(); ++it)
    {
        object.insert(it.key(), it.value());
    }
    return object;
}
QMap<QString, QString> jsonFields(const QJsonObject &object)
{
    QMap<QString, QString> fields;
    for (auto it = object.begin(); it != object.end(); ++it)
    {
        fields.insert(it.key(), it.value().toString());
    }
    return fields;
}
QString advancedJson(const ProjectAdvanced &advanced)
{
    QJsonObject object;
    object.insert(QStringLiteral("fields"), fieldsJson(advanced.fields));
    QJsonArray samples, reagents;
    for (const auto &row : advanced.samples) samples.append(fieldsJson(row));
    for (const auto &row : advanced.reagents) reagents.append(fieldsJson(row));
    object.insert(QStringLiteral("samples"), samples);
    object.insert(QStringLiteral("reagents"), reagents);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
bool parseAdvanced(const QString &json, ProjectAdvanced *advanced)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
    const auto object = document.object();
    if (object.size() != 3 || !object.value(QStringLiteral("fields")).isObject() ||
        !object.value(QStringLiteral("samples")).isArray() ||
        !object.value(QStringLiteral("reagents")).isArray()) return false;
    advanced->configured = true;
    auto stringFields = [](const QJsonObject &fields)
    {
        for (auto field = fields.begin(); field != fields.end(); ++field)
            if (!field.value().isString()) return false;
        return true;
    };
    if (!stringFields(object.value(QStringLiteral("fields")).toObject())) return false;
    advanced->fields = jsonFields(object.value(QStringLiteral("fields")).toObject());
    for (const auto &row : object.value(QStringLiteral("samples")).toArray())
    {
        if (!row.isObject() || !stringFields(row.toObject())) return false;
        advanced->samples.append(jsonFields(row.toObject()));
    }
    for (const auto &row : object.value(QStringLiteral("reagents")).toArray())
    {
        if (!row.isObject() || !stringFields(row.toObject())) return false;
        advanced->reagents.append(jsonFields(row.toObject()));
    }
    return true;
}
bool writeActionReferences(QSqlDatabase database, qint64 projectId, const ProjectAdvanced &advanced,
                           QString *message)
{
    const bool haveReagents = database.tables().contains(QStringLiteral("reagent_list"));
    QSqlQuery query(database);
    if (haveReagents)
    {
        if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS project_action_reagent_reference(project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE CASCADE,reagent_id INTEGER NOT NULL REFERENCES reagent_list(id) ON DELETE RESTRICT,PRIMARY KEY(project_id,reagent_id))")))
        {
            *message = sqlError(query);
            return false;
        }
        query.prepare(QStringLiteral("DELETE FROM project_action_reagent_reference WHERE project_id=?"));
        query.addBindValue(projectId);
        if (!query.exec()) { *message = sqlError(query); return false; }
    }
    QList<QPair<QString, QString>> required;
    for (const auto &row : advanced.samples)
        required.append(qMakePair(row.value(QStringLiteral("before_clean_name")), QStringLiteral("清洗液")));
    for (const auto &row : advanced.reagents)
    {
        required.append(qMakePair(row.value(QStringLiteral("reagent_name")), QStringLiteral("试剂")));
        required.append(qMakePair(row.value(QStringLiteral("before_clean_name")), QStringLiteral("清洗液")));
    }
    for (const auto &reference : required)
    {
        if (reference.first.isEmpty() || reference.first == QStringLiteral("none")) continue;
        if (!haveReagents)
        {
            *message = QStringLiteral("试剂目录尚未就绪，不能保存具名动作试剂。请先保存试剂资料并刷新项目页。");
            return false;
        }
        query.prepare(QStringLiteral("SELECT id FROM reagent_list WHERE reagent_name=? AND reagent_type=? AND valid=1"));
        query.addBindValue(reference.first);
        query.addBindValue(reference.second);
        if (!query.exec() || !query.next())
        {
            *message = QStringLiteral("动作中的%1“%2”未在有效试剂资料中配置。").arg(reference.second, reference.first);
            return false;
        }
        const qint64 reagentId = query.value(0).toLongLong();
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO project_action_reagent_reference VALUES(?,?)"));
        query.addBindValue(projectId);
        query.addBindValue(reagentId);
        if (!query.exec()) { *message = sqlError(query); return false; }
    }
    return true;
}
} // namespace

ProjectsStore::ProjectsStore(QObject *parent) : QObject(parent) {}
ProjectsStore::~ProjectsStore()
{
    release();
}
void ProjectsStore::registerTypes()
{
    qRegisterMetaType<AccountSession>();
    qRegisterMetaType<ProjectUnit>();
    qRegisterMetaType<ProjectUnits>();
    qRegisterMetaType<ProjectResult>();
    qRegisterMetaType<ProjectResults>();
    qRegisterMetaType<ProjectRecord>();
    qRegisterMetaType<ProjectRecords>();
    qRegisterMetaType<ProjectSampleRate>();
    qRegisterMetaType<ProjectSampleRates>();
    qRegisterMetaType<ProjectAdvanced>();
}
bool ProjectsStore::canEdit(const AccountSession &session)
{
    return session.authenticated && session.actionReady && (session.role == 4 || session.role == 5);
}
QString ProjectsStore::validate(const ProjectRecord &project, const ProjectUnits &units)
{
    if (!validName(project.name, 16) || !validName(project.fullName, 16))
    {
        return QStringLiteral("项目名称和全称须以文字或数字开头，长度1至16，不能有首尾空格或路径字符。");
    }
    if (project.lisCode.size() > 8 || project.lisCode.contains(QChar::Null))
    {
        return QStringLiteral("项目通道号最长8个字符。");
    }
    if (project.results.isEmpty() || project.results.size() > 6)
    {
        return QStringLiteral("项目须含1至6个结果配置。");
    }
    int mainCount = 0;
    QSet<QString> resultTypes;
    for (const ProjectResult &result : project.results)
    {
        if (resultTypes.contains(result.type) || result.type.isEmpty())
        {
            return QStringLiteral("结果类型重复或为空。");
        }
        resultTypes.insert(result.type);
        if (!validName(result.name, 16) || result.lisCode.size() > 8 || result.decimals < 1 ||
            result.decimals > 6)
        {
            return QStringLiteral("结果名称/传输代号或小数位数无效（小数位1至6）。");
        }
        const double ranges[] = {result.testLow, result.testHigh, result.normalLow, result.normalHigh};
        for (double range : ranges)
        {
            if (!std::isfinite(range) || range < 0 || range > 999)
            {
                return QStringLiteral("结果范围须为0至999的有限数值。");
            }
        }
        if (result.testLow > result.testHigh || result.normalLow > result.normalHigh)
        {
            return QStringLiteral("结果下限不能大于上限。");
        }
        bool found = false;
        for (const ProjectUnit &unit : units)
        {
            if (unit.name == result.unit)
            {
                found = true;
            }
        }
        if (!found)
        {
            return QStringLiteral("结果单位未在已加载单位字典中配置。");
        }
        if (result.mainResult)
        {
            ++mainCount;
        }
    }
    if (mainCount != 1)
    {
        return QStringLiteral("须选择一个主显示单位。");
    }
    QStringList rates;
    for (const auto &rate : project.sampleRates) rates.append(rate.sampleRate);
    return ProjectAdvancedEditor::validate(project.advanced, rates);
}
void ProjectsStore::fail(qint64 requestId, const QString &operation, const QString &message)
{
    emit error(requestId, operation, message);
}
bool ProjectsStore::check(qint64 requestId, const QString &operation, bool write)
{
    if (QThread::currentThread() != thread())
    {
        fail(requestId, operation, QStringLiteral("项目存储调用线程错误。"));
        return false;
    }
    if (!m_ready)
    {
        fail(requestId, operation, QStringLiteral("项目数据库未就绪。"));
        return false;
    }
    if (write && !canEdit(m_session))
    {
        fail(requestId, operation, QStringLiteral("仅工程师/管理员可在就绪状态修改项目。"));
        return false;
    }
    return true;
}
void ProjectsStore::initialize(QString directory, bool seedDemo)
{
    if (QThread::currentThread() != thread())
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("线程错误。"));
        return;
    }
    release();
    directory = QDir::cleanPath(directory);
    if (!QDir::isAbsolutePath(directory) ||
        directory.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive))
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("仅接受显式隔离数据绝对目录，禁止参考路径。"));
        return;
    }
    for (QFileInfo current(directory); !current.filePath().isEmpty(); current.setFile(current.absolutePath()))
    {
        if (current.isSymLink())
        {
            fail(0, QStringLiteral("initialize"), QStringLiteral("隔离目录不能经过符号链接。"));
            return;
        }
        if (current.absoluteFilePath() == current.absolutePath())
        {
            break;
        }
    }
    QDir dir(directory);
    const bool marked = QFileInfo::exists(dir.filePath(QString::fromLatin1(markerName)));
    if (!marked && dir.exists() &&
        !dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty())
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("非空未标记目录不允许初始化项目库。"));
        return;
    }
    if (!dir.mkpath(QStringLiteral(".")))
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("无法建立数据目录。"));
        return;
    }
    const QString databasePath = dir.filePath(QStringLiteral("catalog.db"));
    const bool newDatabase = !QFileInfo::exists(databasePath);
    if (QFileInfo(databasePath).isSymLink())
    {
        fail(0, QStringLiteral("initialize"), QStringLiteral("数据库不能是符号链接。"));
        return;
    }
    m_connectionName = QStringLiteral("Projects-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_database.setDatabaseName(databasePath);
    m_database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1000"));
    if (!m_database.open())
    {
        fail(0, QStringLiteral("initialize"), m_database.lastError().text());
        release();
        return;
    }
    QSqlQuery query(m_database);
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    const QStringList schema = {
        QStringLiteral("PRAGMA foreign_keys=ON"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS catalog_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS option_unit(unit TEXT PRIMARY KEY,ratio REAL NOT NULL "
                       "CHECK(ratio>0))"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project_base(id INTEGER PRIMARY KEY AUTOINCREMENT,project_id TEXT "
            "NOT NULL,project_en_name TEXT NOT NULL UNIQUE COLLATE NOCASE,project_cn_name TEXT NOT "
            "NULL,project_lis_code TEXT NOT NULL DEFAULT '',is_used INTEGER NOT NULL CHECK(is_used "
            "IN(0,1)),fixed INTEGER NOT NULL CHECK(fixed IN(0,1)),change_time TEXT NOT NULL)"),
        QStringLiteral(
            "CREATE TABLE IF NOT EXISTS project_sample_rate(project_id INTEGER NOT NULL REFERENCES "
            "project_base(id) ON DELETE CASCADE,sample_rate TEXT NOT NULL,sample_option INTEGER NOT NULL "
            "CHECK(sample_option IN(0,1)),sample_default_option INTEGER NOT NULL CHECK(sample_default_option "
            "IN(0,1)),xuhao INTEGER NOT NULL,PRIMARY KEY(project_id,sample_rate),UNIQUE(project_id,xuhao))"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_result(project_id INTEGER NOT NULL REFERENCES "
                       "project_base(id) ON DELETE CASCADE,result_type TEXT NOT NULL,result_name TEXT NOT "
                       "NULL,decimal_point INTEGER NOT NULL,test_low_range REAL NOT NULL,test_up_range REAL "
                       "NOT NULL,normal_low_range REAL NOT NULL,normal_up_range REAL NOT NULL,lis_code TEXT "
                       "NOT NULL,unit_name TEXT NOT NULL REFERENCES option_unit(unit) ON DELETE "
                       "RESTRICT,main_result INTEGER NOT NULL,PRIMARY KEY(project_id,result_type))")};
    if (!newDatabase)
    {
        if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='schema'")) ||
            !query.next() || query.value(0).toString() != QStringLiteral("sn100-catalog-offline-v1"))
        {
            fail(0, QStringLiteral("initialize"),
                 QStringLiteral("现有数据库不是本模块隔离结构，保留原文件。"));
            release();
            return;
        }
    }
    if (!m_database.transaction())
    {
        fail(0, QStringLiteral("initialize"), m_database.lastError().text());
        release();
        return;
    }
    for (const QString &statement : schema)
    {
        if (!query.exec(statement))
        {
            m_database.rollback();
            fail(0, QStringLiteral("initialize"), sqlError(query));
            release();
            return;
        }
    }
    const QStringList additions = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS project_advanced(project_id INTEGER PRIMARY KEY REFERENCES project_base(id) ON DELETE CASCADE,version INTEGER NOT NULL CHECK(version=1),payload TEXT NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS catalog_project_reference(owner_module TEXT NOT NULL,reference_key TEXT NOT NULL,project_id INTEGER NOT NULL REFERENCES project_base(id) ON DELETE RESTRICT,PRIMARY KEY(owner_module,reference_key))"),
        QStringLiteral("INSERT OR IGNORE INTO catalog_metadata VALUES('project_advanced_schema','sn100-project-advanced-local-v1')")};
    if (!query.exec(QStringLiteral("SELECT value FROM catalog_metadata WHERE key='project_advanced_schema'")))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), sqlError(query));
        release();
        return;
    }
    if (query.next() && query.value(0).toString() != QStringLiteral("sn100-project-advanced-local-v1"))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), QStringLiteral("高级项目结构版本未知，保留原文件。"));
        release();
        return;
    }
    for (const auto &statement : additions)
    {
        if (!query.exec(statement))
        {
            m_database.rollback();
            fail(0, QStringLiteral("initialize"), sqlError(query));
            release();
            return;
        }
    }
    if (newDatabase && !query.exec(QStringLiteral(
                           "INSERT INTO catalog_metadata VALUES('schema','sn100-catalog-offline-v1')")))
    {
        m_database.rollback();
        fail(0, QStringLiteral("initialize"), sqlError(query));
        release();
        return;
    }
    if (!m_database.commit())
    {
        fail(0, QStringLiteral("initialize"), m_database.lastError().text());
        release();
        return;
    }
    query.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (newDatabase && seedDemo)
    {
        if (!m_database.transaction())
        {
            fail(0, QStringLiteral("initialize"), m_database.lastError().text());
            release();
            return;
        }
        bool ok =
            query.exec(QStringLiteral("INSERT INTO option_unit VALUES('s',1),('a.u.',1),('a.u.x10',10)"));
        ProjectRecord demo;
        demo.instrumentProjectId = QStringLiteral("OFFLINE-001");
        demo.name = QStringLiteral("DEMO_A");
        demo.fullName = QStringLiteral("离线项目A");
        demo.fixed = true;
        ProjectResult time;
        time.type = QStringLiteral("Time");
        time.name = QStringLiteral("DEMO_A");
        time.unit = QStringLiteral("s");
        time.mainResult = true;
        ProjectResult amount;
        amount.type = QStringLiteral("Calibration");
        amount.name = QStringLiteral("DEMO_A_C");
        amount.unit = QStringLiteral("a.u.");
        demo.results = {time, amount};
        // Explicitly anonymous labels avoid importing or inventing clinical
        // dilution/volume parameters. They only exercise selection/default rules.
        demo.sampleRates = {{QStringLiteral("OFFLINE_RATE_A"), true, true, 1},
                            {QStringLiteral("OFFLINE_RATE_B"), true, false, 2}};
        QString message;
        qint64 id = 0;
        ok = ok && writeProject(demo, true, &message, &id);
        if (!ok || !m_database.commit())
        {
            m_database.rollback();
            fail(0, QStringLiteral("initialize"), message.isEmpty() ? sqlError(query) : message);
            release();
            return;
        }
    }
    if (!marked)
    {
        QFile marker(dir.filePath(QString::fromLatin1(markerName)));
        if (!marker.open(QIODevice::WriteOnly) || marker.write("sn100-catalog-offline-v1\n") < 0)
        {
            fail(0, QStringLiteral("initialize"), QStringLiteral("无法写入隔离目录标记。"));
            release();
            return;
        }
    }
    m_ready = true;
    emit ready(databasePath);
}
void ProjectsStore::setSession(AccountSession session)
{
    if (QThread::currentThread() == thread())
    {
        m_session = session;
    }
}
bool ProjectsStore::read(ProjectRecords *projects, ProjectUnits *units, QString *message)
{
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("SELECT unit,ratio FROM option_unit ORDER BY length(unit),unit")))
    {
        *message = sqlError(query);
        return false;
    }
    while (query.next())
    {
        units->append({query.value(0).toString(), query.value(1).toDouble()});
    }
    if (!query.exec(QStringLiteral(
            "SELECT id,project_id,project_en_name,project_cn_name,project_lis_code,is_used,fixed,change_time "
            "FROM project_base ORDER BY id")))
    {
        *message = sqlError(query);
        return false;
    }
    while (query.next())
    {
        ProjectRecord p;
        p.id = query.value(0).toLongLong();
        p.instrumentProjectId = query.value(1).toString();
        p.name = query.value(2).toString();
        p.fullName = query.value(3).toString();
        p.lisCode = query.value(4).toString();
        p.enabled = query.value(5).toBool();
        p.fixed = query.value(6).toBool();
        p.changedAt = query.value(7).toString();
        QSqlQuery results(m_database);
        results.prepare(QStringLiteral(
            "SELECT "
            "result_type,result_name,decimal_point,test_low_range,test_up_range,normal_low_range,normal_up_"
            "range,lis_code,unit_name,main_result FROM project_result WHERE project_id=? ORDER BY rowid"));
        results.addBindValue(p.id);
        if (!results.exec())
        {
            *message = sqlError(results);
            return false;
        }
        while (results.next())
        {
            ProjectResult r;
            r.type = results.value(0).toString();
            r.name = results.value(1).toString();
            r.decimals = results.value(2).toInt();
            r.testLow = results.value(3).toDouble();
            r.testHigh = results.value(4).toDouble();
            r.normalLow = results.value(5).toDouble();
            r.normalHigh = results.value(6).toDouble();
            r.lisCode = results.value(7).toString();
            r.unit = results.value(8).toString();
            r.mainResult = results.value(9).toBool();
            p.results.append(r);
        }
        results.prepare(QStringLiteral("SELECT sample_rate,sample_option,sample_default_option,xuhao FROM "
                                       "project_sample_rate WHERE project_id=? ORDER BY xuhao"));
        results.addBindValue(p.id);
        if (!results.exec())
        {
            *message = sqlError(results);
            return false;
        }
        while (results.next())
        {
            p.sampleRates.append({results.value(0).toString(), results.value(1).toBool(),
                                  results.value(2).toBool(), results.value(3).toInt()});
        }
        results.prepare(QStringLiteral("SELECT version,payload FROM project_advanced WHERE project_id=?"));
        results.addBindValue(p.id);
        if (!results.exec())
        {
            *message = sqlError(results);
            return false;
        }
        if (results.next() && (results.value(0).toInt() != 1 ||
            !parseAdvanced(results.value(1).toString(), &p.advanced)))
        {
            *message = QStringLiteral("高级方案损坏或版本不支持，保留原数据。项目ID=%1").arg(p.id);
            return false;
        }
        QStringList labels;
        for (const auto &rate : p.sampleRates) labels.append(rate.sampleRate);
        const QString invalid = ProjectAdvancedEditor::validate(p.advanced, labels);
        if (!invalid.isEmpty())
        {
            *message = QStringLiteral("已存高级方案未通过校验，保留原数据。项目ID=%1：%2").arg(p.id).arg(invalid);
            return false;
        }
        projects->append(p);
    }
    return true;
}
void ProjectsStore::load(qint64 requestId)
{
    if (!check(requestId, QStringLiteral("load")))
    {
        return;
    }
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    QStringList reagentNames, cleanerNames;
    if (!m_database.transaction())
    {
        fail(requestId, QStringLiteral("load"), m_database.lastError().text());
        return;
    }
    bool ok = read(&projects, &units, &message);
    if (ok && m_database.tables().contains(QStringLiteral("reagent_list")))
    {
        QSqlQuery query(m_database);
        ok = query.exec(QStringLiteral("SELECT reagent_name,reagent_type FROM reagent_list WHERE valid=1 ORDER BY id"));
        if (!ok) message = sqlError(query);
        while (ok && query.next())
        {
            if (query.value(1).toString() == QStringLiteral("试剂")) reagentNames.append(query.value(0).toString());
            if (query.value(1).toString() == QStringLiteral("清洗液")) cleanerNames.append(query.value(0).toString());
        }
    }
    if (!ok || !m_database.commit())
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("load"), message);
        return;
    }
    emit reagentOptionsReady(reagentNames, cleanerNames);
    emit snapshotReady(requestId, projects, units);
}
bool ProjectsStore::writeProject(ProjectRecord p, bool insert, QString *message, qint64 *id)
{
    QSqlQuery query(m_database);
    query.prepare(insert ? QStringLiteral("INSERT INTO "
                                          "project_base(project_id,project_en_name,project_cn_name,project_"
                                          "lis_code,is_used,fixed,change_time) VALUES(?,?,?,?,?,?,?)")
                         : QStringLiteral("UPDATE project_base SET "
                                          "project_id=?,project_en_name=?,project_cn_name=?,project_lis_code="
                                          "?,is_used=?,fixed=?,change_time=? WHERE id=?"));
    query.addBindValue(p.instrumentProjectId);
    query.addBindValue(p.name);
    query.addBindValue(p.fullName);
    query.addBindValue(p.lisCode.isNull() ? QStringLiteral("") : p.lisCode);
    query.addBindValue(p.enabled ? 1 : 0);
    query.addBindValue(p.fixed ? 1 : 0);
    query.addBindValue(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!insert)
    {
        query.addBindValue(p.id);
    }
    if (!query.exec() || (!insert && query.numRowsAffected() != 1))
    {
        *message = sqlError(query);
        return false;
    }
    *id = insert ? query.lastInsertId().toLongLong() : p.id;
    query.prepare(QStringLiteral("DELETE FROM project_result WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec())
    {
        *message = sqlError(query);
        return false;
    }
    for (const ProjectResult &r : p.results)
    {
        query.prepare(QStringLiteral("INSERT INTO project_result VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        query.addBindValue(*id);
        query.addBindValue(r.type);
        query.addBindValue(r.name);
        query.addBindValue(r.decimals);
        query.addBindValue(r.testLow);
        query.addBindValue(r.testHigh);
        query.addBindValue(r.normalLow);
        query.addBindValue(r.normalHigh);
        query.addBindValue(r.lisCode.isNull() ? QStringLiteral("") : r.lisCode);
        query.addBindValue(r.unit);
        query.addBindValue(r.mainResult ? 1 : 0);
        if (!query.exec())
        {
            *message = sqlError(query);
            return false;
        }
    }
    query.prepare(QStringLiteral("DELETE FROM project_sample_rate WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec())
    {
        *message = sqlError(query);
        return false;
    }
    for (const ProjectSampleRate &rate : p.sampleRates)
    {
        query.prepare(QStringLiteral("INSERT INTO project_sample_rate VALUES(?,?,?,?,?)"));
        query.addBindValue(*id);
        query.addBindValue(rate.sampleRate);
        query.addBindValue(rate.sampleOption ? 1 : 0);
        query.addBindValue(rate.defaultOption ? 1 : 0);
        query.addBindValue(rate.order);
        if (!query.exec())
        {
            *message = sqlError(query);
            return false;
        }
    }
    query.prepare(QStringLiteral("DELETE FROM project_advanced WHERE project_id=?"));
    query.addBindValue(*id);
    if (!query.exec())
    {
        *message = sqlError(query);
        return false;
    }
    if (p.advanced.configured)
    {
        query.prepare(QStringLiteral("INSERT INTO project_advanced VALUES(?,1,?)"));
        query.addBindValue(*id);
        query.addBindValue(advancedJson(p.advanced));
        if (!query.exec())
        {
            *message = sqlError(query);
            return false;
        }
    }
    return writeActionReferences(m_database, *id, p.advanced, message);
}
void ProjectsStore::save(qint64 requestId, ProjectRecord project)
{
    if (!check(requestId, QStringLiteral("save"), true))
    {
        return;
    }
    // 当前身份、单位和引用判断必须属于同一次写事务。否则另一写者可在
    // 读取之后改名并建立引用，本次保存却仍按旧名称跳过保护再写回旧值。
    QSqlQuery transaction(m_database);
    if (!transaction.exec(QStringLiteral("BEGIN IMMEDIATE")))
    {
        fail(requestId, QStringLiteral("save"), sqlError(transaction));
        return;
    }
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    if (!read(&projects, &units, &message))
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("save"), message);
        return;
    }
    const auto found = std::find_if(projects.cbegin(), projects.cend(),
                                    [&project](const ProjectRecord &p) { return p.id == project.id; });
    if (found == projects.cend())
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("save"), QStringLiteral("待保存项目已不存在，请刷新。"));
        return;
    }
    // Editing this slice must never mutate instrument identity, fixed status or unsupported result types.
    project.instrumentProjectId = found->instrumentProjectId;
    project.fixed = found->fixed;
    project.sampleRates = found->sampleRates; // Rate labels/defaults stay owned by the existing contract.
    for (auto &action : project.advanced.samples)
        for (const auto &rate : project.sampleRates)
            if (action.value(QStringLiteral("sample_rate")) == rate.sampleRate)
                action.insert(QStringLiteral("sample_option"), rate.sampleOption ? QStringLiteral("1") : QStringLiteral("0"));
    if (project.results.size() != found->results.size())
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("save"), QStringLiteral("本切片不能增删结果类型。"));
        return;
    }
    for (int i = 0; i < project.results.size(); ++i)
    {
        if (project.results[i].type != found->results[i].type)
        {
            m_database.rollback();
            fail(requestId, QStringLiteral("save"), QStringLiteral("结果类型不能修改。"));
            return;
        }
        if (project.results[i].type != QStringLiteral("Calibration") &&
            project.results[i].type != QStringLiteral("Rate"))
        {
            project.results[i].unit = found->results[i].unit;
        }
    }
    message = validate(project, units);
    if (!message.isEmpty())
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("save"), message);
        return;
    }
    qint64 id = 0;
    if (project.name != found->name || project.fullName != found->fullName)
    {
        QSqlQuery reference(m_database);
        reference.prepare(QStringLiteral("SELECT owner_module,reference_key FROM catalog_project_reference WHERE project_id=? LIMIT 1"));
        reference.addBindValue(project.id);
        if (!reference.exec() || reference.next())
        {
            m_database.rollback();
            fail(requestId, QStringLiteral("save"), QStringLiteral("项目被外部记录引用或引用检查失败，不能改名；请先在所属模块解除引用。"));
            return;
        }
    }
    if (!writeProject(project, false, &message, &id) || !m_database.commit())
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("save"), message.isEmpty() ? m_database.lastError().text() : message);
        return;
    }
    emit succeeded(requestId, QStringLiteral("save"), id);
}
void ProjectsStore::copy(qint64 requestId, qint64 sourceId, QString newName)
{
    if (!check(requestId, QStringLiteral("copy"), true))
    {
        return;
    }
    ProjectRecords projects;
    ProjectUnits units;
    QString message;
    if (!read(&projects, &units, &message))
    {
        fail(requestId, QStringLiteral("copy"), message);
        return;
    }
    const auto found = std::find_if(projects.cbegin(), projects.cend(),
                                    [sourceId](const ProjectRecord &p) { return p.id == sourceId; });
    if (found == projects.cend())
    {
        fail(requestId, QStringLiteral("copy"), QStringLiteral("源项目已不存在。"));
        return;
    }
    ProjectRecord project = *found;
    project.id = 0;
    project.name = newName;
    project.fixed = false;
    for (ProjectResult &r : project.results)
    {
        r.name.replace(found->name, newName);
    }
    message = validate(project, units);
    if (!message.isEmpty())
    {
        fail(requestId, QStringLiteral("copy"), message);
        return;
    }
    qint64 id = 0;
    if (!m_database.transaction())
    {
        fail(requestId, QStringLiteral("copy"), m_database.lastError().text());
        return;
    }
    if (!writeProject(project, true, &message, &id) || !m_database.commit())
    {
        m_database.rollback();
        fail(requestId, QStringLiteral("copy"), message.isEmpty() ? m_database.lastError().text() : message);
        return;
    }
    emit succeeded(requestId, QStringLiteral("copy"), id);
}
void ProjectsStore::remove(qint64 requestId, qint64 projectId)
{
    if (!check(requestId, QStringLiteral("remove"), true))
    {
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM project_base WHERE id=? AND fixed=0"));
    query.addBindValue(projectId);
    if (!query.exec())
    {
        fail(requestId, QStringLiteral("remove"),
             QStringLiteral("删除失败；项目可能被试剂/样本引用。%1").arg(sqlError(query)));
        return;
    }
    if (query.numRowsAffected() != 1)
    {
        fail(requestId, QStringLiteral("remove"), QStringLiteral("固定项目不能删除，或项目已不存在。"));
        return;
    }
    emit succeeded(requestId, QStringLiteral("remove"), projectId);
}
void ProjectsStore::release()
{
    m_ready = false;
    if (m_database.isValid())
    {
        m_database.close();
    }
    m_database = QSqlDatabase();
    if (!m_connectionName.isEmpty())
    {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_connectionName.clear();
}
void ProjectsStore::holdProjectReference(qint64 requestId, QString ownerModule, QString referenceKey,
                                         qint64 projectId)
{
    const QString operation = QStringLiteral("holdProjectReference");
    if (QThread::currentThread() != thread() || !m_ready || !m_session.authenticated ||
        !m_session.actionReady || m_session.role < 1 || m_session.role > 5)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("引用登记需要存储线程、已就绪目录及允许业务操作的登录身份。"));
        return;
    }
    static const QRegularExpression module(QStringLiteral("^[a-z][a-z0-9_]{0,31}\\z"));
    if (!module.match(ownerModule).hasMatch() || referenceKey.isEmpty() || referenceKey.size() > 128 ||
        referenceKey != referenceKey.trimmed() || referenceKey.contains(QChar::Null) || projectId <= 0)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("引用所属模块、稳定记录键或项目ID无效。"));
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT OR IGNORE INTO catalog_project_reference VALUES(?,?,?)"));
    query.addBindValue(ownerModule);
    query.addBindValue(referenceKey);
    query.addBindValue(projectId);
    if (!query.exec())
    {
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    query.prepare(QStringLiteral("SELECT project_id FROM catalog_project_reference WHERE owner_module=? AND reference_key=?"));
    query.addBindValue(ownerModule);
    query.addBindValue(referenceKey);
    if (!query.exec() || !query.next() || query.value(0).toLongLong() != projectId)
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("该外部记录键已关联其他项目或核对失败，请先核对所属记录。"));
        return;
    }
    emit projectReferenceSucceeded(requestId, operation);
}
void ProjectsStore::releaseProjectReference(qint64 requestId, QString ownerModule, QString referenceKey)
{
    const QString operation = QStringLiteral("releaseProjectReference");
    static const QRegularExpression module(QStringLiteral("^[a-z][a-z0-9_]{0,31}\\z"));
    if (QThread::currentThread() != thread() || !m_ready || !m_session.authenticated ||
        !m_session.actionReady || m_session.role < 1 || m_session.role > 5 ||
        !module.match(ownerModule).hasMatch() || referenceKey.isEmpty() || referenceKey.size() > 128 ||
        referenceKey != referenceKey.trimmed() || referenceKey.contains(QChar::Null))
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("引用释放条件无效；须先确认所属外部记录已删除或保存失败。"));
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM catalog_project_reference WHERE owner_module=? AND reference_key=?"));
    query.addBindValue(ownerModule);
    query.addBindValue(referenceKey);
    if (!query.exec())
    {
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    emit projectReferenceSucceeded(requestId, operation);
}
void ProjectsStore::checkProjectReferences(qint64 requestId, qint64 projectId)
{
    const QString operation = QStringLiteral("checkProjectReferences");
    if (QThread::currentThread() != thread() || !m_ready || projectId <= 0 || !m_database.transaction())
    {
        emit projectReferenceError(requestId, operation, QStringLiteral("项目引用检查不可用，禁止假定未引用。"));
        return;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT id FROM project_base WHERE id=?"));
    query.addBindValue(projectId);
    if (!query.exec())
    {
        m_database.rollback();
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    const bool exists = query.next();
    query.prepare(QStringLiteral("SELECT owner_module,reference_key FROM catalog_project_reference WHERE project_id=? ORDER BY owner_module,reference_key"));
    query.addBindValue(projectId);
    QStringList references;
    if (!query.exec())
    {
        m_database.rollback();
        emit projectReferenceError(requestId, operation, sqlError(query));
        return;
    }
    while (query.next()) references.append(query.value(0).toString() + QStringLiteral(":") + query.value(1).toString());
    if (query.lastError().isValid())
    {
        const QString message = sqlError(query);
        query.finish();
        m_database.rollback();
        emit projectReferenceError(requestId, operation, message);
        return;
    }
    query.finish();
    if (!m_database.commit())
    {
        m_database.rollback();
        emit projectReferenceError(requestId, operation, m_database.lastError().text());
        return;
    }
    emit projectReferencesReady(requestId, projectId, exists, references);
}
void ProjectsStore::close()
{
    if (QThread::currentThread() != thread())
    {
        return;
    }
    release();
    m_session = AccountSession();
    emit closed();
}
