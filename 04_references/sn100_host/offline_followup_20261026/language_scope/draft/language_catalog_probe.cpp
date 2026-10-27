#include "language_catalog_probe.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QTranslator>

QVector<LanguageMessageProbe> inspectLanguageSamples(const QTranslator &translator)
{
    QVector<LanguageMessageProbe> samples{
        {QByteArray("frmLanguageSetting"), QStringLiteral("应用"), QString()},
        {QByteArray("frmLanguageSetting"), QStringLiteral("请选择要切换的语言："), QString()}
    };
    for (LanguageMessageProbe &sample : samples)
    {
        const QByteArray utf8 = sample.source.toUtf8();
        // Direct translator lookup exposes missing entries. Application-level
        // translate() would silently fall back to the Chinese source text.
        sample.translation = translator.translate(sample.context.constData(), utf8.constData());
    }
    return samples;
}

int translatedSampleCount(const QVector<LanguageMessageProbe> &samples)
{
    int count = 0;
    for (const LanguageMessageProbe &sample : samples)
    {
        if (!sample.translation.isEmpty()) ++count;
    }
    return count;
}

bool sampleCoverageComplete(const QVector<LanguageMessageProbe> &samples)
{
    return !samples.isEmpty() && translatedSampleCount(samples) == samples.size();
}

LanguageCatalogInspection inspectLanguageCatalog(const QString &absolutePath)
{
    LanguageCatalogInspection result;
    result.path = absolutePath;
    const QFileInfo file(absolutePath);
    if (absolutePath.startsWith(QChar(':')) || !file.isAbsolute()
        || file.suffix().compare(QStringLiteral("qm"), Qt::CaseInsensitive) != 0)
    {
        result.error = QStringLiteral("仅检查显式指定的本地绝对路径.qm文件。");
        return result;
    }
    if (!file.isFile() || !file.isReadable())
    {
        result.error = QStringLiteral("翻译文件不存在或不可读取。");
        return result;
    }
    QTranslator translator;
    result.loaded = translator.load(file.absoluteFilePath());
    result.samples = inspectLanguageSamples(translator);
    if (!result.loaded)
    {
        result.error = QStringLiteral("Qt未成功载入该翻译文件。");
    }
    // Deliberately no installTranslator, QSettings, locale mutation or UI hook.
    return result;
}

QJsonObject languageCatalogJson(const LanguageCatalogInspection &inspection)
{
    QJsonArray samples;
    for (const LanguageMessageProbe &sample : inspection.samples)
    {
        samples.append(QJsonObject{{QStringLiteral("context"), QString::fromLatin1(sample.context)},
            {QStringLiteral("source"), sample.source}, {QStringLiteral("translation"), sample.translation},
            {QStringLiteral("entry_found"), !sample.translation.isEmpty()}});
    }
    return {{QStringLiteral("scope"), QStringLiteral("仅检查语言页两条抽样文字；不切换程序语言。")},
        {QStringLiteral("catalog"), inspection.path}, {QStringLiteral("catalog_loaded"), inspection.loaded},
        {QStringLiteral("error"), inspection.error}, {QStringLiteral("samples"), samples},
        {QStringLiteral("translated_sample_count"), translatedSampleCount(inspection.samples)},
        {QStringLiteral("sample_coverage_complete"), inspection.loaded && sampleCoverageComplete(inspection.samples)},
        {QStringLiteral("full_application_switching_assessed"), false},
        {QStringLiteral("translator_installed"), false}, {QStringLiteral("language_setting_written"), false}};
}
