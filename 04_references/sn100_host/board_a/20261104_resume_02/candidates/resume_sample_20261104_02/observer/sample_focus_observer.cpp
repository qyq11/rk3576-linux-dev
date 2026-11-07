// 只在获准的板A联机验证时加载。只观察容器菜单和原弹窗，不注入输入、身份或退出请求。
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFocusEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QPointer>
#include <QTableWidget>
#include <QTimer>
#include <QDebug>

namespace
{
QJsonObject describe(QWidget *widget)
{
    if (!widget) return QJsonObject();
    const QRect r = widget->geometry();
    QJsonObject result{
        {"id", QString::number(reinterpret_cast<quintptr>(widget), 16)},
        {"class", QString::fromLatin1(widget->metaObject()->className())},
        {"visible", widget->isVisible()}, {"enabled", widget->isEnabled()},
        {"window", widget->isWindow()}, {"window_type", int(widget->windowType())},
        {"x", r.x()}, {"y", r.y()}, {"width", r.width()}, {"height", r.height()}};
    const QString name = widget->objectName();
    if (name == QStringLiteral("tblSetting") || name == QStringLiteral("sampleDiscardDialog") ||
        name == QStringLiteral("wgtSampleSetting") || name == QStringLiteral("shellViewport"))
        result.insert("name", name);
    return result; // 不输出编辑内容、账号、密码、样本号或任意按钮文字。
}
bool belongsToContainer(QWidget *widget)
{
    for (auto *p = widget; p; p = p->parentWidget())
        if (QString::fromLatin1(p->metaObject()->className()) == QStringLiteral("CusCombox_Base"))
            return true;
    return false;
}
class ContainerObserver : public QObject
{
    QFile output_;
    QElapsedTimer elapsed_;
    QTimer timer_;
    int lines_ = 0;
    bool active_ = false;
    QByteArray previousState_;

    void write(QJsonObject object)
    {
        if (!active_) return;
        object.insert("ms", elapsed_.elapsed());
        const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if (output_.write(bytes) != bytes.size()) { stop("write_failed"); return; }
        output_.flush();
        if (++lines_ >= 4000) stop("diagnostic_limit");
    }
    void stop(const char *reason)
    {
        if (!active_) return;
        QJsonObject end{{"type", "observer_stopped"}, {"reason", QString::fromLatin1(reason)},
                        {"ms", elapsed_.elapsed()}, {"application_exit_requested", false}};
        output_.write(QJsonDocument(end).toJson(QJsonDocument::Compact) + '\n');
        active_ = false;
        timer_.stop();
        qApp->removeEventFilter(this);
        output_.close(); // 只关闭观察文件，不结束程序或改变业务状态。
    }
    void state()
    {
        QJsonObject current{{"type", "window_state"},
            {"popup", describe(QApplication::activePopupWidget())},
            {"modal", describe(QApplication::activeModalWidget())},
            {"focus", describe(QApplication::focusWidget())},
            {"mouse_grabber", describe(QWidget::mouseGrabber())},
            {"keyboard_grabber", describe(QWidget::keyboardGrabber())}};
        const QByteArray fingerprint = QJsonDocument(current).toJson(QJsonDocument::Compact);
        if (fingerprint != previousState_) { previousState_ = fingerprint; write(current); }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!active_ || !widget) return false;
        const auto type = event->type();
        if (type != QEvent::Show && type != QEvent::Hide && type != QEvent::Close &&
            type != QEvent::FocusIn && type != QEvent::FocusOut &&
            type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease &&
            type != QEvent::KeyPress) return false;
        const bool relevant = belongsToContainer(widget) ||
            widget->objectName() == QStringLiteral("sampleDiscardDialog") ||
            QString::fromLatin1(widget->metaObject()->className()) == QStringLiteral("MaskDlgLogin");
        if (!relevant) return false;
        QJsonObject item{{"type", "widget_event"}, {"event", int(type)}, {"widget", describe(widget)}};
        if (type == QEvent::FocusIn || type == QEvent::FocusOut)
            item.insert("focus_reason", int(static_cast<QFocusEvent *>(event)->reason()));
        if (type == QEvent::KeyPress)
            item.insert("escape", static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape);
        write(item);
        return false; // 原事件继续交给产品；观察器不接管或补发任何操作。
    }

public:
    explicit ContainerObserver(QObject *parent) : QObject(parent)
    {
        const QString output = qEnvironmentVariable("SN100_SAMPLE_FOCUS_OBSERVER_OUTPUT");
        const QString prefix = QStringLiteral(
            "/home/rkshare/programs/sn100_shell/verification/resume_sample_20261104_02/");
        if (!output.startsWith(prefix) || !output.endsWith(QStringLiteral(".jsonl")) ||
            output.mid(prefix.size()).contains('/') || output.contains(QStringLiteral(".."))) return;
        output_.setFileName(output);
        if (!output_.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        {
            qWarning() << "SAMPLE_FOCUS_OBSERVER_DISABLED output unavailable";
            return;
        }
        elapsed_.start();
        active_ = true;
        write(QJsonObject{{"type", "observer_started"}, {"qt", QString::fromLatin1(qVersion())},
            {"platform", QGuiApplication::platformName()}, {"input_injected", false}});
        if (!active_) return;
        qApp->installEventFilter(this);
        connect(&timer_, &QTimer::timeout, this, [this]() { state(); });
        timer_.start(250);
        connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { stop("normal_about_to_quit"); });
        QTimer::singleShot(300000, this, [this]() { stop("observation_time_limit"); });
        qInfo() << "SAMPLE_FOCUS_OBSERVER_READY";
        state();
    }
};
void scheduleContainerObserver()
{
#if defined(__aarch64__) && defined(__linux__)
    if (qEnvironmentVariableIsEmpty("SN100_SAMPLE_FOCUS_OBSERVER_OUTPUT")) return;
    QTimer::singleShot(0, qApp, []() { new ContainerObserver(qApp); });
#endif
}
}
Q_COREAPP_STARTUP_FUNCTION(scheduleContainerObserver)
