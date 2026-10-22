#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QTemporaryFile>
#include <QTimer>
#include <QVBoxLayout>
#include <csignal>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

static int userId() {
#ifdef Q_OS_UNIX
    return int(getuid());
#else
    return -1;
#endif
}

static volatile std::sig_atomic_t stopRequested = 0;
static void requestStop(int) { stopRequested = 1; }

static void logEvent(const char *kind, const QString &details = {}) {
    const QByteArray timestamp = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8();
    const QByteArray data = details.toUtf8();
    std::fprintf(stdout, "%s %s %s\n", timestamp.constData(), kind, data.constData());
    std::fflush(stdout);
}

class InputMonitor final : public QObject {
public:
    int keyPresses = 0;
    int mousePresses = 0;
protected:
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->spontaneous() && event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            ++keyPresses;
            logEvent("KEY_PRESS", QString("key=%1 count=%2").arg(key->key()).arg(keyPresses));
        } else if (event->spontaneous() && event->type() == QEvent::MouseButtonPress) {
            const auto *mouse = static_cast<QMouseEvent *>(event);
            ++mousePresses;
            logEvent("MOUSE_PRESS", QString("button=%1 x=%2 y=%3 count=%4")
                     .arg(int(mouse->button())).arg(mouse->globalX()).arg(mouse->globalY()).arg(mousePresses));
        }
        return false;
    }
};

int main(int argc, char **argv) {
#ifdef RK_BOARD
    // Only this process uses these settings. No system graphics configuration is changed.
    QTemporaryFile kmsConfig;
    if (!kmsConfig.open()) {
        std::fprintf(stderr, "Cannot create temporary EGLFS configuration\n");
        return 2;
    }
    kmsConfig.write(R"({"device":"/dev/dri/card0","hwcursor":false,"outputs":[{"name":"HDMI1","mode":"preferred","primary":true},{"name":"DP1","mode":"off"},{"name":"Writeback1","mode":"off"}]})");
    kmsConfig.flush();
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "eglfs");
    if (qEnvironmentVariableIsEmpty("QT_QPA_EGLFS_INTEGRATION")) qputenv("QT_QPA_EGLFS_INTEGRATION", "eglfs_kms");
    if (qEnvironmentVariableIsEmpty("QT_QPA_EGLFS_KMS_CONFIG"))
        qputenv("QT_QPA_EGLFS_KMS_CONFIG", kmsConfig.fileName().toUtf8());
#endif

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("qt_demo");
    QCoreApplication::setApplicationVersion("1.0");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"fullscreen", "Show fullscreen on the desktop as well."});
    parser.addOption({"windowed", "Show a desktop window instead of the board's fullscreen default."});
    parser.addOption({"timeout", "Exit normally after N seconds; 0 means no limit.", "seconds", "0"});
    parser.addOption({"state", "Write observed interaction state to a JSON file.", "path", "state.json"});
    parser.addOption({"snapshot", "Save a rendering of this application's widget (not a monitor photograph).", "path", "render.png"});
    parser.process(app);
    bool timeoutValid = false;
    const int timeoutSeconds = parser.value("timeout").toInt(&timeoutValid);
    if (!timeoutValid || timeoutSeconds < 0 || timeoutSeconds > 86400) {
        std::fprintf(stderr, "--timeout must be an integer from 0 to 86400\n");
        return 2;
    }
    const QString statePath = parser.value("state");
    const QString snapshotPath = parser.value("snapshot");
#ifdef Q_OS_WIN
    app.setFont(QFont("Microsoft YaHei", 12));
#else
    QFontDatabase::addApplicationFont("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf");
    app.setFont(QFont("DejaVu Sans", 17));
#endif
    app.setStyle("Fusion");
    app.setStyleSheet(R"(
        QWidget { background: #101b2b; color: #e7eef7; }
        QLabel { background: transparent; }
        QFrame#card { background: #1a2b40; border: 1px solid #304761; border-radius: 16px; }
        QPushButton { background: #167f84; border: 1px solid #39afb2; border-radius: 10px;
                      padding: 14px 26px; min-height: 36px; color: white; font-size: 22px; }
        QPushButton:hover { background: #229a9e; }
        QPushButton:pressed { background: #0b6266; }
        QPushButton:focus { border: 3px solid #b3f1dd; }
        QPushButton#secondary { background: #263c55; border-color: #57708a; }
        QLineEdit { background: #101d2e; color: white; border: 2px solid #51718e;
                    border-radius: 10px; padding: 16px; font-size: 25px; }
        QLineEdit:focus { border-color: #51d1c0; }
    )");

    QWidget window;
    window.setWindowTitle("RK3576 Qt HDMI / USB verification");
    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(44, 28, 44, 28);
    layout->setSpacing(20);
    auto *title = new QLabel(QString::fromUtf8("RK3576 · Qt 显示与交互验证"));
    title->setStyleSheet("font-size: 34px; font-weight: bold;");
    layout->addWidget(title);
    auto *screen = app.primaryScreen();
    const QString screenInfo = QString("Qt %1  |  %2  |  %3 x %4  |  UID %5")
        .arg(qVersion(), QGuiApplication::platformName()).arg(screen->size().width())
        .arg(screen->size().height()).arg(userId());
    auto *subtitle = new QLabel(screenInfo);
    subtitle->setStyleSheet("color: #9db4ca; font-size: 19px;");
    layout->addWidget(subtitle);
    auto *cards = new QHBoxLayout;
    cards->setSpacing(24);
    layout->addLayout(cards, 1);
    auto makeCard = [&]() {
        auto *frame = new QFrame;
        frame->setObjectName("card");
        cards->addWidget(frame, 1);
        auto *box = new QVBoxLayout(frame);
        box->setContentsMargins(28, 24, 28, 24);
        box->setSpacing(20);
        return box;
    };
    auto *mouseBox = makeCard();
    mouseBox->addWidget(new QLabel(QString::fromUtf8("01  鼠标测试 / MOUSE")));
    auto *counter = new QLabel("0");
    counter->setAlignment(Qt::AlignCenter);
    counter->setStyleSheet("font-size: 100px; color: #66dec7; font-weight: bold;");
    mouseBox->addWidget(counter, 1);
    auto *increment = new QPushButton(QString::fromUtf8("点击 +1 / CLICK +1"));
    auto *reset = new QPushButton(QString::fromUtf8("清零 / RESET"));
    reset->setObjectName("secondary");
    mouseBox->addWidget(increment);
    mouseBox->addWidget(reset);
    auto *keyboardBox = makeCard();
    keyboardBox->addWidget(new QLabel(QString::fromUtf8("02  键盘测试 / KEYBOARD")));
    auto *instruction = new QLabel(QString::fromUtf8("在下方输入 RK3576_OK，然后按 Enter。\nClick below, type RK3576_OK, then press Enter."));
    instruction->setWordWrap(true);
    instruction->setStyleSheet("font-size: 20px; color: #b6c8db;");
    keyboardBox->addWidget(instruction);
    auto *input = new QLineEdit;
    input->setPlaceholderText("RK3576_OK");
    input->setMaxLength(64);
    input->setClearButtonEnabled(true);
    keyboardBox->addWidget(input);
    auto *submitted = new QLabel(QString::fromUtf8("等待输入 / Waiting for input"));
    submitted->setWordWrap(true);
    submitted->setStyleSheet("color: #66dec7; font-size: 23px;");
    keyboardBox->addWidget(submitted, 1);
    auto *submit = new QPushButton(QString::fromUtf8("确认输入 / SUBMIT"));
    keyboardBox->addWidget(submit);
    auto *footer = new QHBoxLayout;
    auto *clock = new QLabel;
    clock->setStyleSheet("color: #9db4ca; font-size: 18px;");
    footer->addWidget(clock, 1);
    auto *exitButton = new QPushButton(QString::fromUtf8("退出 / EXIT"));
    exitButton->setObjectName("secondary");
    footer->addWidget(exitButton);
    layout->addLayout(footer);

    InputMonitor monitor;
    app.installEventFilter(&monitor);
    int clicks = 0, totalIncrements = 0, resets = 0, submissions = 0;
    QString lastSubmitted, exitReason = "window_closed";
    const QString started = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    auto saveState = [&]() {
        QJsonObject state{{"started", started}, {"updated", QDateTime::currentDateTime().toString(Qt::ISODateWithMs)},
            {"qt_version", qVersion()}, {"platform", QGuiApplication::platformName()}, {"uid", userId()},
            {"screen", screen->name()}, {"width", screen->size().width()}, {"height", screen->size().height()},
            {"counter", clicks}, {"total_increments", totalIncrements}, {"resets", resets},
            {"submissions", submissions}, {"submitted_text", lastSubmitted}, {"input_text", input->text()},
            {"key_presses", monitor.keyPresses}, {"mouse_presses", monitor.mousePresses}, {"exit_reason", exitReason}};
        QSaveFile file(statePath);
        if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(state).toJson()) < 0 || !file.commit())
            logEvent("STATE_ERROR", file.errorString());
    };
    QTimer snapshotTimer;
    snapshotTimer.setSingleShot(true);
    QObject::connect(&snapshotTimer, &QTimer::timeout, [&]() {
        if (!window.grab().save(snapshotPath)) logEvent("SNAPSHOT_ERROR", snapshotPath);
    });
    auto changed = [&]() { saveState(); snapshotTimer.start(150); };
    QObject::connect(increment, &QPushButton::clicked, [&]() {
        ++clicks; ++totalIncrements; counter->setText(QString::number(clicks));
        logEvent("COUNTER", QString("value=%1 total_increments=%2").arg(clicks).arg(totalIncrements)); changed();
    });
    QObject::connect(reset, &QPushButton::clicked, [&]() {
        clicks = 0; ++resets; counter->setText("0"); logEvent("RESET", QString("count=%1").arg(resets)); changed();
    });
    auto submitText = [&]() {
        lastSubmitted = input->text(); ++submissions;
        submitted->setText(QString::fromUtf8("已收到 / Received: ") + lastSubmitted);
        logEvent("SUBMIT", QString::fromUtf8(QJsonDocument(QJsonObject{{"text", lastSubmitted}, {"count", submissions}}).toJson(QJsonDocument::Compact)));
        changed();
    };
    QObject::connect(submit, &QPushButton::clicked, submitText);
    QObject::connect(input, &QLineEdit::returnPressed, submitText);
    QObject::connect(input, &QLineEdit::textEdited, changed);
    QObject::connect(exitButton, &QPushButton::clicked, [&]() { exitReason = "exit_button"; app.quit(); });
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&]() {
        clock->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd  HH:mm:ss") + QString::fromUtf8("    ·    界面运行中 / RUNNING"));
        if (stopRequested) { exitReason = "signal"; app.quit(); }
    });
    heartbeat.start(250);
    std::signal(SIGTERM, requestStop);
    std::signal(SIGINT, requestStop);
    if (timeoutSeconds > 0) QTimer::singleShot(timeoutSeconds * 1000, [&]() { exitReason = "timeout"; app.quit(); });
    bool fullscreen = parser.isSet("fullscreen");
#ifdef RK_BOARD
    fullscreen = !parser.isSet("windowed");
#endif
    if (fullscreen) {
        window.showFullScreen();
    } else {
        const QSize available = screen->availableGeometry().size();
        window.resize(qMin(1320, available.width() - 60), qMin(820, available.height() - 80));
        window.show();
    }
    input->setFocus();
    logEvent("START", screenInfo + QString(" screen=%1 cwd=%2").arg(screen->name(), QDir::currentPath()));
    QTimer::singleShot(1500, [&]() { changed(); logEvent("READY", "screen content requires observation; input has not been simulated"); });
    const int code = app.exec();
    saveState();
    logEvent("EXIT", QString("code=%1 reason=%2").arg(code).arg(exitReason));
    return code;
}
