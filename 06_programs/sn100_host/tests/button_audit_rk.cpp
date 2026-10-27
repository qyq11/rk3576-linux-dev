// 在RK加载到指定版本的正式程序，记录按钮状态及鼠标事件结果。
// 只使用独立匿名测试目录；弹窗自动取消，不用于证明保存、检测或出纸已通过。
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <dlfcn.h>
#include <algorithm>

namespace {
QString objectPath(QObject *object)
{
    QStringList parts;
    for (QObject *p = object; p && !qobject_cast<QApplication *>(p); p = p->parent())
        parts.prepend(QString::fromLatin1(p->metaObject()->className()) + QLatin1Char(':') + p->objectName());
    return parts.join(QLatin1Char('/'));
}
bool buttonLike(QWidget *widget)
{
    return qobject_cast<QAbstractButton *>(widget) ||
           widget->metaObject()->indexOfSignal("clicked()") >= 0 || widget->metaObject()->indexOfSignal("signalClicked(bool)") >= 0;
}
QString buttonText(QWidget *widget)
{
    if (auto *button = qobject_cast<QAbstractButton *>(widget)) return button->text();
    return widget->accessibleName();
}
class Inspector : public QObject
{
public:
    static int clickedReceivers(QWidget *widget)
    {
        // QObject公开元信息没有接线数量接口，仅用于只读观测，不改接线。
        const auto count = &Inspector::receivers;
        return (widget->*count)(
            qobject_cast<QAbstractButton *>(widget) ? SIGNAL(clicked(bool)) : SIGNAL(clicked()));
    }
};
class Audit : public QObject
{
    QFile output;
    QPointer<QWidget> shell;
    QTimer modalTimer;
    QElapsedTimer clock;
    QSet<QString> clicked;
    QSet<QWidget *> observedDialogs;
    QSet<QString> dialogClicks;
    QSet<QString> menuChoices;
    QSet<QString> finishedMenus;
    QStringList menus{QStringLiteral("btnReagentAdptive"), QStringLiteral("btnSampleAdptive"),
        QStringLiteral("btnReportAdptive"), QStringLiteral("btnCalibrateAdptive"),
        QStringLiteral("btnQualityAdptive"), QStringLiteral("btnSystemAdptive"),
        QStringLiteral("btnUserSettingAdptive"), QStringLiteral("btnDivStatusAdptive")};
    int page = -1, steps = 0;
    bool performing = false;
    bool finishing = false;
    bool confirmExit = false;
    QString activeAction;

    void emitJson(QJsonObject row)
    {
        row.insert(QStringLiteral("elapsed_ms"), clock.elapsed());
        output.write(QJsonDocument(row).toJson(QJsonDocument::Compact) + '\n');
        output.flush();
    }
    QJsonObject state(QWidget *widget)
    {
        const QPoint point = widget->mapToGlobal(widget->rect().center());
        return {{"path",objectPath(widget)}, {"object",widget->objectName()},
                {"class",QString::fromLatin1(widget->metaObject()->className())},
                {"text",buttonText(widget)}, {"tooltip",widget->toolTip()},
                {"enabled",widget->isEnabled()}, {"visible",widget->isVisible()},
                {"clicked_receivers",Inspector::clickedReceivers(widget)},
                {"x",point.x()}, {"y",point.y()}, {"width",widget->width()}, {"height",widget->height()}};
    }
    void snapshot(QWidget *root, const QString &context)
    {
        for (auto *widget : root->findChildren<QWidget *>())
        {
            if (!buttonLike(widget)) continue;
            QJsonObject row = state(widget);
            row.insert("type","button"); row.insert("context",context);
            emitJson(row);
        }
    }
    void mouse(QWidget *widget)
    {
        for (auto *parent=widget->parentWidget(); parent; parent=parent->parentWidget())
        {
            if (auto *scroll=qobject_cast<QScrollArea *>(parent)) scroll->ensureWidgetVisible(widget,8,8);
            if (parent->isWindow()) break;
        }
        const QPointF local = widget->rect().center();
        QMouseEvent press(QEvent::MouseButtonPress,local,widget->mapToGlobal(local.toPoint()),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease,local,widget->mapToGlobal(local.toPoint()),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(widget,&press);
        QApplication::sendEvent(widget,&release);
    }
    void dismissDialogs()
    {
        for (auto *window : QApplication::topLevelWidgets())
        {
            if (!window->isVisible() || window == shell || window->objectName()==QStringLiteral("shellViewport")) continue;
            if (auto *menu=qobject_cast<QMenu *>(window))
            {
                QJsonArray actions;
                for (auto *action:menu->actions()) actions.append(QJsonObject{{"text",action->text()},{"enabled",action->isEnabled()},{"object",action->objectName()}});
                if (!observedDialogs.contains(window)) emitJson({{"type","menu"},{"action",activeAction},{"actions",actions}});
                observedDialogs.insert(window);
                for(auto *action:menu->actions())
                {
                    const QString key=activeAction+QLatin1Char(':')+action->objectName()+action->text();
                    if(!action->isEnabled() || action->isSeparator() || menuChoices.contains(key))continue;
                    menuChoices.insert(key);
                    QPointer<QMenu> safeMenu(menu);
                    const QRect rect=menu->actionGeometry(action);
                    emitJson({{"type","menu_choice"},{"action",activeAction},{"text",action->text()},{"object",action->objectName()}});
                    QTimer::singleShot(0,this,[safeMenu,rect]()
                    {
                        if(!safeMenu || !safeMenu->isVisible())return;
                        const QPointF point=rect.center();
                        QMouseEvent press(QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                        QMouseEvent release(QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                        QApplication::sendEvent(safeMenu,&press);QApplication::sendEvent(safeMenu,&release);
                    });
                    return;
                }
                finishedMenus.insert(activeAction);
                QTimer::singleShot(550,menu,&QMenu::close);
                continue;
            }
            auto *dialog=qobject_cast<QDialog *>(window);
            if (!dialog) continue;
            if (auto *active=QApplication::activeModalWidget()) if (active!=dialog) continue;
            if(!observedDialogs.contains(dialog))
            {
                observedDialogs.insert(dialog);
                connect(dialog,&QObject::destroyed,this,[this,dialog](){observedDialogs.remove(dialog);});
                emitJson({{"type","dialog"},{"action",activeAction},{"class",QString::fromLatin1(dialog->metaObject()->className())},{"object",dialog->objectName()}});
                snapshot(dialog,QStringLiteral("dialog:")+activeAction);
            }
            const bool customQuestion=QString::fromLatin1(dialog->metaObject()->className())==QStringLiteral("CusMessageBox");
            if(!qobject_cast<QMessageBox *>(dialog) && !qobject_cast<QFileDialog *>(dialog) && !customQuestion)
            {
                for(auto *button:dialog->findChildren<QAbstractButton *>())
                {
                    const QString name=button->objectName();
                    const QString key=objectPath(button);
                    if(!button->isEnabled() || !button->isVisible() || name.isEmpty() || name.startsWith(QStringLiteral("qt_")) ||
                       name.contains(QStringLiteral("Close"),Qt::CaseInsensitive) || name.contains(QStringLiteral("Cancel"),Qt::CaseInsensitive) ||
                       name==QStringLiteral("btnShutDown") || dialogClicks.contains(key))continue;
                    dialogClicks.insert(key);
                    QPointer<QAbstractButton> actionButton(button);
                    QTimer::singleShot(0,this,[this,actionButton,key]()
                    {
                        if(!actionButton || !actionButton->isVisible() || !actionButton->isEnabled())return;
                        QJsonObject row=state(actionButton);row.insert("type","dialog_click_before");emitJson(row);
                        mouse(actionButton);
                        emitJson({{"type","dialog_click_returned"},{"action",key}});
                    });
                    return;
                }
            }
            QPointer<QDialog> safe(dialog);
            QTimer::singleShot(650,this,[this,safe]()
            {
                if (!safe || !safe->isVisible()) return;
                if(confirmExit && QString::fromLatin1(safe->metaObject()->className())==QStringLiteral("MaskDlgShutDown"))
                {
                    auto *button=safe->findChild<QAbstractButton *>(QStringLiteral("btnShutDown"));
                    if(button && button->isEnabled())
                    {
                        emitJson({{"type","shutdown_confirm_mouse"},{"path",objectPath(button)}});
                        confirmExit=false;mouse(button);return;
                    }
                }
                if (auto *box=qobject_cast<QMessageBox *>(safe.data()))
                {
                    const bool discard=box->objectName().contains(QStringLiteral("discard"),Qt::CaseInsensitive) ||
                        box->text().contains(QStringLiteral("放弃"));
                    auto *button=box->button(discard ? QMessageBox::Yes : QMessageBox::No);
                    if (!button) button=box->button(QMessageBox::Cancel);
                    if (!button) button=box->button(QMessageBox::Ok);
                    if (button) { mouse(button); return; }
                }
                const QStringList cancelNames{QStringLiteral("btnClose"),QStringLiteral("btnCancel"),QStringLiteral("btn_cancel"),QStringLiteral("btn_cancel_modify")};
                for (const auto &name:cancelNames)
                {
                    auto *button=safe->findChild<QAbstractButton *>(name);
                    if (button && button->isVisible() && button->isEnabled()) { mouse(button); return; }
                }
                safe->QDialog::reject();
            });
        }
    }
    bool isExcluded(QWidget *widget) const
    {
        const QString name=widget->objectName();
        return menus.contains(name) || name==QStringLiteral("btnLoginOut") || name==QStringLiteral("btnUser") ||
            name==QStringLiteral("btnShutDown") || name==QStringLiteral("btnStart") || name==QStringLiteral("btnPause") ||
            name.isEmpty() || name.startsWith(QStringLiteral("qt_"));
    }
    void next()
    {
        if (!shell || ++steps>750 || clock.elapsed()>420000) { finish(); return; }
        if (QApplication::activeModalWidget()) { QTimer::singleShot(300,this,[this](){next();}); return; }
        for(auto *window:QApplication::topLevelWidgets())
            if(window->isVisible() && qobject_cast<QDialog *>(window)) {QTimer::singleShot(300,this,[this](){next();});return;}
        if (page<0 || page>=menus.size())
        {
            if (page>=menus.size()) { finish(); return; }
            page=0; selectPage(); return;
        }
        const QString context=menus[page];
        for (auto *widget:shell->findChildren<QWidget *>())
        {
            if (!buttonLike(widget) || isExcluded(widget) || !widget->isVisible() || !widget->isEnabled()) continue;
            const QString key=context+objectPath(widget);
            if (clicked.contains(key) && !(widget->objectName()==QStringLiteral("btnChange") && !finishedMenus.contains(key))) continue;
            clicked.insert(key); activeAction=key;
            QJsonObject row=state(widget);row.insert("type","click_before");row.insert("context",context);emitJson(row);
            QPointer<QWidget> safe(widget);
            int clickedCount=0;
            QMetaObject::Connection connection;
            if(auto *button=qobject_cast<QAbstractButton *>(widget))
                connection=connect(button,&QAbstractButton::clicked,this,[&clickedCount](){++clickedCount;});
            mouse(widget);
            QObject::disconnect(connection);
            emitJson({{"type","click_returned"},{"action",key},{"clicked_signals",clickedCount},{"widget_survived",!safe.isNull()}});
            QTimer::singleShot(300,this,[this](){snapshot(shell,menus[page]);next();});return;
        }
        // 遍历可见选项卡，覆盖原界面的次级页面，不强制解禁控件。
        for (auto *tabs:shell->findChildren<QTabWidget *>())
        {
            if (!tabs->isVisible()) continue;
            for (int i=0;i<tabs->count();++i)
            {
                const QString key=context+objectPath(tabs)+QString::number(i);
                if (!tabs->isTabEnabled(i) || clicked.contains(key)) continue;
                clicked.insert(key);
                auto *bar=tabs->findChild<QTabBar *>();
                if (bar) { const QPoint center=bar->tabRect(i).center();
                    QMouseEvent press(QEvent::MouseButtonPress,center,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                    QMouseEvent release(QEvent::MouseButtonRelease,center,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                    QApplication::sendEvent(bar,&press);QApplication::sendEvent(bar,&release); }
                emitJson({{"type","tab"},{"context",context},{"object",tabs->objectName()},{"index",i},{"text",tabs->tabText(i)}});
                snapshot(shell,context+QStringLiteral(":tab:")+tabs->tabText(i));
                QTimer::singleShot(250,this,[this](){next();}); return;
            }
        }
        ++page;selectPage();
    }
    void selectPage()
    {
        if(page>=menus.size()){finish();return;}
        auto *button=shell->findChild<QAbstractButton *>(menus[page]);
        emitJson({{"type","navigation"},{"context",menus[page]},{"enabled",button && button->isEnabled()}});
        if(button && button->isEnabled())mouse(button);
        QTimer::singleShot(1000,this,[this](){
            shell->grab().save(QFileInfo(output.fileName()).dir().filePath(QStringLiteral("page_%1.png").arg(page)));
            snapshot(shell,menus[page]);next();});
    }
    void finish()
    {
        if(finishing)return;
        finishing=true;
        emitJson({{"type","audit_complete"},{"pages",page},{"action_keys",clicked.size()},{"scope","RK鼠标事件入口与界面响应；弹窗取消，不代表业务保存/真实仪器通过"}});
        auto *exitButton=shell ? shell->findChild<QWidget *>(QStringLiteral("btnShutDown")) : nullptr;
        if(exitButton && exitButton->isEnabled())
        {
            emitJson({{"type","shutdown_cancel_mouse"},{"path",objectPath(exitButton)}});mouse(exitButton);
            QTimer::singleShot(1500,this,[this]()
            {
                emitJson({{"type","shutdown_cancel_result"},{"shell_visible",shell && shell->isVisible()}});
                confirmExit=true;
                if(shell)if(auto *button=shell->findChild<QWidget *>(QStringLiteral("btnShutDown")))mouse(button);
            });
        }
        else
        {
            emitJson({{"type","shutdown_button_unavailable"}});
            for(auto *w:QApplication::topLevelWidgets())if(w->objectName()==QStringLiteral("shellViewport"))w->close();
        }
        QTimer::singleShot(14000,this,[this](){emitJson({{"type","cleanup_event_loop_exit"},{"reason","正常关闭未在14秒内完成，匿名测试清理退出；此项不得算关闭按钮通过"}});QCoreApplication::exit(94);});
    }
public:
    explicit Audit(QObject *parent):QObject(parent)
    {
        clock.start();
        output.setFileName(qEnvironmentVariable("SN100_BUTTON_AUDIT_OUTPUT"));
        if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return;
        for(auto *widget:QApplication::allWidgets())
            if(QString::fromLatin1(widget->metaObject()->className())==QStringLiteral("frmMain")){shell=widget;break;}
        emitJson({{"type","start"},{"shell_found",!shell.isNull()},{"platform",QGuiApplication::platformName()},{"qt",QString::fromLatin1(qVersion())}});
        if(!shell){QCoreApplication::exit(92);return;}
        connect(&modalTimer,&QTimer::timeout,this,[this](){dismissDialogs();});modalTimer.start(80);
        snapshot(shell,QStringLiteral("startup_all_pages"));
        QTimer::singleShot(1000,this,[this](){next();});
    }
};
}
extern "C" int sn100AuditExec() asm("_ZN16QCoreApplication4execEv");
extern "C" int sn100AuditExec()
{
#if !defined(__aarch64__) || !defined(__linux__)
    return 91;
#endif
    using Exec=int(*)();
    auto original=reinterpret_cast<Exec>(dlsym(RTLD_NEXT,"_ZN16QCoreApplication4execEv"));
    const QString path=qEnvironmentVariable("SN100_BUTTON_AUDIT_OUTPUT");
    if(!original || !path.startsWith(QStringLiteral("/home/rkshare/programs/sn100_shell/verification/button_audit_")))return 93;
    QTimer::singleShot(9000,qApp,[](){new Audit(qApp);});
    return original();
}
