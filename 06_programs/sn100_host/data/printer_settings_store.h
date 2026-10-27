#ifndef SN100_PRINTER_SETTINGS_STORE_H
#define SN100_PRINTER_SETTINGS_STORE_H

#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <functional>

// 只管理显式脱机目录中的 printer/name，不更改系统默认设备或提交打印任务。
// 枚举和文件操作均在所属worker；可替换的枚举函数仅用于固定匿名测试。
class PrinterSettingsStore : public QObject
{
    Q_OBJECT
public:
    using PrinterNames = std::function<QStringList()>;
    explicit PrinterSettingsStore(PrinterNames names = PrinterNames(), QObject *parent = nullptr);

public slots:
    void initialize(QString absoluteDirectory);
    void load();
    void save(QString name);
    void close();

signals:
    void loaded(QString name, QStringList available);
    void saved(QString name, QStringList available);
    void error(QString operation, QString message);
    void closed();

private:
    PrinterNames names_;
    QString path_;
    QString committed_;
    QByteArray committedBytes_;
    QStringList available_;
    bool ready_ = false;
    bool checkThread(const QString &operation);
    bool readBytes(QByteArray *bytes, QString *message);
    bool readStored(QString *name, QByteArray *bytes, QString *message);
    bool writeStored(const QString &name, const QByteArray &expected, QByteArray *written, QString *message);
    bool enumerate(QStringList *names, QString *message);
};

#endif
