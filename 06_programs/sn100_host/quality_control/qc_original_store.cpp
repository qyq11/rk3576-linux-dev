#include "qc_store.h"
#include "../calibration/original_sql.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QSet>
#include <QThread>
#include <QLockFile>
#include <algorithm>

using namespace Sn100OriginalSql;
namespace
{
const QStringList qcFiles{QStringLiteral("result/quality.db"), QStringLiteral("result/quality_Data.db")};
const QStringList strategyNames{QStringLiteral("r1_2s"),QStringLiteral("r1_3s"),QStringLiteral("r2_2s"),
    QStringLiteral("r4_1s"),QStringLiteral("r5_5s"),QStringLiteral("rR4s"),QStringLiteral("r7T"),
    QStringLiteral("r10X"),QStringLiteral("limit_control")};
bool exists(QSqlDatabase db, const QString &sql, const QVariantList &values, bool *found, QString *message)
{
    QSqlQuery q(db); if (!query(&q, sql, values, message)) return false;
    *found = q.next(); return !q.lastError().isValid();
}
bool replaceChanged(QSqlDatabase db, const QString &table, qint64 id,
                    const QMap<QString,QVariant> &values, QString *message)
{
    QSqlQuery q(db);
    if (!query(&q, QStringLiteral("SELECT * FROM ")+table+QStringLiteral(" WHERE id=?"), {id}, message)) return false;
    if (!q.next()) return error(message, QStringLiteral("原记录已不存在，请重新读取。"));
    const auto before = row(q); q.finish();
    QStringList assignments; QVariantList bindings;
    for (auto it=values.cbegin();it!=values.cend();++it)
        if (before.value(it.key()).toString()!=it.value().toString())
        { assignments << ('"'+it.key()+QStringLiteral("\"=?")); bindings << it.value(); }
    if (assignments.isEmpty()) return true;
    bindings << id;
    return Sn100OriginalSql::execute(db,QStringLiteral("UPDATE ")+table+QStringLiteral(" SET ")+assignments.join(',')+QStringLiteral(" WHERE id=?"),bindings,message,1);
}
QMap<QString,QVariant> ruleFields(const QcRule &rule)
{
    QMap<QString,QVariant> fields{{"used",rule.used?1:0},{"upper_limit",rule.upperLimit},{"lower_limit",rule.lowerLimit},
        {"upper_bound",rule.upperBound},{"lower_bound",rule.lowerBound},{"sd_value",rule.sdValue},{"target",rule.target}};
    for(int n=0;n<strategyNames.size();++n) fields.insert(strategyNames[n],rule.strategies.value(n));
    if(!rule.originalValues.isEmpty())
    {
        if(rule.originalValues.value("used").toBool()==rule.used)fields.insert("used",rule.originalValues.value("used"));
        for(int n=0;n<strategyNames.size();++n)
            if(rule.originalValues.value(strategyNames[n]).toInt()==rule.strategies.value(n))fields.insert(strategyNames[n],rule.originalValues.value(strategyNames[n]));
    }
    return fields;
}
QString changedRuleError(const QcRule &rule)
{
    const auto fields=ruleFields(rule);bool changed=false;
    for(auto it=fields.cbegin();it!=fields.cend();++it)
        if(rule.originalValues.value(it.key()).toString()!=it.value().toString())changed=true;
    if(!changed)return {};
    QcRule checked=rule;
    const auto clearUnchanged=[&](const QString &name,QString *value)
    {if(rule.originalValues.value(name).toString()==*value)value->clear();};
    clearUnchanged("upper_limit",&checked.upperLimit);clearUnchanged("lower_limit",&checked.lowerLimit);
    clearUnchanged("upper_bound",&checked.upperBound);clearUnchanged("lower_bound",&checked.lowerBound);
    clearUnchanged("sd_value",&checked.sdValue);clearUnchanged("target",&checked.target);
    for(int index=0;index<checked.strategies.size()&&index<strategyNames.size();++index)
        if(rule.originalValues.value(strategyNames[index]).toInt()==checked.strategies[index])checked.strategies[index]=0;
    return QcStore::validateRules({checked});
}
}

void QcStore::setSession(AccountSession session)
{
    if (QThread::currentThread()==thread()) originalSession_=session;
}
void QcStore::initializeOriginal(QString rootPath)
{
    if (QThread::currentThread()!=thread() || QThread::currentThread()==QCoreApplication::instance()->thread())
    { fail(0,"initializeOriginal",QStringLiteral("原质控库须在数据库工作线程打开。")); return; }
    QString message;
    if (!root(&rootPath,&message)) { fail(0,"initializeOriginal",message); return; }
    release();
    originalRoot_=rootPath;
    if (!open(&database_,&connectionName_,QDir(rootPath).filePath(qcFiles.first()),&message))
    { fail(0,"initializeOriginal",message);release();return; }
    const QString history=QDir(rootPath).filePath(qcFiles.last());
    QSqlQuery attach(database_);
    if (!regular(history) || !query(&attach,"ATTACH DATABASE ? AS original_results",{existingUri(history)},&message) ||
        !columns(database_,"quality_control_list",{"id","item_name","factor_name","is_used"},&message) ||
        !columns(database_,"quality_item_proj",{"id","item_id","proj_name"},&message) ||
        !columns(database_,"quality_control_batch",{"id","fid","batch_name","expiration_date","cur_batch","prv_batch"},&message) ||
        !columns(database_,"quality_rules",{"id","item_proj_id","type","name","used","target","r1_2s","limit_control"},&message) ||
        !columns(database_,"original_results.quality_data",{"id","control_name","project_name","control_batch","result_val","date","time"},&message))
    { fail(0,"initializeOriginal",message.isEmpty()?QStringLiteral("质控历史原文件不存在，未新建替代文件。"):message);release();return; }
    originalMode_=true;ready_=true;referencesReady_=true;
    datasetId_=QStringLiteral("original-quality:")+rootPath;
    emit originalSourceReady(rootPath);
    emit ready(QDir(rootPath).filePath(qcFiles.first()));
    emit catalogBound(QDir(rootPath).filePath("project"));
}
bool QcStore::originalWrite(qint64 requestId,const QString &operation,QString *message)
{
    if (!check(requestId,operation)) return false;
    if (requestId<=0 || requestId<=originalLastWrite_)
    { fail(requestId,operation,QStringLiteral("重复或迟到的原质控写请求未执行。")); return false; }
    originalLastWrite_=requestId;
    if (!originalSession_.authenticated || !originalSession_.actionReady || originalSession_.originalPolicySource!=originalRoot_)
    { fail(requestId,operation,QStringLiteral("须以允许操作且设备就绪的账户维护原质控配置。")); return false; }
    originalProjectLock_.reset(new QLockFile(QDir(originalRoot_).filePath(".sn100-original-project-write.lock")));
    if(!originalProjectLock_->tryLock(0))
    {fail(requestId,operation,QStringLiteral("原项目关联正在写入，请保留质控草稿后重试。"));return false;}
    if (originalFingerprint_.isEmpty() || fingerprint(originalRoot_,qcFiles,message)!=originalFingerprint_)
    { fail(requestId,operation,QStringLiteral("原质控资料在读取后已变化；草稿保留，请重新读取后核对。")); return false; }
    if (!Sn100OriginalSql::execute(database_,"BEGIN IMMEDIATE",{},message)) { fail(requestId,operation,*message);return false; }
    if (fingerprint(originalRoot_,qcFiles,message)!=originalFingerprint_)
    { database_.rollback();fail(requestId,operation,QStringLiteral("建立写事务时原资料发生变化，未覆盖。"));return false; }
    return true;
}
void QcStore::originalFinished(qint64 requestId,const QString &operation,qint64 id)
{
    if (!database_.commit()) { database_.rollback();fail(requestId,operation,database_.lastError().text());return; }
    QString message;
    originalFingerprint_=fingerprint(originalRoot_,qcFiles,&message);
    originalProjectLock_.reset();
    if (originalFingerprint_.isEmpty()) { fail(requestId,operation,QStringLiteral("已提交，但原库版本读回失败，请重新读取确认。"));return; }
    emit succeeded(requestId,operation,id);
}
void QcStore::loadOriginal(qint64 requestId)
{
    if (!check(requestId,"load"))return;
    QString message;const QString before=fingerprint(originalRoot_,qcFiles,&message);
    if (before.isEmpty() || !database_.transaction()) { fail(requestId,"load",message);return; }
    QcControls controls;QSqlQuery q(database_);bool ok=query(&q,"SELECT * FROM quality_control_list ORDER BY id",{},&message);
    while(ok && q.next())
    {
        const auto values=row(q);QcControl control;control.id=values.value("id").toLongLong();
        control.name=values.value("item_name").toString();control.manufacturer=values.value("factor_name").toString();control.used=values.value("is_used").toBool();
        control.originalRoot=originalRoot_;control.originalFingerprint=before;
        QSqlQuery associations(database_);ok=query(&associations,"SELECT id,proj_name FROM quality_item_proj WHERE item_id=? ORDER BY id",{control.id},&message);
        while(ok && associations.next())
        {
            QcAssociation association;association.id=associations.value(0).toLongLong();association.controlId=control.id;association.projectName=associations.value(1).toString();
            for(const QcProjectRef &project:catalog_) if(project.name==association.projectName)
            {association.projectId=project.id;association.resultRefs=project.results;break;}
            QSqlQuery rules(database_);ok=query(&rules,"SELECT * FROM quality_rules WHERE item_proj_id=? ORDER BY id",{association.id},&message);
            while(ok && rules.next())
            {
                const auto fields=row(rules);QcRule rule;rule.id=fields.value("id").toLongLong();rule.associationId=association.id;
                rule.originalValues=fields;rule.originalRoot=originalRoot_;rule.originalFingerprint=before;
                rule.resultType=fields.value("type").toInt();rule.name=fields.value("name").toString();rule.used=fields.value("used").toBool();
                rule.upperLimit=fields.value("upper_limit").toString();rule.lowerLimit=fields.value("lower_limit").toString();
                rule.upperBound=fields.value("upper_bound").toString();rule.lowerBound=fields.value("lower_bound").toString();rule.sdValue=fields.value("sd_value").toString();rule.target=fields.value("target").toString();
                rule.strategies.clear();for(const QString &name:strategyNames)rule.strategies<<fields.value(name).toInt();
                association.rules<<rule;
            }
            if(rules.lastError().isValid()){ok=false;message=rules.lastError().text();}
            control.projects<<association;
        }
        QSqlQuery batches(database_);if(ok)ok=query(&batches,"SELECT * FROM quality_control_batch WHERE fid=? ORDER BY id",{control.id},&message);
        while(ok && batches.next())
        {
            const auto fields=row(batches);QcBatch batch;batch.id=fields.value("id").toLongLong();batch.controlId=control.id;
            batch.name=fields.value("batch_name").toString();batch.expirationDate=fields.value("expiration_date").toString();
            batch.current=fields.value("cur_batch").toBool();batch.previous=fields.value("prv_batch").toBool();control.batches<<batch;
            control.batches.last().originalRoot=originalRoot_;control.batches.last().originalFingerprint=before;
        }
        controls<<control;
        if(controls.size()>2048){ok=false;message=QStringLiteral("原质控品超过读取上限，未返回截断结果。");}
    }
    if(q.lastError().isValid()){ok=false;message=q.lastError().text();}
    if(!ok || !database_.commit()){database_.rollback();fail(requestId,"load",message);return;}
    const QString after=fingerprint(originalRoot_,qcFiles,&message);
    if(after.isEmpty() || before!=after){fail(requestId,"load",QStringLiteral("读取原质控资料期间文件发生变化，请重试。"));return;}
    originalFingerprint_=after;emit snapshotReady(requestId,controls);
}
void QcStore::saveOriginalControl(qint64 requestId,QcControl control)
{
    QString message=control.id?QString():validateControl(control);const QString op="saveControl";
    if(control.name.trimmed().isEmpty()||control.name.contains(QChar::Null)||control.name.size()>128||control.manufacturer.contains(QChar::Null)||control.manufacturer.size()>128)
        message=QStringLiteral("质控品名称不能为空，名称与厂家须为有效文字。");
    if(!message.isEmpty()){fail(requestId,op,message);return;}
    if(control.id&&(control.originalRoot!=originalRoot_||control.originalFingerprint!=originalFingerprint_))
    {fail(requestId,op,QStringLiteral("质控品草稿的原目录或读取版本已变化，请重读。"));return;}
    if(!originalWrite(requestId,op,&message))return;
    const auto reject=[&](){database_.rollback();fail(requestId,op,message);};
    bool present=false;
    if(!exists(database_,"SELECT 1 FROM quality_control_list WHERE item_name=? AND id<>?",{control.name,control.id},&present,&message) || present)
    {if(message.isEmpty())message=QStringLiteral("原质控品名称已存在，未覆盖。");reject();return;}
    if(!control.id)
    {
        QSqlQuery q(database_);if(!query(&q,"INSERT INTO quality_control_list(item_name,is_used,factor_name) VALUES(?,?,?)",{control.name,control.used?1:0,control.manufacturer},&message)){reject();return;}
        control.id=q.lastInsertId().toLongLong();
    }
    else
    {
        QString old;if(!controlName(control.id,&old,&message)){reject();return;}
        if(old!=control.name && (!historyExists("control_name=?",{old},&present,&message) || present))
        {if(present)message=QStringLiteral("原质控品已有历史，不能改名破坏关联。");reject();return;}
        if(!replaceChanged(database_,"quality_control_list",control.id,{{"item_name",control.name},{"factor_name",control.manufacturer}},&message)){reject();return;}
        for(const QcAssociation &association:control.projects)for(const QcRule &rule:association.rules)
        {
            if(rule.originalRoot!=originalRoot_||rule.originalFingerprint!=originalFingerprint_)
            {message=QStringLiteral("原规则草稿版本已变化，未混入新快照。");reject();return;}
            message=changedRuleError(rule);if(!message.isEmpty()){reject();return;}
            if(!exists(database_,"SELECT 1 FROM quality_item_proj a JOIN quality_rules r ON r.item_proj_id=a.id WHERE a.id=? AND a.item_id=? AND a.proj_name=? AND r.id=? AND r.type=? AND r.name=?",{association.id,control.id,association.projectName,rule.id,rule.resultType,rule.name},&present,&message) || !present || !replaceChanged(database_,"quality_rules",rule.id,ruleFields(rule),&message))
            {if(message.isEmpty())message=QStringLiteral("原项目与规则关联已变化，未写入。");reject();return;}
        }
    }
    originalFinished(requestId,op,control.id);
}
void QcStore::removeOriginalControl(qint64 requestId,qint64 id)
{
    const QString op="removeControl";QString message;if(!originalWrite(requestId,op,&message))return;
    QString name;bool held=false;
    if(!controlName(id,&name,&message) || !historyExists("control_name=?",{name},&held,&message) || held)
    {database_.rollback();fail(requestId,op,held?QStringLiteral("原质控品已有历史，保留质控品及关联。"):message);return;}
    bool ok=Sn100OriginalSql::execute(database_,"DELETE FROM quality_rules WHERE item_proj_id IN(SELECT id FROM quality_item_proj WHERE item_id=?)",{id},&message) &&
        Sn100OriginalSql::execute(database_,"DELETE FROM quality_item_proj WHERE item_id=?",{id},&message) && Sn100OriginalSql::execute(database_,"DELETE FROM quality_control_batch WHERE fid=?",{id},&message) &&
        Sn100OriginalSql::execute(database_,"DELETE FROM quality_control_list WHERE id=?",{id},&message,1);
    if(!ok){database_.rollback();fail(requestId,op,message);return;}originalFinished(requestId,op,id);
}
void QcStore::setOriginalProjects(qint64 requestId,qint64 controlId,QcProjectRefs projects)
{
    const QString op="setProjects";QString message;if(!originalWrite(requestId,op,&message))return;
    const auto reject=[&](){database_.rollback();fail(requestId,op,message);};
    QString control;if(!controlName(controlId,&control,&message)){reject();return;}
    QSet<QString> selected;
    for(const QcProjectRef &project:projects)
    {
        const auto found=std::find_if(catalog_.cbegin(),catalog_.cend(),[&](const QcProjectRef &value){return value.id==project.id && value.name==project.name;});
        if(found==catalog_.cend() || selected.contains(project.name)){message=QStringLiteral("项目不是当前原项目快照或重复选择，未关联。");reject();return;}
        selected.insert(project.name);
    }
    QSqlQuery q(database_);if(!query(&q,"SELECT id,proj_name FROM quality_item_proj WHERE item_id=?",{controlId},&message)){reject();return;}
    QMap<QString,qint64> existing;
    while(q.next())existing.insert(q.value(1).toString(),q.value(0).toLongLong());q.finish();
    for(auto it=existing.cbegin();it!=existing.cend();++it)if(!selected.contains(it.key()))
    {
        bool held=false;if(!historyExists("control_name=? AND project_name=?",{control,it.key()},&held,&message) || held)
        {if(held)message=QStringLiteral("原项目已有质控历史，不能解除关联。");reject();return;}
        if(!Sn100OriginalSql::execute(database_,"DELETE FROM quality_rules WHERE item_proj_id=?",{it.value()},&message) || !Sn100OriginalSql::execute(database_,"DELETE FROM quality_item_proj WHERE id=?",{it.value()},&message,1)){reject();return;}
    }
    for(const QcProjectRef &project:projects)if(!existing.contains(project.name))
    {
        if(!query(&q,"INSERT INTO quality_item_proj(item_id,proj_name) VALUES(?,?)",{controlId,project.name},&message)){reject();return;}
        const qint64 association=q.lastInsertId().toLongLong();
        for(const QcResultRef &result:project.results)
        {
            if(result.type<1 || result.type>4)continue;
            if(!Sn100OriginalSql::execute(database_,"INSERT INTO quality_rules(item_proj_id,type,name,used,upper_limit,lower_limit,upper_bound,lower_bound,sd_value,target,r1_2s,r1_3s,r2_2s,r4_1s,r5_5s,rR4s,r7T,r10X,limit_control) VALUES(?,?,?,1,'','','','','','',0,0,0,0,0,0,0,0,0)",{association,result.type,result.name},&message,1)){reject();return;}
        }
    }
    originalFinished(requestId,op,controlId);
}
void QcStore::saveOriginalRules(qint64 requestId,qint64 associationId,QcRules rules)
{
    const QString op="saveRules";QString message;
    if(!message.isEmpty() || rules.isEmpty()){fail(requestId,op,message.isEmpty()?QStringLiteral("没有待保存规则。"):message);return;}
    if(!originalWrite(requestId,op,&message))return;
    for(const QcRule &rule:rules)
    {
        message=changedRuleError(rule);if(!message.isEmpty()){database_.rollback();fail(requestId,op,message);return;}
        if(rule.originalRoot!=originalRoot_||rule.originalFingerprint!=originalFingerprint_)
        {database_.rollback();fail(requestId,op,QStringLiteral("原规则读取版本已变化，未覆盖。"));return;}
        bool found=false;
        if(rule.associationId!=associationId || !exists(database_,"SELECT 1 FROM quality_rules WHERE id=? AND item_proj_id=? AND type=? AND name=?",{rule.id,associationId,rule.resultType,rule.name},&found,&message) || !found || !replaceChanged(database_,"quality_rules",rule.id,ruleFields(rule),&message))
        {database_.rollback();fail(requestId,op,message.isEmpty()?QStringLiteral("规则身份已变化，未保存。"):message);return;}
    }
    originalFinished(requestId,op,associationId);
}
void QcStore::saveOriginalBatch(qint64 requestId,QcBatch batch)
{
    const QString op="saveBatch";QString message=validateBatch(batch);
    if(!message.isEmpty()){fail(requestId,op,message);return;}
    if(batch.id&&(batch.originalRoot!=originalRoot_||batch.originalFingerprint!=originalFingerprint_))
    {fail(requestId,op,QStringLiteral("原批次草稿目录或版本已变化，请重读。"));return;}
    if(!originalWrite(requestId,op,&message))return;
    const auto reject=[&](){database_.rollback();fail(requestId,op,message);};
    QString control;if(!controlName(batch.controlId,&control,&message)){reject();return;}
    bool duplicate=false;if(!exists(database_,"SELECT 1 FROM quality_control_batch WHERE batch_name=? AND id<>?",{batch.name,batch.id},&duplicate,&message) || duplicate)
    {if(duplicate)message=QStringLiteral("原质控批号已存在，未覆盖。");reject();return;}
    QSqlQuery q(database_);
    if(!batch.id)
    {
        if(!query(&q,"SELECT COUNT(*) FROM quality_control_batch WHERE fid=?",{batch.controlId},&message) || !q.next()){reject();return;}
        const bool first=q.value(0).toLongLong()==0;q.finish();
        if(!query(&q,"INSERT INTO quality_control_batch(fid,batch_name,expiration_date,cur_batch,prv_batch) VALUES(?,?,?,?,0)",{batch.controlId,batch.name,batch.expirationDate,first?1:0},&message)){reject();return;}
        batch.id=q.lastInsertId().toLongLong();
    }
    else
    {
        if(!query(&q,"SELECT batch_name FROM quality_control_batch WHERE id=? AND fid=?",{batch.id,batch.controlId},&message) || !q.next()){message=QStringLiteral("原批次归属已变化。");reject();return;}
        const QString old=q.value(0).toString();q.finish();bool held=false;
        if(old!=batch.name && (!historyExists("control_name=? AND control_batch=?",{control,old},&held,&message)||held))
        {if(held)message=QStringLiteral("原批次已有历史，不能改名。");reject();return;}
        if(!replaceChanged(database_,"quality_control_batch",batch.id,{{"batch_name",batch.name},{"expiration_date",batch.expirationDate}},&message)){reject();return;}
    }
    originalFinished(requestId,op,batch.id);
}
void QcStore::removeOriginalBatch(qint64 requestId,qint64 id)
{
    const QString op="removeBatch";QString message;if(!originalWrite(requestId,op,&message))return;
    QSqlQuery q(database_);bool ok=query(&q,"SELECT c.item_name,b.batch_name,b.cur_batch,b.prv_batch FROM quality_control_batch b JOIN quality_control_list c ON c.id=b.fid WHERE b.id=?",{id},&message)&&q.next();
    bool held=false;
    if(ok && (q.value(2).toBool()||q.value(3).toBool())){ok=false;message=QStringLiteral("当前或前一原批次不能删除，请先切换。");}
    if(ok)ok=historyExists("control_name=? AND control_batch=?",{q.value(0),q.value(1)},&held,&message);
    if(ok && held){ok=false;message=QStringLiteral("原批次已有历史，不能删除。");}q.finish();
    if(ok)ok=Sn100OriginalSql::execute(database_,"DELETE FROM quality_control_batch WHERE id=?",{id},&message,1);
    if(!ok){database_.rollback();fail(requestId,op,message);return;}originalFinished(requestId,op,id);
}
void QcStore::switchOriginalBatch(qint64 requestId,qint64 controlId,qint64 batchId)
{
    const QString op="switchBatch";QString message;if(!originalWrite(requestId,op,&message))return;
    QSqlQuery q(database_);bool ok=query(&q,"SELECT id,cur_batch,prv_batch FROM quality_control_batch WHERE fid=?",{controlId},&message);
    qint64 old=0;int currents=0,previous=0;bool found=false;
    while(ok && q.next())
    {const auto id=q.value(0).toLongLong();const bool cur=q.value(1).toBool(),prv=q.value(2).toBool();if(cur){old=id;++currents;}if(prv)++previous;if(id==batchId)found=true;if(cur&&prv)ok=false;}
    q.finish();if(!found||currents>1||previous>1){ok=false;message=QStringLiteral("原批次归属或当前/前批标志冲突，未自动修复。");}
    if(ok && old!=batchId)
    {ok=Sn100OriginalSql::execute(database_,"UPDATE quality_control_batch SET cur_batch=0,prv_batch=0 WHERE fid=?",{controlId},&message);
     if(ok&&old)ok=Sn100OriginalSql::execute(database_,"UPDATE quality_control_batch SET prv_batch=1 WHERE id=?",{old},&message,1);
     if(ok)ok=Sn100OriginalSql::execute(database_,"UPDATE quality_control_batch SET cur_batch=1 WHERE id=? AND fid=?",{batchId,controlId},&message,1);}
    if(!ok){database_.rollback();fail(requestId,op,message);return;}originalFinished(requestId,op,batchId);
}
void QcStore::recordOriginalResult(qint64 requestId,QcRawResult result)
{
    const QString op="recordOriginalResult";QString message;
    if(!originalMode_){fail(requestId,op,QStringLiteral("当前不是原质控库模式。"));return;}
    const auto parsed=Sn100QcHistory::parseResult(result.resultValue,result.projectName);
    if(!parsed.envelopeValid || !QDate::fromString(result.date,"yyyy/MM/dd").isValid() || !QTime::fromString(result.time,"HH:mm:ss").isValid())
    {fail(requestId,op,QStringLiteral("质控结果应为该原项目的完整结果包及有效日期时间，未猜造结果。"));return;}
    if(!originalWrite(requestId,op,&message))return;
    bool found=false;
    if(!exists(database_,"SELECT 1 FROM quality_control_list c JOIN quality_item_proj a ON a.item_id=c.id JOIN quality_control_batch b ON b.fid=c.id WHERE c.item_name=? AND a.proj_name=? AND b.batch_name=?",{result.controlName,result.projectName,result.batchName},&found,&message)||!found)
    {database_.rollback();fail(requestId,op,message.isEmpty()?QStringLiteral("结果的原质控品/项目/批号关联不存在，未写入。"):message);return;}
    QSqlQuery q(database_);const QVariantList fields{result.controlName,result.projectName,result.batchName,result.resultValue,result.date,result.time};
    // 原QualityDataTable::upAddQualityData按同质控品/项目/批号/日期覆盖当天结果和时间。
    if(!query(&q,"SELECT id FROM original_results.quality_data WHERE control_name=? AND project_name=? AND control_batch=? AND date=? ORDER BY id",{result.controlName,result.projectName,result.batchName,result.date},&message))
    {database_.rollback();fail(requestId,op,message);return;}
    if(q.next())
    {
        const qint64 id=q.value(0).toLongLong();
        if(q.next()){q.finish();database_.rollback();fail(requestId,op,QStringLiteral("当天原质控结果存在多条，未擅自选一条覆盖。"));return;}
        q.finish();
        if(!replaceChanged(database_,"original_results.quality_data",id,{{"result_val",result.resultValue},{"time",result.time}},&message))
        {database_.rollback();fail(requestId,op,message);return;}
        originalFinished(requestId,op,id);return;
    }
    q.finish();
    if(!query(&q,"INSERT INTO original_results.quality_data(control_name,project_name,control_batch,result_val,date,time) VALUES(?,?,?,?,?,?)",fields,&message))
    {database_.rollback();fail(requestId,op,message);return;}
    originalFinished(requestId,op,q.lastInsertId().toLongLong());
}

void QcStore::loadOriginalHistory(qint64 requestId,qint64 associationId)
{
    const auto reject=[&](const QString &message){database_.rollback();emit historyError(requestId,associationId,"original_history",message);};
    if(!check(requestId,"loadHistory"))return;
    QString message;const QString before=fingerprint(originalRoot_,qcFiles,&message);
    if(before.isEmpty()||!database_.transaction()){reject(message);return;}
    QcHistoryReply reply;reply.datasetId_=datasetId_;reply.associationId_=associationId;
    QSqlQuery q(database_);
    if(!query(&q,"SELECT c.id,c.item_name,a.proj_name FROM quality_item_proj a JOIN quality_control_list c ON c.id=a.item_id WHERE a.id=?",{associationId},&message)||!q.next())
    {reject(message.isEmpty()?QStringLiteral("原质控项目关联不存在。"):message);return;}
    reply.controlId_=q.value(0).toLongLong();reply.controlName_=q.value(1).toString();reply.projectName_=q.value(2).toString();q.finish();
    for(const auto &project:catalog_)if(project.name==reply.projectName_)reply.projectId_=project.id;
    if(!query(&q,"SELECT id,type,name,used,target,lower_limit,upper_limit FROM quality_rules WHERE item_proj_id=? ORDER BY id",{associationId},&message)){reject(message);return;}
    while(q.next())
    {const int type=q.value(1).toInt();if(type<1||type>4)continue;QcHistoryResult result;result.ruleId=q.value(0).toLongLong();result.type=static_cast<Sn100QcHistory::ResultType>(type);result.name=q.value(2).toString();result.used=q.value(3).toBool();result.target=q.value(4).toString();result.lowerLimit=q.value(5).toString();result.upperLimit=q.value(6).toString();reply.results_<<result;}q.finish();
    if(!query(&q,"SELECT id,batch_name,cur_batch,prv_batch FROM quality_control_batch WHERE fid=?",{reply.controlId_},&message)){reject(message);return;}
    int currents=0,previous=0;
    while(q.next())
    {if(q.value(2).toBool()){reply.currentBatch_={q.value(0).toLongLong(),q.value(1).toString()};++currents;}if(q.value(3).toBool()){reply.previousBatch_={q.value(0).toLongLong(),q.value(1).toString()};++previous;}}q.finish();
    if(currents>1||previous>1||(reply.currentBatch_.id&&reply.currentBatch_.id==reply.previousBatch_.id)){reject(QStringLiteral("原当前/前批标志冲突，未混合历史。"));return;}
    if(currents)
    {
        if(!query(&q,"SELECT id,control_name,project_name,control_batch,date,time,result_val FROM original_results.quality_data WHERE control_name=? AND project_name=? AND control_batch IN(?,?) ORDER BY date DESC,time DESC,id DESC LIMIT 10001",{reply.controlName_,reply.projectName_,reply.currentBatch_.name,reply.previousBatch_.name},&message)){reject(message);return;}
        qint64 characters=0;
        while(q.next())
        {Sn100QcHistory::HistoryRow history;history.id=q.value(0).toLongLong();history.controlName=q.value(1).toString();history.projectName=q.value(2).toString();history.batchName=q.value(3).toString();history.date=q.value(4).toString();history.time=q.value(5).toString();history.resultValue=q.value(6).toString();characters+=history.resultValue.size();
         if(reply.rows_.size()>=Sn100QcHistory::kMaximumRows||history.resultValue.size()>Sn100QcHistory::kMaximumResultCharacters||characters>Sn100QcHistory::kMaximumTotalResultCharacters){reject(QStringLiteral("所选原历史超过读取预算，未截断后返回成功。"));return;}reply.rows_<<history;}
    }
    q.finish();
    int enabled=0;for(const auto &result:reply.results_)if(result.used)
    {
        ++enabled;Sn100QcHistory::SelectionRequest request;request.controlName=reply.controlName_;request.projectName=reply.projectName_;request.currentBatch=reply.currentBatch_.name;request.previousBatch=reply.previousBatch_.name;request.type=result.type;
        // 原历史没有逐条单位快照；展示原值，不能用今天的单位倍率重算旧结果。
        QcHistorySeries series;series.result_=result;series.selection_=Sn100QcHistory::selectHistory(reply.rows_,request);series.summary_=Sn100QcHistory::summarize(series.selection_);series.validRawCount_=series.selection_.plotOrder.size()-series.selection_.invalidResultCount;reply.series_<<series;
    }
    reply.status_=!currents?QcHistoryStatus::NoCurrentBatch:!enabled?QcHistoryStatus::NoEnabledResults:reply.rows_.isEmpty()?QcHistoryStatus::Empty:QcHistoryStatus::Ready;
    reply.complete_=true;
    if(!database_.commit()){reject(database_.lastError().text());return;}
    if(fingerprint(originalRoot_,qcFiles,&message)!=before){emit historyError(requestId,associationId,"changed_during_read",QStringLiteral("读取期间原质控文件变化，请重新查询。"));return;}
    emit historyReady(requestId,reply);
}
