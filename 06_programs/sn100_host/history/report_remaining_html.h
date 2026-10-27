#ifndef HISTORY_REPORT_REMAINING_HTML_H
#define HISTORY_REPORT_REMAINING_HTML_H

#include "report_document.h"
#include <QRegularExpression>
#include <QTextCodec>
#include <QTextDocument>

// QtRPT's original label-only HTML is reused verbatim. This guard is shared by
// the isolated helper and the publication controller; it does not render a new
// template. The source renderer inserts text without entity escaping, so an
// ampersand must be refused until that upstream behavior has its own migration.
inline bool validateHistoryHtmlSource(const HistoryPrintDocument &document, QString *error)
{
    const auto check = [error](const QString &text) {
        if (text.contains('&'))
        {
            *error = QStringLiteral("原HTML输出未转义&字符；本次不生成可能改变内容的HTML，原始数据保留，可使用PDF导出。");
            return false;
        }
        return true;
    };
    for (auto it = document.headers.cbegin(); it != document.headers.cend(); ++it)
        if (!check(it.value())) return false;
    for (const auto &row : document.rows)
        for (auto it = row.cbegin(); it != row.cend(); ++it)
            if (!check(it.value())) return false;
    return true;
}

inline bool validateHistoryHtml(const QByteArray &bytes, const HistoryPrintDocument *document,
                                QString *error)
{
    QTextCodec::ConverterState state;
    const QString html = QTextCodec::codecForName("UTF-8")->toUnicode(bytes.constData(), bytes.size(), &state);
    if (bytes.size() < 100 || bytes.size() > 32 * 1024 * 1024 || state.invalidChars
        || !html.startsWith(QStringLiteral("<HTML><HEAD><meta charset=\"UTF-8\"></HEAD><BODY>"))
        || !html.trimmed().endsWith(QStringLiteral("</BODY></HTML>"))
        || html.contains(QChar::Null) || !html.contains(QStringLiteral("<div ")))
    {
        *error = QStringLiteral("HTML不是完整的原模板UTF-8文件，未发布。");
        return false;
    }
    if (!document) return true;
    QTextDocument parsed;
    parsed.setHtml(html);
    const QString text = parsed.toPlainText().simplified();
    const auto check = [&text, error](const QString &value) {
        if (!value.trimmed().isEmpty() && !text.contains(value.simplified()))
        {
            *error = QStringLiteral("HTML回读未找到完整报告字段，未发布。");
            return false;
        }
        return true;
    };
    for (auto it = document->headers.cbegin(); it != document->headers.cend(); ++it)
        if (!check(it.value())) return false;
    for (const auto &row : document->rows)
        for (auto it = row.cbegin(); it != row.cend(); ++it)
            if (!check(it.value())) return false;
    return true;
}

#endif
