// 仅供RK专项检查：复用未变化的高级区时，不能保留旧项目、旧来源或未保存草稿。
// 本驱动不改正式界面，不用SQL造业务成功，不连接仪器，不输出原参数值。
#include "project_page.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <functional>

static void settle(int milliseconds = 30)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
    // QTableWidget按Qt规则延迟释放旧单元格；这里核对实际释放，不能只比较原始地址。
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

static bool waitFor(const std::function<bool()> &condition, int timeout = 10000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeout) settle(10);
    return condition();
}

static bool mouseClick(QWidget *widget, const QPoint &point = QPoint())
{
    if (!widget || !widget->isEnabled() || !widget->isVisible()) return false;
    const QPoint local = point.isNull() ? widget->rect().center() : point;
    const QPoint global = widget->mapToGlobal(local);
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
    QApplication::sendEvent(widget, &release);
    return true;
}

static bool typeText(QLineEdit *edit, const QString &text)
{
    if (!edit || !edit->isEnabled() || edit->isReadOnly() || !edit->isVisible()) return false;
    edit->setFocus();
    QKeyEvent select(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier);
    QKeyEvent erase(QEvent::KeyPress, Qt::Key_Backspace, Qt::NoModifier);
    QKeyEvent input(QEvent::KeyPress, 0, Qt::NoModifier, text);
    QApplication::sendEvent(edit, &select);
    QApplication::sendEvent(edit, &erase);
    QApplication::sendEvent(edit, &input);
    return edit->text() == text;
}

static bool sameAdvanced(const ProjectAdvanced &left, const ProjectAdvanced &right)
{
    return left.configured == right.configured && left.fields == right.fields &&
           left.samples == right.samples && left.reagents == right.reagents;
}

static ProjectRecord findProject(const ProjectRecords &projects, qint64 id)
{
    for (const auto &project : projects) if (project.id == id) return project;
    return ProjectRecord();
}

static bool visibleValuesMatch(const ProjectAdvanced &shown, const ProjectAdvanced &stored)
{
    // 按明确的编辑列核对整个高级区，不只比较首行。没有控件的原值另由存储快照逐项核对。
    ProjectAdvanced expected;
    expected.configured = stored.configured;
    for (const QString &field : ProjectAdvancedEditor::scalarFields())
    {
        if (field == "IBS_name" && !stored.fields.contains(field)) continue;
        expected.fields.insert(field, stored.fields.value(field));
    }
    const auto rows = [](const QList<ProjectActionFields> &source, const QStringList &columns)
    {
        QList<ProjectActionFields> result;
        for (const auto &sourceRow : source)
        {
            ProjectActionFields row;
            for (const QString &column : columns)
            {
                QString text = sourceRow.value(column);
                if (column == "sample_option" || column == "calibration_option") text = text == "1" ? "1" : "0";
                row.insert(column, text);
            }
            if (sourceRow.contains("_original_rowid")) row.insert("_original_rowid", sourceRow.value("_original_rowid"));
            result.append(row);
        }
        return result;
    };
    expected.samples = rows(stored.samples, ProjectAdvancedEditor::sampleColumns());
    expected.reagents = rows(stored.reagents, ProjectAdvancedEditor::reagentColumns());
    return sameAdvanced(shown, expected);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setStyle("Fusion");
    app.setFont(QFont("Noto Sans CJK SC", 11));
    QCommandLineParser parser;
    parser.addOptions({{"data-dir", "RK专用原工作副本", "root"},
                       {"output-dir", "本次新证据目录", "output"}});
    parser.process(app);
    const QString root = parser.value("data-dir"), output = parser.value("output-dir");
    QFile workMarker(root + "/.sn100-original-work-v1");
    QFile testMarker(root + "/.sn100-original-rk-test-v1");
    if (!QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(output) ||
        QFileInfo(root).isSymLink() || QFileInfo(output).isSymLink() || QFileInfo::exists(output) ||
        !workMarker.open(QIODevice::ReadOnly) || workMarker.readAll() != "sn100-original-work-v1\n" ||
        !testMarker.open(QIODevice::ReadOnly) || testMarker.readAll() != "sn100-original-rk-test-v1\n" ||
        !QDir().mkpath(output)) return 2;

    int passed = 0, failed = 0;
    QJsonArray assertions;
    const auto check = [&](bool good, const QString &name)
    {
        good ? ++passed : ++failed;
        assertions.append(QJsonObject{{"name", name}, {"passed", good}});
        qInfo().noquote() << (good ? "PASS" : "FAIL") << name;
        return good;
    };
    ProjectsStore::registerTypes();
    QThread worker;
    auto *store = new ProjectsStore;
    store->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, store, &QObject::deleteLater);
    auto *page = new ProjectPage(store);
    AccountSession engineer;
    engineer.name = "rkReuseGuard";
    engineer.role = 4;
    engineer.authenticated = true;
    engineer.source = AccountSessionSource::Simulated;
    page->setSession(engineer);
    QScrollArea host;
    host.setWidget(page);
    host.setWidgetResizable(false);
    host.resize(1600, 900);
    host.show();
    auto *advanced = page->findChild<ProjectAdvancedEditor *>();
    auto *table = page->findChild<QTableWidget *>("sampleRateTable");
    auto *list = page->findChild<QTableWidget *>("lstProject");
    int loadCount = 0, saveCount = 0;
    bool lastSaveOK = false;
    QMap<qint64, bool> receipts;
    QObject::connect(page, &ProjectPage::operationFinished, &app, [&](const QString &operation, bool good)
    {
        if (operation == "load") ++loadCount;
        if (operation == "save") { ++saveCount; lastSaveOK = good; }
    });
    QObject::connect(store, &ProjectsStore::succeeded, &app,
                     [&](qint64 id, const QString &, qint64) { receipts.insert(id, true); });
    QObject::connect(store, &ProjectsStore::error, &app,
                     [&](qint64 id, const QString &, const QString &) { receipts.insert(id, false); });
    QTimer dismissError;
    QObject::connect(&dismissError, &QTimer::timeout, &app, [&]()
    {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            mouseClick(box->button(QMessageBox::Ok));
    });
    dismissError.start(30);
    const auto click = [&](const char *name)
    {
        auto *button = page->findChild<QWidget *>(QString::fromLatin1(name));
        if (button) host.ensureWidgetVisible(button);
        return mouseClick(button);
    };
    const auto firstCell = [&]() -> QWidget *
    { return table && table->rowCount() > 0 ? table->cellWidget(0, 0) : nullptr; };
    const auto selectProject = [&](qint64 id)
    {
        if (!list || page->isBusy() || page->hasUnsavedChanges()) return false;
        for (int row = 0; row < list->rowCount(); ++row)
            if (list->item(row, 0)->data(Qt::UserRole).toLongLong() == id)
            {
                list->scrollToItem(list->item(row, 1));
                host.ensureWidgetVisible(list);
                return mouseClick(list->viewport(), list->visualItemRect(list->item(row, 1)).center());
            }
        return false;
    };
    const auto reload = [&]()
    {
        const int previous = loadCount;
        page->reload();
        return waitFor([&]() { return loadCount > previous && !page->isBusy(); });
    };
    const auto saveLis = [&](const QString &code)
    {
        if (!click("btnBaseInfo")) return false;
        auto *edit = page->findChild<QLineEdit *>("project_lis_code");
        if (edit) host.ensureWidgetVisible(edit);
        if (!typeText(edit, code)) return false;
        const int previousSave = saveCount, previousLoad = loadCount;
        const bool clicked = click("btnSave");
        return clicked && waitFor([&]()
        { return saveCount == previousSave + 1 && loadCount > previousLoad && !page->isBusy(); }) && lastSaveOK;
    };

    worker.start();
    QMetaObject::invokeMethod(store, [=]() { store->initializeOriginal(root); }, Qt::QueuedConnection);
    const bool loaded = check(waitFor([&]()
    { return page->isReady() && !page->isBusy() && page->projects().size() == 7; }),
                             QStringLiteral("七原项目在正式页面加载"));
    ProjectRecord pt, aptt;
    for (const auto &project : page->projects())
    {
        if (project.name == "PT") pt = project;
        if (project.name == "APTT") aptt = project;
    }
    const bool usable = check(loaded && advanced && table && list && pt.id > 0 && aptt.id > 0,
                              QStringLiteral("高级编辑器与原PT/APTT检查对象齐全"));
    if (usable && check(selectProject(pt.id), QStringLiteral("实际点击选中PT")))
    {
        settle();
        const ProjectAdvanced storedBefore = pt.advanced;
        const ProjectAdvanced uiBefore = advanced->advanced();
        check(visibleValuesMatch(uiBefore, storedBefore), QStringLiteral("PT全部高级编辑字段与提交快照对应"));
        QPointer<QWidget> originalCell = firstCell();
        const bool hasCell = check(!originalCell.isNull(), QStringLiteral("保存前记录原高级表首格对象"));
        if (hasCell)
        {
            check(saveLis(pt.lisCode == "GUARD1" ? "GUARD2" : "GUARD1"), QStringLiteral("真实页面仅改LIS代码并保存读回"));
            check(!originalCell.isNull() && originalCell.data() == firstCell(), QStringLiteral("同项目不变高级区保存后复用原控件"));
            check(sameAdvanced(advanced->advanced(), uiBefore) &&
                  sameAdvanced(findProject(page->projects(), pt.id).advanced, storedBefore),
                  QStringLiteral("复用后高级完整UI快照及原存储快照不变"));
            check(reload(), QStringLiteral("同项目明确重新读取"));
            check(!originalCell.isNull() && originalCell.data() == firstCell() && sameAdvanced(advanced->advanced(), uiBefore),
                  QStringLiteral("明确重读也保留原高级控件和全部高级值"));
            check(saveLis(pt.lisCode), QStringLiteral("恢复原LIS代码并保存读回"));
            check(!originalCell.isNull() && originalCell.data() == firstCell() && !page->hasUnsavedChanges(),
                  QStringLiteral("恢复基础参数后仍复用且没有残留草稿"));

            // 沿用原UI的历史对象名：样本稀释动作表在第2页“稀释比例”，
            // btnSampleAdd实际打开第1页“测试方案”，其中没有sampleRateTable。
            check(click("btnReagentAdd") && table->isVisible(), QStringLiteral("打开含样本动作表的原稀释比例页"));
            auto *amount = qobject_cast<QLineEdit *>(table->cellWidget(0, 3));
            if (amount) host.ensureWidgetVisible(amount);
            const QString oldAmount = amount ? amount->text() : QString();
            const QString changedAmount = oldAmount == "1" ? QStringLiteral("2") : QStringLiteral("1");
            check(amount && amount->isVisible() && amount->isEnabled() && !amount->isReadOnly(),
                  QStringLiteral("样本量输入框可见且允许编辑"));
            const bool edited = check(typeText(amount, changedAmount) && page->hasUnsavedChanges() &&
                                      !sameAdvanced(advanced->advanced(), uiBefore),
                                      QStringLiteral("实际修改样本量形成高级草稿"));
            if (edited)
            {
                QPointer<QWidget> draftCell = firstCell();
                check(click("btnCancel"), QStringLiteral("点击取消高级草稿"));
                check(waitFor([&]() { return draftCell.isNull(); }) && firstCell(),
                      QStringLiteral("取消后旧高级控件释放并完整重建"));
                check(sameAdvanced(advanced->advanced(), uiBefore) && !page->hasUnsavedChanges() &&
                      sameAdvanced(findProject(page->projects(), pt.id).advanced, storedBefore),
                      QStringLiteral("取消恢复全部高级原值且不改变已提交快照"));
            }

            QPointer<QWidget> beforeSwitch = firstCell();
            check(selectProject(aptt.id), QStringLiteral("实际列表切换至APTT"));
            check(waitFor([&]() { return beforeSwitch.isNull(); }) && firstCell(),
                  QStringLiteral("不同项目不能复用PT高级控件"));
            const ProjectAdvanced apttUi = advanced->advanced();
            check(sameAdvanced(findProject(page->projects(), aptt.id).advanced, aptt.advanced) &&
                  visibleValuesMatch(apttUi, aptt.advanced) &&
                  page->findChild<QLineEdit *>("project_en_name")->text() == "APTT",
                  QStringLiteral("APTT全部高级字段与APTT提交快照对应"));
            QPointer<QWidget> apttCell = firstCell();
            check(selectProject(pt.id), QStringLiteral("实际列表切回PT"));
            check(waitFor([&]() { return apttCell.isNull(); }) && sameAdvanced(advanced->advanced(), uiBefore),
                  QStringLiteral("切回PT重建并恢复全部PT高级值"));

            // 以下手工存储请求只放在页面写操作之后，避免改变页面后续写号的顺序。
            // 仍调用正式导出/切源接口，由真实ready/load信号驱动正式页面，不搬原患者库。
            const QString secondRoot = QDir(output).filePath("exported_configuration");
            qint64 request = 1000000;
            QMetaObject::invokeMethod(store, [=]() { store->exportOriginalBundle(request, 0, secondRoot); }, Qt::QueuedConnection);
            const bool exported = check(waitFor([&]() { return receipts.contains(request); }) && receipts.value(request),
                                        QStringLiteral("正式导出生成同原结构的第二配置根"));
            if (exported)
            {
                const int beforeLoad = loadCount;
                QPointer<QWidget> oldRootCell = firstCell();
                ++request;
                QMetaObject::invokeMethod(store, [=]() { store->openOriginalDirectory(request, secondRoot); }, Qt::QueuedConnection);
                const bool switched = check(waitFor([&]()
                { return receipts.contains(request) && loadCount > beforeLoad && !page->isBusy(); }) && receipts.value(request),
                                            QStringLiteral("通过正式接口切换导出配置根并实际读回页面"));
                if (switched)
                {
                    check(waitFor([&]() { return oldRootCell.isNull(); }) && firstCell(),
                          QStringLiteral("同项目同字节但不同来源必须释放旧高级控件"));
                    const ProjectRecord currentPt = findProject(page->projects(), pt.id);
                    check(page->property("originalRoot").toString() == QFileInfo(secondRoot).canonicalFilePath() &&
                          currentPt.originalRoot == QFileInfo(secondRoot).canonicalFilePath() &&
                          sameAdvanced(currentPt.advanced, storedBefore) && sameAdvanced(advanced->advanced(), uiBefore),
                          QStringLiteral("切源后完整高级快照一致且页面与记录都绑定新根"));
                }
                const int beforeReturn = loadCount;
                QPointer<QWidget> secondRootCell = firstCell();
                ++request;
                QMetaObject::invokeMethod(store, [=]() { store->openOriginalDirectory(request, root); }, Qt::QueuedConnection);
                const bool returned = check(waitFor([&]()
                { return receipts.contains(request) && loadCount > beforeReturn && !page->isBusy(); }) && receipts.value(request),
                                           QStringLiteral("正式接口切回原测试根并读回"));
                if (returned)
                    check(waitFor([&]() { return secondRootCell.isNull(); }) &&
                          page->property("originalRoot").toString() == QFileInfo(root).canonicalFilePath() &&
                          sameAdvanced(advanced->advanced(), uiBefore) && !page->hasUnsavedChanges(),
                          QStringLiteral("切回原根重建并恢复全部高级值且无草稿"));
            }
        }
    }
    bool closed = false;
    QObject::connect(store, &ProjectsStore::closed, &app, [&]() { closed = true; });
    QMetaObject::invokeMethod(store, "close", Qt::QueuedConnection);
    check(waitFor([&]() { return closed; }), QStringLiteral("工作线程关闭原库"));
    worker.quit();
    check(worker.wait(3000), QStringLiteral("工作线程正常结束"));
    dismissError.stop();
    host.close();
    const QJsonObject report{{"passed", passed}, {"failed", failed}, {"assertions", assertions},
        {"instrumentActions", 0}, {"sqlBusinessWrites", 0}, {"formalSourceModified", false},
        {"root", root}, {"scope", QStringLiteral("实际页面基础保存/重读复用；高级草稿取消、切项目和切原根必须重建。只导出配置，不读取患者或账户。")}};
    QFile result(output + "/reuse_guard.json");
    if (!result.open(QIODevice::WriteOnly) || result.write(QJsonDocument(report).toJson()) <= 0) return 3;
    qInfo() << "REUSE_GUARD_RESULT" << passed << failed;
    return failed ? 1 : 0;
}
