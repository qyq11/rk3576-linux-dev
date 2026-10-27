#include "printer_report_shell.h"
#include "printer_settings_store.h"
#include "report_controller.h"
#include <QThread>

PrinterReportShell::PrinterReportShell(PrinterSettingsStore *store, QObject *parent) : QObject(parent)
{
    if (!store) return;
    connect(store, &PrinterSettingsStore::loaded, this,
            [this](const QString &name, const QStringList &) { receiveCommitted(name); }, Qt::QueuedConnection);
    connect(store, &PrinterSettingsStore::saved, this,
            [this](const QString &name, const QStringList &) { receiveCommitted(name); }, Qt::QueuedConnection);
    connect(store, &PrinterSettingsStore::error, this, [this](const QString &operation, const QString &message) {
        // save失败不把草稿变成提交值；初始化/重读失败则不能声明有可用绑定。
        if (operation != QStringLiteral("save")) invalidate();
        emit applicationError(message);
    }, Qt::QueuedConnection);
    connect(store, &PrinterSettingsStore::closed, this, [this]() { invalidate(); }, Qt::QueuedConnection);
    connect(store, &QObject::destroyed, this, [this]() { invalidate(); }, Qt::QueuedConnection);
}

PrinterReportShell::~PrinterReportShell()
{
    invalidate();
}

void PrinterReportShell::receiveCommitted(QString name)
{
    if (name.size() > 1024 || name.contains(QChar::Null))
    {
        invalidate();
        emit applicationError(tr("已提交打印设备名称无效，未绑定报告。"));
        return;
    }
    committed_ = name;
    ready_ = true;
    if (reports_) reports_->setCommittedPrinter(name);
}

void PrinterReportShell::invalidate()
{
    ready_ = false;
    committed_.clear();
    if (reports_) reports_->invalidatePrinterBinding();
}

void PrinterReportShell::setReportController(HistoryReportController *controller)
{
    if (QThread::currentThread() != thread() || (controller && controller->thread() != thread()))
    {
        emit applicationError(tr("打印应用桥和报告控制器必须在同一GUI线程绑定。"));
        return;
    }
    if (reports_ == controller) return;
    if (reports_) reports_->invalidatePrinterBinding();
    reports_ = controller;
    if (!reports_) return;
    if (ready_) reports_->setCommittedPrinter(committed_);
    else reports_->invalidatePrinterBinding();
}
