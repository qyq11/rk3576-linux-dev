#include "ui/language_setting_page.h"
#include "ui/cusmessagebox.h"

#include <QApplication>
// VIEWPORT_OBSERVER_BEGIN
#include <QScrollArea>
#include <QScrollBar>
#include <QJsonDocument>
#include <QJsonObject>
// VIEWPORT_OBSERVER_END
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMap>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace
{
const QString chinese = QStringLiteral("Cn 中文简体");
const QString unsupported = QStringLiteral("当前仅支持简体中文，暂不提供语言切换。");

class TestIniFormatScope
{
public:
    TestIniFormatScope() : previous_(QSettings::defaultFormat())
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }
    ~TestIniFormatScope()
    {
        QSettings::setDefaultFormat(previous_);
    }
private:
    QSettings::Format previous_;
};

QPushButton *applyButton(LanguageSettingPage *page)
{
    return page->findChild<QPushButton *>(QStringLiteral("btnApply"));
}

QPointer<CusMessageBox> feedback(LanguageSettingPage *page)
{
    return page->findChild<CusMessageBox *>(QStringLiteral("languageSettingUnavailableMessage"));
}

QMap<QString, QByteArray> fileSnapshot(const QString &directory)
{
    QMap<QString, QByteArray> result;
    QDirIterator files(directory, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (files.hasNext())
    {
        QFile file(files.next());
        const QString name = QDir(directory).relativeFilePath(file.fileName());
        if (!file.open(QIODevice::ReadOnly))
        {
            result.insert(QStringLiteral("unreadable:") + name, QByteArray());
            continue;
        }
        result.insert(name, QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256));
    }
    return result;
}
} // namespace

class LanguageSettingPageTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        root_ = qEnvironmentVariable("SN100_LANGUAGE_PAGE_TEST_ROOT");
        QVERIFY2(QDir::isAbsolutePath(root_) && !QFileInfo::exists(root_),
                 "SN100_LANGUAGE_PAGE_TEST_ROOT must be a fresh absolute anonymous directory");
        QVERIFY(!root_.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive));
        QVERIFY(QDir().mkpath(root_));
    }

    void originalCanvasAndSingleChineseChoice()
    {
        LanguageSettingPage page;
        QCOMPARE(page.objectName(), QStringLiteral("frmLanguageSetting"));
        QCOMPARE(page.size(), QSize(1451, 853));
        QCOMPARE(page.minimumSize(), page.maximumSize());
        auto *panel = page.findChild<QWidget *>(QStringLiteral("widget"));
        auto *label = page.findChild<QLabel *>(QStringLiteral("lblHoleinfo_7"));
        auto *edit = page.findChild<QLineEdit *>(QStringLiteral("ldtLanguageSelect"));
        auto *line = page.findChild<QLineEdit *>(QStringLiteral("lineEdit_5"));
        auto *list = page.findChild<QListWidget *>(QStringLiteral("lstProject"));
        auto *apply = applyButton(&page);
        QVERIFY(panel && label && edit && line && list && apply);
        QCOMPARE(panel->geometry(), QRect(390, 20, 681, 821));
        QCOMPARE(label->geometry(), QRect(38, 26, 261, 30));
        QCOMPARE(edit->geometry(), QRect(20, 90, 641, 61));
        QCOMPARE(line->geometry(), QRect(20, 170, 641, 20));
        QCOMPARE(list->geometry(), QRect(20, 190, 641, 551));
        QCOMPARE(apply->geometry(), QRect(500, 750, 161, 61));
        QCOMPARE(edit->parentWidget(), panel);
        QCOMPARE(apply->parentWidget(), panel);
        QCOMPARE(label->text(), QStringLiteral("请选择要切换的语言："));
        QCOMPARE(edit->text(), chinese);
        QVERIFY(edit->isReadOnly() && line->isReadOnly());
        QCOMPARE(apply->text(), QStringLiteral("应用"));
        QVERIFY(apply->isEnabled());
        QCOMPARE(apply->focusPolicy(), Qt::NoFocus);
        QCOMPARE(list->count(), 1);
        QCOMPARE(list->item(0)->text(), chinese);
        QCOMPARE(list->item(0)->sizeHint(), QSize(636, 40));
        QVERIFY(!(list->item(0)->flags() & Qt::ItemIsEditable));
        page.show();
        QTRY_VERIFY(page.isVisible());
        QTest::mouseClick(edit, Qt::LeftButton);
        QTest::keyClicks(edit, QStringLiteral("OtherLanguage"));
        QCOMPARE(edit->text(), chinese);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                          list->visualItemRect(list->item(0)).center());
        QCOMPARE(list->count(), 1);
        QCOMPARE(edit->text(), chinese);
        QVERIFY(!feedback(&page));
    }

    void applyReturnsWithOriginalAsynchronousWarning()
    {
        LanguageSettingPage page;
        QSignalSpy status(&page, &LanguageSettingPage::statusChanged);
        QVERIFY(status.isValid());
        page.show();
        QTRY_VERIFY(page.isVisible());
        applyButton(&page)->click(); // 若错误改成exec，此调用不会返回，外层超时会保留失败。
        QPointer<CusMessageBox> message = feedback(&page);
        QVERIFY(message && message->isVisible());
        QCOMPARE(message->getMsg(), unsupported);
        QCOMPARE(page.property("lastStatus").toString(), unsupported);
        QCOMPARE(status.count(), 1);
        QCOMPARE(status.first().first().toString(), unsupported);
        QTimer heartbeat;
        heartbeat.setInterval(5);
        QSignalSpy ticks(&heartbeat, &QTimer::timeout);
        heartbeat.start();
        QTRY_VERIFY(ticks.count() >= 2);
        QVERIFY(message && message->isVisible());
        QTRY_VERIFY(message && message->isReadyForInput());
        auto *icon = message->findChild<QLabel *>(QStringLiteral("lblIco"));
        QVERIFY(icon && icon->styleSheet().contains(QStringLiteral("message_warning.png")));
        heartbeat.stop();
        page.dismissFeedback();
        QTRY_VERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
    }

    void repeatedApplyCannotStackCards()
    {
        LanguageSettingPage page;
        QSignalSpy status(&page, &LanguageSettingPage::statusChanged);
        page.show();
        QTRY_VERIFY(page.isVisible());
        applyButton(&page)->click();
        QPointer<CusMessageBox> message = feedback(&page);
        QVERIFY(message);
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            applyButton(&page)->click();
        }
        QCOMPARE(page.findChildren<CusMessageBox *>().size(), 1);
        QCOMPARE(feedback(&page).data(), message.data());
        QCOMPARE(status.count(), 1);
        page.dismissFeedback();
        QTRY_VERIFY(message.isNull());
        applyButton(&page)->click();
        QTRY_VERIFY(feedback(&page) && feedback(&page)->isVisible());
        QCOMPARE(page.findChildren<CusMessageBox *>().size(), 1);
        QCOMPARE(status.count(), 2);
        page.dismissFeedback();
    }

    void warningCloseControls_data()
    {
        QTest::addColumn<QString>("action");
        QTest::newRow("escape") << QStringLiteral("escape");
        QTest::newRow("original-sure") << QStringLiteral("btnSure");
        QTest::newRow("original-top-x") << QStringLiteral("btnClose");
    }

    void warningCloseControls()
    {
        QFETCH(QString, action);
        LanguageSettingPage page;
        page.show();
        QTRY_VERIFY(page.isVisible());
        QTest::mouseClick(applyButton(&page), Qt::LeftButton);
        QPointer<CusMessageBox> message = feedback(&page);
        QTRY_VERIFY(message && message->isReadyForInput());
        if (action == QStringLiteral("escape"))
        {
            QTest::keyClick(message.data(), Qt::Key_Escape);
        }
        else
        {
            auto *close = message->findChild<QPushButton *>(action);
            QVERIFY(close && close->isVisible() && close->isEnabled());
            QTest::mouseClick(close, Qt::LeftButton);
        }
        QTRY_VERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
        QCOMPARE(page.findChild<QLineEdit *>(QStringLiteral("ldtLanguageSelect"))->text(), chinese);
    }

    void hidingOrClosingPageDismissesPrompt_data()
    {
        QTest::addColumn<bool>("closePage");
        QTest::newRow("hide-during-open-animation") << false;
        QTest::newRow("close-during-open-animation") << true;
    }

    void hidingOrClosingPageDismissesPrompt()
    {
        QFETCH(bool, closePage);
        LanguageSettingPage page;
        page.show();
        QTRY_VERIFY(page.isVisible());
        applyButton(&page)->click();
        QPointer<CusMessageBox> message = feedback(&page);
        QVERIFY(message && message->isVisible());
        if (closePage)
        {
            QVERIFY(page.close());
        }
        else
        {
            page.hide();
        }
        QTRY_VERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
        page.show();
        QTRY_VERIFY(page.isVisible());
        QCOMPARE(page.findChild<QListWidget *>(QStringLiteral("lstProject"))->count(), 1);
        QVERIFY(!feedback(&page));
    }

    void switchingStackDismissesPrompt()
    {
        QStackedWidget host;
        auto *page = new LanguageSettingPage;
        auto *other = new QWidget;
        host.addWidget(page);
        host.addWidget(other);
        host.resize(1451, 853);
        host.show();
        QTRY_VERIFY(page->isVisible());
        applyButton(page)->click();
        QPointer<CusMessageBox> message = feedback(page);
        QVERIFY(message && message->isVisible());
        host.setCurrentWidget(other);
        QTRY_VERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
        host.setCurrentWidget(page);
        QCOMPARE(page->findChild<QLineEdit *>(QStringLiteral("ldtLanguageSelect"))->text(), chinese);
    }

    void destroyingPageDeletesPromptAndAnimation()
    {
        auto *page = new LanguageSettingPage;
        page->show();
        QTRY_VERIFY(page->isVisible());
        applyButton(page)->click();
        QPointer<CusMessageBox> message = feedback(page);
        QVERIFY(message);
        delete page;
        QVERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
    }

    void readOnlyPageDoesNotPersistOrSwitchApplicationState()
    {
        QTemporaryDir directory(QDir(root_).filePath(QStringLiteral("settings-XXXXXX")));
        QVERIFY(directory.isValid());
        // 仅测试进程把默认设置路由到新匿名目录，不接触真实用户设置。
        TestIniFormatScope settingsFormat;
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, directory.path());
        QSettings sentinel;
        sentinel.setValue(QStringLiteral("test/sentinel"), QStringLiteral("unchanged"));
        sentinel.sync();
        QCOMPARE(sentinel.status(), QSettings::NoError);
        const auto files = fileSnapshot(directory.path());
        QVERIFY(!files.isEmpty());
        const QString locale = QLocale().name();
        const QFont applicationFont = QApplication::font();
        LanguageSettingPage page;
        page.show();
        QTRY_VERIFY(page.isVisible());
        applyButton(&page)->click();
        QPointer<CusMessageBox> message = feedback(&page);
        QVERIFY(message);
        QCOMPARE(message->getMsg(), unsupported);
        page.dismissFeedback();
        QTRY_VERIFY(message.isNull());
        QVERIFY(page.close());
        sentinel.sync();
        QCOMPARE(fileSnapshot(directory.path()), files);
        QCOMPARE(sentinel.value(QStringLiteral("test/sentinel")).toString(), QStringLiteral("unchanged"));
        QCOMPARE(QLocale().name(), locale);
        QCOMPARE(QApplication::font(), applicationFont);
    }

    void hiddenPageCannotOpenFeedback()
    {
        LanguageSettingPage page;
        QSignalSpy status(&page, &LanguageSettingPage::statusChanged);
        applyButton(&page)->click();
        QVERIFY(!feedback(&page));
        QCOMPARE(status.count(), 0);
        QVERIFY(!QApplication::activeModalWidget());
    }
    // VIEWPORT_OBSERVER_BEGIN
    void smallViewportScrollAndApply()
    {
        QScrollArea host;
        host.setFrameShape(QFrame::NoFrame);
        host.setWidgetResizable(false);
        auto *page = new LanguageSettingPage;
        host.setWidget(page);
        host.resize(900, 600);
        host.move(20, 20);
        host.show();
        QTRY_VERIFY(page->isVisible());
        auto *apply = applyButton(page);
        QVERIFY(apply);
        host.ensureWidgetVisible(apply, 12, 12);
        QTRY_VERIFY(host.horizontalScrollBar()->value() > 0 && host.verticalScrollBar()->value() > 0);
        const QRect buttonRect(apply->mapTo(host.viewport(), QPoint()), apply->size());
        QVERIFY(host.viewport()->rect().contains(buttonRect));
        QCOMPARE(page->size(), QSize(1451, 853));
        bool nativeHit = false;
        if (QGuiApplication::platformName() == QStringLiteral("xcb"))
        {
            host.raise();
            host.activateWindow();
            QVERIFY(QTest::qWaitForWindowExposed(&host));
            QVERIFY(QTest::qWaitForWindowActive(&host));
            QWidget *hit = QApplication::widgetAt(apply->mapToGlobal(apply->rect().center()));
            nativeHit = hit == apply || (hit && apply->isAncestorOf(hit));
            QVERIFY(nativeHit);
        }
        const QString evidence = qEnvironmentVariable("SN100_CN_VIEWPORT_EVIDENCE");
        QVERIFY(QDir::isAbsolutePath(evidence) && QFileInfo(evidence).isDir());
        QVERIFY(host.grab().save(QDir(evidence).filePath(QStringLiteral("scrolled_page.png"))));
        QTest::mouseClick(apply, Qt::LeftButton);
        QPointer<CusMessageBox> message = feedback(page);
        QTRY_VERIFY(message && message->isReadyForInput());
        QCOMPARE(message->getMsg(), unsupported);
        QCOMPARE(page->property("lastStatus").toString(), unsupported);
        QVERIFY(message->grab().save(QDir(evidence).filePath(QStringLiteral("unavailable_warning.png"))));
        QJsonObject observed{{"host_width",host.width()},{"host_height",host.height()},
            {"canvas_width",page->width()},{"canvas_height",page->height()},
            {"viewport_width",host.viewport()->width()},{"viewport_height",host.viewport()->height()},
            {"scroll_x",host.horizontalScrollBar()->value()},{"scroll_y",host.verticalScrollBar()->value()},
            {"button_fully_in_viewport",host.viewport()->rect().contains(buttonRect)},
            {"xcb_native_widgetAt_hit",nativeHit},{"qpa",QGuiApplication::platformName()},
            {"lastStatus",page->property("lastStatus").toString()},
            {"capture","Actual QWidget::grab; no crop, scale, overlay or original-form changes"}};
        page->dismissFeedback();
        QTRY_VERIFY(message.isNull());
        QVERIFY(!QApplication::activeModalWidget());
        observed.insert("message_destroyed_and_modal_cleared",true);
        QVERIFY(host.close());
        QFile record(QDir(evidence).filePath(QStringLiteral("observed.json")));
        QVERIFY(record.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        const QByteArray bytes=QJsonDocument(observed).toJson(QJsonDocument::Indented);
        QCOMPARE(record.write(bytes),qint64(bytes.size()));
    }
    // VIEWPORT_OBSERVER_END

private:
    QString root_;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setOrganizationName(QStringLiteral("SN100LanguagePageTests"));
    QCoreApplication::setApplicationName(QStringLiteral("language_setting_page_test"));
    LanguageSettingPageTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "language_setting_page_test.moc"
