// One bounded diagnostic A/B. No production settings, SQL or device access.
#include "ui/language_setting_page.h"
#include "ui/cusmessagebox.h"
#include <QtTest>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPointer>
#include <QPixmap>
#include <QPushButton>
#include <QRegion>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QWindow>
#include <functional>

namespace
{
QJsonArray rect(QRect r) { return {r.x(),r.y(),r.width(),r.height()}; }
QJsonArray point(QPoint p) { return {p.x(),p.y()}; }
QJsonObject identity(QObject *object)
{
    return object?QJsonObject{{"class",object->metaObject()->className()},{"name",object->objectName()}}:
                  QJsonObject{{"class",QJsonValue::Null},{"name",QJsonValue::Null}};
}
QJsonObject screen(QScreen *value)
{
    if(!value)return {{"present",false}};
    return {{"present",true},{"name",value->name()},{"geometry",rect(value->geometry())},
        {"availableGeometry",rect(value->availableGeometry())},{"virtualGeometry",rect(value->virtualGeometry())},
        {"availableVirtualGeometry",rect(value->availableVirtualGeometry())},{"devicePixelRatio",value->devicePixelRatio()},
        {"logicalDpi",value->logicalDotsPerInch()},{"physicalDpi",value->physicalDotsPerInch()}};
}
QJsonObject safeEnvironment()
{
    QJsonObject result;
    for(const char *name:{"DISPLAY","WAYLAND_DISPLAY","XDG_SESSION_TYPE","XDG_CURRENT_DESKTOP",
        "QT_QPA_PLATFORM","QT_SCALE_FACTOR","QT_AUTO_SCREEN_SCALE_FACTOR","QT_ENABLE_HIGHDPI_SCALING",
        "QT_SCREEN_SCALE_FACTORS","QT_FONT_DPI","QT_DEVICE_PIXEL_RATIO","GDK_SCALE","GDK_DPI_SCALE"})
    {
        const QByteArray value=qgetenv(name);
        result.insert(QString::fromLatin1(name),value.isNull()?QJsonValue(QJsonValue::Null):
            QJsonValue(QString::fromLocal8Bit(value.left(4096))));
    }
    return result;
}
class MouseTrace : public QObject
{
public:
    QJsonArray events;
    QElapsedTimer *clock=nullptr;
protected:
    bool eventFilter(QObject *watched,QEvent *event) override
    {
        if(events.size()<64&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease))
        {
            const auto *mouse=static_cast<QMouseEvent*>(event);
            events.append(QJsonObject{{"target",identity(watched)},{"type",int(event->type())},
                {"local",QJsonArray{mouse->localPos().x(),mouse->localPos().y()}},
                {"window",QJsonArray{mouse->windowPos().x(),mouse->windowPos().y()}},
                {"screen",QJsonArray{mouse->screenPos().x(),mouse->screenPos().y()}},
                {"button",int(mouse->button())},{"spontaneous",mouse->spontaneous()},
                {"monotonic_ms",double(clock->elapsed())}});
        }
        return QObject::eventFilter(watched,event);
    }
};
struct Receipt
{
    QJsonObject value;
    QString path;
    bool *written=nullptr;
    ~Receipt()
    {
        QFile file(path);
        const QByteArray bytes=QJsonDocument(value).toJson(QJsonDocument::Indented);
        *written=file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(bytes)==bytes.size();
    }
};
}

class HitAbProbe : public QObject
{
    Q_OBJECT
    QString evidence_;
    QElapsedTimer clock_;
    bool controlWritten_=false,cnWritten_=false;
    int remaining()const {return qMax(0,24000-int(clock_.elapsed()));}
    bool until(const std::function<bool()> &predicate)
    {
        QElapsedTimer wait;wait.start();const int bound=qMin(5000,remaining());
        while(!predicate()&&wait.elapsed()<bound)QTest::qWait(5);
        return predicate();
    }
    void runArm(bool cn)
    {
        Receipt receipt;
        receipt.path=QDir(evidence_).filePath(cn?"cn.json":"control.json");
        receipt.written=cn?&cnWritten_:&controlWritten_;
        auto &record=receipt.value;
        record={{"arm",cn?"CN":"ordinary_control"},{"outcome","assertion_failed"},
            {"started_monotonic_ms",double(clock_.elapsed())},{"qpa",QGuiApplication::platformName()},
            {"click_injection_attempted",false},{"clicked_count",0},{"mouse_trace",QJsonArray()},
            {"safe_environment",safeEnvironment()},{"screenshot_scope","QWidget::grab content only; not compositor/occlusion/native pointer proof"}};
        QScrollArea host;
        host.setObjectName(cn?"cnProbeHost":"controlProbeHost");
        host.setWindowTitle(cn?"SN100 private hit probe CN":"SN100 private hit probe control");
        host.setFrameShape(QFrame::NoFrame);host.setWidgetResizable(false);
        QWidget *page=nullptr;QPushButton *apply=nullptr;
        if(cn)
        {
            page=new LanguageSettingPage;
            apply=page->findChild<QPushButton*>("btnApply");
        }
        else
        {
            page=new QWidget;page->setObjectName("ordinaryCanvas");page->setFixedSize(1451,853);
            auto *panel=new QWidget(page);panel->setObjectName("widget");panel->setGeometry(390,20,681,821);
            panel->setStyleSheet("QWidget#widget{background-color:transparent;border:1px solid #E4E8EB;border-radius:5px;}");
            apply=new QPushButton("Control",panel);apply->setObjectName("btnApply");apply->setGeometry(500,750,161,61);
            apply->setFocusPolicy(Qt::NoFocus);
            apply->setFont(QFont("Noto Sans CJK SC",13));
            apply->setStyleSheet("QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}");
        }
        host.setWidget(page);host.resize(900,600);host.move(20,20);host.show();
        record.insert("stage","wait_visible");QVERIFY(until([&](){return page->isVisible();}));QVERIFY(apply);
        host.ensureWidgetVisible(apply,12,12);
        QVERIFY(until([&](){return host.horizontalScrollBar()->value()>0&&host.verticalScrollBar()->value()>0;}));
        const QRect buttonRect(apply->mapTo(host.viewport(),QPoint()),apply->size());
        record.insert("button_in_viewport",rect(buttonRect));
        QVERIFY(host.viewport()->rect().contains(buttonRect));QCOMPARE(page->size(),QSize(1451,853));
        host.raise();host.activateWindow();record.insert("stage","wait_exposed_active");
        QVERIFY(QTest::qWaitForWindowExposed(&host,qMin(5000,remaining())));
        QVERIFY(QTest::qWaitForWindowActive(&host,qMin(5000,remaining())));
        QScreen *actualScreen=host.windowHandle()?host.windowHandle()->screen():nullptr;
        QJsonArray screens;for(QScreen *value:QGuiApplication::screens())screens.append(screen(value));
        const QPoint center=apply->mapToGlobal(apply->rect().center());
        const QRect globalRect(apply->mapToGlobal(QPoint()),apply->size());
        const QRegion visible=apply->visibleRegion();QJsonArray regions;
        for(const QRect &r:visible)if(regions.size()<32)regions.append(rect(r));
        record.insert("screens",screens);record.insert("actual_window_screen",screen(actualScreen));
        record.insert("active",host.isActiveWindow());record.insert("exposed",host.windowHandle()->isExposed());
        record.insert("widget_DPR",host.devicePixelRatioF());record.insert("window_DPR",host.windowHandle()->devicePixelRatio());
        record.insert("client_geometry",rect(host.geometry()));record.insert("frame_geometry",rect(host.frameGeometry()));
        record.insert("host_global_origin",point(host.mapToGlobal(QPoint())));
        record.insert("viewport_rect",rect(host.viewport()->rect()));record.insert("page_pos",point(page->pos()));
        record.insert("viewport_global_rect",rect(QRect(host.viewport()->mapToGlobal(QPoint()),host.viewport()->size())));
        record.insert("button_global_rect",rect(globalRect));record.insert("button_global_center",point(center));
        record.insert("button_rect",rect(apply->rect()));record.insert("button_parent_geometry",rect(apply->parentWidget()->geometry()));
        record.insert("visible_region",regions);
        record.insert("visible_region_contains_entire_button",(QRegion(apply->rect())-visible).isEmpty());
        const QPoint windowPoint=host.windowHandle()->mapFromGlobal(center);
        record.insert("center_map_to_host",point(host.mapFromGlobal(center)));
        record.insert("center_map_to_viewport",point(host.viewport()->mapFromGlobal(center)));
        record.insert("center_map_to_button",point(apply->mapFromGlobal(center)));
        record.insert("center_map_to_QWindow",point(windowPoint));
        record.insert("QWindow_roundtrip_global",point(host.windowHandle()->mapToGlobal(windowPoint)));
        record.insert("cursor_global_before",point(QCursor::pos()));
        const bool frameFits=actualScreen&&actualScreen->availableGeometry().contains(host.frameGeometry());
        const bool buttonFits=actualScreen&&actualScreen->availableGeometry().contains(globalRect);
        record.insert("frame_in_actual_screen_available",frameFits);record.insert("button_in_actual_screen_available",buttonFits);
        const auto captureContent=[&]()
        {
            const QPixmap capture=host.grab();record.insert("grab_physical_pixels",QJsonArray{capture.width(),capture.height()});
            record.insert("grab_DPR",capture.devicePixelRatio());
            record.insert("grab_saved",capture.save(QDir(evidence_).filePath(cn?"cn_content.png":"control_content.png")));
        };
        if(!frameFits||!buttonFits)
        {
            record.insert("outcome","prerequisite_unmet_screen_frame_or_button");record.insert("stage","screen_precondition");
            captureContent();
            QFAIL("PREREQUISITE_UNMET: actual screen available area does not contain fixed host frame/button; not a CN hit failure");
        }
        record.insert("stage","original_global_hit");
        record.insert("hit_monotonic_ms",double(clock_.elapsed()));
        QWidget *hit=QApplication::widgetAt(center);
        const bool nativeHit=hit==apply||(hit&&apply->isAncestorOf(hit));
        record.insert("widgetAt",identity(hit));record.insert("topLevelAt_QWidget",identity(QApplication::topLevelAt(center)));
        record.insert("topLevelAt_QWindow",identity(QGuiApplication::topLevelAt(center)));
        record.insert("host_childAt",identity(host.childAt(host.mapFromGlobal(center))));
        record.insert("viewport_childAt",identity(host.viewport()->childAt(host.viewport()->mapFromGlobal(center))));
        record.insert("nativeHit",nativeHit);
        // Capture only after the one-shot lookup, so grabbing/repainting does
        // not become an unreported input-preparation step that changes its result.
        captureContent();
        // Exactly the prior one-shot center hit predicate. No alternate point,
        // hit-until-success loop, button movement or assertion removal.
        QVERIFY(nativeHit);
        record.insert("stage","window_mouse_path");
        MouseTrace trace;trace.clock=&clock_;
        host.installEventFilter(&trace);host.viewport()->installEventFilter(&trace);
        apply->installEventFilter(&trace);host.windowHandle()->installEventFilter(&trace);
        int clicked=0;connect(apply,&QPushButton::clicked,&host,[&clicked](){++clicked;});
        record.insert("click_injection_attempted",true);
        record.insert("injection","QTest::mouseClick(QWindow*,window-local point); Qt window event path, not OS XTest or physical pointer");
        record.insert("window_local_click",point(windowPoint));record.insert("button_local_click",point(apply->rect().center()));
        QTest::mouseClick(host.windowHandle(),Qt::LeftButton,Qt::NoModifier,windowPoint);
        record.insert("mouse_trace",trace.events);record.insert("clicked_count",clicked);
        record.insert("cursor_global_after",point(QCursor::pos()));QCOMPARE(clicked,1);
        if(cn)
        {
            auto *language=static_cast<LanguageSettingPage*>(page);
            QPointer<CusMessageBox> warning=page->findChild<CusMessageBox*>("languageSettingUnavailableMessage");
            record.insert("warning_created",!warning.isNull());record.insert("CN_status",page->property("lastStatus").toString());
            QVERIFY(warning);QCOMPARE(page->property("lastStatus").toString(),QStringLiteral("当前仅支持简体中文，暂不提供语言切换。"));
            // Cleanup only: this diagnostic does not test warning ACK input.
            language->dismissFeedback();QVERIFY(until([&](){return warning.isNull();}));
            QVERIFY(!QApplication::activeModalWidget());
        }
        QVERIFY(host.close());record.insert("outcome","passed");record.insert("stage","closed");
        record.insert("finished_monotonic_ms",double(clock_.elapsed()));
    }
private slots:
    void initTestCase()
    {
        clock_.start();QCOMPARE(QGuiApplication::platformName(),QString("xcb"));
        evidence_=qEnvironmentVariable("SN100_HIT_AB_EVIDENCE");QVERIFY(QDir::isAbsolutePath(evidence_));
        const QString source=QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().canonicalPath();
        const QString resolved=QFileInfo(evidence_).canonicalFilePath();
        const QString relative=QDir(source).relativeFilePath(resolved);
        QVERIFY(!source.isEmpty()&&!resolved.isEmpty()&&relative!="."&&relative!="..");
        QVERIFY(!relative.startsWith("../")&&!QDir::isAbsolutePath(relative));
        QVERIFY(QDir(resolved).entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot).isEmpty());
        evidence_=resolved;
    }
    void ordinaryControl(){runArm(false);}
    void cnPage(){runArm(true);}
    void cleanupTestCase()
    {QVERIFY(controlWritten_);QVERIFY(cnWritten_);QVERIFY(clock_.elapsed()<25000);QVERIFY(!QApplication::activeModalWidget());}
};
int main(int argc,char **argv)
{
    QApplication app(argc,argv);app.setStyle("Fusion");app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName("SN100PrivateHitProbe");
    QCoreApplication::setApplicationName("hit_ab_probe");
    HitAbProbe test;return QTest::qExec(&test,argc,argv);
}
#include "hit_ab_probe.moc"
