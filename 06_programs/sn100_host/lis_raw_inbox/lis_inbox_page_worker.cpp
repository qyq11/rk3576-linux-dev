#include "lis_inbox_page_worker.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QUuid>

#ifdef Q_OS_LINUX
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace
{
class FileDescriptor
{
public:
    explicit FileDescriptor(int value) : value_(value) {}
    ~FileDescriptor() { if (value_ >= 0) ::close(value_); }
    int get() const { return value_; }
    void reset(int value) { if (value_ >= 0) ::close(value_); value_ = value; }
private:
    Q_DISABLE_COPY(FileDescriptor)
    int value_;
};
bool sameFile(const struct stat &left, const struct stat &right)
{
    return left.st_dev == right.st_dev && left.st_ino == right.st_ino && left.st_size == right.st_size
        && left.st_mtim.tv_sec == right.st_mtim.tv_sec && left.st_mtim.tv_nsec == right.st_mtim.tv_nsec
        && left.st_ctim.tv_sec == right.st_ctim.tv_sec && left.st_ctim.tv_nsec == right.st_ctim.tv_nsec;
}
QByteArray fileStamp(const struct stat &value)
{
    return QByteArray::number(static_cast<qulonglong>(value.st_dev)) + '|'
        + QByteArray::number(static_cast<qulonglong>(value.st_ino)) + '|'
        + QByteArray::number(static_cast<qlonglong>(value.st_size)) + '|'
        + QByteArray::number(static_cast<qlonglong>(value.st_mtim.tv_sec)) + '|'
        + QByteArray::number(static_cast<qlonglong>(value.st_mtim.tv_nsec)) + '|'
        + QByteArray::number(static_cast<qlonglong>(value.st_ctim.tv_sec)) + '|'
        + QByteArray::number(static_cast<qlonglong>(value.st_ctim.tv_nsec));
}
}
#endif

LisInboxPageWorker::LisInboxPageWorker(QObject *parent) : QObject(parent), store_(new LisInboxStore(this))
{
    connect(store_, &LisInboxStore::ready, this, [this](QString path) { ready_ = true; emit ready(path); });
    connect(store_, &LisInboxStore::ingested, this, &LisInboxPageWorker::ingested);
    connect(store_, &LisInboxStore::listReady, this, &LisInboxPageWorker::listReady);
    connect(store_, &LisInboxStore::detailReady, this, &LisInboxPageWorker::detailReady);
    connect(store_, &LisInboxStore::error, this, &LisInboxPageWorker::error);
    connect(store_, &LisInboxStore::closed, this, &LisInboxPageWorker::closed);
}
bool LisInboxPageWorker::onWorker(qint64 request, const QString &operation)
{
    if (QThread::currentThread() == thread() && QCoreApplication::instance()
            && thread() != QCoreApplication::instance()->thread()) return true;
    emit error(request, operation, QStringLiteral("原报文文件桥必须在收件箱工作线程调用。"));
    return false;
}
void LisInboxPageWorker::initialize(QString directory, AccountSession session)
{
    if (!onWorker(0, QStringLiteral("initialize")) || closing_) return;
    ready_ = false;
    session_ = session;
    store_->setSession(session_);
    store_->initialize(directory);
}
void LisInboxPageWorker::setSession(AccountSession session)
{
    if (!onWorker(0, QStringLiteral("setSession")) || closing_) return;
    session_ = session;
    store_->setSession(session_);
}
bool LisInboxPageWorker::readExplicitFile(const QString &path, QByteArray *bytes, QString *failure)
{
    // Check metadata only after admission; actual content opens below never follow links.
    const QFileInfo selected(path);
    if (path.contains(QChar('\0')) || !QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path
            || path.contains(QStringLiteral("sn100_host_reference"), Qt::CaseInsensitive)
            || !selected.isFile() || selected.isSymLink() || selected.canonicalFilePath() != path)
    { *failure = QStringLiteral("请选择绝对规范路径的匿名普通文件；不读取符号链接或参考工程数据。"); return false; }
#ifdef Q_OS_LINUX
    // Walk each component with openat/O_NOFOLLOW: a pre-open QFileInfo check alone
    // would permit a link substitution between checking and opening the file.
    const QStringList components = path.mid(1).split(QChar('/'), Qt::SkipEmptyParts);
    if (components.isEmpty()) { *failure = QStringLiteral("没有可读取的文件名。"); return false; }
    FileDescriptor parent(::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (parent.get() < 0) { *failure = QStringLiteral("无法打开文件路径根目录。"); return false; }
    for (int index = 0; index + 1 < components.size(); ++index)
    {
        const QByteArray component = QFile::encodeName(components.at(index));
        const int next = ::openat(parent.get(), component.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) { *failure = QStringLiteral("文件路径已变更、无权限或含符号链接。"); return false; }
        parent.reset(next);
    }
    const QByteArray name = QFile::encodeName(components.last());
    FileDescriptor source(::openat(parent.get(), name.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK));
    if (source.get() < 0)
    { *failure = QStringLiteral("不能安全打开所选文件：%1").arg(QString::fromLocal8Bit(std::strerror(errno))); return false; }
    struct stat before{}, after{}, namedAfter{};
    if (::fstat(source.get(), &before) != 0 || !S_ISREG(before.st_mode) || before.st_size < 0)
    { *failure = QStringLiteral("所选对象不是可安全读取的普通文件。"); return false; }
    const QByteArray stamp = fileStamp(before);
    if (!fileStamp_.isEmpty() && fileStamp_ != stamp)
    { *failure = QStringLiteral("首次安全打开后的文件身份或内容元数据已变化；请取消草稿后重新选择，不能沿用原重试身份。"); return false; }
    fileStamp_ = stamp; // Retain even if the following read fails; never retry a changed file under this UUID.
    if (before.st_size > LisDsrRaw::MaximumFrameBytes)
    { *failure = QStringLiteral("文件超过1MiB；不截断导入。"); return false; }
    QFile input;
    if (!input.open(source.get(), QIODevice::ReadOnly))
    { *failure = QStringLiteral("无法读取所选匿名文件。"); return false; }
    const QByteArray captured = input.read(LisDsrRaw::MaximumFrameBytes + 1);
    if (input.error() != QFile::NoError || !input.atEnd() || captured.size() > LisDsrRaw::MaximumFrameBytes
            || ::fstat(source.get(), &after) != 0 || !sameFile(before, after)
            || captured.size() != before.st_size
            || ::fstatat(parent.get(), name.constData(), &namedAfter, AT_SYMLINK_NOFOLLOW) != 0
            || !S_ISREG(namedAfter.st_mode) || !sameFile(before, namedAfter)
            || QFileInfo(path).canonicalFilePath() != path)
    { *failure = QStringLiteral("读取失败、文件读取中变化或超过1MiB；未提交，请取消草稿后重新选择稳定文件。"); return false; }
    *bytes = captured;
    return true;
#else
    Q_UNUSED(bytes);
    *failure = QStringLiteral("当前文件导入仅实现Linux安全读取；本平台尚未开放安全导入，可只读查看已有原报文。");
    return false;
#endif
}
void LisInboxPageWorker::importFile(qint64 request, QString path, QString identity, QString expected)
{
    const QString operation = QStringLiteral("importFile");
    if (!onWorker(request, operation)) return;
    // No file metadata/content access before the supplied session is checked here.
    if (closing_ || !ready_)
    { emit error(request, operation, QStringLiteral("收件箱尚未就绪或正在关闭。")); return; }
    if (!session_.authenticated || session_.role < 1 || session_.role > 5 || session_.source == AccountSessionSource::Guest)
    { emit error(request, operation, QStringLiteral("已认证的1至5角色方可导入；访客只读。")); return; }
    const QUuid parsed(identity);
    if (parsed.isNull() || parsed.toString(QUuid::WithoutBraces) != identity)
    { emit error(request, operation, QStringLiteral("缺少调用者确认导入时建立的规范本地接收身份。")); return; }
    if (identity != identity_)
    {
        identity_ = identity; path_ = path; expectedControl_ = expected;
        frame_.clear(); fileStamp_.clear(); frameCaptured_ = false;
    }
    else if (path != path_ || expected != expectedControl_)
    { emit error(request, operation, QStringLiteral("同一导入身份不能换文件或控制号；请先显式取消草稿。")); return; }
    if (!frameCaptured_)
    {
        QString failure;
        if (!readExplicitFile(path_, &frame_, &failure)) { emit error(request, operation, failure); return; }
        frameCaptured_ = true;
    }
    // Retries use the first captured bytes, UUID and expected control unchanged.
    // Original Store performs strict decode, transaction/readback/COMMIT and emits.
    store_->ingestBytes(request, identity_, LisDsrRaw::profileName(), expectedControl_, frame_);
}
void LisInboxPageWorker::discardDraft(qint64 request)
{
    if (!onWorker(request, QStringLiteral("discardDraft")) || closing_) return;
    identity_.clear(); path_.clear(); expectedControl_.clear(); frame_.clear(); fileStamp_.clear(); frameCaptured_ = false;
    emit draftDiscarded(request); // Memory only. Never delete committed inbox rows.
}
void LisInboxPageWorker::query(qint64 request, int offset)
{
    if (onWorker(request, QStringLiteral("query")) && !closing_) store_->query(request, offset, 20);
}
void LisInboxPageWorker::detail(qint64 request, qint64 id)
{
    if (onWorker(request, QStringLiteral("detail")) && !closing_) store_->detail(request, id);
}
void LisInboxPageWorker::close()
{
    if (!onWorker(0, QStringLiteral("close")) || closing_) return;
    closing_ = true; ready_ = false; session_ = AccountSession();
    identity_.clear(); path_.clear(); expectedControl_.clear(); frame_.clear(); fileStamp_.clear(); frameCaptured_ = false;
    store_->close(); // closed is relayed only from the actual Store receipt.
}
