#include "original_runtime_settings.h"
#include "original_business_database.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QRegularExpression>
#include <cmath>

namespace OriginalRuntime
{
namespace
{
bool fail(QString *message, const QString &text)
{
    if (message) *message = text;
    return false;
}
class IniReader
{
public:
    OriginalIni::Settings settings;
    QSet<QString> consumed;
    QStringList diagnostics; // 仅记录键名及错误类别，不记录原配置值。
    bool valid = true;
    QVariant value(const QString &key, const QVariant &fallback = QVariant())
    {
        if (settings.values.contains(key)) { consumed.insert(key); return settings.values.value(key); }
        // 原Windows INI键名大小写不敏感；只在唯一匹配时适配，不改原键名。
        QString found;
        for (auto it = settings.values.cbegin(); it != settings.values.cend(); ++it)
            if (it.key().compare(key, Qt::CaseInsensitive) == 0)
            {
                if (!found.isEmpty()) { valid = false; diagnostics << key + ":ambiguous_case"; return fallback; }
                found = it.key();
            }
        if (!found.isEmpty()) { consumed.insert(found); return settings.values.value(found); }
        return fallback;
    }
    QString text(const QString &key, const QString &fallback = QString()) { return value(key, fallback).toString(); }
    bool flag(const QString &key, bool fallback = false)
    {
        const QString textValue = value(key, fallback).toString().trimmed().toLower();
        if (textValue == "true" || textValue == "1") return true;
        if (textValue == "false" || textValue == "0" || textValue.isEmpty()) return false;
        valid = false;
        diagnostics << key + ":expected_boolean";
        return fallback;
    }
    int number(const QString &key, int fallback, int minimum, int maximum)
    {
        const QVariant stored = value(key, fallback);
        if (stored.toString().isEmpty()) return fallback;
        bool ok = false;
        const int numberValue = stored.toString().toInt(&ok);
        if (!ok || numberValue < minimum || numberValue > maximum)
        { valid = false; diagnostics << key + ":expected_bounded_integer"; return fallback; }
        return numberValue;
    }
    QStringList remaining() const
    {
        QStringList result;
        for (const QString &key : settings.values.keys()) if (!consumed.contains(key)) result << key;
        return result;
    }
};
bool jsonObject(const QString &root, const QString &relative, QJsonObject *result, QString *message)
{
    const QString path = QDir(root).filePath(relative);
    for (QFileInfo entry(path); ; entry.setFile(entry.absolutePath()))
    {
        if (entry.isSymLink()) return fail(message, QStringLiteral("原配置路径不能经过符号链接。"));
        if (entry.absoluteFilePath() == entry.absolutePath()) break;
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly) || input.size() > 8 * 1024 * 1024)
        return fail(message, QStringLiteral("原配置文件无法读取或超过8MB：") + relative);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return fail(message, QStringLiteral("原配置不是完整JSON对象：") + relative);
    *result = document.object();
    return true;
}
bool parseLegacy(const OriginalIni::Settings &settings, RuntimeSettings *result, QString *message)
{
    static const QRegularExpression bracket(QStringLiteral("\\[([^\\]]*)\\]"));
    for (auto it = settings.values.cbegin(); it != settings.values.cend(); ++it)
    {
        const QString raw = it.value().toString();
        if (it.key().startsWith("needle/"))
        {
            bool ok = false;
            const double offset = raw.toDouble(&ok);
            if (!ok || !std::isfinite(offset)) return fail(message, QStringLiteral("原针补偿参数不是有限数。"));
            result->legacyNeedleOffsets.insert(it.key().mid(7), offset);
            continue;
        }
        auto matches = bracket.globalMatch(raw);
        QVector<QPair<double, double>> points;
        QVector<double> offsets;
        int matchedLength = 0;
        while (matches.hasNext())
        {
            const auto match = matches.next();
            matchedLength += match.captured(0).size();
            const QStringList pair = match.captured(1).split(',');
            bool xOk = false, yOk = false;
            const double x = pair.value(0).toDouble(&xOk);
            if (pair.size() == 2)
            {
                const double y = pair[1].toDouble(&yOk);
                if (!xOk || !yOk || !std::isfinite(x) || !std::isfinite(y))
                    return fail(message, QStringLiteral("原旧式容量点参数无效。"));
                points.append({x, y});
            }
            else if (pair.size() == 1 && xOk && std::isfinite(x)) offsets << x;
            else return fail(message, QStringLiteral("原旧式容量表达式不是已知点列，未猜测公式。"));
        }
        if (matchedLength != raw.size() || (points.isEmpty() && offsets.isEmpty()))
            return fail(message, QStringLiteral("原旧式容量表达式不完整。"));
        if (!points.isEmpty()) result->legacyCapacityPoints.insert(it.key(), points);
        if (!offsets.isEmpty()) result->legacySurfaceOffsets.insert(it.key(), offsets.first());
    }
    return true;
}
}

bool loadRuntimeSettings(const QString &root, RuntimeSettings *result, QString *message)
{
    if (!result || !OriginalBusiness::workingRoot(root, message)) return false;
    RuntimeSettings candidate;
    candidate.root = QFileInfo(root).canonicalFilePath();
    QMap<QString, IniReader> readers;
    const QStringList files{"config.ini", "config_Debug.ini", "theme_config.ini", "calibrationFile/ReagentAmountCalibration.ini"};
    for (const auto &relative : files)
    {
        IniReader reader;
        if (!OriginalIni::read(QDir(root).filePath(relative), &reader.settings, message)) return false;
        candidate.originalIniFiles.insert(relative, reader.settings);
        readers.insert(relative, reader);
    }
    auto &normal = readers["config.ini"];
    auto &debug = readers["config_Debug.ini"];
    auto &theme = readers["theme_config.ini"];
    candidate.theme.shortName = theme.text("theme/name");
    candidate.theme.company = theme.text("theme/company");
    candidate.theme.driverModel = theme.text("theme/DriverModel");
    candidate.theme.hospitalName = theme.text("hospital/name");
    candidate.theme.logoSize = theme.text("theme/logoSize", "82x82");
    candidate.theme.reagentPositions = theme.number("theme/ReagentPanelChannelCount", 20, 1, 1000);
    candidate.theme.sampleHoles = theme.number("theme/SampleHoleCount", 8, 1, 1000);
    candidate.theme.emergencyHoles = theme.number("theme/EmergencyHoleCount", 3, 1, 1000);
    candidate.theme.testChannels = theme.number("theme/ChannelCount", 18, 1, 1000);
    candidate.theme.reportTableType = theme.number("theme/ReportTableType", 0, 0, 20);
    const QStringList colours{"leftBaseColor", "leftBtnColor", "leftBtnFontColor", "rightBaseColor", "rightBtnColor",
        "rightBtnFontColor", "matchColor", "matchFontColor", "matchHoverColor", "kBtnNormalColor", "kBtnHoverColor",
        "kBtnPressedColor", "kReagentHoleSelectedColor", "kCalibrationLineColor", "kSampleSetBtnChecked",
        "kSampleSetWidget", "kSampleSetRowSelected", "kListWidgetRowSelected", "kReportRowSelected"};
    for (const auto &name : colours)
    {
        QString value = theme.text("theme/" + name, name.contains("Font") ? QStringLiteral("255,255,255,255") : QString());
        value.replace('.', ',');
        if (!value.isEmpty()) candidate.theme.colours.insert(name, value);
    }
    auto &user = candidate.user;
    user.touchKeyboard = normal.flag("userOption/isTouchDeviceOpen");
    user.containerIsTube = normal.flag("userOption/isContainerTube");
    user.sampleScanClosed = normal.flag("userOption/isSampleScanClose");
    user.sampleHoleScanClosed = normal.flag("userOption/isSampleHoleScanClose");
    user.emptyHoleScanWorks = normal.flag("userOption/isSampleHoleScanOpenHoleNoneWork", true);
    user.scanDefaultBottle = normal.text("userOption/scanDefaultBottleType", "7mL");
    user.deleteFinishedRacks = normal.flag("userOption/RackFinishedDelete", true);
    user.deleteFinishedTasks = normal.flag("userOption/TaskFinishedDelete", false);
    user.averageEnabled = normal.flag("userOption/isOpenAverage", true);
    candidate.dilutionConfiguration = normal.text("calibrationOption/diluentConfig");
    candidate.reagentConfiguration = normal.text("calibrationOption/reagentConfig");
    candidate.fitCapacityByHeight = normal.flag("calibrationOption/isVolFitHigh");
    candidate.printerName = normal.text("printer/name");
    candidate.savedHospitalName = normal.text("hospital/name"); // 保留保存来源，不覆盖theme启动来源。
    candidate.curveType = normal.text("curve/type", "raw");
    candidate.barcodeOne = normal.text("code/code1");
    candidate.barcodeTwo = normal.text("code/code2");
    auto &trace = candidate.debug;
    trace.sendImmediately = debug.flag("userOption/SendTaskImmediate");
    trace.sendByRowId = debug.flag("userOption/SendTaskByRowID", true);
    trace.deleteSentTasks = debug.flag("userOption/TaskSendDelete");
    trace.reconnect = debug.flag("userOption/ReconnectTimer", true);
    trace.reconnectMilliseconds = qMax(2000, debug.number("userOption/ReconnectTime", 3000, 0, 86400000));
    trace.allowMultipleInstances = debug.flag("userOption/isAppOpenMultiple");
    trace.temperatureControl = debug.flag("userOption/isTempCtrl");
    trace.showCommandsInitially = debug.flag("TCPTest/isDefaultShowOpen");
    trace.showCommandFilter = debug.flag("TCPTest/isShowOpen");
    trace.maskCommandFilter = debug.flag("TCPTest/isMaskOpen");
    // 原QString::split保留空项：空显示项匹配所有接收，空屏蔽项屏蔽所有接收。
    trace.shownCommands = debug.text("TCPTest/ShowCommand").split('|');
    trace.maskedCommands = debug.text("TCPTest/MaskCommand").split('|');
    trace.requestedAutoData = debug.flag("isDataAuto/isOpen");
    trace.requestedAutoCalibration = debug.flag("isDataAuto/isCalibrationAuto");
    for (const QString &key : debug.settings.values.keys())
        if (key.startsWith("isDataAuto/") && key != "isDataAuto/isOpen" && key != "isDataAuto/isCalibrationAuto")
            trace.requestedAutomaticProjects.insert(key.mid(11), debug.text(key));
    trace.loginOpen = debug.flag("login/open", true);
    trace.defaultPermission = debug.number("login/defaultPermission", 0, 0, 1000000000);
    candidate.legacyMysqlHost = debug.text("mysql/hostName");
    candidate.legacyMysqlUser = debug.text("mysql/userName");
    auto &lis = candidate.lis;
    lis.simulated = normal.flag("lis/moni"); lis.type = normal.text("lis/type");
    lis.automaticGet = normal.flag("lis/autoget"); lis.automaticSend = normal.flag("lis/autosend");
    lis.link = normal.flag("lis/link");
    lis.linkIntervalMilliseconds = normal.number("lis/linkintervalms", 1000, 0, 86400000);
    lis.serial.name = normal.text("lis/commname");
    lis.serial.baudRate = normal.number("lis/baudrate", 9600, 1, 10000000);
    lis.serial.dataBitsName = normal.text("lis/databits", "data8");
    const auto bits = QRegularExpression(QStringLiteral("^(?:data)?([5-8])$")).match(lis.serial.dataBitsName);
    if (!bits.hasMatch()) { normal.valid = false; normal.diagnostics << "lis/databits:expected_data5_to_data8"; }
    else lis.serial.dataBits = bits.captured(1).toInt();
    lis.serial.parity = normal.text("lis/parity"); lis.serial.stopBits = normal.text("lis/stopbits");
    lis.serial.flowControl = normal.text("lis/flowcontrol");
    lis.address = normal.text("lis/ip"); lis.port = normal.number("lis/port", 0, 0, 65535);
    lis.tcpType = normal.text("lis/tcptype"); lis.uploadByHole = normal.flag("lis/isUploadForHole");
    lis.sequenceNumber = normal.text("lis/sequencenumber");
    candidate.connection.mainAddress = normal.text("tcpmain/ip");
    candidate.connection.mainPort = normal.number("tcpmain/port", 5000, 1, 65535);
    candidate.connection.logAddress = normal.text("tcplog/ip");
    candidate.connection.linkOpen = normal.flag("link/open");
    auto &remote = candidate.remote;
    remote.terminalType = normal.text("putty/type"); remote.terminalAddress = normal.text("putty/IP");
    remote.terminalPort = normal.number("putty/port", 0, 0, 65535);
    remote.terminalUser = normal.text("putty/usr"); remote.terminalPassword = normal.text("putty/pswd");
    remote.shareAddress = normal.text("ShareFile/IP"); remote.shareUser = normal.text("ShareFile/user");
    remote.sharePassword = normal.text("ShareFile/pswd");
    if (!normal.valid || !debug.valid || !theme.valid)
        return fail(message, QStringLiteral("原INI类型不兼容，仅列键名：") +
                    (normal.diagnostics + debug.diagnostics + theme.diagnostics).join(','));
    if (!parseLegacy(readers["calibrationFile/ReagentAmountCalibration.ini"].settings, &candidate, message)) return false;
    readers["calibrationFile/ReagentAmountCalibration.ini"].consumed = QSet<QString>(
        readers["calibrationFile/ReagentAmountCalibration.ini"].settings.values.keyBegin(),
        readers["calibrationFile/ReagentAmountCalibration.ini"].settings.values.keyEnd());
    for (const auto &file : files) candidate.unconsumedKeys.insert(file, readers[file].remaining());
    if (!jsonObject(root, "lib/config/AL4A_config.cfg", &candidate.algorithmProjects, message) ||
        !jsonObject(root, "lib/config/GL4A_config.cfg", &candidate.log.original, message)) return false;
    const auto &log = candidate.log.original;
    for (const QString &key : {QStringLiteral("Assem"), QStringLiteral("Driver"), QStringLiteral("Fatal")})
        if (!log.value(key).isBool()) return fail(message, QStringLiteral("原日志开关不是布尔值。"));
    candidate.log.assembly = log.value("Assem").toBool(); candidate.log.driver = log.value("Driver").toBool();
    candidate.log.fatal = log.value("Fatal").toBool(); candidate.log.originalDirectory = log.value("dir").toString();
    if (!log.value("Comp").isArray()) return fail(message, QStringLiteral("原日志组件列表无效。"));
    for (const auto &item : log.value("Comp").toArray())
    {
        const auto component = item.toObject();
        const QString name = component.value("name").toString();
        if (name.isEmpty() || !component.value("open").isBool() || candidate.log.components.contains(name))
            return fail(message, QStringLiteral("原日志组件名称或开关无效。"));
        candidate.log.components.insert(name, component.value("open").toBool());
    }
    *result = candidate;
    return true;
}
bool RuntimeSettings::showCommunicationCommand(const QString &command, bool sending) const
{
    if (!debug.showCommandsInitially) return false;
    if (sending) return true;
    if (debug.showCommandFilter)
    {
        for (const QString &shown : debug.shownCommands)
            if (command.contains(shown)) return true;
        return false;
    }
    if (debug.maskCommandFilter)
        for (const QString &masked : debug.maskedCommands)
            if (command.contains(masked)) return false;
    return true;
}
bool RuntimeSettings::algorithmParameters(const QString &name, QJsonObject *parameters, QString *message) const
{
    if (!parameters || !algorithmProjects.value(name).isObject())
        return fail(message, QStringLiteral("原算法配置中没有此项目。"));
    *parameters = algorithmProjects.value(name).toObject();
    return true; // 这里只提供真实参数；完整计算必须由可用算法实现接收，不能返回假结果。
}
bool RuntimeSettings::legacyCorrectedHeight(const QString &section, const QString &hole, int needle,
                                            double measured, double *corrected, QString *message) const
{
    if (!corrected || !std::isfinite(measured) || needle < 1 || needle > 3 ||
        !legacySurfaceOffsets.contains(section + '/' + hole))
        return fail(message, QStringLiteral("旧式面型补偿点或针号无效。"));
    const QRegularExpression suffix(QStringLiteral("(\\d+)$"));
    const auto match = suffix.match(hole);
    if (!match.hasMatch()) return fail(message, QStringLiteral("孔位名称没有原编号。"));
    double value = measured;
    if (needle == 2)
    {
        const QString key = match.captured(1).toInt() >= 15 ? QStringLiteral("rightToLeftInner") : QStringLiteral("rightToLeftOuter");
        if (!legacyNeedleOffsets.contains(key)) return fail(message, QStringLiteral("原左右针高度补偿不存在。"));
        value -= legacyNeedleOffsets.value(key);
    }
    *corrected = value - legacySurfaceOffsets.value(section + '/' + hole);
    return true;
}
bool LogOptions::accepts(const QString &component, const QString &kind) const
{
    if (components.contains(component) && !components.value(component)) return false;
    if (kind == "Fatal") return fatal;
    if (kind == "Driver") return driver;
    if (kind == "Assem") return assembly;
    return true;
}
bool appendConfiguredLog(const RuntimeSettings &settings, const QString &component, const QString &kind,
                          const QString &text, QString *message)
{
    if (!OriginalBusiness::workingRoot(settings.root, message)) return false;
    static const QRegularExpression safeName(QStringLiteral("^[A-Za-z0-9_.-]{1,64}$"));
    if (!safeName.match(component).hasMatch() || !safeName.match(kind).hasMatch() || text.size() > 65536)
        return fail(message, QStringLiteral("日志分类或单条长度无效。"));
    if (!settings.log.accepts(component, kind)) return true;
    QString directory = QDir::cleanPath(settings.log.originalDirectory);
    if (directory.compare("GL4A_log", Qt::CaseInsensitive) == 0) directory = "GL4A_LOG";
    if (directory != "GL4A_LOG") return fail(message, QStringLiteral("原日志路径不是已确认的GL4A_LOG目录，未写到其他位置。"));
    const QString absolute = QDir(settings.root).filePath(directory);
    if (QFileInfo(absolute).isSymLink() || (!QFileInfo::exists(absolute) && !QDir().mkpath(absolute)))
        return fail(message, QStringLiteral("无法创建原分类日志目录。"));
    if (!QFile::setPermissions(absolute, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        return fail(message, QStringLiteral("无法设置原分类日志目录权限。"));
    const QString path = QDir(absolute).filePath(component + ".log");
    if (QFileInfo(path).isSymLink()) return fail(message, QStringLiteral("日志文件不能是符号链接。"));
    QLockFile lock(path + ".sn100-write.lock");
    if (!lock.tryLock(0)) return fail(message, QStringLiteral("该日志正在写入，请稍后再试。"));
    if (QFileInfo(path).size() > 4 * 1024 * 1024)
    {
        const QString previous = path + ".1";
        if (QFileInfo(previous).isSymLink() || (QFileInfo::exists(previous) && !QFile::remove(previous)) || !QFile::rename(path, previous))
            return fail(message, QStringLiteral("日志滚动失败，未无限增长。"));
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner))
        return fail(message, QStringLiteral("无法写入原分类日志。"));
    const QByteArray bytes = (QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + ' ' + kind + ' ' + text + '\n').toUtf8();
    if (file.write(bytes) != bytes.size() || !file.flush()) return fail(message, QStringLiteral("原分类日志未完整写入。"));
    return true;
}
}
