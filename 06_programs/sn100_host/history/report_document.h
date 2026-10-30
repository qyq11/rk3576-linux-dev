#ifndef HISTORY_REPORT_DOCUMENT_H
#define HISTORY_REPORT_DOCUMENT_H

#include "history_store.h"
#include <QJsonObject>
#include <QMap>

struct HistoryPrintColumn
{
    QString project;
    int resultType = -1;
    QString description;
    bool enabled = true;
    bool mainResult = true;
    bool hasReference = false;
    double lower = 0.0;
    double upper = 0.0;
};

struct HistoryPrintOptions
{
    QString hospitalName;
    QList<HistoryPrintColumn> columns;
    bool originalProfile = false;
};

// Immutable render input. No SQL, QWidget, account or device object crosses
// into the renderer process; each preview/export owns its own value snapshot.
struct HistoryPrintDocument
{
    QMap<QString, QString> headers;
    QList<QMap<QString, QString>> rows;
    QByteArray originalTemplateXml;
};

class HistoryReportDocument
{
public:
    static bool anonymousOptions(HistoryPrintOptions *options, QString *error);
    static bool originalOptions(const HistoryReport &report, HistoryPrintOptions *options, QString *error);
    static bool build(const HistoryReport &report, const HistoryPrintOptions &options,
                      HistoryPrintDocument *document, QString *error);
    static QJsonObject toJson(const HistoryPrintDocument &document);
    static bool fromJson(const QJsonObject &json, HistoryPrintDocument *document, QString *error);
};

#endif
