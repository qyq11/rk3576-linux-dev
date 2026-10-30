#ifndef SN100_LIS_SETTINGS_STORE_H
#define SN100_LIS_SETTINGS_STORE_H

#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <QVariantMap>
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
    // 原底层配置；原Windows设置页没有这些控件，页面编辑九个可见项时原样保留。
    QString tcpType, sequenceNumber, serverId, linkIntervalMs;
    int dataBits = 0;
    int parity = 0; // 沿Qt原枚举：无0、偶2、奇3、空格4、标记5。
    int stopBits = 0; // 沿Qt原枚举：1位1、1.5位3、2位2。
    int flowControl = 0; // 无0、硬件1、软件2。
    bool serialFramingKnown = false;
    bool simulation = false;
    bool uploadForHole = false;
    bool utf16 = false;
    bool sampleCodeCanRepeatTest = true;
    bool originalProfile = false;
    QString originalRoot, originalFingerprint;
    QVariantMap originalValues;
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
    void initializeOriginal(QString root);
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
    QString originalRoot_;
    QString originalFingerprint_;
    bool checkThread(const QString &operation);
    bool enumerate(QStringList *names, QString *message);
    bool readBytes(QByteArray *bytes, QString *message);
    bool readStored(LisSettings *settings, QByteArray *bytes, QString *message, QString *fingerprint = nullptr);
    bool writeStored(const LisSettings &settings, const QByteArray &expected,
                     QByteArray *written, QString *message);
};

#endif
