// 在板A原配置和原主控连接中只读观察界面，不代替登录或发送任何按钮动作。
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTimer>
#include <dlfcn.h>

namespace
{
class LiveObserver : public QObject
{
    QFile output_;
    bool sentSeen_ = false, receivedSeen_ = false;
    void write(QJsonObject row)
    {
        row.insert("time", QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
        output_.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
        output_.flush();
    }
    void observeTimestamp(QTextBrowser *view, bool *seen)
    {
        if (*seen || view->document()->isEmpty()) return;
        const QTextBlock block = view->document()->begin();
        const QString text = block.text();
        if (text.size() < 17 || text.at(2) != ':' || text.at(5) != ':') return;
        QTextCursor prefix(view->document());
        prefix.setPosition(block.position());
        prefix.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        QTextCursor body(view->document());
        body.setPosition(block.position() + 12);
        body.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        write({{"kind", "communication_timestamp"}, {"view", view->objectName()},
               {"timestamp_colour", prefix.charFormat().foreground().color().name()},
               {"body_colour", body.charFormat().foreground().color().name()},
               {"timestamp_darkgreen", prefix.charFormat().foreground().color() == QColor("darkgreen")},
               {"has_hex", text.contains(" hex=")}, {"characters", text.size()}});
        *seen = true;
    }
    bool eventFilter(QObject *object, QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::Show: case QEvent::Hide: case QEvent::FocusIn: case QEvent::FocusOut:
        case QEvent::MouseButtonPress: case QEvent::MouseButtonRelease:
            break;
        default: return false;
        }
        bool sampleCombo = false;
        for (QObject *parent = object; parent; parent = parent->parent())
            if (QByteArray(parent->metaObject()->className()) == "CusCombox_Base") sampleCombo = true;
        if (sampleCombo)
        {
            auto *widget = qobject_cast<QWidget *>(object);
            auto *popup = QApplication::activePopupWidget();
            write({{"kind", "container_event"}, {"class", object->metaObject()->className()},
                   {"event", int(event->type())}, {"visible", widget && widget->isVisible()},
                   {"window", widget && widget->isWindow()},
                   {"popup_active", popup != nullptr},
                   {"popup_class", popup ? popup->metaObject()->className() : ""}});
        }
        return false;
    }
public:
    explicit LiveObserver(QObject *parent) : QObject(parent)
    {
        qunsetenv("LD_PRELOAD");
        output_.setFileName(qEnvironmentVariable("SN100_LIVE_UI_OBSERVER"));
        if (!output_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return;
        write({{"kind", "observer_started"}, {"platform", QApplication::platformName()},
               {"scope", "只读通信文字格式和容器焦点事件；不点击、不登录、不改资料"}});
        qApp->installEventFilter(this);
        auto *timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, [this]()
        {
            for (auto *widget : QApplication::allWidgets())
                if (auto *browser = qobject_cast<QTextBrowser *>(widget))
                {
                    if (browser->objectName() == "textBrowser_SendData") observeTimestamp(browser, &sentSeen_);
                    if (browser->objectName() == "textBrowser_ReceiveData") observeTimestamp(browser, &receivedSeen_);
                }
        });
        timer->start(500);
    }
};
}
extern "C" int sn100LiveExec() asm("_ZN16QCoreApplication4execEv");
extern "C" int sn100LiveExec()
{
    using Exec = int (*)();
    auto original = reinterpret_cast<Exec>(dlsym(RTLD_NEXT, "_ZN16QCoreApplication4execEv"));
    const QString path = qEnvironmentVariable("SN100_LIVE_UI_OBSERVER");
    if (original && path.startsWith("/home/rkshare/programs/sn100_shell/verification/resume_20261104_02/"))
        QTimer::singleShot(0, qApp, []() { new LiveObserver(qApp); });
    return original ? original() : 93;
}
