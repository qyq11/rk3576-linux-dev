#include "data/hospital_store.h"
#include "data/account_store.h"
#include "instrument/maintenance_store.h"
#include "alarm_history/alarm_history_store.h"
#include "alarm_history/original_alarm_events.h"
#include "original_files/original_ini.h"
#include "protocol/read_only_messages.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QDebug>
#include <functional>

// 仅上传RK执行。只在显式新目录准备匿名原表夹具，口令在运行时随机生成且不写入日志或报告。
namespace
{
bool awaitResult(const std::function<bool()> &done, int limit = 8000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!done() && elapsed.elapsed() < limit)
    {
        QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
    }
    return done();
}
QString fileHash(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256); if (!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
bool bytes(const QString &path, const QByteArray &value)
{
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(value) == value.size();
}
QByteArray readBytes(const QString &path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}
class Connection
{
public:
    QSqlDatabase database; QString name;
    ~Connection() { database.close(); database = QSqlDatabase(); if (!name.isEmpty()) QSqlDatabase::removeDatabase(name); }
    bool open(const QString &path, bool writable = false, bool createFixture = false)
    {
        if (!createFixture && !QFileInfo::exists(path)) return false;
        name = QStringLiteral("ManagementDriver-") + QUuid::createUuid().toString();
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name); database.setDatabaseName(path);
        database.setConnectOptions(writable ? QStringLiteral("QSQLITE_BUSY_TIMEOUT=200") : QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=200"));
        return database.open();
    }
};
bool execute(QSqlDatabase &database, const QString &sql, const QVariantList &values = {})
{
    QSqlQuery query(database); if (!query.prepare(sql)) return false;
    for (const auto &value : values) query.addBindValue(value);
    return query.exec();
}
QVariant scalar(const QString &path, const QString &sql)
{
    Connection connection; if (!connection.open(path)) return {};
    QSqlQuery query(connection.database); if (!query.exec(sql) || !query.next()) return {}; return query.value(0);
}
QString schemaHash(const QString &path)
{
    Connection connection; if (!connection.open(path)) return {};
    QSqlQuery query(connection.database); if (!query.exec(QStringLiteral("SELECT type,name,sql FROM sqlite_master ORDER BY type,name"))) return {};
    QByteArray text;
    while (query.next()) for (int column = 0; column < 3; ++column) { text += query.value(column).toString().toUtf8(); text += '\0'; }
    return QString::fromLatin1(QCryptographicHash::hash(text, QCryptographicHash::Sha256).toHex());
}
bool fixtures(const QString &root, const QString &secret)
{
    if (!QDir().mkpath(root + QStringLiteral("/db/userSet")) || !QDir().mkpath(root + QStringLiteral("/db/systemInfo"))) return false;
    if (!bytes(root + QStringLiteral("/.sn100-original-work-v1"), "sn100-original-work-v1\n")) return false;
    QByteArray ini("\xef\xbb\xbf# 匿名原配置注释保留\r\n[source_keys]\r\n");
    for (int index = 0; index < 134; ++index) ini += "key" + QByteArray::number(index) + "=value" + QByteArray::number(index) + "\r\n";
    if (!bytes(root + QStringLiteral("/config.ini"), ini) || !bytes(root + QStringLiteral("/theme_config.ini"), QByteArray("# original theme\r\n[hospital]\r\nname=主题医院\r\n[theme]\r\ncolor=#00AABB\r\n")) ||
        !bytes(root + QStringLiteral("/config_Debug.ini"), QByteArray("# anonymous configured role\r\n[login]\r\nopen=true\r\ndefaultPermission=3\r\n"))) return false;
    {
        Connection connection; if (!connection.open(root + QStringLiteral("/db/userSet/hospital_Information.db"), true, true)) return false;
        if (!execute(connection.database, QStringLiteral("CREATE TABLE hospital_information(id INTEGER PRIMARY KEY AUTOINCREMENT,column_one VARCHAR,column_two VARCHAR,column_three VARCHAR,type VARCHAR,vendor_note TEXT DEFAULT 'KEEP')")) ||
            !execute(connection.database, QStringLiteral("INSERT INTO hospital_information(column_one,column_two,column_three,type) VALUES('科室甲','KSJ','备注','keshi')"))) return false;
    }
    {
        Connection connection; if (!connection.open(root + QStringLiteral("/db/userSet/user_Information.db"), true, true)) return false;
        if (!execute(connection.database, QStringLiteral("CREATE TABLE user_information(id INTEGER PRIMARY KEY AUTOINCREMENT,name VARCHAR,role INTEGER,pwd VARCHAR,isdefault INTEGER,vendor_note TEXT DEFAULT 'KEEP')")) ||
            !execute(connection.database, QStringLiteral("INSERT INTO user_information(name,role,pwd,isdefault) VALUES('fixture_admin',5,?,0)"), {secret})) return false;
    }
    {
        Connection connection; if (!connection.open(root + QStringLiteral("/db/systemInfo/user.db"), true, true)) return false;
        if (!execute(connection.database, QStringLiteral("CREATE TABLE user_role(id INTEGER PRIMARY KEY AUTOINCREMENT,role VARCHAR)")) ||
            !execute(connection.database, QStringLiteral("CREATE TABLE user_powers(id INTEGER PRIMARY KEY AUTOINCREMENT,describe VARCHAR,can_role_id VARCHAR)"))) return false;
        const QStringList names = {QStringLiteral("访客"),QStringLiteral("送检医师"),QStringLiteral("检验医师"),QStringLiteral("审核医师"),QStringLiteral("工程师"),QStringLiteral("管理员")};
        for (int index = 0; index < names.size(); ++index)
            if (!execute(connection.database, QStringLiteral("INSERT INTO user_role(id,role) VALUES(?,?)"), {index,names[index]})) return false;
        if (!execute(connection.database, QStringLiteral("INSERT INTO user_powers(id,describe,can_role_id) VALUES(1,'校准权限','3;4;5')"))) return false;
    }
    {
        Connection connection; if (!connection.open(root + QStringLiteral("/db/userSet/maintain.db"), true, true)) return false;
        if (!execute(connection.database, QStringLiteral("CREATE TABLE day_maintain(id INTEGER PRIMARY KEY AUTOINCREMENT,day DATE,long_clean TEXT,waste_cup TEXT,waste_water TEXT,waste_cup_box TEXT,shut_down TEXT,vendor_note TEXT DEFAULT 'KEEP')")) ||
            !execute(connection.database, QStringLiteral("CREATE TABLE week_maintain(id INTEGER PRIMARY KEY AUTOINCREMENT,week DATE,clean_dirver VARCHAR,clean_pool VARCHAR,vendor_note TEXT DEFAULT 'KEEP')")) ||
            !execute(connection.database, QStringLiteral("CREATE TABLE month_maintain(id INTEGER PRIMARY KEY AUTOINCREMENT,month DATE,clean_needle VARCHAR,vendor_note TEXT DEFAULT 'KEEP')"))) return false;
    }
    {
        Connection connection; if (!connection.open(root + QStringLiteral("/db/userSet/abnormal_Log.db"), true, true)) return false;
        if (!execute(connection.database, QStringLiteral("CREATE TABLE abnormal_log(id INTEGER PRIMARY KEY AUTOINCREMENT,error_code VARCHAR,error_describe VARCHAR,measure VARCHAR,date_time DATETIME,corrective_action VARCHAR,use_user VARCHAR,vendor_note TEXT DEFAULT 'KEEP')"))) return false;
        for (int index = 0; index < 3; ++index)
            if (!execute(connection.database, QStringLiteral("INSERT INTO abnormal_log(error_code,error_describe,measure,date_time,corrective_action,use_user) VALUES(?,?,?,?,?,?)"),
                {QString::number(6000000+index),QStringLiteral("匿名旧事件"),QString(),QDateTime::currentDateTime().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")),QString(),QStringLiteral("fixture_user")})) return false;
    }
    return true;
}
class Verify
{
public:
    int passed = 0, failed = 0; QJsonArray results;
    void check(bool ok, const QString &name)
    {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << name; ok ? ++passed : ++failed;
        results.append(QJsonObject{{QStringLiteral("name"),name},{QStringLiteral("passed"),ok}});
    }
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 5 || QString::fromLocal8Bit(argv[1]) != QStringLiteral("--data-dir") || QString::fromLocal8Bit(argv[3]) != QStringLiteral("--output-dir")) return 2;
    const QString root = QDir::cleanPath(QString::fromLocal8Bit(argv[2]));
    const QString output = QDir::cleanPath(QString::fromLocal8Bit(argv[4]));
    if (!QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(output) || QFileInfo::exists(root) || QFileInfo::exists(output) || root.contains(QStringLiteral("sn100_host_reference")) ||
        !QDir().mkpath(output)) return 2;
    const QString secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!fixtures(root, secret)) return 2;
    Verify verify;
    HospitalStore::registerTypes(); AccountStore::registerTypes(); MaintenanceStore::registerTypes(); AlarmHistory::Store::registerTypes();
    QThread worker;
    auto *hospital = new HospitalStore; auto *accounts = new AccountStore; auto *maintenance = new MaintenanceStore; auto *alarms = new AlarmHistory::Store;
    for (QObject *object : {static_cast<QObject*>(hospital),static_cast<QObject*>(accounts),static_cast<QObject*>(maintenance),static_cast<QObject*>(alarms)})
    { object->moveToThread(&worker); QObject::connect(&worker,&QThread::finished,object,&QObject::deleteLater); }
    int receipt = 0; bool success = false;
    auto accepted = [&]() { ++receipt; success = true; };
    auto rejected = [&]() { ++receipt; success = false; };
    QString hospitalName; HospitalRows hospitalRows; AccountSession session; AccountRows accountRows; MaintenanceRecords maintenanceRows;
    QString alarmToken;
    QObject::connect(hospital,&HospitalStore::ready,&app,accepted);
    QObject::connect(hospital,&HospitalStore::error,&app,[&](const QString &operation,const QString &message)
    { qWarning().noquote()<<"HOSPITAL_ERROR"<<operation<<message; rejected(); });
    QObject::connect(hospital,&HospitalStore::loaded,&app,[&](const QString &,const QString &name,const HospitalRows &rows) { hospitalName=name;hospitalRows=rows;accepted(); });
    QObject::connect(hospital,&HospitalStore::saved,&app,[&](const QString &,const QString &name,const HospitalRows &rows) { hospitalName=name;hospitalRows=rows;accepted(); });
    QObject::connect(hospital,&HospitalStore::removed,&app,[&](const QString &,qint64) { accepted(); });
    QObject::connect(hospital,&HospitalStore::closed,&app,accepted);
    QObject::connect(accounts,&AccountStore::ready,&app,accepted);
    QObject::connect(accounts,&AccountStore::error,&app,[&](const QString &,const QString &) { rejected(); });
    QObject::connect(accounts,&AccountStore::loaded,&app,[&](const AccountRows &rows) { accountRows=rows;accepted(); });
    QObject::connect(accounts,&AccountStore::sessionChanged,&app,[&](AccountSession value) { session=value; });
    QObject::connect(accounts,&AccountStore::loginFinished,&app,[&](quint64,bool ok,bool,const QString &,const QString &) { ++receipt; success=ok; });
    QObject::connect(accounts,&AccountStore::succeeded,&app,[&](const QString &,bool) { accepted(); });
    QObject::connect(accounts,&AccountStore::closed,&app,accepted);
    QObject::connect(maintenance,&MaintenanceStore::ready,&app,accepted);
    QObject::connect(maintenance,&MaintenanceStore::error,&app,[&](const QString &,const QString &) { rejected(); });
    QObject::connect(maintenance,&MaintenanceStore::loaded,&app,[&](const MaintenanceRecords &rows) { maintenanceRows=rows;accepted(); });
    QObject::connect(maintenance,&MaintenanceStore::succeeded,&app,[&](const QString &) { accepted(); });
    QObject::connect(maintenance,&MaintenanceStore::closed,&app,accepted);
    QObject::connect(alarms,&AlarmHistory::Store::opened,&app,[&](const QString &token,const QString &) { alarmToken=token;accepted(); });
    QObject::connect(alarms,&AlarmHistory::Store::openFailed,&app,[&](AlarmHistory::Error,const QString &) { rejected(); });
    QObject::connect(alarms,&AlarmHistory::Store::originalEventsAppended,&app,[&](quint64,qint64) { accepted(); });
    QObject::connect(alarms,&AlarmHistory::Store::originalEventsError,&app,[&](quint64,const QString &) { rejected(); });
    QObject::connect(alarms,&AlarmHistory::Store::closed,&app,accepted);
    worker.start();
    auto invoke = [&](QObject *object,const std::function<void()> &operation,bool expected,const QString &name)
    {
        const int before=receipt; QMetaObject::invokeMethod(object,operation,Qt::QueuedConnection);
        const bool arrived=awaitResult([&]() { return receipt>before; }); verify.check(arrived && success==expected,name); return arrived && success;
    };
    const QString hospitalFile=root+QStringLiteral("/db/userSet/hospital_Information.db"), accountFile=root+QStringLiteral("/db/userSet/user_Information.db");
    const QString maintainFile=root+QStringLiteral("/db/userSet/maintain.db"), alarmFile=root+QStringLiteral("/db/userSet/abnormal_Log.db");
    const QString userDictionary=root+QStringLiteral("/db/systemInfo/user.db");
    const QString dictionaryBefore=fileHash(userDictionary), themeBefore=fileHash(root+QStringLiteral("/theme_config.ini"));
    QMap<QString,QString> schemas; for(const QString &file:{hospitalFile,accountFile,maintainFile,alarmFile,userDictionary}) schemas.insert(file,schemaHash(file));
    const QByteArray iniBefore=readBytes(root+QStringLiteral("/config.ini")); OriginalIni::Settings beforeSettings; QString message;
    verify.check(OriginalIni::read(root+QStringLiteral("/config.ini"),&beforeSettings,&message) && beforeSettings.values.size()==134 && !beforeSettings.values.contains(QStringLiteral("hospital/name")),QStringLiteral("匿名原配置134键及医院名缺键准备"));
    invoke(hospital,[=]() { hospital->initializeOriginal(root); },true,QStringLiteral("医院原库打开，不新增表或独立库"));
    invoke(hospital,[=]() { hospital->load(QStringLiteral("keshi")); },true,QStringLiteral("医院原字典直接读取"));
    verify.check(hospitalName==QStringLiteral("主题医院") && hospitalRows.size()==1,QStringLiteral("首次医院名按原theme值显示"));
    HospitalRows editedRows=hospitalRows; editedRows.first().columnThree=QStringLiteral("新备注");
    invoke(hospital,[=]() { hospital->save(QStringLiteral("keshi"),QStringLiteral("已存医院"),editedRows); },true,QStringLiteral("字典与医院名实际写原表和全局本键"));
    OriginalIni::Settings afterSettings; verify.check(OriginalIni::read(root+QStringLiteral("/config.ini"),&afterSettings,&message),QStringLiteral("保存后兼容回读全局INI"));
    const QString savedHospital=afterSettings.values.take(QStringLiteral("hospital/name")).toString();
    verify.check(savedHospital==QStringLiteral("已存医院") && afterSettings.values==beforeSettings.values && readBytes(root+QStringLiteral("/config.ini")).startsWith(iniBefore),QStringLiteral("首次新增医院键，原134键类型值及其他原字节保留"));
    verify.check(fileHash(root+QStringLiteral("/theme_config.ini"))==themeBefore && scalar(hospitalFile,QStringLiteral("SELECT vendor_note FROM hospital_information WHERE id=1")).toString()==QStringLiteral("KEEP"),QStringLiteral("theme和未知医院列不改"));
    invoke(hospital,[=]() { hospital->close(); },true,QStringLiteral("医院正常关闭"));
    invoke(hospital,[=]() { hospital->initializeOriginal(root); },true,QStringLiteral("医院同原根重开"));
    invoke(hospital,[=]() { hospital->load(QStringLiteral("keshi")); },true,QStringLiteral("重开读取已保存医院名"));
    verify.check(hospitalName==QStringLiteral("已存医院") && hospitalRows.first().columnThree==QStringLiteral("新备注"),QStringLiteral("医院名称与字典保存重开闭环"));
    invoke(hospital,[=]() { hospital->save(QStringLiteral("keshi"),QString(),editedRows); },false,QStringLiteral("医院非法名称不改库"));
    HospitalRow newHospitalRow;newHospitalRow.type=QStringLiteral("keshi");newHospitalRow.columnOne=QStringLiteral("新增科室");
    const HospitalRows newHospitalRows{newHospitalRow};
    invoke(hospital,[=]() {hospital->save(QStringLiteral("keshi"),QStringLiteral("已存医院"),newHospitalRows);},true,QStringLiteral("医院按原规则新增可空助记符备注"));
    verify.check(scalar(hospitalFile,QStringLiteral("SELECT typeof(column_two)||':'||column_two||':'||typeof(column_three)||':'||column_three FROM hospital_information WHERE column_one='新增科室'")).toString()==QStringLiteral("text::text:"),QStringLiteral("医院新增可空字段按原DAL绑定空TEXT"));
    const qint64 addedHospitalId=scalar(hospitalFile,QStringLiteral("SELECT id FROM hospital_information WHERE column_one='新增科室'")).toLongLong();
    invoke(hospital,[=]() {hospital->remove(QStringLiteral("keshi"),addedHospitalId);},true,QStringLiteral("医院新增原字典行可按类别删除"));
    invoke(accounts,[=]() { accounts->initializeOriginal(root); },true,QStringLiteral("原账户库和user.db角色权限读取"));
    invoke(accounts,[=]() { accounts->applyOriginalConfiguredSession(100); },false,QStringLiteral("登录开启时不能从关闭登录入口建立默认角色"));
    invoke(accounts,[=]() { accounts->setSession(QStringLiteral("fixture_admin"),true); },false,QStringLiteral("原用户名不能绕口令建立高权限模拟会话"));
    verify.check(!session.authenticated,QStringLiteral("拒绝模拟提权后身份未变成管理员"));
    const QString beforeEmptyLoginHash=fileHash(accountFile), debugBefore=fileHash(root+QStringLiteral("/config_Debug.ini"));
    invoke(accounts,[=]() { accounts->authenticate(101,QStringLiteral("fixture_admin"),QString(),true); },true,QStringLiteral("显式空口令按原配置登录Guest默认角色"));
    verify.check(session.authenticated && session.source==AccountSessionSource::Configured && session.name==QStringLiteral("Guest") && session.role==3 && AccountStore::hasOriginalPower(session,QStringLiteral("1")) && fileHash(accountFile)==beforeEmptyLoginHash && fileHash(root+QStringLiteral("/config_Debug.ini"))==debugBefore,QStringLiteral("空口令不查用户名身份、不保存默认账户且权限来自原配置和字典"));
    verify.check(bytes(root+QStringLiteral("/config_Debug.ini"),QByteArray("# anonymous configured role\r\n[login]\r\nopen=false\r\ndefaultPermission=2\r\n")),QStringLiteral("仅匿名夹具切换关闭登录配置"));
    invoke(accounts,[=]() { accounts->applyOriginalConfiguredSession(102); },true,QStringLiteral("关闭登录时按配置直接建立默认角色"));
    verify.check(session.name==QStringLiteral("Guest") && session.role==2 && session.source==AccountSessionSource::Configured && !AccountStore::hasOriginalPower(session,QStringLiteral("1")),QStringLiteral("配置角色不冒充原管理员也不按高低补授权"));
    invoke(accounts,[=]() { accounts->add(QStringLiteral("fixture_configured"),secret,secret,2); },true,QStringLiteral("有据配置身份按原角色规则实际新增账户"));
    invoke(accounts,[=]() { accounts->remove(QStringLiteral("fixture_configured"),secret); },true,QStringLiteral("配置身份按原目标口令规则删除测试账户"));
    verify.check(bytes(root+QStringLiteral("/config_Debug.ini"),QByteArray("# anonymous configured role\r\n[login]\r\nopen=true\r\ndefaultPermission=2\r\n")),QStringLiteral("仅匿名夹具恢复登录开启"));
    invoke(accounts,[=]() { accounts->add(QStringLiteral("must_not_exist"),secret,secret,2); },false,QStringLiteral("配置关闭登录来源失效后写入拒绝并清除身份"));
    verify.check(!session.authenticated && scalar(accountFile,QStringLiteral("SELECT count(*) FROM user_information WHERE name='must_not_exist'")).toInt()==0,QStringLiteral("失效配置不遗留可写身份或账户"));
    verify.check(bytes(root+QStringLiteral("/config_Debug.ini"),QByteArray("# anonymous configured role\r\n[login]\r\nopen=true\r\ndefaultPermission=0\r\n")),QStringLiteral("仅匿名夹具准备原访客默认角色"));
    invoke(accounts,[=]() { accounts->authenticate(103,QStringLiteral("ignored"),QString(),false); },true,QStringLiteral("原角色0可经明确空口令建立配置会话"));
    const AccountSession configuredGuest=session;
    invoke(accounts,[=]() { accounts->authenticate(1,QStringLiteral("fixture_admin"),secret,false); },true,QStringLiteral("匿名原账户真实口令认证"));
    verify.check(session.authenticated && session.role==5 && session.roleLabel==QStringLiteral("管理员") && session.originalPolicySource==root && AccountStore::hasOriginalPower(session,QStringLiteral("1")),QStringLiteral("会话角色名与原权限1授权来自user.db"));
    invoke(accounts,[=]() { accounts->load(); },true,QStringLiteral("原账户列表无口令字段"));
    verify.check(accountRows.size()==1 && accountRows.first().roleLabel==QStringLiteral("管理员"),QStringLiteral("账户页面原角色标签可用"));
    invoke(accounts,[=]() { accounts->add(QStringLiteral("fixture_user"),secret,secret,2); },true,QStringLiteral("按原角色范围新增匿名账户"));
    invoke(accounts,[=]() { accounts->remove(QStringLiteral("fixture_admin"),secret); },false,QStringLiteral("原管理员不允许删除规则保留"));
    invoke(accounts,[=]() { accounts->update(QStringLiteral("fixture_user"),secret,QStringLiteral("fixture_user2"),secret,secret,2); },true,QStringLiteral("原账户更新成功并退出身份"));
    verify.check(!session.authenticated,QStringLiteral("原更新后登出行为保留"));
    invoke(accounts,[=]() { accounts->authenticate(2,QStringLiteral("fixture_admin"),secret,false); },true,QStringLiteral("更新后重新认证"));
    invoke(accounts,[=]() { accounts->remove(QStringLiteral("fixture_user2"),secret); },true,QStringLiteral("正确口令按原权限删除普通账户"));
    verify.check(scalar(accountFile,QStringLiteral("SELECT count(*) FROM user_information")).toInt()==1 && scalar(accountFile,QStringLiteral("SELECT vendor_note FROM user_information WHERE id=1")).toString()==QStringLiteral("KEEP") && fileHash(userDictionary)==dictionaryBefore,QStringLiteral("原账户操作不改未知列和权限字典"));
    const AccountSession authenticated=session;
    invoke(maintenance,[=]() { maintenance->initializeOriginal(root); },true,QStringLiteral("直接接入原maintain.db三表"));
    QMetaObject::invokeMethod(maintenance,[=]() { maintenance->setSession(authenticated); },Qt::QueuedConnection);
    invoke(maintenance,[=]() { maintenance->record(QStringLiteral("waste_cup"),QDate::currentDate()); },true,QStringLiteral("当天人工维护写回原表"));
    const QString firstMaintenanceHash=fileHash(maintainFile);
    invoke(maintenance,[=]() { maintenance->record(QStringLiteral("waste_cup"),QDate::currentDate()); },true,QStringLiteral("重复人工维护不增日期行"));
    verify.check(scalar(maintainFile,QStringLiteral("SELECT count(*) FROM day_maintain")).toInt()==1 && fileHash(maintainFile)==firstMaintenanceHash,QStringLiteral("原无UNIQUE日期表仍仅一行且重复不重写"));
    QMetaObject::invokeMethod(maintenance,[=]() { maintenance->setSession(configuredGuest); },Qt::QueuedConnection);
    invoke(maintenance,[=]() { maintenance->record(QStringLiteral("waste_cup"),QDate::currentDate()); },true,QStringLiteral("原已登录访客也能按原界面规则登记人工维护"));
    invoke(maintenance,[=]() { maintenance->loadMonth(QDate::currentDate()); },true,QStringLiteral("原维护日历读取"));
    verify.check(maintenanceRows.size()==1 && maintenanceRows.first().action==QStringLiteral("waste_cup") && scalar(maintainFile,QStringLiteral("SELECT vendor_note FROM day_maintain LIMIT 1")).toString()==QStringLiteral("KEEP") && scalar(maintainFile,QStringLiteral("SELECT typeof(long_clean)||':'||long_clean FROM day_maintain LIMIT 1")).toString()==QStringLiteral("text:"),QStringLiteral("维护动作、原空TEXT及未知列保留"));
    {
        Connection fixtureEditor;verify.check(fixtureEditor.open(maintainFile,true) && execute(fixtureEditor.database,QStringLiteral("UPDATE day_maintain SET waste_water='2'")),QStringLiteral("仅匿名夹具准备原正数完成值2"));
    }
    const QString positiveFlagHash=fileHash(maintainFile);
    invoke(maintenance,[=]() {maintenance->loadMonth(QDate::currentDate());},true,QStringLiteral("原正数维护值按原日历读取"));
    verify.check(maintenanceRows.size()==2 && fileHash(maintainFile)==positiveFlagHash && scalar(maintainFile,QStringLiteral("SELECT waste_water FROM day_maintain LIMIT 1")).toString()==QStringLiteral("2"),QStringLiteral("值2显示完成且原值字节不改写"));
    {
        Connection fixtureEditor;verify.check(fixtureEditor.open(maintainFile,true) && execute(fixtureEditor.database,QStringLiteral("INSERT INTO day_maintain(day,waste_cup_box) SELECT day,'1' FROM day_maintain LIMIT 1")),QStringLiteral("仅匿名夹具准备原重复日期行"));
    }
    invoke(maintenance,[=]() {maintenance->loadMonth(QDate::currentDate());},true,QStringLiteral("原重复日期读取仍按首行显示"));
    verify.check(maintenanceRows.size()==2,QStringLiteral("原重复日期不合并成额外完成项"));
    invoke(maintenance,[=]() {maintenance->record(QStringLiteral("waste_cup_box"),QDate::currentDate());},false,QStringLiteral("原重复日期写入明确拒绝不猜测覆盖行"));
    invoke(maintenance,[=]() { maintenance->record(QStringLiteral("long_clean"),QDate::currentDate()); },false,QStringLiteral("未收到设备完成不伪造洗针完成"));
    invoke(alarms,[=]() { alarms->openOriginal(root); },true,QStringLiteral("原告警历史只读查询来源打开"));
    Sn100Protocol::ReadOnlyMessage parsed; AlarmHistory::Rows events; QString error;
    OriginalRuntime::AlarmRule rule;rule.code=QStringLiteral("5000001");rule.description=QStringLiteral("通道%1需检查%2");rule.measure=QStringLiteral("检查连接");
    const QVector<OriginalRuntime::AlarmRule> rules{rule};
    verify.check(Sn100Protocol::parseReadOnlyMessage(QByteArray("Ql[1][5000001][A|B]"),&parsed,&error) && AlarmHistory::originalNewEvents(parsed,rules,authenticated.name,QDateTime::currentDateTime(),&events,&error) && events.size()==1 && events.first().description==QStringLiteral("通道A需检查B"),QStringLiteral("新告警Ql按原CSV和参数转换"));
    const QString sourceToken=alarmToken;const AlarmHistory::Rows alarmEvents=events;
    invoke(alarms,[=]() { alarms->appendOriginalEvents(1,sourceToken,alarmEvents); },true,QStringLiteral("明确新告警追加原abnormal_Log"));
    verify.check(scalar(alarmFile,QStringLiteral("SELECT count(*) FROM abnormal_log")).toInt()==4 && scalar(alarmFile,QStringLiteral("SELECT count(*) FROM abnormal_log WHERE vendor_note='KEEP'")).toInt()==4,QStringLiteral("原告警仅追加且未知列默认保留"));
    invoke(alarms,[=]() { alarms->appendOriginalEvents(1,sourceToken,alarmEvents); },false,QStringLiteral("同请求新告警不重复追加"));
    invoke(alarms,[=]() { alarms->appendOriginalEvents(2,QStringLiteral("stale-source"),alarmEvents); },false,QStringLiteral("旧来源告警不写入当前根"));
    AlarmHistory::Rows invalidEvents=events;invalidEvents.first().dateTime.clear();
    invoke(alarms,[=]() { alarms->appendOriginalEvents(2,sourceToken,invalidEvents); },false,QStringLiteral("未知接收时间不造历史"));
    Sn100Protocol::ReadOnlyMessage current;AlarmHistory::Rows untouched=events;
    verify.check(Sn100Protocol::parseReadOnlyMessage(QByteArray("Qk[1][5000001][A]"),&current,&error) && !AlarmHistory::originalNewEvents(current,rules,authenticated.name,QDateTime::currentDateTime(),&untouched,&error) && untouched.size()==events.size(),QStringLiteral("当前告警轮询不转成新事件写库"));
    verify.check(Sn100Protocol::parseReadOnlyMessage(QStringLiteral("QL[1][2000001][匿名主控异常][检查提示]").toUtf8(),&parsed,&error) && AlarmHistory::originalNewEvents(parsed,rules,authenticated.name,QDateTime::currentDateTime(),&events,&error) && events.first().errorCode==QStringLiteral("2000001*"),QStringLiteral("主控QL描述处理提示及星号原样映射"));
    const AlarmHistory::Rows boardEvents=events;
    invoke(alarms,[=]() { alarms->appendOriginalEvents(3,sourceToken,boardEvents); },true,QStringLiteral("主控新事件追加原历史"));
    bool queryDone=false,queryOk=false;AlarmHistory::Rows history;
    QObject::connect(alarms,&AlarmHistory::Store::continuationNeeded,&app,[&](AlarmHistory::Request request,qint64,quint64 step) { QMetaObject::invokeMethod(alarms,[=]() { alarms->advance(request.key,step); },Qt::QueuedConnection); });
    QObject::connect(alarms,&AlarmHistory::Store::batchReady,&app,[&](AlarmHistory::Request request,const AlarmHistory::Rows &rows,bool more,quint64 step) { history+=rows;if(more)QMetaObject::invokeMethod(alarms,[=]() { alarms->advance(request.key,step); },Qt::QueuedConnection); });
    QObject::connect(alarms,&AlarmHistory::Store::finished,&app,[&](AlarmHistory::Request,AlarmHistory::Completion,qint64 delivered,qint64 matching) {queryDone=true;queryOk=delivered==matching;});
    QObject::connect(alarms,&AlarmHistory::Store::failed,&app,[&](AlarmHistory::Request,AlarmHistory::Error,const QString &,qint64) {queryDone=true;queryOk=false;});
    AlarmHistory::Request request;request.key={1,1,sourceToken};request.scope=AlarmHistory::Scope::DateInterval;request.first=QDate::currentDate();request.last=request.first;request.today=request.first;request.batchSize=1;
    const int beforeAppend=receipt;
    QMetaObject::invokeMethod(alarms,[=]() { alarms->begin(request);alarms->appendOriginalEvents(4,sourceToken,alarmEvents); },Qt::QueuedConnection);
    verify.check(awaitResult([&]() {return queryDone && receipt>beforeAppend;}) && queryOk && success && history.size()==5 && scalar(alarmFile,QStringLiteral("SELECT count(*) FROM abnormal_log")).toInt()==6,QStringLiteral("只读查询快照不掺新行，排队新事件查询后提交"));
    bool sameSchema=true;for(auto item=schemas.cbegin();item!=schemas.cend();++item)sameSchema&=schemaHash(item.key())==item.value();
    verify.check(sameSchema && !QFileInfo::exists(root+QStringLiteral("/maintenance.db")) && !QFileInfo::exists(root+QStringLiteral("/hospital_Information.db")) && !QFileInfo::exists(root+QStringLiteral("/user_Information.db")),QStringLiteral("四原库结构及路径分类不变，未造独立替代库"));
    invoke(hospital,[=]() {hospital->close();},true,QStringLiteral("医院连接回收"));invoke(accounts,[=]() {accounts->close();},true,QStringLiteral("账户连接回收"));
    invoke(maintenance,[=]() {maintenance->close();},true,QStringLiteral("维护连接回收"));invoke(alarms,[=]() {alarms->close();},true,QStringLiteral("告警连接回收"));worker.quit();worker.wait();
    const QJsonObject report{{QStringLiteral("passed"),verify.passed},{QStringLiteral("failed"),verify.failed},{QStringLiteral("results"),verify.results},
        {QStringLiteral("scope"),QStringLiteral("仅RK匿名原表夹具；原路径、未知列、医院134原键/字节、角色权限、维护与新告警追加；没有连接仪器，没有输出账户口令或真实记录。")}};
    if(!bytes(output+QStringLiteral("/verification.json"),QJsonDocument(report).toJson(QJsonDocument::Indented)))return 2;
    qInfo()<<"MANAGEMENT_ORIGINAL_RESULT"<<verify.passed<<verify.failed;return verify.failed==0?0:1;
}
