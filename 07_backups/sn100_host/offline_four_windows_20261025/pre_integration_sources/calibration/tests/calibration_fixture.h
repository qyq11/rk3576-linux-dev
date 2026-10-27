#ifndef SN100_CALIBRATION_PAGE_FIXTURE_H
#define SN100_CALIBRATION_PAGE_FIXTURE_H

#include "calibration/calibration_page.h"
#include <QtTest>
#include <QDir>
#include <QElapsedTimer>
#include <QScrollArea>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <functional>

namespace CalibrationGuiFixture
{
inline bool waitUntil(const std::function<bool()> &condition, int timeout = 4000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) QTest::qWait(10);
    return condition();
}

inline QString directoryTemplate()
{
    QString root = qEnvironmentVariable("CALIBRATION_TEST_ROOT");
    if (root.isEmpty()) root = QDir::current().filePath(QStringLiteral("calibration_page_runtime"));
    QDir().mkpath(root);
    return QDir(root).filePath(QStringLiteral("page-XXXXXX"));
}

class Database
{
public:
    explicit Database(const QString &path)
    {
        name = QUuid::createUuid().toString();
        value = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        value.setDatabaseName(path);
        value.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=250"));
        value.open();
    }
    ~Database()
    {
        value.close(); value = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
    }
    bool exec(const QString &sql)
    {
        QSqlQuery query(value);
        if (query.exec(sql)) return true;
        qWarning().noquote() << sql << query.lastError().text();
        return false;
    }
    QVariant scalar(const QString &sql)
    {
        QSqlQuery query(value);
        if (!query.exec(sql) || !query.next()) return {};
        return query.value(0);
    }
    QString name;
    QSqlDatabase value;
};

inline AccountSession engineer()
{
    AccountSession session;
    session.name = QStringLiteral("anonymous-gui-operator"); session.role = 4;
    session.authenticated = true; session.actionReady = true;
    session.source = AccountSessionSource::Simulated;
    return session;
}

inline ProjectUnits units()
{
    return {{QStringLiteral("mg/dL"), 1}, {QStringLiteral("Sec"), 1}};
}

inline ProjectRecords projects(const QString &name = QStringLiteral("LOCAL_CAL"))
{
    ProjectRecord project;
    project.id = 1; project.name = name; project.fullName = QStringLiteral("匿名校准项目");
    ProjectResult original; original.type = QStringLiteral("Original"); original.unit = QStringLiteral("Sec");
    ProjectResult calibration; calibration.type = QStringLiteral("Calibration"); calibration.unit = QStringLiteral("mg/dL");
    project.results = {original, calibration};
    return {project};
}

class Harness
{
public:
    Harness() : directory(directoryTemplate()), catalog(directory.path() + QStringLiteral("/catalog.db"))
    {
        directory.setAutoRemove(qEnvironmentVariableIsEmpty("CALIBRATION_TEST_ROOT"));
        store = new CalibrationStore;
        store->moveToThread(&thread);
        QObject::connect(&thread, &QThread::finished, store, &QObject::deleteLater);
        page = new CalibrationPage(store);
        host.setWidget(page); host.setWidgetResizable(false); host.resize(1920, 1080);
        page->setSession(engineer());
        thread.start();
    }
    ~Harness() { shutdown(); }
    bool initialize(bool withProject = true)
    {
        const QStringList schema = {
            "CREATE TABLE catalog_metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
            "INSERT INTO catalog_metadata VALUES('schema','sn100-catalog-offline-v1')",
            "CREATE TABLE option_unit(unit TEXT PRIMARY KEY,ratio REAL NOT NULL)",
            "INSERT INTO option_unit VALUES('mg/dL',1)",
            "INSERT INTO option_unit VALUES('Sec',1)",
            "CREATE TABLE project_base(id INTEGER PRIMARY KEY,project_id TEXT,project_en_name TEXT,project_cn_name TEXT,project_lis_code TEXT,is_used INTEGER,fixed INTEGER,change_time TEXT)",
            "CREATE TABLE project_result(project_id INTEGER,result_type TEXT,result_name TEXT,decimal_point INTEGER,test_low_range REAL,test_up_range REAL,normal_low_range REAL,normal_up_range REAL,lis_code TEXT,unit_name TEXT,main_result INTEGER)",
            "CREATE TABLE reagent_batch_group(id INTEGER PRIMARY KEY,project_id INTEGER,batch_code TEXT,batch_group TEXT)"
        };
        for (const QString &sql : schema) if (!catalog.exec(sql)) return false;
        if (withProject)
        {
            const QStringList rows = {
                "INSERT INTO project_base VALUES(1,'offline-1','LOCAL_CAL','匿名校准项目','',1,0,'2026-10-25T00:00:00Z')",
                "INSERT INTO project_result VALUES(1,'Calibration','浓度',2,0,99999,0,99999,'','mg/dL',0)",
                "INSERT INTO project_result VALUES(1,'Original','原始值',2,0,99999,0,99999,'','Sec',1)",
                "INSERT INTO reagent_batch_group VALUES(7,1,'ANON-BATCH','ANON-R1|ANON-R2')"
            };
            for (const QString &sql : rows) if (!catalog.exec(sql)) return false;
        }
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory.path()), Q_ARG(QString, directory.path() + QStringLiteral("/catalog.db")));
        host.show();
        return waitUntil([this]() { return page->isReady() && !page->isBusy(); });
    }
    bool reopenPage()
    {
        delete host.takeWidget(); page = nullptr;
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        page = new CalibrationPage(store); host.setWidget(page); page->setSession(engineer());
        QMetaObject::invokeMethod(store, "initialize", Qt::QueuedConnection,
            Q_ARG(QString, directory.path()), Q_ARG(QString, directory.path() + QStringLiteral("/catalog.db")));
        host.show();
        return waitUntil([this]() { return page->isReady() && !page->isBusy(); });
    }
    void shutdown()
    {
        if (stopped) return;
        if (page)
        {
            delete host.takeWidget(); page = nullptr;
        }
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        thread.quit(); thread.wait(); stopped = true;
        qInfo().noquote() << "GUI_FIXTURE normal database/worker shutdown; runtime=" << directory.path();
    }
    QString localPath() const { return directory.path() + QStringLiteral("/calibration_local.db"); }
    QTemporaryDir directory;
    Database catalog;
    QThread thread;
    CalibrationStore *store = nullptr;
    CalibrationPage *page = nullptr;
    QScrollArea host;
    bool stopped = false;
};
}
#endif
