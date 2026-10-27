// 仅在RK显示环境运行。驱动正式页面控件；SQL只用于只读核对和制造写锁。
#include "project_page.h"
#include "cuscheckbox.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <algorithm>
#include <functional>

static bool waitFor(const std::function<bool()> &condition, int timeout = 10000)
{
    QElapsedTimer elapsed; elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout)
    {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}

static bool mouseClick(QWidget *widget, QPoint point = QPoint(), Qt::MouseButton button = Qt::LeftButton)
{
    if (!widget || !widget->isEnabled() || !widget->isVisible()) return false;
    if (point.isNull())
    {
        if (auto *checkbox = qobject_cast<QCheckBox *>(widget))
        {
            // 表格把无文字复选框拉满整格；控件中心可能不在实际勾选命中区域。
            QStyleOptionButton option; option.initFrom(checkbox);
            point = checkbox->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, checkbox).center();
        }
        else point = widget->rect().center();
    }
    const QPoint global = widget->mapToGlobal(point);
    QMouseEvent press(QEvent::MouseButtonPress, point, global, button, button, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, button, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press); QApplication::sendEvent(widget, &release);
    return true;
}

static void typeText(QLineEdit *edit, const QString &value)
{
    if (!edit) return;
    edit->setFocus();
    QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QKeyEvent erase(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QKeyEvent type(QEvent::KeyPress, 0, Qt::NoModifier, value);
    QApplication::sendEvent(edit, &select); QApplication::sendEvent(edit, &erase);
    if (!value.isEmpty()) QApplication::sendEvent(edit, &type);
}

static QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return QByteArray();
    return hash.result();
}

// 把原项目每张表的完整内容做指纹，只输出是否相同，不输出实际配置值。
static QMap<QString, QByteArray> tableHashes(const QString &path)
{
    QMap<QString, QByteArray> hashes;
    const QString connection = QStringLiteral("page_audit");
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        database.setDatabaseName(path);
        if (database.open())
            for (const QString &table : database.tables())
            {
                QSqlQuery query(database);
                if (!query.exec(QStringLiteral("SELECT * FROM \"%1\" ORDER BY rowid").arg(table))) continue;
                QByteArray bytes; QDataStream stream(&bytes, QIODevice::WriteOnly);
                const auto record = query.record();
                for (int column = 0; column < record.count(); ++column) stream << record.fieldName(column);
                while (query.next())
                    for (int column = 0; column < record.count(); ++column) stream << query.value(column);
                hashes.insert(table, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
            }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return hashes;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle(QStringLiteral("Fusion"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    QCommandLineParser parser;
    parser.addOptions({{"data-dir", "RK专用原配置工作副本", "root"},
                       {"output-dir", "本次证据目录", "output"},
                       {"phase", "read/edit/results/units/files/failure/all", "phase", "all"}});
    parser.process(app);
    const QString root = parser.value("data-dir"), output = parser.value("output-dir"), phase = parser.value("phase");
    if (!QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(output) ||
        !QFileInfo::exists(root + "/.sn100-original-work-v1") ||
        !QStringList{"read", "edit", "results", "units", "files", "failure", "all"}.contains(phase) ||
        !QDir().mkpath(output)) return 2;
    const auto includes = [&](const char *name) { return phase == "all" || phase == QLatin1String(name); };
    int passed = 0, failed = 0;
    QJsonArray assertions, operations;
    const auto check = [&](bool good, const QString &name)
    {
        good ? ++passed : ++failed;
        qInfo().noquote() << (good ? "PASS" : "FAIL") << name;
        assertions.append(QJsonObject{{"passed", good}, {"name", name}});
    };
    ProjectsStore::registerTypes();
    QThread worker;
    auto *store = new ProjectsStore;
    store->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new ProjectPage(store);
    AccountSession engineer;
    engineer.name = "rkOriginalPageEngineer"; engineer.role = 4; engineer.authenticated = true;
    engineer.source = AccountSessionSource::Simulated;
    page->setSession(engineer);
    QScrollArea host;
    host.setWidget(page); host.setWidgetResizable(false); host.resize(1600, 900); host.show();
    int receiptCount = 0, saveCount = 0;
    bool lastSaveOK = false;
    QObject::connect(page, &ProjectPage::operationFinished, &app, [&](const QString &operation, bool good)
    {
        ++receiptCount;
        if (operation == "save") { ++saveCount; lastSaveOK = good; }
        operations.append(QJsonObject{{"operation", operation}, {"success", good}});
    });
    const auto widget = [&](const char *name) { return page->findChild<QWidget *>(QString::fromLatin1(name)); };
    const auto click = [&](const char *name)
    {
        QWidget *target = widget(name);
        if (target) host.ensureWidgetVisible(target);
        return mouseClick(target);
    };
    const auto text = [&](const char *name, const QString &value)
    {
        auto *edit = page->findChild<QLineEdit *>(QString::fromLatin1(name));
        if (edit) host.ensureWidgetVisible(edit);
        check(edit && edit->isEnabled() && !edit->isReadOnly(), QStringLiteral("正式输入框可编辑：") + name);
        typeText(edit, value);
    };
    QElapsedTimer heartbeat; heartbeat.start(); qint64 lastBeat = 0;
    QVector<qint64> heartbeatGaps, feedbackTimes, completionTimes;
    QTimer beat;
    beat.setTimerType(Qt::PreciseTimer); beat.setInterval(10);
    QObject::connect(&beat, &QTimer::timeout, &app, [&]()
    {
        const qint64 now = heartbeat.elapsed();
        if (lastBeat) heartbeatGaps.append(now - lastBeat);
        lastBeat = now;
    });
    beat.start();
    QTimer messages;
    QObject::connect(&messages, &QTimer::timeout, &app, [&]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
        {
            const auto answer = box->standardButtons().testFlag(QMessageBox::Yes) ? QMessageBox::Yes : QMessageBox::Ok;
            mouseClick(box->button(answer));
        }
    });
    messages.start(40);
    const auto closeDialog = [](QWidget *dialog)
    {
        for (auto *buttons : dialog->findChildren<QDialogButtonBox *>())
            if (auto *close = buttons->button(QDialogButtonBox::Close)) { mouseClick(close); return; }
        if (auto *window = qobject_cast<QDialog *>(dialog)) window->reject();
    };
    const auto menu = [&](const QString &actionName, const QString &dialogName,
                          const std::function<void(QWidget *)> &task, bool resultMenu = false)
    {
        bool chosen = false, handled = false; int ticks = 0;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &app, [&]()
        {
            if (++ticks > 250)
            {
                if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
                if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) popup->close();
                poll.stop(); return;
            }
            if (!chosen)
                if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
                    for (auto *action : popup->actions())
                        if (action->objectName() == actionName)
                        {
                            chosen = action->isEnabled();
                            if (chosen) mouseClick(popup, popup->actionGeometry(action).center());
                            else popup->close();
                            return;
                        }
            if (chosen && !handled)
            {
                QWidget *dialog = QApplication::activeModalWidget();
                if (!dialog || dialog->objectName() != dialogName) dialog = page->findChild<QWidget *>(dialogName);
                if (dialog && dialog->isVisible() && dialog->objectName() == dialogName)
                { handled = true; task(dialog); }
            }
        });
        poll.start(40);
        bool opened = false;
        if (resultMenu)
        {
            auto *table = page->findChild<QTableWidget *>("resultTable");
            if (table)
            {
                host.ensureWidgetVisible(table);
                const QPoint point(30, 20);
                QContextMenuEvent context(QContextMenuEvent::Mouse, point, table->viewport()->mapToGlobal(point));
                opened = QApplication::sendEvent(table->viewport(), &context);
            }
        }
        else opened = click("btnChange");
        poll.stop();
        check(opened && chosen && handled, QStringLiteral("正式菜单和弹窗：") + actionName);
    };
    const auto choosePath = [&](QWidget *dialog, const QString &path)
    {
        auto *fileDialog = qobject_cast<QFileDialog *>(dialog);
        if (!fileDialog) return;
        fileDialog->setDirectory(QFileInfo(path).absolutePath());
        fileDialog->selectFile(QFileInfo(path).fileName());
        auto *name = fileDialog->findChild<QLineEdit *>("fileNameEdit");
        typeText(name, path);
        auto *buttons = fileDialog->findChild<QDialogButtonBox *>("buttonBox");
        QPushButton *accept = buttons ? buttons->button(fileDialog->acceptMode() == QFileDialog::AcceptSave ?
                                                        QDialogButtonBox::Save : QDialogButtonBox::Open) : nullptr;
        check(accept && waitFor([&]() { return accept->isEnabled(); }), QStringLiteral("文件选择确认可用"));
        if (!mouseClick(accept)) fileDialog->reject();
    };
    const auto save = [&](bool expected)
    {
        const int previous = saveCount;
        QElapsedTimer elapsed; elapsed.start();
        const bool clicked = click("btnSave");
        check(clicked, QStringLiteral("点击正式保存按钮"));
        feedbackTimes.append(elapsed.elapsed());
        check(page->isBusy(), QStringLiteral("投递后立即进入忙碌状态"));
        // 前置条件失败时不要再试点其它业务按钮；空闲的复制按钮会打开真实模态窗。
        if (!clicked || !page->isBusy()) return;
        check(!click("btnSave") && !click("btnCopy") && !click("btnDelProj"), QStringLiteral("在途任务禁止重复及冲突点击"));
        check(waitFor([&]() { return saveCount > previous && !page->isBusy(); }), QStringLiteral("收到保存结果并完成读回"));
        completionTimes.append(elapsed.elapsed());
        check(saveCount == previous + 1 && lastSaveOK == expected, QStringLiteral("单次保存只收到一次实际结果"));
        if (expected) check(!page->hasUnsavedChanges(), QStringLiteral("成功读回后清除已提交草稿"));
        else check(page->hasUnsavedChanges(), QStringLiteral("失败保留页面草稿"));
    };
    worker.start();
    QMetaObject::invokeMethod(store, [=]() { store->initializeOriginal(root); }, Qt::QueuedConnection);
    check(waitFor([&]() { return page->isReady() && !page->isBusy() && page->projects().size() == 7; }), QStringLiteral("原工作目录7项目真实页面加载"));
    check(page->property("originalProfile").toBool() && page->units().size() == 7, QStringLiteral("原库模式和7项原单位读取"));
    auto *list = page->findChild<QTableWidget *>("lstProject");
    int selectedRow = -1;
    for (int row = 0; row < page->projects().size(); ++row)
        if (page->projects()[row].name == "PT") selectedRow = row;
    if (list && selectedRow >= 0)
    {
        list->scrollToItem(list->item(selectedRow, 1)); host.ensureWidgetVisible(list);
        check(mouseClick(list->viewport(), list->visualItemRect(list->item(selectedRow, 1)).center()), QStringLiteral("在原项目列表选择PT"));
    }
    check(selectedRow >= 0, QStringLiteral("原项目列表存在PT"));
    ProjectRecord initial;
    for (const auto &project : page->projects()) if (project.name == "PT") initial = project;
    const QString projectPath = QDir(root).filePath("project/" + initial.originalFileName);
    const auto current = [&]()
    {
        for (const auto &project : page->projects()) if (project.id == initial.id) return project;
        return ProjectRecord();
    };
    if (includes("read"))
    {
        check(initial.originalAlgorithmValues.size() == 91, QStringLiteral("PT全部91项算法参数读入"));
        for (const auto &project : page->projects())
            check(project.results.size() == 5 && !project.originalBaseValues.isEmpty() && !project.originalFileName.isEmpty(),
                  QStringLiteral("原项目保留5结果及完整参数：") + project.name);
        menu("viewOriginalProjectParameters", "originalProjectParametersDialog", [&](QWidget *dialog)
        {
            auto *base = dialog->findChild<QTableWidget *>("originalProjectBaseTable");
            auto *algorithm = dialog->findChild<QTableWidget *>("originalProjectAlgorithmTable");
            const auto matches = [](QTableWidget *table, const QMap<QString, QString> &values)
            {
                if (!table || table->rowCount() != values.size() || table->editTriggers() != QAbstractItemView::NoEditTriggers) return false;
                for (int row = 0; row < table->rowCount(); ++row)
                    if (!table->item(row, 0) || !table->item(row, 1) ||
                        values.value(table->item(row, 0)->text()) != table->item(row, 1)->text()) return false;
                return true;
            };
            check(matches(base, initial.originalBaseValues) && matches(algorithm, initial.originalAlgorithmValues), QStringLiteral("完整参数窗口逐项显示相同原值且只读"));
            dialog->grab().save(output + "/original_parameters.png"); closeDialog(dialog);
        });
        const QByteArray before = fileHash(projectPath);
        check(!widget("btnSave")->isEnabled() && !click("btnSave"), QStringLiteral("无草稿时保存不可重复触发"));
        check(!before.isEmpty() && before == fileHash(projectPath), QStringLiteral("只读查看不改原项目字节"));
        page->grab().save(output + "/original_base.png");
    }
    if (includes("edit"))
    {
        const auto beforeTables = tableHashes(projectPath);
        const auto before = current();
        text("project_lis_code", "RKUI123"); save(true);
        const auto after = current();
        check(after.lisCode == "RKUI123", QStringLiteral("正式页面修改已写入原项目并读回"));
        check(after.algorithm.coefficientA == before.algorithm.coefficientA && after.algorithm.coefficientB == before.algorithm.coefficientB &&
              after.algorithm.coefficientC == before.algorithm.coefficientC, QStringLiteral("未编辑系数完整精度保留"));
        const auto afterTables = tableHashes(projectPath);
        bool untouched = beforeTables.size() == 7 && afterTables.size() == 7;
        for (auto table = beforeTables.cbegin(); table != beforeTables.cend(); ++table)
            if (table.key() != "project_base") untouched = untouched && afterTables.value(table.key()) == table.value();
        check(untouched, QStringLiteral("编辑LIS代码不改其他6张原表含91算法键及MDA"));
        const int previous = receiptCount; page->reload();
        check(waitFor([&]() { return receiptCount > previous && !page->isBusy(); }) && current().lisCode == "RKUI123", QStringLiteral("重新读取保留已保存值"));
        text("project_lis_code", before.lisCode); save(true);
        const QByteArray bytes = fileHash(projectPath);
        text("project_lis_code", before.lisCode); save(true);
        check(bytes == fileHash(projectPath), QStringLiteral("输入原值形成空改动时不重写DB字节"));
        text("project_lis_code", "CANCEL");
        check(click("btnCancel") && !page->hasUnsavedChanges(), QStringLiteral("取消恢复提交值而不写库"));
    }
    if (includes("results"))
    {
        const auto before = current(); int changedRow = -1;
        for (int row = 0; row < before.results.size(); ++row)
            if (before.results[row].type == "Calibration") changedRow = row;
        const auto changeResult = [&]()
        {
            menu("configureCurrentResultFormat", "originalProjectResultsDialog", [&](QWidget *dialog)
            {
                auto *table = dialog->findChild<QTableWidget *>("originalProjectResultsTable");
                check(table && table->rowCount() == 5, QStringLiteral("正式原结果窗口显示全部5行"));
                auto *mda = dialog->findChild<QCheckBox *>("originalResult_4_4");
                check(mda && !mda->isEnabled(), QStringLiteral("MDA未知规则保持只读"));
                auto *display = dialog->findChild<QCheckBox *>(QStringLiteral("originalResult_%1_4").arg(changedRow));
                const bool wasChecked = display && display->isChecked();
                check(mouseClick(display) && display->isChecked() != wasChecked,
                      QStringLiteral("实际点击勾选区域并反转已确认结果显示状态"));
                dialog->grab().save(output + "/original_results.png");
                check(mouseClick(dialog->findChild<QPushButton *>("applyOriginalResultDraft")), QStringLiteral("应用结果选择到草稿"));
            }, true);
        };
        const QByteArray bytes = fileHash(projectPath); changeResult();
        check(page->hasUnsavedChanges() && bytes == fileHash(projectPath), QStringLiteral("结果弹窗仅生成草稿"));
        save(true);
        check(changedRow >= 0 && current().results[changedRow].display != before.results[changedRow].display &&
              current().results.size() == 5 && current().results.last().type == "MDA", QStringLiteral("保存结果显示且保留第5行MDA"));
        changeResult(); save(true);
    }
    if (includes("units"))
    {
        menu("manageProjectUnits", "projectUnitDialog", [&](QWidget *dialog)
        {
            auto *name = dialog->findChild<QLineEdit *>("projectUnitName");
            auto *ratio = dialog->findChild<QLineEdit *>("projectOriginalUnitRatio");
            auto *type = dialog->findChild<QComboBox *>("projectOriginalUnitType");
            check(name && ratio && type && type->count() > 0, QStringLiteral("原单位类型及实数倍率输入框"));
            typeText(name, "RK_UI_TMP"); typeText(ratio, "1.25"); if (type) type->setCurrentIndex(0);
            check(mouseClick(dialog->findChild<QPushButton *>("addProjectUnit")), QStringLiteral("新增未引用的原单位"));
            check(waitFor([&]() { return !page->isBusy() && page->units().size() == 8; }), QStringLiteral("单位1.25倍率保存读回"));
            auto *table = dialog->findChild<QTableWidget *>("projectUnitsTable");
            int row = -1;
            for (int index = 0; table && index < table->rowCount(); ++index)
                if (table->item(index, 0)->text() == "RK_UI_TMP") row = index;
            if (row >= 0)
            {
                table->scrollToItem(table->item(row, 0));
                mouseClick(table->viewport(), table->visualItemRect(table->item(row, 0)).center());
            }
            typeText(ratio, "2.5");
            check(mouseClick(dialog->findChild<QPushButton *>("updateProjectUnit")), QStringLiteral("修改原单位倍率"));
            check(waitFor([&]()
            {
                for (const auto &unit : page->units()) if (unit.name == "RK_UI_TMP") return !page->isBusy() && unit.ratio == 2.5;
                return false;
            }), QStringLiteral("原单位小数倍率2.5读回未整数化"));
            check(mouseClick(dialog->findChild<QPushButton *>("removeProjectUnit")), QStringLiteral("删除未引用原单位"));
            check(waitFor([&]() { return !page->isBusy() && page->units().size() == 7; }), QStringLiteral("删除后恢复7项原单位"));
            dialog->grab().save(output + "/original_units.png"); closeDialog(dialog);
        });
    }
    if (includes("files"))
    {
        const auto before = current(); text("project_lis_code", "DRAFT1");
        const QString exported = output + "/exported_configuration";
        const int previous = receiptCount;
        menu("exportOriginalProjectConfiguration", "originalProjectExportDialog", [&](QWidget *dialog) { choosePath(dialog, exported); });
        check(waitFor([&]() { return receiptCount > previous && !page->isBusy(); }), QStringLiteral("后台原DB整组导出完成"));
        check(page->hasUnsavedChanges() && page->findChild<QLineEdit *>("project_lis_code")->text() == "DRAFT1" && current().lisCode == before.lisCode,
              QStringLiteral("导出已提交DB且保留未保存页面输入"));
        check(QFileInfo::exists(exported + "/db/systemInfo/option_Unit.db") &&
              QFileInfo::exists(exported + "/db/userSet/reagent_Information.db") &&
              QDir(exported + "/project").entryList({"*.db"}, QDir::Files).size() == 7,
              QStringLiteral("导出保留原project及db/systemInfo、db/userSet目录分类"));
        check(click("btnCancel"), QStringLiteral("导出后取消未保存草稿"));
        const int importBefore = receiptCount;
        menu("importOriginalProjectDatabase", "originalProjectImportDialog", [&](QWidget *dialog) { choosePath(dialog, projectPath); });
        check(waitFor([&]() { return receiptCount > importBefore && !page->isBusy(); }) && page->projects().size() == 7,
              QStringLiteral("同名原项目DB导入拒绝覆盖且仍为7项目"));
    }
    if (includes("failure"))
    {
        const auto before = current();
        {
            auto database = QSqlDatabase::addDatabase("QSQLITE", "page_lock"); database.setDatabaseName(projectPath);
            check(database.open(), QStringLiteral("专用RK副本写锁连接就绪"));
            QSqlQuery lock(database); check(lock.exec("BEGIN IMMEDIATE"), QStringLiteral("原项目真实写锁建立"));
            text("project_lis_code", "LOCKTRY"); save(false);
            check(page->findChild<QLineEdit *>("project_lis_code")->text() == "LOCKTRY", QStringLiteral("失败后保留原输入"));
            check(lock.exec("ROLLBACK"), QStringLiteral("释放测试写锁")); database.close();
        }
        QSqlDatabase::removeDatabase("page_lock"); save(true);
        check(current().lisCode == "LOCKTRY", QStringLiteral("失败恢复后重试实际写回原DB"));
        text("project_lis_code", before.lisCode); save(true);
    }
    AccountSession guest; page->setSession(guest);
    check(!widget("btnSave")->isEnabled() && !widget("btnCopy")->isEnabled(), QStringLiteral("无编辑权限禁用原库写入"));
    page->setSession(engineer);
    page->grab().save(output + "/final_page.png");
    check(!page->hasUnsavedChanges(), QStringLiteral("测试退出没有遗留草稿"));
    QMetaObject::invokeMethod(store, "close", Qt::BlockingQueuedConnection);
    worker.quit(); check(worker.wait(3000), QStringLiteral("数据库工作线程正常退出"));
    beat.stop(); messages.stop(); host.close();
    const auto distribution = [](QVector<qint64> values)
    {
        std::sort(values.begin(), values.end());
        if (values.isEmpty()) return QJsonObject{{"samples", 0}};
        return QJsonObject{{"samples", values.size()}, {"p95Ms", double(values[(values.size()-1)*95/100])},
                           {"p99Ms", double(values[(values.size()-1)*99/100])}, {"maxMs", double(values.last())}};
    };
    const QJsonObject summary{{"phase", phase}, {"passed", passed}, {"failed", failed}, {"instrumentActions", 0},
        {"mouseEventsToRealWidgets", true}, {"physicalHdmiVisualConfirmed", false}, {"heartbeatIntervalMs", 10},
        {"heartbeatGaps", distribution(heartbeatGaps)}, {"feedback", distribution(feedbackTimes)},
        {"completion", distribution(completionTimes)}, {"assertions", assertions}, {"operations", operations}};
    QFile report(output + "/page_result.json");
    if (!report.open(QIODevice::WriteOnly) || report.write(QJsonDocument(summary).toJson()) <= 0) return 3;
    qInfo() << "RESULT" << "passed" << passed << "failed" << failed << "instrumentActions" << 0;
    return failed ? 1 : 0;
}
