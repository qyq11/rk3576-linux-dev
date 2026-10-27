#pragma once

#include "ui/cusdialogmask.h"
#include "ui/maskdlglogin.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWindow>

// Private diagnostic copy only. No widget text, account data or secret is read.
// Buffer geometry in memory; write it after the original test method returns,
// including a normal QVERIFY early return. Do not wait for card containment.
class LoginGeometryProbe : public QObject
{
public:
    explicit LoginGeometryProbe(QScrollArea &host) : host_(host)
    {
        clock_.start();
        qApp->installEventFilter(this);
        mark("probe_started");
    }
    ~LoginGeometryProbe() override
    {
        qApp->removeEventFilter(this);
        mark("method_returned");
        const QString path = qEnvironmentVariable("SN100_DIAG_GEOMETRY_TRACE");
        QFile file(path);
        const QJsonObject value{{"schema", "sn100-private-login-geometry-v1"},
            {"pending_move_requested", qEnvironmentVariableIsSet("SN100_DIAG_PENDING_HOST_MOVE")},
            {"pending_moves_executed", movesExecuted_}, {"buffer_overflow", overflow_}, {"events", rows_}};
        const QByteArray bytes = QJsonDocument(value).toJson(QJsonDocument::Indented);
        if (!QDir::isAbsolutePath(path) || !file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
            file.write(bytes) != bytes.size())
            qWarning("LOGIN_GEOMETRY_PROBE_WRITE_FAILED");
    }
    void mark(const char *stage)
    {
        record(stage, -1, nullptr);
    }
    void prepareTransition(const QSize &requested)
    {
        mark("before_host_transition");
        if (!qEnvironmentVariableIsSet("SN100_DIAG_PENDING_HOST_MOVE")) return;
        QScreen *screen = host_.screen();
        if (!screen) return;
        const QRect available = screen->availableGeometry();
        // Both target client rectangles fit the owned screen when it is large
        // enough. This is an explicit synthetic pending host move, not a claim
        // about what happened in suite07. No other window is operated on.
        const int roomX = qMax(0, available.width() - requested.width());
        const int roomY = qMax(0, available.height() - requested.height());
        const QPoint offset = requested.width() == 1600
            ? QPoint(qMin(20, roomX), qMin(20, roomY))
            : QPoint(qMax(0, roomX - 20), qMax(0, roomY - 20));
        const QPoint target = available.topLeft() + offset;
        QTimer::singleShot(0, this, [this, target]()
        {
            mark("pending_host_move_dispatched");
            host_.move(target);
            ++movesExecuted_;
            mark("pending_host_move_requested");
        });
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type())
        {
        case QEvent::Move: case QEvent::Resize: case QEvent::Show: case QEvent::Hide:
        case QEvent::WindowActivate: case QEvent::WindowDeactivate: case QEvent::Expose:
            break;
        default:
            return false;
        }
        auto *widget = qobject_cast<QWidget *>(watched);
        const bool relevant = watched == &host_ || watched == host_.windowHandle() ||
            (widget && (qobject_cast<MaskDlgLogin *>(widget) || widget->objectName() == "view_frame"));
        if (relevant) record("event", int(event->type()), watched);
        return false; // Observe only; retain the actual widget/WM event path.
    }
private:
    static QJsonArray rectangle(const QRect &value)
    {
        return {value.x(), value.y(), value.width(), value.height()};
    }
    static QJsonObject geometry(QWidget *widget)
    {
        if (!widget) return {};
        return {{"class", QString::fromLatin1(widget->metaObject()->className())},
            {"name", widget->objectName()}, {"local", rectangle(widget->geometry())},
            {"global", rectangle(QRect(widget->mapToGlobal(QPoint()), widget->size()))},
            {"frame", rectangle(widget->frameGeometry())}, {"visible", widget->isVisible()},
            {"active", widget->isActiveWindow()},
            {"exposed", widget->windowHandle() && widget->windowHandle()->isExposed()}};
    }
    void record(const char *stage, int eventType, QObject *watched)
    {
        if (rows_.size() >= 512) { overflow_ = true; return; }
        MaskDlgLogin *dialog = nullptr;
        for (auto *candidate : host_.findChildren<MaskDlgLogin *>())
            if (candidate->isVisible()) { dialog = candidate; break; }
        auto *card = dialog ? dialog->findChild<QFrame *>("view_frame") : nullptr;
        auto *mask = dialog ? dialog->findChild<CusDialogMask *>("mask") : nullptr;
        QJsonObject row{{"sequence", rows_.size() + 1}, {"ms", double(clock_.nsecsElapsed()) / 1e6},
            {"stage", stage}, {"event_type", eventType},
            {"watched_class", watched ? watched->metaObject()->className() : ""},
            {"host", geometry(&host_)}, {"viewport", geometry(host_.viewport())},
            {"dialog", geometry(dialog)}, {"card", geometry(card)},
            {"mask_ready", mask && mask->isReadyForInput()},
            {"hscroll", host_.horizontalScrollBar()->value()},
            {"vscroll", host_.verticalScrollBar()->value()}};
        if (card)
            row.insert("host_contains_card", QRect(host_.mapToGlobal(QPoint()), host_.size()).contains(
                QRect(card->mapToGlobal(QPoint()), card->size())));
        if (QScreen *screen = host_.screen())
        {
            row.insert("screen", rectangle(screen->geometry()));
            row.insert("screen_available", rectangle(screen->availableGeometry()));
            row.insert("logical_dpi", screen->logicalDotsPerInch());
            row.insert("device_pixel_ratio", screen->devicePixelRatio());
        }
        rows_.append(row);
    }
    QScrollArea &host_;
    QElapsedTimer clock_;
    QJsonArray rows_;
    int movesExecuted_ = 0;
    bool overflow_ = false;
};
