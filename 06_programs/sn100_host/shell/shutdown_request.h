#ifndef SN100_SHUTDOWN_REQUEST_H
#define SN100_SHUTDOWN_REQUEST_H

#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

// 将进程停止请求交给原窗口关闭守卫；拒绝关闭时仍保留草稿和在途操作。
class ShutdownRequest : public QObject
{
public:
    explicit ShutdownRequest(QObject *parent = nullptr);
    ~ShutdownRequest() override;

    bool initialize(QString *error = nullptr);
    void setWindow(QWidget *window);

private:
    class NativeState;
    NativeState *native_ = nullptr;
    QPointer<QWidget> window_;
    bool handling_ = false;

    void handlePending();
};

#endif
