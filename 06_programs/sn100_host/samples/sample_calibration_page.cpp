#include "sample_page.h"
#include "original_calibration_preset.h"
#include "ui_frmsamplesettingmaterial.h"
#include "ui_maskdlgcalibratorsetting.h"
#include <QCloseEvent>
#include <QDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QTableWidget>
#include <cmath>
#include <memory>

namespace
{
class CalibrationDialog : public QDialog
{
public:
    explicit CalibrationDialog(QWidget *parent) : QDialog(parent) {}
    bool saving = false;
    void reject() override { if (!saving) QDialog::reject(); }
protected:
    void closeEvent(QCloseEvent *event) override
    { if (saving) event->ignore(); else QDialog::closeEvent(event); }
};
double unitRatio(const ProjectRecord &project, const ProjectUnits &units)
{
    QString name;
    for (const auto &result : project.results) if (result.type == QStringLiteral("Calibration")) name = result.unit;
    for (const auto &unit : units) if (unit.name == name) return unit.ratio;
    return 1;
}
}

void SamplePage::refreshCalibrationButtons()
{
    auto *list = setting_->calProjButtonList;
    list->clear();
    if (!originalProfile_) return;
    for (const auto &project : projects_)
    {
        if (!OriginalCalibrationPreset::available(project)) continue;
        auto *button = new QPushButton(project.name, list);
        button->setObjectName(QStringLiteral("calibrationProject_") + QString::number(project.id));
        button->setFixedSize(80, 84);
        button->setFocusPolicy(Qt::NoFocus);
        button->setStyleSheet(QStringLiteral("QPushButton{background:#FFFFFF;color:#444444;border-radius:5px;border:1px solid #DDDDDD;}"));
        auto *item = new QListWidgetItem(list);
        item->setSizeHint(QSize(84, 84));
        list->setItemWidget(item, button);
        connect(button, &QPushButton::clicked, this, [this, id = project.id]() { calibration(id); });
    }
}

void SamplePage::calibration(qint64 projectId)
{
    if (dialog_ || !originalProfile_ || !canEditHole(row_)) return;
    if (!editing_.holes[row_].tasks.isEmpty())
    { report(tr("此孔位已设置任务，请先删除后再进行校准。")); return; }
    const auto *selected = project(projectId);
    if (!selected) { report(tr("原校准项目已不存在，请重新读取。")); return; }
    const ProjectRecord source = *selected;
    SampleCalibrationPreset initial;
    QString message;
    if (!OriginalCalibrationPreset::defaults(source, row_ + 1, editing_.holes.size(), &initial, &message))
    { report(message); return; }
    auto *dialog = new CalibrationDialog(this);
    dialog->setObjectName(QStringLiteral("sampleCalibrationDialog"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog_ = dialog;
    const qint64 rackId = editing_.id;
    auto form = std::make_shared<Ui::MaskDlgCalibratorSetting>();
    form->setupUi(dialog);
    form->mask->setupForm();
    form->lblProjectName->setText(source.name);
    form->ldtCalBatch->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^[0-9A-Za-z]{16}$")), dialog));
    // 原SystemCommon::getExpStr(0,999,3)实际生成三位整数/三位小数正则，
    // 不支持指数写法；不能改用默认允许科学记数的QDoubleValidator。
    form->ldtCalCon->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^(?:0|[1-9]\\d{0,2})(?:\\.\\d{1,3})?$")), dialog));
    form->cbxBatchGroup->setView(new QListView);
    form->cbxCalSampleHole->setView(new QListView);
    form->cbxBatchGroup->clear();
    const bool related = source.originalBaseValues.value(QStringLiteral("is_related")).toInt() > 0;
    if (!related) form->cbxBatchGroup->addItem(QStringLiteral("Default"));
    else if (reagents_.originalProfile && reagents_.originalRoot == source.originalRoot)
        for (const auto &batch : reagents_.batches)
            if (batch.projectId == projectId && batch.batchGroup != QStringLiteral("PTFbg")) form->cbxBatchGroup->addItem(batch.batchGroup);
    form->cbxBatchGroup->setEnabled(related);
    form->cbxCalSampleHole->clear();
    for (int number = 1; number <= editing_.holes.size(); ++number) form->cbxCalSampleHole->addItem(QString::number(number));
    form->cbxCalSampleHole->setCurrentText(QString::number(row_ + 1));
    for (const auto &result : source.results)
        if (result.type == QStringLiteral("Calibration")) form->unit_label->setText(result.unit);
    form->lblCalSampleHole->setText(initial.autoDilution ? tr("校准孔位") : tr("基准孔位"));
    form->lblCalCon->setVisible(initial.autoDilution);
    form->ldtCalCon->setVisible(initial.autoDilution);
    form->unit_label->setVisible(initial.autoDilution);
    QRect tableRect = form->tableSetData->geometry();
    tableRect.setY(initial.autoDilution ? 184 : 114);
    tableRect.setHeight(initial.autoDilution ? 471 : 541);
    form->tableSetData->setGeometry(tableRect);
    QRect keyboardRect = form->wgtNumberInput->geometry();
    keyboardRect.setY(184); keyboardRect.setHeight(391); form->wgtNumberInput->setGeometry(keyboardRect);
    form->tableSetData->setColumnCount(3);
    form->tableSetData->setHorizontalHeaderLabels({tr("索引"), initial.autoDilution ? tr("比例") : tr("孔位号"), tr("浓度") + form->unit_label->text()});
    form->tableSetData->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    form->tableSetData->setEditTriggers(QAbstractItemView::NoEditTriggers);
    form->tableSetData->setSelectionMode(QAbstractItemView::NoSelection);
    form->tableSetData->setColumnHidden(0, true);
    form->tableSetData->setRowCount(initial.pointCount);
    const double multiplier = unitRatio(source, originalUnits_);
    const auto saved = source.originalBaseValues.value(QStringLiteral("last_cal_para")).split('|', Qt::SkipEmptyParts);
    for (int point = 0; point < initial.pointCount; ++point)
    {
        form->tableSetData->setRowHeight(point, 50);
        form->tableSetData->setItem(point, 0, new QTableWidgetItem(QString::number(point + 1)));
        form->tableSetData->setItem(point, 1, new QTableWidgetItem(initial.autoDilution ? initial.rates[point] : QString::number(initial.holes[point])));
        if (initial.autoDilution) form->tableSetData->setItem(point, 2, new QTableWidgetItem);
        else
        {
            auto *input = new QLineEdit(form->tableSetData);
            input->setAlignment(Qt::AlignCenter);
            input->setStyleSheet(QStringLiteral("border:none;background:transparent;color:#444444;font:18px 'Noto Sans CJK SC';"));
            // 原手动浓度框无数值范围校验；保存时明确拒绝不可识别值，避免toDouble静默变0。
            if (point < saved.size())
            {
                bool ok = false; const double number = saved[point].toDouble(&ok);
                if (ok && std::isfinite(number)) input->setText(QString::number(number * multiplier));
            }
            form->tableSetData->setCellWidget(point, 2, input);
            connect(input, &QLineEdit::textChanged, dialog, [this]() { dialogDirty_ = true; });
        }
    }
    connect(form->ldtCalCon, &QLineEdit::textChanged, dialog, [this, form, source](const QString &text)
    {
        int decimals = 1;
        for (const auto &result : source.results) if (result.type == QStringLiteral("Calibration")) decimals = result.decimals;
        for (int point = 0; point < form->tableSetData->rowCount(); ++point)
        {
            const auto rate = form->tableSetData->item(point, 1)->text().split('/');
            if (rate.size() != 2 || rate[1].toDouble() == 0) continue;
            form->tableSetData->item(point, 2)->setText(QString::number(text.toDouble() * rate[0].toDouble() / rate[1].toDouble(), 'f', decimals));
        }
        dialogDirty_ = true;
    });
    if (initial.autoDilution && !saved.isEmpty())
    {
        bool ok = false; const double number = saved.first().toDouble(&ok);
        if (ok && std::isfinite(number)) form->ldtCalCon->setText(QString::number(number * multiplier));
    }
    connect(form->cbxCalSampleHole, &QComboBox::currentTextChanged, dialog, [this, form, initial](const QString &text)
    {
        if (!initial.autoDilution)
            for (int point = 0; point < initial.pointCount; ++point)
                form->tableSetData->item(point, 1)->setText(QString::number(text.toInt() + point));
        dialogDirty_ = true;
    });
    connect(form->ldtCalBatch, &QLineEdit::textChanged, dialog, [this]() { dialogDirty_ = true; });
    connect(form->cbxBatchGroup, &QComboBox::currentTextChanged, dialog, [this]() { dialogDirty_ = true; });
    connect(form->btnSave, &QPushButton::clicked, dialog, [this, form, dialog, source, rackId]()
    {
        if (dialog->saving || editing_.id != rackId || !SampleStore::canEdit(session_)) return;
        SampleCalibrationPreset preset;
        QString error;
        if (!OriginalCalibrationPreset::defaults(source, form->cbxCalSampleHole->currentText().toInt(), editing_.holes.size(), &preset, &error))
        { report(error); return; }
        preset.calibratorBatch = form->ldtCalBatch->text();
        preset.reagentBatchGroup = form->cbxBatchGroup->currentText();
        QStringList displayValues;
        if (preset.autoDilution)
        {
            if (form->ldtCalCon->text().isEmpty()) { report(tr("校准品浓度为空!")); return; }
            displayValues.append(form->ldtCalCon->text());
        }
        else for (int point = 0; point < preset.pointCount; ++point)
            displayValues.append(qobject_cast<QLineEdit *>(form->tableSetData->cellWidget(point, 2))->text());
        if (!OriginalCalibrationPreset::setConcentrations(source, originalUnits_, displayValues, &preset, &error))
        { report(error); return; }
        SampleRack candidate = editing_;
        if (!OriginalCalibrationPreset::apply(source, preset, &candidate, &error)) { report(error); return; }
        QString lastValues = preset.concentrations.join('|');
        if (preset.autoDilution)
        {
            QString unitName;
            for (const auto &result : source.results) if (result.type == QStringLiteral("Calibration")) unitName = result.unit;
            int originalMultiplier = 1;
            for (const auto &unit : originalUnits_) if (unit.name == unitName) originalMultiplier = unit.originalRatio.toInt();
            if (originalMultiplier == 0) { report(tr("原自动校准上次浓度保存使用整数倍率，该单位倍率不能无损读取；请交总窗口核对。")); return; }
            lastValues = QString::number(displayValues.first().toDouble() / originalMultiplier);
        }
        const qint64 request = ++calibrationParametersRequest_;
        dialog->saving = true; form->btnSave->setEnabled(false); form->btnCancel->setEnabled(false); form->btnClose->setEnabled(false);
        auto completion = std::make_shared<QMetaObject::Connection>();
        *completion = connect(store_, &SampleStore::originalCalibrationParametersSaved, dialog,
                [this, dialog, form, source, preset, rackId, request, completion](qint64 returned, bool ok, const QString &message)
        {
            if (returned != request) return;
            disconnect(*completion);
            dialog->saving = false; form->btnSave->setEnabled(true); form->btnCancel->setEnabled(true); form->btnClose->setEnabled(true);
            if (!ok) { report(message); return; }
            QString error;
            if (editing_.id != rackId || !SampleStore::canEdit(session_) || !OriginalCalibrationPreset::apply(source, preset, &editing_, &error))
            { report(tr("原上次浓度参数已保存，但当前架或权限变化，未覆盖样本草稿：") + error); return; }
            dirty_ = true; dialogDirty_ = false;
            refreshTable(); refreshProjectButtons();
            report(tr("上次浓度参数已本地保存，设备参数确认另行核对；请保存校准样本架，样本任务尚未下发，曲线尚未计算。"));
            dialog->accept();
        });
        QMetaObject::invokeMethod(store_, "saveOriginalCalibrationParameters", Qt::QueuedConnection,
            Q_ARG(qint64, request), Q_ARG(qint64, source.id), Q_ARG(QString, source.originalFingerprint), Q_ARG(QString, lastValues));
    });
    connect(form->btnCancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(form->btnClose, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(form->mask, &CusDialogMask::closeFinished, dialog, &QDialog::reject);
    // 原入口只允许空孔打开设置；此时没有已有校准任务可删，沿原删除动作关闭。
    connect(form->btnDel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(dialog, &QDialog::finished, this, [this]()
    { dialog_.clear(); dialogDirty_ = false; refreshProjects(); refreshCalibrationButtons(); });
    dialogDirty_ = false;
    dialog->open(); form->mask->showAnimation(); actions();
}
