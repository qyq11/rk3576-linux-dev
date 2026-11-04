// 只在获准的RK时段运行：与本次正式main及完整业务对象一起链接。
// 通过真实控件核“先新增普通架，再打开设置”，不创建账户、资料或设备回包。
#include <QApplication>
#include <QAbstractButton>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QTableWidget>
#include <QWindow>
#include <QtTest/QTest>
#include <algorithm>
#include <memory>
#include "shell/frmmain.h"
#include "samples/sample_page.h"
#include "samples/sample_widgets.h"
#include "samples/cuscombox_base.h"
#include "instrument/device_control.h"
#include "original_files/original_runtime_settings.h"
#include "protocol/frame_codec.h"
#include "ui/cusdialogmask.h"
#include "ui/maskdlglogin.h"

#ifndef SN100_SAMPLE_ADD_RELEASE
#error This checker must be bound to the full repaired ARM64 build.
#endif

namespace
{
QByteArray fileHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray();
}

// 用于比较既有架未改动；只保存摘要，不输出样本号、条码或患者内容。
QByteArray rackHash(const SampleRack &rack)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_5_15);
    out << rack.id << rack.name << rack.emergency << rack.holes.size();
    for (const auto &hole : rack.holes)
    {
        out << hole.number << hole.sampleId << hole.container << hole.revise << hole.sampleType << hole.tasks.size();
        const auto &quality = hole.quality.selected;
        out << hole.quality.slotToken << hole.quality.changeToken << quality.datasetId << quality.controlId
            << quality.batchId << quality.controlName << quality.batchName << quality.sampleId << quality.projects.size();
        for (const auto &project : quality.projects) out << project.associationId << project.projectId;
        const auto &cal = hole.calibration;
        out << cal.projectId << cal.groupToken << cal.calibratorBatch << cal.reagentBatchGroup << cal.autoDilution
            << cal.pointCount << cal.repeatCount << cal.baselineConcentration << cal.rates << cal.concentrations << cal.holes;
        for (const auto &task : hole.tasks)
        {
            out << task.projectId << task.rate << task.repeats << task.prolonged
                << task.calibrationPoint << task.calibrationConcentration << task.states.size();
            for (const auto state : task.states) out << static_cast<qint32>(state);
        }
    }
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

class SampleAddReview : public QObject
{
public:
    explicit SampleAddReview(QObject *parent) : QObject(parent)
    {
        output_ = qEnvironmentVariable("SN100_SAMPLE_ADD_REVIEW_DIR");
        clock_.start();
        timer_.setInterval(25);
        QObject::connect(&timer_, &QTimer::timeout, this, [this]() { tick(); });
        timer_.start();
    }

private:
    enum class Step
    {
        Startup, Login, AwaitLogin, OpenSamples, Overview, AddFirst, WaitFirst,
        AddSecond, WaitSecond, SelectFirst, CheckSelection, ReselectSecond,
        OpenFirst, SecondDoubleClickPress, WaitOpenFirst,
        ReturnFirst, WaitReturnFirst, SelectSecond, OpenSecond, WaitOpenSecond,
        OpenContainer, PickOtherContainer, AddThirdWithDraft, WaitThirdCancel,
        ReturnSecond, WaitReturnSecond, CleanupSelect, CleanupDelete, CleanupWait, Complete
    };
    QTimer timer_;
    QElapsedTimer clock_, stepClock_, responseClock_;
    QPointer<frmMain> shell_;
    QPointer<SamplePage> page_;
    QPointer<DeviceControl> observedControl_;
    AccountSession observedSession_, boundSession_;
    QString output_, currentStep_ = QStringLiteral("startup");
    QString originalRoot_;
    QByteArray configHash_, themeHash_;
    QMap<qint64, QByteArray> beforeRacks_;
    QJsonArray checks_, responseTimes_, txTypes_;
    QSet<QString> observedTxTypes_;
    QList<qint64> created_;
    QStringList names_;
    QVector<qint64> intervals_;
    Sn100Protocol::FrameDecoder txDecoder_{Sn100Protocol::Direction::HostToDevice};
    Step step_ = Step::Startup;
    int expectedHoles_ = 0, expectedContainer_ = 1, failed_ = 0, initialCount_ = 0, cleanupIndex_ = -1;
    qint64 previousTick_ = 0;
    qint64 txObservationStartedMs_ = -1;
    quint64 connection_ = 0, identityRevision_ = 0, boundIdentityRevision_ = 0;
    bool identityBound_ = false, outputReady_ = false;
    bool entered_ = false, inTick_ = false, finished_ = false, forbiddenTx_ = false, questionAnswered_ = false;

    void check(bool ok, const QString &name)
    {
        checks_.append(QJsonObject{{"name", name}, {"passed", ok}});
        if (!ok) ++failed_;
        qInfo().noquote() << (ok ? "SAMPLE_ADD_PASS" : "SAMPLE_ADD_FAIL") << name;
    }
    void advance(Step next, const QString &name)
    {
        step_ = next; currentStep_ = name; entered_ = false; stepClock_.restart();
    }
    QWidget *editor() const { return page_ ? page_->findChild<QWidget *>("wgtSampleSetting") : nullptr; }
    QListWidget *list() const { return page_ ? page_->findChild<QListWidget *>("rackList") : nullptr; }
    MaskDlgLogin *login() const
    {
        if (shell_) for (auto *item : shell_->findChildren<MaskDlgLogin *>()) if (item->isVisible()) return item;
        return nullptr;
    }
    bool loginReady() const
    {
        auto *dialog = login();
        auto *mask = dialog ? dialog->findChild<CusDialogMask *>("mask") : nullptr;
        auto *button = dialog ? dialog->findChild<QPushButton *>("btnLogin") : nullptr;
        return dialog && !dialog->isBusy() && mask && mask->isReadyForInput() && button && button->isEnabled();
    }
    static bool sameIdentity(const AccountSession &a, const AccountSession &b)
    {
        return a.authenticated == b.authenticated && a.name == b.name && a.role == b.role && a.source == b.source
            && a.roleLabel == b.roleLabel && a.originalPolicySource == b.originalPolicySource && a.grantedPowerIds == b.grantedPowerIds;
    }
    bool contextReady() const
    {
        if (!shell_ || !page_ || !observedControl_ || shell_->deviceControl() != observedControl_
            || observedControl_->connectionGeneration() != connection_) return false;
        const auto &state = observedControl_->state();
        if (!state.connected || !state.fresh || state.status.mainStatus != 0 || state.outcomeUnknown || !state.pending.isEmpty()
            || !page_->originalProfile() || page_->property("originalRoot").toString() != originalRoot_) return false;
        if (identityBound_ && (!sameIdentity(shell_->accountSession(), boundSession_)
            || identityRevision_ != boundIdentityRevision_ || !shell_->accountSession().authenticated
            || shell_->accountSession().role != 0 || shell_->accountSession().source != AccountSessionSource::Configured)) return false;
        return true;
    }
    void bindIdentity()
    {
        boundSession_ = shell_->accountSession();
        boundIdentityRevision_ = identityRevision_;
        identityBound_ = true;
    }
    bool click(QWidget *widget, bool doubleClick = false)
    {
        if (!contextReady() || !widget || !widget->isVisible() || !widget->isEnabled() || QApplication::activeModalWidget()) return false;
        const QPoint position = widget->rect().center();
        // 首个press可能重建列表；通过稳定QWindow命中，不给已退休QWidget补发release。
        QPointer<QWindow> window = widget->window()->windowHandle();
        if (!window) return false;
        const QPoint windowPosition = window->mapFromGlobal(widget->mapToGlobal(position));
        if (doubleClick) QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, windowPosition, 5);
        else QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, windowPosition, 5);
        return true;
    }
    bool pageClick(const char *name) { return click(page_ ? page_->findChild<QWidget *>(name) : nullptr); }
    int rackIndex(qint64 id) const
    {
        if (page_)
        {
            const auto snapshot = page_->snapshot();
            for (int n = 0; n < snapshot.racks.size(); ++n) if (snapshot.racks[n].id == id) return n;
        }
        return -1;
    }
    QWidget *card(qint64 id) const
    {
        const int n = rackIndex(id);
        auto *rows = list();
        if (!rows || n < 0 || n >= rows->count()) return nullptr;
        // 原有架较多时，先滚动到本轮目标行，避免窗口命中落在被裁切区域外。
        rows->scrollToItem(rows->item(n), QAbstractItemView::EnsureVisible);
        rows->doItemsLayout();
        return rows->itemWidget(rows->item(n));
    }
    bool idle() const
    { return page_ && page_->isReady() && !page_->isBusy() && !page_->hasUnsavedChanges(); }
    bool selected(qint64 id) const
    { auto *rows = list(); return rows && rows->currentRow() == rackIndex(id) && rackIndex(id) >= 0; }
    CusCombox_Base *firstContainer() const
    {
        auto *table = page_ ? page_->findChild<QTableWidget *>("tblSetting") : nullptr;
        return table && table->rowCount() > 0 ? qobject_cast<CusCombox_Base *>(table->cellWidget(0, 3)) : nullptr;
    }
    bool isCreatedEmpty(qint64 id) const
    {
        if (!page_ || !created_.contains(id)) return false;
        const auto snapshot = page_->snapshot();
        for (const auto &rack : snapshot.racks) if (rack.id == id)
        {
            if (rack.emergency || rack.name != names_.value(created_.indexOf(id)) || rack.holes.size() != expectedHoles_) return false;
            for (const auto &hole : rack.holes)
            {
                const auto &quality = hole.quality.selected;
                const auto &cal = hole.calibration;
                if (!hole.sampleId.isEmpty() || !hole.tasks.isEmpty() || hole.sampleType != 0 || !hole.revise
                    || hole.container != expectedContainer_ || !hole.quality.slotToken.isEmpty() || !hole.quality.changeToken.isEmpty()
                    || !quality.datasetId.isEmpty() || quality.controlId || quality.batchId || !quality.controlName.isEmpty()
                    || !quality.batchName.isEmpty() || !quality.sampleId.isEmpty() || !quality.projects.isEmpty()
                    || cal.projectId || !cal.groupToken.isEmpty() || !cal.calibratorBatch.isEmpty()
                    || cal.reagentBatchGroup != SampleCalibrationPreset().reagentBatchGroup || cal.autoDilution
                    || cal.pointCount || cal.repeatCount || !cal.baselineConcentration.isEmpty()
                    || !cal.rates.isEmpty() || !cal.concentrations.isEmpty() || !cal.holes.isEmpty()) return false;
            }
            return true;
        }
        return false;
    }
    QString nextName() const
    {
        QSet<QString> names;
        if (page_) for (const auto &rack : page_->snapshot().racks) names.insert(rack.name);
        for (int n = 1; n <= 98; ++n)
        {
            const QString candidate = QStringLiteral("S%1").arg(n, 2, 10, QLatin1Char('0'));
            if (!names.contains(candidate)) return candidate;
        }
        return {};
    }
    bool rememberNewRack(int wantedCount, const QString &name, bool allowDirty = false)
    {
        if (!page_ || !page_->isReady() || page_->isBusy() || (!allowDirty && page_->hasUnsavedChanges())) return false;
        const auto snapshot = page_->snapshot();
        if (snapshot.racks.size() != wantedCount) return false;
        qint64 found = 0;
        for (const auto &rack : snapshot.racks)
            if (!beforeRacks_.contains(rack.id) && !created_.contains(rack.id))
            {
                if (found || rack.emergency || rack.name != name || rack.id <= 0) return false;
                found = rack.id;
            }
        if (!found) return false;
        created_.append(found);
        return isCreatedEmpty(found);
    }
    void timing(const QString &action)
    { responseTimes_.append(QJsonObject{{"action", action}, {"ms", responseClock_.elapsed()}}); }

    // 只回答本专项步骤明确触发的原样本提示；遇到别的弹窗均不代答。
    void armQuestion(const QString &text, QMessageBox::StandardButton choice, qint64 id, bool dirty)
    {
        questionAnswered_ = false;
        auto *answer = new QTimer(this);
        auto started = std::make_shared<QElapsedTimer>(); started->start();
        answer->setInterval(10);
        QObject::connect(answer, &QTimer::timeout, this, [this, answer, started, text, choice, id, dirty]()
        {
            if (finished_) { answer->stop(); answer->deleteLater(); return; }
            if (forbiddenTx_)
            {
                answer->stop(); answer->deleteLater();
                finish(false, QStringLiteral("确认框等待期间已观察到任务或动作报文，停止代答并保留弹窗。")); return;
            }
            // confirm的exec会嵌套事件循环，主tick可能仍持inTick_；这里必须独立封口超时。
            if (started->elapsed() > 9000)
            {
                answer->stop(); answer->deleteLater();
                finish(false, QStringLiteral("确认框等待超过9秒，专项失败并保留弹窗；未关闭程序。")); return;
            }
            if (!contextReady())
            {
                answer->stop(); answer->deleteLater();
                finish(false, QStringLiteral("确认框等待期间原会话/连接或动作状态变化，未代答。")); return;
            }
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box->parentWidget() != page_ || box->objectName() != QStringLiteral("sampleDiscardDialog")
                || box->text() != text || !selected(id) || !isCreatedEmpty(id)
                || !page_ || page_->hasUnsavedChanges() != dirty) return;
            auto *button = box->button(choice);
            if (button && button->isEnabled())
            {
                answer->stop(); answer->deleteLater();
                questionAnswered_ = true;
                QWindow *window = button->window()->windowHandle();
                if (!window) { finish(false, QStringLiteral("确认框没有实际窗口句柄，未代答。")); return; }
                if (forbiddenTx_)
                { finish(false, QStringLiteral("确认框点击前已观察到任务或动作报文，未代答。")); return; }
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    window->mapFromGlobal(button->mapToGlobal(button->rect().center())), 5);
            }
        });
        answer->start();
    }
    bool requestDelete(qint64 id)
    {
        if (!idle() || !selected(id) || !isCreatedEmpty(id) || !editor() || !editor()->isHidden()) return false;
        armQuestion(QStringLiteral("删除当前本地预设架？不会取消设备任务。"), QMessageBox::Discard, id, false);
        return pageClick("btnDelRack");
    }

    void finish(bool completed, const QString &reason)
    {
        if (finished_) return;
        finished_ = true; timer_.stop();
        if (!completed) check(false, reason);
        if (!outputReady_)
        {
            qCritical() << "SAMPLE_ADD_REVIEW_OUTPUT_UNAVAILABLE_NOT_WRITTEN";
            qApp->setProperty("sampleAddReviewComplete", true);
            return;
        }
        std::sort(intervals_.begin(), intervals_.end());
        QJsonArray created;
        for (int n = 0; n < created_.size(); ++n)
            created.append(QJsonObject{{"id", created_[n]}, {"name", names_.value(n)},
                {"still_in_snapshot", rackIndex(created_[n]) >= 0}});
        const int code = completed && failed_ == 0 ? 0 : 1;
        QJsonObject result{{"schema", "sn100-sample-add-order-review-v1"}, {"board", "board_a"},
            {"release_id", QStringLiteral(SN100_SAMPLE_ADD_RELEASE)}, {"completed", completed},
            {"check_exit", code}, {"failed", failed_}, {"checks", checks_}, {"last_step", currentStep_},
            {"reason", reason}, {"created_racks", created}, {"initial_rack_count", initialCount_},
            {"expected_ordinary_holes", expectedHoles_}, {"expected_default_container", expectedContainer_},
            {"configuration_unchanged", fileHash(QDir(originalRoot_).filePath("config.ini")) == configHash_
                && fileHash(QDir(originalRoot_).filePath("theme_config.ini")) == themeHash_},
            {"observed_tx_types", txTypes_}, {"watched_task_or_motion_tx_observed", forbiddenTx_},
            {"tx_observation_started_ms", txObservationStartedMs_},
            {"tx_observation_scope", "wireActivity中已入队的实际报文；仅Ts/Tp/Tt或M前缀触发停止；不能阻发或证明全部业务副作用不存在"},
            {"existing_rack_hash_scope", "含孔、任务、质控和校准身份；动态remainingSeconds/remainingAtMs不作冻结比较"},
            {"programmatic_qttest_widget_events", true}, {"physical_mouse_keyboard_verified", false},
            {"physical_screen_verified", false}, {"real_instrument_detection_verified", false},
            {"application_close_requested_by_checker", false}, {"application_exit_verified", false},
            {"operation_response_ms", responseTimes_}, {"duration_ms", clock_.elapsed()},
            {"event_loop_probe", QJsonObject{{"period_ms", 25}, {"samples", intervals_.size()},
                {"interval_p95_ms", intervals_.isEmpty() ? 0 : intervals_[(intervals_.size() - 1) * 95 / 100]},
                {"interval_max_ms", intervals_.isEmpty() ? 0 : intervals_.last()}}}};
        QSaveFile resultFile(QDir(output_).filePath("result.json"));
        const bool saved = resultFile.open(QIODevice::WriteOnly)
            && resultFile.write(QJsonDocument(result).toJson()) > 0 && resultFile.commit();
        QSaveFile exitFile(QDir(output_).filePath("check_exit.txt"));
        const bool exitSaved = exitFile.open(QIODevice::WriteOnly)
            && exitFile.write(QByteArray::number(saved ? code : 2) + '\n') > 0 && exitFile.commit();
        qInfo() << "SAMPLE_ADD_REVIEW_COMPLETE check_exit=" << code << "saved=" << saved << "exit_saved=" << exitSaved;
        qApp->setProperty("sampleAddReviewComplete", true);
        // 不退出或关闭程序。实际关闭与APP_EXIT是否取得由唯一总控另行决定。
    }

    void tick()
    {
        if (finished_ || inTick_) return;
        inTick_ = true;
        struct TickGuard { bool &flag; ~TickGuard() { flag = false; } } unguard{inTick_};
        const qint64 now = clock_.elapsed();
        if (previousTick_ && intervals_.size() < 8192) intervals_.append(now - previousTick_);
        previousTick_ = now;
        if (!stepClock_.isValid()) stepClock_.start();
        if (now > 120000 || stepClock_.elapsed() > (step_ == Step::Startup ? 60000 : 10000))
        { finish(false, QStringLiteral("等待当前步骤超时；保留现场，不强关、不自动放弃其他页面。")); return; }
        if (forbiddenTx_) { finish(false, QStringLiteral("观察到任务或动作命令；停止页面自动化并保留现场。")); return; }
        if (step_ != Step::Startup && !contextReady())
        { finish(false, QStringLiteral("实际原会话、连接代次、空闲/待核/等待命令状态变化，停止操作并留现场。")); return; }
        switch (step_)
        {
        case Step::Startup:
        {
            if (!QDir::isAbsolutePath(output_) || !QFileInfo(output_).isDir()
                || QFileInfo(output_).canonicalFilePath() != output_)
            { finish(false, QStringLiteral("缺少本次专用绝对输出目录。")); return; }
            if (QFileInfo::exists(QDir(output_).filePath("result.json"))
                || QFileInfo::exists(QDir(output_).filePath("check_exit.txt")))
            { qCritical() << "SAMPLE_ADD_REVIEW_OUTPUT_ALREADY_EXISTS"; timer_.stop(); finished_ = true; return; }
            outputReady_ = true;
            if (!shell_)
            {
                for (QWidget *widget : QApplication::allWidgets())
                    if (auto *candidate = qobject_cast<frmMain *>(widget)) { shell_ = candidate; break; }
            }
            if (!shell_ || !shell_->deviceControl()) return;
            if (!observedControl_)
            {
                observedControl_ = shell_->deviceControl();
                observedSession_ = shell_->accountSession();
                QObject::connect(shell_, &frmMain::sessionChanged, this, [this](const AccountSession &session)
                { if (!sameIdentity(observedSession_, session)) ++identityRevision_; observedSession_ = session; });
                txObservationStartedMs_ = clock_.elapsed();
                QObject::connect(shell_->deviceControl(), &DeviceControl::wireActivity, this,
                    [this](const QString &direction, const QByteArray &bytes)
                {
                    if (!direction.startsWith("TX") || finished_) return;
                    const auto decoded = txDecoder_.feed(bytes, clock_.elapsed());
                    for (const auto &message : decoded.messages)
                    {
                        const QString kind = QString::fromLatin1(message.left(2));
                        if (!observedTxTypes_.contains(kind)) { observedTxTypes_.insert(kind); txTypes_.append(kind); }
                        if (kind == "Ts" || kind == "Tp" || kind == "Tt" || kind.startsWith('M')) forbiddenTx_ = true;
                    }
                });
            }
            page_ = shell_->samplePage();
            auto *splash = shell_->window()->findChild<QWidget *>("startupSplash");
            auto *control = shell_->deviceControl();
            if (!page_ || !page_->isReady() || page_->isBusy() || !page_->originalProfile()
                || !splash || splash->isVisible() || !control->state().connected || !control->state().fresh) return;
            if (control->state().status.mainStatus != 0)
            { finish(false, QStringLiteral("需要实际主控空闲状态，未在检测或暂停任务中操作。")); return; }
            QFile serial("/proc/device-tree/serial-number");
            QByteArray serialValue;
            if (serial.open(QIODevice::ReadOnly)) { serialValue = serial.readAll(); while (serialValue.endsWith('\0')) serialValue.chop(1); }
            if (serialValue.trimmed() != "3bf8222fe1417f6a")
            { finish(false, QStringLiteral("板A序列号不匹配。")); return; }
            originalRoot_ = page_->property("originalRoot").toString();
            if (!QDir::isAbsolutePath(originalRoot_) || QFileInfo(originalRoot_).canonicalFilePath() != originalRoot_)
            { finish(false, QStringLiteral("无法确认本次原资料根。")); return; }
            configHash_ = fileHash(QDir(originalRoot_).filePath("config.ini"));
            themeHash_ = fileHash(QDir(originalRoot_).filePath("theme_config.ini"));
            OriginalRuntime::RuntimeSettings settings; QString ignored;
            if (configHash_.isEmpty() || themeHash_.isEmpty() || !OriginalRuntime::loadRuntimeSettings(originalRoot_, &settings, &ignored))
            { finish(false, QStringLiteral("当前实际INI只读解析失败；未输出配置或凭据。")); return; }
            expectedHoles_ = settings.theme.sampleHoles;
            expectedContainer_ = settings.user.containerIsTube ? 0 : 1;
            connection_ = control->connectionGeneration();
            if (control->state().outcomeUnknown || !control->state().pending.isEmpty())
            { finish(false, QStringLiteral("实际控制命令仍等待或结果待核，未开始样本操作。")); return; }
            check(true, QStringLiteral("板A、原资料、实际主控连接及空闲状态已核对"));
            advance(Step::Login, "configured_login"); return;
        }
        case Step::Login:
            if (shell_->accountSession().authenticated)
            {
                if (shell_->accountSession().source != AccountSessionSource::Configured || shell_->accountSession().role != 0)
                { finish(false, QStringLiteral("需要原配置默认访客身份；不改现有账户或权限。")); return; }
                bindIdentity();
                advance(Step::OpenSamples, "open_sample_page"); return;
            }
            if (!loginReady()) return;
            if (!login()->findChild<QLineEdit *>("ldtPassword") || !login()->findChild<QLineEdit *>("ldtPassword")->text().isEmpty())
            { finish(false, QStringLiteral("真实登录卡不是空口令默认入口；不代填或清除已有凭据。")); return; }
            if (!contextReady()) { finish(false, QStringLiteral("登录点击前实际控制上下文已变化。")); return; }
            {
                auto *button = login()->findChild<QPushButton *>("btnLogin");
                QWindow *window = button->window()->windowHandle();
                if (!window) { finish(false, QStringLiteral("登录卡没有实际窗口句柄。")); return; }
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    window->mapFromGlobal(button->mapToGlobal(button->rect().center())));
            }
            advance(Step::AwaitLogin, "wait_real_guest_login"); return;
        case Step::AwaitLogin:
            if (!shell_->accountSession().authenticated || login()) return;
            if (shell_->accountSession().source != AccountSessionSource::Configured || shell_->accountSession().role != 0)
            { finish(false, QStringLiteral("空口令未得到配置默认角色0；停止操作。")); return; }
            bindIdentity();
            check(true, QStringLiteral("真实登录UI建立原配置角色0会话，未设置模拟身份"));
            advance(Step::OpenSamples, "open_sample_page"); return;
        case Step::OpenSamples:
            if (!entered_) { if (!click(shell_->findChild<QWidget *>("btnSampleAdptive"))) return; entered_ = true; }
            if (!page_->isVisible() || !idle()) return;
            advance(Step::Overview, "return_to_overview"); return;
        case Step::Overview:
            if (!editor() || !list()) { finish(false, QStringLiteral("样本总览控件缺失。")); return; }
            if (!editor()->isHidden()) { if (!pageClick("btnMenuReturn")) return; }
            if (!editor()->isHidden() || !idle()) return;
            for (const auto &rack : page_->snapshot().racks) beforeRacks_.insert(rack.id, rackHash(rack));
            initialCount_ = beforeRacks_.size();
            check(list()->count() == initialCount_, QStringLiteral("总览列表与后端原快照数量一致"));
            advance(Step::AddFirst, "add_first_normal_rack"); return;
        case Step::AddFirst:
            if (nextName().isEmpty()) { finish(false, QStringLiteral("普通架号预算已满；未删除已有架。")); return; }
            names_.append(nextName()); responseClock_.restart();
            if (!pageClick("btnAdd")) { finish(false, QStringLiteral("总览加号不可操作。")); return; }
            advance(Step::WaitFirst, "wait_first_snapshot"); return;
        case Step::WaitFirst:
            if (!rememberNewRack(initialCount_ + 1, names_[0])) return;
            timing("first_add_to_real_snapshot");
            check(editor()->isHidden() && selected(created_[0]) && list()->count() == initialCount_ + 1,
                QStringLiteral("第一次加号先新增普通空架并选中新行，仍留在总览"));
            if (!editor()->isHidden()) { finish(false, QStringLiteral("新增后错误打开设置区。")); return; }
            if (auto *view = card(created_[0])) view->grab().save(QDir(output_).filePath("first_new_empty_rack.png"));
            advance(Step::AddSecond, "add_second_normal_rack"); return;
        case Step::AddSecond:
            names_.append(nextName()); responseClock_.restart();
            if (names_[1].isEmpty() || !pageClick("btnAdd")) { finish(false, QStringLiteral("无法连续新增第二个普通架。")); return; }
            advance(Step::WaitSecond, "wait_second_snapshot"); return;
        case Step::WaitSecond:
            if (!rememberNewRack(initialCount_ + 2, names_[1])) return;
            timing("second_add_to_real_snapshot");
            check(editor()->isHidden() && selected(created_[1]) && list()->count() == initialCount_ + 2
                && isCreatedEmpty(created_[0]), QStringLiteral("无需先保存样本设置，即可连续新增两个不同的普通空架"));
            if (!editor()->isHidden()) { finish(false, QStringLiteral("第二次新增错误打开设置区。")); return; }
            if (auto *view = card(created_[1])) view->grab().save(QDir(output_).filePath("second_new_empty_rack.png"));
            advance(Step::SelectFirst, "single_select_first"); return;
        case Step::SelectFirst:
            if (!click(card(created_[0]))) { finish(false, QStringLiteral("第一新架行不可单击。")); return; }
            advance(Step::CheckSelection, "verify_selection_without_editor"); return;
        case Step::CheckSelection:
            if (!idle() || !selected(created_[0])) return;
            check(editor()->isHidden(), QStringLiteral("单击普通架只选中该行，不进入设置区"));
            if (!editor()->isHidden()) { finish(false, QStringLiteral("普通单击错误进入设置区。")); return; }
            advance(Step::ReselectSecond, "select_second_before_unselected_double_click"); return;
        case Step::ReselectSecond:
            if (!click(card(created_[1]))) { finish(false, QStringLiteral("双击前不能重新选中第二架。")); return; }
            advance(Step::OpenFirst, "first_click_of_unselected_double_click"); return;
        case Step::OpenFirst:
            if (!idle() || !selected(created_[1]) || !editor()->isHidden()) return;
            responseClock_.restart();
            if (!click(card(created_[0]))) { finish(false, QStringLiteral("未选中架的首个双击press/release不可操作。")); return; }
            // 退出本次tick，让Qt处理deleteLater；第二下重新命中已重建的真实卡片。
            advance(Step::SecondDoubleClickPress, "second_click_after_old_card_retired"); return;
        case Step::SecondDoubleClickPress:
            if (!idle() || !selected(created_[0]) || !editor()->isHidden()) return;
            // 匹配QtTest5.15.13：release人为加500ms防误双击；此处恢复连续双击时间线。
            QTest::lastMouseTimestamp -= QTest::mouseDoubleClickInterval;
            if (!click(card(created_[0]))) { finish(false, QStringLiteral("重建后第二下双击无法命中新架。")); return; }
            advance(Step::WaitOpenFirst, "wait_first_editor"); return;
        case Step::WaitOpenFirst:
            if (!idle() || editor()->isHidden()) return;
            timing("double_click_to_editor");
            check(selected(created_[0]) && isCreatedEmpty(created_[0]), QStringLiteral("双击未选中卡：首击重建退休后第二击仍打开对应新架，默认孔数/容器保持原INI"));
            advance(Step::ReturnFirst, "return_without_sample_settings"); return;
        case Step::ReturnFirst:
            if (!pageClick("btnMenuReturn")) { finish(false, QStringLiteral("新架设置返回按钮不可用。")); return; }
            advance(Step::WaitReturnFirst, "wait_return_preserves_racks"); return;
        case Step::WaitReturnFirst:
            if (!idle() || !editor()->isHidden()) return;
            check(rackIndex(created_[0]) >= 0 && rackIndex(created_[1]) >= 0 && list()->count() == initialCount_ + 2,
                QStringLiteral("未填写样本设置直接返回总览，两次新增的空架均保留"));
            advance(Step::SelectSecond, "single_select_second"); return;
        case Step::SelectSecond:
            if (!click(card(created_[1]))) { finish(false, QStringLiteral("第二新架行不可选中。")); return; }
            advance(Step::OpenSecond, "edit_button_for_second"); return;
        case Step::OpenSecond:
            if (!idle() || !selected(created_[1])) return;
            if (!pageClick("btnEditRack")) { finish(false, QStringLiteral("编辑架按钮不可用。")); return; }
            advance(Step::WaitOpenSecond, "wait_second_editor"); return;
        case Step::WaitOpenSecond:
            if (!idle() || editor()->isHidden()) return;
            check(selected(created_[1]) && isCreatedEmpty(created_[1]), QStringLiteral("编辑架按钮也能打开选中普通架，未错误进入急诊架"));
            advance(Step::OpenContainer, "open_own_empty_container_selector"); return;
        case Step::OpenContainer:
            if (!firstContainer() || firstContainer()->getCurrentIndex() != expectedContainer_
                || !click(firstContainer())) { finish(false, QStringLiteral("本轮空架的原容器选择器不可操作。")); return; }
            advance(Step::PickOtherContainer, "choose_other_container_as_unsaved_draft"); return;
        case Step::PickOtherContainer:
        {
            auto *container = firstContainer();
            if (!container) { finish(false, QStringLiteral("容器选择器已失效。")); return; }
            const QString target = expectedContainer_ == 0 ? QStringLiteral("生化杯") : QStringLiteral("试管");
            bool chosen = false;
            for (auto *button : container->findChildren<QAbstractButton *>())
                if (button->isVisible() && button->isEnabled() && button->text() == target)
                {
                    if (!contextReady()) { finish(false, QStringLiteral("容器选择前原控制上下文变化。")); return; }
                    QWindow *window = button->window()->windowHandle();
                    if (!window) { finish(false, QStringLiteral("容器下拉框没有实际窗口句柄。")); return; }
                    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                        window->mapFromGlobal(button->mapToGlobal(button->rect().center())));
                    chosen = true; break;
                }
            if (!chosen) return;
            check(page_->hasUnsavedChanges() && firstContainer() && firstContainer()->getCurrentIndex() != expectedContainer_
                && isCreatedEmpty(created_[1]), QStringLiteral("仅在本轮空架切换容器形成未保存草稿，后端原默认值未写入"));
            if (!page_->hasUnsavedChanges()) { finish(false, QStringLiteral("真实容器操作未产生待确认草稿。")); return; }
            advance(Step::AddThirdWithDraft, "add_before_canceling_draft_switch"); return;
        }
        case Step::AddThirdWithDraft:
            if (nextName().isEmpty()) { finish(false, QStringLiteral("第三个普通架号预算不足。")); return; }
            names_.append(nextName());
            armQuestion(QStringLiteral("当前样本设置未保存，是否放弃？"), QMessageBox::Cancel, created_[1], true);
            if (!pageClick("btnAdd")) { finish(false, QStringLiteral("已有草稿时加号不可操作。")); return; }
            advance(Step::WaitThirdCancel, "wait_added_rack_and_cancel_preserves_draft"); return;
        case Step::WaitThirdCancel:
            if (!questionAnswered_ || QApplication::activeModalWidget()) return;
            if (!rememberNewRack(initialCount_ + 3, names_[2], true)) return;
            check(!editor()->isHidden() && selected(created_[1]) && page_->hasUnsavedChanges()
                && firstContainer() && firstContainer()->getCurrentIndex() != expectedContainer_
                && isCreatedEmpty(created_[1]) && isCreatedEmpty(created_[2]),
                QStringLiteral("有未保存草稿时先新增第三架；取消切换仍保留新增架和原架容器草稿"));
            if (editor()->isHidden() || !selected(created_[1]) || !page_->hasUnsavedChanges())
            { finish(false, QStringLiteral("取消切换没有保留原编辑位置和草稿。")); return; }
            advance(Step::ReturnSecond, "discard_only_own_container_draft"); return;
        case Step::ReturnSecond:
            armQuestion(QStringLiteral("当前样本设置未保存，是否放弃？"), QMessageBox::Discard, created_[1], true);
            if (!pageClick("btnMenuReturn")) { finish(false, QStringLiteral("第二架返回按钮不可用。")); return; }
            advance(Step::WaitReturnSecond, "wait_second_overview"); return;
        case Step::WaitReturnSecond:
            if (!questionAnswered_ || !idle() || !editor()->isHidden()) return;
            check(isCreatedEmpty(created_[1]), QStringLiteral("放弃本轮容器草稿后，后端仍为原默认空架"));
            cleanupIndex_ = created_.size() - 1;
            advance(Step::CleanupSelect, "select_own_empty_rack_for_cleanup"); return;
        case Step::CleanupSelect:
            if (cleanupIndex_ < 0) { advance(Step::Complete, "complete"); return; }
            if (!click(card(created_[cleanupIndex_]))) { finish(false, QStringLiteral("本轮空架清理前不可选中。")); return; }
            advance(Step::CleanupDelete, "delete_only_own_empty_rack"); return;
        case Step::CleanupDelete:
            if (!idle() || !selected(created_[cleanupIndex_])) return;
            if (!requestDelete(created_[cleanupIndex_])) { finish(false, QStringLiteral("本轮架已变化或删除条件不符；未自动删除。")); return; }
            advance(Step::CleanupWait, "wait_own_empty_rack_removed"); return;
        case Step::CleanupWait:
            if (!questionAnswered_ || !idle() || rackIndex(created_[cleanupIndex_]) >= 0) return;
            check(page_->snapshot().racks.size() == initialCount_ + cleanupIndex_,
                QStringLiteral("真实UI仅删除本轮创建且仍为空的架%1").arg(cleanupIndex_ + 1));
            if (!editor()->isHidden()) { finish(false, QStringLiteral("删除后未保持总览。")); return; }
            --cleanupIndex_;
            advance(Step::CleanupSelect, "select_next_own_empty_rack_for_cleanup"); return;
        case Step::Complete:
        {
            const auto snapshot = page_->snapshot();
            bool unchanged = snapshot.racks.size() == beforeRacks_.size();
            for (const auto &rack : snapshot.racks) unchanged = unchanged && beforeRacks_.value(rack.id) == rackHash(rack);
            check(unchanged, QStringLiteral("原有普通架、急诊架及其样本任务内容未改动，列表恢复原数量"));
            check(editor()->isHidden() && list()->count() == initialCount_, QStringLiteral("专项结束留在原样本总览，无未保存编辑"));
            check(fileHash(QDir(originalRoot_).filePath("config.ini")) == configHash_
                && fileHash(QDir(originalRoot_).filePath("theme_config.ini")) == themeHash_, QStringLiteral("实际原INI和主题配置指纹保持"));
            check(!forbiddenTx_, QStringLiteral("观察区间未见Ts/Tp/Tt及M前缀报文；其他实际TX种类单独记录"));
            finish(true, QStringLiteral("完成程序内控件事件检查；实体键鼠、实屏与仪器检测另验。")); return;
        }
        }
    }
};

void scheduleSampleAddReview()
{
    if (qEnvironmentVariableIsEmpty("SN100_SAMPLE_ADD_REVIEW_DIR")) return;
    QTimer::singleShot(0, qApp, []() { new SampleAddReview(qApp); });
}
}
Q_COREAPP_STARTUP_FUNCTION(scheduleSampleAddReview)
