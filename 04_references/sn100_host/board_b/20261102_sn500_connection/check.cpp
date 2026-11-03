// 只在RK检查本轮用户配置、原42位面板和当前空闲主控的命令保护，不执行检测。
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QDebug>
#include "original_files/theme_configuration.h"
#include "reagents/wgtreagentpanel.h"
#include "reagents/cusreagenthole.h"
#include "instrument/device_control.h"
#include "reagents/original_reagent_repository.h"
#include "samples/original_sample_repository.h"

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    QFile serial("/proc/device-tree/serial-number");
    if (!serial.open(QIODevice::ReadOnly) || serial.readAll().replace('\0',"")!="8314b1e8ffd0321f" || argc!=3) return 2;
    const QString themeFile=QString::fromLocal8Bit(argv[1]), output=QString::fromLocal8Bit(argv[2]);
    if (!QDir::isAbsolutePath(output) || !QDir().mkpath(output)) return 2;
    int passed=0,failed=0;
    const auto check=[&](bool ok,const QString &name)
    { if (ok) ++passed; else ++failed; qInfo().noquote()<<(ok ? "PASS" : "FAIL")<<name; };
    OriginalRuntime::ThemeOptions theme; QString message;
    const bool loaded=loadThemeConfiguration(themeFile,&theme,&message);
    check(loaded,"读取实际用户theme_config.ini");
    if (!loaded) { qCritical().noquote()<<message; return 1; }
    check(theme.driverModel=="SN500" && theme.reagentPositions==42 && theme.sampleHoles==5 && theme.emergencyHoles==5 &&
          theme.testChannels==24 && theme.reportTableType==1,"机型和42/5/5/24/报告1与用户文件一致");
    check(theme.company==QStringLiteral("上海水凝科技有限公司") && theme.hospitalName==QStringLiteral("上海医院") &&
          theme.logoSize=="82x82","公司、医院和标志尺寸保持用户原值");
    check(themeColour(theme,"leftBaseColor",Qt::red)==QColor(28,28,28,255) &&
          themeColour(theme,"leftBtnColor",Qt::black)==QColor(232,51,35,200) &&
          themeColour(theme,"kReagentHoleSelectedColor",Qt::black)==QColor(42,118,242,255),"点号分隔颜色与透明度读取一致");
    OriginalReagentRepository repository;
    OriginalReagentState originalReagents;
    bool originalLoaded=repository.initialize(QFileInfo(themeFile).absolutePath(),&message)
        && repository.load(&originalReagents,&message);
    check(originalLoaded,"按42位配置读取实际原试剂库，允许原库保留负的历史剩余量");
    if (!originalLoaded) qCritical().noquote()<<message;
    bool preserved=false;
    for (const auto &row : originalReagents.holes)
        if (row.value("hole_name").toString()=="SD_1") preserved=row.value("used_ul").toString()=="-12560";
    check(preserved,"原SD_1剩余量-12560按原文读入，未重写数据库");
    OriginalSampleRepository sampleRepository;
    const bool samplesLoaded=sampleRepository.initialize(QFileInfo(themeFile).absolutePath(),&message);
    check(samplesLoaded && sampleRepository.ordinaryHoles()==5 && sampleRepository.emergencyHoles()==5 &&
          !sampleRepository.snapshot().racks.isEmpty() && sampleRepository.snapshot().racks.first().holes.size()==5,
          "原样本资料读取器使用普通5孔和急诊5孔，急诊架实际生成5孔");
    if (!samplesLoaded) qCritical().noquote()<<message;
    wgtReagentPanel panel;
    panel.resize(1111,941); panel.configureLayout(theme.reagentPositions); panel.show();
    app.processEvents();
    int reagent=0,diluent=0; bool bounds=true;
    for (auto *hole : panel.findChildren<cusReagentHole*>())
    {
        if (!hole->isVisibleTo(&panel)) continue;
        if (hole->objectName().startsWith("SD_")) ++diluent; else ++reagent;
        const QRect area(hole->mapTo(&panel,QPoint()),hole->size());
        bounds=bounds && panel.rect().contains(area);
    }
    check(reagent==42 && diluent==4,"原42位面板显示42试剂位及4个独立稀释液位，隐藏旧20位面板");
    check(bounds,"原42位孔控件均在面板边界内");
    check(panel.grab().save(QDir(output).filePath("reagent_panel_42.png")),"保存RK实际渲染的42位面板截图");
    panel.hide();
    DeviceControl control;
    QList<QByteArray> transmitted;
    QObject::connect(&control,&DeviceControl::wireActivity,&app,[&](const QString &direction,const QByteArray &bytes)
    { qInfo().noquote()<<"CHECK_WIRE"<<direction<<bytes.toHex(' '); if (direction.startsWith("TX")) transmitted.append(bytes); });
    check(control.open("127.0.0.1",5000),"使用上位机实际通信类连接当前主控");
    QElapsedTimer wait; wait.start();
    while (!control.state().fresh && wait.elapsed()<10000) { app.processEvents(); QThread::msleep(10); }
    check(control.state().connected && control.state().fresh && control.state().status.model=="SN500","收到并解析主控的SN500状态");
    check(control.state().status.generalFields.value(13)==DeviceControl::originalVersion() &&
          control.state().status.generalFields.value(19)=="LinuxNew","实际匹配号20250905016及LinuxNew分支一致");
    check(control.state().status.generalFields.value(21)=="24" && control.state().status.generalFields.value(22)=="5" &&
          control.state().status.generalFields.value(23)=="5","主控实际返回24检测通道和急诊/普通各5孔");
    if (control.state().status.generalFields.value(20)!=QString::number(theme.reagentPositions))
        qWarning().noquote()<<"PENDING_INTERFACE reagent_positions configured="<<theme.reagentPositions
            <<"controller="<<control.state().status.generalFields.value(20)<<"按用户附件保留配置，主控字段定义待核";
    check(control.state().status.mainStatus==0,"当前主控仍为空闲状态，未启动检测");
    check(!control.start() && !control.pause(),"空闲状态拒绝开始和暂停，不绕过原状态条件");
    bool onlyQueries=true;
    for (const QByteArray &frame : transmitted)
        onlyQueries=onlyQueries && !frame.contains("Ts") && !frame.contains("Tp[0]");
    check(onlyQueries,"实际发送记录不含开始Ts或暂停Tp[0]");
    const QJsonObject result{{"passed",passed},{"failed",failed},{"model",theme.driverModel},
        {"reagent_positions",reagent},{"diluent_positions",diluent},{"sample_holes",theme.sampleHoles},
        {"emergency_holes",theme.emergencyHoles},{"channels",theme.testChannels},{"physical_actions",false},
        {"controller_reagent_positions",control.state().status.generalFields.value(20)},
        {"reagent_positions_consistent",control.state().status.generalFields.value(20)==QString::number(theme.reagentPositions)}};
    control.close();
    QFile json(QDir(output).filePath("result.json"));
    if (!json.open(QIODevice::WriteOnly) || json.write(QJsonDocument(result).toJson())<0) return 2;
    qInfo().noquote()<<"RESULT"<<passed<<"passed"<<failed<<"failed";
    return failed ? 1 : 0;
}
