#ifndef HISTORY_RESULT_STATISTICS_H
#define HISTORY_RESULT_STATISTICS_H

#include "history_store.h"

// A value snapshot of one original task. Statistics never edit the source
// report, task, abnormal code or raw result text.
struct HistoryStatisticEntry
{
    qint64 taskId = 0;
    QDateTime testedAt;
    QString abnormalType;
    HistoryResultValue value;
    // False means this task has no saved value for a column observed in
    // another task of the same project; value contains only column metadata.
    bool valuePresent = true;
    bool numeric = false;
    double scaledValue = 0.0;
};

struct HistoryStatisticGroup
{
    QString project;
    int resultType = -1;
    QString label;
    QString unit;
    int decimals = -1;
    double factor = 1.0;
    QList<HistoryStatisticEntry> entries;
    int validCount = 0;
    int invalidCount = 0;
    bool configurationConsistent = true;
    bool meanAvailable = false;
    bool sampleSdAvailable = false;
    bool cvAvailable = false;
    double mean = 0.0;
    double sampleSd = 0.0;
    double cvPercent = 0.0;
    QString meanText;
    QString cvText;
    QString meanUnavailableReason;
    QString cvUnavailableReason;

    // The legacy report inserts one summary row only for repeat tasks,
    // including invalid repeats. A single task gets no extra row.
    bool hasAverageRow() const { return entries.size() > 1; }
};

class HistoryResultStatistics
{
public:
    // Groups belong to a single report, keyed by project + resultType and
    // ordered by first occurrence in the report's explicit column contract.
    // Only columns observed in this report are known. Each known column has
    // an entry per same-project task, including explicitly missing values.
    // Calibration(2)/Rate(3) alone use factor; invalid/nonfinite values do not
    // participate, while a numeric result bearing abnormalType=="1" does.
    // SD is the sample SD (n-1); CV is signed SD/mean*100, displayed to 2 dp.
    // Empty/one valid/zero mean/overflow/config conflict are explicit states.
    static QList<HistoryStatisticGroup> groups(const HistoryReport &report);
};

#endif
