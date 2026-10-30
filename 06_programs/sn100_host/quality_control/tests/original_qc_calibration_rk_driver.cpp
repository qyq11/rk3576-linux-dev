// 仅在RK建立匿名原表夹具并调用正式Store；不打开用户的患者或账户资料。
#include "quality_control/qc_store.h"
#include "calibration/calibration_store.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <QCryptographicHash>
#include <QDebug>

namespace
{
struct Checks
{
    QJsonArray cases;int failures=0;
    void check(bool ok,const QString &name)
    {cases.append(QJsonObject{{"name",name},{"passed",ok}});if(!ok)++failures;qInfo().noquote()<<(ok?"PASS":"FAIL")<<name;}
};
QByteArray hash(const QString &path)
{QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&file))return {};return h.result().toHex();}
bool database(const QString &path,const QStringList &statements)
{
    const QString name=QUuid::createUuid().toString();bool ok=true;
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);ok=db.open();if(ok){QSqlQuery q(db);for(const auto &sql:statements)if(!q.exec(sql)){qWarning()<<"FIXTURE_SQL_ERROR"<<q.lastError().text();ok=false;break;}}db.close();}
    QSqlDatabase::removeDatabase(name);return ok;
}
QVariant scalar(const QString &path,const QString &sql)
{
    const QString name=QUuid::createUuid().toString();QVariant result;
    {auto db=QSqlDatabase::addDatabase("QSQLITE",name);db.setDatabaseName(path);db.setConnectOptions("QSQLITE_OPEN_READONLY");if(db.open()){QSqlQuery q(db);if(q.exec(sql)&&q.next())result=q.value(0);}db.close();}
    QSqlDatabase::removeDatabase(name);return result;
}
bool fixture(const QString &root)
{
    QDir dir(root);
    if(!QDir::isAbsolutePath(root)||(dir.exists()&&!dir.entryList(QDir::AllEntries|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty()))return false;
    if(!dir.mkpath("result")||!dir.mkpath("db/userSet")||!dir.mkpath("project"))return false;
    QFile marker(dir.filePath(".sn100-original-work-v1"));if(!marker.open(QIODevice::WriteOnly)||marker.write("sn100-original-work-v1\n")!=23)return false;marker.close();
    return database(dir.filePath("result/quality.db"),{
        "CREATE TABLE quality_control_list(id INTEGER PRIMARY KEY AUTOINCREMENT,item_name VARCHAR,is_used INTEGER,factor_name VARCHAR,vendor_extra TEXT DEFAULT 'KEEP')",
        "CREATE TABLE quality_item_proj(id INTEGER PRIMARY KEY AUTOINCREMENT,item_id INTEGER,proj_name VARCHAR,vendor_extra TEXT DEFAULT 'KEEP')",
        "CREATE TABLE quality_control_batch(id INTEGER PRIMARY KEY AUTOINCREMENT,fid INTEGER,batch_name VARCHAR,expiration_date VARCHAR,cur_batch INTEGER,prv_batch INTEGER,vendor_extra TEXT DEFAULT 'KEEP')",
        "CREATE TABLE quality_rules(id INTEGER PRIMARY KEY AUTOINCREMENT,item_proj_id INTEGER,type INTEGER,name VARCHAR,used INTEGER,upper_limit VARCHAR,lower_limit VARCHAR,upper_bound VARCHAR,lower_bound VARCHAR,sd_value VARCHAR,target VARCHAR,r1_2s INTEGER,r1_3s INTEGER,r2_2s INTEGER,r4_1s INTEGER,r5_5s INTEGER,rR4s INTEGER,r7T INTEGER,r10X INTEGER,limit_control INTEGER,vendor_extra TEXT DEFAULT 'KEEP')"})&&
        database(dir.filePath("result/quality_Data.db"),{"CREATE TABLE quality_data(id INTEGER PRIMARY KEY AUTOINCREMENT,control_name VARCHAR,project_name VARCHAR,control_batch VARCHAR,result_val VARCHAR,date VARCHAR,time VARCHAR,vendor_extra TEXT DEFAULT 'KEEP')"})&&
        database(dir.filePath("db/userSet/reagent_Information.db"),{"CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY AUTOINCREMENT,project_name VARCHAR,batch_group VARCHAR)","INSERT INTO reagent_batch_group(project_name,batch_group) VALUES('TEST','GroupA')"})&&
        database(dir.filePath("db/userSet/isi_Value.db"),{"CREATE TABLE isi_value(id INTEGER PRIMARY KEY AUTOINCREMENT,batch_group VARCHAR,isi_value VARCHAR,normal_value VARCHAR,vendor_extra TEXT DEFAULT 'KEEP')","INSERT INTO isi_value(batch_group,isi_value,normal_value) VALUES('GroupA','1.0000','12.5000')"})&&
        database(dir.filePath("result/calibration.db"),{
        "CREATE TABLE calibraiton_total_count(id INTEGER PRIMARY KEY AUTOINCREMENT,total_count INTEGER)","INSERT INTO calibraiton_total_count(total_count) VALUES(101)",
        "CREATE TABLE calibration_base(id INTEGER PRIMARY KEY AUTOINCREMENT,calibration_id INTEGER,project_name TEXT,calibration_datetime VARCHAR,boold_batch TEXT,point_size INTEGER,repeat_number INTEGER,calculate_id TEXT,is_checked INTEGER,is_edited INTEGER,result_unit TEXT,con_unit TEXT,x_change TEXT,y_change TEXT,visible INTEGER,vendor_extra TEXT DEFAULT 'KEEP')",
        "CREATE TABLE calibration_data(id INTEGER PRIMARY KEY AUTOINCREMENT,calibration_id INTEGER,point_index INTEGER,sample_rate TEXT,concentration TEXT,result_flag TEXT,result_value TEXT,only_count TEXT,vendor_extra TEXT DEFAULT 'KEEP')",
        "CREATE TABLE calibration_batch_group(id INTEGER PRIMARY KEY AUTOINCREMENT,project_name VARCHAR,batch_group VARCHAR,calibration_id INTEGER,vendor_extra TEXT DEFAULT 'KEEP')",
        "INSERT INTO calibration_base(calibration_id,project_name,calibration_datetime,boold_batch,point_size,repeat_number,calculate_id,is_checked,is_edited,result_unit,con_unit,x_change,y_change,visible) VALUES(100,'TEST','20261027120000','B0',1,2,'直线法',0,0,'s','u','x','y',1)",
        "INSERT INTO calibration_data(calibration_id,point_index,sample_rate,concentration,result_flag,result_value,only_count) VALUES(100,1,'1/1','100.000000123456','0|4','12.3456789012345|****','91|92')"});
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
#if !defined(Q_OS_LINUX)||!defined(__aarch64__)
    qCritical()<<"此驱动只能在RK ARM64执行，电脑只编辑与交叉编译。";return 2;
#endif
    if(argc!=3)return 2;const QString root=QString::fromLocal8Bit(argv[1]),output=QString::fromLocal8Bit(argv[2]);
    if(!QDir::isAbsolutePath(root)||!QDir::isAbsolutePath(output))return 2;
    QThread thread;QObject runner;runner.moveToThread(&thread);
    QObject::connect(&thread,&QThread::started,&runner,[&]()
    {
        Checks c;c.check(fixture(root),QStringLiteral("匿名夹具保留原路径与表字段，未知列单独检查"));
        if(c.failures){app.exit(3);thread.quit();return;}
        const QString quality=QDir(root).filePath("result/quality.db"),history=QDir(root).filePath("result/quality_Data.db"),calibration=QDir(root).filePath("result/calibration.db"),isi=QDir(root).filePath("db/userSet/isi_Value.db");
        AccountSession session;session.authenticated=true;session.actionReady=true;session.role=5;session.name="AnonymousRk";session.originalPolicySource=root;session.grantedPowerIds=QStringList{QStringLiteral("1")};
        ProjectRecord project;project.id=1;project.name="TEST";project.enabled=true;
        ProjectUnit unit;unit.name="u";unit.ratio=0.125;
        QcProjectRef qcProject;qcProject.id=1;qcProject.name="TEST";QcResultRef result;result.type=1;result.name="TEST";result.unit="s";qcProject.results={result};
        QcStore qc;QcControls controls;bool qcError=false;QString qcOperation; qint64 saved=0;
        QObject::connect(&qc,&QcStore::error,&qc,[&](qint64,QString op,QString){qcError=true;qcOperation=op;});
        QObject::connect(&qc,&QcStore::succeeded,&qc,[&](qint64,QString,qint64 id){saved=id;});
        QObject::connect(&qc,&QcStore::snapshotReady,&qc,[&](qint64,QcControls values){controls=values;});
        qc.setSession(session);qc.initializeOriginal(root);qc.setCatalog(1,{qcProject});qc.load(2);
        c.check(!qcError&&controls.isEmpty(),QStringLiteral("直接打开原质控配置与独立历史库，不创建normalized表"));
        QcControl control;control.name="ControlA";control.manufacturer="Anonymous";qc.saveControl(3,control);qc.load(4);
        c.check(!qcError&&controls.size()==1&&saved>0,QStringLiteral("正式入口新增原质控品并读回"));
        const qint64 controlId=controls.value(0).id;qc.setProjects(5,controlId,{qcProject});qc.load(6);
        c.check(!qcError&&controls[0].projects.size()==1&&controls[0].projects[0].projectId==1,QStringLiteral("原名称关联映射真实项目，不加project_id列"));
        QcBatch batch;batch.controlId=controlId;batch.name="BatchA";batch.expirationDate="2027/10/27";qc.saveBatch(7,batch);qc.load(8);
        c.check(!qcError&&controls[0].batches.size()==1&&controls[0].batches[0].current,QStringLiteral("首批按原规则设为当前批"));
        auto rules=controls[0].projects[0].rules;rules[0].target="12.5000";rules[0].strategies[0]=1;qc.saveRules(9,controls[0].projects[0].id,rules);qc.load(10);
        c.check(!qcError&&controls[0].projects[0].rules[0].target=="12.5000"&&scalar(quality,"SELECT vendor_extra FROM quality_rules LIMIT 1")=="KEEP",QStringLiteral("原规则保存文本精度并保留未知列"));
        const auto unchanged=hash(quality);qc.saveControl(11,controls[0]);c.check(!qcError&&hash(quality)==unchanged,QStringLiteral("无变化保存原质控库字节不变"));
        QcRawResult raw;raw.controlName="ControlA";raw.projectName="TEST";raw.batchName="BatchA";raw.resultValue="[TEST||=Original-12:1-42]";raw.date="2026/10/27";raw.time="12:00:00";
        qc.recordOriginalResult(12,raw);raw.time="13:00:00";raw.resultValue="[TEST||=Original-13:1-43]";qc.recordOriginalResult(13,raw);
        c.check(!qcError&&scalar(history,"SELECT COUNT(*) FROM quality_data").toInt()==1&&scalar(history,"SELECT time FROM quality_data LIMIT 1")=="13:00:00",QStringLiteral("按原同日覆盖规则写真实质控历史库"));
        QcHistoryReply reply;QObject::connect(&qc,&QcStore::historyReady,&qc,[&](qint64,QcHistoryReply value){reply=value;});qc.loadHistory(14,controls[0].projects[0].id);
        c.check(reply.complete()&&reply.sourceRowCount()==1,QStringLiteral("当前批历史从quality_Data原库查询并返回完整快照"));
        qcError=false;qc.removeControl(15,controlId);c.check(qcError,QStringLiteral("原历史引用拒绝删除质控品"));qcError=false;qc.load(16);
        database(quality,{"UPDATE quality_control_list SET vendor_extra='EXTERNAL' WHERE id=1"});qc.saveControl(17,controls[0]);
        c.check(qcError&&scalar(quality,"SELECT vendor_extra FROM quality_control_list WHERE id=1")=="EXTERNAL",QStringLiteral("外部修改拒绝旧质控草稿覆盖"));
        qcError=false;qc.load(18);qc.close();

        CalibrationStore cal;CalibrationRecords records;CalibrationIsiRecords isiRecords;bool calError=false; qint64 calSaved=0;
        QObject::connect(&cal,&CalibrationStore::error,&cal,[&](qint64,QString,QString){calError=true;});
        QObject::connect(&cal,&CalibrationStore::succeeded,&cal,[&](qint64,QString,qint64 id){calSaved=id;});
        QObject::connect(&cal,&CalibrationStore::snapshotReady,&cal,[&](qint64,CalibrationRecords value){records=value;});
        QObject::connect(&cal,&CalibrationStore::isiReady,&cal,[&](qint64,CalibrationIsiRecords value){isiRecords=value;});
        cal.setSession(session);cal.initializeOriginal(root);cal.setOriginalCatalog({project},{unit},ReagentSnapshot());cal.load(100);
        c.check(!calError&&records.size()==1&&records[0].id==100&&records[0].points[0].originalValues.value("result_value")=="12.3456789012345|****",QStringLiteral("原校准编号及高精度/未知结果文本完整读取"));
        auto before=hash(calibration);cal.save(101,records[0]);c.check(!calError&&hash(calibration)==before,QStringLiteral("原校准无变化保存字节不变"));cal.load(102);
        auto edited=records[0];edited.bloodBatch="B1";cal.save(103,edited);cal.load(104);
        c.check(!calError&&scalar(calibration,"SELECT boold_batch FROM calibration_base WHERE calibration_id=100")=="B1"&&scalar(calibration,"SELECT concentration FROM calibration_data LIMIT 1")=="100.000000123456"&&scalar(calibration,"SELECT only_count FROM calibration_data LIMIT 1")=="91|92",QStringLiteral("保存校准品批号不改变原点精度、标志和任务号"));
        cal.setOriginalMethod(105,100,records[0].revision,"折线法","x","y");cal.load(106);
        c.check(!calError&&records[0].method==QStringLiteral("折线法"),QStringLiteral("原计算方法字段真实写回，未伪造拟合结果"));
        cal.setOriginalReview(107,100,records[0].revision,true);cal.load(108);c.check(!calError&&records[0].checked,QStringLiteral("显式审核只保存原is_checked标志"));
        cal.applyOriginalCurve(109,100,records[0].revision,"GroupA");cal.load(110);c.check(!calError&&records[0].appliedBatchGroups.contains("GroupA"),QStringLiteral("原批组绑定写回calibration_batch_group"));
        cal.copy(111,100,records[0].revision);cal.load(112);c.check(!calError&&calSaved==101&&records.size()==2&&scalar(calibration,"SELECT total_count FROM calibraiton_total_count LIMIT 1").toInt()==102,QStringLiteral("复制按原计数分配编号，保留完整点与未知字段"));
        CalibrationRecord copied;for(const auto &value:records)if(value.id==101)copied=value;
        c.check(copied.appliedBatchGroups.isEmpty()&&copied.originalBaseValues.value("vendor_extra")=="KEEP",QStringLiteral("复制不继承批组应用，保留未知列"));
        cal.remove(113,101,copied.revision);cal.load(114);c.check(!calError&&records.size()==1&&scalar(calibration,"SELECT COUNT(*) FROM calibration_data WHERE calibration_id=101").toInt()==1,QStringLiteral("原删除仅隐藏，点数据保留"));
        calError=false;cal.remove(115,100,records[0].revision);c.check(calError,QStringLiteral("应用中的原曲线拒绝删除"));calError=false;
        cal.loadIsi(116);c.check(!calError&&isiRecords.size()==1&&isiRecords[0].isiValue=="1.0000",QStringLiteral("原ISI文本从既有相对目录完整读取"));
        auto isiEdit=isiRecords[0];isiEdit.isiValue="1.1250";cal.saveIsi(117,isiEdit);c.check(!calError&&scalar(isi,"SELECT isi_value FROM isi_value LIMIT 1")=="1.1250"&&scalar(isi,"SELECT vendor_extra FROM isi_value LIMIT 1")=="KEEP",QStringLiteral("原ISI事务写回精度与未知列保留"));
        calError=false;cal.saveIsi(118,isiEdit);c.check(calError,QStringLiteral("旧ISI草稿拒绝覆盖已变化版本"));calError=false;cal.load(119);
        auto denied=session;denied.grantedPowerIds.clear();cal.setSession(denied);cal.setOriginalReview(120,100,records[0].revision,true);c.check(calError,QStringLiteral("没有原power1权限拒绝审核，不按高角色补权"));
        cal.close();
        for(const auto &file:{quality,history,calibration,isi})c.check(scalar(file,"PRAGMA quick_check")=="ok",QStringLiteral("写入后原数据库完整性检查"));
        QFile report(output);if(!report.open(QIODevice::WriteOnly)||report.write(QJsonDocument(QJsonObject{{"format","sn100-original-qc-calibration-rk-v1"},{"passed",c.cases.size()-c.failures},{"failed",c.failures},{"checks",c.cases},{"anonymousFixtureOnly",true},{"realInstrument",false},{"fittingOrInverseComputed",false}}).toJson())<1)++c.failures;
        qInfo()<<"ORIGINAL_QC_CALIBRATION_RESULT"<<c.cases.size()-c.failures<<c.failures;
        QMetaObject::invokeMethod(&app,[&app,failures=c.failures](){app.exit(failures?1:0);},Qt::QueuedConnection);thread.quit();
    });
    thread.start();const int code=app.exec();thread.quit();thread.wait();return code;
}
