#include "instrument/instrument_page.h"
#include "instrument/maintenance_store.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>
#include <memory>

class InstrumentConnectionRkTest : public QObject
{
    Q_OBJECT
    QThread worker;
    MaintenanceStore *store = nullptr;
    std::unique_ptr<QTemporaryDir> directory;
    std::unique_ptr<InstrumentPage> page;
    std::unique_ptr<QSignalSpy> ready, errors;
    QString configuration;

    AccountSession administrator() const
    {
        AccountSession session;
        session.authenticated = true;
        session.name = QStringLiteral("rkConfigurationAdmin");
        session.role = 5;
        session.actionReady = true;
        return session;
    }

    QByteArray configurationBytes() const
    {
        QFile file(configuration);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }

private slots:
    void initTestCase()
    {
#if !defined(Q_OS_LINUX) || !defined(__aarch64__)
        QFAIL("本驱动只允许交叉编译后在RK ARM64运行。");
#endif
        MaintenanceStore::registerTypes();
        QVERIFY(QDir::isAbsolutePath(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT")));
    }

    void init()
    {
        directory.reset(new QTemporaryDir(QDir(qEnvironmentVariable("SN100_BUTTON_TEST_ROOT"))
                                              .filePath(QStringLiteral("connection_settings_XXXXXX"))));
        QVERIFY(directory->isValid());
        configuration = directory->filePath(QStringLiteral("config.ini"));
        QFile file(configuration);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QVERIFY(file.write("[tcpmain]\nip=192.168.100.123\nport=5000\n"
                           "[tcplog]\nip=192.168.100.123\nport=5001\n"
                           "[unchanged]\nlabel=KEEP\n") > 0);
        file.close();
        store = new MaintenanceStore;
        store->moveToThread(&worker);
        connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        ready.reset(new QSignalSpy(store, &MaintenanceStore::connectionSettingsReady));
        errors.reset(new QSignalSpy(store, &MaintenanceStore::connectionSettingsError));
        worker.start();
        page.reset(new InstrumentPage(store));
        page->setSession(administrator());
        page->show();
        auto *connectionTab = page->findChild<QPushButton *>(QStringLiteral("btnPort"));
        QVERIFY(connectionTab);
        QTest::mouseClick(connectionTab, Qt::LeftButton);
        QVERIFY(page->setConnectionConfigurationFile(configuration));
        QTRY_COMPARE(ready->count(), 1);
        QTRY_VERIFY(!page->isBusy());
    }

    void cleanup()
    {
        page.reset();
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        QVERIFY(worker.wait(5000));
        store = nullptr;
        ready.reset();
        errors.reset();
        directory.reset();
    }

    void readsOriginalKeysAndSavesOnce()
    {
        auto *address = page->findChild<QLineEdit *>(QStringLiteral("lblTcpIp"));
        auto *port = page->findChild<QLineEdit *>(QStringLiteral("lblTcpPort"));
        auto *save = page->findChild<QPushButton *>(QStringLiteral("btnSaveTcpSet"));
        QVERIFY(address && port && save);
        QCOMPARE(address->text(), QStringLiteral("192.168.100.123"));
        QCOMPARE(port->text(), QStringLiteral("5000"));
        QVERIFY(!save->isEnabled());
        port->setText(QStringLiteral("6000"));
        QVERIFY(save->isEnabled());
        QTest::mouseClick(save, Qt::LeftButton);
        QTest::mouseClick(save, Qt::LeftButton);
        QTRY_COMPARE(ready->count(), 2);
        QTRY_VERIFY(!page->isBusy());
        QCOMPARE(errors->count(), 0);
        QVERIFY(ready->last().at(4).toBool());
        QVERIFY(!save->isEnabled());
        QSettings saved(configuration, QSettings::IniFormat);
        QCOMPARE(saved.value(QStringLiteral("tcpmain/port")).toString(), QStringLiteral("6000"));
        QCOMPARE(saved.value(QStringLiteral("tcplog/ip")).toString(), QStringLiteral("192.168.100.123"));
        QCOMPARE(saved.value(QStringLiteral("tcplog/port")).toString(), QStringLiteral("5001"));
        QCOMPARE(saved.value(QStringLiteral("unchanged/label")).toString(), QStringLiteral("KEEP"));
        QVERIFY(page->setConnectionConfigurationFile(configuration));
        QTRY_COMPARE(ready->count(), 3);
        QCOMPARE(port->text(), QStringLiteral("6000"));
    }

    void invalidDraftDoesNotWrite()
    {
        const QByteArray original = configurationBytes();
        auto *port = page->findChild<QLineEdit *>(QStringLiteral("lblTcpPort"));
        port->setText(QStringLiteral("bad-port"));
        QTest::mouseClick(page->findChild<QPushButton *>(QStringLiteral("btnSaveTcpSet")), Qt::LeftButton);
        QTRY_COMPARE(errors->count(), 1);
        QTRY_VERIFY(!page->isBusy());
        QCOMPARE(port->text(), QStringLiteral("bad-port"));
        QCOMPARE(configurationBytes(), original);
    }

    void hashCommentsAndUnrelatedBytesSurviveSave()
    {
        const QByteArray original = "# 原版配置说明\r\n[tcpmain]\r\nip=192.168.100.123\r\nport=5000\r\n"
            "[tcplog]\r\nip=192.168.100.123\r\nport=5001\r\n# keep comment\r\n"
            "[unchanged]\r\nlabel=KEEP  spaces\r\n";
        QFile file(configuration);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        QVERIFY(page->setConnectionConfigurationFile(configuration));
        QTRY_COMPARE(ready->count(), 2);
        QTRY_VERIFY(!page->isBusy());
        QCOMPARE(errors->count(), 0);
        auto *port = page->findChild<QLineEdit *>(QStringLiteral("lblTcpPort"));
        port->setText(QStringLiteral("6100"));
        QTest::mouseClick(page->findChild<QPushButton *>(QStringLiteral("btnSaveTcpSet")), Qt::LeftButton);
        QTRY_COMPARE(ready->count(), 3);
        QTRY_VERIFY(!page->isBusy());
        QByteArray expected = original;
        expected.replace("port=5000", "port=6100");
        QCOMPARE(configurationBytes(), expected);
    }

    void externalChangePreservesDraft()
    {
        {
            QSettings external(configuration, QSettings::IniFormat);
            external.setValue(QStringLiteral("unchanged/label"), QStringLiteral("EXTERNAL"));
            external.sync();
            QCOMPARE(external.status(), QSettings::NoError);
        }
        const QByteArray changed = configurationBytes();
        auto *port = page->findChild<QLineEdit *>(QStringLiteral("lblTcpPort"));
        port->setText(QStringLiteral("6001"));
        QTest::mouseClick(page->findChild<QPushButton *>(QStringLiteral("btnSaveTcpSet")), Qt::LeftButton);
        QTRY_COMPARE(errors->count(), 1);
        QTRY_VERIFY(!page->isBusy());
        QCOMPARE(port->text(), QStringLiteral("6001"));
        QCOMPARE(configurationBytes(), changed);
    }

    void nonAdministratorCannotSave()
    {
        const QByteArray original = configurationBytes();
        AccountSession engineer = administrator();
        engineer.role = 4;
        page->setSession(engineer);
        QVERIFY(page->findChild<QLineEdit *>(QStringLiteral("lblTcpIp"))->isReadOnly());
        QVERIFY(!page->findChild<QPushButton *>(QStringLiteral("btnSaveTcpSet"))->isEnabled());
        QMetaObject::invokeMethod(store, "saveConnectionSettings", Qt::QueuedConnection,
            Q_ARG(quint64, 1000000), Q_ARG(QString, configuration),
            Q_ARG(QString, ready->first().at(3).toString()),
            Q_ARG(QString, QStringLiteral("192.168.100.123")), Q_ARG(QString, QStringLiteral("6002")));
        QTRY_COMPARE(errors->count(), 1);
        QCOMPARE(configurationBytes(), original);
    }
};

QTEST_MAIN(InstrumentConnectionRkTest)
#include "instrument_connection_rk_test.moc"
