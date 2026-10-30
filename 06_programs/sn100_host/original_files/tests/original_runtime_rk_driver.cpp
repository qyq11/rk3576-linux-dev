// ARM64编译后在RK执行。SQL建表仅生成全新匿名夹具，不修改原参考或正式工作数据。
#include "original_files/original_system_profile.h"
#include "original_files/original_runtime_settings.h"
#include "original_files/original_capacity_calibration.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextCodec>
#include <QDebug>
#include <cmath>

static bool writeFile(const QString &root, const QString &relative, const QByteArray &bytes)
{
    const QString path = QDir(root).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
static QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static bool createDatabase(const QString &root, const QString &relative, const QStringList &statements)
{
    const QString path = QDir(root).filePath(relative);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    const QString connection = "fixture_" + relative;
    bool good = true;
    {
        auto database = QSqlDatabase::addDatabase("QSQLITE", connection);
        database.setDatabaseName(path);
        good = database.open();
        for (const auto &statement : statements)
        {
            QSqlQuery query(database);
            good = good && query.exec(statement);
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return good;
}
static QJsonObject capacityFixture()
{
    const QJsonObject base{{"R_1", QJsonObject{{"100", 1.0}, {"200", 2.0}}},
                           {"R_2", QJsonObject{{"100", 2.0}, {"200", 3.0}}}};
    const QJsonObject face{{"R_1", QJsonObject{{"100", 1.0}}}, {"R_2", QJsonObject{{"100", 2.0}}}};
    const QJsonObject volume{{"R_1", QJsonObject{{"100", 1.0}}}, {"R_2", QJsonObject{{"200", 3.0}}}};
    const QJsonObject needle{{"R_1", QJsonObject{{"100", 1.0}}}};
    QJsonObject region{{"BASE", QJsonObject{{"7mL", base}}}, {"FACE", QJsonObject{{"7mL", face}}},
                       {"VOL", QJsonObject{{"7mL", volume}}}, {"N1", QJsonObject{{"7mL", needle}}},
                       {"N2", QJsonObject{{"7mL", needle}}}, {"S1", QJsonObject{{"7mL", needle}}}};
    QJsonObject configuration;
    for (const QString &mode : {QStringLiteral("FACE"), QStringLiteral("VOL"), QStringLiteral("N1"), QStringLiteral("N2"), QStringLiteral("S1")})
        configuration.insert(mode, QJsonObject{{"7mL", QJsonObject{{"Area_R1", "SUB_R1"}}}});
    return {{"Area_R1", QJsonObject{{"SUB_R1", region}}}, {"CONFIG", configuration},
            {"vendor_unknown", QJsonObject{{"text", "keep"}, {"numeric", 17.25}, {"array", QJsonArray{1, true, "x"}}}}};
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOptions({{"output-dir", "匿名夹具和结果的新目录", "output"},
                       {"original-root", "可选：只读核对已授权原工作根", "root"}});
    parser.process(app);
    const QString output = parser.value("output-dir");
    if (!QDir::isAbsolutePath(output) || QFileInfo::exists(output) || !QDir().mkpath(output)) return 2;
    QTemporaryDir fixture(QDir(output).filePath("anonymous-XXXXXX"));
    if (!fixture.isValid()) return 2;
    const QString root = fixture.path();
    int passed = 0, failed = 0;
    QJsonArray checks;
    const auto check = [&](bool good, const QString &name)
    {
        good ? ++passed : ++failed;
        checks.append(QJsonObject{{"name", name}, {"passed", good}});
        qInfo().noquote() << (good ? "PASS" : "FAIL") << name;
        return good;
    };
    check(writeFile(root, ".sn100-original-work-v1", "sn100-original-work-v1\n"), QStringLiteral("匿名工作根标记"));
    check(createDatabase(root, "db/systemInfo/user.db", {
        "CREATE TABLE user_role(id INTEGER PRIMARY KEY,role TEXT)",
        "CREATE TABLE user_powers(id INTEGER PRIMARY KEY,describe TEXT,can_role_id TEXT)",
        QStringLiteral("INSERT INTO user_role VALUES(0,'访客'),(5,'管理员')"),
        QStringLiteral("INSERT INTO user_powers VALUES(1,'校准','5;')")}), QStringLiteral("匿名原角色表夹具"));
    check(createDatabase(root, "db/systemInfo/channel.db", {
        "CREATE TABLE test_channel(id INTEGER PRIMARY KEY,channel_num TEXT,warmup_only INTEGER,is_open INTEGER)",
        "CREATE TABLE warm_channel(id INTEGER PRIMARY KEY,channel_num TEXT,is_open INTEGER)",
        "CREATE TABLE cache_channel(id INTEGER PRIMARY KEY,channel_num TEXT,is_open INTEGER)",
        "INSERT INTO test_channel VALUES(1,'001',0,1),(2,'002',1,1)",
        "INSERT INTO warm_channel VALUES(1,'W1',1)", "INSERT INTO cache_channel VALUES(1,'C1',0)"}), QStringLiteral("匿名原三类通道表夹具"));
    check(createDatabase(root, "db/systemInfo/result_Check.db", {
        "CREATE TABLE result_check(id INTEGER PRIMARY KEY,describe TEXT,type TEXT,result_message_cn TEXT,result_message_en TEXT,bit_index INTEGER)",
        QStringLiteral("INSERT INTO result_check VALUES(1,'阈值','2','检查结果','Check result',1)")}), QStringLiteral("匿名原结果标记夹具"));
    const QString csv = QStringLiteral("id,error_code,describe_cn,measure,dowork_notesting,dowork_testing,dtest_status,open,para\r\n"
        "1,AB001,中文阻止规则,,stop_all,stop_sample_now,error_stop,TRUE,\r\n"
        "2,AB001,原重复码保留,,none,warning,warning,TRUE,\r\n"
        "3,AB002,普通提示,,none,none,none,FALSE,\r\n");
    check(writeFile(root, "db/systemInfo/abnormal_Config.csv", QTextCodec::codecForName("GB18030")->fromUnicode(csv)),
          QStringLiteral("GB18030中文异常规则夹具"));
    QString message;
    OriginalRuntime::SystemProfile profile;
    const bool loaded = check(OriginalRuntime::loadSystemProfile(root, &profile, &message), QStringLiteral("真实支持入口读取系统资料"));
    if (loaded)
    {
        check(profile.users.allows("1", 5) && !profile.users.allows("1", 0) && !profile.users.allows("9", 5), QStringLiteral("原权限字典实际决定允许与拒绝"));
        check(profile.users.grantedPowers(5) == QStringList{"1"}, QStringLiteral("会话取得原授权集合"));
        check(profile.channels.activeTestNumbers() == QStringList{"001"} &&
              profile.channels.activeTestNumbers(true) == QStringList({"001", "002"}), QStringLiteral("原启用与仅预热通道决定可用列表"));
        check(profile.messagesForBits(QVector<bool>{false, true}) == QStringList{QStringLiteral("检查结果")}, QStringLiteral("原结果位序映射中文信息"));
        check(profile.messagesForBits(QVector<bool>{false, true}, true) == QStringList{"Check result"}, QStringLiteral("原结果位序映射英文信息"));
        check(profile.alarms.size() == 3 && profile.alarms[0].description == QStringLiteral("中文阻止规则"), QStringLiteral("中文CSV无损读取且重复码原行保留"));
        check(!profile.mayStartWithAlarms({"AB001"}, &message) && profile.mayStartWithAlarms({"AB002"}, &message), QStringLiteral("异常配置实际阻止或允许开始"));
        const QString channelPath = QDir(root).filePath("db/systemInfo/channel.db");
        check(OriginalRuntime::setTestChannelEnabled(root, "001", false, profile.channels.fingerprint, &message), QStringLiteral("原通道开关写回同一原表"));
        check(!OriginalRuntime::setTestChannelEnabled(root, "001", true, profile.channels.fingerprint, &message), QStringLiteral("通道旧指纹拒绝覆盖"));
        check(OriginalRuntime::loadSystemProfile(root, &profile, &message) && profile.channels.activeTestNumbers().isEmpty(), QStringLiteral("通道保存后读回实际应用"));
        const QByteArray before = readFile(channelPath);
        check(OriginalRuntime::setTestChannelEnabled(root, "001", false, profile.channels.fingerprint, &message) && before == readFile(channelPath), QStringLiteral("同值通道提交不重写DB字节"));
    }
    check(writeFile(root, "config.ini", "[userOption]\nisTouchDeviceOpen=true\n[tcpmain]\nip=127.0.0.1\nport=5000\n[printer]\nname=ANONYMOUS\n[lis]\ntype=tcp\n[unknown]\nkeep=preserved\n"), QStringLiteral("匿名主INI"));
    check(writeFile(root, "config_Debug.ini", "[userOption]\nReconnectTime=1000\n[isDataAuto]\nisOpen=true\nPT=0012.34000\nPT_C=0.000012300\n[TCPTest]\nisDefaultShowOpen=true\nisShowOpen=true\nShowCommand=Qj|Qk\nisMaskOpen=true\nMaskCommand=Qk\n"), QStringLiteral("匿名调试INI"));
    check(writeFile(root, "theme_config.ini", QStringLiteral("[hospital]\nname=匿名医院\n[theme]\nname=SN100\nDriverModel=SN100\nLogosize=20x20\nleftBaseColor=1.2.3.255\n").toUtf8()), QStringLiteral("匿名主题INI"));
    check(writeFile(root, "calibrationFile/ReagentAmountCalibration.ini", "[s_area]\nSD_1=[0.1]\n7mL=\"[0,0][1,100]\"\n[needle]\nrightToLeftOuter=-0.2\nrightToLeftInner=-0.3\n"), QStringLiteral("匿名旧容量INI"));
    const QJsonObject log{{"Assem", true}, {"Driver", false}, {"Fatal", true}, {"dir", "GL4A_log//"},
        {"Comp", QJsonArray{QJsonObject{{"name", "SeerainLogger"}, {"open", false}}, QJsonObject{{"name", "ForlinxLogger"}, {"open", true}}}},
        {"unknown", "preserve"}};
    check(writeFile(root, "lib/config/GL4A_config.cfg", QJsonDocument(log).toJson()) &&
          writeFile(root, "lib/config/AL4A_config.cfg", QJsonDocument(QJsonObject{{"1 PT", QJsonObject{{"v_mode", 1}, {"unknown", "keep"}}}}).toJson()),
          QStringLiteral("匿名原日志与算法cfg"));
    OriginalRuntime::RuntimeSettings settings;
    const bool runtime = check(OriginalRuntime::loadRuntimeSettings(root, &settings, &message), QStringLiteral("真实强类型入口读取四INI与两cfg"));
    if (runtime)
    {
        check(settings.theme.driverModel == "SN100" && settings.theme.logoSize == "20x20" &&
              settings.theme.colours.value("leftBaseColor") == "1,2,3,255", QStringLiteral("机型/大小写别名/原颜色实际应用值"));
        check(settings.user.touchKeyboard && settings.debug.reconnectMilliseconds == 2000, QStringLiteral("原用户开关与重连最小间隔"));
        check(settings.debug.requestedAutoData && settings.debug.requestedAutomaticProjects.value("PT") == "0012.34000" &&
              settings.debug.requestedAutomaticProjects.value("PT_C") == "0.000012300", QStringLiteral("模拟结果原串精度保留且未自动执行"));
        check(settings.unconsumedKeys.value("config.ini") == QStringList{"unknown/keep"} &&
              settings.originalIniFiles.value("config.ini").values.value("unknown/keep").toString() == "preserved", QStringLiteral("未知INI键和值保留且可审计"));
        check(settings.showCommunicationCommand("Qjdata") && settings.showCommunicationCommand("Qkdata") &&
              !settings.showCommunicationCommand("Ttdata"), QStringLiteral("原接收显示列表优先，屏蔽名单不再二次拦截"));
        check(settings.showCommunicationCommand("Ttdata", true), QStringLiteral("原发送窗口不按接收名单过滤"));
        auto filter = settings;
        filter.debug.showCommandsInitially = false;
        check(!filter.showCommunicationCommand("Qjdata") && !filter.showCommunicationCommand("Qjdata", true),
              QStringLiteral("总显示开关关闭时发送接收均隐藏"));
        filter = settings; filter.debug.showCommandFilter = false;
        check(!filter.showCommunicationCommand("Qkdata") && filter.showCommunicationCommand("Ttdata"),
              QStringLiteral("未启用显示名单时按原屏蔽名单处理接收"));
        filter.debug.maskCommandFilter = false;
        check(filter.showCommunicationCommand("Ttdata"), QStringLiteral("两名单均关闭时保留接收"));
        filter = settings; filter.debug.shownCommands = QStringLiteral("Qj|").split('|');
        check(filter.showCommunicationCommand("Ttdata"), QStringLiteral("原split保留末尾空显示项，匹配全部接收"));
        filter.debug.showCommandFilter = false; filter.debug.maskedCommands = QStringLiteral("Qk|").split('|');
        check(!filter.showCommunicationCommand("Ttdata") && filter.showCommunicationCommand("Ttdata", true),
              QStringLiteral("原空屏蔽项隐藏全部接收，发送不受名单影响"));
        QJsonObject algorithm;
        check(settings.algorithmParameters("1 PT", &algorithm, &message) && algorithm.value("unknown") == "keep" &&
              !settings.algorithmParameters("missing", &algorithm, &message), QStringLiteral("完整算法参数绑定真实项目并拒绝无配置项目"));
        double corrected = 0;
        check(settings.legacyCorrectedHeight("s_area", "SD_1", 2, 1.0, &corrected, &message) &&
              std::abs(corrected - 1.1) < 1e-9 && settings.legacyCapacityPoints.value("s_area/7mL").size() == 2,
              QStringLiteral("旧容量面型/左右针偏移实际修正高度并保留控制点"));
        check(OriginalRuntime::appendConfiguredLog(settings, "SeerainLogger", "Info", "anonymous", &message) &&
              !QFileInfo::exists(root + "/GL4A_LOG/SeerainLogger.log"), QStringLiteral("原日志关闭组件不写入"));
        check(OriginalRuntime::appendConfiguredLog(settings, "ForlinxLogger", "Info", QStringLiteral("匿名日志"), &message) &&
              readFile(root + "/GL4A_LOG/ForlinxLogger.log").contains(QStringLiteral("匿名日志").toUtf8()), QStringLiteral("Linux替代日志按原分类实际写入UTF8"));
        const auto logBefore = readFile(root + "/GL4A_LOG/ForlinxLogger.log");
        check(OriginalRuntime::appendConfiguredLog(settings, "ForlinxLogger", "Driver", "suppressed", &message) &&
              logBefore == readFile(root + "/GL4A_LOG/ForlinxLogger.log"), QStringLiteral("原Driver日志开关实际控制写入"));
    }
    const QJsonObject original = capacityFixture();
    check(writeFile(root, "calibrationFile/ReagentAmount_450.json", QJsonDocument(original).toJson()), QStringLiteral("匿名原容量JSON夹具"));
    OriginalRuntime::CapacityCalibration calibration;
    const bool capacity = check(OriginalRuntime::loadCapacityCalibration(root, "calibrationFile/ReagentAmount_450.json", &calibration, &message), QStringLiteral("原容量JSON真实读取"));
    if (capacity)
    {
        double volume = 0;
        check(calibration.volumeForHeight("R_1", 1, "r_area", "1", 500, 150, &volume, &message) &&
              std::abs(volume - 150.0) < 1e-6, QStringLiteral("原BASE与针偏移分段换算150微升"));
        check(calibration.volumeForHeight("R_1", 1, "r_area", "7mL", 250, 400, &volume, &message) &&
              std::abs(volume - 250.0) < 1e-6, QStringLiteral("原容量最大量夹限"));
        check(!calibration.volumeForHeight("missing", 1, "r_area", "7mL", 500, 150, &volume, &message), QStringLiteral("缺原孔位矩阵明确拒绝而非假0成功"));
        const auto points = calibration.configuredMeasurements();
        OriginalRuntime::CapacityMeasurement point;
        for (const auto &candidate : points) if (candidate.mode == "FACE" && candidate.hole == "R_1") point = candidate;
        check(!point.hole.isEmpty() && !OriginalRuntime::saveCapacityMeasurement(calibration, point, {100, 100}, false, &message), QStringLiteral("校准点未收齐3次不保存"));
        check(OriginalRuntime::saveCapacityMeasurement(calibration, point, {110, 110, 110}, true, &message), QStringLiteral("原3次平均、FACE结束BASE与针补偿更新保存"));
        check(!OriginalRuntime::saveCapacityMeasurement(calibration, point, {120, 120, 120}, true, &message), QStringLiteral("容量旧指纹拒绝覆盖新记录"));
        check(OriginalRuntime::loadCapacityCalibration(root, "calibrationFile/ReagentAmount_450.json", &calibration, &message) &&
              calibration.original.value("vendor_unknown") == original.value("vendor_unknown"), QStringLiteral("容量写回保留全部未知JSON节点"));
        const auto region = calibration.original.value("Area_R1").toObject().value("SUB_R1").toObject();
        check(std::abs(region.value("FACE").toObject().value("7mL").toObject().value("R_1").toObject().value("100").toDouble() - 1.1) < 1e-9 &&
              std::abs(region.value("BASE").toObject().value("7mL").toObject().value("R_1").toObject().value("200").toDouble() - 2.1) < 1e-9,
              QStringLiteral("校准读回平均高度与原BASE公式正确"));
    }
    if (parser.isSet("original-root"))
    {
        OriginalRuntime::SystemProfile actualProfile;
        OriginalRuntime::RuntimeSettings actualSettings;
        OriginalRuntime::CapacityCalibration actualCapacity;
        const QString actualRoot = parser.value("original-root");
        check(OriginalRuntime::loadSystemProfile(actualRoot, &actualProfile, &message), QStringLiteral("真实原系统库及中文CSV只读加载"));
        const bool actualRuntime = OriginalRuntime::loadRuntimeSettings(actualRoot, &actualSettings, &message);
        if (!actualRuntime) qWarning().noquote() << "RUNTIME_COMPATIBILITY_DIAGNOSTIC" << message;
        check(actualRuntime, QStringLiteral("真实四INI两cfg强类型加载不启用旧连接"));
        check(OriginalRuntime::loadCapacityCalibration(actualRoot, "calibrationFile/ReagentAmount_450.json", &actualCapacity, &message) &&
              !actualCapacity.configuredMeasurements().isEmpty(), QStringLiteral("真实原容量JSON配置点完整读取"));
    }
    const QJsonObject report{{"passed", passed}, {"failed", failed}, {"checks", checks},
        {"instrumentActions", 0}, {"rawOriginalValuesPrinted", false}, {"originalWrites", 0},
        {"scope", QStringLiteral("匿名夹具实际读写及可选原资料只读；不连接仪器、不执行完整检测算法。")}};
    QFile reportFile(QDir(output).filePath("runtime_result.json"));
    if (!reportFile.open(QIODevice::WriteOnly) || reportFile.write(QJsonDocument(report).toJson()) <= 0) return 3;
    qInfo() << "ORIGINAL_RUNTIME_RESULT" << passed << failed;
    return failed ? 1 : 0;
}
