#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include "data/printer_settings_store.h"
#include "ui/frmprintersetting.h"

namespace
{
template<class T> T *control(QObject *page, const char *name)
{
    return page->findChild<T *>(QString::fromLatin1(name));
}

QString root()
{
    const QString directory = qEnvironmentVariable("SN100_PRINTER_TEST_ROOT");
    if (!QDir::isAbsolutePath(directory)) qFatal("SN100_PRINTER_TEST_ROOT must be absolute");
    QDir().mkpath(directory);
    return directory;
}

QByteArray bytes(const QString &name)
{
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct Fixture
{
    QThread worker;
    PrinterSettingsStore *store;
    frmPrinterSetting page;
    QSignalSpy operations;
    explicit Fixture(const QString &directory, PrinterSettingsStore::PrinterNames names) :
        store(new PrinterSettingsStore(names)), page(store),
        operations(&page, &frmPrinterSetting::operationFinished)
    {
        store->moveToThread(&worker);
        QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
        QObject::connect(&worker, &QThread::started, store, [this, directory]() { store->initialize(directory); });
        QObject::connect(&page, &frmPrinterSetting::statusChanged, &page,
                         [](const QString &message, bool error) { if (error) qInfo().noquote() << message; });
        page.resize(1453, 863);
        page.show();
        worker.start();
    }
    ~Fixture()
    {
        QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
    }
    void save(const QString &name)
    {
        QMetaObject::invokeMethod(store, "save", Qt::QueuedConnection, Q_ARG(QString, name));
    }
    int errorCount() const
    {
        // QSignalSpy对worker信号使用DirectConnection，不能把该计数当GUI
        // 已处理失败/已恢复按钮的屏障；仅观察GUI页的最终完成信号。
        int count = 0;
        for (const auto &operation : operations)
            if (operation.size() == 2 && !operation.at(1).toBool()) ++count;
        return count;
    }
};

const QString first = QStringLiteral("匿名打印设备甲");
const QString second = QStringLiteral("匿名打印设备乙");
PrinterSettingsStore::PrinterNames fixedNames()
{
    return []() { return QStringList{first, second}; };
}

void answerDraft(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(20, qApp, [answer]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(answer)->click();
    });
}
}

class PrinterSettingsTest : public QObject
{
    Q_OBJECT
private slots:
    void emptyListAndOriginalGeometry();
    void saveUnicodeAndReopenMissing();
    void invalidUnavailableAndNoop();
    void storageFailureRetry();
    void rejectForeignAndSymlink();
    void rejectAncestorReplacement();
    void draftCancelDiscardAndBusy();
    void workerEnumerationKeepsGuiResponsive();
    void externalSameSizeAndMtimeConflict();
    void concurrentLockAndEnumerationConflict();
    void preserveUnrelatedKeys();
    void closedStoreRejectsWrite();
};

void PrinterSettingsTest::emptyListAndOriginalGeometry()
{
    QTemporaryDir directory(root() + QStringLiteral("/empty_XXXXXX"));
    Fixture f(directory.path(), []() { return QStringList(); });
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    QVERIFY(!f.page.isBusy());
    QCOMPARE(control<QListWidget>(&f.page,"lstProject")->count(), 0);
    QVERIFY(control<QLineEdit>(&f.page,"ldtPrinterSelect")->isReadOnly());
    QCOMPARE(control<QWidget>(&f.page,"widget")->geometry(), QRect(390,20,681,821));
    QCOMPARE(control<QPushButton>(&f.page,"btnApply")->geometry(), QRect(500,750,161,61));
    QVERIFY(!control<QPushButton>(&f.page,"btnApply")->isEnabled());
    QVERIFY(f.page.requestLeave(QStringLiteral("退出")));
}

void PrinterSettingsTest::saveUnicodeAndReopenMissing()
{
    QTemporaryDir directory(root() + QStringLiteral("/reopen_XXXXXX"));
    {
        Fixture f(directory.path(), fixedNames());
        QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
        control<QListWidget>(&f.page,"lstProject")->setCurrentRow(0);
        QVERIFY(f.page.isDirty());
        QTest::mouseClick(control<QPushButton>(&f.page,"btnApply"), Qt::LeftButton);
        QTRY_COMPARE(f.page.committedName(), first);
        QVERIFY(!f.page.isDirty());
        QSettings settings(directory.filePath(QStringLiteral("printer.ini")), QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        QCOMPARE(settings.value(QStringLiteral("printer/name")).toString(), first);
    }
    Fixture f(directory.path(), []() { return QStringList(); });
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    QCOMPARE(f.page.committedName(), first);
    QCOMPARE(control<QLineEdit>(&f.page,"ldtPrinterSelect")->text(), first);
    QVERIFY(f.page.statusText().contains(QStringLiteral("不可用")));
    QCOMPARE(control<QListWidget>(&f.page,"lstProject")->count(), 0);
}

void PrinterSettingsTest::invalidUnavailableAndNoop()
{
    QTemporaryDir directory(root() + QStringLiteral("/validation_XXXXXX"));
    Fixture f(directory.path(), fixedNames());
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    f.save(first);
    QTRY_COMPARE(f.page.committedName(), first);
    const QByteArray before = bytes(directory.filePath(QStringLiteral("printer.ini")));
    f.save(QString());
    QTRY_COMPARE(f.errorCount(), 1);
    f.save(QStringLiteral("非本机设备"));
    QTRY_COMPARE(f.errorCount(), 2);
    f.save(QString(1025, QChar('a')));
    QTRY_COMPARE(f.errorCount(), 3);
    const int count = f.operations.count();
    f.save(first);
    QTRY_COMPARE(f.operations.count(), count + 1);
    QCOMPARE(bytes(directory.filePath(QStringLiteral("printer.ini"))), before);
    QCOMPARE(f.page.committedName(), first);
}

void PrinterSettingsTest::storageFailureRetry()
{
    QTemporaryDir directory(root() + QStringLiteral("/failure_XXXXXX"));
    const QString file = directory.filePath(QStringLiteral("printer.ini"));
    const QString retained = directory.filePath(QStringLiteral("retained.ini"));
    int enumerations = 0;
    // 在枚举后的实际原子写入点制造OS失败，不用假成功/假错误替代文件操作。
    Fixture f(directory.path(), [&]()
    {
        if (++enumerations == 3)
        {
            QFile::rename(file, retained);
            QDir().mkdir(file);
        }
        return QStringList{first, second};
    });
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    f.save(first);
    QTRY_COMPARE(f.page.committedName(), first);
    const QByteArray committed = bytes(file);
    QSignalSpy requests(&f.page, &frmPrinterSetting::requestSave);
    control<QListWidget>(&f.page,"lstProject")->setCurrentRow(1);
    QTest::mouseClick(control<QPushButton>(&f.page,"btnApply"), Qt::LeftButton);
    QTRY_COMPARE(f.errorCount(), 1);
    QVERIFY(!f.page.isBusy());
    QVERIFY(control<QPushButton>(&f.page,"btnApply")->isEnabled());
    QCOMPARE(requests.count(),1);
    QCOMPARE(f.page.committedName(), first);
    QVERIFY(f.page.isDirty());
    QCOMPARE(bytes(retained), committed);
    QVERIFY(QDir().rmdir(file));
    QVERIFY(QFile::rename(retained, file));
    QTest::mouseClick(control<QPushButton>(&f.page,"btnApply"), Qt::LeftButton);
    QCOMPARE(requests.count(),2);
    QTRY_COMPARE(f.page.committedName(), second);
    QVERIFY(!f.page.isDirty());
}

void PrinterSettingsTest::rejectForeignAndSymlink()
{
    QTemporaryDir directory(root() + QStringLiteral("/foreign_XXXXXX"));
    const QString file = directory.filePath(QStringLiteral("printer.ini"));
    QFile foreign(file);
    QVERIFY(foreign.open(QIODevice::WriteOnly));
    foreign.write("[printer]\nname=foreign\n");
    foreign.close();
    const QByteArray before = bytes(file);
    {
        Fixture f(directory.path(), fixedNames());
        QTRY_COMPARE(f.errorCount(), 1);
        QVERIFY(!f.page.isReady());
        QCOMPARE(bytes(file), before);
    }
    const QString link = directory.path() + QStringLiteral("_link");
    QVERIFY(QFile::link(directory.path(), link));
    {
        Fixture f(link, fixedNames());
        QTRY_COMPARE(f.errorCount(), 1);
        QVERIFY(!f.page.isReady());
    }
    QVERIFY(QFile::remove(link));
    PrinterSettingsStore store(fixedNames());
    QSignalSpy errors(&store, &PrinterSettingsStore::error);
    store.initialize(QStringLiteral("relative"));
    QCOMPARE(errors.count(), 1);
}

void PrinterSettingsTest::rejectAncestorReplacement()
{
    QTemporaryDir directory(root() + QStringLiteral("/ancestor_XXXXXX"));
    const QString retained = directory.path() + QStringLiteral("_retained");
    Fixture f(directory.path(), fixedNames());
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    f.save(first);
    QTRY_COMPARE(f.page.committedName(), first);
    const QByteArray before = bytes(directory.filePath(QStringLiteral("printer.ini")));
    QVERIFY(QDir().rename(directory.path(), retained));
    QVERIFY(QFile::link(retained, directory.path()));
    QMetaObject::invokeMethod(f.store, "load", Qt::QueuedConnection);
    QTRY_COMPARE(f.errorCount(), 1);
    f.save(second);
    QTRY_COMPARE(f.errorCount(), 2);
    QCOMPARE(bytes(retained + QStringLiteral("/printer.ini")), before);
    QVERIFY(QFile::remove(directory.path()));
    QVERIFY(QDir().rename(retained, directory.path()));
}

void PrinterSettingsTest::draftCancelDiscardAndBusy()
{
    QTemporaryDir directory(root() + QStringLiteral("/draft_XXXXXX"));
    Fixture f(directory.path(), fixedNames());
    QVERIFY(!f.page.requestLeave(QStringLiteral("离页")));
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    control<QListWidget>(&f.page,"lstProject")->setCurrentRow(1);
    answerDraft(QMessageBox::No);
    QVERIFY(!f.page.requestLeave(QStringLiteral("切页")));
    QVERIFY(f.page.isDirty());
    answerDraft(QMessageBox::Yes);
    QVERIFY(f.page.requestLeave(QStringLiteral("注销")));
    QVERIFY(!f.page.isDirty());
    QVERIFY(control<QLineEdit>(&f.page,"ldtPrinterSelect")->text().isEmpty());
    control<QListWidget>(&f.page,"lstProject")->setCurrentRow(0);
    QTest::mouseClick(control<QPushButton>(&f.page,"btnApply"), Qt::LeftButton);
    QVERIFY(f.page.isBusy());
    QVERIFY(!f.page.requestLeave(QStringLiteral("关闭")));
    QTRY_COMPARE(f.page.committedName(), first);
    QVERIFY(f.page.requestLeave(QStringLiteral("关闭")));
}

void PrinterSettingsTest::workerEnumerationKeepsGuiResponsive()
{
    QTemporaryDir directory(root() + QStringLiteral("/responsive_XXXXXX"));
    int heartbeats = 0;
    QTimer timer;
    connect(&timer, &QTimer::timeout, this, [&]() { ++heartbeats; });
    timer.start(10);
    Fixture f(directory.path(), []()
    {
        QThread::msleep(180);
        return QStringList{first};
    });
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    QVERIFY(heartbeats >= 5);
}

void PrinterSettingsTest::externalSameSizeAndMtimeConflict()
{
    QTemporaryDir directory(root() + QStringLiteral("/external_XXXXXX"));
    Fixture f(directory.path(), fixedNames());
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    f.save(first);
    QTRY_COMPARE(f.page.committedName(), first);
    const QString path = directory.filePath(QStringLiteral("printer.ini"));
    const QDateTime originalTime = QFileInfo(path).lastModified();
    const QByteArray before = bytes(path);
    QByteArray changed = before;
    changed.replace(first.toUtf8(), second.toUtf8());
    QVERIFY(changed != before);
    QCOMPARE(changed.size(), before.size());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(changed), qint64(changed.size()));
    QVERIFY(file.flush());
    QVERIFY(file.setFileTime(originalTime, QFileDevice::FileModificationTime));
    file.close();
    f.save(second);
    QTRY_COMPARE(f.errorCount(), 1);
    QCOMPARE(bytes(path), changed);
    QCOMPARE(f.page.committedName(), first);
    QMetaObject::invokeMethod(f.store, "load", Qt::QueuedConnection);
    QTRY_COMPARE(f.page.committedName(), second);
}

void PrinterSettingsTest::concurrentLockAndEnumerationConflict()
{
    QTemporaryDir directory(root() + QStringLiteral("/concurrent_XXXXXX"));
    const QString path = directory.filePath(QStringLiteral("printer.ini"));
    int enumerations = 0;
    Fixture f(directory.path(), [&]()
    {
        if (++enumerations == 3)
        {
            QFile file(path);
            if (file.open(QIODevice::Append)) file.write("\n; external anonymous writer\n");
        }
        return QStringList{first, second};
    });
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    f.save(first);
    QTRY_COMPARE(f.page.committedName(), first);
    QLockFile lock(directory.filePath(QStringLiteral(".printer-settings.lock")));
    QVERIFY(lock.tryLock(0));
    f.save(second);
    QTRY_COMPARE(f.errorCount(), 1);
    lock.unlock();
    const QByteArray before = bytes(path);
    f.save(second);
    QTRY_COMPARE(f.errorCount(), 2);
    QCOMPARE(bytes(path), before + QByteArray("\n; external anonymous writer\n"));
    QCOMPARE(f.page.committedName(), first);
    QMetaObject::invokeMethod(f.store, "load", Qt::QueuedConnection);
    const int operations = f.operations.count();
    QTRY_VERIFY(f.operations.count() > operations);
    f.save(second);
    QTRY_COMPARE(f.page.committedName(), second);
}

void PrinterSettingsTest::preserveUnrelatedKeys()
{
    QTemporaryDir directory(root() + QStringLiteral("/unrelated_XXXXXX"));
    Fixture f(directory.path(), fixedNames());
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    const QString path = directory.filePath(QStringLiteral("printer.ini"));
    {
        QSettings settings(path, QSettings::IniFormat);
        settings.setIniCodec("UTF-8");
        settings.setValue(QStringLiteral("other/anonymous_text"),QStringLiteral("无关配置保留"));
        settings.setValue(QStringLiteral("other/count"),17);
        settings.sync();
        QCOMPARE(settings.status(),QSettings::NoError);
    }
    const int before = f.operations.count();
    QMetaObject::invokeMethod(f.store,"load",Qt::QueuedConnection);
    QTRY_VERIFY(f.operations.count() > before);
    f.save(first);
    QTRY_COMPARE(f.page.committedName(),first);
    QSettings settings(path,QSettings::IniFormat);
    settings.setIniCodec("UTF-8"); settings.sync();
    QCOMPARE(settings.value(QStringLiteral("other/anonymous_text")).toString(),QStringLiteral("无关配置保留"));
    QCOMPARE(settings.value(QStringLiteral("other/count")).toInt(),17);
    QCOMPARE(settings.value(QStringLiteral("printer/name")).toString(),first);
}

void PrinterSettingsTest::closedStoreRejectsWrite()
{
    QTemporaryDir directory(root() + QStringLiteral("/close_XXXXXX"));
    Fixture f(directory.path(), fixedNames());
    QTRY_VERIFY(f.page.isReady() || f.errorCount() != 0); QVERIFY2(f.page.isReady(), qPrintable(f.page.statusText()));
    QMetaObject::invokeMethod(f.store, "close", Qt::BlockingQueuedConnection);
    const QByteArray before = bytes(directory.filePath(QStringLiteral("printer.ini")));
    f.save(first);
    QTRY_COMPARE(f.errorCount(), 1);
    QCOMPARE(bytes(directory.filePath(QStringLiteral("printer.ini"))), before);
}

QTEST_MAIN(PrinterSettingsTest)
#include "printer_settings_test.moc"
