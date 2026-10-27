#ifndef LANGUAGE_CATALOG_PROBE_H
#define LANGUAGE_CATALOG_PROBE_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

class QTranslator;

struct LanguageMessageProbe
{
    QByteArray context;
    QString source;
    QString translation;
};

struct LanguageCatalogInspection
{
    QString path;
    bool loaded = false;
    QString error;
    QVector<LanguageMessageProbe> samples;
};

// Sample coverage is a resource diagnostic, never whole-application readiness.
QVector<LanguageMessageProbe> inspectLanguageSamples(const QTranslator &translator);
int translatedSampleCount(const QVector<LanguageMessageProbe> &samples);
bool sampleCoverageComplete(const QVector<LanguageMessageProbe> &samples);
LanguageCatalogInspection inspectLanguageCatalog(const QString &absolutePath);
QJsonObject languageCatalogJson(const LanguageCatalogInspection &inspection);

#endif
