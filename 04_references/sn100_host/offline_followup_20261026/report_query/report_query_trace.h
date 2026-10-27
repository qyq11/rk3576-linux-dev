#ifndef REPORT_QUERY_TRACE_H
#define REPORT_QUERY_TRACE_H

// Diagnostic copy only: all observers run on the GUI thread. No new waits,
// retry, control mutation, production logging or changed assertions.
namespace LegacyQueryTrace
{
inline QJsonObject widget(QWidget *value)
{
    if (!value) return {{QStringLiteral("exists"), false}};
    const QRect r = value->rect();
    const QPoint p = value->mapToGlobal(r.center());
    return {{QStringLiteral("exists"), true},
        {QStringLiteral("class"), QString::fromLatin1(value->metaObject()->className())},
        {QStringLiteral("name"), value->objectName()},
        {QStringLiteral("visible"), value->isVisible()},
        {QStringLiteral("enabled"), value->isEnabled()},
        {QStringLiteral("width"), r.width()}, {QStringLiteral("height"), r.height()},
        {QStringLiteral("global_x"), p.x()}, {QStringLiteral("global_y"), p.y()}};
}

inline void event(const QString &stage, QJsonObject data = {})
{
    static QElapsedTimer elapsed;
    static qint64 serial = 0;
    if (!elapsed.isValid()) elapsed.start();
    data.insert(QStringLiteral("stage"), stage);
    data.insert(QStringLiteral("serial"), double(++serial));
    data.insert(QStringLiteral("monotonic_ns"), double(elapsed.nsecsElapsed()));
    data.insert(QStringLiteral("method"), QString::fromLatin1(QTest::currentTestFunction()));
    data.insert(QStringLiteral("focus"), widget(QApplication::focusWidget()));
    data.insert(QStringLiteral("active_modal"), widget(QApplication::activeModalWidget()));
    QFile file(QString::fromUtf8(qgetenv("SN100_LEGACY_QUERY_TRACE")));
    const QByteArray line = QJsonDocument(data).toJson(QJsonDocument::Compact) + '\n';
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || file.write(line) != line.size())
        qWarning() << "LEGACY_QUERY_TRACE_WRITE_FAILED" << stage;
}

inline QJsonObject filter(const HistoryFilter &value)
{
    return {{QStringLiteral("by_date"), value.byDate},
        {QStringLiteral("start"), value.start.toString(Qt::ISODateWithMs)},
        {QStringLiteral("end"), value.end.toString(Qt::ISODateWithMs)},
        {QStringLiteral("sql_start"), value.start.toString(QStringLiteral("yyyyMMddHHmmss"))},
        {QStringLiteral("sql_end"), value.end.toString(QStringLiteral("yyyyMMddHHmmss"))},
        {QStringLiteral("sample_number"), value.sampleNumber},
        {QStringLiteral("barcode"), value.barcode},
        {QStringLiteral("offset"), value.offset}, {QStringLiteral("limit"), value.limit}};
}

inline void watch(HistoryPage *page, HistoryStore *store, const QString &directory)
{
    QObject::connect(page, &HistoryPage::requestQuery, page, [directory](qint64 id, HistoryFilter value) {
        event(QStringLiteral("request_query"), {{QStringLiteral("id"), double(id)},
            {QStringLiteral("directory"), directory}, {QStringLiteral("filter"), filter(value)}});
    });
    QObject::connect(store, &HistoryStore::queryReady, page,
        [](qint64 id, HistoryFilter value, HistoryReports reports, qint64 total) {
        event(QStringLiteral("query_ready_gui_delivery"), {{QStringLiteral("id"), double(id)},
            {QStringLiteral("filter"), filter(value)}, {QStringLiteral("rows"), reports.size()},
            {QStringLiteral("total"), double(total)}});
    });
    QObject::connect(store, &HistoryStore::error, page, [](qint64 id, QString operation, QString message) {
        event(QStringLiteral("store_error_gui_delivery"), {{QStringLiteral("id"), double(id)},
            {QStringLiteral("operation"), operation}, {QStringLiteral("message"), message}});
    });
    QObject::connect(page, &HistoryPage::operationFinished, page, [page](QString operation, bool success) {
        event(QStringLiteral("operation_finished"), {{QStringLiteral("operation"), operation},
            {QStringLiteral("success"), success}, {QStringLiteral("busy"), page->isBusy()},
            {QStringLiteral("status"), page->property("lastStatus").toString()}});
    });
}
}
#endif
