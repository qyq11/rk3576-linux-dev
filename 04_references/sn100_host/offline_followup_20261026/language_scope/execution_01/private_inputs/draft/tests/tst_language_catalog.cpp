#include <QtTest>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QTemporaryDir>
#include <QTranslator>
#include "language_catalog_probe.h"

class SampleTranslator : public QTranslator
{
public:
    enum Kind { Complete, Partial, FrameworkOnly };
    explicit SampleTranslator(Kind kind) : m_kind(kind) {}
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override
    {
        const QByteArray ctx(context);
        const QString text = QString::fromUtf8(source);
        if (m_kind == FrameworkOnly)
            return ctx == QByteArray("QDialogButtonBox") && text == QStringLiteral("Apply")
                ? QStringLiteral("Apply") : QString();
        if (ctx != QByteArray("frmLanguageSetting")) return QString();
        if (text == QStringLiteral("应用")) return QStringLiteral("Apply");
        if (m_kind == Complete && text == QStringLiteral("请选择要切换的语言："))
            return QStringLiteral("Choose the language:");
        return QString();
    }
private:
    Kind m_kind;
};

class LanguageCatalogTest : public QObject
{
    Q_OBJECT
    QString directory_;
private slots:
    void initTestCase()
    {
        directory_ = QString::fromUtf8(qgetenv("LANGUAGE_SCOPE_TEST_DIR"));
        QVERIFY(QDir::isAbsolutePath(directory_) && !QFileInfo::exists(directory_));
        QVERIFY(!directory_.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive));
        QVERIFY(!directory_.contains(QStringLiteral("Seerain450_PC"), Qt::CaseInsensitive));
        QVERIFY(QDir().mkpath(directory_));
    }
    void emptySamplesAreNotCoverage()
    {
        QVERIFY(!sampleCoverageComplete({}));
    }
    void frameworkMessagesDoNotCoverApplicationContext()
    {
        SampleTranslator translator(SampleTranslator::FrameworkOnly);
        const auto samples = inspectLanguageSamples(translator);
        QCOMPARE(samples.size(), 2);
        QCOMPARE(translatedSampleCount(samples), 0);
        QVERIFY(!sampleCoverageComplete(samples));
    }
    void partialCatalogIsNotCompleteCoverage()
    {
        SampleTranslator translator(SampleTranslator::Partial);
        const auto samples = inspectLanguageSamples(translator);
        QCOMPARE(translatedSampleCount(samples), 1);
        QVERIFY(!sampleCoverageComplete(samples));
    }
    void sampleCoverageDoesNotSwitchTheApplication()
    {
        const QString before = QCoreApplication::translate("frmLanguageSetting", "应用");
        const QString localeBefore = QLocale().name();
        SampleTranslator translator(SampleTranslator::Complete);
        const auto samples = inspectLanguageSamples(translator);
        QVERIFY(sampleCoverageComplete(samples));
        LanguageCatalogInspection result;
        result.loaded = true;
        result.samples = samples;
        const auto json = languageCatalogJson(result);
        QVERIFY(json.value(QStringLiteral("sample_coverage_complete")).toBool());
        QVERIFY(!json.value(QStringLiteral("full_application_switching_assessed")).toBool());
        QVERIFY(!json.value(QStringLiteral("translator_installed")).toBool());
        QVERIFY(!json.value(QStringLiteral("language_setting_written")).toBool());
        QCOMPARE(QCoreApplication::translate("frmLanguageSetting", "应用"), before);
        QCOMPARE(QLocale().name(), localeBefore);
    }
    void relativePathIsRejectedWithoutImplicitSearch()
    {
        const auto result = inspectLanguageCatalog(QStringLiteral("qt_en.qm"));
        QVERIFY(!result.loaded && !result.error.isEmpty());
    }
    void missingCatalogIsAnExplicitFailure()
    {
        QTemporaryDir directory(QDir(directory_).filePath(QStringLiteral("case_XXXXXX")));
        QVERIFY(directory.isValid());
        const auto result = inspectLanguageCatalog(directory.filePath(QStringLiteral("missing.qm")));
        QVERIFY(!result.loaded && !result.error.isEmpty());
    }
    void malformedCatalogDoesNotModifyItsInput()
    {
        QTemporaryDir directory(QDir(directory_).filePath(QStringLiteral("case_XXXXXX")));
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("broken.qm")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("anonymous-not-a-qm");
        QCOMPARE(file.write(original), qint64(original.size()));
        file.close();
        const auto result = inspectLanguageCatalog(file.fileName());
        QVERIFY(!result.loaded && !result.error.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }
    void providedQtEnglishSidecarIsNotApplicationCoverage()
    {
        QFile fixture(QFINDTESTDATA("../../fixtures/qt_en_catalog.json"));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const auto data = QJsonDocument::fromJson(fixture.readAll()).object();
        const QByteArray bytes = QByteArray::fromHex(data.value(QStringLiteral("hex")).toString().toLatin1());
        QCOMPARE(bytes.size(), data.value(QStringLiteral("bytes")).toInt());
        QCOMPARE(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex(),
            data.value(QStringLiteral("sha256")).toString().toLatin1());
        QTemporaryDir directory(QDir(directory_).filePath(QStringLiteral("case_XXXXXX")));
        QVERIFY(directory.isValid());
        QFile file(directory.filePath(QStringLiteral("qt_en.qm")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
        file.close();
        const auto result = inspectLanguageCatalog(file.fileName());
        QCOMPARE(translatedSampleCount(result.samples), 0);
        QVERIFY(!sampleCoverageComplete(result.samples));
        // Qt-version-specific load() success is recorded by the CLI, not guessed.
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
    }
};

QTEST_GUILESS_MAIN(LanguageCatalogTest)
#include "tst_language_catalog.moc"
