// 仅在RK执行：真实HistoryStore工作线程与原表形状；SQL写入只准备全新匿名夹具。
#include "history/history_store.h"
#include "history/history_current_display_state.h"
#include "projects/project_advanced.h"
#include "history/report_document.h"
#include "history/report_render.h"
#include "history/report_controller.h"
#include "history/history_page.h"
#include "qtrpt.h"
#include <QTableWidget>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtTest>
#include <functional>
#include <memory>

class OriginalHistoryRkTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> directory;
    QString root;
    QThread worker;
    HistoryStore *store = nullptr;
    std::unique_ptr<HistoryCurrentDisplayState> displayState;
    std::unique_ptr<QSignalSpy> ready, errors, queries, details, patients, loaded, saved, exported, accepted;
    qint64 sequence = 1000;
    QString fixtureError;

    bool until(const std::function<bool()> &condition, int milliseconds = 10000)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        while (!condition() && elapsed.elapsed() < milliseconds)
        {
            QEventLoop loop;
            QTimer::singleShot(10, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return condition();
    }
    bool bytes(const QString &relative, const QByteArray &content)
    {
        const QString path = QDir(root).filePath(relative);
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(content) == content.size();
    }
    bool database(const QString &relative, const QStringList &statements)
    {
        const QString path = QDir(root).filePath(relative);
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        const QString name = QUuid::createUuid().toString();
        bool good = true;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(path);
            good = db.open();
            if (good) for (const QString &sql : statements)
            {
                QSqlQuery query(db);
                if (!query.exec(sql)) { fixtureError = query.lastError().text(); good = false; break; }
            }
            else fixtureError = db.lastError().text();
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        return good;
    }
    QVariant audit(const QString &sql)
    {
        const QString name = QUuid::createUuid().toString();
        QVariant result;
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", name);
            db.setDatabaseName(QDir(root).filePath("result/results.db"));
            db.setConnectOptions("QSQLITE_OPEN_READONLY");
            if (db.open())
            {
                QSqlQuery query(db);
                if (query.exec(sql) && query.next()) result = query.value(0);
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(name);
        return result;
    }
    QStringList definitions(const QStringList &names)
    {
        QStringList result;
        for (const QString &name : names) result << '"' + name + "\" TEXT";
        return result;
    }
    bool fixture()
    {
        if (!bytes(".sn100-original-work-v1", "sn100-original-work-v1\n")) return false;
        const QString templatePath = qEnvironmentVariable("SN100_ORIGINAL_REPORT_XML");
        if (!templatePath.isEmpty())
        {
            QFile file(templatePath);
            if (!QDir::isAbsolutePath(templatePath) || !file.open(QIODevice::ReadOnly) ||
                !bytes("resource/report_template/report.xml", file.readAll())) return false;
        }
        if (!database("db/systemInfo/option_Unit.db", {
            "CREATE TABLE option_unit(id INTEGER PRIMARY KEY,unit TEXT,type TEXT,ratio TEXT)",
            "INSERT INTO option_unit VALUES(1,'mg/L','Calibration','1000'),(2,'g/L','Rate','10')"})) return false;
        if (!database("db/userSet/reagent_Information.db", {
            "CREATE TABLE reagent_list(id INTEGER PRIMARY KEY,reagent_name TEXT,reagent_type TEXT,valid INTEGER)",
            "INSERT INTO reagent_list VALUES(1,'ANON_R','r_reagent',1),(2,'ANON_C','cleaner',1),(3,'ANON_D','s_ibs',1)"})) return false;
        const QStringList sample = ProjectAdvancedEditor::sampleColumns();
        const QStringList reagent = ProjectAdvancedEditor::reagentColumns();
        QStringList project{
            "CREATE TABLE project_base(name TEXT,value TEXT)",
            "INSERT INTO project_base VALUES('xuhao','1'),('project_id','010'),('project_en_name','PT'),('project_cn_name','匿名PT'),('is_used','1'),('fixed','0'),('project_lis_code','PT'),('vendor_unknown','KEEP_BASE')",
            "CREATE TABLE AlgParam(key TEXT,value TEXT)", "INSERT INTO AlgParam VALUES('vendor_algorithm','KEEP_ALGORITHM')",
            "CREATE TABLE project_sample_rate(xuhao TEXT,sample_default_option TEXT," + definitions(sample).join(',') + ")",
            "INSERT INTO project_sample_rate(xuhao,sample_rate,sample_option,sample_default_option,sample_amount_ul) VALUES('1','1/2','1','1','20')",
            "CREATE TABLE project_reagent(xuhao TEXT," + definitions(reagent).join(',') + ")",
            "INSERT INTO project_reagent(xuhao,reagent_name,reagent_amount_ul) VALUES('1','ANON_R','30')",
            "CREATE TABLE project_results(result_id INTEGER,result_type TEXT,result_name TEXT,decimal_point TEXT,unit_name TEXT,test_low_range TEXT,test_up_range TEXT,normal_low_range TEXT,normal_up_range TEXT,is_used TEXT,main_result TEXT,display TEXT,lis_code TEXT)",
            "INSERT INTO project_results VALUES(1,'Original','PT Time','2','s','0','999','0','999','1','1','1','PT1'),(2,'Calibration','PT C','3','mg/L','0','999','0','999','1','0','1','PT2'),(3,'Rate','PT Rate','2','g/L','0','999','0','999','1','0','1','PT3'),(4,'INR','PT INR','2','','0','999','0','999','1','0','1','PT4'),(5,'MDA','PT MDA','2','mabs','0','999','0','999','0','0','0','PT5')",
            "CREATE TABLE project_mda(name TEXT,rate_select TEXT)",
            "CREATE TABLE project_quality(control_name TEXT,sample_rate TEXT,result_control TEXT,result_rule TEXT,rule_value TEXT,control_batch_current TEXT,control_batch_prv TEXT)"};
        if (!database("project/PT.db", project)) return false;
        QStringList reports{
            "CREATE TABLE report_data(id INTEGER PRIMARY KEY,row_id INTEGER,sn INTEGER,type INTEGER,statue INTEGER,uploaded INTEGER,is_auto_id INTEGER,sample_hole TEXT,sample_id TEXT,data_time TEXT,visible INTEGER,sex INTEGER,age INTEGER,name TEXT,department TEXT,patient_type TEXT,application_serial TEXT,bed_number TEXT,jianyan TEXT,checker TEXT,auditor TEXT,remark TEXT,state TEXT,vendor_unknown TEXT)",
            "INSERT INTO report_data VALUES(1,77,17,1,0,0,0,'S01-1','ANON-17','20261027120000',1,0,30,'匿名甲','检验','门诊','A17','B1','检验者','送检者','审核者','匿名备注','0','KEEP_PATIENT')",
            "INSERT INTO report_data VALUES(2,78,18,1,0,0,0,'S01-2','ANON-18','20261027120100',1,1,40,'匿名乙','检验','门诊','A18','B2','检验者','送检者','审核者','匿名备注','0','KEEP_UNKNOWN')",
            "CREATE TABLE report_results(id INTEGER PRIMARY KEY,fid INTEGER,project_name TEXT,task_id INTEGER,uploaded INTEGER,calibration_id TEXT,abnormal_type TEXT,rate TEXT,sample_id TEXT,data_time TEXT,original_value TEXT,calibration_value TEXT,rate_value TEXT,inr_value TEXT,mda_value TEXT,formula_value TEXT,vendor_unknown TEXT)",
            "INSERT INTO report_results VALUES(1,77,'PT',101,0,'','','1/2','ANON-17','20261027120001','0012.34560','0.00123460','0000.1250','1.00000','0.00000900','F(原串)','KEEP_RESULT_1')",
            "INSERT INTO report_results VALUES(2,77,'PT',102,0,'','','1/2','ANON-17','20261027120002','0013.50000','0.00250000','0000.2500','1.25000','0.00000800','SECOND_FORMULA','KEEP_RESULT_2')",
            "INSERT INTO report_results VALUES(3,78,'NO_PROJECT',103,0,'','','RAW','ANON-18','20261027120101','UNKNOWN_RAW','<0.01','raw_rate','raw_inr','raw_mda','raw_formula','KEEP_RESULT_3')",
            "CREATE TABLE report_column_header(id INTEGER PRIMARY KEY,project_name TEXT,table_type INTEGER,display INTEGER,result_type INTEGER,column_index INTEGER,display_name TEXT,vendor_unknown TEXT)",
            "INSERT INTO report_column_header VALUES(1,'Name',1,1,0,0,'原姓名列','KEEP_HEADER'),(2,'PT',2,1,1,0,'原PT列','KEEP_PT_HEADER'),(3,'OLD_PROJECT',2,0,1,99,'旧未知列','KEEP_OLD_HEADER')",
            "CREATE TABLE report_statue(id INTEGER PRIMARY KEY,statue INTEGER)",
            "CREATE TABLE total_count(id INTEGER PRIMARY KEY,total_count INTEGER,project_total_count INTEGER,row_total_count INTEGER,cup_conut INTEGER,cup_limit_count INTEGER,auto_string INTEGER,count INTEGER)",
            "INSERT INTO total_count VALUES(1,103,3,78,0,0,0,0)"};
        return database("result/results.db", reports);
    }
    bool initialize()
    {
        const int previous = ready->count(), previousError = errors->count();
        QMetaObject::invokeMethod(store, [this]() { store->initializeOriginal(root); }, Qt::QueuedConnection);
        return until([&]() { return ready->count() > previous || errors->count() > previousError; }) && ready->count() == previous + 1;
    }
    HistoryFilter filter() const
    {
        HistoryFilter value;
        value.start = QDateTime(QDate(2026, 10, 1), QTime(0, 0));
        value.end = QDateTime(QDate(2026, 10, 31), QTime(23, 59, 59));
        value.limit = 20;
        return value;
    }
    bool query(HistoryReports *reports)
    {
        const qint64 request = ++sequence;
        if (!displayState->expectQuery(request)) return false;
        const int previous = queries->count(), previousAccepted = accepted->count(), previousError = errors->count();
        const HistoryFilter selected = filter();
        QMetaObject::invokeMethod(store, [=]() { store->query(request, selected); }, Qt::QueuedConnection);
        if (!until([&]() { return accepted->count() > previousAccepted || errors->count() > previousError; })) return false;
        if (queries->count() != previous + 1 || accepted->count() != previousAccepted + 1 || !displayState->valid()) return false;
        *reports = displayState->reports();
        return true;
    }
    bool detail(qint64 reportId, HistoryReport *report)
    {
        const qint64 request = ++sequence;
        if (!displayState->expectDetail(request)) return false;
        const int previous = details->count(), previousError = errors->count();
        QMetaObject::invokeMethod(store, [=]() { store->detail(request, reportId); }, Qt::QueuedConnection);
        if (!until([&]() { return details->count() > previous || errors->count() > previousError; })) return false;
        if (details->count() != previous + 1 || !until([&]() { return displayState->valid(); })) return false;
        *report = displayState->detail();
        return true;
    }
    static QStringList rawValues(const HistoryResult &result)
    {
        QStringList values;
        for (const auto &value : result.storedValues) values << value.rawValue;
        return values;
    }

private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("本驱动只在RK执行；电脑只负责编辑、源码检查和ARM64交叉编译。");
#endif
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_HISTORY_TEST_ROOT")));
        QVERIFY(QDir().mkpath(qEnvironmentVariable("SN100_HISTORY_TEST_ROOT")));
        HistoryStore::registerTypes();
    }
    void init()
    {
        directory.reset(new QTemporaryDir(QDir(qEnvironmentVariable("SN100_HISTORY_TEST_ROOT")).filePath("original_history_XXXXXX")));
        QVERIFY(directory->isValid());
        root = directory->path();
        fixtureError.clear();
        QVERIFY2(fixture(), qPrintable(fixtureError));
        store = new HistoryStore;
        store->moveToThread(&worker);
        connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        ready.reset(new QSignalSpy(store, &HistoryStore::ready)); errors.reset(new QSignalSpy(store, &HistoryStore::error));
        queries.reset(new QSignalSpy(store, &HistoryStore::queryReady)); details.reset(new QSignalSpy(store, &HistoryStore::detailReady));
        patients.reset(new QSignalSpy(store, &HistoryStore::patientSaved)); loaded.reset(new QSignalSpy(store, &HistoryStore::displayLoaded));
        saved.reset(new QSignalSpy(store, &HistoryStore::displaySaved)); exported.reset(new QSignalSpy(store, &HistoryStore::csvExported));
        displayState.reset(new HistoryCurrentDisplayState(store));
        accepted.reset(new QSignalSpy(displayState.get(), &HistoryCurrentDisplayState::queryAccepted));
        worker.start();
        AccountSession session;
        session.name = "anonymousHistoryEngineer"; session.role = 4; session.authenticated = true;
        session.source = AccountSessionSource::Simulated; session.actionReady = true;
        QMetaObject::invokeMethod(store, [=]() { store->setSession(session); }, Qt::QueuedConnection);
    }
    void cleanup()
    {
        if (store && worker.isRunning())
        {
            QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
            worker.quit(); QVERIFY(worker.wait(3000)); store = nullptr;
        }
        accepted.reset(); displayState.reset(); ready.reset(); errors.reset(); queries.reset(); details.reset();
        patients.reset(); loaded.reset(); saved.reset(); exported.reset(); directory.reset();
    }
    void readsSixOriginalStringsAndRepeatTasks()
    {
        QVERIFY(initialize());
        HistoryReports reports;
        QVERIFY(query(&reports)); QCOMPARE(reports.size(), 2);
        const auto report = reports.first();
        QCOMPARE(report.id, qint64(1)); QCOMPARE(report.rowId, qint64(77));
        QCOMPARE(report.results.size(), 2); QCOMPARE(report.results[0].taskId, qint64(101)); QCOMPARE(report.results[1].taskId, qint64(102));
        QCOMPARE(rawValues(report.results[0]), QStringList({"0012.34560", "0.00123460", "0000.1250", "1.00000", "0.00000900", QStringLiteral("F(原串)")}));
        QCOMPARE(rawValues(report.results[1]), QStringList({"0013.50000", "0.00250000", "0000.2500", "1.25000", "0.00000800", "SECOND_FORMULA"}));
        QCOMPARE(displayState->source().catalogVersion, QStringLiteral("sn100-original-projects-v1"));
        QCOMPARE(displayState->source().formatVersion, QStringLiteral("sn100-original-project-result-format-v1"));
        QCOMPARE(audit("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN('report_values','meta','report_columns')").toInt(), 0);
    }
    void appliesKnownUnitsAndPrecisionWithoutRewriting()
    {
        QVERIFY(initialize());
        HistoryReport report; QVERIFY(detail(1, &report));
        const auto &values = report.results.first().storedValues;
        QCOMPARE(values.size(), 6);
        QCOMPARE(values[0].unit, QStringLiteral("s")); QCOMPARE(HistoryStore::displayValue(values[0]), QStringLiteral("12.35"));
        QCOMPARE(values[1].unit, QStringLiteral("mg/L")); QCOMPARE(values[1].factor, 1000.0);
        QCOMPARE(HistoryStore::displayValue(values[1]), QStringLiteral("1.235"));
        QCOMPARE(values[2].unit, QStringLiteral("g/L")); QCOMPARE(values[2].factor, 10.0);
        QCOMPARE(HistoryStore::displayValue(values[2]), QStringLiteral("1.25"));
        QCOMPARE(values[5].formatOrigin, HistoryValueFormatOrigin::UnknownCurrent);
        QCOMPARE(HistoryStore::displayValue(values[5]), QStringLiteral("F(原串)"));
        QCOMPARE(audit("SELECT calibration_value FROM report_results WHERE id=1").toString(), QStringLiteral("0.00123460"));
    }
    void preservesUnknownProjectAndRejectsFakeOldCatalogSource()
    {
        QVERIFY(initialize());
        HistoryReports reports; QVERIFY(query(&reports));
        const auto unknown = reports.last().results.first();
        QCOMPARE(unknown.project, QStringLiteral("NO_PROJECT")); QCOMPARE(unknown.projectId, qint64(0));
        QCOMPARE(rawValues(unknown), QStringList({"UNKNOWN_RAW", "<0.01", "raw_rate", "raw_inr", "raw_mda", "raw_formula"}));
        for (const auto &value : unknown.storedValues)
        { QCOMPARE(value.formatOrigin, HistoryValueFormatOrigin::UnknownCurrent); QCOMPARE(HistoryStore::displayValue(value), value.rawValue); }
        const auto original = displayState->source();
        const qint64 request = ++sequence;
        QVERIFY(displayState->expectQuery(request));
        HistoryCurrentFormatSource fake = original;
        fake.requestRevision = 900;
        fake.catalogVersion = QStringLiteral("sn100-catalog-offline-v1"); // 故意与真实原库formatVersion不配套。
        const HistoryFilter selected = filter();
        QSignalSpy rejected(displayState.get(), &HistoryCurrentDisplayState::rejected);
        QMetaObject::invokeMethod(store, [=]()
        {
            emit store->currentFormatInvalidated(request, 900, "anonymous fake source injection");
            emit store->currentFormatRead(request, fake);
            emit store->queryReady(request, selected, reports, reports.size());
        }, Qt::QueuedConnection);
        QVERIFY(until([&]() { return rejected.count() == 1; })); QVERIFY(!displayState->valid());
    }
    void savesPatientInOriginalTableAndReopens()
    {
        QVERIFY(initialize());
        HistoryReport report; QVERIFY(detail(1, &report));
        const auto raw = rawValues(report.results.first());
        HistoryPatient patient = report.patient;
        patient.name = QStringLiteral("匿名修改姓名"); patient.age = 41; patient.sex = 1;
        patient.department = QStringLiteral("新科室"); patient.remark = QStringLiteral("保留原结果");
        const qint64 request = ++sequence;
        QMetaObject::invokeMethod(store, [=]() { store->savePatient(request, patient); }, Qt::QueuedConnection);
        QVERIFY(until([&]() { return patients->count() == 1 || !errors->isEmpty(); })); QCOMPARE(patients->count(), 1);
        QCOMPARE(audit("SELECT vendor_unknown FROM report_data WHERE id=1").toString(), QStringLiteral("KEEP_PATIENT"));
        QCOMPARE(audit("SELECT vendor_unknown FROM report_results WHERE id=1").toString(), QStringLiteral("KEEP_RESULT_1"));
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        accepted.reset();
        displayState.reset(new HistoryCurrentDisplayState(store)); // 新开会话不沿用已closed的值缓存。
        accepted.reset(new QSignalSpy(displayState.get(), &HistoryCurrentDisplayState::queryAccepted));
        QVERIFY(initialize()); QVERIFY(detail(1, &report));
        QCOMPARE(report.patient.name, patient.name); QCOMPARE(report.patient.remark, patient.remark); QCOMPARE(report.patient.age, 41);
        QCOMPARE(rawValues(report.results.first()), raw);
        QCOMPARE(audit("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN('report_values','meta')").toInt(), 0);
    }
    void savesOriginalHeaderAndPreservesOldUnknownColumns()
    {
        QVERIFY(initialize());
        const qint64 request = ++sequence;
        QMetaObject::invokeMethod(store, [=]() { store->loadDisplay(request); }, Qt::QueuedConnection);
        QVERIFY(until([&]() { return loaded->count() == 1 || !errors->isEmpty(); })); QCOMPARE(loaded->count(), 1);
        auto columns = qvariant_cast<HistoryDisplayColumns>(loaded->first().at(1));
        bool changed = false;
        for (auto &column : columns) if (column.key == "patient:sex") { column.visible = false; changed = true; }
        QVERIFY(changed);
        const qint64 saveRequest = ++sequence;
        QMetaObject::invokeMethod(store, [=]() { store->saveDisplay(saveRequest, columns); }, Qt::QueuedConnection);
        QVERIFY(until([&]() { return saved->count() == 1 || !errors->isEmpty(); })); QCOMPARE(saved->count(), 1);
        QCOMPARE(audit("SELECT display FROM report_column_header WHERE project_name='Sex' AND table_type=1 AND result_type=0").toInt(), 0);
        QCOMPARE(audit("SELECT display_name FROM report_column_header WHERE id=1").toString(), QStringLiteral("原姓名列"));
        QCOMPARE(audit("SELECT vendor_unknown FROM report_column_header WHERE id=1").toString(), QStringLiteral("KEEP_HEADER"));
        QCOMPARE(audit("SELECT vendor_unknown FROM report_column_header WHERE id=3").toString(), QStringLiteral("KEEP_OLD_HEADER"));
        QCOMPARE(audit("SELECT column_index FROM report_column_header WHERE id=3").toInt(), 99);
        QCOMPARE(audit("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN('report_columns','meta')").toInt(), 0);
    }
    void explicitCsvKeepsRepeatTasksAndRawUnknownValues()
    {
        QVERIFY(initialize());
        const QString path = QDir(root).filePath("result/exports/anonymous.csv");
        const HistoryFilter selected = filter();
        const qint64 request = ++sequence;
        QMetaObject::invokeMethod(store, [=]() { store->exportCsv(request, selected, {}, path); }, Qt::QueuedConnection);
        QVERIFY(until([&]() { return exported->count() == 1 || !errors->isEmpty(); })); QCOMPARE(exported->count(), 1);
        QCOMPARE(exported->first().at(2).toLongLong(), qint64(2)); QCOMPARE(exported->first().at(3).toLongLong(), qint64(3));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const QString csv = QTextCodec::codecForName("GBK")->toUnicode(file.readAll());
        QVERIFY(csv.contains("12.35")); QVERIFY(csv.contains("13.50"));
        QVERIFY(csv.contains("UNKNOWN_RAW")); QVERIFY(csv.contains("raw_formula"));
        QVERIFY(csv.contains(QStringLiteral("F(原串)"))); QVERIFY(csv.contains("SECOND_FORMULA"));
        QCOMPARE(audit("SELECT original_value FROM report_results WHERE id=1").toString(), QStringLiteral("0012.34560"));
        QCOMPARE(audit("SELECT COUNT(*) FROM report_results WHERE project_name='PT'").toInt(), 2);
    }
    void missingOriginalResultFailsWithoutCreatingEmptyDatabase()
    {
        QVERIFY(QFile::remove(QDir(root).filePath("result/results.db")));
        const int previous = errors->count();
        QMetaObject::invokeMethod(store, [this]() { store->initializeOriginal(root); }, Qt::QueuedConnection);
        QVERIFY(until([&]() { return errors->count() > previous; })); QCOMPARE(ready->count(), 0);
        QVERIFY(!QFileInfo::exists(QDir(root).filePath("result/results.db")));
        QVERIFY(!QFileInfo::exists(QDir(root).filePath("result/offline-history.tag")));
    }
    void originalReportUsesCommittedParametersAndExternalXml()
    {
        QVERIFY(initialize());
        HistoryReport report; QVERIFY(detail(1, &report));
        QVERIFY(report.results.first().originalProfile);
        QVERIFY(!report.results.first().originalTemplateXml.isEmpty());
        HistoryPrintOptions options; QString error;
        QVERIFY2(HistoryReportDocument::originalOptions(report, &options, &error), qPrintable(error));
        HistoryPrintDocument document;
        QVERIFY2(HistoryReportDocument::build(report, options, &document, &error), qPrintable(error));
        QCOMPARE(document.rows.size(), 8);
        QCOMPARE(document.headers.value("sample_sn"), QStringLiteral("ANON-17"));
        QCOMPARE(document.headers.value("hospital_name"), QString()); // 原回调没有医院标题绑定。
        QCOMPARE(document.rows[0].value("project_name"), QStringLiteral("匿名PT"));
        QCOMPARE(document.rows[0].value("project_result"), QStringLiteral("12.35 "));
        QCOMPARE(document.rows[1].value("project_result"), QStringLiteral("1.235 "));
        QCOMPARE(document.rows[2].value("project_result"), QStringLiteral("1.25 "));
        QCOMPARE(document.rows[1].value("reference_range"), QStringLiteral("0.000-999.000"));
        HistoryPrintDocument restored;
        QVERIFY2(HistoryReportDocument::fromJson(HistoryReportDocument::toJson(document), &restored, &error), qPrintable(error));
        QCOMPARE(restored.originalTemplateXml, document.originalTemplateXml);
        QtRPT engine;
        QString renderError;
        connect(&engine, &QtRPT::renderError, this, [&](QString value) { renderError = value; });
        QVERIFY2(prepareHistoryReport(&engine, restored, &error), qPrintable(error));
        const QString output = qEnvironmentVariable("SN100_HISTORY_OUTPUT_DIR");
        QVERIFY(QDir::isAbsolutePath(output)); QVERIFY(QDir().mkpath(output));
        const QString pdf = output + "/original_external_template.pdf";
        QVERIFY(!QFileInfo::exists(pdf));
        engine.printPDF(pdf, false);
        QVERIFY2(renderError.isEmpty(), qPrintable(renderError));
        QFile file(pdf); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.read(5), QByteArray("%PDF-"));
        QCOMPARE(audit("SELECT original_value FROM report_results WHERE id=1").toString(), QStringLiteral("0012.34560"));
    }
    void originalPrintIgnoresScreenDisplayAndRetainsWindowsUnitConversion()
    {
        QVERIFY(database("project/PT.db", {"UPDATE project_results SET display='0' WHERE result_id=3"}));
        QVERIFY(database("db/systemInfo/option_Unit.db", {"UPDATE option_unit SET ratio='0.5' WHERE unit='mg/L'"}));
        QVERIFY(initialize());
        HistoryReport report; QVERIFY(detail(1, &report));
        HistoryPrintOptions options; HistoryPrintDocument document; QString error;
        QVERIFY(HistoryReportDocument::originalOptions(report, &options, &error));
        QVERIFY2(HistoryReportDocument::build(report, options, &document, &error), qPrintable(error));
        QCOMPARE(document.rows.size(), 8); // 原打印只检查used；display仅决定历史窗口。
        QCOMPARE(document.rows[1].value("project_result"), QStringLiteral("0.000 ")); // 原QString("0.5").toInt()为0。
        QCOMPARE(document.rows[2].value("project_result"), QStringLiteral("1.25 "));
        QVERIFY(database("project/PT.db", {"UPDATE project_results SET main_result='0' WHERE result_id=1"}));
        QVERIFY(detail(1, &report));
        QVERIFY(HistoryReportDocument::originalOptions(report, &options, &error));
        QVERIFY(HistoryReportDocument::build(report, options, &document, &error));
        QCOMPARE(document.rows.size(), 6);
    }
    void actualHistoryPageReadsOriginalRows()
    {
        const QString today = QDate::currentDate().toString(QStringLiteral("yyyyMMdd"));
        QVERIFY(database("result/results.db", {"UPDATE report_data SET data_time='" + today + "120000'"}));
        HistoryPage page(store);
        page.setDataDirectory(root + "/result");
        page.show();
        QVERIFY(initialize());
        QVERIFY(until([&]() { return page.isReady() && !page.isBusy(); }));
        auto *table = page.findChild<QTableWidget *>(QStringLiteral("tableResultReportOne"));
        QVERIFY(table);
        QVERIFY(table->rowCount() > 0);
        const QString output = qEnvironmentVariable("SN100_HISTORY_OUTPUT_DIR");
        QVERIFY(QDir::isAbsolutePath(output)); QVERIFY(QDir().mkpath(output));
        QVERIFY(page.grab().save(output + "/original_history_page.png"));
        QVERIFY(!QFileInfo::exists(root + "/result/offline-history.tag"));
    }
    void missingExternalTemplateRefusesAnonymousFallback()
    {
        QVERIFY(QFile::remove(root + "/resource/report_template/report.xml"));
        QVERIFY(initialize());
        HistoryReport report; QVERIFY(detail(1, &report));
        HistoryPrintOptions options; HistoryPrintDocument document; QString error;
        QVERIFY(HistoryReportDocument::originalOptions(report, &options, &error));
        QVERIFY(!HistoryReportDocument::build(report, options, &document, &error));
        QVERIFY(error.contains(QStringLiteral("缺失")));
    }
    void originalReportControllerExportsThroughMatchingHelper()
    {
        QVERIFY(initialize());
        HistoryReport report; QVERIFY(detail(1, &report));
        HistoryReportController controller;
        controller.setDataDirectory(root + "/result");
        const QString helper = qEnvironmentVariable("SN100_FORMAL_REPORT_RENDERER");
        QVERIFY(QFileInfo(helper).isExecutable());
        controller.setRendererPath(helper);
        QSignalSpy pdf(&controller, &HistoryReportController::pdfExported);
        QSignalSpy failed(&controller, &HistoryReportController::failed);
        const QString path = root + "/result/exports/original_controller.pdf";
        controller.exportPdf(report, path);
        QTRY_VERIFY_WITH_TIMEOUT(!pdf.isEmpty() || !failed.isEmpty(), 35000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.first().at(1).toString()));
        QCOMPARE(pdf.count(), 1); QCOMPARE(pdf.first().at(1).toInt(), 8);
        QVERIFY(!QFileInfo::exists(root + "/result/.sn100-history-offline"));
        const QString output = qEnvironmentVariable("SN100_HISTORY_OUTPUT_DIR");
        QVERIFY(QFile::copy(path, output + "/original_controller.pdf"));
        const qint64 size = QFileInfo(path).size();
        controller.exportPdf(report, path);
        QCOMPARE(failed.count(), 1); QCOMPARE(QFileInfo(path).size(), size);
        controller.shutdown();
    }
};

QTEST_MAIN(OriginalHistoryRkTest)
#include "original_history_rk_test.moc"
