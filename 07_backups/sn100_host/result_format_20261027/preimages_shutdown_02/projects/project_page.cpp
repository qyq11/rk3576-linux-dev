#include "project_page.h"
#include "project_format_dialog.h"
#include "../history/history_current_display_state.h"
#include <QMenu>
#include <QAction>
#include <QFileInfo>
#include <QTimer>
#include "ui_frmsystemmaterial.h"
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QInputDialog>
#include <QIntValidator>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QSignalBlocker>

ProjectPage::ProjectPage(ProjectsStore *store, QWidget *parent)
    : QWidget(parent), ui(new Ui::frmSystemMaterial), m_store(store)
{
    ui->setupUi(this);
    setFixedSize(1751, 968);
    applyStyle();
    ui->lstProject->setColumnHidden(0, true);
    ui->lstProject->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->lstProject->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->lstProject->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->lstProject->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->lstProject->verticalHeader()->hide();
    ui->resultTable->verticalHeader()->hide();
    ui->resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->resultTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->resultTable, &QWidget::customContextMenuRequested, this, [this](const QPoint &point)
    {
        QMenu menu(this);
        auto *configure = menu.addAction(tr("配置结果显示…"));
        configure->setObjectName(QStringLiteral("configureCurrentResultFormat"));
        configure->setEnabled(m_ready && m_selectedId > 0);
        if (menu.exec(ui->resultTable->viewport()->mapToGlobal(point)) == configure) openResultFormat();
    });
    // Keep the original .ui 116px columns and last-column stretch. Resizing
    // to editor hints would push the unit column offscreen on Linux.
    ui->is_used->setText(tr("项目启用"));
    ui->is_related->setText(tr("组分管理"));
    ui->ckxCalibrationUsed->setText(tr("启用浓度结果计算"));
    ui->ckxRUsed->setText(tr("启用FBG结果计算"));
    ui->ckxINRUsed->setText(tr("启用INR结果计算"));
    ui->ckxMdaUsed->setText(tr("启用MDA结果计算"));
    // Preserve original hidden controls. Unported tabs stay in their original
    // positions and explicitly report unavailable; no instrument is constructed.
    ui->wgtMda->hide();
    ui->btnReTest->hide();
    ui->btnCalculate->hide();
    ui->btnReport->hide();
    ui->btnChange->hide();
    ui->btnDect->hide();
    ui->btnScan->hide();
    ui->label_32->hide();
    ui->is_related->hide();
    for (QPushButton *tab : {ui->btnCalibrate})
    {
        tab->setEnabled(false);
        tab->setToolTip(tr("校准计算设置尚未验收；本页仅保存本地测试方案资料。"));
    }
    for (CusCheckBox *checkbox : {ui->ckxCalibrationUsed, ui->ckxRUsed, ui->ckxINRUsed, ui->ckxMdaUsed})
    {
        checkbox->setEnabled(false);
        checkbox->setToolTip(tr("算法结果启用配置尚未移植，不能在脱机基础页启用计算。"));
    }
    ui->test_analysis_method->setEnabled(false);
    ui->d_T->setReadOnly(true);
    ui->d_T->clear();
    ui->d_T->setToolTip(tr("原字段命名与显示单位尚待核对，当前不编辑或保存采样频率。"));
    ui->manager_id->setReadOnly(true);
    ui->change_time->setReadOnly(true);
    ui->ldt_methon->setReadOnly(true);
    for (QLineEdit *edit : {ui->xishu_a, ui->xishu_b, ui->sample_warmup_length})
    {
        edit->setReadOnly(true);
        edit->clear();
        edit->setToolTip(tr("算法系数未在本地基础库配置。"));
    }
    ui->project_cn_name->setMaxLength(16);
    ui->project_en_name->setMaxLength(16);
    ui->project_lis_code->setMaxLength(8);
    ui->stkPanel->setCurrentIndex(0);
    m_advanced = new ProjectAdvancedEditor(this, this);
    connect(m_advanced, &ProjectAdvancedEditor::edited, this, &ProjectPage::markDirty);
    connect(ui->btnBaseInfo, &QPushButton::clicked, this, [this]() { ui->stkPanel->setCurrentIndex(0); });
    connect(ui->btnSampleAdd, &QPushButton::clicked, this, [this]() { ui->stkPanel->setCurrentIndex(1); });
    connect(ui->btnReagentAdd, &QPushButton::clicked, this, [this]() { ui->stkPanel->setCurrentIndex(2); });
    for (QLineEdit *edit : {ui->project_cn_name, ui->project_en_name, ui->project_lis_code})
    {
        connect(edit, &QLineEdit::textChanged, this, &ProjectPage::markDirty);
    }
    connect(ui->is_used, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
            [this](bool) { markDirty(); });
    connect(ui->btnSave, &QPushButton::clicked, this, &ProjectPage::save);
    connect(ui->btnCopy, &QPushButton::clicked, this, &ProjectPage::copy);
    connect(ui->btnDelProj, &QPushButton::clicked, this, &ProjectPage::remove);
    connect(ui->btnCancel, &QPushButton::clicked, this,
            [this]()
            {
                showProject(m_selectedId);
                report(tr("已取消本地未保存修改。"));
            });
    connect(ui->lstProject, &QTableWidget::itemSelectionChanged, this,
            [this]()
            {
                if (m_populating || m_busy || ui->lstProject->currentRow() < 0)
                {
                    return;
                }
                const qint64 id =
                    ui->lstProject->item(ui->lstProject->currentRow(), 0)->data(Qt::UserRole).toLongLong();
                if (id == m_selectedId)
                {
                    return;
                }
                if (!prepareToLeave())
                {
                    QSignalBlocker blocker(ui->lstProject);
                    for (int row = 0; row < ui->lstProject->rowCount(); ++row)
                    {
                        if (ui->lstProject->item(row, 0)->data(Qt::UserRole).toLongLong() == m_selectedId)
                        {
                            ui->lstProject->selectRow(row);
                        }
                    }
                    return;
                }
                showProject(id);
            });
    connect(store, &ProjectsStore::ready, this,
            [this](const QString &databasePath)
            {
                m_ready = true;
                m_catalogDatabasePath = QFileInfo(databasePath).canonicalFilePath();
                if (m_formatDialog)
                {
                    m_formatRefreshAfterClose = true;
                    m_formatDialog->invalidateContext(tr("项目资料源已重新打开，请关闭后重新进入。"));
                }
                emit currentFormatInvalidated(tr("项目资料源已更新。"));
                reload();
            });
    connect(store, &ProjectsStore::closed, this, [this]()
    {
        m_ready = false;
        m_catalogDatabasePath.clear();
        emit currentFormatInvalidated(tr("项目资料源已关闭。"));
        updateActions();
    });
    connect(store, &QObject::destroyed, this, [this]()
    {
        m_ready = false;
        m_catalogDatabasePath.clear();
        emit currentFormatInvalidated(tr("项目存储已销毁。"));
        if (!m_formatDialog) setBusy(false);
        else updateActions();
    });
    connect(store, &ProjectsStore::reagentOptionsReady, m_advanced, &ProjectAdvancedEditor::setReagentChoices);
    connect(
        store, &ProjectsStore::snapshotReady, this,
        [this](qint64 request, ProjectRecords projects, ProjectUnits units)
        {
            if (request != m_request)
            {
                return;
            }
            m_projects = projects;
            m_units = units;
            m_populating = true;
            ui->lstProject->setRowCount(projects.size());
            for (int row = 0; row < projects.size(); ++row)
            {
                auto *id = new QTableWidgetItem(QString::number(projects[row].id));
                id->setData(Qt::UserRole, projects[row].id);
                ui->lstProject->setItem(row, 0, id);
                ui->lstProject->setItem(row, 1, new QTableWidgetItem(projects[row].name));
                ui->lstProject->setRowHeight(row, 41);
            }
            m_populating = false;
            qint64 selected = m_selectAfterLoad > 0 ? m_selectAfterLoad : m_selectedId;
            bool exists = false;
            for (const ProjectRecord &project : projects)
            {
                if (project.id == selected)
                {
                    exists = true;
                }
            }
            if (!exists)
            {
                selected = projects.isEmpty() ? 0 : projects.first().id;
            }
            m_selectAfterLoad = 0;
            showProject(selected);
            setBusy(false);
            emit snapshotChanged(m_projects, m_units);
            if (projects.isEmpty())
            {
                report(tr(
                    "项目库为空；原版仅支持复制。匿名演示请在新隔离库显式启用种子，生产初始配置导入待验。"));
            }
            else
            {
                report(tr("项目/单位已读取；当前仅保存本地配置，未同步仪器。"));
            }
            emit operationFinished(QStringLiteral("load"), true);
        });
    connect(store, &ProjectsStore::succeeded, this,
            [this](qint64 request, const QString &operation, qint64 id)
            {
                // The auxiliary writer owns its receipt; never clear the ordinary Page draft for it.
                if (operation == QStringLiteral("replaceCurrentResultFormat")) return;
                if (request != m_request)
                {
                    return;
                }
                emit currentFormatInvalidated(tr("项目配置已实际提交。"));
                m_dirty = false;
                m_selectAfterLoad = operation == QStringLiteral("remove") ? 0 : id;
                report(tr("本地%1成功；未同步仪器。").arg(operation));
                emit operationFinished(operation, true);
                reload();
            });
    connect(store, &ProjectsStore::error, this,
            [this](qint64 request, const QString &operation, const QString &message)
            {
                if (operation == QStringLiteral("replaceCurrentResultFormat") || operation == QStringLiteral("loadCurrentProjectFormat"))
                {
                    if (request == m_request)
                    {
                        report(message);
                        emit currentFormatInvalidated(message);
                        emit operationFinished(operation, false);
                    }
                    return;
                }
                if (request != 0 && request != m_request)
                {
                    return;
                }
                setBusy(false);
                report(message);
                emit currentFormatInvalidated(message);
                emit operationFinished(operation, false);
                // Failed writes retain the editable draft, and the next action may retry.
                if (isVisible())
                {
                    QMessageBox::warning(this, tr("项目设置"), message);
                }
            });
    updateActions();
}
ProjectPage::~ProjectPage()
{
    emit currentFormatInvalidated(tr("项目设置界面已关闭。"));
    delete ui;
}
void ProjectPage::applyStyle()
{
    // Source public/stylecolor.cpp buttonStyle/listWidgetStyle/tableStyle2/
    // comboboxStyle + frmsystemmaterial.cpp constructor; shipped theme values
    // already used by the existing shell. Active Windows theme remains unverified.
    setStyleSheet(QStringLiteral(
        ".QPushButton{background-color:rgba(42,118,242,255);color:#FFFFFF;border-radius:4px;padding:2px;}"
        ".QPushButton:hover{background-color:rgba(42,118,242,170);}"
        ".QPushButton:pressed{background-color:rgba(135,206,250,255);}"
        "QHeaderView{background-color:transparent;}"
        "QHeaderView::section{color:#444444;font:bold 15px 'Noto Sans CJK "
        "SC';height:40px;background-color:transparent;border:1px solid "
        "#DDDDDD;border-left:none;border-right:none;border-top:none;}"
        "QTableWidget::item{border:0;border-bottom:1px solid #DDDDDD;}"
        "QTableView::item:selected{background:rgba(183,215,255,255);}"
        "QTableWidget{background-color:transparent;border:1px solid #E4E8EB;border-radius:5px;}"
        "QComboBox{border:1px solid #E4E8EB;border-radius:5px;padding:2px "
        "4px;background-color:transparent;color:#444444;font:18px 'Noto Sans CJK SC';}"
        "QComboBox:disabled{background-color:#E4E4E4;}"
        "QComboBox QAbstractItemView{border:none;color:#444444;font:20px 'Noto Sans CJK SC';}"
        "QComboBox QAbstractItemView::item{height:50px;}"
        "QComboBox QAbstractItemView::item:selected{background:rgba(183,215,255,255);color:white;}"
        "QComboBox::drop-down{width:20px;border:none;}QComboBox::down-arrow{width:25px;height:25px;image:url("
        ":/image/arrow_drop_down.svg);}"));
    const QString tabs = QStringLiteral(
        ".QPushButton{font:bold 18px 'Noto Sans CJK "
        "SC';color:#AAAAAA;border:none;background-color:transparent;border-bottom:5px solid transparent;}"
        ".QPushButton:checked{color:#444444;border-bottom:5px solid rgba(232,51,35,255);}");
    ui->wgtTabs->setStyleSheet(tabs);
    for (QPushButton *tab : {ui->btnBaseInfo, ui->btnSampleAdd, ui->btnReagentAdd, ui->btnCalibrate,
                             ui->btnReport, ui->btnReTest, ui->btnCalculate})
    {
        tab->setStyleSheet(tabs);
    }
    ui->systemFunMenu->setBackgroundColor(QColor(28, 28, 28));
    QLinearGradient gradient(0, 0, 0, ui->systemFunMenu->height());
    gradient.setColorAt(0, QColor(255, 255, 255, 0));
    gradient.setColorAt(1, QColor(255, 255, 255, 25));
    ui->systemFunMenu->setGraBackgroundColor(gradient);
    ui->systemFunMenu->setShadowEffect(Qt::AlignLeft);
    ui->wgtProjTableBg->setRadius(10);
    ui->wgtProjTableBg->setBackgroundColor(Qt::white);
    ui->wgtProjDetailBg->setRadius(10);
    ui->wgtProjDetailBg->setBackgroundColor(Qt::white);
    for (QtMaterialFlatButton *button :
         {ui->btnChange, ui->btnDect, ui->btnScan, ui->btnSave, ui->btnCancel, ui->btnCopy, ui->btnDelProj})
    {
        button->setBackgroundMode(Qt::OpaqueMode);
        button->setCornerRadius(8);
        button->setBackgroundColor(QColor(127, 127, 127));
        button->setForegroundColor(Qt::white);
        button->setUseThemeColors(false);
    }
}
void ProjectPage::setSession(AccountSession session)
{
    m_session = session;
    if (m_formatDialog) m_formatDialog->setSession(session);
    QMetaObject::invokeMethod(m_store, "setSession", Qt::QueuedConnection, Q_ARG(AccountSession, session));
    updateActions();
}
void ProjectPage::setBusy(bool busy)
{
    if (busy)
    {
        emit currentFormatInvalidated(tr("项目操作开始，当前结果显示需重新核查。"));
    }
    if (m_busy != busy)
    {
        m_busy = busy;
        emit busyChanged(busy);
    }
    updateActions();
}
void ProjectPage::updateActions()
{
    const bool writable = m_ready && !m_busy && m_formatDialog.isNull() && m_selectedId > 0 && ProjectsStore::canEdit(m_session);
    ui->btnSave->setEnabled(writable);
    ui->btnCancel->setEnabled(writable);
    ui->btnCopy->setEnabled(writable);
    ui->btnDelProj->setEnabled(writable);
    ui->lstProject->setEnabled(!m_busy && m_formatDialog.isNull());
    for (QLineEdit *edit : {ui->project_en_name, ui->project_cn_name, ui->project_lis_code})
    {
        edit->setReadOnly(!writable);
    }
    ui->is_used->setEnabled(writable);
    // Guest keeps selection/context-menu access, while every actual editor stays read-only.
    ui->resultTable->setEnabled(m_ready && !m_busy && m_formatDialog.isNull());
    for (int row = 0; row < ui->resultTable->rowCount(); ++row)
        for (int column = 0; column < ui->resultTable->columnCount(); ++column)
        {
            auto *widget = ui->resultTable->cellWidget(row, column);
            if (auto *edit = qobject_cast<QLineEdit *>(widget))
                edit->setReadOnly(!writable || column == 1 || column == 9);
            else if (widget) widget->setEnabled(writable);
        }
    m_advanced->setWritable(writable);
    ui->btnSampleAdd->setEnabled(m_ready && !m_busy && m_formatDialog.isNull());
    ui->btnReagentAdd->setEnabled(m_ready && !m_busy && m_formatDialog.isNull());
}
void ProjectPage::reload()
{
    if (m_formatDialog)
    {
        report(tr("请先完成或关闭结果显示配置；未覆盖任何草稿。"));
        return;
    }
    if (!m_ready)
    {
        return;
    }
    if (m_dirty && !prepareToLeave())
    {
        return;
    }
    setBusy(true);
    ++m_request;
    QMetaObject::invokeMethod(m_store, "load", Qt::QueuedConnection, Q_ARG(qint64, m_request));
}
void ProjectPage::showProject(qint64 id)
{
    if (m_formatDialog) return; // A late ordinary reply cannot repopulate an open auxiliary editor.
    m_populating = true;
    m_selectedId = id;
    ProjectRecord project;
    for (const ProjectRecord &candidate : m_projects)
    {
        if (candidate.id == id)
        {
            project = candidate;
        }
    }
    ui->project_en_name->setText(project.name);
    ui->project_cn_name->setText(project.fullName);
    ui->project_lis_code->setText(project.lisCode);
    ui->manager_id->setText(project.instrumentProjectId);
    ui->change_time->setText(project.changedAt);
    ui->is_used->setCheckState(project.enabled && id > 0 ? Qt::Checked : Qt::Unchecked);
    ui->ldt_methon->clear();
    ui->test_analysis_method->setCurrentIndex(-1);
    QStringList rateLabels;
    QMap<QString, bool> rateOptions;
    QString defaultRate;
    for (const auto &rate : project.sampleRates)
    {
        rateLabels.append(rate.sampleRate);
        rateOptions.insert(rate.sampleRate, rate.sampleOption);
        if (rate.defaultOption) defaultRate = rate.sampleRate;
    }
    m_advanced->showAdvanced(project.advanced, rateLabels);
    m_advanced->setSampleSelections(rateOptions, defaultRate);
    ui->resultTable->setRowCount(project.results.size());
    for (int row = 0; row < project.results.size(); ++row)
    {
        const ProjectResult &result = project.results[row];
        auto *main = new CusCheckBox(ui->resultTable);
        main->setCheckState(result.mainResult ? Qt::Checked : Qt::Unchecked);
        main->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
        main->setText(tr("选用"));
        main->setFocusPolicy(Qt::NoFocus);
        ui->resultTable->setCellWidget(row, 0, main);
        connect(main, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
                [this, main](bool checked)
                {
                    if (checked)
                    {
                        for (int otherRow = 0; otherRow < ui->resultTable->rowCount(); ++otherRow)
                        {
                            auto *other =
                                qobject_cast<CusCheckBox *>(ui->resultTable->cellWidget(otherRow, 0));
                            if (other && other != main)
                            {
                                other->setCheckState(Qt::Unchecked);
                            }
                        }
                    }
                    else
                    {
                        main->setCheckState(Qt::Checked);
                    }
                    markDirty();
                });
        const QStringList values = {result.type,
                                    result.name,
                                    QString::number(result.decimals),
                                    QString::number(result.testLow),
                                    QString::number(result.testHigh),
                                    QString::number(result.normalLow),
                                    QString::number(result.normalHigh),
                                    result.lisCode};
        for (int index = 0; index < values.size(); ++index)
        {
            const int column = index + 1;
            if (column == 3)
            {
                auto *decimals = new QComboBox(ui->resultTable);
                decimals->addItems({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
                                    QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("6")});
                decimals->setCurrentText(values[index]);
                decimals->setView(new QListView);
                decimals->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
                connect(decimals, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                        [this](int) { markDirty(); });
                ui->resultTable->setCellWidget(row, column, decimals);
                continue;
            }
            auto *edit = new QLineEdit(values[index], ui->resultTable);
            edit->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
            edit->setAlignment(Qt::AlignCenter);
            edit->setStyleSheet(QStringLiteral("border:1px solid #E4E8EB;border-radius:5px;padding:2px "
                                               "4px;background-color:transparent;color:#444444;"));
            edit->setReadOnly(column == 1);
            edit->setContextMenuPolicy(Qt::NoContextMenu);
            if (column == 2)
            {
                edit->setMaxLength(16);
            }
            if (column == 8)
            {
                edit->setMaxLength(8);
            }
            if (column >= 4 && column <= 7)
            {
                edit->setValidator(new QDoubleValidator(0, 999, 3, edit));
            }
            connect(edit, &QLineEdit::textEdited, this, &ProjectPage::markDirty);
            ui->resultTable->setCellWidget(row, column, edit);
        }
        if (result.type == QStringLiteral("Calibration") || result.type == QStringLiteral("Rate"))
        {
            auto *unit = new QComboBox(ui->resultTable);
            unit->setView(new QListView);
            for (const ProjectUnit &value : m_units)
            {
                unit->addItem(value.name, value.ratio);
            }
            unit->setCurrentText(result.unit);
            unit->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
            ui->resultTable->setCellWidget(row, 9, unit);
            connect(unit, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { markDirty(); });
        }
        else
        {
            auto *unit = new QLineEdit(result.unit, ui->resultTable);
            unit->setReadOnly(true);
            unit->setAlignment(Qt::AlignCenter);
            unit->setStyleSheet(QStringLiteral("border:1px solid #E4E8EB;border-radius:5px;padding:2px "
                                               "4px;background-color:transparent;color:#444444;"));
            ui->resultTable->setCellWidget(row, 9, unit);
        }
        ui->resultTable->setRowHeight(row, 41);
    }
    for (int row = 0; row < ui->lstProject->rowCount(); ++row)
    {
        if (ui->lstProject->item(row, 0)->data(Qt::UserRole).toLongLong() == id)
        {
            ui->lstProject->selectRow(row);
        }
    }
    m_dirty = false;
    m_populating = false;
    updateActions();
}
ProjectRecord ProjectPage::editedProject() const
{
    ProjectRecord project;
    for (const ProjectRecord &candidate : m_projects)
    {
        if (candidate.id == m_selectedId)
        {
            project = candidate;
        }
    }
    project.name = ui->project_en_name->text();
    project.fullName = ui->project_cn_name->text();
    project.lisCode = ui->project_lis_code->text();
    project.enabled = ui->is_used->isChecked();
    project.advanced = m_advanced->advanced();
    for (int row = 0; row < project.results.size(); ++row)
    {
        ProjectResult &result = project.results[row];
        auto text = [this, row](int column)
        { return qobject_cast<QLineEdit *>(ui->resultTable->cellWidget(row, column))->text(); };
        result.mainResult = qobject_cast<CusCheckBox *>(ui->resultTable->cellWidget(row, 0))->isChecked();
        result.name = text(2);
        result.decimals =
            qobject_cast<QComboBox *>(ui->resultTable->cellWidget(row, 3))->currentText().toInt();
        result.testLow = text(4).toDouble();
        result.testHigh = text(5).toDouble();
        result.normalLow = text(6).toDouble();
        result.normalHigh = text(7).toDouble();
        result.lisCode = text(8);
        if (auto *unit = qobject_cast<QComboBox *>(ui->resultTable->cellWidget(row, 9)))
        {
            result.unit = unit->currentText();
        }
    }
    return project;
}
void ProjectPage::markDirty()
{
    if (!m_populating && !m_busy && ProjectsStore::canEdit(m_session))
    {
        m_dirty = true;
    }
}
void ProjectPage::save()
{
    if (m_busy || !ProjectsStore::canEdit(m_session) || m_selectedId <= 0)
    {
        return;
    }
    // Validators can leave intermediate empty input. Reject before conversion
    // so empty/nonfinite values never silently become zero in the stored record.
    for (QLineEdit *edit : ui->resultTable->findChildren<QLineEdit *>())
    {
        if (edit->validator() && !edit->hasAcceptableInput())
        {
            report(tr("结果数值输入无效或为空。"));
            QMessageBox::warning(this, tr("项目设置"), property("lastStatus").toString());
            return;
        }
    }
    const ProjectRecord project = editedProject();
    const QString message = ProjectsStore::validate(project, m_units);
    if (!message.isEmpty())
    {
        report(message);
        QMessageBox::warning(this, tr("项目设置"), message);
        return;
    }
    setBusy(true);
    ++m_request;
    QMetaObject::invokeMethod(m_store, "save", Qt::QueuedConnection, Q_ARG(qint64, m_request),
                              Q_ARG(ProjectRecord, project));
}
void ProjectPage::copy()
{
    if (m_busy || !ProjectsStore::canEdit(m_session) || !prepareToLeave())
    {
        return;
    }
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("复制项目"), tr("项目名称："), QLineEdit::Normal,
                              ui->project_en_name->text() + QStringLiteral("_copy"), &accepted);
    if (!accepted)
    {
        return;
    }
    setBusy(true);
    ++m_request;
    QMetaObject::invokeMethod(m_store, "copy", Qt::QueuedConnection, Q_ARG(qint64, m_request),
                              Q_ARG(qint64, m_selectedId), Q_ARG(QString, name));
}
void ProjectPage::remove()
{
    if (m_busy || !ProjectsStore::canEdit(m_session) || !prepareToLeave())
    {
        return;
    }
    if (QMessageBox::question(this, tr("删除项目"), tr("是否删除当前项目？已被试剂/样本引用的项目不能删除。"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }
    setBusy(true);
    ++m_request;
    QMetaObject::invokeMethod(m_store, "remove", Qt::QueuedConnection, Q_ARG(qint64, m_request),
                              Q_ARG(qint64, m_selectedId));
}
bool ProjectPage::prepareToLeave()
{
    if (m_formatDialog)
    {
        m_formatDialog->reject();
        if (m_formatDialog && m_formatDialog->isVisible()) return false;
    }
    if (m_busy)
    {
        report(tr("项目操作尚未完成，请稍后。"));
        return false;
    }
    if (!m_dirty)
    {
        return true;
    }
    QMessageBox dialog(QMessageBox::Question, tr("项目设置"), tr("当前项目有未保存修改，是否放弃？"),
                       QMessageBox::Discard | QMessageBox::Cancel, this);
    dialog.setObjectName(QStringLiteral("projectDiscardDialog"));
    dialog.setDefaultButton(QMessageBox::Cancel);
    if (dialog.exec() != QMessageBox::Discard)
    {
        return false;
    }
    showProject(m_selectedId);
    return true;
}
void ProjectPage::closeEvent(QCloseEvent *event)
{
    if (prepareToLeave())
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}
void ProjectPage::report(const QString &message)
{
    setProperty("lastStatus", message);
    emit statusChanged(message);
}

bool ProjectPage::hasUnsavedChanges() const
{
    return m_dirty || (m_formatDialog && m_formatDialog->hasDraft());
}
bool ProjectPage::bindCurrentDisplayState(HistoryCurrentDisplayState *state)
{
    if (!state)
    {
        report(tr("当前结果显示缓存不存在，未建立绑定。"));
        return false;
    }
    if (state->thread() != thread())
    {
        report(tr("当前显示缓存须在界面线程绑定，未建立跨线程直接访问。"));
        return false;
    }
    // This optional cross-page hook uses the real QObject slot at runtime so
    // the independent project target need not link the history implementation.
    const QMetaObject::Connection connection = QObject::connect(this,
        SIGNAL(currentFormatInvalidated(QString)), state,
        SLOT(invalidateConfiguration(QString)), Qt::UniqueConnection);
    if (!connection)
    {
        report(tr("当前结果显示缓存连接失败；请核对是否重复绑定以及真实槽是否可用。"));
        return false;
    }
    return true;
}
void ProjectPage::openResultFormat()
{
    if (m_formatDialog) { m_formatDialog->raise(); m_formatDialog->activateWindow(); return; }
    if (!m_ready || m_busy || m_selectedId <= 0)
    { report(tr("项目未就绪或正在处理，请稍后再配置结果显示。")); return; }
    if (m_dirty)
    { report(tr("当前项目有基础或高级参数草稿，请先自行保存或放弃，再配置结果显示。")); return; }
    if (m_catalogDatabasePath.isEmpty())
    { report(tr("当前项目资料源路径不可确认，未打开编辑器。")); return; }
    QPointer<ProjectPage> owner(this);
    auto *dialog = new ProjectFormatDialog(m_store, m_selectedId, m_catalogDatabasePath, m_session,
        [owner]() -> qint64 { return owner ? ++owner->m_request : 0; }, this);
    m_formatDialog = dialog; m_formatRefreshAfterClose = false;
    connect(dialog, &ProjectFormatDialog::operationBusyChanged, this, &ProjectPage::setBusy);
    connect(dialog, &ProjectFormatDialog::draftChanged, this, [this]() { emit busyChanged(m_busy); });
    connect(dialog, &ProjectFormatDialog::configurationInvalidated, this, [this](const QString &reason)
    { emit currentFormatInvalidated(reason); report(reason); });
    connect(dialog, &ProjectFormatDialog::committed, this, [this](qint64 id)
    {
        emit currentFormatInvalidated(tr("结果显示配置已实际提交，请重新查询。"));
        m_formatRefreshAfterClose = true;
        report(tr("结果显示配置已保存，其它项目参数未由此操作修改。"));
        emit operationFinished(QStringLiteral("replaceCurrentResultFormat"), true);
        Q_UNUSED(id);
    });
    connect(dialog, &QDialog::finished, this, [this]()
    {
        m_formatDialog.clear(); setBusy(false); emit busyChanged(m_busy);
        const bool refresh = m_formatRefreshAfterClose; m_formatRefreshAfterClose = false;
        if (refresh && m_ready) QTimer::singleShot(0, this, &ProjectPage::reload);
    });
    updateActions(); dialog->open(); dialog->startRead();
}
