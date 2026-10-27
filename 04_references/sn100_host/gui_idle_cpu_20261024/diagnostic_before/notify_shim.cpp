#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

namespace
{
double nowSeconds()
{
    timespec value{};
    clock_gettime(CLOCK_MONOTONIC, &value);
    return value.tv_sec + value.tv_nsec / 1e9;
}
const double started = nowSeconds();
const bool smokeMode = std::getenv("SN100_NOTIFY_SMOKE") != nullptr;
std::atomic<bool> scheduled{false};
std::mutex statsMutex;
struct Count
{
    qint64 events = 0, materialPaints = 0, zeroHaloOpacity = 0, zeroHaloSize = 0, disabledPaints = 0;
};
std::map<std::string, Count> counts;
qint64 overflow = 0;
QPointer<QWidget> window;
bool navigationClicked = false;

QString outputPrefix()
{
    return qEnvironmentVariable("SN100_NOTIFY_DIAG_DIR") + "/notify_" + QString::number(getpid());
}
void finish()
{
    QJsonArray rows;
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        for (const auto &entry : counts)
        {
            rows.append(QJsonObject{{"key_class_object_event_tid", QString::fromStdString(entry.first)},
                                    {"events", double(entry.second.events)},
                                    {"material_paints", double(entry.second.materialPaints)},
                                    {"zero_halo_opacity_paints", double(entry.second.zeroHaloOpacity)},
                                    {"zero_halo_size_paints", double(entry.second.zeroHaloSize)},
                                    {"disabled_material_paints", double(entry.second.disabledPaints)}});
        }
    }
    int page = -1;
    bool imageSaved = false;
    if (window)
    {
        if (auto *stack = window->findChild<QStackedWidget *>("stkCenterAdptive"))
            page = stack->currentIndex();
        imageSaved = window->grab().save(outputPrefix() + ".png");
    }
    QJsonObject result{{"schema", "sn100-notify-shim-diagnostic-v1"},
                       {"pid", getpid()},
                       {"observed_seconds", nowSeconds() - started},
                       {"count_window_seconds", smokeMode ? QJsonArray{0.1, 1} : QJsonArray{10, 20}},
                       {"navigation_click_scheduled_seconds", smokeMode ? 0.5 : 6},
                       {"original_navigation_clicked", navigationClicked},
                       {"current_main_page", page},
                       {"screenshot_saved", imageSaved},
                       {"overflow_events", double(overflow)},
                       {"cpu_baseline", false},
                       {"scope", "LD_PRELOAD constructor plus public global QObject eventFilter on GUI thread; diagnostic overhead included"},
                       {"rows", rows}};
    QFile file(outputPrefix() + ".json");
    if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        file.write(QJsonDocument(result).toJson());
}
void drive(QApplication *app)
{
    if (!app || QThread::currentThread() != app->thread() || scheduled.exchange(true))
        return;
    QTimer::singleShot(smokeMode ? 500 : 6000, app, []()
    {
        for (QWidget *candidate : QApplication::topLevelWidgets())
        {
            const auto buttons = candidate->findChildren<QAbstractButton *>("btnDivStatusAdptive");
            for (auto *button : buttons)
            {
                if (!candidate->isVisible() || !button->isVisible() || !button->isEnabled())
                    continue;
                window = candidate;
                button->click();
                navigationClicked = true;
                return;
            }
        }
    });
    QTimer::singleShot(smokeMode ? 1100 : 20000, app, []() { finish(); });
}
void count(QObject *receiver, QEvent *event)
{
    const double age = nowSeconds() - started;
    if (age < (smokeMode ? 0.1 : 10) || age >= (smokeMode ? 1 : 20) || !receiver || !event ||
        (event->type() != QEvent::Paint && event->type() != QEvent::Timer))
        return;
    const QString key = QString::fromLatin1(receiver->metaObject()->className()) + "|" + receiver->objectName() +
                        "|" + (event->type() == QEvent::Paint ? "Paint|" : "Timer|") +
                        QString::number(syscall(SYS_gettid));
    bool material = event->type() == QEvent::Paint && receiver->inherits("QtMaterialFlatButton");
    qreal opacity = -1, size = -1;
    bool disabled = false;
    if (material)
    {
        // Paint dispatch is on the GUI thread. Read only numeric state properties, never text/data.
        for (QObject *child : receiver->children())
        {
            if (child->inherits("QtMaterialFlatButtonStateMachine"))
            {
                opacity = child->property("haloOpacity").toDouble();
                size = child->property("haloSize").toDouble();
                break;
            }
        }
        if (auto *widget = qobject_cast<QWidget *>(receiver))
            disabled = !widget->isEnabled();
    }
    std::lock_guard<std::mutex> lock(statsMutex);
    const std::string mapKey = key.toStdString();
    if (counts.size() >= 2048 && counts.find(mapKey) == counts.end())
    {
        ++overflow;
        return;
    }
    auto &value = counts[mapKey];
    ++value.events;
    if (material)
    {
        ++value.materialPaints;
        if (std::abs(opacity) < 1e-12) ++value.zeroHaloOpacity;
        if (std::abs(size) < 1e-12) ++value.zeroHaloSize;
        if (disabled) ++value.disabledPaints;
    }
}
class DiagnosticFilter : public QObject
{
public:
    explicit DiagnosticFilter(QApplication *app) : QObject(app) {}
    bool eventFilter(QObject *receiver, QEvent *event) override
    {
        count(receiver, event);
        return false;
    }
};
void install(QApplication *app)
{
    app->installEventFilter(new DiagnosticFilter(app));
    drive(app);
}
}

extern "C" void applicationConstructorC1(QApplication *, int &, char **, int)
    asm("_ZN12QApplicationC1ERiPPci");
extern "C" void applicationConstructorC1(QApplication *app, int &argc, char **argv, int flags)
{
    using Original = void (*)(QApplication *, int &, char **, int);
    static Original original = reinterpret_cast<Original>(dlsym(RTLD_NEXT, "_ZN12QApplicationC1ERiPPci"));
    if (!original)
        _exit(125);
    original(app, argc, argv, flags);
    install(app);
}
extern "C" void applicationConstructorC2(QApplication *, int &, char **, int)
    asm("_ZN12QApplicationC2ERiPPci");
extern "C" void applicationConstructorC2(QApplication *app, int &argc, char **argv, int flags)
{
    using Original = void (*)(QApplication *, int &, char **, int);
    static Original original = reinterpret_cast<Original>(dlsym(RTLD_NEXT, "_ZN12QApplicationC2ERiPPci"));
    if (!original)
        _exit(125);
    original(app, argc, argv, flags);
    install(app);
}
