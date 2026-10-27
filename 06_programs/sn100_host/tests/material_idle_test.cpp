#include <QtTest>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include "shell/widgets/material/qtmaterialflatbutton.h"
#include "shell/widgets/material/qtmaterialflatbutton_internal.h"

class PaintCounter : public QObject
{
public:
    int paints = 0;
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::Paint) ++paints;
        return false;
    }
};

class MaterialIdleTest : public QObject
{
    Q_OBJECT
private slots:
    void invisibleHaloDoesNotRepaint();
};

void MaterialIdleTest::invisibleHaloDoesNotRepaint()
{
    QWidget host;
    host.resize(420, 240);
    QtMaterialFlatButton button(QStringLiteral("登录"), &host);
    button.setGeometry(80, 70, 260, 64);
    button.setUseThemeColors(false);
    button.setBackgroundMode(Qt::OpaqueMode);
    button.setBackgroundColor(QColor(127, 127, 127));
    button.setForegroundColor(Qt::white);
    button.setCheckable(true);
    auto *machine = button.findChild<QtMaterialFlatButtonStateMachine *>();
    QVERIFY(machine);
    PaintCounter counter;
    button.installEventFilter(&counter);
    host.show();
    QApplication::setActiveWindow(&host);
    QTest::mouseMove(&host, QPoint(5, 5));
    button.clearFocus();
    QTest::qWait(700);
    QCOMPARE(machine->property("haloOpacity").toDouble(), 0.0);
    counter.paints = 0;
    QTest::qWait(600);
    const int idle = counter.paints;
    // 透明光晕不改变任何像素；这里检查真实Paint事件而非复述setter。
    QVERIFY2(idle <= 3, qPrintable(QStringLiteral("Invisible halo caused %1 paints in 600ms").arg(idle)));

    button.setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(machine->property("haloOpacity").toDouble() > 0);
    QTest::qWait(350);
    counter.paints = 0;
    QTest::qWait(600);
    const int focused = counter.paints;
    QVERIFY2(focused >= 8, "Visible focused halo must retain its original pulse");

    button.setHaloVisible(false);
    QTest::qWait(100);
    counter.paints = 0;
    QTest::qWait(600);
    const int hiddenHalo = counter.paints;
    QVERIFY(hiddenHalo <= 3);
    button.setHaloVisible(true);
    counter.paints = 0;
    QTest::qWait(350);
    QVERIFY(counter.paints >= 5);

    QSignalSpy clicks(&button, &QPushButton::clicked);
    QTest::mouseClick(&button, Qt::LeftButton);
    QCOMPARE(clicks.size(), 1);
    QVERIFY(button.isChecked());
    button.clearFocus();
    QTest::mouseMove(&host, QPoint(5, 5));
    QTest::qWait(1500); // ripple原有1300ms渐出完成后再测空载。
    counter.paints = 0;
    QTest::qWait(600);
    const int afterClick = counter.paints;
    QVERIFY(afterClick <= 3);
    button.setEnabled(false);
    QTest::qWait(300);
    counter.paints = 0;
    QTest::qWait(600);
    const int disabled = counter.paints;
    QVERIFY(disabled <= 3);
    QTest::mouseClick(&button, Qt::LeftButton);
    QCOMPARE(clicks.size(), 1);
    button.hide();
    QTest::qWait(100);
    counter.paints = 0;
    QTest::qWait(300);
    QCOMPARE(counter.paints, 0);
    button.setEnabled(true);
    button.show();
    button.setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(machine->property("haloOpacity").toDouble() > 0);
    counter.paints = 0;
    QTest::qWait(600);
    QVERIFY(counter.paints >= 8);

    const QString directory = QString::fromUtf8(qgetenv("SN100_MATERIAL_EVIDENCE_DIR"));
    if (!directory.isEmpty())
    {
        QVERIFY(QDir().mkpath(directory));
        QFile file(QDir(directory).filePath(QStringLiteral("paint_counts.json")));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.write(QJsonDocument(QJsonObject{{"idle_600ms", idle}, {"focused_600ms", focused},
            {"hidden_halo_600ms", hiddenHalo}, {"after_click_600ms", afterClick},
            {"disabled_600ms", disabled}, {"clicks", clicks.size()}}).toJson());
        QVERIFY(host.grab().save(QDir(directory).filePath(QStringLiteral("focused.png"))));
    }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    MaterialIdleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "material_idle_test.moc"
