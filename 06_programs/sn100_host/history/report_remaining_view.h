#ifndef SN100_REPORT_REMAINING_VIEW_H
#define SN100_REPORT_REMAINING_VIEW_H

#include <QList>
#include <QMetaType>
#include <QString>

// The key identifies a saved column, never its translated title or list row.
// Group 1 is patient information; group 2 is a saved result column. Display
// preferences never change the saved unit, scale, precision or result value.
struct HistoryDisplayColumn
{
    QString key;
    QString title;
    int group = 0;
    bool visible = true;
    bool required = false;
};
using HistoryDisplayColumns = QList<HistoryDisplayColumn>;

Q_DECLARE_METATYPE(HistoryDisplayColumn)
Q_DECLARE_METATYPE(HistoryDisplayColumns)

#endif
