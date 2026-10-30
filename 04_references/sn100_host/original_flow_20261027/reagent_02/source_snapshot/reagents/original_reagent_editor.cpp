#include "original_reagent_editor.h"
#include <QDateTime>
#include <QDir>
#include <QLockFile>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUrl>
#include <cmath>

using namespace OriginalBusiness;
namespace
{
const QString informationFile = QStringLiteral("db/userSet/reagent_Information.db");
const QString bottleFile = QStringLiteral("db/systemInfo/reagent_Bottle_Type.db");
const QString settingFile = QStringLiteral("db/userSet/reagent_Setting.db");
const QString isiFile = QStringLiteral("db/userSet/isi_Value.db");
const QStringList storedTypes{" ","r_reagent","s_ibs","cleaner","s_control","s_calibrator","s_Factor"};
const QStringList shownTypes{QStringLiteral("无"),QStringLiteral("试剂"),QStringLiteral("缓冲稀释液"),QStringLiteral("清洗液"),
                             QStringLiteral("质控品"),QStringLiteral("校准品"),QStringLiteral("因子血浆")};
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
const Row *byId(const Rows &rows, qint64 id)
{
    for (const auto &row : rows) if (row.value("id").toLongLong()==id) return &row;
    return nullptr;
}
qint64 idByName(const Rows &rows, const QString &key, const QString &name)
{
    for (const auto &row : rows) if (row.value(key).toString()==name) return row.value("id").toLongLong();
    return 0;
}
bool nameValid(const QString &name, int maximum=64)
{
    if (name.isEmpty() || name!=name.trimmed() || name.size()>maximum) return false;
    for (const auto c : name) if (c.unicode()<32 || c=='[' || c==']' || c=='|' || c=='/' || c=='\\') return false;
    return true;
}
bool allRows(Database &database, const QString &table, Rows *rows, QString *message)
{
    QSqlQuery query(database.db);
    if (!query.exec(QStringLiteral("SELECT * FROM \"%1\" ORDER BY id").arg(table))) return fail(message,query.lastError().text());
    const auto columns=query.record();
    rows->clear();
    while (query.next())
    {
        if (rows->size()>=100000) return fail(message,QStringLiteral("原试剂资料超过10万行，未截断。"));
        Row row;
        for (int i=0;i<columns.count();++i) row.insert(columns.fieldName(i),query.value(i));
        rows->append(row);
    }
    return !query.lastError().isValid() || fail(message,query.lastError().text());
}
void changedInteger(QVariantMap *values, const Row *before, const QString &key, int value)
{
    values->insert(key,before && before->value(key).toInt()==value ? before->value(key) : QVariant(value));
}
QString displayedType(const QString &stored)
{
    const int index=storedTypes.indexOf(stored);
    return index<0 ? stored : shownTypes[index];
}
bool attachIsi(Database &database, const QString &root, QString *message)
{
    // 与主库一致，附加库也必须已存在，不能在检查后被移走时另建空库。
    const QString uri=QUrl::fromLocalFile(QDir(root).filePath(isiFile)).toString(QUrl::FullyEncoded)+QStringLiteral("?mode=rw");
    if (!database.execute("ATTACH DATABASE ? AS isi",{uri},message)) return false;
    for (const auto &schema : {QStringLiteral("main"),QStringLiteral("isi")})
    {
        QSqlQuery mode(database.db);
        if (!mode.exec("PRAGMA "+schema+".journal_mode") || !mode.next()) return fail(message,mode.lastError().text());
        if (!QStringList{"delete","truncate","persist"}.contains(mode.value(0).toString().toLower()))
            return fail(message,QStringLiteral("原组表日志模式不支持跨文件共同恢复，未改模式或保存部分数据。"));
    }
    return true;
}
}

bool OriginalReagentEditor::initialize(const QString &root, QString *message)
{
    if (!repository_.initialize(root,message)) return false;
    root_=root;
    ReagentSnapshot snapshot; ReagentProjects projects;
    return load(&snapshot,&projects,message);
}

bool OriginalReagentEditor::load(ReagentSnapshot *snapshot, ReagentProjects *choices, QString *message)
{
    OriginalReagentState observed;
    if (!snapshot || !choices || !repository_.load(&observed,message)) return false;
    OriginalProjectRepository projects;
    ProjectRecords records; ProjectUnits units; QStringList reagents,cleaners,diluents;
    if (!projects.initialize(root_,QString(),message) || !projects.load(&records,&units,&reagents,&cleaners,&diluents,message)) return false;
    Database isi;
    Rows isiRows;
    const QString isiVersion=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(isiFile),message);
    if (isiVersion.isEmpty() || !isi.open(root_,isiFile,false,message) || !allRows(isi,"isi_value",&isiRows,message)) return false;
    ReagentSnapshot result; result.originalProfile=true; result.originalRoot=root_;
    ReagentProjects projectChoices;
    QMap<QString,QString> versions=observed.versions;
    versions.insert(isiFile,isiVersion);
    for (const auto &project : records)
    {
        projectChoices.append({project.id,project.name,QString(),project.enabled});
        versions.insert(QStringLiteral("project/")+project.originalFileName,project.originalFingerprint);
    }
    for (const auto &row : observed.reagents)
    {
        ReagentRecord r;
        r.id=row.value("id").toLongLong(); r.name=row.value("reagent_name").toString(); r.type=displayedType(row.value("reagent_type").toString());
        r.manufacturer=row.value("manufacturer").toString(); r.warningUl=row.value("warning_ul").toInt(); r.outUl=row.value("out_ul").toInt();
        r.valid=row.value("valid").toBool(); r.checkExpiry=row.value("check_limit").toBool(); r.fixed=row.value("fixed").toBool();
        r.originalValues=row; r.originalFingerprint=versions.value(informationFile); result.reagents.append(r);
    }
    for (const auto &row : observed.bottles)
    {
        ReagentBottle b;
        b.id=row.value("id").toLongLong(); b.name=row.value("bottle_type_name").toString(); b.area=row.value("area").toString();
        b.capacityUl=row.value("max_amount_ul").toInt(); b.heightMm=row.value("bottle_real_height_mm").toDouble();
        b.originalValues=row; b.originalFingerprint=versions.value(bottleFile); result.bottles.append(b);
    }
    for (const auto &row : observed.batchGroups)
    {
        ReagentBatch b; b.id=row.value("id").toLongLong(); b.code=b.batchGroup=row.value("batch_group").toString();
        for (const auto &project : records) if (project.name==row.value("project_name").toString()) b.projectId=project.id;
        for (const auto &metadata : isiRows) if (metadata.value("batch_group").toString()==b.batchGroup)
        { b.hasMetadata=true; b.isiValue=metadata.value("isi_value").toString(); b.normalValue=metadata.value("normal_value").toString(); }
        b.originalValues=row; b.originalFingerprint=versions.value(informationFile); result.batches.append(b);
    }
    for (const auto &row : observed.barcodes)
    {
        ReagentBarcode b; b.id=row.value("id").toLongLong(); b.code=row.value("reagent_barcode").toString();
        b.reagentId=idByName(observed.reagents,"reagent_name",row.value("reagent_name").toString());
        b.bottleId=idByName(observed.bottles,"bottle_type_name",row.value("bottle_type").toString());
        b.expiry=row.value("reagent_limit_date").toString(); b.originalValues=row; b.originalFingerprint=versions.value(informationFile);
        result.barcodes.append(b);
    }
    for (const auto &row : observed.holes)
    {
        ReagentHole h; h.id=row.value("id").toLongLong(); h.name=row.value("hole_name").toString().toUpper(); h.area=row.value("area").toString();
        h.reagentId=idByName(observed.reagents,"reagent_name",row.value("reagent_name").toString());
        h.bottleId=idByName(observed.bottles,"bottle_type_name",row.value("bottle_type").toString());
        h.originalBatch=row.value("reagent_batch").toString(); h.expiry=row.value("out_date").toString();
        h.amountUl=row.value("amount_uL").toDouble(); h.remainingUl=row.value("used_ul").toDouble(); h.priority=row.value("priority").toInt();
        h.changedAt=row.value("datetime").toString(); h.originalValues=row; h.originalFingerprint=versions.value(settingFile); result.holes.append(h);
    }
    for (auto it=versions.cbegin();it!=versions.cend();++it)
        if (it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),message))
            return fail(message,QStringLiteral("读取期间原试剂或项目资料变化，请重新读取。"));
    state_=observed; isiRows_=isiRows; projects_=records; versions_=versions;
    *snapshot=result; *choices=projectChoices;
    return true;
}

bool OriginalReagentEditor::mutate(const QString &file, const QString &expected,
    const std::function<bool(Database &,QString *)> &change, QString *message)
{
    QLockFile projectLock(QDir(root_).filePath(QStringLiteral(".sn100-original-project-write.lock")));
    QLockFile reagentLock(QDir(root_).filePath(QStringLiteral(".sn100-original-reagent-write.lock")));
    if (!projectLock.tryLock(0) || !reagentLock.tryLock(0)) return fail(message,QStringLiteral("原项目或试剂正在保存，请稍后重试，草稿保留。"));
    if (!expected.isEmpty() && expected!=versions_.value(file)) return fail(message,QStringLiteral("试剂草稿已过期，请重新读取后编辑。"));
    for (auto it=versions_.cbegin();it!=versions_.cend();++it)
        if (it.value()!=OriginalProjectRepository::databaseFingerprint(QDir(root_).filePath(it.key()),message))
            return fail(message,QStringLiteral("原试剂或项目已被其他操作修改，本次没有覆盖，请重新读取。"));
    Database database;
    if (!database.open(root_,file,true,message) || !database.begin(message)) return false;
    if (!change(database,message)) { database.rollback(); return false; }
    return database.commit(message);
}

bool OriginalReagentEditor::saveRow(Database &database, const QString &table, qint64 id, QVariantMap values, QString *message)
{
    values.remove("id");
    // 只更新本页明确负责的列，不删表重建，也不覆盖未知厂商列。
    QStringList columns,places,assignments; QVariantList arguments;
    for (auto it=values.cbegin();it!=values.cend();++it)
    {
        if (!QRegularExpression("^[A-Za-z_][A-Za-z0-9_]*$").match(it.key()).hasMatch()) return fail(message,QStringLiteral("试剂列名非法。"));
        const QString key='"'+it.key()+'"'; columns.append(key); places.append("?"); assignments.append(key+"=?"); arguments.append(it.value());
    }
    if (id)
    {
        arguments.append(id);
        return database.execute(QStringLiteral("UPDATE \"%1\" SET %2 WHERE id=?").arg(table,assignments.join(',')),arguments,message,1);
    }
    return database.execute(QStringLiteral("INSERT INTO \"%1\"(%2) VALUES(%3)").arg(table,columns.join(','),places.join(',')),arguments,message,1);
}

bool OriginalReagentEditor::projectUses(const QString &name, bool, QString *message) const
{
    for (const auto &project : projects_)
    {
        if (project.originalBaseValues.value("IBS_name")==name) return true;
        Database database;
        if (!database.open(root_,QStringLiteral("project/")+project.originalFileName,false,message)) return true;
        for (const auto &table : {QStringLiteral("project_sample_rate"),QStringLiteral("project_reagent")})
        {
            QSqlQuery query(database.db);
            if (!query.exec(QStringLiteral("SELECT * FROM ")+table)) { fail(message,query.lastError().text()); return true; }
            const auto columns=query.record();
            while (query.next())
                for (int i=0;i<columns.count();++i)
                    if (columns.fieldName(i).endsWith("_name") && query.value(i).toString()==name) return true;
        }
    }
    return false;
}

bool OriginalReagentEditor::saveReagent(const ReagentRecord &r, QString *message)
{
    const int type=shownTypes.indexOf(r.type);
    if (!nameValid(r.name,16) || type<0 || r.outUl<0 || r.warningUl<r.outUl || r.warningUl>9999)
        return fail(message,QStringLiteral("试剂名称、类型或警告量/缺少量不合法。"));
    const Row *before=byId(state_.reagents,r.id);
    const QString oldName=before ? before->value("reagent_name").toString() : QString();
    const bool changed=before && (oldName!=r.name || before->value("reagent_type").toString()!=storedTypes[type] || !r.valid);
    if (changed)
    {
        bool referenced=projectUses(oldName,false,message);
        for (const auto &row : state_.holes) if (row.value("reagent_name").toString()==oldName) referenced=true;
        for (const auto &row : state_.barcodes) if (row.value("reagent_name").toString()==oldName) referenced=true;
        if (referenced) return fail(message,QStringLiteral("原项目、孔位或条码仍引用该试剂，不能改名、改类型或停用；先解除引用。"));
    }
    for (const auto &row : state_.reagents)
        if (row.value("id").toLongLong()!=r.id && row.value("reagent_name").toString()==r.name) return fail(message,QStringLiteral("原库已有同名试剂。"));
    QVariantMap values{{"reagent_name",r.name},{"reagent_type",storedTypes[type]},{"manufacturer",r.manufacturer},
                       {"valid",r.valid ? "1":"0"},{"check_limit",r.checkExpiry ? "1":"0"}};
    changedInteger(&values,before,"warning_ul",r.warningUl); changedInteger(&values,before,"out_ul",r.outUl);
    for (const auto &key : {"banding_needle","dect_clean","cleaner_name"})
        if (r.originalValues.contains(key)) values.insert(key,r.originalValues.value(key));
    if (!r.id)
        for (const auto &key : {"reagent_code","ref_use_ul","useage_ul","fixed"}) values.insert(key,r.originalValues.value(key,QStringLiteral("0")));
    return mutate(informationFile,r.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,"reagent_list",r.id,values,m); },message);
}

bool OriginalReagentEditor::saveBottle(const ReagentBottle &b, QString *message)
{
    if (!nameValid(b.name,32) || (b.area!="r_area" && b.area!="s_area") || b.capacityUl<1 || b.capacityUl>100000 || !std::isfinite(b.heightMm) || b.heightMm<0 || b.heightMm>500)
        return fail(message,QStringLiteral("瓶型名称、区域、容量或高度不合法。"));
    const Row *before=byId(state_.bottles,b.id);
    if (before)
    {
        const QString name=before->value("bottle_type_name").toString();
        for (const auto &row : state_.holes) if (row.value("bottle_type").toString()==name) return fail(message,QStringLiteral("瓶型已被原孔位引用，请先解除配置。"));
        for (const auto &row : state_.barcodes) if (row.value("bottle_type").toString()==name) return fail(message,QStringLiteral("瓶型已被原条码引用，请先解除配置。"));
    }
    for (const auto &row : state_.bottles)
        if (row.value("id").toLongLong()!=b.id && row.value("bottle_type_name").toString()==b.name && row.value("area").toString()==b.area)
            return fail(message,QStringLiteral("该区域已有同名瓶型。"));
    QVariantMap values{{"bottle_type_name",b.name},{"area",b.area},{"max_amount_ul",b.capacityUl},{"bottle_real_height_mm",b.heightMm}};
    if (!b.id) for (const auto &key : {"is_adpter","bottom_diameter","angle"}) values.insert(key,b.originalValues.value(key,QStringLiteral("0")));
    return mutate(bottleFile,b.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,"reagent_bottle_type",b.id,values,m); },message);
}

bool OriginalReagentEditor::saveBarcode(const ReagentBarcode &b, QString *message)
{
    const Row *reagent=byId(state_.reagents,b.reagentId), *bottle=byId(state_.bottles,b.bottleId);
    QDate expiry=QDate::fromString(b.expiry,Qt::ISODate);
    if (!expiry.isValid()) expiry=QDate::fromString(b.expiry,"yyyy/MM/dd");
    if (!reagent || !bottle || !nameValid(b.code,128) || !expiry.isValid()) return fail(message,QStringLiteral("原条码、试剂、瓶型或有效期不合法。"));
    for (const auto &row : state_.barcodes) if (row.value("id").toLongLong()!=b.id && row.value("reagent_barcode").toString()==b.code)
        return fail(message,QStringLiteral("原库已有相同条码。"));
    QVariantMap values{{"reagent_name",reagent->value("reagent_name")},{"reagent_barcode",b.code},{"reagent_limit_date",b.expiry},{"bottle_type",bottle->value("bottle_type_name")}};
    if (!b.id) values.insert("isi_value",b.originalValues.value("isi_value",QString()));
    return mutate(informationFile,b.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,"reagent_barcode",b.id,values,m); },message);
}

bool OriginalReagentEditor::saveHole(const ReagentHole &h, QString *message)
{
    const Row *before=byId(state_.holes,h.id), *reagent=byId(state_.reagents,h.reagentId), *bottle=byId(state_.bottles,h.bottleId);
    if (!before || h.name!=before->value("hole_name").toString().toUpper() || !reagent || !bottle || !reagent->value("valid").toBool() ||
        h.area!=before->value("area").toString() || h.area!=bottle->value("area").toString() || !std::isfinite(h.amountUl) || !std::isfinite(h.remainingUl) ||
        h.amountUl<0 || h.remainingUl<0 || h.remainingUl>h.amountUl || h.amountUl>bottle->value("max_amount_ul").toDouble() || h.priority<0 || h.priority>999)
        return fail(message,QStringLiteral("原孔位身份、试剂、瓶型区域或液量不合法，未改写。"));
    const QString type=reagent->value("reagent_type").toString();
    if (type!="cleaner" && (h.area=="r_area" ? type!="r_reagent" : type!="s_ibs")) return fail(message,QStringLiteral("该试剂类型不能配置到所选区域。"));
    QDate expiry=QDate::fromString(h.expiry,Qt::ISODate); if (!expiry.isValid()) expiry=QDate::fromString(h.expiry,"yyyy/MM/dd");
    if (!expiry.isValid()) return fail(message,QStringLiteral("原孔位有效期格式无效。"));
    const QString batch=h.originalBatch.isEmpty() ? QStringLiteral("Default") : h.originalBatch;
    if (!nameValid(batch,128)) return fail(message,QStringLiteral("试剂批号格式无效。"));
    QVariantMap values{{"reagent_type",type},{"reagent_name",reagent->value("reagent_name")},{"reagent_batch",batch},
        {"bottle_type",bottle->value("bottle_type_name")},{"amount_uL",h.amountUl},{"used_ul",h.remainingUl},{"priority",h.priority},
        {"out_date",h.expiry},{"datetime",QDateTime::currentDateTime().toString("yyyy/MM/dd HH:mm:ss")},{"state","r_normal"},
        {"out_ul",reagent->value("out_ul")},{"warning_ul",reagent->value("warning_ul")},{"max_ul",bottle->value("max_amount_ul")}};
    return mutate(settingFile,h.originalFingerprint,[&](Database &db,QString *m){ return saveRow(db,state_.settingTable,h.id,values,m); },message);
}

bool OriginalReagentEditor::clearHole(const QString &name, QString *message)
{
    qint64 id=0;
    for (const auto &row : state_.holes) if (row.value("hole_name").toString().compare(name,Qt::CaseInsensitive)==0) id=row.value("id").toLongLong();
    if (!id) return fail(message,QStringLiteral("原孔位不存在，未创建或删除孔位行。"));
    QVariantMap values{{"reagent_type"," "},{"reagent_name","none"},{"reagent_batch","Default"},{"bottle_type","none"},
                       {"amount_uL","0"},{"used_ul","0"},{"state","r_none"},{"out_date",""},{"datetime",""}};
    return mutate(settingFile,QString(),[&](Database &db,QString *m){ return saveRow(db,state_.settingTable,id,values,m); },message);
}

bool OriginalReagentEditor::saveBatch(const ReagentBatch &b, QString *message)
{
    QString projectName;
    for (const auto &project : projects_) if (project.id==b.projectId && project.enabled) projectName=project.name;
    const Row *before=byId(state_.batchGroups,b.id);
    if (projectName.isEmpty() || b.batchGroup.isEmpty() || b.batchGroup.size()>256)
        return fail(message,QStringLiteral("请先选择有效原项目并设置试剂批号组。"));
    QSet<QString> seen;
    for (const auto &member : b.batchGroup.split('|',Qt::KeepEmptyParts))
    {
        if (!nameValid(member,32) || seen.contains(member)) return fail(message,QStringLiteral("试剂组含空批号、重复批号或非法文字。"));
        seen.insert(member);
    }
    const QString oldGroup=before ? before->value("batch_group").toString() : QString();
    if (before && (oldGroup=="Default" || oldGroup=="None" || oldGroup=="PTFbg") && oldGroup!=b.batchGroup)
        return fail(message,QStringLiteral("原内置试剂组不能改名。"));
    for (const auto &row : state_.batchGroups)
        if (row.value("id").toLongLong()!=b.id && row.value("project_name").toString()==projectName && row.value("batch_group").toString()==b.batchGroup)
            return fail(message,QStringLiteral("原项目已有相同试剂组。"));
    const QRegularExpression decimal("^(0|[1-9][0-9]{0,3})(\\.[0-9]{1,4})?$");
    if (b.metadataEdited && (!decimal.match(b.isiValue).hasMatch() || !decimal.match(b.normalValue).hasMatch() ||
                             b.isiValue.toDouble()>9999 || b.normalValue.toDouble()>9999))
        return fail(message,QStringLiteral("ISI及正常测定值须同时填写，范围0至9999、最多4位小数。"));
    return mutate(informationFile,b.originalFingerprint,[&](Database &db,QString *m)
    {
        // 原组表和isi_Value.db分开；同一SQLite连接提交，避免只改一半的组名/参数。
        if (!attachIsi(db,root_,m)) return false;
        if (!saveRow(db,"reagent_batch_group",b.id,{{"project_name",projectName},{"batch_group",b.batchGroup}},m)) return false;
        const Row *metadata=nullptr;
        for (const auto &row : isiRows_) if (row.value("batch_group").toString()==(oldGroup.isEmpty() ? b.batchGroup : oldGroup))
        { if (metadata) return fail(m,QStringLiteral("原ISI表存在同名重复记录，未选择其中一条。")); metadata=&row; }
        if (metadata)
        {
            const QString isi=b.metadataEdited ? b.isiValue : metadata->value("isi_value").toString();
            const QString normal=b.metadataEdited ? b.normalValue : metadata->value("normal_value").toString();
            return db.execute("UPDATE isi.isi_value SET batch_group=?,isi_value=?,normal_value=? WHERE id=?",
                              {b.batchGroup,isi,normal,metadata->value("id")},m,1);
        }
        if (b.metadataEdited)
            return db.execute("INSERT INTO isi.isi_value(batch_group,isi_value,normal_value) VALUES(?,?,?)",{b.batchGroup,b.isiValue,b.normalValue},m,1);
        return true;
    },message);
}

bool OriginalReagentEditor::remove(const QString &kind, qint64 id, QString *message)
{
    QString file=informationFile,table;
    if (kind=="reagent")
    {
        table="reagent_list"; const Row *row=byId(state_.reagents,id);
        if (!row || row->value("fixed").toBool()) return fail(message,QStringLiteral("内置试剂或不存在的记录不能删除。"));
        const QString name=row->value("reagent_name").toString();
        if (projectUses(name,false,message)) return fail(message,QStringLiteral("原项目仍引用该试剂，请先解除引用。"));
        for (const auto &hole : state_.holes) if (hole.value("reagent_name").toString()==name) return fail(message,QStringLiteral("原孔位仍引用该试剂。"));
        for (const auto &barcode : state_.barcodes) if (barcode.value("reagent_name").toString()==name) return fail(message,QStringLiteral("原条码仍引用该试剂。"));
    }
    else if (kind=="bottle")
    {
        file=bottleFile; table="reagent_bottle_type"; const Row *row=byId(state_.bottles,id);
        if (!row) return fail(message,QStringLiteral("原瓶型不存在。"));
        const QString name=row->value("bottle_type_name").toString();
        for (const auto &hole : state_.holes) if (hole.value("bottle_type").toString()==name) return fail(message,QStringLiteral("原孔位仍引用该瓶型。"));
        for (const auto &barcode : state_.barcodes) if (barcode.value("bottle_type").toString()==name) return fail(message,QStringLiteral("原条码仍引用该瓶型。"));
    }
    else if (kind=="barcode") table="reagent_barcode";
    else if (kind=="batch")
    {
        const Row *row=byId(state_.batchGroups,id);
        if (!row) return fail(message,QStringLiteral("原试剂组不存在。"));
        const QString group=row->value("batch_group").toString(),projectName=row->value("project_name").toString();
        if (group=="Default" || group=="None" || group=="PTFbg") return fail(message,QStringLiteral("原内置试剂组不能删除。"));
        for (const auto &project : projects_)
            if (project.name==projectName && project.originalBaseValues.value("is_related").toInt()>0)
            {
                int others=0;
                for (const auto &candidate : state_.batchGroups)
                    if (candidate.value("id").toLongLong()!=id && candidate.value("project_name").toString()==projectName &&
                        candidate.value("batch_group").toString()!="Default" && candidate.value("batch_group").toString()!="PTFbg") ++others;
                if (!others) return fail(message,QStringLiteral("项目已启用组分管理，最后一个实际试剂组不能删除。"));
            }
        // 原校准库按项目/批组名关联，不能仅删除组表留下悬空引用。
        Database calibration;
        if (!calibration.open(root_,"result/calibration.db",false,message)) return false;
        QSqlQuery references(calibration.db);
        if (!references.prepare("SELECT 1 FROM calibration_batch_group WHERE project_name=? AND batch_group=? LIMIT 1")) return fail(message,references.lastError().text());
        references.addBindValue(projectName); references.addBindValue(group);
        if (!references.exec()) return fail(message,references.lastError().text());
        if (references.next()) return fail(message,QStringLiteral("原校准记录仍引用该试剂组，请先解除引用。"));
        return mutate(informationFile,QString(),[&](Database &db,QString *m)
        {
            if (!db.execute("DELETE FROM reagent_batch_group WHERE id=?",{id},m,1)) return false;
            int remaining=0;
            for (const auto &other : state_.batchGroups) if (other.value("id").toLongLong()!=id && other.value("batch_group").toString()==group) ++remaining;
            if (remaining) return true;
            if (!attachIsi(db,root_,m)) return false;
            return db.execute("DELETE FROM isi.isi_value WHERE batch_group=?",{group},m);
        },message);
    }
    else return fail(message,QStringLiteral("原试剂删除类型不支持。"));
    return mutate(file,QString(),[&](Database &db,QString *m){ return db.execute(QStringLiteral("DELETE FROM %1 WHERE id=?").arg(table),{id},m,1); },message);
}
