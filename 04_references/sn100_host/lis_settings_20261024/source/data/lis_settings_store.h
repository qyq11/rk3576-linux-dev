#ifndef SN100_LIS_SETTINGS_STORE_H
#define SN100_LIS_SETTINGS_STORE_H

#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <functional>

// 原LIS页面的九个可见键。空端点表示尚未配置，不猜测设备参数。
struct LisSettings
{
    QString type;
    QString ip;
    QString port;
    QString commName;
    QString baudrate;
    int sampleMode = 0;
    bool autoGet = false;
    bool autoSend = false;
    bool link = false;
    bool operator==(const LisSettings &other) const;
    bool operator!=(const LisSettings &other) const { return !(*this == other); }
};
Q_DECLARE_METATYPE(LisSettings)

// 仅在显式隔离目录保存本地意图；不创建socket、listener或打开串口。
// QObject/INI/只读串口枚举均归调用者安排的worker线程。
class LisSettingsStore : public QObject
{
    Q_OBJECT
public:
    using PortNames = std::function<QStringList()>;
    explicit LisSettingsStore(PortNames names = PortNames(), QObject *parent = nullptr);
    static QStringList supportedBaudrates();
    static QString validate(const LisSettings &settings);

public slots:
    void initialize(QString absoluteDirectory);
    void load();
    void save(LisSettings settings);
    void close();

signals:
    void loaded(LisSettings settings, QStringList availablePorts);
    void saved(LisSettings settings, QStringList availablePorts);
    void error(QString operation, QString message);
    void closed();

private:
    PortNames names_;
    QString path_;
    QString lockPath_;
    LisSettings committed_;
    QByteArray committedBytes_;
    QStringList available_;
    bool ready_ = false;
    bool checkThread(const QString &operation);
    bool enumerate(QStringList *names, QString *message);
    bool readBytes(QByteArray *bytes, QString *message);
    bool readStored(LisSettings *settings, QByteArray *bytes, QString *message);
    bool writeStored(const LisSettings &settings, const QByteArray &expected,
                     QByteArray *written, QString *message);
};

#endif
