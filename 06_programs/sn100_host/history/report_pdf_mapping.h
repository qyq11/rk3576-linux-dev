#pragma once

#include <QFont>
#include <QList>
#include <QString>

// Exact final label text observed immediately around QtRPT's PDF draw call.
// The id is also carried by two off-page rectangle markers in that PDF.
struct HistoryPdfTextSpan
{
    int id = 0;
    QString text;
    QFont font;
};

// Accepts only this renderer's bounded, single-revision Qt5 PDF dialect.
// The output must be a new file. Failure never publishes a partial result.
// Visible glyph outlines, positions and page geometry are left unchanged.
bool rewriteHistoryPdfTextMapping(const QString &input, const QString &newOutput,
                                 const QList<HistoryPdfTextSpan> &spans, QString *error);
