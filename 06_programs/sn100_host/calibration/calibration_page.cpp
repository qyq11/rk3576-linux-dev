#include "calibration_page.h"
#include "ui_frmcalibrationmaterial.h"
#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <cmath>
#include <numeric>
#include <limits>

CalibrationPage::CalibrationPage(CalibrationStore *store, QWidget *parent)
    : QWidget(parent), ui(new Ui::frmCalibrationMaterial), m_store(store)
{
    ui->setupUi(this);
    // The 1797x1013 design has unused outer margins. Original shell content and
    // the right-side menu end at 1751x968; keep all business geometry intact.
    setFixedSize(1751, 968);
    applyStyle();
    ui->ldtChangeDate->setReadOnly(true);
    ui->ldtBooldBatch->setReadOnly(true);
    ui->ldtChangeDate->clear(); ui->ldtBooldBatch->clear();
    ui->tbCalibrationResult->clearContents(); ui->tbCalibrationResult->setRowCount(0);
    for (QTableWidget *table : {ui->tb_calibration_curve, ui->tblLineApply})
    {
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setStretchLastSection(true);
    }
    ui->tbCalibrationResult->verticalHeader()->hide();
    ui->tbCalibrationResult->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tbCalibrationResult->horizontalHeader()->setMinimumSectionSize(95);
    ui->stkLinePanel->setCurrentIndex(0);
    connect(ui->btnLineInfo, &QPushButton::clicked, this, [this]()
    { ui->stkLinePanel->setCurrentIndex(0); });
    connect(ui->btnLineUse, &QPushButton::clicked, this, [this]()
    { ui->stkLinePanel->setCurrentIndex(1); renderBatchReferences(); });
    connect(ui->btnSave, &QPushButton::clicked, this, &CalibrationPage::saveCurrent);
    connect(ui->btnCancel, &QPushButton::clicked, this, &CalibrationPage::cancelDraft);
    connect(ui->btnCopy, &QPushButton::clicked, this, &CalibrationPage::copyCurrent);
    connect(ui->btnDelLine, &QPushButton::clicked, this, &CalibrationPage::deleteCurrent);
    connect(store,&CalibrationStore::originalSourceReady,this,[this](const QString &root)
    {m_originalMode=true;m_originalRoot=root;updateActions();});
    connect(ui->btnCheck,&QPushButton::clicked,this,&CalibrationPage::reviewOriginalCurve);
    connect(ui->btnApply,&QPushButton::clicked,this,[this](){applyOriginalCurve(false);});
    connect(ui->btn_use_curve_2,&QPushButton::clicked,this,[this](){applyOriginalCurve(true);});
    connect(ui->btnCalculateIDSetting,&QPushButton::clicked,this,&CalibrationPage::editOriginalMethod);
    // This original button refers to instrument test waveforms, which are not
    // present in anonymous point records. The raw scatter remains on this page.
    ui->btnShowChart->setToolTip(tr("原始点在页面图中显示；真实测试波形尚未提供。"));
    auto *showRaw = new QAction(tr("放大原始点（未拟合）"), this);
    connect(showRaw, &QAction::triggered, this, &CalibrationPage::showRawChart);
    ui->cuplotCalibration->addAction(showRaw);
    ui->cuplotCalibration->setContextMenuPolicy(Qt::ActionsContextMenu);
    auto *create = new QAction(tr("新增本地原始点记录"), this);
    create->setObjectName(QStringLiteral("calibrationNewRecord"));
    create->setShortcut(QKeySequence(Qt::Key_Insert));
    create->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(create, &QAction::triggered, this, &CalibrationPage::startNewRecord);
    addAction(create);
    ui->tb_calibration_curve->addAction(create);
    ui->tb_calibration_curve->setContextMenuPolicy(Qt::ActionsContextMenu);
    ui->tb_calibration_curve->setToolTip(tr("右键或Insert新增本地原始点记录；不会审核或应用。"));
    connect(ui->tbCalibrationResult, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *)
    {
        // setItem also emits itemChanged while a saved record is being filled.
        // Read only a complete table, never the partially populated rows.
        if (!m_populating) { markDirty(); updateRawPlot(); }
    });
    connect(ui->lstProject, &QListWidget::currentRowChanged, this, [this](int row)
    {
        if (m_populating || row < 0) return;
        const qint64 id = ui->lstProject->item(row)->data(Qt::UserRole).toLongLong();
        if (id == m_projectId) return;
        if (!prepareToLeave())
        {
            QSignalBlocker blocker(ui->lstProject);
            for (int old = 0; old < ui->lstProject->count(); ++old)
                if (ui->lstProject->item(old)->data(Qt::UserRole).toLongLong() == m_projectId)
                    ui->lstProject->setCurrentRow(old);
            return;
        }
        showProject(id);
    });
    connect(ui->tb_calibration_curve, &QTableWidget::itemSelectionChanged, this, [this]()
    {
        const int row = ui->tb_calibration_curve->currentRow();
        if (m_populating || row < 0) return;
        const qint64 id = ui->tb_calibration_curve->item(row, 0)->data(Qt::UserRole).toLongLong();
        if (id == m_record.id) return;
        if (!prepareToLeave())
        {
            QSignalBlocker blocker(ui->tb_calibration_curve);
            for (int old = 0; old < ui->tb_calibration_curve->rowCount(); ++old)
                if (ui->tb_calibration_curve->item(old, 0)->data(Qt::UserRole).toLongLong() == m_record.id)
                    ui->tb_calibration_curve->selectRow(old);
            return;
        }
        showRecord(id);
    });
    connect(store, &CalibrationStore::ready, this, [this](const QString &)
    { m_ready = true; reload(); });
    connect(store, &CalibrationStore::catalogReady, this,
            [this](qint64 request, ProjectRecords projects, ProjectUnits units, ReagentSnapshot reagents)
    {
        if (request != m_request) return;
        m_projects = projects; m_units = units; m_reagents = reagents;
        QMetaObject::invokeMethod(m_store, "load", Qt::QueuedConnection, Q_ARG(qint64, request));
    });
    connect(store, &CalibrationStore::snapshotReady, this, [this](qint64 request, CalibrationRecords records)
    {
        if (request != m_request) return;
        m_records = records;
        populateProjects();
        qint64 selected = m_selectAfterLoad ? m_selectAfterLoad : m_record.id;
        m_selectAfterLoad = 0;
        for (const CalibrationRecord &record : m_records)
            if (record.id == selected) m_projectId = record.projectId;
        showProject(m_projectId);
        if (selected) showRecord(selected);
        setBusy(false);
        report(m_originalMode?tr("已读取原校准库，审核标志与批组应用关系按原记录显示；拟合与反算另行核对。"):tr("已读取本地原始点记录；全部未审核、未应用。右键曲线列表可新增。"));
        emit snapshotChanged(m_records);
        emit operationFinished(QStringLiteral("load"), true);
    });
    connect(store, &CalibrationStore::succeeded, this, [this](qint64 request, const QString &op, qint64 id)
    {
        if (request != m_request) return;
        m_dirty = false;
        m_selectAfterLoad = op == QStringLiteral("remove") ? 0 : id;
        if (op == QStringLiteral("remove")) m_record = CalibrationRecord();
        emit operationFinished(op, true);
        setBusy(false);
        reload();
    });
    connect(store, &CalibrationStore::error, this,
            [this](qint64 request, const QString &op, const QString &message)
    {
        if (request && request != m_request) return;
        setBusy(false);
        report(tr("%1失败：%2；草稿保留，可修正后重试。").arg(op, message));
        emit operationFinished(op, false);
    });
    renderRecord(); updateActions();
}
CalibrationPage::~CalibrationPage() { delete ui; }

void CalibrationPage::applyStyle()
{
    // Original stylecolor.cpp tableStyle3/4/5 and listWidgetStyle, with the
    // already migrated shell's default values. Active Windows theme is pending.
    setStyleSheet(QStringLiteral("QWidget#frmCalibrationMaterial{background:rgb(238,238,238);}"
        "QHeaderView{background:transparent;}QHeaderView::section{color:#444444;"
        "font:bold 14px 'Noto Sans CJK SC';height:40px;background:transparent;"
        "border:1px solid #E4E8EB;border-left:none;border-right:none;border-top:none;}"
        "QTableWidget{background:transparent;border:1px solid #E4E8EB;border-radius:5px;}"
        "QTableWidget::item{border:0;border-bottom:1px solid #DDDDDD;color:#444444;}"
        "QTableView::item:selected{background:rgba(183,215,255,255);}"
        "QListWidget{background:transparent;border:1px solid #E4E8EB;border-radius:5px;}"
        "QListWidget::item{color:#444444;height:40px;}"
        "QListWidget::item:selected{background:rgba(183,215,255,255);color:white;}"));
    ui->tb_calibration_curve->setStyleSheet(QStringLiteral(
        "QTableWidget{background:white;alternate-background-color:#EEEEEE;border-radius:10px;}"
        "QHeaderView::section{font:bold 18px 'Noto Sans CJK SC';height:52px;"
        "border-bottom:2px solid #DDDDDD;}"));
    ui->tblLineApply->setStyleSheet(QStringLiteral("QHeaderView::section{font:bold 18px 'Noto Sans CJK SC';}"));
    const QString buttons = QStringLiteral(".QPushButton{background:rgba(42,118,242,255);color:white;"
        "border-radius:4px;padding:2px;}.QPushButton:hover{background:rgba(42,118,242,170);}"
        ".QPushButton:pressed{background:rgba(135,206,250,255);}"
        ".QPushButton:disabled{background:#E4E4E4;color:#AAAAAA;}");
    ui->btnApply->setStyleSheet(buttons); ui->btn_use_curve_2->setStyleSheet(buttons);
    ui->wgtTabs->setStyleSheet(QStringLiteral(".QPushButton{font:bold 18px 'Noto Sans CJK SC';"
        "color:#AAAAAA;border:none;background:transparent;border-bottom:5px solid transparent;}"
        ".QPushButton:checked{color:#444444;border-bottom:5px solid rgba(232,51,35,255);}"));
    ui->wgtProjTableBg->setRadius(10); ui->wgtProjTableBg->setBackgroundColor(Qt::white);
    ui->wgtChartBg->setRadius(10); ui->wgtChartBg->setBackgroundColor(QColor("#F6F6F6"));
    ui->calFunMenu->setBackgroundColor(QColor(28, 28, 28));
    QLinearGradient gradient(0, 0, 0, ui->calFunMenu->height());
    gradient.setColorAt(0, QColor(255, 255, 255, 0)); gradient.setColorAt(1, QColor(255, 255, 255, 25));
    ui->calFunMenu->setGraBackgroundColor(gradient); ui->calFunMenu->setShadowEffect(Qt::AlignLeft);
    for (QtMaterialFlatButton *button : {ui->btnCheck, ui->btnShowChart, ui->btnReCalculate, ui->btnSave,
        ui->btnCancel, ui->btnDelLine, ui->btnCopy, ui->btnCalculateIDSetting})
    {
        button->setBackgroundMode(Qt::OpaqueMode); button->setCornerRadius(8);
        button->setBackgroundColor(QColor(127, 127, 127)); button->setForegroundColor(Qt::white);
        button->setUseThemeColors(false);
    }
}
void CalibrationPage::setSession(AccountSession session)
{
    // Integration must call prepareToLeave before logout; never silently discard
    // another user's draft if a session change nevertheless reaches this page.
    m_session = session;
    QMetaObject::invokeMethod(m_store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, session));
    updateActions();
}
void CalibrationPage::setProjects(ProjectRecords projects, ProjectUnits units)
{
    m_projects = projects; m_units = units;
    populateProjects(); updateActions();
    if (m_dirty) report(tr("项目资料已更新，当前原始点草稿保留；保存前会核对项目及两轴单位。"));
}
void CalibrationPage::setReagents(ReagentSnapshot reagents)
{
    m_reagents = reagents; renderBatchReferences(); updateActions();
}
void CalibrationPage::setBusy(bool busy)
{
    if (m_busy != busy) { m_busy = busy; emit busyChanged(busy); }
    updateActions();
}
void CalibrationPage::report(const QString &message)
{
    setToolTip(message); emit statusChanged(message);
}
void CalibrationPage::updateActions()
{
    bool currentProject = false;
    for (const ProjectRecord &project : m_projects)
        if (project.id == m_projectId && project.enabled) currentProject = true;
    const bool edit = m_ready && !m_busy && currentProject && CalibrationStore::canEdit(m_session);
    ui->btnSave->setEnabled(edit);
    ui->btnCopy->setEnabled(edit && m_record.id > 0 && !m_dirty);
    // 停用项目只允许历史读回；软隐藏同样改变本地状态，不能绕过启用门槛。
    ui->btnDelLine->setEnabled(edit && m_record.id > 0 && !m_dirty);
    ui->btnCancel->setEnabled(!m_busy && m_dirty);
    ui->lstProject->setEnabled(!m_busy); ui->tb_calibration_curve->setEnabled(!m_busy);
    ui->tbCalibrationResult->setEditTriggers(edit ? QAbstractItemView::DoubleClicked
        | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed : QAbstractItemView::NoEditTriggers);
    if (QAction *create = findChild<QAction *>(QStringLiteral("calibrationNewRecord"))) create->setEnabled(edit);
    const QString unavailable = tr("本轮仅本地原始点；真实曲线、拟合/逆算、审核应用及计算方法尚未验收。" );
    for (QWidget *control : QList<QWidget *>{ui->btnCheck, ui->btnReCalculate,
         ui->btnShowChart, ui->btnCalculateIDSetting, ui->btnApply, ui->btn_use_curve_2})
    { control->setEnabled(false); control->setToolTip(unavailable); }
    if(m_originalMode)
    {
        const bool stable=edit&&m_record.id>0&&!m_dirty;
        ui->btnCheck->setEnabled(stable&&!m_record.checked);
        ui->btnCheck->setToolTip(tr("按原版保存操作者确认的审核标志；不会把未执行的计算标记为已计算。"));
        ui->btnApply->setEnabled(stable);
        ui->btn_use_curve_2->setEnabled(stable);
        ui->btnCalculateIDSetting->setEnabled(stable&&m_session.role>=4);
        ui->btnApply->setToolTip(tr("把所选原批组绑定到当前原校准编号。"));
        ui->btn_use_curve_2->setToolTip(tr("把原Default批组绑定到当前原校准编号。"));
        ui->btnCalculateIDSetting->setToolTip(tr("保存原计算方法及坐标变换；拟合和反算仍需匹配算法实现。"));
    }
    ui->btnCalculateIDSetting->setVisible(m_session.role >= 4);
}
void CalibrationPage::reload()
{
    if (!m_ready || m_busy || m_dirty) return;
    setBusy(true);
    QMetaObject::invokeMethod(m_store, "loadCatalog", Qt::QueuedConnection, Q_ARG(qint64, ++m_request));
}
void CalibrationPage::populateProjects()
{
    const bool oldPopulating = m_populating; m_populating = true;
    ui->lstProject->clear();
    QList<qint64> seen;
    for (const ProjectRecord &project : m_projects)
    {
        bool hasCalibration = false;
        for (const ProjectResult &result : project.results)
            if (result.type == QStringLiteral("Calibration")) hasCalibration = true;
        bool hasHistory = false;
        for (const CalibrationRecord &record : m_records)
            if (record.projectId == project.id) hasHistory = true;
        if (!hasCalibration && !hasHistory) continue;
        auto *item = new QListWidgetItem(project.name, ui->lstProject);
        item->setData(Qt::UserRole, project.id); item->setSizeHint(QSize(250, 50));
        seen.append(project.id);
    }
    for (const CalibrationRecord &record : m_records)
    {
        if (seen.contains(record.projectId)) continue;
        auto *item = new QListWidgetItem(record.projectName + tr("（历史）"), ui->lstProject);
        item->setData(Qt::UserRole, record.projectId); item->setSizeHint(QSize(250, 50));
        seen.append(record.projectId);
    }
    if (m_dirty && m_record.projectId && !seen.contains(m_record.projectId))
    {
        auto *item = new QListWidgetItem(m_record.projectName + tr("（草稿／项目已变化）"), ui->lstProject);
        item->setData(Qt::UserRole, m_record.projectId); item->setSizeHint(QSize(250, 50));
        seen.append(m_record.projectId);
    }
    if (!seen.contains(m_projectId)) m_projectId = seen.isEmpty() ? 0 : seen.first();
    for (int row = 0; row < ui->lstProject->count(); ++row)
        if (ui->lstProject->item(row)->data(Qt::UserRole).toLongLong() == m_projectId)
            ui->lstProject->setCurrentRow(row);
    m_populating = oldPopulating;
}
void CalibrationPage::showProject(qint64 projectId)
{
    m_projectId = projectId;
    m_populating = true;
    ui->tb_calibration_curve->setRowCount(0);
    qint64 firstId = 0;
    for (const CalibrationRecord &record : m_records)
    {
        if (record.projectId != projectId) continue;
        const int row = ui->tb_calibration_curve->rowCount();
        ui->tb_calibration_curve->insertRow(row);
        auto *id = new QTableWidgetItem(QString::number(record.id));
        id->setData(Qt::UserRole, record.id);
        ui->tb_calibration_curve->setItem(row, 0, id);
        ui->tb_calibration_curve->setItem(row, 1, new QTableWidgetItem(record.changedAt.left(10)));
        ui->tb_calibration_curve->setRowHeight(row, 46);
        if (!firstId) firstId = record.id;
    }
    for (int row = 0; row < ui->lstProject->count(); ++row)
        if (ui->lstProject->item(row)->data(Qt::UserRole).toLongLong() == projectId)
            ui->lstProject->setCurrentRow(row);
    m_populating = false;
    showRecord(firstId); renderBatchReferences();
}
void CalibrationPage::showRecord(qint64 id)
{
    m_record = CalibrationRecord();
    for (const CalibrationRecord &record : m_records)
        if (record.id == id && record.projectId == m_projectId) { m_record = record; break; }
    m_dirty = false;
    m_populating = true;
    ui->tb_calibration_curve->clearSelection();
    for (int row = 0; row < ui->tb_calibration_curve->rowCount(); ++row)
        if (ui->tb_calibration_curve->item(row, 0)->data(Qt::UserRole).toLongLong() == m_record.id)
            ui->tb_calibration_curve->selectRow(row);
    m_populating = false;
    renderRecord(); updateActions();
}
void CalibrationPage::renderRecord()
{
    m_populating = true;
    const QString storedDate = m_record.changedAt.isEmpty() ? m_record.createdAt : m_record.changedAt;
    const QDateTime date = m_record.originalProfile?QDateTime::fromString(storedDate,QStringLiteral("yyyyMMddHHmmss")):QDateTime::fromString(storedDate,Qt::ISODateWithMs);
    ui->ldtChangeDate->setText(date.isValid() ? date.toLocalTime().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")) : storedDate);
    ui->ldtChangeDate->setToolTip(storedDate);
    ui->ldtBooldBatch->setText(m_record.bloodBatch);
    const int repeats = m_record.points.isEmpty() ? 2 : m_record.points.first().measurements.size();
    QStringList headers = {tr("索引"), tr("稀释比例"), tr("浓度 (%1)").arg(m_record.concentrationUnit)};
    for (int repeat = 0; repeat < repeats; ++repeat)
        headers << tr("测试值%1 (%2)").arg(repeat + 1).arg(m_record.resultUnit);
    headers << tr("平均值 (%1)").arg(m_record.resultUnit) << tr("反算值");
    ui->tbCalibrationResult->clear();
    ui->tbCalibrationResult->setColumnCount(headers.size());
    ui->tbCalibrationResult->setHorizontalHeaderLabels(headers);
    for (int column = 0; column < headers.size(); ++column)
        ui->tbCalibrationResult->horizontalHeaderItem(column)->setToolTip(headers[column]);
    ui->tbCalibrationResult->setRowCount(m_record.points.size());
    for (int row = 0; row < m_record.points.size(); ++row)
    {
        const CalibrationPoint &point = m_record.points[row];
        const double displayedConcentration=point.concentration*(m_record.originalProfile?m_record.unitRatio:1.0);
        QStringList values = {QString::number(point.index), point.sampleRate,QString::number(displayedConcentration,'g',17)};
        if(m_record.originalProfile&&!std::isfinite(displayedConcentration))values[2]=point.originalValues.value("concentration").toString();
        const QStringList originalMeasurements=point.originalValues.value("result_value").toString().split('|',Qt::KeepEmptyParts);
        for (int index=0;index<point.measurements.size();++index)
            values << (m_record.originalProfile&&!point.originalValues.isEmpty()?originalMeasurements.value(index):QString::number(point.measurements[index],'g',17));
        double sum=0;int count=0;
        for(double value:point.measurements)if(std::isfinite(value)&&(!m_record.originalProfile||value!=0)){sum+=value;++count;}
        const double mean=count?sum/count:std::numeric_limits<double>::quiet_NaN();
        values << (std::isfinite(mean)?QString::number(mean,'g',15):tr("无有效值")) << tr("未计算");
        for (int column = 0; column < values.size(); ++column)
        {
            auto *item = new QTableWidgetItem(values[column]);
            item->setToolTip(values[column]);
            item->setTextAlignment(Qt::AlignCenter);
            if (column == 0 || column >= 3 + repeats) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            ui->tbCalibrationResult->setItem(row, column, item);
        }
        ui->tbCalibrationResult->setRowHeight(row, 46);
    }
    ui->lblDetail->setPlainText(m_record.projectId ?
        tr("本地记录\n未审核／未应用\n方法：%1\n试剂批次：%2\n横轴：浓度 (%3)\n纵轴：测试平均值 (%4)\n未变换、未拟合；平均值只用于展示。")
            .arg(m_record.method, m_record.batchCode.isEmpty() ? tr("未关联") : m_record.batchCode,
                 m_record.concentrationUnit, m_record.resultUnit) :
        tr("无本地曲线\n右键曲线列表或按Insert录入原始点。\n拟合、审核、应用未启用。"));
    if(m_record.originalProfile)
        ui->lblDetail->setPlainText(tr("原校准编号：%1\n原审核：%2；原编辑：%3\n原应用批组：%4\n方法：%5；X：%6；Y：%7\n原始值直接读写；曲线拟合与浓度反算尚未执行。")
            .arg(m_record.id).arg(m_record.checked?tr("已审核"):tr("未审核"),m_record.edited?tr("已编辑"):tr("未编辑"),m_record.appliedBatchGroups.isEmpty()?tr("无"):m_record.appliedBatchGroups.join(QStringLiteral("、")),m_record.method,m_record.xTransform,m_record.yTransform));
    m_populating = false;
    updateRawPlot();
}
void CalibrationPage::renderBatchReferences()
{
    ui->tblLineApply->setRowCount(0);
    if(m_originalMode)
    {
        QStringList groups{QStringLiteral("Default")};
        for(const ReagentBatch &batch:m_reagents.batches)if(batch.projectId==m_projectId&&!groups.contains(batch.batchGroup))groups<<batch.batchGroup;
        for(const QString &group:groups)
        {const int row=ui->tblLineApply->rowCount();ui->tblLineApply->insertRow(row);ui->tblLineApply->setItem(row,0,new QTableWidgetItem(group));QStringList ids;for(const auto &record:m_records)if(record.projectId==m_projectId&&record.appliedBatchGroups.contains(group))ids<<QString::number(record.id);ui->tblLineApply->setItem(row,1,new QTableWidgetItem(ids.isEmpty()?tr("未绑定"):ids.join(',')));}
        return;
    }
    for (const ReagentBatch &batch : m_reagents.batches)
    {
        if (batch.projectId != m_projectId) continue;
        const int row = ui->tblLineApply->rowCount(); ui->tblLineApply->insertRow(row);
        ui->tblLineApply->setItem(row, 0, new QTableWidgetItem(batch.batchGroup));
        ui->tblLineApply->setItem(row, 1, new QTableWidgetItem(tr("未应用（仅本地记录）")));
        ui->tblLineApply->setRowHeight(row, 46);
    }
}
void CalibrationPage::startNewRecord()
{
    if (!m_ready || m_busy || !CalibrationStore::canEdit(m_session) || !prepareToLeave()) return;
    ProjectRecord selected;
    for (const ProjectRecord &project : m_projects) if (project.id == m_projectId) selected = project;
    if (!selected.id || !selected.enabled) { report(tr("请选择仍启用的项目。")); return; }
    QString concentrationUnit, resultUnit;
    double ratio = 0;
    for (const ProjectResult &result : selected.results)
    {
        if (result.type == QStringLiteral("Calibration")) concentrationUnit = result.unit;
        if (result.type == QStringLiteral("Original")) resultUnit = result.unit;
    }
    for (const ProjectUnit &unit : m_units) if (unit.name == concentrationUnit) ratio = unit.ratio;
    if (concentrationUnit.isEmpty() || resultUnit.isEmpty() || ratio <= 0)
    { report(tr("此项目缺少已保存的校准浓度单位或原始测试单位，请先在项目设置完成。")); return; }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("calibrationNewDialog"));
    dialog.setWindowTitle(tr("新增本地原始点记录（未审核／未应用）"));
    auto *form = new QFormLayout(&dialog);
    form->addRow(tr("项目"), new QLabel(selected.name, &dialog));
    auto *batch = new QComboBox(&dialog); batch->setObjectName(QStringLiteral("localBatch"));
    batch->addItem(tr("未关联试剂批次"), qint64(0));
    for (const ReagentBatch &value : m_reagents.batches)
        if (value.projectId == selected.id) batch->addItem(value.code, value.id);
    auto *blood = new QLineEdit(&dialog); blood->setObjectName(QStringLiteral("localBloodBatch")); blood->setMaxLength(64);
    auto *points = new QSpinBox(&dialog); points->setObjectName(QStringLiteral("localPointCount")); points->setRange(1, 32); points->setValue(3);
    auto *repeats = new QSpinBox(&dialog); repeats->setObjectName(QStringLiteral("localRepeatCount")); repeats->setRange(1, 8); repeats->setValue(2);
    form->addRow(tr("试剂批次（只作记录关联）"), batch); form->addRow(tr("校准品批号"), blood);
    form->addRow(tr("原始点数"), points); form->addRow(tr("每点重复数"), repeats);
    form->addRow(tr("输入单位"), new QLabel(tr("浓度 %1；测试值 %2，原值保存，不自动换算。").arg(concentrationUnit, resultUnit), &dialog));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons); connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    setBusy(true);
    const int dialogResult = dialog.exec();
    setBusy(false);
    if (dialogResult != QDialog::Accepted) { report(tr("已取消新增，未写入数据库。")); return; }
    bool selectionStillCurrent = m_projectId == selected.id && CalibrationStore::canEdit(m_session);
    for (const ProjectRecord &project : m_projects)
        if (project.id == selected.id)
        {
            selectionStillCurrent = selectionStillCurrent && project.enabled && project.name == selected.name;
            QString currentConcentration, currentResult;
            for (const ProjectResult &result : project.results)
            {
                if (result.type == QStringLiteral("Calibration")) currentConcentration = result.unit;
                if (result.type == QStringLiteral("Original")) currentResult = result.unit;
            }
            selectionStillCurrent = selectionStillCurrent && currentConcentration == concentrationUnit && currentResult == resultUnit;
        }
    bool projectExists = false;
    for (const ProjectRecord &project : m_projects) if (project.id == selected.id) projectExists = true;
    if (!selectionStillCurrent || !projectExists)
    { report(tr("录入期间项目或身份已变化，请重新选择后录入；未创建记录。")); return; }
    if (blood->text().trimmed().isEmpty()) { report(tr("校准品批号不能为空，未创建草稿。")); return; }
    m_record = CalibrationRecord();
    m_record.projectId = selected.id; m_record.projectName = selected.name;
    m_record.originalProfile=m_originalMode;m_record.originalRoot=m_originalRoot;
    m_record.batchId = batch->currentData().toLongLong();
    if (m_record.batchId) m_record.batchCode = batch->currentText();
    for (const ReagentBatch &value : m_reagents.batches)
        if (value.id == m_record.batchId) m_record.batchGroup = value.batchGroup;
    m_record.bloodBatch = blood->text().trimmed();
    m_record.createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    m_record.concentrationUnit = concentrationUnit; m_record.resultUnit = resultUnit; m_record.unitRatio = ratio;
    m_record.method = tr("未拟合"); m_record.xTransform = QStringLiteral("x"); m_record.yTransform = QStringLiteral("y");
    for (int index = 1; index <= points->value(); ++index)
    {
        CalibrationPoint point; point.index = index;
        // New numeric cells are cleared below: zeros must be an explicit input,
        // never fabricated calibration measurements.
        for (int repeat = 0; repeat < repeats->value(); ++repeat) point.measurements << 0;
        m_record.points << point;
    }
    renderRecord();
    m_populating = true;
    for (int row = 0; row < ui->tbCalibrationResult->rowCount(); ++row)
        for (int column = 2; column < 3 + repeats->value(); ++column)
            ui->tbCalibrationResult->item(row, column)->setText(QString());
    m_populating = false;
    m_dirty = true; updateRawPlot(); updateActions();
    report(tr("新增草稿：请逐点输入浓度和测试值，保存后仍未审核／未应用。"));
}
bool CalibrationPage::readEditedRecord(CalibrationRecord *record, QString *message) const
{
    *record = m_record;
    if (!record->projectId) { *message = tr("尚无原始点记录。"); return false; }
    const QRegularExpression numeric(QStringLiteral("^(?:0|[1-9][0-9]{0,4})(?:\\.[0-9]{1,4})?$"));
    const int repeats = record->points.isEmpty() ? 0 : record->points.first().measurements.size();
    if (ui->tbCalibrationResult->rowCount() != record->points.size()
        || ui->tbCalibrationResult->columnCount() != repeats + 5)
    { *message = tr("原始点表尚未完整，未读取部分数据。"); return false; }
    for (int row = 0; row < record->points.size(); ++row)
    {
        CalibrationPoint &point = record->points[row];
        for (int column = 0; column < ui->tbCalibrationResult->columnCount(); ++column)
            if (!ui->tbCalibrationResult->item(row, column))
            { *message = tr("原始点表尚未完整，未读取部分数据。"); return false; }
        point.sampleRate = ui->tbCalibrationResult->item(row, 1)->text().trimmed();
        for (int column = 2; column < 3 + repeats; ++column)
        {
            const QString value = ui->tbCalibrationResult->item(row, column)->text().trimmed();
            QString unchanged;
            if(record->originalProfile&&record->id>0&&!point.originalValues.isEmpty())
            {
                if(column==2){const double displayed=point.concentration*record->unitRatio;unchanged=std::isfinite(displayed)?QString::number(displayed,'g',17):point.originalValues.value("concentration").toString();}
                else unchanged=point.originalValues.value("result_value").toString().split('|',Qt::KeepEmptyParts).value(column-3);
                if(value==unchanged.trimmed())continue; // 未编辑的原文本、未知值和额外精度不经新库格式重写。
            }
            if (!numeric.match(value).hasMatch())
            { *message = tr("第%1点第%2列须为0至99999、最多4位小数的数，不能为空。").arg(row + 1).arg(column + 1); return false; }
            bool ok = false; const double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number) || number > 99999)
            { *message = tr("原始点超出允许范围。"); return false; }
            if (column == 2) point.concentration = number/(record->originalProfile?record->unitRatio:1.0);
            else point.measurements[column - 3] = number;
        }
    }
    *message = CalibrationStore::validate(*record);
    return message->isEmpty();
}
void CalibrationPage::markDirty()
{
    if (m_populating) return;
    m_dirty = true; updateActions();
}
void CalibrationPage::updateRawPlot()
{
    CalibrationRecord edited; QString message;
    if (!readEditedRecord(&edited, &message))
    {
        ui->cuplotCalibration->setRawPoints({}, m_record.concentrationUnit, m_record.resultUnit);
        // Clear stale summaries if a point becomes empty/invalid.
        QSignalBlocker blocker(ui->tbCalibrationResult);
        const int meanColumn = ui->tbCalibrationResult->columnCount() - 2;
        for (int row = 0; row < ui->tbCalibrationResult->rowCount(); ++row)
            if (auto *mean = ui->tbCalibrationResult->item(row, meanColumn)) mean->setText(tr("输入待修正"));
        return;
    }
    QList<QPointF> points;
    QSignalBlocker blocker(ui->tbCalibrationResult);
    for (int row = 0; row < edited.points.size(); ++row)
    {
        const CalibrationPoint &point = edited.points[row];
        double sum=0;int count=0;
        for(double value:point.measurements)if(std::isfinite(value)&&(!edited.originalProfile||value!=0)){sum+=value;++count;}
        const double mean=count?sum/count:std::numeric_limits<double>::quiet_NaN();
        if(std::isfinite(mean)&&std::isfinite(point.concentration))points<<QPointF(point.concentration*(edited.originalProfile?edited.unitRatio:1.0),mean);
        ui->tbCalibrationResult->item(row,ui->tbCalibrationResult->columnCount()-2)->setText(std::isfinite(mean)?QString::number(mean,'g',12):tr("无有效值"));
    }
    ui->cuplotCalibration->setRawPoints(points, edited.concentrationUnit, edited.resultUnit);
}
void CalibrationPage::saveCurrent()
{
    if (m_busy || !m_ready || !CalibrationStore::canEdit(m_session)) return;
    if (!m_record.projectId) { startNewRecord(); return; }
    if (m_record.projectId != m_projectId)
    { report(tr("当前项目与原始点记录不一致，请取消并重新选择。")); return; }
    if (!m_dirty) { report(tr("没有未保存的修改。")); return; }
    CalibrationRecord record; QString message;
    if (!readEditedRecord(&record, &message))
    { report(message); emit operationFinished(QStringLiteral("save"), false); return; }
    if (QMessageBox::question(this, tr("保存修改"), tr("保存本地原始点？此操作不会审核或应用曲线。"),
        QMessageBox::Save | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Save) return;
    setBusy(true);
    QMetaObject::invokeMethod(m_store, "save", Qt::QueuedConnection, Q_ARG(qint64, ++m_request), Q_ARG(CalibrationRecord, record));
}
void CalibrationPage::copyCurrent()
{
    if (m_busy || m_dirty || !m_record.id || !CalibrationStore::canEdit(m_session)) return;
    if (QMessageBox::question(this, tr("复制曲线"), tr("复制已保存的本地记录及原始点？副本仍未审核／未应用。"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    setBusy(true);
    QMetaObject::invokeMethod(m_store, "copy", Qt::QueuedConnection, Q_ARG(qint64, ++m_request),
        Q_ARG(qint64, m_record.id), Q_ARG(qint64, m_record.revision));
}
void CalibrationPage::deleteCurrent()
{
    if (m_busy || m_dirty || !m_record.id || !CalibrationStore::canEdit(m_session)) return;
    bool enabled = false;
    for (const ProjectRecord &project : m_projects)
        if (project.id == m_record.projectId && project.enabled) enabled = true;
    if (!enabled) { report(tr("项目已停用或缺失，仅可读取历史，不能隐藏删除校准记录。")); return; }
    if (QMessageBox::question(this, tr("删除曲线"), tr("隐藏此本地记录？数据库保留记录和原始点以便追溯。"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    setBusy(true);
    QMetaObject::invokeMethod(m_store, "remove", Qt::QueuedConnection, Q_ARG(qint64, ++m_request),
        Q_ARG(qint64, m_record.id), Q_ARG(qint64, m_record.revision));
}
void CalibrationPage::cancelDraft()
{
    if (m_busy) return;
    const qint64 id = m_record.id;
    showRecord(id);
    report(tr("已取消本地草稿，数据库未改变。"));
}
void CalibrationPage::reviewOriginalCurve()
{
    if(!m_originalMode||m_busy||m_dirty||!m_record.id||!CalibrationStore::canEdit(m_session))return;
    if(QMessageBox::question(this,tr("审核原曲线"),tr("确认按原业务审核当前曲线？此操作只保存原审核标志；尚未接入的拟合与反算不会自动执行。"),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    setBusy(true);
    QMetaObject::invokeMethod(m_store,"setOriginalReview",Qt::QueuedConnection,Q_ARG(qint64,++m_request),Q_ARG(qint64,m_record.id),Q_ARG(qint64,m_record.revision),Q_ARG(bool,true));
}
void CalibrationPage::applyOriginalCurve(bool useDefault)
{
    if(!m_originalMode||m_busy||m_dirty||!m_record.id||!CalibrationStore::canEdit(m_session))return;
    QString group=QStringLiteral("Default");
    if(!useDefault)
    {const int row=ui->tblLineApply->currentRow();if(row<0||!ui->tblLineApply->item(row,0)){report(tr("请先选择原试剂批组。"));return;}group=ui->tblLineApply->item(row,0)->text();}
    if(!m_record.checked)
    {report(tr("请先审核该原曲线，再选择批组应用；项目已明确启用自动应用时由后台按原规则核对。"));
     bool autoApply=false;for(const auto &project:m_projects)if(project.id==m_record.projectId)autoApply=project.algorithm.autoApply;if(!autoApply)return;}
    if(QMessageBox::question(this,tr("应用原曲线"),tr("把原批组“%1”绑定到校准编号%2？").arg(group).arg(m_record.id),QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes)return;
    setBusy(true);
    QMetaObject::invokeMethod(m_store,"applyOriginalCurve",Qt::QueuedConnection,Q_ARG(qint64,++m_request),Q_ARG(qint64,m_record.id),Q_ARG(qint64,m_record.revision),Q_ARG(QString,group));
}
void CalibrationPage::editOriginalMethod()
{
    if(!m_originalMode||m_busy||m_dirty||!m_record.id||m_session.role<4||!CalibrationStore::canEdit(m_session))return;
    QDialog dialog(this);dialog.setObjectName("originalCalibrationMethodDialog");dialog.setWindowTitle(tr("原计算方法设置"));
    auto *form=new QFormLayout(&dialog);auto *method=new QComboBox(&dialog),*x=new QComboBox(&dialog),*y=new QComboBox(&dialog);
    method->setObjectName("originalCalibrationMethod");x->setObjectName("originalCalibrationXTransform");y->setObjectName("originalCalibrationYTransform");
    method->addItems({tr("直线法"),tr("一元三次曲线法"),tr("插值曲线法"),tr("折线法"),tr("双线回归法")});
    x->addItems({"x","log(x)","1/x","ln(x)","log(log(x))","ln(ln(x))"});
    y->addItems({"y","log(y)","1/y","ln(y)","log(log(y))","ln(ln(y))"});
    for(auto pair:{qMakePair(method,m_record.method),qMakePair(x,m_record.xTransform),qMakePair(y,m_record.yTransform)})
    {if(pair.first->findText(pair.second)<0)pair.first->insertItem(0,pair.second);pair.first->setCurrentText(pair.second);}
    form->addRow(tr("计算方法"),method);form->addRow(tr("X坐标变换"),x);form->addRow(tr("Y坐标变换"),y);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);form->addRow(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    setBusy(true);const int result=dialog.exec();setBusy(false);
    if(result!=QDialog::Accepted)return;
    setBusy(true);QMetaObject::invokeMethod(m_store,"setOriginalMethod",Qt::QueuedConnection,Q_ARG(qint64,++m_request),Q_ARG(qint64,m_record.id),Q_ARG(qint64,m_record.revision),Q_ARG(QString,method->currentText()),Q_ARG(QString,x->currentText()),Q_ARG(QString,y->currentText()));
}
bool CalibrationPage::prepareToLeave()
{
    if (m_busy) { report(tr("本地操作尚未结束，请稍后再离开。")); return false; }
    if (!m_dirty) return true;
    const auto choice = QMessageBox::question(this, tr("未保存的校准原始点"),
        tr("有未保存修改，是否放弃草稿？"), QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (choice != QMessageBox::Discard) return false;
    cancelDraft(); return true;
}
void CalibrationPage::showRawChart()
{
    QDialog dialog(this); dialog.setWindowTitle(tr("本地原始点（未拟合／未应用）")); dialog.resize(900, 600);
    auto *layout = new QVBoxLayout(&dialog);
    auto *chart = new CalibrationChart(&dialog);
    chart->setRawPoints(ui->cuplotCalibration->rawPoints(), m_record.concentrationUnit, m_record.resultUnit);
    layout->addWidget(chart);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject); dialog.exec();
}
void CalibrationPage::closeEvent(QCloseEvent *event)
{
    if (prepareToLeave()) QWidget::closeEvent(event); else event->ignore();
}
