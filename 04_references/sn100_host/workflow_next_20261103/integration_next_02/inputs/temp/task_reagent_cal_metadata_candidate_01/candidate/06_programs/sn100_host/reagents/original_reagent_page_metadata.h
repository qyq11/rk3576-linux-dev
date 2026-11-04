#ifndef SN100_ORIGINAL_REAGENT_PAGE_METADATA_H
#define SN100_ORIGINAL_REAGENT_PAGE_METADATA_H
#include "original_files/original_business_database.h"
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <limits>

// 原Windows试剂孔控件的批号独立于数据库：旧CAL可只改控件，普通剩余量刷新不会覆盖它。
// 本对象由试剂资料/盘面持有，样本线程共享纯值；互斥区不访问SQL、控件或发送信号。
class OriginalReagentPageMetadata
{
public:
    struct SourceTicket
    {
        QString instance;
        quint64 panel = 0, revision = 0;
    };
    struct View
    {
        QString instance, root;
        quint64 panel = 0, revision = 0;
        QMap<qint64,QVariantMap> originals;
        QMap<qint64,QString> batches;
        QSet<qint64> defaulted; // 本次真实遍历到的空批号，含未选中或随后跳过的候选。
        bool available = false;
    };

    // 仅在真实初始化/盘面重建或已提交的孔位覆盖处调用；同值覆盖也推进代次。
    // names为空且rebuild=false不是覆盖事件，普通load/N/扣退量不调用这里。
    SourceTicket sourceTicket() const
    {
        QMutexLocker guard(&mutex_);
        SourceTicket ticket;
        ticket.instance = instance_; ticket.panel = panel_; ticket.revision = revision_;
        return ticket;
    }
    bool overwrite(const QString &root, const OriginalBusiness::Rows &rows,
                   const QStringList &names, bool rebuild, const SourceTicket &ticket, QString *message)
    {
        QMutexLocker guard(&mutex_);
        if (ticket.instance != instance_ || ticket.panel != panel_ || ticket.revision != revision_)
            return failure(message,QStringLiteral("覆盖来源已过期，未改变较新的盘面批号。"));
        if (!rebuild && names.isEmpty()) return true;
        if (revision_ == std::numeric_limits<quint64>::max() ||
            (rebuild && panel_ == std::numeric_limits<quint64>::max()))
            return reject(message,QStringLiteral("盘面批号代次已耗尽，未回绕。"));
        if (!rebuild && root != root_)
            return failure(message,QStringLiteral("覆盖属于其他资料根，未改变当前盘面批号。"));
        if (root.isEmpty() || (!rebuild && !available_))
            return reject(message,QStringLiteral("盘面批号覆盖不属于当前资料根，须重新初始化。"));
        QMap<qint64,QVariantMap> incoming;
        QSet<QString> found;
        for (const auto &row : rows)
        {
            const auto value = identity(row);
            bool ok = false;
            const qint64 id = row.value(QStringLiteral("id")).toLongLong(&ok);
            const QString name = row.value(QStringLiteral("hole_name")).toString();
            if (!ok || id <= 0 || name.isEmpty() || value.size() != 7 || incoming.contains(id) || found.contains(name))
                return reject(message,QStringLiteral("盘面批号来源缺少唯一原孔位身份。"));
            incoming.insert(id,value); found.insert(name);
        }
        for (const auto &name : names)
            if (!found.contains(name)) return reject(message,QStringLiteral("盘面批号覆盖范围没有对应原孔：")+name);
        if (rebuild)
        {
            root_ = root; originals_.clear(); batches_.clear(); ++panel_;
        }
        for (auto it = incoming.cbegin(); it != incoming.cend(); ++it)
            if (rebuild || names.contains(it.value().value(QStringLiteral("hole_name")).toString()))
            {
                originals_[it.key()] = it.value();
                batches_[it.key()] = it.value().value(QStringLiteral("reagent_batch")).toString();
            }
        ++revision_; available_ = true;
        return true;
    }

    void invalidate(const SourceTicket &ticket)
    {
        QMutexLocker guard(&mutex_);
        if (ticket.instance == instance_ && ticket.panel == panel_ && ticket.revision == revision_) available_ = false;
    }

    bool capture(const QString &root, const OriginalBusiness::Rows &rows, View *result, QString *message) const
    {
        if (!result) return failure(message,QStringLiteral("盘面批号输出为空。"));
        *result = View();
        QMutexLocker guard(&mutex_);
        if (!available_ || root != root_ || rows.size() != originals_.size())
            return failure(message,QStringLiteral("盘面批号尚未建立或原孔集合已变化，请重读并核对来源。"));
        QSet<qint64> ids;
        for (const auto &row : rows)
        {
            const qint64 id = row.value(QStringLiteral("id")).toLongLong();
            if (ids.contains(id) || !originals_.contains(id) || !sameIdentity(originals_.value(id),identity(row)))
                return failure(message,QStringLiteral("原孔位资料与当前盘面批号来源不同，未用普通重读覆盖运行批号。"));
            ids.insert(id);
        }
        View read;
        read.instance = instance_; read.root = root_; read.panel = panel_; read.revision = revision_;
        read.originals = originals_; read.batches = batches_; read.available = true;
        *result = read;
        return true;
    }

    bool current(const View &view, QString *message) const
    {
        QMutexLocker guard(&mutex_);
        return matches(view) || failure(message,QStringLiteral("盘面批号已经由较新操作覆盖，未采用旧任务视图。"));
    }

    // 真实Tt的CAL setter事实；预检查只用View私有副本，不调用此方法。
    bool publishDefaults(View *result, QString *message)
    {
        if (!result) return failure(message,QStringLiteral("CAL盘面批号事实为空。"));
        const View &view = *result;
        QMutexLocker guard(&mutex_);
        if (!matches(view)) return failure(message,QStringLiteral("CAL遍历期间盘面批号已变化，未覆盖较新的来源。"));
        for (qint64 id : view.defaulted)
            if (!batches_.contains(id)) return failure(message,QStringLiteral("CAL批号事实包含未知原孔号。"));
        for (auto it = batches_.cbegin(); it != batches_.cend(); ++it)
        {
            const bool changed = view.defaulted.contains(it.key());
            if (view.batches.value(it.key()) != (changed ? QStringLiteral("Default") : it.value()) ||
                (changed && !it.value().isEmpty()))
                return failure(message,QStringLiteral("CAL批号事实不是原空批号到Default的变化。"));
        }
        if (!view.defaulted.isEmpty())
        {
            if (revision_ == std::numeric_limits<quint64>::max())
                return reject(message,QStringLiteral("盘面批号修订号已耗尽，未发布CAL变化。"));
            batches_ = view.batches; ++revision_;
        }
        result->revision = revision_;
        result->defaulted.clear();
        return true;
    }

    bool batch(const QString &root, const QVariantMap &original, QString *result) const
    {
        QMutexLocker guard(&mutex_);
        const qint64 id = original.value(QStringLiteral("id")).toLongLong();
        if (!result || !available_ || root != root_ || !originals_.contains(id) ||
            !sameIdentity(originals_.value(id),identity(original))) return false;
        *result = batches_.value(id);
        return true;
    }

private:
    static QVariantMap identity(const QVariantMap &row)
    {
        QVariantMap result;
        for (const auto &key : {"id","hole_name","area","reagent_type","reagent_name","reagent_batch","bottle_type"})
            if (row.contains(QLatin1String(key))) result.insert(QLatin1String(key),row.value(QLatin1String(key)));
        return result;
    }
    static bool sameIdentity(const QVariantMap &first, const QVariantMap &second)
    {
        if (first.keys() != second.keys()) return false;
        for (auto it = first.cbegin(); it != first.cend(); ++it)
        {
            const auto value = second.value(it.key());
            if (it.value().userType() != value.userType() || it.value().isNull() != value.isNull() || it.value() != value) return false;
        }
        return true;
    }
    bool matches(const View &view) const
    {
        if (!available_ || !view.available || view.instance != instance_ || view.root != root_ ||
            view.panel != panel_ || view.revision != revision_ || view.originals.keys() != originals_.keys() ||
            view.batches.keys() != batches_.keys()) return false;
        for (auto it = originals_.cbegin(); it != originals_.cend(); ++it)
            if (!sameIdentity(it.value(),view.originals.value(it.key()))) return false;
        return true;
    }
    static bool failure(QString *message, const QString &reason)
    { if (message) *message = reason; return false; }
    bool reject(QString *message, const QString &reason)
    { available_ = false; return failure(message,reason); }
    mutable QMutex mutex_;
    const QString instance_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString root_;
    quint64 panel_ = 0, revision_ = 0;
    bool available_ = false;
    QMap<qint64,QVariantMap> originals_;
    QMap<qint64,QString> batches_;
};
#endif
