#ifndef SN100_ORIGINAL_HISTORY_SOURCE_H
#define SN100_ORIGINAL_HISTORY_SOURCE_H
#include "history_store.h"
#include "../projects/original_project_repository.h"

// 仅由HistoryStore所属线程使用，直接读取原result/results.db和当前原项目参数。
// 不生成检测结果，也不把原表转换为匿名演示库。
class OriginalHistorySource
{
public:
    bool initialize(const QString &root, QString *message);
    bool refresh(quint64 revision, QString *message);
    QString root() const { return root_; }
    QString databasePath() const;
    HistoryCurrentFormatSource source() const { return source_; }
    bool readResults(QSqlDatabase &db, qint64 rowId, HistoryResults *results, QString *message) const;
    bool display(QSqlDatabase &db, HistoryDisplayColumns *columns,
                 HistoryDisplayColumns *defaults, QString *message) const;
    bool saveDisplay(QSqlDatabase &db, HistoryDisplayColumns columns, QString *message) const;
private:
    bool defaults(HistoryDisplayColumns *columns, QString *message) const;
    QString root_;
    OriginalProjectRepository projects_;
    ProjectRecords records_;
    ProjectUnits units_;
    HistoryCurrentFormatSource source_;
    quint64 generation_ = 0;
    QByteArray templateXml_;
};
#endif
