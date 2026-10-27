#include "history_store.h"
#include <cmath>

QString HistoryStore::sexText(int sex)
{
    if (sex == 0)
        return QStringLiteral("男");
    if (sex == 1)
        return QStringLiteral("女");
    return QString();
}

QString HistoryStore::displayValue(const HistoryResultValue &value)
{
    if (value.decimals < 0)
        return value.rawValue; // Unknown project settings must not be guessed.
    bool ok = false;
    double number = value.rawValue.toDouble(&ok);
    if (value.resultType == 2 || value.resultType == 3)
        number *= value.factor;
    if (!ok || !std::isfinite(number) || value.decimals > 12)
        return QStringLiteral("****");
    return QString::number(number, 'f', value.decimals);
}

