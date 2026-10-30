#include "calibration_store.h"
#include "original_sql.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QLockFile>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace Sn100OriginalSql;
namespace
{
const QStringList files{QStringLiteral("result/calibration.db"),QStringLiteral("db/userSet/isi_Value.db"),QStringLiteral("db/userSet/reagent_Information.db")};
qint64 revisionOf(const QString &version) { return version.left(15).toLongLong(nullptr,16)+1; }
QString number(double value) { return QString::number(value,'g',17); }
bool sameNumber(double a,double b) { return a==b || (std::isnan(a)&&std::isnan(b)); }
bool sameMeasurements(const QList<double> &a,const QList<double> &b)
{if(a.size()!=b.size())return false;for(int n=0;n<a.size();++n)if(!sameNumber(a[n],b[n]))return false;return true;}
bool updateFields(QSqlDatabase db,const QString &table,qint64 id,const QVariantMap &values,QString *message)
{
    QSqlQuery q(db);if(!query(&q,"SELECT * FROM "+table+" WHERE id=?",{id},message)||!q.next())return Sn100OriginalSql::error(message,QStringLiteral("原校准记录已不存在，请重新读取。"));
    const auto before=row(q);q.finish();QStringList sets;QVariantList bindings;
    for(auto it=values.cbegin();it!=values.cend();++it)if(before.value(it.key()).toString()!=it.value().toString())
    {sets<<('"'+it.key()+QStringLiteral("\"=?"));bindings<<it.value();}
    if(sets.isEmpty())return true;bindings<<id;
    return execute(db,"UPDATE "+table+" SET "+sets.join(',')+" WHERE id=?",bindings,message,1);
}
bool insertFields(QSqlDatabase db,const QString &table,QVariantMap fields,qint64 *id,QString *message)
{
    fields.remove("id");QStringList columns,marks;QVariantList bindings;
    for(auto it=fields.cbegin();it!=fields.cend();++it){columns<<('"'+it.key()+'"');marks<<"?";bindings<<it.value();}
    QSqlQuery q(db);if(!query(&q,"INSERT INTO "+table+"("+columns.join(',')+") VALUES("+marks.join(',')+")",bindings,message))return false;
    *id=q.lastInsertId().toLongLong();return *id>0;
}
bool allocateCurve(QSqlDatabase db,qint64 *id,QString *message)
{
    QSqlQuery q(db);if(!query(&q,"SELECT id,total_count FROM calibraiton_total_count ORDER BY id",{},message)||!q.next())
        return Sn100OriginalSql::error(message,QStringLiteral("原校准编号计数不存在，未另造编号表。"));
    const qint64 rowId=q.value(0).toLongLong();*id=q.value(1).toLongLong();
    if(q.next()||*id<=0){q.finish();return Sn100OriginalSql::error(message,QStringLiteral("原校准编号计数不唯一或无效，未自动修复。"));}q.finish();
    if(!query(&q,"SELECT 1 FROM calibration_base WHERE calibration_id=?",{*id},message))return false;
    if(q.next()){q.finish();return Sn100OriginalSql::error(message,QStringLiteral("原校准编号计数与已有曲线冲突，未覆盖。"));}q.finish();
    return execute(db,"UPDATE calibraiton_total_count SET total_count=? WHERE id=?",{*id+1,rowId},message,1);
}
}

void CalibrationStore::initializeOriginal(QString rootPath)
{
    if(QThread::currentThread()!=thread()||QThread::currentThread()==QCoreApplication::instance()->thread())
    {fail(0,"initializeOriginal",QStringLiteral("原校准库必须在数据库工作线程打开。"));return;}
    QString message;if(!root(&rootPath,&message)){fail(0,"initializeOriginal",message);return;}
    release();m_originalRoot=rootPath;
    if(!open(&m_database,&m_connectionName,QDir(rootPath).filePath(files[0]),&message))
    {fail(0,"initializeOriginal",message);release();return;}
    QSqlQuery q(m_database);
    for(int index=1;index<files.size();++index)
    {
        const QString path=QDir(rootPath).filePath(files[index]);
        if(!regular(path)||!query(&q,QStringLiteral("ATTACH DATABASE ? AS ")+(index==1?"original_isi":"original_reagent"),{existingUri(path)},&message))
        {fail(0,"initializeOriginal",message.isEmpty()?QStringLiteral("原校准关联库不存在，未创建替代库。"):message);release();return;}
    }
    if(!columns(m_database,"calibration_base",{"id","calibration_id","project_name","calibration_datetime","boold_batch","point_size","repeat_number","calculate_id","is_checked","is_edited","result_unit","con_unit","x_change","y_change","visible"},&message)||
       !columns(m_database,"calibration_data",{"id","calibration_id","point_index","sample_rate","concentration","result_flag","result_value","only_count"},&message)||
       !columns(m_database,"calibration_batch_group",{"id","project_name","batch_group","calibration_id"},&message)||
       !columns(m_database,"calibraiton_total_count",{"id","total_count"},&message)||
       !columns(m_database,"original_isi.isi_value",{"id","batch_group","isi_value","normal_value"},&message))
    {fail(0,"initializeOriginal",message);release();return;}
    m_originalMode=true;m_ready=true;
    emit originalSourceReady(rootPath);emit ready(QDir(rootPath).filePath(files[0]));
}
void CalibrationStore::setOriginalCatalog(ProjectRecords projects,ProjectUnits units,ReagentSnapshot reagents)
{
    if(QThread::currentThread()!=thread())return;
    m_originalProjects=projects;m_originalUnits=units;m_originalReagents=reagents;
}
bool CalibrationStore::beginOriginal(qint64 requestId,const QString &operation,QString *message)
{
    if(!check(requestId,operation,true))return false;
    if(m_session.originalPolicySource!=m_originalRoot||!m_session.grantedPowerIds.contains(QStringLiteral("1")))
    {fail(requestId,operation,QStringLiteral("当前会话不是该原工作根授予的校准权限，未按离线角色高低补权。"));return false;}
    if(requestId<=0||requestId<=m_originalLastWrite){fail(requestId,operation,QStringLiteral("重复或迟到的原校准写请求未执行。"));return false;}
    m_originalLastWrite=requestId;
    // 与项目/试剂维护按相同顺序持锁，避免跨模块改批组或ISI后仍用旧草稿覆盖。
    m_originalProjectLock.reset(new QLockFile(QDir(m_originalRoot).filePath(".sn100-original-project-write.lock")));
    m_originalReagentLock.reset(new QLockFile(QDir(m_originalRoot).filePath(".sn100-original-reagent-write.lock")));
    if(!m_originalProjectLock->tryLock(0)||!m_originalReagentLock->tryLock(0))
    {m_originalReagentLock.reset();m_originalProjectLock.reset();fail(requestId,operation,QStringLiteral("原项目或试剂资料正在写入，请保留草稿后重试。"));return false;}
    if(m_originalFingerprint.isEmpty()||fingerprint(m_originalRoot,files,message)!=m_originalFingerprint)
    {m_originalReagentLock.reset();m_originalProjectLock.reset();fail(requestId,operation,QStringLiteral("原校准/ISI/试剂资料在读取后已变化，请重读核对。"));return false;}
    if(!execute(m_database,"BEGIN IMMEDIATE",{},message))
    {m_originalReagentLock.reset();m_originalProjectLock.reset();fail(requestId,operation,*message);return false;}
    if(fingerprint(m_originalRoot,files,message)!=m_originalFingerprint)
    {rejectOriginal(requestId,operation,QStringLiteral("写事务开始时原资料已变化，未覆盖。"));return false;}
    return true;
}
void CalibrationStore::rejectOriginal(qint64 requestId,const QString &operation,const QString &message)
{
    m_database.rollback();m_originalReagentLock.reset();m_originalProjectLock.reset();fail(requestId,operation,message);
}
void CalibrationStore::finishOriginal(qint64 requestId,const QString &operation,qint64 id)
{
    if(!m_database.commit()){rejectOriginal(requestId,operation,m_database.lastError().text());return;}
    QString message;m_originalFingerprint=fingerprint(m_originalRoot,files,&message);
    m_originalReagentLock.reset();m_originalProjectLock.reset();
    if(m_originalFingerprint.isEmpty()){fail(requestId,operation,QStringLiteral("原校准已提交但版本回读失败，请重新读取确认。"));return;}
    emit succeeded(requestId,operation,id);
}
bool CalibrationStore::readOriginal(CalibrationRecords *records,QString *message,bool includeHidden)
{
    records->clear();const QString version=fingerprint(m_originalRoot,files,message);if(version.isEmpty())return false;
    QSqlQuery q(m_database);
    if(!query(&q,QStringLiteral("SELECT * FROM calibration_base")+(includeHidden?QString():QStringLiteral(" WHERE visible<>0"))+QStringLiteral(" ORDER BY id LIMIT 2049"),{},message))return false;
    QSet<qint64> ids;
    while(q.next())
    {
        const auto values=row(q);CalibrationRecord record;record.id=values.value("calibration_id").toLongLong();
        if(record.id<=0||ids.contains(record.id)||records->size()>=2048)return Sn100OriginalSql::error(message,QStringLiteral("原校准编号重复、无效或超过查询上限，未返回部分结果。"));ids.insert(record.id);
        record.originalProfile=true;record.originalRoot=m_originalRoot;record.originalFingerprint=version;record.revision=revisionOf(version);record.originalBaseValues=values;
        record.projectName=values.value("project_name").toString();
        for(const auto &project:m_originalProjects)if(project.name==record.projectName)record.projectId=project.id;
        if(!record.projectId)record.projectId=-qint64(qHash(record.projectName))-1; // 缺项目的历史仍可查看，不把它关联到新项目。
        record.createdAt=values.value("calibration_datetime").toString();record.changedAt=record.createdAt;
        record.bloodBatch=values.value("boold_batch").toString();record.method=values.value("calculate_id").toString();
        record.xTransform=values.value("x_change").toString();record.yTransform=values.value("y_change").toString();
        record.resultUnit=values.value("result_unit").toString();record.concentrationUnit=values.value("con_unit").toString();
        record.visible=values.value("visible").toBool();record.checked=values.value("is_checked").toBool();record.edited=values.value("is_edited").toBool();
        for(const auto &unit:m_originalUnits)if(unit.name==record.concentrationUnit)record.unitRatio=unit.ratio;
        QSqlQuery points(m_database);
        if(!query(&points,"SELECT * FROM calibration_data WHERE calibration_id=? ORDER BY point_index,id",{record.id},message))return false;
        while(points.next())
        {
            const auto fields=row(points);CalibrationPoint point;point.originalValues=fields;point.originalRowId=fields.value("id").toLongLong();point.index=fields.value("point_index").toInt();point.sampleRate=fields.value("sample_rate").toString();
            bool valid=false;point.concentration=fields.value("concentration").toString().toDouble(&valid);if(!valid)point.concentration=std::numeric_limits<double>::quiet_NaN();
            const QStringList measured=fields.value("result_value").toString().split('|',Qt::KeepEmptyParts);
            for(const QString &text:measured){const double value=text.toDouble(&valid);point.measurements<<(valid?value:std::numeric_limits<double>::quiet_NaN());}
            const int repeats=values.value("repeat_number").toInt();
            if(repeats>8||repeats<0||point.measurements.size()>8)return Sn100OriginalSql::error(message,QStringLiteral("原重复次数超出当前页面支持范围，未截断或改写。"));
            while(point.measurements.size()<repeats)point.measurements<<std::numeric_limits<double>::quiet_NaN();
            record.points<<point;if(record.points.size()>32)return Sn100OriginalSql::error(message,QStringLiteral("原曲线点数超过当前页面上限，未截断。"));
        }
        if(points.lastError().isValid())return Sn100OriginalSql::error(message,points.lastError().text());
        QSqlQuery bindings(m_database);if(!query(&bindings,"SELECT batch_group FROM calibration_batch_group WHERE project_name=? AND calibration_id=? ORDER BY id",{record.projectName,record.id},message))return false;
        while(bindings.next())record.appliedBatchGroups<<bindings.value(0).toString();records->append(record);
    }
    return !q.lastError().isValid()||Sn100OriginalSql::error(message,q.lastError().text());
}
void CalibrationStore::loadOriginal(qint64 requestId)
{
    if(!check(requestId,"load"))return;
    QString message;const QString before=fingerprint(m_originalRoot,files,&message);
    if(before.isEmpty()||!m_database.transaction()){fail(requestId,"load",message);return;}
    CalibrationRecords records;
    if(!readOriginal(&records,&message)||!m_database.commit()){m_database.rollback();fail(requestId,"load",message);return;}
    const QString after=fingerprint(m_originalRoot,files,&message);
    if(before!=after||after.isEmpty()){fail(requestId,"load",QStringLiteral("读取原校准期间资料变化，请重试。"));return;}
    m_originalFingerprint=after;emit snapshotReady(requestId,records);
}
void CalibrationStore::saveOriginal(qint64 requestId,CalibrationRecord record)
{
    const QString op="save";QString message;
    if(!beginOriginal(requestId,op,&message))return;
    CalibrationRecords existing;if(!readOriginal(&existing,&message,true)){rejectOriginal(requestId,op,message);return;}
    auto found=std::find_if(existing.cbegin(),existing.cend(),[&](const CalibrationRecord &value){return value.id==record.id;});
    const bool fresh=record.id==0;
    if(!fresh&&(found==existing.cend()||record.originalRoot!=m_originalRoot||record.originalFingerprint!=m_originalFingerprint||record.revision!=found->revision||record.projectName!=found->projectName))
    {rejectOriginal(requestId,op,QStringLiteral("原曲线草稿的目录、编号或读取版本已变化，未覆盖。"));return;}
    if(record.points.isEmpty()||record.points.size()>32||record.projectName.isEmpty())
    {rejectOriginal(requestId,op,QStringLiteral("原曲线项目和1至32个原始点必须明确提供。"));return;}
    const auto project=std::find_if(m_originalProjects.cbegin(),m_originalProjects.cend(),[&](const ProjectRecord &value){return value.name==record.projectName&&value.id==record.projectId;});
    if(project==m_originalProjects.cend()||!project->enabled){rejectOriginal(requestId,op,QStringLiteral("原项目缺失或停用，不能提交校准修改。"));return;}
    if(!fresh&&record.points.size()!=found->points.size()){rejectOriginal(requestId,op,QStringLiteral("已有原曲线不在保存时增删点；请复制或新增完整曲线。"));return;}
    QSet<int> indices;bool changed=fresh;QVariantMap base;
    if(fresh)
    {
        if(!allocateCurve(m_database,&record.id,&message)){rejectOriginal(requestId,op,message);return;}
        base={{"calibration_id",record.id},{"project_name",record.projectName},{"calibration_datetime",QDateTime::currentDateTime().toString("yyyyMMddHHmmss")},
            {"boold_batch",record.bloodBatch},{"point_size",record.points.size()},{"repeat_number",record.points.first().measurements.size()},
            {"calculate_id",record.method==QStringLiteral("未拟合")?QString():record.method},{"is_checked",0},{"is_edited",1},
            {"result_unit",record.resultUnit},{"con_unit",record.concentrationUnit},{"x_change",record.xTransform},{"y_change",record.yTransform},{"visible",1}};
    }
    else
    {
        base={{"boold_batch",record.bloodBatch},{"calculate_id",record.method},{"x_change",record.xTransform},{"y_change",record.yTransform}};
        for(auto it=base.cbegin();it!=base.cend();++it)if(found->originalBaseValues.value(it.key()).toString()!=it.value().toString())changed=true;
    }
    for(const CalibrationPoint &point:record.points)
    {
        if(point.index<=0||indices.contains(point.index)||point.measurements.isEmpty()||point.measurements.size()>8)
        {rejectOriginal(requestId,op,QStringLiteral("原始点序号重复或重复测试数不在1至8范围。"));return;}indices.insert(point.index);
        const CalibrationPoint *old=nullptr;if(!fresh)for(const auto &value:found->points)if(value.originalRowId==point.originalRowId)old=&value;
        if(!fresh&&(!old||old->index!=point.index)){rejectOriginal(requestId,op,QStringLiteral("原点行身份发生变化，未写错行。"));return;}
        QVariantMap fields;
        if(fresh)fields={{"calibration_id",record.id},{"point_index",point.index},{"sample_rate",point.sampleRate}};
        else if(point.sampleRate!=old->sampleRate)fields.insert("sample_rate",point.sampleRate);
        if(fresh||!sameNumber(point.concentration,old->concentration))
        {
            if(!std::isfinite(point.concentration)||point.concentration<0){rejectOriginal(requestId,op,QStringLiteral("修改后的原浓度须为有效非负数。"));return;}
            fields.insert("concentration",number(point.concentration));
        }
        if(fresh||!sameMeasurements(point.measurements,old->measurements))
        {
            QStringList values=fresh?QStringList():old->originalValues.value("result_value").toString().split('|',Qt::KeepEmptyParts);
            QStringList flags=fresh?QStringList():old->originalValues.value("result_flag").toString().split('|',Qt::KeepEmptyParts);
            QStringList tasks=fresh?QStringList():old->originalValues.value("only_count").toString().split('|',Qt::KeepEmptyParts);
            if(!fresh&&point.measurements.size()!=old->measurements.size()){rejectOriginal(requestId,op,QStringLiteral("已有点的重复测试数不能在普通保存时改变。"));return;}
            while(values.size()<point.measurements.size())values<<QString();while(flags.size()<point.measurements.size())flags<<"5";while(tasks.size()<point.measurements.size())tasks<<"0";
            for(int index=0;index<point.measurements.size();++index)if(fresh||!sameNumber(point.measurements[index],old->measurements[index]))
            {const double value=point.measurements[index];if(!std::isfinite(value)||value<0||value>99999){rejectOriginal(requestId,op,QStringLiteral("编辑的测试值须为0至99999的有效数，原未知文本保持原样。"));return;}values[index]=number(value);flags[index]="0";}
            fields.insert("result_value",values.join('|'));fields.insert("result_flag",flags.join('|'));
            if(fresh)fields.insert("only_count",tasks.join('|'));
        }
        if(!fields.isEmpty())changed=true;
        if(fresh){qint64 pointId=0;if(!insertFields(m_database,"calibration_data",fields,&pointId,&message)){rejectOriginal(requestId,op,message);return;}}
        else if(!fields.isEmpty()&&!updateFields(m_database,"calibration_data",point.originalRowId,fields,&message)){rejectOriginal(requestId,op,message);return;}
    }
    if(fresh)
    {qint64 baseId=0;if(!insertFields(m_database,"calibration_base",base,&baseId,&message)){rejectOriginal(requestId,op,message);return;}}
    else if(changed)
    {
        base.insert("calibration_datetime",QDateTime::currentDateTime().toString("yyyyMMddHHmmss"));base.insert("is_edited",1);
        if(!updateFields(m_database,"calibration_base",found->originalBaseValues.value("id").toLongLong(),base,&message)){rejectOriginal(requestId,op,message);return;}
    }
    finishOriginal(requestId,op,record.id);
}
void CalibrationStore::copyOriginal(qint64 requestId,qint64 sourceId,qint64 sourceRevision)
{
    const QString op="copy";QString message;if(!beginOriginal(requestId,op,&message))return;
    CalibrationRecords records;if(!readOriginal(&records,&message)){rejectOriginal(requestId,op,message);return;}
    const auto found=std::find_if(records.cbegin(),records.cend(),[&](const CalibrationRecord &value){return value.id==sourceId;});
    if(found==records.cend()||found->revision!=sourceRevision){rejectOriginal(requestId,op,QStringLiteral("原曲线复制来源已变化，请重读。"));return;}
    qint64 id=0;if(!allocateCurve(m_database,&id,&message)){rejectOriginal(requestId,op,message);return;}
    QVariantMap base=found->originalBaseValues;base.insert("calibration_id",id);base.insert("calibration_datetime",QDateTime::currentDateTime().toString("yyyyMMddHHmmss"));base.insert("is_edited",1);
    qint64 ignored=0;if(!insertFields(m_database,"calibration_base",base,&ignored,&message)){rejectOriginal(requestId,op,message);return;}
    for(const auto &point:found->points)
    {QVariantMap fields=point.originalValues;fields.insert("calibration_id",id);if(!insertFields(m_database,"calibration_data",fields,&ignored,&message)){rejectOriginal(requestId,op,message);return;}}
    finishOriginal(requestId,op,id); // 不复制应用绑定，保持原版新增曲线行为。
}
void CalibrationStore::removeOriginal(qint64 requestId,qint64 id,qint64 revision)
{
    const QString op="remove";QString message;if(!beginOriginal(requestId,op,&message))return;
    CalibrationRecords records;if(!readOriginal(&records,&message)){rejectOriginal(requestId,op,message);return;}
    const auto found=std::find_if(records.cbegin(),records.cend(),[&](const CalibrationRecord &value){return value.id==id;});
    if(found==records.cend()||found->revision!=revision){rejectOriginal(requestId,op,QStringLiteral("原曲线已变化或不可见，请重读。"));return;}
    if(!found->appliedBatchGroups.isEmpty()){rejectOriginal(requestId,op,QStringLiteral("原曲线仍被批组应用，不能删除。"));return;}
    if(!execute(m_database,"UPDATE calibration_base SET visible=0 WHERE id=?",{found->originalBaseValues.value("id")},&message,1)){rejectOriginal(requestId,op,message);return;}
    finishOriginal(requestId,op,id); // 原版仅隐藏，点和未知字段保留。
}
void CalibrationStore::setOriginalReview(qint64 requestId,qint64 id,qint64 revision,bool checked)
{
    const QString op="setOriginalReview";QString message;if(!m_originalMode){fail(requestId,op,QStringLiteral("当前不是原校准模式。"));return;}
    if(!beginOriginal(requestId,op,&message))return;
    CalibrationRecords records;if(!readOriginal(&records,&message)){rejectOriginal(requestId,op,message);return;}
    const auto found=std::find_if(records.cbegin(),records.cend(),[&](const CalibrationRecord &value){return value.id==id;});
    if(found==records.cend()||found->revision!=revision){rejectOriginal(requestId,op,QStringLiteral("待审核原曲线已变化。"));return;}
    if(!checked){rejectOriginal(requestId,op,QStringLiteral("原界面只提供确认审核，没有未经依据的反审核操作。"));return;}
    if(!updateFields(m_database,"calibration_base",found->originalBaseValues.value("id").toLongLong(),{{"is_checked",1}},&message)){rejectOriginal(requestId,op,message);return;}
    finishOriginal(requestId,op,id); // 保存操作者确认的原审核标志，不代表已经执行拟合或临床验证。
}
void CalibrationStore::setOriginalMethod(qint64 requestId,qint64 id,qint64 revision,QString method,QString xTransform,QString yTransform)
{
    const QString op="setOriginalMethod";QString message;
    const QStringList methods{QStringLiteral("直线法"),QStringLiteral("一元三次曲线法"),QStringLiteral("插值曲线法"),QStringLiteral("折线法"),QStringLiteral("双线回归法")};
    const QStringList xs{"x","log(x)","1/x","ln(x)","log(log(x))","ln(ln(x))"};
    const QStringList ys{"y","log(y)","1/y","ln(y)","log(log(y))","ln(ln(y))"};
    if(!m_originalMode||m_session.role<4||!methods.contains(method)||!xs.contains(xTransform)||!ys.contains(yTransform))
    {fail(requestId,op,QStringLiteral("计算方法须由工程师按原方法和坐标变换列表选择。"));return;}
    if(!beginOriginal(requestId,op,&message))return;
    CalibrationRecords records;if(!readOriginal(&records,&message)){rejectOriginal(requestId,op,message);return;}
    const auto found=std::find_if(records.cbegin(),records.cend(),[&](const CalibrationRecord &value){return value.id==id;});
    if(found==records.cend()||found->revision!=revision){rejectOriginal(requestId,op,QStringLiteral("方法设置来源曲线已变化，请重读。"));return;}
    if(!updateFields(m_database,"calibration_base",found->originalBaseValues.value("id").toLongLong(),{{"calculate_id",method},{"x_change",xTransform},{"y_change",yTransform}},&message))
    {rejectOriginal(requestId,op,message);return;}
    finishOriginal(requestId,op,id); // 原设置卡只保存方法；没有把未执行的DLL拟合伪装成完成。
}
void CalibrationStore::applyOriginalCurve(qint64 requestId,qint64 id,qint64 revision,QString batchGroup)
{
    const QString op="applyOriginalCurve";QString message;if(!m_originalMode){fail(requestId,op,QStringLiteral("当前不是原校准模式。"));return;}
    if(!canEdit(m_session)){fail(requestId,op,QStringLiteral("当前原账户没有曲线应用权限。"));return;}
    if(!beginOriginal(requestId,op,&message))return;
    CalibrationRecords records;if(!readOriginal(&records,&message)){rejectOriginal(requestId,op,message);return;}
    const auto found=std::find_if(records.cbegin(),records.cend(),[&](const CalibrationRecord &value){return value.id==id;});
    if(found==records.cend()||found->revision!=revision){rejectOriginal(requestId,op,QStringLiteral("待应用原曲线已变化。"));return;}
    bool autoApply=false,projectFound=false;for(const auto &project:m_originalProjects)if(project.name==found->projectName){projectFound=project.enabled;autoApply=project.algorithm.autoApply;}
    if(!projectFound||(!found->checked&&!autoApply)){rejectOriginal(requestId,op,QStringLiteral("原项目停用或曲线尚未审核，未改变应用绑定。"));return;}
    if(batchGroup.isEmpty()||batchGroup.contains(QChar::Null)){rejectOriginal(requestId,op,QStringLiteral("须明确选择原试剂批组或Default。"));return;}
    if(batchGroup!="Default")
    {QSqlQuery batch(m_database);if(!query(&batch,"SELECT 1 FROM original_reagent.reagent_batch_group WHERE project_name=? AND batch_group=?",{found->projectName,batchGroup},&message)||!batch.next()){rejectOriginal(requestId,op,QStringLiteral("原试剂批组不存在或不属于该项目。"));return;}}
    QSqlQuery q(m_database);if(!query(&q,"SELECT id FROM calibration_batch_group WHERE project_name=? AND batch_group=?",{found->projectName,batchGroup},&message)){rejectOriginal(requestId,op,message);return;}
    qint64 binding=0;if(q.next())binding=q.value(0).toLongLong();if(q.next()){rejectOriginal(requestId,op,QStringLiteral("原批组应用绑定重复，未任选一条覆盖。"));return;}q.finish();
    bool ok=binding?updateFields(m_database,"calibration_batch_group",binding,{{"calibration_id",id}},&message):execute(m_database,"INSERT INTO calibration_batch_group(project_name,batch_group,calibration_id) VALUES(?,?,?)",{found->projectName,batchGroup,id},&message,1);
    if(!ok){rejectOriginal(requestId,op,message);return;}finishOriginal(requestId,op,id);
}
void CalibrationStore::loadIsi(qint64 requestId)
{
    const QString op="loadIsi";if(!m_originalMode||!check(requestId,op)){if(!m_originalMode)fail(requestId,op,QStringLiteral("当前不是原ISI模式。"));return;}
    QString message;const QString before=fingerprint(m_originalRoot,files,&message);
    if(before.isEmpty()||!m_database.transaction()){fail(requestId,op,message);return;}
    QSqlQuery q(m_database);CalibrationIsiRecords records;
    if(!query(&q,"SELECT id,batch_group,isi_value,normal_value FROM original_isi.isi_value ORDER BY id LIMIT 4097",{},&message))
    {m_database.rollback();fail(requestId,op,message);return;}
    while(q.next())
    {CalibrationIsiRecord record;record.id=q.value(0).toLongLong();record.batchGroup=q.value(1).toString();record.isiValue=q.value(2).toString();record.normalValue=q.value(3).toString();record.originalRoot=m_originalRoot;record.originalFingerprint=before;records<<record;}
    if(records.size()>4096||q.lastError().isValid()||!m_database.commit()){m_database.rollback();fail(requestId,op,QStringLiteral("原ISI读取失败或超出查询范围，未返回部分结果。"));return;}
    if(fingerprint(m_originalRoot,files,&message)!=before){fail(requestId,op,QStringLiteral("读取原ISI期间资料变化，请重试。"));return;}
    m_originalFingerprint=before;emit isiReady(requestId,records);
}
void CalibrationStore::saveIsi(qint64 requestId,CalibrationIsiRecord record)
{
    const QString op="saveIsi";QString message;if(!m_originalMode){fail(requestId,op,QStringLiteral("当前不是原ISI模式。"));return;}
    bool isiOk=false,normalOk=false;const double isi=record.isiValue.toDouble(&isiOk),normal=record.normalValue.toDouble(&normalOk);
    if(record.batchGroup.isEmpty()||record.batchGroup.contains(QChar::Null)||!isiOk||!normalOk||!std::isfinite(isi)||!std::isfinite(normal)||isi<0||normal<0||isi>9999||normal>9999)
    {fail(requestId,op,QStringLiteral("原批组、ISI和正常参考值必须明确，两个参数须按原设置范围为0至9999。"));return;}
    if(!beginOriginal(requestId,op,&message))return;
    if(record.originalRoot!=m_originalRoot||record.originalFingerprint!=m_originalFingerprint){rejectOriginal(requestId,op,QStringLiteral("原ISI草稿所属目录或版本已变化。"));return;}
    QSqlQuery q(m_database);if(!query(&q,"SELECT id FROM original_isi.isi_value WHERE batch_group=?",{record.batchGroup},&message)){rejectOriginal(requestId,op,message);return;}
    qint64 existing=0;if(q.next())existing=q.value(0).toLongLong();if(q.next()){rejectOriginal(requestId,op,QStringLiteral("原ISI批组重复，未覆盖不确定记录。"));return;}q.finish();
    if(record.id!=existing){rejectOriginal(requestId,op,QStringLiteral("原ISI记录身份已变化，请重读后编辑。"));return;}
    if(!existing)
    {if(!query(&q,"INSERT INTO original_isi.isi_value(batch_group,isi_value,normal_value) VALUES(?,?,?)",{record.batchGroup,record.isiValue,record.normalValue},&message)){rejectOriginal(requestId,op,message);return;}record.id=q.lastInsertId().toLongLong();}
    else if(!updateFields(m_database,"original_isi.isi_value",existing,{{"isi_value",record.isiValue},{"normal_value",record.normalValue}},&message)){rejectOriginal(requestId,op,message);return;}
    finishOriginal(requestId,op,record.id);
}
