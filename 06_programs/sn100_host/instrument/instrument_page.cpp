#include "instrument_page.h"
#include "device_control.h"
#include "original_device_commands.h"
#include "ui_frmdriverset.h"
#include "ui/cusmessagebox.h"
#include "samples/material/qtmaterialscrollbar.h"

#include <QAbstractButton>
#include <QAtomicInteger>
#include <QComboBox>
#include <QCoreApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScroller>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextDocument>

namespace
{
const QString deviceUnavailable = QStringLiteral("仪器控制命令尚未接入；当前未建立真实连接、未发送命令。");
const QString buttonStyle = QStringLiteral(
    ".QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
    ".QPushButton:hover{background-color:rgba(42,118,242,170);}"
    ".QPushButton:pressed{background-color:rgba(135,206,250,255);}");

QString stateName(const QStringList &names, int value)
{
    return value >= 0 && value < names.size() ? names.at(value) : QStringLiteral("未知");
}

quint64 nextConnectionRequest()
{
    static QAtomicInteger<quint64> sequence(0);
    return sequence.fetchAndAddRelaxed(1) + 1;
}
}

InstrumentPage::InstrumentPage(MaintenanceStore *store, QWidget *parent) :
    QWidget(parent), ui(new Ui::frmDriverSet), store_(store)
{
    ui->setupUi(this);
    setFixedSize(1751,968);
    applyOriginalStyle();
    disableDeviceActions();
    clearDeviceValues();
    refreshSensors();

    // 子页编号沿用原 PageIndex；此处没有原主窗口的自动连接或首次收包联动。
    const QList<QPair<QtMaterialFlatButton *,int>> navigation = {
        {ui->btnPort,0},{ui->btnStatus,1},{ui->btnMaintain,2},{ui->btnVersion,3},
        {ui->btnFunTest,6},{ui->btn_CommunicationTest,7}
    };
    for (const auto &item : navigation)
    {
        connect(item.first,&QPushButton::clicked,this,[this,item]()
        {
            if (isBusy()) return;
            ui->stackedWidget->setCurrentIndex(item.second);
            if (item.second == 2 && ready_) loadSelectedMonth();
            if (item.second == 0 && !connectionFile_.isEmpty() && prepareToLeave())
                loadConnectionConfiguration();
        });
    }
    connect(ui->btn_board_status,&QPushButton::clicked,this,[this]()
    {
        if (busy_) return;
        // 未知机型仅查看原第一块参考布局，不据布局推断真实 SN100 配置。
        ui->stackedWidget->setCurrentIndex(replayModel_ == QStringLiteral("SN600") ? 5 : 4);
    });
    ui->btn_board_status->setToolTip(tr("原板卡参考布局；真实机型、传感器映射及状态待设备核实。"));

    initializeMaintenanceTable(ui->tableDayMaintain,
        {QStringLiteral("btnCleanCup"),QStringLiteral("btnWasteWater"),QStringLiteral("btnWasteCup"),
         QStringLiteral("btnLongClean"),QString()},
        {QStringLiteral("waste_cup"),QStringLiteral("waste_water"),QStringLiteral("waste_cup_box"),
         QStringLiteral("long_clean"),QStringLiteral("shut_down")},
        {tr("清空废杯"),tr("清空废液"),tr("清洗废杯盒"),tr("清洗加样针"),tr("关机")});
    initializeMaintenanceTable(ui->tableWeekMaintain,
        {QStringLiteral("btnCleanPool"),QStringLiteral("btnCleanDriver")},
        {QStringLiteral("clean_pool"),QStringLiteral("clean_dirver")},
        {tr("清洁洗针孔"),tr("清洁仪器")});
    initializeMaintenanceTable(ui->tableMonthMaintain,{QStringLiteral("btnCleanNeedle")},
        {QStringLiteral("clean_needle")},{tr("清洁加样针")});

    ui->dateSelect->setDate(QDate::currentDate());
    connect(ui->dateSelect,&QDateEdit::dateChanged,this,[this](QDate)
    { if (ready_ && !busy_) loadSelectedMonth(); });
    connect(ui->btnPrvMonth,&QPushButton::clicked,this,[this]()
    { if (!busy_) ui->dateSelect->setDate(ui->dateSelect->date().addMonths(-1)); });
    connect(ui->btnNextMonth,&QPushButton::clicked,this,[this]()
    { if (!busy_) ui->dateSelect->setDate(ui->dateSelect->date().addMonths(1)); });
    connect(ui->btnClearText,&QPushButton::clicked,this,[this]()
    { ui->tbr_scanText->clear();ui->tbr_scanText_err->clear(); });
    connect(ui->pushButton_Connect_ClearData,&QPushButton::clicked,this,[this]()
    {
        ui->textBrowser_SendData->clear();ui->textBrowser_ReceiveData->clear();
        ui->label_SendCount->setText(QStringLiteral("0"));
        ui->label_ReceiveCount->setText(QStringLiteral("0"));
    });
    for (QLineEdit *edit : {ui->lblTcpIp, ui->lblTcpPort})
        connect(edit, &QLineEdit::textChanged, this, [this]()
        {
            connectionDirty_ = ui->lblTcpIp->text() != committedAddress_ ||
                               ui->lblTcpPort->text() != committedPort_;
            updatePermissions();
        });
    connect(ui->btnSaveTcpSet, &QPushButton::clicked, this, &InstrumentPage::saveConnectionConfiguration);

    if (store_)
    {
        connect(store_, &MaintenanceStore::connectionAlarmPolicyReady, this,
            [this](quint64 requestId, const QStringList &codes, bool available)
        {
            if (requestId != connectionRequest_) return;
            blockingAlarmCodes_ = codes;
            alarmPolicyAvailable_ = available;
            if (deviceControl_) deviceControl_->setAlarmPolicy(codes, available);
        });
        connect(store_, &MaintenanceStore::connectionSettingsReady, this,
            [this](quint64 requestId, const QString &address, const QString &port,
                   const QString &fingerprint, bool saved)
        {
            if (requestId != connectionRequest_) return;
            connectionRequest_ = 0;
            committedAddress_ = address;
            committedPort_ = port;
            connectionFingerprint_ = fingerprint;
            restoreConnectionDraft();
            setConnectionBusy(false);
            report(saved ? tr("主控连接配置已保存并回读。下次连接使用保存值。")
                         : tr("已从原config.ini读取主控连接配置。"));
            emit operationFinished(saved ? QStringLiteral("saveConnectionSettings")
                                         : QStringLiteral("loadConnectionSettings"), true);
            if (deviceControl_ && autoConnectDevice_ && !deviceControl_->state().connected)
            {
                autoConnectDevice_ = false;
                deviceControl_->open(committedAddress_, committedPort_.toUShort());
            }
        });
        connect(store_, &MaintenanceStore::connectionSettingsError, this,
            [this](quint64 requestId, const QString &message, bool requiresReload)
        {
            if (requestId != connectionRequest_) return;
            connectionRequest_ = 0;
            if (requiresReload) connectionFingerprint_.clear();
            setConnectionBusy(false);
            report(message);
            emit operationFinished(QStringLiteral("connectionSettings"), false);
        });
        connect(store_,&MaintenanceStore::ready,this,[this]()
        { ready_=true;loadSelectedMonth(); });
        connect(store_,&MaintenanceStore::loaded,this,[this](const MaintenanceRecords &records)
        {
            showMaintenanceRecords(records);
            setProperty("maintenanceLoaded",true);
            for (QTableWidget *table : {ui->tableDayMaintain,ui->tableWeekMaintain,ui->tableMonthMaintain})
                table->setToolTip(tr("已读取所选月份的本地维护记录；空格表示未登记。"));
            setBusy(false);
            report(tr("已读取%1维护记录（离线本地）。").arg(requestedMonth_.toString(QStringLiteral("yyyy-MM"))));
            emit operationFinished(QStringLiteral("load"),true);
        });
        connect(store_,&MaintenanceStore::succeeded,this,[this](const QString &action)
        {
            emit operationFinished(action,true);
            // 只有事务提交成功后才重新读回勾选，不能先乐观显示已完成。
            setBusy(false);
            loadSelectedMonth();
        });
        connect(store_,&MaintenanceStore::error,this,[this](const QString &operation,const QString &message)
        {
            setProperty("maintenanceLoaded",false);
            for (QTableWidget *table : {ui->tableDayMaintain,ui->tableWeekMaintain,ui->tableMonthMaintain})
                table->setToolTip(tr("读取或登记失败：%1；空白格不能作为完成情况依据，请重试。").arg(message));
            setBusy(false);
            report(tr("%1失败：%2").arg(operation,message));
            emit operationFinished(operation,false);
        });
        connect(store_,&MaintenanceStore::closed,this,[this]()
        { ready_=false;showMaintenanceRecords({});setProperty("maintenanceLoaded",false);setBusy(false); });
    }
    ui->stackedWidget->setCurrentIndex(1);
    updatePermissions();
}

InstrumentPage::~InstrumentPage()
{
    delete ui;
}

void InstrumentPage::applyOriginalStyle()
{
    // 原frmDriverSet动态样式 + 现统一壳已使用的原主题值。原Windows活动主题另验。
    setStyleSheet(buttonStyle);
    ui->driversetFunMenu->setBackgroundColor(QColor(28,28,28));
    QLinearGradient gradient(0,0,0,ui->driversetFunMenu->height());
    gradient.setColorAt(0,QColor(255,255,255,0));
    gradient.setColorAt(1,QColor(255,255,255,25));
    ui->driversetFunMenu->setGraBackgroundColor(gradient);
    ui->driversetFunMenu->setShadowEffect(Qt::AlignLeft);
    for (QtMaterialFlatButton *button :
         {ui->btnPort,ui->btnHil,ui->btnStatus,ui->btnVersion,ui->btnMaintain,
          ui->btn_board_status,ui->btn_CommunicationTest,ui->btnFunTest})
    {
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setBackgroundColor(QColor(127,127,127));
        button->setForegroundColor(Qt::white);
        button->setUseThemeColors(false);
    }
    ui->wgtDriverSetBg->setRadius(10);
    ui->wgtDriverSetBg->setBackgroundColor(Qt::white);
    const QString accent = QStringLiteral("border:none;background-color:transparent;border-left:10px solid rgba(232,51,35,255);");
    for (const QString &name : {QStringLiteral("lineEdit_27"),QStringLiteral("lineEdit_13"),
         QStringLiteral("lineEdit_16"),QStringLiteral("lineEdit_14"),QStringLiteral("lineEdit_26"),
         QStringLiteral("lineEdit_31"),QStringLiteral("lineEdit_33"),QStringLiteral("lineEdit_35")})
        if (auto *edit=findChild<QLineEdit *>(name)) edit->setStyleSheet(accent);

    for (QTableWidget *table : {ui->tableChannel,ui->tlbSensor})
    {
        auto *scrollbar=new QtMaterialScrollBar;
        scrollbar->setSliderColor(QColor(34,34,34,50));
        scrollbar->setBackgroundColor(Qt::transparent);
        scrollbar->setCanvasColor(Qt::transparent);
        scrollbar->setHideOnMouseOut(true);
        table->setVerticalScrollBar(scrollbar);
        QScroller::scroller(table)->grabGesture(table,QScroller::LeftMouseButtonGesture);
        table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setFocusPolicy(Qt::NoFocus);
    }
    ui->tableChannel->setColumnCount(6);
    ui->tableChannel->setHorizontalHeaderLabels({tr("通道号"),tr("开启状态"),QStringLiteral("405nm"),
        QStringLiteral("570nm"),QStringLiteral("660nm"),QStringLiteral("800nm")});
    ui->tableChannel->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    ui->tableChannel->setRowCount(0);
    ui->tableChannel->setToolTip(tr("离线：通道数量、开关与AD值未知；待实际机型配置和仪器验证。"));
    ui->tlbSensor->setColumnWidth(0,180);
    ui->cmb_dectMode->hide();
    ui->lbl10_25->hide();ui->lblWPlateTemp->hide();ui->label_13->hide();
    ui->btnHil->hide();
}

void InstrumentPage::disableDeviceActions()
{
    const QSet<QString> localButtons = {
        QStringLiteral("btnPort"),QStringLiteral("btnStatus"),QStringLiteral("btnMaintain"),
        QStringLiteral("btnVersion"),QStringLiteral("btn_board_status"),QStringLiteral("btnFunTest"),
        QStringLiteral("btn_CommunicationTest"),QStringLiteral("btnPrvMonth"),QStringLiteral("btnNextMonth"),
        QStringLiteral("btnClearText"),QStringLiteral("pushButton_Connect_ClearData")
    };
    for (QAbstractButton *button : findChildren<QAbstractButton *>())
    {
        if (!localButtons.contains(button->objectName()))
        {
            button->setEnabled(false);
            button->setToolTip(deviceUnavailable);
            button->setProperty("requiresInstrument",true);
        }
    }
    for (QLineEdit *edit : findChildren<QLineEdit *>()) edit->setReadOnly(true);
    for (QComboBox *combo : findChildren<QComboBox *>())
    {
        combo->setEnabled(false);
        combo->setToolTip(deviceUnavailable);
    }
    ui->lblTcpIp->clear();ui->lblTcpPort->clear();
    ui->lblTcpIp->setPlaceholderText(tr("待提供"));
    ui->lblTcpPort->setPlaceholderText(tr("待提供"));
    ui->lblTcpIp->setToolTip(tr("不读取原生产连接配置；真实连接参数待用户提供。"));
    ui->lblTcpPort->setToolTip(ui->lblTcpIp->toolTip());
    for (QTextBrowser *browser : findChildren<QTextBrowser *>()) browser->clear();
}

void InstrumentPage::clearDeviceValues()
{
    // 明确设备输出控件清单来自原reloadControl/BoradStatus_flush/slotStatusChanged。
    // 不按“未连接”等字符串全局替换，静态说明与单位仍由原.ui保存。
    const QStringList outputNames = QString::fromLatin1(
        "algorithm_version algorithm_version1 label_2ReagentWork label_ConnectStatus label_isNew label_isNew_2 lbl6LCatcher_blenderState lbl6LCatcher_blenderV "
        "lbl6LCatcher_cupDetect lbl6LCatcher_Error lbl6LCatcher_heatPatchTemp lbl6LCatcher_steadyState lbl6LCatcher_testPlateTemp lbl6LCatcher_VGe lbl6LCatcher_XGe lbl6LCatcher_YGe "
        "lbl6Main_emergencyCover_2 lbl6Main_emergencyCover_5 lbl6Main_emergencyCover_7 lbl6Main_machineLight_3 lbl6Main_mainPeriod lbl6Main_plateButton lbl6Main_plateButton_4 lbl6Main_plateButton_5 "
        "lbl6Main_plateButton_6 lbl6Main_plateButton_7 lbl6Main_plateButton_9 lbl6Main_reagentScaner lbl6Main_reagentScaner_3 lbl6Main_reagentScaner_5 lbl6Main_reagentScaner_6 lbl6Main_reagentScaner_7 "
        "lbl6Main_reagentScaner_8 lbl6Main_sampleScaner lbl6Main_sampleScaner_3 lbl6Main_startButton lbl6Main_startButton_3 lbl6Main_startButton_5 lbl6Main_subSystemCount lbl6Main_subSystemCount_3 "
        "lbl6Main2ReagentWork lbl6Plate_button lbl6Plate_door lbl6Plate_DownBall lbl6Plate_Error lbl6Plate_Ge lbl6Plate_shutdown lbl6Plate_Temp "
        "lbl6Plate_UpBall lbl6PlateCool_Belt lbl6PlateCool_beng lbl6PlateCool_flow lbl6PlateCool_flowGe lbl6PlateCool_work lbl6PlateDoor lbl6Rack_Error "
        "lbl6Rack_LeftBeam lbl6Rack_ready lbl6Rack_reservedSignal lbl6Rack_rightBeam lbl6Rack_sampleDetect lbl6RackAdd_Ge lbl6RackIn_Ge lbl6RackIn_rackArea "
        "lbl6RackOut_Ge lbl6RackOut_rackArea lbl6RackOut_ready lbl6Rail_Error lbl6Rail_Ge lbl6Rail_plateGe lbl6Sam_liquidSignal lbl6Sam_minusValveState "
        "lbl6Sam_pinSignal lbl6Sam_pumpPressure lbl6Sam_signal lbl6Sam_supplyValveControl lbl6Sam_waterPumpState lbl6SamP_Ge lbl6SCatcher_blenderState lbl6SCatcher_blenderV "
        "lbl6SCatcher_cupDetect lbl6SCatcher_Error lbl6SCatcher_HGe lbl6SCatcher_VGe lbl6SCatcher_wasteBox lbl6SCatcher_wastecup lbl6Select_bottleFallBeam lbl6Select_EnvirTemp "
        "lbl6Select_Error lbl6Select_hopperState lbl6Select_leftBeam lbl6SelectH_flagBit lbl6SelectH_Ge lbl6SelectS_flagBit lbl6SelectS_Ge lbl6SelectV_Ge "
        "lbl6SNeedle_Error lbl6SNeedle_pressureRelifValve lbl6SNeedle_sensor lbl6SNeedle_solenoidValve lbl6SNeedle_threeWayValve lbl6SNeedleH_Ge lbl6SNeedleV_Ge lbl6WarmPlate_Ge "
        "lblCleaner_Beng lblCleaner_low lblCleaner_up lblDilp_door lblDilp_Ge lblDilp_Sata_Error lblDriverModel lblDriverNo "
        "lblDuilentOpen lblEmergencyOpen lblEnvirTemp lbll6WarmPlate_steadyState lbll6WarmPlate_temp lblLeftNeedleTemp lblLNeedle1_Ltemp lblMain_mainPeriod "
        "lblPlate_button lblPlate_danger lblPlate_door lblPlate_Error lblPlate_Ge lblPlate_shutdown lblPlate_Temp lblPlateCool_beng "
        "lblPlateCool_flow lblPlateCool_flowGe lblPlateCool_low lblPlateCool_up lblPlateCool_work lblPressure_Beng lblPressure_Beng_2 lblRack_Error "
        "lblRack_ScanerCur lblRack_ScanerR lblRack_ScanerS lblRackAdd_Arrive lblRackAdd_DS1 lblRackAdd_DS2 lblRackAdd_Ge lblRackIn_Arrive "
        "lblRackIn_Ge lblRackIn_rackEmpty lblRackIn_Te lblRackOut_Ge lblRackOut_rackFull lblReagent_3Fa lblReagent_Blender lblReagent_Clean "
        "lblReagent_danger lblReagent_Error lblReagent_Fa lblReagent_LTemp lblReagent_Needel lblReagent_RTemp lblReagent_Waste lblReagent_Zz "
        "lblReagent_ZzAlarm lblReagentOpen lblReagentP_Ge lblReagentR_Ge lblReagentYW_LBit lblReagentYW_LFre lblReagentYW_RBit lblReagentYW_RFre "
        "lblReagentYW_sensor lblReagentZ_Ge lblRightNeedleTemp lblRNeedle1_ballState lblRNeedle1_cleaningMinusValve lblRNeedle1_cleaningPumpValve lblRNeedle1_Error lblRNeedle1_HGe "
        "lblRNeedle1_liquidSignal lblRNeedle1_minusValveState lblRNeedle1_PGe lblRNeedle1_pinSignal lblRNeedle1_pumpControl lblRNeedle1_pumpPressure lblRNeedle1_sensor lblRNeedle1_solenoidValve "
        "lblRNeedle1_supplyValveState lblRNeedle1_VGe lblRNeedle2_ballState lblRNeedle2_cleaningMinusValve lblRNeedle2_cleaningPumpValve lblRNeedle2_Error lblRNeedle2_HGe lblRNeedle2_liquidSignal "
        "lblRNeedle2_minusPressureValve lblRNeedle2_minusPumpControl lblRNeedle2_minusPumpPressure lblRNeedle2_pinSignal lblRNeedle2_pumpGe lblRNeedle2_Rtemp lblRNeedle2_sensor lblRNeedle2_solenoidValve "
        "lblRNeedle2_supplyValve lblRNeedle2_VGe lblRPlateTemp lblS_EnvirTemp lblSam_Blender lblSam_danger lblSam_Fa lblSam_Zz "
        "lblSam_ZzAlarm lblSamP_Ge lblSample_Error lblSampleCup_Status lblSamR_Ge lblSamWaster_Beng lblSamWaster_s lblSamYW_base "
        "lblSamYW_bit lblSamYW_Cur lblSamYW_sensor lblSamZ_Ge lblSata_door lblSata_Ge lblScaner_Status lblSCatcher_Error "
        "lblSCatcherR_Ge lblSCatcherZ_Ge lblSCatcherZ_Te lblSelect_auto lblSelect_big lblSelect_Error lblSelect_low lblSelect_up "
        "lblSelectMoter_Base lblSelectMoter_Ge lblSelectMoter_Speed lblSelectMoter_work lblSelectN_Ge lblSelectNewCup lblSelectS_Ge lblSelectS_Te "
        "lblSelectSamll_low lblSelectSamll_up lblTcpIp lblTcpPort lblTestCatcher_Error lblTestCatcherR_Ge lblTestCatcherS_Ge lblTestCatcherS_Te "
        "lblTestCatcherZ_Ge lblTestCatcherZ_Te lblTestHeat_ready lblTestHeat_temp lblTestHeat_temp2 lblTestHeat_work lblTestWaste_Beng lblTestWaste_s "
        "lblTPlateTemp lblVersionFb lblVersionWz lblVersionZk lblWasterCup lblWasterCup_s lblWPlateTemp ver_letd ").split(QLatin1Char(' '),Qt::SkipEmptyParts);
    for (const QString &name : outputNames)
    {
        if (QLabel *label=findChild<QLabel *>(name))
        {
            label->setText(tr("未知"));
            label->setToolTip(tr("脱机：无真实仪器数据。"));
            label->setProperty("instrumentDeviceValue",true);
        }
    }
    for (int number=9;number<=16;++number)
    {
        auto *label=findChild<QLabel *>(QStringLiteral("lblName_%1").arg(number));
        if (label) { label->setText(tr("未知"));label->setProperty("instrumentDeviceValue",true); }
    }
    ui->lblDriverModel->setText(tr("未知"));
    ui->label_isNew_3->setText(tr("未知"));
    ui->label_isNew_4->setText(tr("未知"));
    ui->lbl6Main_machineLight->setText(tr("未知"));
    ui->lblVersionFb->setText(tr("离线移植版"));
    ui->lblVersionWz->setText(QCoreApplication::applicationVersion().isEmpty()
        ? QStringLiteral("SN100 / Qt %1").arg(QString::fromLatin1(qVersion()))
        : QCoreApplication::applicationVersion());
    ui->lblVersionZk->setText(tr("未知"));
    ui->lblDriverNo->setText(tr("未知"));
    ui->lstBoardVersion->clear();
    ui->algorithm_version->setText(tr("未接入"));
    ui->algorithm_version1->setText(tr("未接入"));
    ui->lineEdit->clear();
    ui->label_SendCount->setText(QStringLiteral("0"));
    ui->label_ReceiveCount->setText(QStringLiteral("0"));
}

void InstrumentPage::refreshSensors()
{
    QStringList names = {tr("清洗液桶:"),tr("废液桶:"),tr("反应杯舱:"),tr("废杯状态:"),
        tr("丢杯桶:"),tr("制冷液:"),tr("清洗液瓶:"),tr("样本容器检测:")};
    if (session_.authenticated && session_.role >= 4)
        names << tr("进架区域:") << tr("出架区域:") << tr("进架就位:")
              << tr("加样架对射右:") << tr("加样架对射左:") << tr("退架就位:")
              << tr("样本针废液浮球:") << tr("试剂针废液浮球:") << tr("供杯导轨上光耦:")
              << tr("导轨杯状态:") << tr("选杯料斗上光耦:") << tr("选杯料斗下光耦:")
              << tr("新杯位状态:") << tr("开始按钮状态:") << tr("试剂盘按钮状态:");
    ui->tlbSensor->clearContents();
    ui->tlbSensor->setRowCount(names.size());
    for (int row=0;row<names.size();++row)
    {
        auto *name=new QTableWidgetItem(names.at(row));
        name->setFont(QFont(QStringLiteral("Noto Sans CJK SC"),10));
        name->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
        ui->tlbSensor->setItem(row,0,name);
        auto *value=new QLabel(tr("未知"));
        value->setFont(name->font());value->setAlignment(Qt::AlignCenter);
        value->setToolTip(tr("机型/传感器映射待实物核实；当前没有真实仪器数据。"));
        ui->tlbSensor->setCellWidget(row,1,value);
        ui->tlbSensor->setRowHeight(row,31);
    }
}

void InstrumentPage::initializeMaintenanceTable(QTableWidget *table, const QStringList &names,
    const QStringList &actions, const QStringList &texts)
{
    table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    for (int column=1;column<table->columnCount();++column)
    {
        table->setColumnWidth(column,41);
        auto *day=new QTableWidgetItem(QString::number(column));
        day->setFont(QFont(QStringLiteral("Noto Sans CJK SC"),12));
        day->setTextAlignment(Qt::AlignCenter);table->setItem(0,column,day);
    }
    for (int index=0;index<actions.size();++index)
    {
        auto *container=new QWidget(table);
        auto *layout=new QHBoxLayout(container);
        layout->setSpacing(0);layout->setContentsMargins(0,0,0,0);
        QWidget *control=nullptr;
        if (names.at(index).isEmpty())
        {
            auto *label=new QLabel(texts.at(index),container);
            label->setAlignment(Qt::AlignCenter);
            label->setToolTip(tr("关机完成记录须由设备完成事件产生；离线不登记。"));
            control=label;
        }
        else
        {
            auto *button=new QPushButton(texts.at(index),container);
            button->setObjectName(names.at(index));button->setStyleSheet(buttonStyle);
            const QString action=actions.at(index),text=texts.at(index);
            if (action == QStringLiteral("long_clean"))
            {
                button->setEnabled(false);button->setToolTip(deviceUnavailable);
                button->setProperty("requiresInstrument",true);
            }
            else
            {
                manualButtons_.append(button);
                button->setProperty("maintenanceAction",action);
                connect(button,&QPushButton::clicked,this,[this,action,text]()
                { recordManualMaintenance(action,text); });
            }
            control=button;
        }
        control->setFont(QFont(QStringLiteral("Noto Sans CJK SC"),12));
        control->setFixedSize(110,50);layout->addWidget(control);
        table->setCellWidget(index+1,0,container);
    }
}

void InstrumentPage::setSession(AccountSession session)
{
    session_=session;
    if (store_) QMetaObject::invokeMethod(store_,"setSession",Qt::QueuedConnection,Q_ARG(AccountSession,session));
    updatePermissions();
    refreshSensors();
    ui->stackedWidget->setCurrentIndex(1);
}

void InstrumentPage::updatePermissions()
{
    const bool authenticated=session_.authenticated && session_.role>=1 && session_.role<=5;
    const bool administrator=authenticated && session_.role>=5;
    ui->btnPort->setVisible(administrator);
    ui->btnFunTest->setVisible(administrator);
    ui->btn_board_status->setVisible(authenticated && session_.role>=4);
    ui->btn_CommunicationTest->setVisible(administrator);
    ui->btnHil->hide();
    ui->lblDriverNo->setVisible(administrator);
    ui->lbl10_24->setVisible(administrator);
    ui->lstBoardVersion->hide();
    for (QPushButton *button : manualButtons_)
    {
        button->setEnabled(authenticated && ready_ && !isBusy());
        button->setToolTip(authenticated
            ? tr("确认人工已完成后，登记今天的本地维护记录；不会驱动仪器。")
            : tr("请先选择有效账户；Guest只能查看维护记录。"));
    }
    ui->dateSelect->setEnabled(ready_ && !isBusy());
    ui->btnPrvMonth->setEnabled(ready_ && !isBusy());
    ui->btnNextMonth->setEnabled(ready_ && !isBusy());
    const bool editConnection = administrator && !isBusy() && !connectionFingerprint_.isEmpty();
    ui->lblTcpIp->setReadOnly(!editConnection);
    ui->lblTcpPort->setReadOnly(!editConnection);
    ui->btnSaveTcpSet->setEnabled(editConnection && connectionDirty_);
    ui->btnSaveTcpSet->setProperty("requiresInstrument", false);
    ui->btnSaveTcpSet->setToolTip(connectionFile_.isEmpty()
        ? tr("尚未指定原config.ini工作副本。")
        : tr("管理员修改后写回同一config.ini；保存完成前不重复提交，不自动连接仪器。"));
    updateDeviceButtons();
}

void InstrumentPage::attachDeviceControl(DeviceControl *control, bool connectAtStartup)
{
    if (!control || deviceControl_) return;
    deviceControl_ = control;
    control->setAlarmPolicy(blockingAlarmCodes_, alarmPolicyAvailable_);
    autoConnectDevice_ = connectAtStartup;
    connect(control, &DeviceControl::changed, this, [this](const DeviceControlState &state)
    {
        setReadOnlyStatus(state.status, state.connected && state.fresh, tr("设备"), state.message);
        updateDeviceButtons();
        report(state.message);
    });
    connect(control, &DeviceControl::wireActivity, this,
        [this](const QString &direction, const QByteArray &bytes)
    {
        // 使用原通信收发区域，限制界面保留量，避免长时间接收无限增长。
        QTextBrowser *view = direction.startsWith(QStringLiteral("TX"))
                                 ? ui->textBrowser_SendData : ui->textBrowser_ReceiveData;
        if (view->document()->blockCount() > 500) view->clear();
        view->append(direction + QStringLiteral(" hex=") + QString::fromLatin1(bytes.toHex(' ')));
    });
    const auto toggleConnection = [this]()
    {
        if (!deviceControl_ || isBusy() || connectionDirty_) return;
        if (deviceControl_->state().connected) deviceControl_->close();
        else deviceControl_->open(committedAddress_, committedPort_.toUShort());
    };
    connect(ui->btnTcpConnect, &QPushButton::clicked, this, toggleConnection);
    connect(ui->pushButton_Connect, &QPushButton::clicked, this, toggleConnection);
    for (const auto &command : originalDeviceButtons())
        if (auto *button = findChild<QPushButton *>(command.name))
            connect(button, &QPushButton::clicked, this, [this, command]()
            {
                if (!deviceControl_ || !session_.authenticated || session_.role != 5) return;
                if (command.payload == "Qj" && deviceControl_->state().outcomeUnknown && deviceControl_->state().fresh)
                {
                    CusMessageBox question(this);
                    question.showQuestionBox(tr("上一条控制命令结果未知。已根据当前状态和实际设备核对完成，解除等待？旧命令不会重发。"));
                    if (question.getClickButton() == QMessageBox::Yes) deviceControl_->confirmObservedState();
                }
                deviceControl_->maintenanceCommand(command.payload);
            });
    connect(ui->pushButton_updateTime, &QPushButton::clicked, this, [this]()
    {
        if (deviceControl_ && session_.authenticated && session_.role == 5) deviceControl_->updateDeviceTime();
    });
    if (autoConnectDevice_ && !committedAddress_.isEmpty() && !isBusy())
    {
        autoConnectDevice_ = false;
        control->open(committedAddress_, committedPort_.toUShort());
    }
    updateDeviceButtons();
}

void InstrumentPage::updateDeviceButtons()
{
    const bool administrator = session_.authenticated && session_.role == 5;
    const bool connected = deviceControl_ && deviceControl_->state().connected;
    const bool available = deviceControl_ && administrator && !isBusy();
    for (QPushButton *button : {ui->btnTcpConnect, ui->pushButton_Connect})
    {
        button->setEnabled(available && !connectionDirty_ && !committedAddress_.isEmpty());
        button->setText(connected ? tr("断开") : tr("连接"));
    }
    for (const auto &command : originalDeviceButtons())
        if (auto *button = findChild<QPushButton *>(command.name))
        {
            const bool controlPending = connected && (!deviceControl_->state().pending.isEmpty() ||
                                                      deviceControl_->state().outcomeUnknown);
            button->setEnabled(available && connected &&
                (!controlPending || command.payload.startsWith('Q') || command.payload.startsWith('U')));
            button->setToolTip(tr("沿用原Windows仪器通信测试页的命令；实际返回见收发区域。"));
        }
    ui->pushButton_updateTime->setEnabled(available && connected);
}

void InstrumentPage::setBusy(bool busy)
{
    const bool previous = isBusy();
    busy_ = busy;
    if (previous != isBusy()) emit busyChanged(isBusy());
    updatePermissions();
}

bool InstrumentPage::prepareToLeave()
{
    if (isBusy())
    {
        report(tr("仪器页面的数据操作正在进行，请等待结果。"));
        return false;
    }
    if (!connectionDirty_) return true;
    QMessageBox question(QMessageBox::Question, tr("确认放弃连接配置草稿"),
        tr("主控地址或端口尚未保存，是否放弃本次修改？"), QMessageBox::Yes | QMessageBox::No, this);
    question.setObjectName(QStringLiteral("discardConnectionConfigurationDialog"));
    question.setDefaultButton(QMessageBox::No);
    question.setButtonText(QMessageBox::Yes, tr("放弃修改"));
    question.setButtonText(QMessageBox::No, tr("继续编辑"));
    if (question.exec() != QMessageBox::Yes) return false;
    restoreConnectionDraft();
    return true;
}

bool InstrumentPage::setConnectionConfigurationFile(const QString &absoluteFile)
{
    if (isBusy() || connectionDirty_) return false;
    connectionFile_ = absoluteFile;
    connectionFingerprint_.clear();
    setProperty("connectionConfigurationFile", absoluteFile);
    loadConnectionConfiguration();
    return true;
}

void InstrumentPage::restoreConnectionDraft()
{
    const QSignalBlocker addressBlock(ui->lblTcpIp), portBlock(ui->lblTcpPort);
    ui->lblTcpIp->setText(committedAddress_);
    ui->lblTcpPort->setText(committedPort_);
    ui->lblTcpIp->setToolTip(tr("原config.ini的tcpmain/ip；主控默认192.168.100.123。"));
    ui->lblTcpPort->setToolTip(tr("原config.ini的tcpmain/port；沿用原值，当前原配置为5000。"));
    connectionDirty_ = false;
    updatePermissions();
}

void InstrumentPage::setConnectionBusy(bool busy)
{
    const bool previous = isBusy();
    connectionBusy_ = busy;
    if (previous != isBusy()) emit busyChanged(isBusy());
    updatePermissions();
}

void InstrumentPage::loadConnectionConfiguration()
{
    if (!store_ || connectionFile_.isEmpty() || connectionBusy_) return;
    connectionRequest_ = nextConnectionRequest();
    setConnectionBusy(true);
    if (!QMetaObject::invokeMethod(store_, "loadConnectionSettings", Qt::QueuedConnection,
        Q_ARG(quint64, connectionRequest_), Q_ARG(QString, connectionFile_)))
    {
        connectionRequest_ = 0;
        setConnectionBusy(false);
        report(tr("连接配置读取未进入工作线程，请重新读取。"));
    }
}

void InstrumentPage::saveConnectionConfiguration()
{
    if (!store_ || isBusy() || !connectionDirty_ || connectionFingerprint_.isEmpty() ||
        !session_.authenticated || session_.role != 5) return;
    connectionRequest_ = nextConnectionRequest();
    setConnectionBusy(true);
    if (!QMetaObject::invokeMethod(store_, "saveConnectionSettings", Qt::QueuedConnection,
        Q_ARG(quint64, connectionRequest_), Q_ARG(QString, connectionFile_),
        Q_ARG(QString, connectionFingerprint_), Q_ARG(QString, ui->lblTcpIp->text()),
        Q_ARG(QString, ui->lblTcpPort->text())))
    {
        connectionRequest_ = 0;
        setConnectionBusy(false);
        report(tr("连接配置保存未进入工作线程，草稿保留。"));
    }
}

void InstrumentPage::loadSelectedMonth()
{
    if (!store_ || !ready_ || busy_) return;
    requestedMonth_=ui->dateSelect->date();
    // 换月必须先撤去旧月份勾选；锁库/读失败不能把上月结果显示在新月份下。
    showMaintenanceRecords({});
    setProperty("maintenanceLoaded",false);
    for (QTableWidget *table : {ui->tableDayMaintain,ui->tableWeekMaintain,ui->tableMonthMaintain})
        table->setToolTip(tr("正在读取所选月份维护记录；当前空白格尚未确定。"));
    setBusy(true);
    QMetaObject::invokeMethod(store_,"loadMonth",Qt::QueuedConnection,Q_ARG(QDate,requestedMonth_));
}

void InstrumentPage::showMaintenanceRecords(const MaintenanceRecords &records)
{
    const QList<QTableWidget *> tables={ui->tableDayMaintain,ui->tableWeekMaintain,ui->tableMonthMaintain};
    for (QTableWidget *table : tables)
    {
        for (int column=1;column<table->columnCount();++column)
        {
            // 原月表固定展示1..31列；小月无记录的格保持空白，不重排原列宽。
            table->setColumnHidden(column,false);
            for (int row=1;row<table->rowCount();++row)
            {
                auto *item=table->item(row,column);
                if (!item) { item=new QTableWidgetItem;table->setItem(row,column,item); }
                item->setText(QString());
                item->setFont(QFont(QStringLiteral("Noto Sans CJK SC"),12));
                item->setTextAlignment(Qt::AlignCenter);
            }
        }
    }
    const QHash<QString,QPair<int,int>> positions = {
        {QStringLiteral("waste_cup"),{0,1}},{QStringLiteral("waste_water"),{0,2}},
        {QStringLiteral("waste_cup_box"),{0,3}},{QStringLiteral("long_clean"),{0,4}},
        {QStringLiteral("shut_down"),{0,5}},{QStringLiteral("clean_pool"),{1,1}},
        {QStringLiteral("clean_dirver"),{1,2}},{QStringLiteral("clean_needle"),{2,1}}
    };
    for (const MaintenanceRecord &record : records)
    {
        const QDate date=QDate::fromString(record.date,Qt::ISODate);
        if (!date.isValid() || date.year()!=requestedMonth_.year() || date.month()!=requestedMonth_.month()
            || !positions.contains(record.action)) continue;
        const auto position=positions.value(record.action);
        tables.at(position.first)->item(position.second,date.day())->setText(QString::fromUtf8("✔"));
    }
}

void InstrumentPage::recordManualMaintenance(QString action, QString text)
{
    if (!store_ || !ready_ || busy_ || !session_.authenticated || session_.role<1 || session_.role>5) return;
    setBusy(true);
    CusMessageBox question(this);
    question.showQuestionBox(tr("是否添加%1记录？").arg(text));
    if (question.getClickButton()!=QMessageBox::Yes || !session_.authenticated || session_.role<1 || session_.role>5)
    {
        setBusy(false);return;
    }
    // 原doMaintain始终登记系统今天；月份选择用于历史查询，不允许补造过去记录。
    const QDate today=QDate::currentDate();
    QMetaObject::invokeMethod(store_,"record",Qt::QueuedConnection,Q_ARG(QString,action),Q_ARG(QDate,today));
}

void InstrumentPage::setReplayState(const Sn100Protocol::ReplayState &state)
{
    const bool fresh=state.fresh && state.hasSnapshot && !state.finished && !state.stopped && state.error.isEmpty();
    setProperty("instrumentReplayFresh",fresh);
    const QString source=tr("离线文件回放；真实仪器未知，未发送命令。虚拟记录时间%1ms。").arg(state.sourceTimeMs);
    setReadOnlyStatus(state.snapshot,fresh,tr("回放"),source);
}

void InstrumentPage::setReadOnlyStatus(const Sn100Protocol::StatusSnapshot &snapshot, bool fresh,
    const QString &prefix, const QString &sourceDetail)
{
    clearDeviceValues();
    replayModel_.clear();
    setProperty("instrumentStatusFresh",fresh);
    if (!fresh) return;
    replayModel_=snapshot.model;
    const auto display=[this,&prefix,&sourceDetail](const QStringList &names,const QString &value)
    {
        for (const QString &name : names)
            if (auto *label=findChild<QLabel *>(name))
            {
                label->setTextFormat(Qt::PlainText);
                label->setText(prefix+QLatin1Char('/')+value);
                label->setToolTip(sourceDetail);
            }
    };
    display({QStringLiteral("lblDriverModel"),QStringLiteral("lbl6Main_emergencyCover_5"),
        QStringLiteral("lbl6Main_reagentScaner_6")},snapshot.model);
    display({QStringLiteral("lbl6Main_emergencyCover_2"),QStringLiteral("lbl6Main_emergencyCover_7")},
        stateName({tr("空闲"),tr("育温中"),tr("准备就绪"),tr("初始化中"),tr("初始化完成"),tr("测试中"),tr("暂停")},snapshot.mainStatus));
    display({QStringLiteral("lbl6Main_startButton_3"),QStringLiteral("lbl6Main_startButton_5")},
        stateName({tr("未连接"),tr("未测试"),tr("未测试仪器动作"),tr("未测试异常"),tr("测试全部正常"),
                   tr("测试终止"),tr("测试终止试剂"),tr("测试异常终止")},snapshot.ledStatus));
    display({QStringLiteral("lbl6Main_plateButton_5"),QStringLiteral("lbl6Main_plateButton_9")},
        stateName({tr("无错误"),tr("警告"),tr("进架暂停"),tr("进架立即停止"),tr("暂停加样"),tr("加样立即停止"),
                   tr("停止加试剂"),tr("停止所有")},snapshot.errorWorkType));
    // 原始子系统字段保留在协议快照；尚无SN100映射依据，不猜温度/液位/板卡状态。
}

void InstrumentPage::report(QString message)
{
    setProperty("instrumentStatus",message);
    emit statusChanged(message);
}
