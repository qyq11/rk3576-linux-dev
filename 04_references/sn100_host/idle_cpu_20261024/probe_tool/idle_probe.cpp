// Host-only, bounded passive Qt observation; never omit or replace an event/update.
#include <QApplication>
#include <QAbstractAnimation>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaProperty>
#include <QSequentialAnimationGroup>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <execinfo.h>
#include <map>
#include <unistd.h>

namespace {
thread_local bool insideProbe = false;
struct Guard {
    Guard() { insideProbe = true; }
    ~Guard() { insideProbe = false; }
};
QString address(quintptr value) { return QStringLiteral("0x%1").arg(value, 0, 16); }

QJsonArray stackNow()
{
    void *frames[18];
    const int count = ::backtrace(frames, 18);
    QJsonArray result;
    for (int i = 1; i < count; ++i) {
        Dl_info info = {};
        const bool found = ::dladdr(frames[i], &info);
        QJsonObject frame{{"pc", address(quintptr(frames[i]))}};
        if (found) {
            frame["module"] = QString::fromLocal8Bit(info.dli_fname ? info.dli_fname : "");
            frame["module_offset"] = address(quintptr(frames[i]) - quintptr(info.dli_fbase));
            frame["dynamic_symbol"] = QString::fromLocal8Bit(info.dli_sname ? info.dli_sname : "");
            if (info.dli_saddr) frame["symbol_offset"] = address(quintptr(frames[i]) - quintptr(info.dli_saddr));
        }
        result.append(frame);
    }
    return result;
}

struct ObjectCounts {
    QString className, objectName, parentClass, parentName;
    quintptr pointer = 0;
    quint64 updates = 0, paints = 0, updateRequests = 0, timers = 0;
    quint64 updateState[4] = {}; // index = visible*2 + enabled
    std::map<quintptr, quint64> updateCallsites;
    QJsonArray firstUpdateStack, firstPaintStack, firstTimerStack;
};

class IdleProbe : public QObject {
public:
    explicit IdleProbe(QObject *parent) : QObject(parent), appThread_(parent->thread())
    {
        setObjectName(QStringLiteral("SN100_IDLE_PROBE"));
        clock_.start();
        auto *begin = new QTimer(this);
        auto *end = new QTimer(this);
        begin->setObjectName(QStringLiteral("SN100_IDLE_PROBE_BEGIN"));
        end->setObjectName(QStringLiteral("SN100_IDLE_PROBE_END"));
        begin->setSingleShot(true); end->setSingleShot(true);
        begin->setTimerType(Qt::PreciseTimer); end->setTimerType(Qt::PreciseTimer);
        connect(begin, &QTimer::timeout, this, [this]() {
            Guard guard;
            snapshotBegin_ = materialSnapshot(); beginTaken_ = true;
            beginElapsedMs_ = clock_.elapsed();
        });
        connect(end, &QTimer::timeout, this, [this]() {
            Guard guard;
            snapshotEnd_ = materialSnapshot(); endTaken_ = true;
            endElapsedMs_ = clock_.elapsed();
        });
        begin->start(6000); end->start(10000);
    }

    void updateCalled(QWidget *widget, quintptr caller)
    {
        if (QThread::currentThread() != appThread_) { ++foreignThreadUpdates_; return; }
        if (!inWindow()) return;
        Guard guard;
        ObjectCounts *entry = countsFor(widget);
        if (!entry) return;
        ++entry->updates;
        ++entry->updateState[(widget->isVisible() ? 2 : 0) + (widget->isEnabled() ? 1 : 0)];
        auto at = entry->updateCallsites.find(caller);
        if (at != entry->updateCallsites.end()) ++at->second;
        else if (entry->updateCallsites.size() < 16) entry->updateCallsites[caller] = 1;
        else ++droppedCallsites_;
        if (entry->firstUpdateStack.isEmpty() &&
            (updateStacks_ < 20 || (entry->objectName == QStringLiteral("btnLoginOut") && updateStacks_ < 21))) {
            entry->firstUpdateStack = stackNow(); ++updateStacks_;
        }
    }

    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (insideProbe || QThread::currentThread() != appThread_ || !inWindow()) return false;
        const QEvent::Type type = event->type();
        if (type != QEvent::Paint && type != QEvent::UpdateRequest && type != QEvent::Timer) return false;
        if (object->objectName().startsWith(QStringLiteral("SN100_IDLE_PROBE"))) return false;
        Guard guard;
        ObjectCounts *entry = countsFor(object);
        if (!entry) return false;
        if (type == QEvent::Paint) {
            ++entry->paints;
            if (entry->firstPaintStack.isEmpty() && paintStacks_ < 12) {
                entry->firstPaintStack = stackNow(); ++paintStacks_;
            }
        } else if (type == QEvent::UpdateRequest) ++entry->updateRequests;
        else {
            ++entry->timers;
            if (entry->firstTimerStack.isEmpty() && timerStacks_ < 8) {
                entry->firstTimerStack = stackNow(); ++timerStacks_;
            }
        }
        return false; // Passive: Qt always receives the original event.
    }

    void saveReport(int execResult)
    {
        Guard guard;
        QJsonArray objects;
        for (const auto &pair : counts_) {
            const auto &row = pair.second;
            QJsonArray state;
            for (quint64 value : row.updateState) state.append(double(value));
            QJsonArray callsites;
            for (const auto &callsite : row.updateCallsites)
                callsites.append(QJsonObject{{"return_pc", address(callsite.first)}, {"calls", double(callsite.second)}});
            objects.append(QJsonObject{{"address", address(row.pointer)}, {"class_name", row.className},
                {"object_name", row.objectName}, {"parent_class", row.parentClass}, {"parent_name", row.parentName},
                {"update_calls", double(row.updates)}, {"paint_events", double(row.paints)},
                {"update_request_events", double(row.updateRequests)}, {"timer_events", double(row.timers)},
                {"update_states_hidden_disabled_hidden_enabled_visible_disabled_visible_enabled", state},
                {"update_callsites", callsites}, {"first_update_stack", row.firstUpdateStack},
                {"first_paint_stack", row.firstPaintStack}, {"first_timer_stack", row.firstTimerStack}});
        }
        QJsonObject result{{"schema", "sn100-passive-qt-idle-probe-v1"}, {"qt_version", qVersion()},
            {"pid", double(::getpid())}, {"application_exec_result", execResult},
            {"window_clock", "QElapsedTimer from entry to intercepted QApplication::exec; not process launch time"},
            {"requested_start_ms", 6000}, {"requested_end_ms", 10000},
            {"snapshot_begin_elapsed_ms", double(beginElapsedMs_)}, {"snapshot_end_elapsed_ms", double(endElapsedMs_)},
            {"observation_complete", beginTaken_ && endTaken_ && clock_.elapsed() >= 10000},
            {"exec_elapsed_ms", double(clock_.elapsed())},
            {"foreign_thread_update_calls_ignored", double(foreignThreadUpdates_.load())},
            {"dropped_object_observations", double(droppedObjects_)}, {"dropped_update_callsites", double(droppedCallsites_)},
            {"objects", objects}, {"material_snapshot_begin", snapshotBegin_}, {"material_snapshot_end", snapshotEnd_},
            {"limits", "Passive host-only instrumented run; every original update and event is forwarded. Hooks, map counters, limited backtraces and two snapshots add overhead; not an uninstrumented resource baseline. Paint-event delivery is not a complete raster/GPU time measurement. Object addresses may be reused; no widget text, DB or credential values are read. haloVisible is reported only if a public QObject property exists; otherwise unknown."}};
        const QString path = QString::fromLocal8Bit(qgetenv("SN100_IDLE_PROBE_OUTPUT"));
        if (!QFileInfo(path).isAbsolute()) { std::fprintf(stderr, "SN100_IDLE_PROBE_ERROR output must be absolute\n"); return; }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            std::fprintf(stderr, "SN100_IDLE_PROBE_ERROR cannot create new output\n"); return;
        }
        const QByteArray data = QJsonDocument(result).toJson();
        if (file.write(data) != data.size() || !file.flush()) {
            std::fprintf(stderr, "SN100_IDLE_PROBE_ERROR output write failed\n"); return;
        }
        std::fprintf(stderr, "SN100_IDLE_PROBE_SAVED objects=%zu complete=%d\n", counts_.size(), int(beginTaken_ && endTaken_));
    }

private:
    bool inWindow() const { const qint64 ns = clock_.nsecsElapsed(); return ns >= 6000000000LL && ns < 10000000000LL; }
    ObjectCounts *countsFor(QObject *object)
    {
        auto found = counts_.find(object);
        if (found != counts_.end()) return &found->second;
        if (counts_.size() >= 256) { ++droppedObjects_; return nullptr; }
        ObjectCounts row;
        row.pointer = quintptr(object);
        row.className = QString::fromLatin1(object->metaObject()->className()).left(96);
        row.objectName = object->objectName().left(80);
        if (object->parent()) {
            row.parentClass = QString::fromLatin1(object->parent()->metaObject()->className()).left(96);
            row.parentName = object->parent()->objectName().left(80);
        }
        return &counts_.emplace(object, std::move(row)).first->second;
    }
    QJsonArray materialSnapshot()
    {
        QJsonArray result;
        for (QWidget *widget : QApplication::allWidgets()) {
            if (!widget->inherits("QtMaterialFlatButton")) continue;
            if (result.size() >= 128) break;
            QJsonObject item{{"address", address(quintptr(widget))}, {"object_name", widget->objectName().left(80)},
                {"visible", widget->isVisible()}, {"enabled", widget->isEnabled()}};
            const int index = widget->metaObject()->indexOfProperty("haloVisible");
            item["halo_visible_public_property_available"] = index >= 0;
            if (index >= 0) item["halo_visible"] = QJsonValue::fromVariant(widget->property("haloVisible"));
            else item["halo_visible"] = QJsonValue(QJsonValue::Null);
            QJsonArray machines;
            for (QObject *child : widget->children()) {
                if (!child->inherits("QtMaterialFlatButtonStateMachine")) continue;
                QJsonObject machine{{"class_name", child->metaObject()->className()}};
                for (const char *name : {"haloOpacity", "haloSize", "haloScaleFactor"})
                    machine[QString::fromLatin1(name)] = QJsonValue::fromVariant(child->property(name));
                QJsonArray animations;
                for (QSequentialAnimationGroup *group : child->findChildren<QSequentialAnimationGroup *>())
                    animations.append(QJsonObject{{"loop_count", group->loopCount()}, {"state", int(group->state())},
                        {"current_time_ms", group->currentTime()}, {"current_loop", group->currentLoop()}});
                machine["sequential_animation_groups"] = animations;
                machines.append(machine);
            }
            item["state_machines"] = machines;
            result.append(item);
        }
        return result;
    }
    QThread *const appThread_;
    QElapsedTimer clock_;
    bool beginTaken_ = false, endTaken_ = false;
    qint64 beginElapsedMs_ = -1, endElapsedMs_ = -1;
    std::atomic<quint64> foreignThreadUpdates_{0};
    quint64 droppedObjects_ = 0, droppedCallsites_ = 0;
    int updateStacks_ = 0, paintStacks_ = 0, timerStacks_ = 0;
    std::map<const QObject *, ObjectCounts> counts_;
    QJsonArray snapshotBegin_, snapshotEnd_;
};
std::atomic<IdleProbe *> activeProbe{nullptr};
}

extern "C" void probeWidgetUpdate(QWidget *widget) asm("_ZN7QWidget6updateEv");
extern "C" void probeWidgetUpdate(QWidget *widget)
{
    using Function = void (*)(QWidget *);
    static Function original = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "_ZN7QWidget6updateEv"));
    if (!original) { std::fputs("SN100_IDLE_PROBE_ERROR missing QWidget::update\n", stderr); _exit(127); }
    if (!insideProbe) {
        if (IdleProbe *probe = activeProbe.load(std::memory_order_acquire))
            probe->updateCalled(widget, quintptr(__builtin_return_address(0)));
    }
    original(widget);
}

extern "C" int probeApplicationExec() asm("_ZN12QApplication4execEv");
extern "C" int probeApplicationExec()
{
    using Function = int (*)();
    static Function original = reinterpret_cast<Function>(::dlsym(RTLD_NEXT, "_ZN12QApplication4execEv"));
    if (!original) { std::fputs("SN100_IDLE_PROBE_ERROR missing QApplication::exec\n", stderr); _exit(127); }
    if (!qApp || qEnvironmentVariableIsEmpty("SN100_IDLE_PROBE_OUTPUT")) return original();
    // App ownership keeps the observer alive through later SQL-worker teardown.
    auto *probe = new IdleProbe(qApp);
    qApp->installEventFilter(probe);
    activeProbe.store(probe, std::memory_order_release);
    const int result = original();
    activeProbe.store(nullptr, std::memory_order_release);
    qApp->removeEventFilter(probe);
    probe->saveReport(result);
    return result;
}
