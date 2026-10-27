#include "project_page.h"
#include "cuscheckbox.h"
#include <QApplication>
#include <QDir>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QComboBox>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <functional>

static bool waitFor(const std::function<bool()> &done, int timeout = 5000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!done() && elapsed.elapsed() < timeout)
    {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return done();
}

// 向正式控件发送鼠标按下/松开事件；不直接调用页面私有槽或改业务表。
static bool mouseClick(QWidget *widget, QPoint point = QPoint())
{
    if (!widget || !widget->isEnabled() || !widget->isVisible()) return false;
    if (point.isNull()) point = widget->rect().center();
    const QPoint global = widget->mapToGlobal(point);
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QApplication::sendEvent(widget, &release);
    return true;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    if (argc != 3 || QString::fromLocal8Bit(argv[1]) != "--data-dir") return 2;
    const QString root = QString::fromLocal8Bit(argv[2]);
    if (!QDir::isAbsolutePath(root) || QFileInfo::exists(root) || !QDir().mkpath(root)) return 2;
    int passed = 0, failed = 0;
    auto check = [&](bool good, const QString &name)
    { qInfo().noquote() << (good ? "PASS" : "FAIL") << name; good ? ++passed : ++failed; };
    ProjectsStore::registerTypes();
    QThread worker;
    auto *store = new ProjectsStore;
    store->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new ProjectPage(store);
    AccountSession engineer;
    engineer.name = "rkPageEngineer"; engineer.role = 4; engineer.authenticated = true;
    engineer.source = AccountSessionSource::Simulated;
    page->setSession(engineer);
    QScrollArea host;
    host.setWidget(page); host.setWidgetResizable(false); host.resize(1600, 900); host.show();
    QObject::connect(page, &ProjectPage::statusChanged, &app,
        [](const QString &status) { qInfo().noquote() << "PAGE_STATUS" << status; });
    QString operation; bool operationOK = false; int operationCount = 0;
    QObject::connect(page, &ProjectPage::operationFinished, &app, [&](const QString &op, bool ok)
    { operation = op; operationOK = ok; ++operationCount; qInfo() << "PAGE_RECEIPT" << op << ok; });
    auto widget = [&](const char *name) { return page->findChild<QWidget *>(QString::fromLatin1(name)); };
    auto click = [&](const char *name)
    {
        QWidget *target = widget(name);
        if (target) host.ensureWidgetVisible(target);
        return mouseClick(target);
    };
    auto text = [&](const char *name, const QString &value)
    {
        auto *edit = page->findChild<QLineEdit *>(QString::fromLatin1(name));
        check(edit && !edit->isReadOnly(), QStringLiteral("可编辑字段：") + name);
        if (edit)
        {
            edit->setFocus();
            QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
            QKeyEvent erase(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
            QKeyEvent type(QEvent::KeyPress, 0, Qt::NoModifier, value);
            QApplication::sendEvent(edit, &select);
            QApplication::sendEvent(edit, &erase);
            if (!value.isEmpty()) QApplication::sendEvent(edit, &type);
        }
    };
    QTimer answerMessages;
    QObject::connect(&answerMessages, &QTimer::timeout, &app, [&]()
    {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        qInfo().noquote() << "MESSAGE" << box->text();
        const auto answer = box->standardButtons().testFlag(QMessageBox::Yes) ? QMessageBox::Yes : QMessageBox::Ok;
        mouseClick(box->button(answer));
    });
    answerMessages.start(50);
    auto menu = [&](const QString &actionName, const QString &dialogName,
                    const std::function<void(QWidget *)> &task)
    {
        QTimer poll;
        bool chosen = false, handled = false;
        int ticks = 0;
        QObject::connect(&poll, &QTimer::timeout, &app, [&]()
        {
            if (++ticks > 150)
            {
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
                if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) popup->close();
                poll.stop(); return;
            }
            if (!chosen)
            {
                if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
                    for (auto *action : popup->actions()) if (action->objectName() == actionName)
                    {
                        chosen = true;
                        check(action->isEnabled(), QStringLiteral("菜单可用：") + actionName);
                        mouseClick(popup, popup->actionGeometry(action).center());
                        return;
                    }
            }
            if (chosen && !handled)
            {
                QWidget *dialog = QApplication::activeModalWidget();
                if (dialog && dialog->objectName() == dialogName)
                { handled = true; poll.stop(); task(dialog); }
            }
        });
        poll.start(50);
        const bool opened = click("btnChange");
        poll.stop();
        check(opened && chosen && handled, QStringLiteral("菜单和实际弹窗：") + actionName);
    };
    auto save = [&]()
    {
        const int previous = operationCount;
        check(click("btnSave"), QStringLiteral("点击原保存按钮"));
        check(waitFor([&]() { return operationCount > previous && !page->isBusy(); }), QStringLiteral("等待保存与重新读取"));
        check(!page->hasUnsavedChanges(), QStringLiteral("收到回执后草稿已提交"));
    };
    worker.start();
    QMetaObject::invokeMethod(store, [=]() { store->initialize(root, false); }, Qt::QueuedConnection);
    check(waitFor([&]() { return page->isReady() && !page->isBusy(); }), QStringLiteral("真实页面空库加载"));
    check(page->projects().isEmpty() && page->units().isEmpty(), QStringLiteral("空库没有演示数据"));
    check(widget("btnSave") && !widget("btnSave")->isEnabled(), QStringLiteral("空库保存未伪装可用"));
    menu("manageProjectUnits", "projectUnitDialog", [&](QWidget *dialog)
    {
        auto *name = dialog->findChild<QLineEdit *>("projectUnitName");
        auto *ratio = dialog->findChild<QSpinBox *>("projectUnitRatio");
        check(name && ratio, QStringLiteral("单位维护真实表单"));
        if (name && ratio) { name->setText("s"); ratio->setValue(1); }
        check(mouseClick(dialog->findChild<QPushButton *>("addProjectUnit")), QStringLiteral("点击新增单位"));
        check(waitFor([&]() { return !page->isBusy() && page->units().size() == 1; }), QStringLiteral("单位新增实际保存读回"));
        dialog->grab().save(root + "/unit_dialog.png");
        for (auto *box : dialog->findChildren<QDialogButtonBox *>())
            if (box->button(QDialogButtonBox::Close)) mouseClick(box->button(QDialogButtonBox::Close));
    });
    menu("createProject", "createProjectDialog", [&](QWidget *dialog)
    {
        dialog->findChild<QLineEdit *>("newProjectName")->setText("PAGE_ANON");
        dialog->findChild<QLineEdit *>("newProjectFullName")->setText(QStringLiteral("页面匿名配置"));
        dialog->findChild<QLineEdit *>("newInstrumentProjectId")->setText("ANONYMOUS_CONFIG_ONLY");
        auto *table = dialog->findChild<QTableWidget *>("newProjectResults");
        for (int row = 0; table && row < table->rowCount(); ++row)
            qobject_cast<QComboBox *>(table->cellWidget(row, 1))->setCurrentText("s");
        dialog->grab().save(root + "/create_dialog.png");
        check(mouseClick(dialog->findChild<QPushButton *>("editNewProject")), QStringLiteral("新建表单进入编辑"));
    });
    check(page->hasUnsavedChanges() && page->projects().isEmpty(), QStringLiteral("新建只是草稿，尚未写入"));
    save();
    check(page->projects().size() == 1 && page->projects().first().name == "PAGE_ANON", QStringLiteral("原保存按钮创建成功"));
    text("project_cn_name", QStringLiteral("取消此修改"));
    check(click("btnCancel"), QStringLiteral("点击原取消按钮"));
    check(!page->hasUnsavedChanges() && page->findChild<QLineEdit *>("project_cn_name")->text() == QStringLiteral("页面匿名配置"),
          QStringLiteral("取消恢复提交值"));
    text("xishu_a", "-1.25"); text("xishu_b", "2.5"); text("sample_warmup_length", "3.75");
    save();
    check(page->projects().value(0).algorithm.configured && page->projects().value(0).algorithm.coefficientC == 3.75,
          QStringLiteral("系数A/B/C从真实页面保存"));
    const int beforeInvalid = operationCount;
    text("xishu_a", "");
    click("btnSave");
    check(page->hasUnsavedChanges() && operationCount == beforeInvalid, QStringLiteral("非法算法参数拒绝落库且保留草稿"));
    click("btnCancel");
    check(click("btnReagentAdd"), QStringLiteral("点击稀释比例页签"));
    check(click("btnAddSampleRate"), QStringLiteral("点击新增稀释方案"));
    auto *samples = page->findChild<QTableWidget *>("sampleRateTable");
    check(samples && samples->rowCount() == 1, QStringLiteral("新增方案真实出现在表格"));
    if (samples && samples->rowCount() == 1)
    {
        const auto columns = ProjectAdvancedEditor::sampleColumns();
        for (int column = 0; column < columns.size(); ++column)
        {
            const QString field = columns[column];
            auto *editor = samples->cellWidget(0, column);
            if (auto *edit = qobject_cast<QLineEdit *>(editor))
                edit->setText(field == "sample_rate" ? "ANON_RATE" :
                    field == "sample_amount_ul" || field == "action_amount_ul" ? "5" : "0");
            if (auto *combo = qobject_cast<QComboBox *>(editor))
                combo->setCurrentText(field == "sample_option" ? QStringLiteral("选用") :
                    field == "calibration_option" ? QStringLiteral("不标定") :
                    field == "sample_add_mode" || field == "blender_type" || field == "before_clean_name" ? "none" : "0");
            if (auto *combo = qobject_cast<QComboBox *>(editor))
                if (field == "sample_option" || field == "calibration_option") combo->setCurrentIndex(field == "sample_option" ? 1 : 0);
            if (auto *checkbox = qobject_cast<CusCheckBox *>(editor))
                if (field == "sample_option" && !checkbox->isChecked()) mouseClick(checkbox);
        }
    }
    text("min_test_time", "5"); text("max_test_time", "40"); text("stopTime", "20"); text("sample_change_clean", "0");
    page->findChild<QComboBox *>("first_light")->setCurrentText("405nm");
    page->findChild<QComboBox *>("second_light")->setCurrentText("570nm");
    page->findChild<QComboBox *>("cbxDefaultRate")->setCurrentText("ANON_RATE");
    save();
    check(page->projects().value(0).sampleRates.size() == 1 && page->projects().value(0).sampleRates.value(0).defaultOption,
          QStringLiteral("稀释方案和唯一默认实际保存"));
    page->grab().save(root + "/sample_page.png");
    click("btnAddSampleRate");
    check(samples->rowCount() == 2, QStringLiteral("第二行新增草稿"));
    click("btnCancel");
    check(samples->rowCount() == 1 && !page->hasUnsavedChanges(), QStringLiteral("取消恢复完整稀释方案"));
    AccountSession guest;
    page->setSession(guest);
    check(!widget("btnSave")->isEnabled() && !widget("btnAddSampleRate")->isEnabled(), QStringLiteral("无权限账户禁用保存和加样编辑"));
    page->setSession(engineer);
    click("btnBaseInfo");
    page->grab().save(root + "/base_page.png");
    const int beforeReopen = operationCount;
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(store, [=]() { store->initialize(root, false); }, Qt::QueuedConnection);
    check(waitFor([&]() { return operationCount > beforeReopen && page->isReady() && !page->isBusy(); }), QStringLiteral("同目录无种子重开真实页面"));
    check(page->projects().size() == 1 && page->projects().first().sampleRates.size() == 1 &&
        page->projects().first().algorithm.coefficientC == 3.75, QStringLiteral("重开保留项目、稀释和算法参数"));
    host.close();
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    worker.quit(); check(worker.wait(3000), QStringLiteral("页面与数据库正常关闭"));
    qInfo() << "RESULT" << "passed" << passed << "failed" << failed << "instrumentActions" << 0
            << "mouseEventsToRealWidgets" << true << "physicalHdmiVisualConfirmed" << false;
    return failed ? 1 : 0;
}
