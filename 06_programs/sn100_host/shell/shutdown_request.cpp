#include "shutdown_request.h"

#include <QDebug>
#include <QWidget>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

namespace
{
volatile sig_atomic_t signalWriteFd = -1;

void forwardStopSignal(int number)
{
    const int savedError = errno;
    const unsigned char value = static_cast<unsigned char>(number);
    const int descriptor = static_cast<int>(signalWriteFd);
    if (descriptor >= 0)
    {
        // 非阻塞管道满时合并请求；处理器内不分配内存、不调用Qt、不等待。
        const ssize_t ignored = ::write(descriptor, &value, sizeof(value));
        (void)ignored;
    }
    errno = savedError;
}

sigset_t stopSignals()
{
    sigset_t stopSet;
    sigemptyset(&stopSet);
    sigaddset(&stopSet, SIGTERM);
    sigaddset(&stopSet, SIGINT);
    return stopSet;
}
}

class ShutdownRequest::NativeState
{
public:
    int descriptors[2] = {-1, -1};
    struct sigaction previousTerm = {};
    struct sigaction previousInt = {};
    QSocketNotifier *notifier = nullptr;

    int drain()
    {
        int first = 0;
        unsigned char values[128];
        for (;;)
        {
            const ssize_t count = ::read(descriptors[0], values, sizeof(values));
            if (count > 0)
            {
                if (!first) first = values[0];
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            return first;
        }
    }
};
#else
class ShutdownRequest::NativeState {};
#endif

ShutdownRequest::ShutdownRequest(QObject *parent) : QObject(parent)
{
}

ShutdownRequest::~ShutdownRequest()
{
#ifdef Q_OS_UNIX
    if (native_)
    {
        const sigset_t stopSet = stopSignals();
        sigset_t previousMask;
        const bool blocked = sigprocmask(SIG_BLOCK, &stopSet, &previousMask) == 0;
        native_->notifier->setEnabled(false);
        signalWriteFd = -1;
        sigaction(SIGTERM, &native_->previousTerm, nullptr);
        sigaction(SIGINT, &native_->previousInt, nullptr);
        delete native_->notifier;
        ::close(native_->descriptors[1]);
        ::close(native_->descriptors[0]);
        if (blocked) sigprocmask(SIG_SETMASK, &previousMask, nullptr);
    }
#endif
    delete native_;
}

bool ShutdownRequest::initialize(QString *error)
{
#ifdef Q_OS_UNIX
    if (native_ || signalWriteFd >= 0)
    {
        if (error) *error = QStringLiteral("进程关闭信号桥已安装，不能重复接管");
        return false;
    }
    auto *state = new NativeState;
    const auto fail = [&](const QString &operation)
    {
        const int code = errno;
        for (int descriptor : state->descriptors)
        {
            if (descriptor >= 0) ::close(descriptor);
        }
        delete state;
        if (error) *error = operation + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(code));
        return false;
    };
    if (::pipe(state->descriptors) != 0) return fail(QStringLiteral("创建关闭请求管道失败"));
    for (int descriptor : state->descriptors)
    {
        const int flags = fcntl(descriptor, F_GETFL);
        const int descriptorFlags = fcntl(descriptor, F_GETFD);
        if (flags < 0 || descriptorFlags < 0
            || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) < 0
            || fcntl(descriptor, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0)
            return fail(QStringLiteral("设置关闭请求管道属性失败"));
    }
    const sigset_t stopSet = stopSignals();
    sigset_t previousMask;
    if (sigprocmask(SIG_BLOCK, &stopSet, &previousMask) != 0)
        return fail(QStringLiteral("暂时屏蔽关闭信号失败"));
    struct sigaction action = {};
    action.sa_handler = forwardStopSignal;
    action.sa_mask = stopSet;
    action.sa_flags = SA_RESTART;
    if (sigaction(SIGTERM, &action, &state->previousTerm) != 0)
    {
        const int code = errno;
        sigprocmask(SIG_SETMASK, &previousMask, nullptr);
        errno = code;
        return fail(QStringLiteral("安装SIGTERM关闭处理失败"));
    }
    if (sigaction(SIGINT, &action, &state->previousInt) != 0)
    {
        const int code = errno;
        sigaction(SIGTERM, &state->previousTerm, nullptr);
        sigprocmask(SIG_SETMASK, &previousMask, nullptr);
        errno = code;
        return fail(QStringLiteral("安装SIGINT关闭处理失败"));
    }
    native_ = state;
    signalWriteFd = state->descriptors[1];
    state->notifier = new QSocketNotifier(state->descriptors[0], QSocketNotifier::Read, this);
    connect(state->notifier, &QSocketNotifier::activated, this, [this](int) { handlePending(); });
    sigprocmask(SIG_SETMASK, &previousMask, nullptr);
#else
    Q_UNUSED(error);
#endif
    return true;
}

void ShutdownRequest::setWindow(QWidget *window)
{
    window_ = window;
}

void ShutdownRequest::handlePending()
{
#ifdef Q_OS_UNIX
    if (!native_ || handling_) return;
    native_->notifier->setEnabled(false);
    const int number = native_->drain();
    if (number)
    {
        handling_ = true;
        qInfo().noquote() << "SHUTDOWN_REQUEST signal=" << (number == SIGINT ? "SIGINT" : "SIGTERM");
        const bool accepted = window_ && window_->close();
        qInfo().noquote() << (accepted ? "SHUTDOWN_ACCEPTED" : "SHUTDOWN_REJECTED window_guard_or_unavailable");
        // 确认框的嵌套事件循环内收到的重复请求不重新弹框或绕过守卫。
        native_->drain();
        handling_ = false;
    }
    native_->notifier->setEnabled(true);
#endif
}
