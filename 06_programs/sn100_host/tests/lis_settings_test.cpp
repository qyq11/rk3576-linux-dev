#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <atomic>
#include "data/lis_settings_store.h"
#include "data/serial_port_catalog.h"
#include "ui/frmlissetting.h"
#include "projects/cuscheckbox.h"
#include "samples/material/qtmaterialradiobutton.h"

namespace
{
template<class T> T *control(QObject *page, const char *name)
{
    T *widget = page->findChild<T *>(QString::fromLatin1(name));
    if (!widget) qFatal("Missing original LIS control: %s", name);
    return widget;
}
QByteArray bytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void writeBytes(const QString &path, const QByteArray &value)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(value), qint64(value.size()));
}
LisSettingsStore::PortNames fixedPorts()
{
    return []() { return QStringList{QStringLiteral("ANON_PORT_A"), QStringLiteral("ANON_PORT_B")}; };
}
LisSettings example()
{
    LisSettings value;
    value.type = QStringLiteral("tcp");
    value.ip = QStringLiteral("127.0.0.1");
    value.port = QStringLiteral("12345");
    value.commName = QStringLiteral("ANON_PORT_B");
    value.baudrate = QStringLiteral("19200");
    value.sampleMode = 1;
    value.autoGet = value.autoSend = value.link = true;
    return value;
}
struct Fixture
{
    QThread worker;
    LisSettingsStore *store;
    frmLisSetting page;
    QSignalSpy operations;
    explicit Fixture(const QString &directory, LisSettingsStore::PortNames names = fixedPorts()) :
        store(new LisSettingsStore(names)), page(store), operations(&page, &frmLisSetting::operationFinished)
    {
        worker.setObjectName(QStringLiteral("LisIndependentTest"));
        store->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        QObject::connect(&worker, &QThread::started, store, [this, directory]() { store->initialize(directory); });
        QObject::connect(&page, &frmLisSetting::statusChanged, &page,
            [](const QString &message, bool error) { if (error) qInfo().noquote() << "LIS_EXPECTED_ERROR" << message; });
        page.resize(1451, 853);
        page.show();
        worker.start();
    }
    ~Fixture()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
    }
    void save(LisSettings settings)
    {
        QMetaObject::invokeMethod(store, "save", Qt::QueuedConnection, Q_ARG(LisSettings, settings));
    }
    bool lastSucceeded() const { return !operations.isEmpty() && operations.last().at(1).toBool(); }
};
void answerDraft(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(10, qApp, [answer]()
    {
        if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) dialog->button(answer)->click();
    });
}
}

class LisSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void freshUnconfiguredAndEmptyPorts()
    {
        QTemporaryDir directory;
        Fixture fixture(directory.path(), []() { return QStringList(); });
        QTRY_VERIFY(fixture.page.isReady());
        QCOMPARE(fixture.page.committedSettings().sampleMode, 0);
        QVERIFY(fixture.page.committedSettings().type.isEmpty());
        QVERIFY(!fixture.page.isDirty());
        QCOMPARE(control<QComboBox>(&fixture.page, "cbxCom")->currentText(), QString());
        QCOMPARE(control<QComboBox>(&fixture.page, "cbxBaudrate")->currentText(), QString());
        QVERIFY(!control<QPushButton>(&fixture.page, "btnSave")->isEnabled());
    }
    void nineFieldsPersistAndReopen()
    {
        QTemporaryDir directory;
        {
            Fixture fixture(directory.path());
            QTRY_VERIFY(fixture.page.isReady());
            fixture.save(example());
            QTRY_COMPARE(fixture.operations.count(), 2);
            QVERIFY(fixture.lastSucceeded());
            QVERIFY(fixture.page.committedSettings() == example());
        }
        Fixture reopened(directory.path());
        QTRY_VERIFY(reopened.page.isReady());
        QVERIFY(reopened.page.committedSettings() == example());
        QSettings settings(directory.filePath("lis.ini"), QSettings::IniFormat);
        QCOMPARE(settings.value("lis/type").toString(), QStringLiteral("tcp"));
        QCOMPARE(settings.value("lis/link").toString(), QStringLiteral("true"));
        QCOMPARE(settings.value("lis/samMode").toString(), QStringLiteral("1"));
    }
    void hiddenAndUnrelatedKeysSurvive()
    {
        QTemporaryDir directory;
        QByteArray seed("[sn100]\noffline_lis_schema=1\n[lis]\n");
        const QStringList hidden = {"moni", "linkintervalms", "databits", "parity", "stopbits", "flowcontrol", "tcptype", "isUploadForHole", "sequencenumber", "serverID", "lisCode", "sampleCodeCanRepeatTest"};
        for (const QString &key : hidden) seed += key.toUtf8() + "=anonymous-original\n";
        seed += "[other]\nvalue=preserve-me\n";
        writeBytes(directory.filePath("lis.ini"), seed);
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        fixture.save(example());
        QTRY_COMPARE(fixture.operations.count(), 2);
        QVERIFY(fixture.lastSucceeded());
        QSettings settings(directory.filePath("lis.ini"), QSettings::IniFormat);
        for (const QString &key : hidden) QCOMPARE(settings.value("lis/" + key).toString(), QStringLiteral("anonymous-original"));
        QCOMPARE(settings.value("other/value").toString(), QStringLiteral("preserve-me"));
    }
    void invalidFieldsNeverCommit()
    {
        QTemporaryDir directory;
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        const QByteArray original = bytes(directory.filePath("lis.ini"));
        QList<LisSettings> invalid;
        LisSettings value = example(); value.type = "udp"; invalid.append(value);
        value = example(); value.ip = " leading"; invalid.append(value);
        value = example(); value.ip = "a b"; invalid.append(value);
        value = example(); value.port = "100000"; invalid.append(value);
        value = example(); value.port = "-1"; invalid.append(value);
        value = example(); value.sampleMode = 2; invalid.append(value);
        value = example(); value.commName = "NOT_ENUMERATED"; invalid.append(value);
        value = example(); value.baudrate = "42"; invalid.append(value);
        value = example(); value.ip = QStringLiteral("a") + QChar(1); invalid.append(value);
        value = example(); value.commName = QStringLiteral("port") + QChar(127); invalid.append(value);
        value = example(); value.ip = QStringLiteral("a") + QString::fromUcs4(U"😀😀😀😀😀😀😀😀"); invalid.append(value);
        int finished = fixture.operations.count();
        for (const LisSettings &candidate : invalid)
        {
            fixture.save(candidate);
            ++finished;
            QTRY_COMPARE(fixture.operations.count(), finished);
            QVERIFY(!fixture.lastSucceeded());
            QCOMPARE(bytes(directory.filePath("lis.ini")), original);
        }
    }
    void unavailableSavedPortAndBaudDoNotBecomeFirst()
    {
        QTemporaryDir directory;
        writeBytes(directory.filePath("lis.ini"), "[sn100]\noffline_lis_schema=1\n[lis]\ncommname=MISSING_ORIGINAL\nbaudrate=230400\n");
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        QCOMPARE(control<QComboBox>(&fixture.page, "cbxCom")->currentText(), QStringLiteral("MISSING_ORIGINAL"));
        QCOMPARE(control<QComboBox>(&fixture.page, "cbxBaudrate")->currentText(), QStringLiteral("230400"));
        LisSettings changed = fixture.page.committedSettings(); changed.autoGet = true;
        fixture.save(changed);
        QTRY_COMPARE(fixture.operations.count(), 2);
        QVERIFY(fixture.lastSucceeded());
        QCOMPARE(fixture.page.committedSettings().commName, QStringLiteral("MISSING_ORIGINAL"));
    }
    void twoModeDraftsAndSingleSave()
    {
        QTemporaryDir directory;
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        control<QtMaterialRadioButton>(&fixture.page, "radioBoxTcp")->setChecked(true);
        control<QLineEdit>(&fixture.page, "lblTcpIp")->setText("127.0.0.1");
        control<QLineEdit>(&fixture.page, "lblTcpPort")->setText("54321");
        control<QtMaterialRadioButton>(&fixture.page, "radioBoxCom")->setChecked(true);
        control<QComboBox>(&fixture.page, "cbxCom")->setCurrentText("ANON_PORT_A");
        control<QComboBox>(&fixture.page, "cbxBaudrate")->setCurrentText("9600");
        control<QtMaterialRadioButton>(&fixture.page, "radioBoxTcp")->setChecked(true);
        QCOMPARE(control<QLineEdit>(&fixture.page, "lblTcpPort")->text(), QStringLiteral("54321"));
        QTest::mouseClick(control<CusCheckBox>(&fixture.page, "ckxAutoGet"), Qt::LeftButton);
        QTest::mouseClick(control<QPushButton>(&fixture.page, "btnSave"), Qt::LeftButton);
        QVERIFY(fixture.page.isBusy());
        QTRY_COMPARE(fixture.operations.count(), 2);
        QVERIFY(fixture.lastSucceeded());
        QCOMPARE(fixture.page.committedSettings().commName, QStringLiteral("ANON_PORT_A"));
        QCOMPARE(fixture.page.committedSettings().port, QStringLiteral("54321"));
        QVERIFY(fixture.page.committedSettings().autoGet);
        QVERIFY(!fixture.page.isDirty());
    }
    void draftKeepDiscardAndClose()
    {
        QTemporaryDir directory;
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        control<QLineEdit>(&fixture.page, "lblTcpIp")->setText("localhost");
        QVERIFY(fixture.page.isDirty());
        answerDraft(QMessageBox::No);
        QVERIFY(!fixture.page.requestLeave("leave"));
        QVERIFY(fixture.page.isDirty());
        answerDraft(QMessageBox::Yes);
        QVERIFY(fixture.page.requestLeave("leave"));
        QVERIFY(!fixture.page.isDirty());
        QVERIFY(fixture.page.close());
    }
    void failedSavePreservesDraftAndRetry()
    {
        QTemporaryDir directory;
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        const QByteArray original = bytes(directory.filePath("lis.ini"));
        QLockFile lock(directory.filePath(".lis-locks/settings.lock"));
        QVERIFY(lock.tryLock(0));
        control<QLineEdit>(&fixture.page, "lblTcpIp")->setText("localhost");
        QTest::mouseClick(control<QPushButton>(&fixture.page, "btnSave"), Qt::LeftButton);
        QVERIFY(!fixture.page.requestLeave("leave"));
        QTRY_COMPARE(fixture.operations.count(), 2);
        QVERIFY(!fixture.lastSucceeded());
        QVERIFY(!fixture.page.isBusy());
        QVERIFY(fixture.page.isDirty());
        QCOMPARE(bytes(directory.filePath("lis.ini")), original);
        lock.unlock();
        QTest::mouseClick(control<QPushButton>(&fixture.page, "btnSave"), Qt::LeftButton);
        QTRY_COMPARE(fixture.operations.count(), 3);
        QVERIFY(fixture.lastSucceeded());
        QCOMPARE(fixture.page.committedSettings().ip, QStringLiteral("localhost"));
    }
    void externalByteChangeRequiresReload()
    {
        QTemporaryDir directory;
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        const QByteArray external = bytes(directory.filePath("lis.ini")) + "\n[other]\nchanged=yes\n";
        writeBytes(directory.filePath("lis.ini"), external);
        fixture.save(example());
        QTRY_COMPARE(fixture.operations.count(), 2);
        QVERIFY(!fixture.lastSucceeded());
        QCOMPARE(bytes(directory.filePath("lis.ini")), external);
        QMetaObject::invokeMethod(fixture.store, "load", Qt::QueuedConnection);
        QTRY_COMPARE(fixture.operations.count(), 3);
        QVERIFY(fixture.lastSucceeded());
        fixture.save(example());
        QTRY_COMPARE(fixture.operations.count(), 4);
        QVERIFY(fixture.lastSucceeded());
    }
    void actualFilesystemWriteFailureAndRecovery()
    {
#ifdef Q_OS_LINUX
        QTemporaryDir directory;
        std::atomic<bool> denyNextWrite{false};
        Fixture fixture(directory.path(), [&]()
        {
            if (denyNextWrite.exchange(false))
                QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::ExeOwner);
            return fixedPorts()();
        });
        QTRY_VERIFY(fixture.page.isReady());
        const QByteArray original = bytes(directory.filePath("lis.ini"));
        control<QLineEdit>(&fixture.page, "lblTcpIp")->setText("localhost");
        denyNextWrite = true;
        QTest::mouseClick(control<QPushButton>(&fixture.page, "btnSave"), Qt::LeftButton);
        QTRY_COMPARE(fixture.operations.count(), 2);
        // 先恢复目录权限，确保断言失败也不影响临时目录回收。
        QVERIFY(QFile::setPermissions(directory.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(!fixture.lastSucceeded());
        QVERIFY(fixture.page.statusText().contains(QStringLiteral("临时文件")));
        QVERIFY(fixture.page.isDirty());
        QCOMPARE(bytes(directory.filePath("lis.ini")), original);
        QTest::mouseClick(control<QPushButton>(&fixture.page, "btnSave"), Qt::LeftButton);
        QTRY_COMPARE(fixture.operations.count(), 3);
        QVERIFY(fixture.lastSucceeded());
#else
        QSKIP("POSIX directory permissions fixture only");
#endif
    }
    void invalidUtf8NeverCanonicalized()
    {
        QTemporaryDir directory;
        const QByteArray original = QByteArray("[sn100]\noffline_lis_schema=1\n[other]\nvalue=") + QByteArray::fromHex("f09f");
        writeBytes(directory.filePath("lis.ini"), original);
        Fixture fixture(directory.path());
        QTRY_COMPARE(fixture.operations.count(), 1);
        QVERIFY(!fixture.lastSucceeded());
        QCOMPARE(bytes(directory.filePath("lis.ini")), original);
    }
    void invalidFormatAndUnknownSampleModePreserved()
    {
        for (const QByteArray &seed : {QByteArray("[lis]\nsamMode=0\n"), QByteArray("[sn100]\noffline_lis_schema=1\n[lis]\nsamMode=9\n")})
        {
            QTemporaryDir directory;
            writeBytes(directory.filePath("lis.ini"), seed);
            Fixture fixture(directory.path());
            QTRY_COMPARE(fixture.operations.count(), 1);
            QVERIFY(!fixture.lastSucceeded());
            QVERIFY(!fixture.page.isReady());
            QCOMPARE(bytes(directory.filePath("lis.ini")), seed);
        }
    }
    void oversizedAndInvalidEnumerationRejected()
    {
        QTemporaryDir directory;
        const QByteArray large(65537, 'x');
        writeBytes(directory.filePath("lis.ini"), large);
        Fixture fixture(directory.path());
        QTRY_COMPARE(fixture.operations.count(), 1);
        QVERIFY(!fixture.lastSucceeded());
        QCOMPARE(bytes(directory.filePath("lis.ini")), large);
        QTemporaryDir other;
        Fixture badNames(other.path(), []() { return QStringList{QString()}; });
        QTRY_COMPARE(badNames.operations.count(), 1);
        QVERIFY(!badNames.lastSucceeded());
        QVERIFY(!QFileInfo::exists(other.filePath("lis.ini")));
    }
    void symlinkNeverReadOrWritten()
    {
#ifdef Q_OS_LINUX
        QTemporaryDir directory, outside;
        const QByteArray seed("[sn100]\noffline_lis_schema=1\n");
        writeBytes(outside.filePath("original.ini"), seed);
        QVERIFY(QFile::link(outside.filePath("original.ini"), directory.filePath("lis.ini")));
        Fixture fixture(directory.path());
        QTRY_COMPARE(fixture.operations.count(), 1);
        QVERIFY(!fixture.lastSucceeded());
        QCOMPARE(bytes(outside.filePath("original.ini")), seed);
        QVERIFY(!QFileInfo::exists(directory.filePath(".lis-locks")));
        QTemporaryDir lockDirectory;
        QVERIFY(QFile::link(outside.path(), lockDirectory.filePath(".lis-locks")));
        Fixture lockSymlink(lockDirectory.path());
        QTRY_COMPARE(lockSymlink.operations.count(), 1);
        QVERIFY(!lockSymlink.lastSucceeded());
        QVERIFY(!QFileInfo::exists(lockDirectory.filePath("lis.ini")));
        QVERIFY(!QFileInfo::exists(outside.filePath("settings.lock")));
#else
        QSKIP("POSIX symlink fixture only");
#endif
    }
    void platformMetadataEnumerationAndOriginalGeometry()
    {
        QString error;
        const QStringList ports = localSerialPortNames(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        qInfo() << "READ_ONLY_PORT_CANDIDATES" << ports.size();
        QTemporaryDir directory;
        Fixture fixture(directory.path());
        QTRY_VERIFY(fixture.page.isReady());
        QCOMPARE(control<QWidget>(&fixture.page, "widget_5")->geometry(), QRect(400,40,611,781));
        QCOMPARE(control<QPushButton>(&fixture.page, "btnSave")->geometry(), QRect(420,690,161,61));
        QCOMPARE(control<QComboBox>(&fixture.page, "cmb_SamLis_Mode")->currentIndex(), 0);
        const QString evidence = qEnvironmentVariable("SN100_LIS_TEST_EVIDENCE");
        if (!evidence.isEmpty())
        {
            QDir().mkpath(evidence);
            QVERIFY(fixture.page.grab().save(QDir(evidence).filePath("lis_serial.png")));
            control<QtMaterialRadioButton>(&fixture.page, "radioBoxTcp")->setChecked(true);
            QVERIFY(fixture.page.grab().save(QDir(evidence).filePath("lis_tcp.png")));
            answerDraft(QMessageBox::Yes);
            QVERIFY(fixture.page.requestLeave("cleanup"));
        }
    }
};
QTEST_MAIN(LisSettingsTest)
#include "lis_settings_test.moc"
