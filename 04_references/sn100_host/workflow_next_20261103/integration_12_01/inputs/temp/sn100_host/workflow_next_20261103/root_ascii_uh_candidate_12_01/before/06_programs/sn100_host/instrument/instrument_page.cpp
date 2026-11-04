#include "instrument_page.h"
#include "device_control.h"
#include "original_device_commands.h"
#include "original_status_values.h"
#include "protocol/read_only_messages.h"
#include "original_files/original_runtime_settings.h"
#include "ui_frmdriverset.h"
#include "ui/cusmessagebox.h"
#include "samples/material/qtmaterialscrollbar.h"

#include <QAbstractButton>
#include <QAtomicInteger>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QScroller>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolTip>
#include <QThread>
#include <limits>

namespace
{
const QString deviceUnavailable = QStringLiteral("仪器控制命令尚未接入；当前未建立真实连接、未发送命令。");
const QString buttonStyle = QStringLiteral(
    ".QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
    ".QPushButton:hover{background-color:rgba(42,118,242,170);}"
    ".QPushButton:pressed{background-color:rgba(135,206,250,255);}");


// ASCII是一字节一字符。先完整检查，不能用会替换字符或遇NUL截断的转换。
bool encodeCommunicationAscii(const QString &text, QByteArray *bytes, QString *message)
{
    if (bytes) bytes->clear();
    if (message) message->clear();
    for (int index=0;index<text.size();++index)
    {
        if (text.at(index).unicode() <= 0x7f) continue;
        uint code=text.at(index).unicode();
        if (text.at(index).isHighSurrogate() && index+1<text.size() && text.at(index+1).isLowSurrogate())
            code=QChar::surrogateToUcs4(text.at(index),text.at(index+1));
        if (message) *message=QCoreApplication::translate("InstrumentPage",
            "第%1个文本位置含非ASCII字符（U+%2）；ASCII转换失败，原内容已保留。")
            .arg(index+1).arg(QString::number(code,16).toUpper().rightJustified(4,QLatin1Char('0')));
        return false;
    }
    if (bytes)
    {
        bytes->reserve(text.size());
        for (const QChar character : text) bytes->append(static_cast<char>(character.unicode()));
    }
    return true;
}

QString communicationAsciiDisplay(const QByteArray &bytes)
{
    static const char hex[]="0123456789ABCDEF";
    QString text;
    text.reserve(bytes.size()*4);
    for (const char raw : bytes)
    {
        const unsigned char value=static_cast<unsigned char>(raw);
        if (value == '\\') text+=QStringLiteral("\\\\");
        else if (value >= 0x20 && value <= 0x7e) text+=QChar(value);
        else
        {
            text+=QStringLiteral("\\x");
            text+=QLatin1Char(hex[value>>4]);
            text+=QLatin1Char(hex[value&0x0f]);
        }
    }
    return text;
}

void appendCommunicationLine(QTextBrowser *view, const QString &line)
{
    // 沿原append习惯，仅原来位于底部时跟随；不移动使用者的光标或选择。
    QScrollBar *scroll=view->verticalScrollBar();
    const bool atBottom=scroll->value() >= scroll->maximum();
    // 直接插入纯文本，保留连续空格，不把设备内容作为HTML解释。
    QTextCursor cursor(view->document());
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::End);
    if (!view->document()->isEmpty()) cursor.insertBlock();
    cursor.insertText(line);
    cursor.endEditBlock();
    if (atBottom) scroll->setValue(scroll->maximum());
}

bool mayRecordManualMaintenance(const AccountSession &session)
{
    if (!session.authenticated || session.role < 0 || session.role > 5) return false;
    if (session.originalPolicySource.isEmpty()) return session.role >= 1;
    // 原维护页没有排除角色0；但必须来自口令或明确的原配置，未登录不能登记。
    return session.source == AccountSessionSource::Password || session.source == AccountSessionSource::Configured;
}

quint64 nextConnectionRequest()
{
    static QAtomicInteger<quint64> sequence(0);
    return sequence.fetchAndAddRelaxed(1) + 1;
}

quint64 nextManualCommunicationRequest()
{
    // 页面重建也不复用旧请求号；耗尽时拒绝，不从0重新编号。
    static QAtomicInteger<quint64> sequence(0);
    quint64 previous=sequence.loadRelaxed();
    while (previous<std::numeric_limits<quint64>::max())
    {
        if (sequence.testAndSetRelaxed(previous,previous+1)) return previous+1;
        previous=sequence.loadRelaxed();
    }
    return 0;
}
}

InstrumentPage::InstrumentPage(MaintenanceStore *store, QWidget *parent) :
    QWidget(parent), ui(new Ui::frmDriverSet), store_(store)
{
    ui->setupUi(this);
    setFixedSize(1751,968);
    applyOriginalStyle();
    disableDeviceActions();
    clearDeviceValues(true);
    connect(ui->sendTextEdit,&QTextEdit::textChanged,this,&InstrumentPage::refreshCommunicationAsciiInput);
    refreshCommunicationAsciiInput();
    connect(ui->push_send_btn,&QPushButton::clicked,this,&InstrumentPage::requestManualCommunication);
    displayedValues_=OriginalInstrument::statusDisplay(displayedSnapshot_,0);
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
            if ((item.first == ui->btnPort || item.first == ui->btnFunTest || item.first == ui->btn_CommunicationTest)
                && (!session_.authenticated || session_.role < 5)) return;
            ui->stackedWidget->setCurrentIndex(item.second);
            if (item.second == 2 && ready_) loadSelectedMonth();
            if (item.second == 0 && !connectionFile_.isEmpty() && prepareToLeave())
                loadConnectionConfiguration();
        });
    }
    connect(ui->btn_board_status,&QPushButton::clicked,this,[this]()
    {
        if (isBusy() || !session_.authenticated || session_.role < 4) return;
        // 原on_btn_board_status_clicked：已连接按实际型号，等待连接按20/42试剂位选页。
        const bool linked = displayedStatusFresh_ && displayedSnapshot_.ledStatus != 0;
        const bool otherModel = linked ? replayModel_ != QStringLiteral("SN450")
                                      : configuredReagentPositions_ == 42;
        ui->stackedWidget->setCurrentIndex(otherModel ? 5 : 4);
    });
    ui->btn_board_status->setToolTip(tr("按原版机型规则显示电路板状态；仅显示主控实际上报的值。"));

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
        sentCommunication_=0;receivedCommunication_=0;
    });
    connect(ui->pushButton_ShowSwitch,&QPushButton::clicked,this,[this]()
    {
        if (!originalCommunicationConfigured_) return;
        showCommunication_=!showCommunication_;
        ui->pushButton_ShowSwitch->setText(showCommunication_ ? tr("显示开关：开") : tr("显示开关：关"));
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

void InstrumentPage::clearDeviceValues(bool initializeCommunication)
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
            label->setToolTip(tr("没有当前有效的主控数据；未上报的值不能作为正常状态依据。"));
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
    // 状态刷新不能清除操作者正在输入的命令，也不能把通信计数反复归零。
    if (initializeCommunication)
    {
        ui->lineEdit->clear();
        ui->label_SendCount->setText(QStringLiteral("0"));
        ui->label_ReceiveCount->setText(QStringLiteral("0"));
    }
    clearOriginalAdValues(tr("没有当前有效的主控连接或状态；等待新的Qq读数。"));
}

void InstrumentPage::refreshSensors()
{
    const auto &sensors=displayedValues_.sensors;
    ui->tlbSensor->setRowCount(sensors.size());
    for (int row=0;row<sensors.size();++row)
    {
        const auto &sensor=sensors.at(row);
        auto *name=ui->tlbSensor->item(row,0);
        if (!name)
        {
            name=new QTableWidgetItem;
            name->setFont(QFont(QStringLiteral("Noto Sans CJK SC"),10));
            name->setTextAlignment(Qt::AlignLeft|Qt::AlignVCenter);
            ui->tlbSensor->setItem(row,0,name);
        }
        name->setText(sensor.name);
        auto *value=qobject_cast<QLabel *>(ui->tlbSensor->cellWidget(row,1));
        if (!value)
        {
            value=new QLabel;
            value->setFont(name->font());value->setAlignment(Qt::AlignCenter);
            ui->tlbSensor->setCellWidget(row,1,value);
        }
        const QString valuePrefix=displayedPrefix_.isEmpty() || displayedPrefix_==tr("设备")
            ? QString() : displayedPrefix_+QLatin1Char('/');
        value->setText(displayedStatusFresh_ ? valuePrefix+sensor.value : tr("未知"));
        value->setStyleSheet(!displayedStatusFresh_ || !sensor.known
            ? QStringLiteral("QLabel{color:rgb(170,170,170);}")
            : sensor.normal ? QString() : QStringLiteral("QLabel{background-color:white;color:#FF0000;border-radius:1px;border:1px solid white;}"));
        value->setToolTip(displayedDetail_);
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
    cancelManualCommunication(tr("会话已变化，旧手动请求不再继续；请核对已有收发，不自动重发。"));
    ++manualCommunicationSession_;
    session_=session;
    if (store_) QMetaObject::invokeMethod(store_,"setSession",Qt::QueuedConnection,Q_ARG(AccountSession,session));
    updatePermissions();
    setReadOnlyStatus(displayedSnapshot_, displayedStatusFresh_, displayedPrefix_, displayedDetail_);
    if (originalAdConfigured_) refreshOriginalAdChannels();
    ui->stackedWidget->setCurrentIndex(1);
}

void InstrumentPage::updatePermissions()
{
    const bool authenticated=mayRecordManualMaintenance(session_);
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
            : tr("请先登录或按原配置建立操作员会话，再登记维护记录。"));
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
        if (manualCommunicationPending_ && (!state.connected ||
            manualCommunicationConnection_!=deviceControl_->connectionGeneration()))
            cancelManualCommunication(tr("设备连接已失效或换代，旧手动请求停止等待；不转交新连接或自动重发。"));
        if (deviceControl_ && communicationGeneration_ != deviceControl_->connectionGeneration())
        {
            communicationGeneration_=deviceControl_->connectionGeneration();
            sentDecoder_.reset();
        }
        if (deviceControl_ && originalAdGeneration_ != deviceControl_->connectionGeneration())
        {
            originalAdGeneration_ = deviceControl_->connectionGeneration();
            clearOriginalAdValues(tr("连接已改变，旧连接的AD读数已清除。"));
        }
        setReadOnlyStatus(state.status, state.connected && state.fresh, tr("设备"), state.message);
        updateDeviceButtons();
        report(state.message);
    });
    connect(control, &DeviceControl::wireActivity, this,
        [this](const QString &direction, const QByteArray &bytes)
    {
        if (originalCommunicationConfigured_)
        {
            // 实际发送事件是带封包的字节；只有完整解码后才能按原业务命令显示。
            if (direction.startsWith(QStringLiteral("TX")))
            {
                if (!communicationClock_.isValid()) communicationClock_.start();
                const auto batch=sentDecoder_.feed(bytes,communicationClock_.elapsed());
                for (const QByteArray &payload : batch.messages) appendOriginalCommunication(true,payload);
            }
            return;
        }
        // 使用原通信收发区域，限制界面保留量，避免长时间接收无限增长。
        QTextBrowser *view = direction.startsWith(QStringLiteral("TX"))
                                 ? ui->textBrowser_SendData : ui->textBrowser_ReceiveData;
        if (view->document()->blockCount() > 500) view->clear();
        appendCommunicationLine(view,direction + QStringLiteral(" ASCII=") + communicationAsciiDisplay(bytes) +
            QStringLiteral(" hex=") + QString::fromLatin1(bytes.toHex(' ')));
    });
    connect(control,&DeviceControl::payloadReceived,this,[this](const QByteArray &payload)
    {
        if (originalCommunicationConfigured_) appendOriginalCommunication(false,payload);
    });
    const auto toggleConnection = [this]()
    {
        if (!deviceControl_ || isBusy() || connectionDirty_) return;
        if (deviceControl_->connectionRequested()) deviceControl_->close();
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
        button->setText(deviceControl_ && deviceControl_->connectionRequested() ? tr("断开") : tr("连接"));
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
    const bool realAdministrator=administrator && (session_.source==AccountSessionSource::Password ||
        session_.source==AccountSessionSource::Configured);
    ui->push_send_btn->setEnabled(manualCommunicationConsumerReady_ && realAdministrator && connected &&
        !isBusy() && !connectionDirty_ && !manualCommunicationPending_);
    ui->push_send_btn->setToolTip(tr("本片支持精确正文Qj、Qk、QH、Ts、Tp[0]、Mz；其余正文尚未兼容。开始、暂停与静音沿主流程确认，输入不会自动改写。"));
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
    if (!store_ || !ready_ || busy_ || !mayRecordManualMaintenance(session_)) return;
    setBusy(true);
    CusMessageBox question(this);
    question.showQuestionBox(tr("是否添加%1记录？").arg(text));
    if (question.getClickButton()!=QMessageBox::Yes || !mayRecordManualMaintenance(session_))
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
    // 保存一份最后收到的快照供换账户时重新按角色显示，不重新查询或发送控制命令。
    const bool modelChanged=displayedSnapshot_.model!=snapshot.model;
    displayedSnapshot_=snapshot;
    displayedStatusFresh_=fresh;
    displayedPrefix_=prefix;
    displayedDetail_=sourceDetail;
    displayedValues_=OriginalInstrument::statusDisplay(snapshot,session_.authenticated ? session_.role : 0);
    // 同机型连续状态直接更新现有值，避免每次报文先清空再重画全部输出。
    if (!fresh || modelChanged) clearDeviceValues();
    replayModel_.clear();
    setProperty("instrumentStatusFresh",fresh);
    refreshSensors();
    for (QLabel *cover : {ui->lblReagentOpen,ui->lblDuilentOpen,ui->lblEmergencyOpen})
    {
        const auto normal=displayedValues_.coverNormal.constFind(cover->objectName());
        cover->setStyleSheet(!fresh || normal==displayedValues_.coverNormal.cend()
            ? QStringLiteral("QLabel{color:rgb(170,170,170);}")
            : normal.value() ? QString() : QStringLiteral("QLabel{background-color:white;color:#FF0000;border-radius:1px;border:1px solid white;}"));
    }
    if (deviceControl_)
    {
        const bool connected=deviceControl_->state().connected;
        ui->label_ConnectStatus->setText(connected ? tr("已连接") : tr("已断开"));
        ui->label_ConnectStatus->setStyleSheet(connected ? QStringLiteral("QLabel{color:green;}")
                                                       : QStringLiteral("QLabel{color:red;}"));
    }
    if (!fresh) return;
    replayModel_=snapshot.model;
    // 真实连接沿原版显示文字；历史回放保留来源前缀，来源细节始终放提示文字。
    const QString valuePrefix=prefix.isEmpty() || prefix==tr("设备") ? QString() : prefix+QLatin1Char('/');
    for (auto it=displayedValues_.labels.cbegin();it!=displayedValues_.labels.cend();++it)
    {
        if (auto *label=findChild<QLabel *>(it.key()))
        {
            label->setTextFormat(Qt::PlainText);
            label->setText(valuePrefix+it.value());
            label->setToolTip(sourceDetail+(it.value()==tr("未上报")
                ? tr("\n本次状态报文缺少该字段或数值无效，不能作为正常状态依据。") : QString()));
        }
    }
    // 原slotStatusChanged仅SN600显示育温盘温度，隐藏稀释架盖和急诊架盖。
    const bool sn600=displayedValues_.sn600;
    const QList<QWidget *> covers={ui->lbl10_2,ui->lblDuilentOpen,ui->lineEdit_5,
                                   ui->lbl10_3,ui->lblEmergencyOpen,ui->lineEdit_6};
    for (QWidget *widget : covers) widget->setVisible(!sn600);
    const QList<QWidget *> warming={ui->lbl10_25,ui->lblWPlateTemp,ui->label_13};
    for (QWidget *widget : warming) widget->setVisible(sn600);
}

bool InstrumentPage::configureOriginalAdChannels(const OriginalRuntime::Channels &channels,
    int testChannels, QString *message)
{
    if (message) message->clear();
    if (QThread::currentThread() != thread() || testChannels < 0 || testChannels > 1024
        || channels.test.size() > 1024)
    {
        if (message) *message = tr("通道资料不在界面线程或数量超出本地显示范围，未替换。 ");
        return false;
    }
    QSet<QString> numbers;
    for (const auto &channel : channels.test)
    {
        if (channel.number.isEmpty() || numbers.contains(channel.number))
        {
            if (message) *message = tr("原通道名称为空或重复，未猜测通道身份。 ");
            return false;
        }
        numbers.insert(channel.number);
    }
    originalAdChannels_ = channels;
    originalAdChannelCount_ = testChannels;
    originalAdConfigured_ = true;
    originalAdHeadersReloaded_ = false;
    refreshOriginalAdChannels();
    return true;
}

bool InstrumentPage::reloadOriginalAdChannels(int testChannels, QString *message)
{
    if (message) message->clear();
    if (QThread::currentThread() != thread() || !originalAdConfigured_
        || testChannels < 0 || testChannels > 1024)
    {
        if (message) *message = tr("原通道资料尚未就绪或通道数量无效，未重载AD表。 ");
        return false;
    }
    originalAdChannelCount_ = testChannels;
    originalAdHeadersReloaded_ = true;
    refreshOriginalAdChannels();
    return true;
}

void InstrumentPage::refreshOriginalAdChannels()
{
    ui->tableChannel->clearContents();
    ui->tableChannel->setRowCount(0);
    ui->tableChannel->setHorizontalHeaderLabels({tr("通道号"),tr("开启状态"),
        originalAdHeadersReloaded_ ? QStringLiteral("450nm") : QStringLiteral("405nm"),
        QStringLiteral("570nm"),QStringLiteral("660nm"),
        originalAdChannelCount_ == 24 ? QStringLiteral("   ") : QStringLiteral("800nm")});
    const int role = session_.authenticated ? session_.role : 0;
    for (const auto &channel : originalAdChannels_.test)
    {
        // 原refreshChannelTable保留数据库返回顺序和完整字符串通道名。
        if (channel.number == QStringLiteral("Base"))
        {
            if (role < 4) continue;
        }
        else if (channel.number.toInt() > originalAdChannelCount_) continue;
        const int row = ui->tableChannel->rowCount();
        ui->tableChannel->insertRow(row);
        for (int column = 0; column < 6; ++column)
        {
            if (column == 1) continue;
            auto *item = new QTableWidgetItem(column == 0 ? channel.number : QStringLiteral("-"));
            item->setFont(QFont(QStringLiteral("Noto Sans CJK SC"),12));
            item->setTextAlignment(Qt::AlignCenter);
            ui->tableChannel->setItem(row,column,item);
        }
        auto *enabled = new QPushButton(ui->tableChannel);
        enabled->setCheckable(true);
        enabled->setChecked(channel.enabled);
        enabled->setText(channel.enabled ? tr("开启") : tr("关闭"));
        enabled->setMaximumWidth(95);
        enabled->setEnabled(false);
        enabled->setToolTip(tr("显示原通道资料中的开关状态；此处暂不可修改。"));
        enabled->setStyleSheet(QStringLiteral(
            "QPushButton{background-color:#FFFFFF;color:#444444;border-radius:5px;border:1px solid #DDDDDD;}"
            "QPushButton:checked{color:#FFFFFF;background-color:rgba(%1);}").arg(originalAdCheckedColour_));
        ui->tableChannel->setCellWidget(row,1,enabled);
        ui->tableChannel->setRowHeight(row,39);
    }
    clearOriginalAdValues(tr("已读取原通道资料，等待主控实际上报Qq读数；开关仅显示原库状态。"));
}

void InstrumentPage::clearOriginalAdValues(const QString &reason)
{
    for (int row = 0; row < ui->tableChannel->rowCount(); ++row)
        for (int column = 2; column < 6; ++column)
            if (auto *item = ui->tableChannel->item(row,column))
            {
                item->setText(QStringLiteral("-"));
                item->setToolTip(reason);
                item->setData(Qt::UserRole,false);
            }
    ui->tableChannel->setToolTip(reason);
}

bool InstrumentPage::receiveOriginalAd(const QByteArray &payload, quint64 connectionGeneration, QString *message)
{
    if (message) message->clear();
    const auto reject = [this,message](const QString &reason)
    {
        if (message) *message = reason;
        ui->tableChannel->setToolTip(reason);
        return false;
    };
    if (QThread::currentThread() != thread())
    {
        if (message) *message = tr("AD读数必须在界面线程接收。 ");
        return false;
    }
    if (!originalAdConfigured_) return reject(tr("原通道资料未就绪，未按AD回包创建通道或开关。"));
    if (!deviceControl_ || !deviceControl_->state().connected
        || connectionGeneration != deviceControl_->connectionGeneration())
        return reject(tr("AD回复不属于当前有效连接，未替换通道读数。"));
    Sn100Protocol::ReadOnlyMessage parsed;
    QString error;
    if (!payload.startsWith("Qq") || !Sn100Protocol::parseReadOnlyMessage(payload,&parsed,&error))
        return reject(tr("AD回复未通过原字段校验，保留最后一次读数：%1").arg(error));
    if (originalAdGeneration_ != connectionGeneration)
    {
        originalAdGeneration_ = connectionGeneration;
        clearOriginalAdValues(tr("新连接等待本次AD读数。"));
    }
    const QJsonObject channels = parsed.data.value(QStringLiteral("channels")).toObject();
    const QString receivedAt = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    for (int row = 0; row < ui->tableChannel->rowCount(); ++row)
    {
        const QString number = ui->tableChannel->item(row,0)->text();
        const bool present = channels.contains(number);
        const QStringList values = present ? channels.value(number).toString().split(QLatin1Char(' '),Qt::KeepEmptyParts)
                                           : QStringList();
        for (int column = 0; column < 4; ++column)
        {
            auto *item = ui->tableChannel->item(row,column+2);
            const bool reported = present && column < values.size();
            if (!present) item->setText(QStringLiteral("-"));
            else if (reported) item->setText(values.at(column));
            // 原短列表保留未提供列的旧文字；提示明确它不是本次读数，不能当作新测量。
            item->setData(Qt::UserRole,reported);
            item->setToolTip(reported ? tr("主控Qq，通道%1，第%2项；接收时间%3。")
                .arg(number).arg(column+1).arg(receivedAt)
                : present ? tr("本次Qq未提供此项；按原版保留旧文字，不能作为本次读数。")
                          : tr("本次Qq未包含此通道，显示原版缺值标记。"));
        }
    }
    ui->tableChannel->setToolTip(tr("主控Qq接收于%1；开关是原库状态，不表示设备已确认。重复通道%2项按原版取首值。")
        .arg(receivedAt).arg(parsed.data.value(QStringLiteral("duplicate_channels")).toInt()));
    return true;
}

void InstrumentPage::report(QString message)
{
    setProperty("instrumentStatus",message);
    emit statusChanged(message);
}

bool InstrumentPage::applyOriginalRuntime(const OriginalRuntime::RuntimeSettings &settings, QString *message)
{
    if (message) message->clear();
    // 原main把kBtnNormalColor交给buttonStyle::getStrChecked；通道状态沿用同一主题值。
    originalAdCheckedColour_ = settings.theme.colours.value(QStringLiteral("kBtnNormalColor"),
        QStringLiteral("42,118,242,255"));
    if (originalAdConfigured_) refreshOriginalAdChannels();
    showCommunication_=settings.debug.showCommandsInitially;
    showOnlyCommunication_=settings.debug.showCommandFilter;
    maskCommunication_=settings.debug.maskCommandFilter;
    const auto debug=settings.originalIniFiles.constFind(QStringLiteral("config_Debug.ini"));
    if (debug != settings.originalIniFiles.cend())
    {
        // 原split保留空项；contains(空串)的原行为也须保留，不把空名单另解释成禁用。
        shownCommunication_=debug->values.value(QStringLiteral("TCPTest/ShowCommand"),QString()).toString().split('|');
        maskedCommunication_=debug->values.value(QStringLiteral("TCPTest/MaskCommand"),QString()).toString().split('|');
    }
    else
    {
        shownCommunication_=settings.debug.shownCommands;
        maskedCommunication_=settings.debug.maskedCommands;
    }
    originalCommunicationConfigured_=true;
    ui->pushButton_ShowSwitch->setProperty("requiresInstrument",false);
    ui->pushButton_ShowSwitch->setEnabled(true);
    ui->pushButton_ShowSwitch->setToolTip(tr("只改变通信文本显示，不发送仪器命令，也不修改原配置文件。"));
    ui->pushButton_ShowSwitch->setText(showCommunication_ ? tr("显示开关：开") : tr("显示开关：关"));
    ui->textBrowser_SendData->document()->setMaximumBlockCount(500);
    ui->textBrowser_ReceiveData->document()->setMaximumBlockCount(500);
    ui->lbl10_20->setText(settings.theme.company);
    configuredReagentPositions_=settings.theme.reagentPositions;
    ui->lblDriverModel->setText(settings.theme.driverModel);
    ui->lblDriverModel->setToolTip(tr("原主题配置中的机型名称；实际设备状态以收到的有效报文为准。"));
    return true;
}

bool InstrumentPage::prepareCommunicationAsciiPayload(QByteArray *payload, QString *message) const
{
    // toPlainText会先把不换行空格改为普通空格，必须先保留原字符再检查。
    QString text=ui->sendTextEdit->document()->toRawText();
    text.replace(QChar(QChar::ParagraphSeparator),QLatin1Char('\n'));
    return encodeCommunicationAscii(text,payload,message);
}

void InstrumentPage::setManualCommunicationConsumerReady(bool ready)
{
    if (!ready) cancelManualCommunication(tr("手动请求消费者已撤销，旧请求不再继续。"));
    manualCommunicationConsumerReady_=ready;
    updateDeviceButtons();
}

QString InstrumentPage::manualCommunicationRequestBlocker(quint64 request, quint64 session,
    quint64 connection, const QByteArray &payload) const
{
    if (!request || request!=manualCommunicationPending_ || session!=manualCommunicationSession_ ||
        connection!=manualCommunicationConnection_ || payload!=manualCommunicationPayload_)
        return tr("本次手动请求已撤销或不属于原点击，未继续处理。");
    if (!manualCommunicationConsumerReady_ || !session_.authenticated || session_.role!=5 ||
        (session_.source!=AccountSessionSource::Password && session_.source!=AccountSessionSource::Configured))
        return tr("本次手动请求需要当前真实管理员会话。");
    if (!deviceControl_ || !deviceControl_->state().connected ||
        connection!=deviceControl_->connectionGeneration()) return tr("本次手动请求的设备连接已失效。");
    if (isBusy() || connectionDirty_) return tr("仪器页面正在处理或连接设置尚未保存，未继续本次手动请求。");
    return QString();
}

void InstrumentPage::showManualCommunicationMessage(const QString &stage, const QString &message)
{
    setProperty("manualCommunicationStage",stage);
    setProperty("manualCommunicationMessage",message);
    report(message);
    if (isVisible() && ui->stackedWidget->currentIndex()==7 && isActiveWindow())
        QToolTip::showText(ui->push_send_btn->mapToGlobal(QPoint(0,ui->push_send_btn->height())),
            message,ui->push_send_btn);
}

void InstrumentPage::requestManualCommunication()
{
    if (manualCommunicationPending_) return;
    if (!manualCommunicationConsumerReady_ || !session_.authenticated || session_.role!=5 ||
        (session_.source!=AccountSessionSource::Password && session_.source!=AccountSessionSource::Configured) ||
        !deviceControl_ || !deviceControl_->state().connected || isBusy() || connectionDirty_)
    { showManualCommunicationMessage(QStringLiteral("rejected"),tr("当前会话、连接或页面状态不允许手动提交，未调用发送。")); return; }
    QByteArray payload;
    QString message;
    if (!prepareCommunicationAsciiPayload(&payload,&message))
    { showManualCommunicationMessage(QStringLiteral("encoding-rejected"),message); return; }
    if (payload.isEmpty())
    { showManualCommunicationMessage(QStringLiteral("empty"),tr("正文为空，本次未提交。")); return; }
    if (payload.size()>Sn100Protocol::kMaxMessageBytes)
    { showManualCommunicationMessage(QStringLiteral("length-rejected"),tr("正文超过当前128000字节整条预算，未截断或提交。")); return; }
    if (payload!="Qj" && payload!="Qk" && payload!="QH" && payload!="Ts" && payload!="Tp[0]" && payload!="Mz")
    { showManualCommunicationMessage(QStringLiteral("unsupported"),tr("ASCII编码合法，但本正文尚未接入相应业务流程，本次未提交；并非设备拒绝。")); return; }
    const quint64 request=nextManualCommunicationRequest();
    if (!request)
    { showManualCommunicationMessage(QStringLiteral("rejected"),tr("当前进程的手动请求号已耗尽，未复用旧编号或提交。")); return; }
    manualCommunicationPending_=request;
    manualCommunicationConnection_=deviceControl_->connectionGeneration();
    manualCommunicationPayload_=payload;
    const quint64 session=manualCommunicationSession_;
    const quint64 connection=manualCommunicationConnection_;
    const AccountSession identity=session_;
    updateDeviceButtons();
    // 先登记等待再发值副本，主窗可同步拒绝，不能在emit返回后恢复旧等待。
    showManualCommunicationMessage(QStringLiteral("requested"),tr("已转交本次手动请求，仍待主流程核对；尚未确认入队或完成。"));
    const QString blocker=manualCommunicationRequestBlocker(request,session,connection,payload);
    if (!blocker.isEmpty())
    {
        if (manualCommunicationPending_==request && manualCommunicationSession_==session)
            cancelManualCommunication(blocker);
        return;
    }
    emit manualCommunicationRequested(request,session,connection,identity,payload);
}

void InstrumentPage::cancelManualCommunication(const QString &reason)
{
    if (!manualCommunicationPending_) return;
    const quint64 request=manualCommunicationPending_, session=manualCommunicationSession_;
    const quint64 connection=manualCommunicationConnection_;
    manualCommunicationPending_=0; manualCommunicationConnection_=0; manualCommunicationPayload_.clear();
    updateDeviceButtons();
    emit manualCommunicationRevoked(request,session,connection,reason);
    showManualCommunicationMessage(QStringLiteral("revoked"),reason);
}

void InstrumentPage::receiveManualCommunicationResult(quint64 request, quint64 session, quint64 connection,
    bool terminal, const QString &stage, const QString &message)
{
    if (!request || request!=manualCommunicationPending_ || session!=manualCommunicationSession_ ||
        connection!=manualCommunicationConnection_) return;
    if (terminal)
    { manualCommunicationPending_=0; manualCommunicationConnection_=0; manualCommunicationPayload_.clear(); }
    updateDeviceButtons();
    showManualCommunicationMessage(stage,message);
}

void InstrumentPage::refreshCommunicationAsciiInput()
{
    QString message;
    const bool valid=prepareCommunicationAsciiPayload(nullptr,&message);
    const QString previous=ui->sendTextEdit->property("communicationAsciiInputError").toString();
    ui->sendTextEdit->setProperty("communicationAsciiInputValid",valid);
    ui->sendTextEdit->setProperty("communicationAsciiInputError",message);
    ui->sendTextEdit->setAccessibleDescription(valid ? tr("通讯测试ASCII输入") : message);
    ui->sendTextEdit->setToolTip(valid
        ? tr("仅接受ASCII字符；回车为LF，文字形式的\\xHH按原文字保留。输入检查通过不表示命令已发送。")
        : message);
    if (!valid && message!=previous && ui->sendTextEdit->hasFocus())
        QToolTip::showText(ui->sendTextEdit->mapToGlobal(QPoint(0,ui->sendTextEdit->height())),
            message,ui->sendTextEdit);
    else if (valid && !previous.isEmpty() && ui->sendTextEdit->hasFocus()) QToolTip::hideText();
}

void InstrumentPage::appendOriginalCommunication(bool transmitted, const QByteArray &payload)
{
    if (!showCommunication_) return;
    if (!transmitted)
    {
        // 仅过滤沿用旧正文解码；ASCII展示另行生成，不改变名单匹配或空项含义。
        const QString filterText=QString::fromUtf8(payload);
        if (showOnlyCommunication_)
        {
            bool found=false;
            for (const QString &command : shownCommunication_) if (filterText.contains(command)) {found=true;break;}
            if (!found) return;
        }
        else if (maskCommunication_)
        {
            for (const QString &command : maskedCommunication_) if (filterText.contains(command)) return;
        }
    }
    const QString text=communicationAsciiDisplay(payload);
    QTextBrowser *view=transmitted ? ui->textBrowser_SendData : ui->textBrowser_ReceiveData;
    const QString line=QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss:zzz")) +
        (transmitted ? QStringLiteral(" TX ") : QStringLiteral(" RX ")) + text +
        QStringLiteral(" hex=") + QString::fromLatin1(payload.toHex(' '));
    appendCommunicationLine(view,line);
    if (transmitted) ui->label_SendCount->setText(QString::number(++sentCommunication_));
    else ui->label_ReceiveCount->setText(QString::number(++receivedCommunication_));
}

bool InstrumentPage::applyCommittedOriginalChannels(const OriginalRuntime::Channels &channels,
    quint64 generation, QString *message)
{
    if (message) message->clear();
    if (QThread::currentThread() != thread())
    {
        if (message) *message = tr("通道资料必须在界面线程更新。");
        return false;
    }
    if (!originalAdConfigured_)
    {
        if (message) *message = tr("原通道资料未就绪，未替换通道表。");
        return false;
    }
    if (!deviceControl_ || !deviceControl_->state().connected
        || generation != deviceControl_->connectionGeneration())
    {
        if (message) *message = tr("通道资料不属于当前有效连接，未替换通道表。");
        return false;
    }
    if (channels.test.size() > 1024 || channels.fingerprint.size() != 64)
    {
        if (message) *message = tr("通道数量或资料指纹无效，未替换通道表。");
        return false;
    }
    QSet<QString> numbers;
    for (const auto &channel : channels.test)
    {
        if (channel.number.isEmpty() || numbers.contains(channel.number))
        {
            if (message) *message = tr("原通道名称为空或重复，未猜测通道身份。");
            return false;
        }
        numbers.insert(channel.number);
    }
    originalAdChannels_ = channels;
    refreshOriginalAdChannels();
    return true;
}
