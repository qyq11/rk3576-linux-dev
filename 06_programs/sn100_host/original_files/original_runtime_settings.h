#ifndef SN100_ORIGINAL_RUNTIME_SETTINGS_H
#define SN100_ORIGINAL_RUNTIME_SETTINGS_H

#include "original_ini.h"
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>

namespace OriginalRuntime
{
struct ThemeOptions
{
    QString shortName, company, driverModel, hospitalName, logoSize;
    int reagentPositions = 20, sampleHoles = 8, emergencyHoles = 3, testChannels = 18, reportTableType = 0;
    QMap<QString, QString> colours; // 原点号分隔的RGBA只在应用时换逗号，原配置文字不变。
};
struct UserOptions
{
    bool touchKeyboard = false, containerIsTube = false, sampleScanClosed = false;
    bool sampleHoleScanClosed = false, emptyHoleScanWorks = true;
    bool deleteFinishedRacks = true, deleteFinishedTasks = false, averageEnabled = true;
    QString scanDefaultBottle = QStringLiteral("7mL");
};
struct DebugOptions
{
    bool sendImmediately = false, sendByRowId = true, deleteSentTasks = false;
    bool reconnect = true, allowMultipleInstances = false, temperatureControl = false;
    int reconnectMilliseconds = 3000;
    bool showCommandsInitially = false, showCommandFilter = false, maskCommandFilter = false;
    QStringList shownCommands, maskedCommands;
    bool requestedAutoData = false, requestedAutoCalibration = false;
    QMap<QString, QString> requestedAutomaticProjects; // isDataAuto/PT等是模拟结果原串，不是布尔开关。
    bool loginOpen = true;
    int defaultPermission = 0;
};
struct SerialOptions
{
    QString name;
    int baudRate = 9600, dataBits = 8;
    QString dataBitsName, parity, stopBits, flowControl;
};
struct LisOptions
{
    bool simulated = false, automaticGet = false, automaticSend = false, link = false, uploadByHole = false;
    int linkIntervalMilliseconds = 1000, port = 0;
    QString type, tcpType;
    QString address, sequenceNumber;
    SerialOptions serial;
};
struct ConnectionOptions
{
    QString mainAddress, logAddress;
    int mainPort = 5000;
    bool linkOpen = false;
};
struct RemoteAccessOptions
{
    int terminalPort = 0;
    QString terminalType;
    QString terminalAddress, terminalUser, terminalPassword;
    QString shareAddress, shareUser, sharePassword;
    // 仅供用户明确请求的适配入口使用；加载此结构绝不启动进程、挂载共享或建立连接。
};
struct LogOptions
{
    bool assembly = true, driver = true, fatal = true;
    QMap<QString, bool> components;
    QString originalDirectory;
    QJsonObject original;
    bool accepts(const QString &component, const QString &kind) const;
};
struct RuntimeSettings
{
    QString root, printerName, curveType, dilutionConfiguration, reagentConfiguration, savedHospitalName;
    QString barcodeOne, barcodeTwo, legacyMysqlHost, legacyMysqlUser;
    bool fitCapacityByHeight = false;
    ThemeOptions theme;
    UserOptions user;
    DebugOptions debug;
    LisOptions lis;
    ConnectionOptions connection;
    RemoteAccessOptions remote;
    LogOptions log;
    QJsonObject algorithmProjects;
    QMap<QString, OriginalIni::Settings> originalIniFiles;
    QMap<QString, QStringList> unconsumedKeys;
    QMap<QString, QVector<QPair<double, double>>> legacyCapacityPoints;
    QMap<QString, double> legacySurfaceOffsets;
    QMap<QString, double> legacyNeedleOffsets;
    // 只有调用这些有明确用途的接口才是应用配置，加载不会启用旧连接/模拟造数。
    // 原调试页：发送仅受总开关；接收按包含匹配，显示列表优先于屏蔽列表。
    bool showCommunicationCommand(const QString &command, bool sending = false) const;
    bool algorithmParameters(const QString &projectName, QJsonObject *parameters, QString *message) const;
    bool legacyCorrectedHeight(const QString &section, const QString &hole, int needle,
                                double measuredMillimetres, double *correctedMillimetres, QString *message) const;
};
bool loadRuntimeSettings(const QString &root, RuntimeSettings *result, QString *message);
// 替代Windows DLL的本地日志能力。输出根必须在原工作根内，按原GL4A_LOG分类落盘。
bool appendConfiguredLog(const RuntimeSettings &settings, const QString &component, const QString &kind,
                          const QString &messageText, QString *message);
}

#endif
