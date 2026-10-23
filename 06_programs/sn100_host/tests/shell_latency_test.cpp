// O03-2 latency attribution. Product layout, source animations and worker rules
// remain unchanged. The runner supplies a frozen copy of the O03-2 test helpers.
#include "latency_support.h"

#include <QBuffer>
#include <QMutex>
#include <QMutexLocker>
#include <QPixmap>
#include <QStyle>

// Qt5's installed qtestmouse/qtestkeyboard headers declare these exported
// helpers inside inline function blocks. Repeat their existing ABI declarations
// at namespace scope only to report the diagnostic driver's actual defaults.
namespace QTest
{
extern int Q_TESTLIB_EXPORT defaultMouseDelay();
extern int Q_TESTLIB_EXPORT defaultKeyDelay();
}

namespace
{
class PhaseProbe;
PhaseProbe *activePhaseProbe = nullptr;
QtMessageHandler previousMessageHandler = nullptr;

class PhaseProbe : public QObject
{
public:
    class Scope
    {
    public:
        Scope(PhaseProbe &probe, const QString &name, const QString &category,
              int repetition = -1)
            : probe_(probe), index_(probe.begin(name, category, repetition))
        {
        }

        ~Scope()
        {
            probe_.end(index_);
        }

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

    private:
        PhaseProbe &probe_;
        int index_;
    };

    explicit PhaseProbe(const QStringList &transientSecrets = {})
        : secrets_(transientSecrets)
    {
        timer_.setTimerType(Qt::PreciseTimer);
        timer_.setInterval(10);
        connect(&timer_, &QTimer::timeout, this, [this]()
        {
            const double now = milliseconds();
            heartbeat_.append(QJsonObject{{QStringLiteral("sequence"), heartbeat_.size()},
                {QStringLiteral("previous_ms"), previous_}, {QStringLiteral("now_ms"), now},
                {QStringLiteral("gap_ms"), now - previous_}});
            previous_ = now;
        });
        sampler_.setInterval(200);
        connect(&sampler_, &QTimer::timeout, this, [this]()
        {
            Scope sample(*this, QStringLiteral("test.proc_timer_sample"),
                         QStringLiteral("test_sampler"));
            QJsonObject values = resourceSample(QCoreApplication::applicationPid());
            values.insert(QStringLiteral("at_ms"), milliseconds());
            resources_.append(values);
        });
    }

    ~PhaseProbe() override
    {
        stop();
    }

    void start(bool enableProcSampler = false)
    {
        Q_ASSERT(!started_);
        Q_ASSERT(!activePhaseProbe);
        cpuStart_ = std::clock();
        clock_.start();
        previous_ = milliseconds();
        started_ = true;
        sampling_ = enableProcSampler;
        activePhaseProbe = this;
        previousMessageHandler = qInstallMessageHandler(&PhaseProbe::messageHandler);
        timer_.start();
        if (sampling_)
        {
            sampler_.start();
        }
    }

    void stop()
    {
        if (!started_ || stopped_)
        {
            return;
        }
        timer_.stop();
        sampler_.stop();
        stopAt_ = milliseconds();
        cpuSeconds_ = double(std::clock() - cpuStart_) / CLOCKS_PER_SEC;
        qInstallMessageHandler(previousMessageHandler);
        previousMessageHandler = nullptr;
        activePhaseProbe = nullptr;
        stopped_ = true;
    }

    double milliseconds() const
    {
        return double(clock_.nsecsElapsed()) / 1000000.0;
    }

    int heartbeatCount() const
    {
        return heartbeat_.size();
    }

    int begin(const QString &name, const QString &category, int repetition)
    {
        const int index = phases_.size();
        phases_.append(QJsonObject{{QStringLiteral("id"), index},
            {QStringLiteral("name"), name}, {QStringLiteral("category"), category},
            {QStringLiteral("repetition"), repetition}, {QStringLiteral("begin_ms"), milliseconds()},
            {QStringLiteral("begin_heartbeat_count"), heartbeat_.size()},
            {QStringLiteral("begin_process_cpu_seconds"), double(std::clock()) / CLOCKS_PER_SEC}});
        return index;
    }

    void end(int index)
    {
        QJsonObject phase = phases_.at(index).toObject();
        const double end = milliseconds();
        phase.insert(QStringLiteral("end_ms"), end);
        phase.insert(QStringLiteral("duration_ms"), end - phase.value(QStringLiteral("begin_ms")).toDouble());
        phase.insert(QStringLiteral("end_heartbeat_count"), heartbeat_.size());
        phase.insert(QStringLiteral("heartbeat_deliveries_during_phase"),
                     heartbeat_.size() - phase.value(QStringLiteral("begin_heartbeat_count")).toInt());
        phase.insert(QStringLiteral("process_cpu_ms"),
            1000.0 * (double(std::clock()) / CLOCKS_PER_SEC
                - phase.value(QStringLiteral("begin_process_cpu_seconds")).toDouble()));
        phases_[index] = phase;
    }

    QJsonObject result() const
    {
        Q_ASSERT(stopped_);
        QList<double> gaps;
        QJsonArray attributed;
        for (const QJsonValue &value : heartbeat_)
        {
            QJsonObject gap = value.toObject();
            gaps.append(gap.value(QStringLiteral("gap_ms")).toDouble());
            const double from = gap.value(QStringLiteral("previous_ms")).toDouble();
            const double to = gap.value(QStringLiteral("now_ms")).toDouble();
            QJsonArray intersections;
            for (const QJsonValue &phaseValue : phases_)
            {
                const QJsonObject phase = phaseValue.toObject();
                const double overlapFrom = qMax(from, phase.value(QStringLiteral("begin_ms")).toDouble());
                const double overlapTo = qMin(to, phase.value(QStringLiteral("end_ms")).toDouble());
                if (overlapTo > overlapFrom)
                {
                    intersections.append(QJsonObject{{QStringLiteral("phase_id"), phase.value(QStringLiteral("id"))},
                        {QStringLiteral("name"), phase.value(QStringLiteral("name"))},
                        {QStringLiteral("category"), phase.value(QStringLiteral("category"))},
                        {QStringLiteral("overlap_begin_ms"), overlapFrom},
                        {QStringLiteral("overlap_end_ms"), overlapTo},
                        {QStringLiteral("overlap_duration_ms"), overlapTo - overlapFrom}});
                }
            }
            gap.insert(QStringLiteral("overlapping_phases"), intersections);
            attributed.append(gap);
        }
        QMutexLocker lock(&warningsMutex_);
        return {{QStringLiteral("timer_interval_ms"), 10},
            {QStringLiteral("timer_type"), QStringLiteral("Qt::PreciseTimer")},
            {QStringLiteral("heartbeat"), Probe::summary(gaps)},
            {QStringLiteral("heartbeat_intervals"), attributed},
            {QStringLiteral("phases"), phases_}, {QStringLiteral("warnings"), warnings_},
            {QStringLiteral("elapsed_ms"), stopAt_},
            {QStringLiteral("process_cpu_seconds"), cpuSeconds_},
            {QStringLiteral("cpu_percent_one_core"), stopAt_ > 0 ? 100000.0 * cpuSeconds_ / stopAt_ : 0},
            {QStringLiteral("last_delivered_heartbeat_ms"), previous_},
            {QStringLiteral("undelivered_tail_ms"), stopAt_ - previous_},
            {QStringLiteral("test_proc_sampler_enabled"), sampling_},
            {QStringLiteral("test_proc_samples"), resources_},
            {QStringLiteral("interpretation"), QStringLiteral("Gap is time between GUI timer deliveries; phases may nest and a long phase may deliver many heartbeats. Temporal overlap is not a call-stack proof or compositor/pixel latency.")}};
    }

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &message)
    {
        const QString safeMessage = activePhaseProbe
            ? activePhaseProbe->redactedMessage(message) : message;
        if (activePhaseProbe && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg))
        {
            activePhaseProbe->recordWarning(type, context, safeMessage);
        }
        if (previousMessageHandler)
        {
            previousMessageHandler(type, context, safeMessage);
        }
    }

    QString redactedMessage(QString message) const
    {
        for (const QString &secret : secrets_)
        {
            if (!secret.isEmpty())
            {
                message.replace(secret, QStringLiteral("[redacted transient fixture]"));
            }
        }
        return message;
    }

    void recordWarning(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        QMutexLocker lock(&warningsMutex_);
        warnings_.append(QJsonObject{{QStringLiteral("at_ms"), milliseconds()},
            {QStringLiteral("type"), int(type)},
            {QStringLiteral("category"), QString::fromUtf8(context.category ? context.category : "")},
            {QStringLiteral("message"), message}});
    }

    QElapsedTimer clock_;
    QTimer timer_, sampler_;
    QJsonArray heartbeat_, phases_, resources_, warnings_;
    QStringList secrets_;
    mutable QMutex warningsMutex_;
    std::clock_t cpuStart_ = 0;
    double previous_ = 0, stopAt_ = 0, cpuSeconds_ = 0;
    bool started_ = false, stopped_ = false, sampling_ = false;
};

QJsonObject runtimeMetadata(const QString &workload)
{
    QJsonObject result{{QStringLiteral("workload"), workload},
        {QStringLiteral("process_id"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("platform"), QGuiApplication::platformName()},
        {QStringLiteral("style"), qApp->style()->objectName()},
        {QStringLiteral("font"), qApp->font().toString()},
        {QStringLiteral("qtest_default_mouse_delay_ms"), QTest::defaultMouseDelay()},
        {QStringLiteral("qtest_default_key_delay_ms"), QTest::defaultKeyDelay()},
        {QStringLiteral("render_policy"), QStringLiteral("Original O03-2 layout, QSS, Material/ripple and original CusMessageBox animations preserved; source snapshot and hashes supplied by runner")},
        {QStringLiteral("runtime_data_policy"), QStringLiteral("Fresh anonymous stores; transient random fixture password; no authentication DB, password, derived credential or DB hash in public evidence")},
        {QStringLiteral("condition"), QStringLiteral("WSL x86_64 Release/XCB test process; no RK/compositor/hardware/long-run acceptance")}};
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (screen)
    {
        result.insert(QStringLiteral("screen_width"), screen->size().width());
        result.insert(QStringLiteral("screen_height"), screen->size().height());
        result.insert(QStringLiteral("logical_dpi"), screen->logicalDotsPerInch());
        result.insert(QStringLiteral("device_pixel_ratio"), screen->devicePixelRatio());
    }
#ifdef QTEST_QPA_MOUSE_HANDLING
    result.insert(QStringLiteral("qtest_qpa_mouse_handling"), true);
#else
    result.insert(QStringLiteral("qtest_qpa_mouse_handling"), false);
#endif
    return result;
}

// ASCII through real key events avoids clipboard/X selection transfers in the
// normal-business experiment. Unicode clipboard cost is a separate experiment.
bool keyboardInput(QLineEdit *field, const QString &text)
{
    if (!field || !waitUntil([field]()
        { return field->isVisible() && field->isEnabled() && !field->isReadOnly(); }, 1000))
    {
        return false;
    }
    if (std::any_of(text.cbegin(), text.cend(), [](QChar value) { return value.unicode() > 127; }))
    {
        return false;
    }
    QTest::mouseClick(field, Qt::LeftButton);
    QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(field, text, Qt::NoModifier, 0);
    return waitUntil([field, text]() { return field->text() == text; }, 1000);
}

bool accountAddByKeyboard(Session &session, const QString &name, const QString &secret)
{
    session.menu("btnMenuAdd");
    if (!keyboardInput(session.edit("ldtUserNameAdd"), name)
        || !keyboardInput(session.edit("ldtPasswordAdd"), secret)
        || !keyboardInput(session.edit("ldtPasswordAgainAdd"), secret))
    {
        return false;
    }
    session.combo("cbxTypeSelectAdd")->setCurrentIndex(session.combo("cbxTypeSelectAdd")->findData(1));
    return session.clickAccount("btnAdd", true);
}

bool accountDeleteByKeyboard(Session &session, const QString &name, const QString &secret)
{
    session.menu("btnMenuDel");
    return keyboardInput(session.edit("ldtUserNameDel"), name)
        && keyboardInput(session.edit("ldtPasswordDel"), secret)
        && session.clickAccount("btnDel", true);
}

bool saveHospitalByKeyboard(Session &session, const QString &name)
{
    if (!keyboardInput(session.hospitalName(), name))
    {
        return false;
    }
    session.hospitalOperations->clear();
    QAbstractButton *button = session.hospitalPage->findChild<QAbstractButton *>(QStringLiteral("btnSave"));
    if (!button || !button->isEnabled())
    {
        return false;
    }
    QTest::mouseClick(button, Qt::LeftButton);
    return waitUntil([&session]()
        { return !session.hospitalOperations->isEmpty() && !session.hospitalPage->isBusy(); })
        && session.hospitalOperations->last().at(1).toBool()
        && !session.hospitalPage->hasUnsavedChanges();
}

QJsonObject databaseSizes(const Session &session)
{
    return {{QStringLiteral("account_database_bytes"),
                double(QFileInfo(QDir(session.accountDirectory).filePath(QStringLiteral("user_Information.db"))).size())},
        {QStringLiteral("hospital_database_bytes"),
                double(QFileInfo(QDir(session.hospitalDirectory).filePath(QStringLiteral("hospital_Information.db"))).size())}};
}

void settle(PhaseProbe &probe, int milliseconds = 100)
{
    PhaseProbe::Scope phase(probe, QStringLiteral("test.event_loop_drain"), QStringLiteral("event_loop_interval"));
    QTest::qWait(milliseconds);
}
}

class ShellLatencyTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void burstNavigation();
    void pacedNavigation();
    void normalOperations();
    void evidenceOverhead();

private:
    QString freshRuntime(const QString &slot) const;
    bool writeResult(const QString &slot, const QJsonObject &result) const;
    QString directory_;
};

void ShellLatencyTest::initTestCase()
{
    directory_ = qEnvironmentVariable("SN100_LATENCY_EVIDENCE_DIR");
    QVERIFY2(QDir::isAbsolutePath(directory_), "Set a new absolute SN100_LATENCY_EVIDENCE_DIR");
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(directory_));
    QVERIFY2(normalized.contains(QStringLiteral("/temp/sn100_host/o03_2_latency/")),
             "Latency evidence must stay in the independent o03_2_latency tree");
    QVERIFY2(QGuiApplication::platformName() == QStringLiteral("xcb"), "Requires WSLg/XCB, never offscreen");
    QVERIFY(QDir().mkpath(directory_));
    // The runner writes run_buildmetadata.json before exec. Individual slots
    // reject reused data roots and NewOnly protects every result file.
    qApp->setQuitOnLastWindowClosed(false);
    qApp->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    qApp->setStyle(QStringLiteral("Fusion"));
}

QString ShellLatencyTest::freshRuntime(const QString &slot) const
{
    const QString path = QDir(directory_).filePath(slot + QStringLiteral("_runtime"));
    return QFileInfo::exists(path) ? QString() : path;
}

bool ShellLatencyTest::writeResult(const QString &slot, const QJsonObject &result) const
{
    return saveJson(QDir(directory_).filePath(slot + QStringLiteral("_results.json")), result);
}

void ShellLatencyTest::burstNavigation()
{
    const QString slot = QStringLiteral("burstNavigation");
    const QString runtime = freshRuntime(slot);
    QVERIFY2(!runtime.isEmpty(), "The runtime must be new; never reset a store");
    const QString secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DialogAnswers dialogs;
    Session session(runtime, secret);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    session.clearPasswords();
    QVERIFY(session.passwordsEmpty());
    QTest::qWait(200);
    const bool sampleProc = qEnvironmentVariableIntValue("SN100_LATENCY_PROC_SAMPLER") == 1;
    const bool withScreenshot = qEnvironmentVariableIntValue("SN100_LATENCY_CAPTURE_FIRST") == 1;
    PhaseProbe trace({secret});
    trace.start(sampleProc);
    settle(trace);
    const int heartbeatsBefore = trace.heartbeatCount();
    {
        // A pending interval may include the prior whole-frame capture. Keep
        // the optional exact old adjacency while labeling the two components.
        PhaseProbe::Scope combined(trace, QStringLiteral("test.pre_capture_and_navigation_burst"),
                                    QStringLiteral("test_driver"));
        if (withScreenshot)
        {
            PhaseProbe::Scope capture(trace, QStringLiteral("test.pre_navigation_grab_save"),
                                      QStringLiteral("evidence_capture"));
            QVERIFY(session.window->grab().save(QDir(directory_).filePath(QStringLiteral("burst_before.png"))));
        }
        PhaseProbe::Scope burst(trace, QStringLiteral("test.100_synchronous_widget_clicks"),
                                QStringLiteral("test_driver"));
        for (int iteration = 0; iteration < 50; ++iteration)
        {
            {
                PhaseProbe::Scope click(trace, QStringLiteral("navigation.hospital"),
                                        QStringLiteral("navigation"), iteration);
                QVERIFY(session.navigationClick("btnHospInfo", 6, 1));
            }
            {
                PhaseProbe::Scope click(trace, QStringLiteral("navigation.account"),
                                        QStringLiteral("navigation"), iteration);
                QVERIFY(session.navigationClick("btnAccount", 6, 0));
            }
            QCOMPARE(session.window->findChildren<frmAccountSetting *>().size(), 1);
            QCOMPARE(session.window->findChildren<frmHospInfoSetting *>().size(), 1);
            QVERIFY(!session.hospitalPage->hasUnsavedChanges());
        }
    }
    const int heartbeatsBeforeDrain = trace.heartbeatCount();
    settle(trace);
    trace.stop();
    QJsonObject result = runtimeMetadata(QStringLiteral("100 synchronous QWidget QTest navigation transitions; no yielding between accepted clicks"));
    result.insert(QStringLiteral("trace"), trace.result());
    result.insert(QStringLiteral("pre_navigation_screenshot_enabled"), withScreenshot);
    result.insert(QStringLiteral("heartbeats_before_burst"), heartbeatsBefore);
    result.insert(QStringLiteral("heartbeats_after_burst_before_drain"), heartbeatsBeforeDrain);
    result.insert(QStringLiteral("heartbeat_deliveries_during_burst_and_optional_capture"),
                  heartbeatsBeforeDrain - heartbeatsBefore);
    result.insert(QStringLiteral("navigation_clicks"), 100);
    result.insert(QStringLiteral("database_sizes_after_trace"), databaseSizes(session));
    QVERIFY(session.window->close());
    const bool stopped = session.stopWorkers();
    QVERIFY(stopped);
    result.insert(QStringLiteral("workers_closed_after_trace"), stopped);
    result.insert(QStringLiteral("business_assertions_passed"), true);
    QVERIFY(writeResult(slot, result));
}

void ShellLatencyTest::pacedNavigation()
{
    const QString slot = QStringLiteral("pacedNavigation");
    const QString runtime = freshRuntime(slot);
    QVERIFY2(!runtime.isEmpty(), "The runtime must be new; never reset a store");
    const QString secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DialogAnswers dialogs;
    Session session(runtime, secret);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QTest::qWait(200);
    PhaseProbe trace({secret});
    trace.start(qEnvironmentVariableIntValue("SN100_LATENCY_PROC_SAMPLER") == 1);
    settle(trace);
    for (int iteration = 0; iteration < 50; ++iteration)
    {
        {
            PhaseProbe::Scope click(trace, QStringLiteral("navigation.hospital"),
                                    QStringLiteral("navigation"), iteration);
            QVERIFY(session.navigationClick("btnHospInfo", 6, 1));
        }
        settle(trace, 10);
        {
            PhaseProbe::Scope click(trace, QStringLiteral("navigation.account"),
                                    QStringLiteral("navigation"), iteration);
            QVERIFY(session.navigationClick("btnAccount", 6, 0));
        }
        settle(trace, 10);
        QCOMPARE(session.window->findChildren<frmAccountSetting *>().size(), 1);
        QCOMPARE(session.window->findChildren<frmHospInfoSetting *>().size(), 1);
        QVERIFY(!session.hospitalPage->hasUnsavedChanges());
    }
    settle(trace);
    trace.stop();
    QJsonObject result = runtimeMetadata(QStringLiteral("Same100 QWidget navigation clicks, each followed by10ms event-loop time; no PNG, editing, dialog or external SQL"));
    result.insert(QStringLiteral("trace"), trace.result());
    result.insert(QStringLiteral("navigation_clicks"), 100);
    result.insert(QStringLiteral("event_loop_interval_after_each_click_ms"), 10);
    result.insert(QStringLiteral("database_sizes_after_trace"), databaseSizes(session));
    QVERIFY(session.window->close());
    const bool stopped = session.stopWorkers();
    QVERIFY(stopped);
    result.insert(QStringLiteral("workers_closed_after_trace"), stopped);
    result.insert(QStringLiteral("business_assertions_passed"), true);
    QVERIFY(writeResult(slot, result));
}

void ShellLatencyTest::normalOperations()
{
    const QString slot = QStringLiteral("normalOperations");
    const QString runtime = freshRuntime(slot);
    QVERIFY2(!runtime.isEmpty(), "The runtime must be new; never reset a store");
    const QString secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DialogAnswers dialogs;
    Session session(runtime, secret);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    QTest::qWait(200);
    PhaseProbe trace({secret});
    // No resource sampler, clipboard or external SQL inside this trace. The
    // original modal implementation and its internal animation captures remain.
    trace.start(false);
    settle(trace);
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        const QString name = QStringLiteral("LatencyPair%1").arg(iteration, 2, 10, QLatin1Char('0'));
        {
            PhaseProbe::Scope operation(trace, QStringLiteral("normal.account_add"),
                                        QStringLiteral("normal_operation"), iteration);
            QVERIFY2(accountAddByKeyboard(session, name, secret), qPrintable(session.accountPage->statusText()));
            QVERIFY(!session.accountPage->isBusy());
        }
        settle(trace, 10);
        {
            PhaseProbe::Scope operation(trace, QStringLiteral("normal.account_delete"),
                                        QStringLiteral("normal_operation"), iteration);
            QVERIFY2(accountDeleteByKeyboard(session, name, secret), qPrintable(session.accountPage->statusText()));
            QVERIFY(!session.accountPage->isBusy());
        }
        settle(trace, 10);
    }
    {
        PhaseProbe::Scope navigation(trace, QStringLiteral("normal.navigate_hospital"),
                                     QStringLiteral("normal_operation"));
        QVERIFY(session.navigationClick("btnHospInfo", 6, 1));
    }
    settle(trace, 10);
    QString committedHospital;
    for (int iteration = 0; iteration < 10; ++iteration)
    {
        committedHospital = QStringLiteral("Latency hospital %1").arg(iteration);
        {
            PhaseProbe::Scope operation(trace, QStringLiteral("normal.hospital_save"),
                                        QStringLiteral("normal_operation"), iteration);
            QVERIFY2(saveHospitalByKeyboard(session, committedHospital), qPrintable(session.hospitalPage->statusText()));
        }
        settle(trace, 10);
    }
    {
        PhaseProbe::Scope operation(trace, QStringLiteral("normal.unsaved_navigation_cancel"),
                                    QStringLiteral("normal_operation"));
        QVERIFY(keyboardInput(session.hospitalName(), QStringLiteral("Latency unsaved navigation")));
        dialogs.nextStandard = QMessageBox::No;
        const int before = dialogs.answered;
        QTest::mouseClick(session.button("btnAccount"), Qt::LeftButton);
        QVERIFY(dialogs.answered > before);
        QCOMPARE(session.window->currentUserPage(), 1);
        QVERIFY(session.hospitalPage->hasUnsavedChanges());
        QCOMPARE(session.hospitalName()->text(), QStringLiteral("Latency unsaved navigation"));
    }
    settle(trace, 10);
    {
        PhaseProbe::Scope operation(trace, QStringLiteral("normal.unsaved_navigation_discard"),
                                    QStringLiteral("normal_operation"));
        dialogs.nextStandard = QMessageBox::Yes;
        QVERIFY(session.navigationClick("btnAccount", 6, 0));
        QVERIFY(!session.hospitalPage->hasUnsavedChanges());
        QCOMPARE(session.hospitalName()->text(), committedHospital);
    }
    settle(trace, 10);
    QVERIFY(session.navigationClick("btnHospInfo", 6, 1));
    settle(trace, 10);
    {
        PhaseProbe::Scope operation(trace, QStringLiteral("normal.manual_logout_cancel"),
                                    QStringLiteral("normal_operation"));
        QVERIFY(keyboardInput(session.hospitalName(), QStringLiteral("Latency unsaved logout")));
        session.logout->clear();
        dialogs.nextStandard = QMessageBox::No;
        QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
        QVERIFY(session.hospitalPage->hasUnsavedChanges());
        QCOMPARE(session.window->accountSession().name, QStringLiteral("offlineEngineer"));
        QCOMPARE(session.logout->size(), 0);
    }
    settle(trace, 10);
    {
        PhaseProbe::Scope operation(trace, QStringLiteral("normal.manual_logout_discard"),
                                    QStringLiteral("normal_operation"));
        dialogs.nextStandard = QMessageBox::Yes;
        QTest::mouseClick(session.button("btnLoginOut"), Qt::LeftButton);
        QVERIFY(waitUntil([&session]() { return !session.accountPage->isBusy() && session.guestClean(); }));
        QCOMPARE(session.logout->size(), 1);
        QVERIFY(!session.hospitalPage->hasUnsavedChanges());
        QCOMPARE(session.hospitalName()->text(), committedHospital);
    }
    settle(trace);
    trace.stop();
    QJsonObject result = runtimeMetadata(QStringLiteral("10 real keyboard account Add/Delete pairs,10 hospital saves, dirty navigation and manual logout cancel/discard; no external capture/SQL/clipboard/proc during trace"));
    result.insert(QStringLiteral("trace"), trace.result());
    result.insert(QStringLiteral("successful_adds"), 10);
    result.insert(QStringLiteral("successful_deletes"), 10);
    result.insert(QStringLiteral("successful_hospital_saves"), 10);
    result.insert(QStringLiteral("dialog_answers"), dialogs.answered);
    result.insert(QStringLiteral("external_evidence_work_after_trace"), true);
    // Public-only independent verification deliberately occurs after stop.
    const QJsonObject actual = session.snapshot();
    const QJsonObject accounts = actual.value(QStringLiteral("accounts")).toObject();
    const QJsonObject hospital = actual.value(QStringLiteral("hospital")).toObject();
    QCOMPARE(accounts.value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
    QCOMPARE(accounts.value(QStringLiteral("rows")).toArray().size(), 5);
    for (const QJsonValue &value : accounts.value(QStringLiteral("rows")).toArray())
    {
        QVERIFY(!value.toObject().value(QStringLiteral("name")).toString().startsWith(QStringLiteral("LatencyPair")));
    }
    QCOMPARE(hospital.value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
    QCOMPARE(hospital.value(QStringLiteral("hospital_name")).toString(), committedHospital);
    QCOMPARE(hospital.value(QStringLiteral("rows")).toArray().size(), 0);
    QVERIFY(session.guestClean());
    result.insert(QStringLiteral("public_verification_after_trace"), actual);
    result.insert(QStringLiteral("database_sizes_after_trace"), databaseSizes(session));
    QVERIFY(session.window->close());
    const bool stopped = session.stopWorkers();
    QVERIFY(stopped);
    result.insert(QStringLiteral("workers_closed_after_trace"), stopped);
    result.insert(QStringLiteral("business_assertions_passed"), true);
    QVERIFY(writeResult(slot, result));
}

void ShellLatencyTest::evidenceOverhead()
{
    const QString slot = QStringLiteral("evidenceOverhead");
    const QString runtime = freshRuntime(slot);
    QVERIFY2(!runtime.isEmpty(), "The runtime must be new; never reset a store");
    const QString secret = QUuid::createUuid().toString(QUuid::WithoutBraces);
    DialogAnswers dialogs;
    Session session(runtime, secret);
    QVERIFY(session.ready());
    QVERIFY(QTest::qWaitForWindowExposed(session.window));
    QVERIFY(session.login(QStringLiteral("offlineEngineer")));
    session.clearPasswords();
    QVERIFY(session.passwordsEmpty());
    QTest::qWait(200);
    const QPixmap preCaptured = session.window->grab();
    QVERIFY(!preCaptured.isNull());
    QByteArray preEncoded;
    {
        QBuffer buffer(&preEncoded);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(preCaptured.save(&buffer, "PNG"));
    }
    PhaseProbe trace({secret});
    trace.start(false);
    settle(trace);
    QPixmap lastCapture;
    QJsonObject lastPublic, lastProc;
    QJsonArray byteCounts;
    for (int repetition = 0; repetition < 5; ++repetition)
    {
        QVERIFY(session.passwordsEmpty());
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.window_grab"),
                                    QStringLiteral("evidence_capture"), repetition);
            lastCapture = session.window->grab();
            QVERIFY(!lastCapture.isNull());
        }
        settle(trace, 30);
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.pregrabbed_png_encode"),
                                    QStringLiteral("evidence_encode"), repetition);
            QByteArray encoded;
            QBuffer buffer(&encoded);
            QVERIFY(buffer.open(QIODevice::WriteOnly));
            QVERIFY(preCaptured.save(&buffer, "PNG"));
            byteCounts.append(encoded.size());
        }
        settle(trace, 30);
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.preencoded_file_write_flush"),
                                    QStringLiteral("evidence_file_io"), repetition);
            const QString path = QDir(directory_).filePath(QStringLiteral("encoded_write_%1.png").arg(repetition));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            QCOMPARE(file.write(preEncoded), qint64(preEncoded.size()));
            QVERIFY(file.flush());
            file.close();
        }
        settle(trace, 30);
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.original_window_grab_save"),
                                    QStringLiteral("evidence_capture_encode_file"), repetition);
            const QString path = QDir(directory_).filePath(QStringLiteral("grab_save_%1.png").arg(repetition));
            QVERIFY(!QFileInfo::exists(path));
            QVERIFY(session.window->grab().save(path));
        }
        settle(trace, 30);
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.two_public_sql_snapshots"),
                                    QStringLiteral("test_sql_snapshot"), repetition);
            lastPublic = session.snapshot();
            QCOMPARE(lastPublic.value(QStringLiteral("accounts")).toObject()
                .value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
            QCOMPARE(lastPublic.value(QStringLiteral("hospital")).toObject()
                .value(QStringLiteral("integrity_check")).toString(), QStringLiteral("ok"));
        }
        settle(trace, 30);
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.proc_resource_sample"),
                                    QStringLiteral("test_sampler"), repetition);
            lastProc = resourceSample(QCoreApplication::applicationPid());
            QVERIFY(lastProc.contains(QStringLiteral("Threads:")));
        }
        settle(trace, 30);
    }
    // Matched public ASCII input isolates the driver path. This is deliberately
    // the old paste helper, including setText, Ctrl+V and clipboard clear.
    session.menu("btnMenuAdd");
    QLineEdit *field = session.edit("ldtUserNameAdd");
    for (int repetition = 0; repetition < 10; ++repetition)
    {
        const QString text = QStringLiteral("LatencyInput%1").arg(repetition, 2, 10, QLatin1Char('0'));
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.clipboard_ascii_input"),
                                    QStringLiteral("test_input_driver"), repetition);
            QVERIFY(paste(field, text));
        }
        settle(trace, 30);
        {
            PhaseProbe::Scope phase(trace, QStringLiteral("evidence.keyboard_ascii_input"),
                                    QStringLiteral("test_input_driver"), repetition);
            QVERIFY(keyboardInput(field, text));
        }
        settle(trace, 30);
    }
    settle(trace);
    trace.stop();
    QJsonObject result = runtimeMetadata(QStringLiteral("5 separate whole-frame grab/PNG-encode/write-flush/grab-save/public-SQL/proc phases and10 paired clipboard/keyboard input phases;30ms event-loop time between phases"));
    result.insert(QStringLiteral("trace"), trace.result());
    result.insert(QStringLiteral("capture_width"), preCaptured.width());
    result.insert(QStringLiteral("capture_height"), preCaptured.height());
    result.insert(QStringLiteral("preencoded_png_bytes"), preEncoded.size());
    result.insert(QStringLiteral("encoded_phase_png_bytes"), byteCounts);
    result.insert(QStringLiteral("last_public_snapshot"), lastPublic);
    result.insert(QStringLiteral("last_proc_sample"), lastProc);
    result.insert(QStringLiteral("database_sizes_after_trace"), databaseSizes(session));
    result.insert(QStringLiteral("screenshots_password_fields_empty"), session.passwordsEmpty());
    result.insert(QStringLiteral("file_flush_scope"), QStringLiteral("QFile::flush and close; no fsync/durable-storage claim"));
    QVERIFY(session.passwordsEmpty());
    QVERIFY(session.window->close());
    const bool stopped = session.stopWorkers();
    QVERIFY(stopped);
    result.insert(QStringLiteral("workers_closed_after_trace"), stopped);
    result.insert(QStringLiteral("business_assertions_passed"), true);
    QVERIFY(writeResult(slot, result));
}

QTEST_MAIN(ShellLatencyTest)
#include "shell_latency_test.moc"
