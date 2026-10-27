#include "project_advanced.h"
#include "cuscheckbox.h"
#include <QComboBox>
#include <QDoubleValidator>
#include <QIntValidator>
#include <QLineEdit>
#include <QListView>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QVariant>
#include <QWidget>
#include <cmath>

namespace
{
QStringList choices(const QString &field, bool sample)
{
    if (field == QStringLiteral("sample_add_mode"))
        return {QStringLiteral("none"), QStringLiteral("onedilute"),
                QStringLiteral("predilute"), QStringLiteral("twodilute")};
    if (field == QStringLiteral("blender_type"))
    {
        QStringList result = {QStringLiteral("none"), QStringLiteral("slow"),
                              QStringLiteral("fast"), QStringLiteral("strong")};
        if (!sample)
            result.append(QStringLiteral("catcher"));
        return result;
    }
    if (field == QStringLiteral("needle_select"))
        return {QStringLiteral("1"), QStringLiteral("2")};
    if (field.endsWith(QStringLiteral("_clean_frequency")))
        return {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")};
    return {};
}

bool isName(const QString &field)
{
    return field == QStringLiteral("reagent_name") || field == QStringLiteral("before_clean_name");
}

bool validNumber(const QString &text, bool integer)
{
    static const QRegularExpression whole(QStringLiteral("^(?:0|[1-9][0-9]{0,2})$"));
    static const QRegularExpression decimal(QStringLiteral("^(?:0|[1-9][0-9]{0,2})(?:\\.[0-9])?$"));
    bool ok = false;
    const double number = text.toDouble(&ok);
    return text == text.trimmed() && (integer ? whole : decimal).match(text).hasMatch() && ok && std::isfinite(number) &&
           number >= 0 && number <= 999;
}

bool validReferenceName(const QString &text)
{
    if (text.isEmpty() || text != text.trimmed() || text.size() > 64)
        return false;
    for (const QChar character : text)
    {
        if (character.isNull() || character.category() == QChar::Other_Control ||
            character == QLatin1Char('/') || character == QLatin1Char('\\'))
            return false;
    }
    return true;
}

QString validateAction(const ProjectActionFields &row, bool sample, int index)
{
    const QStringList columns = sample ? ProjectAdvancedEditor::sampleColumns()
                                       : ProjectAdvancedEditor::reagentColumns();
    const QString prefix = QStringLiteral("%1第%2行：")
                               .arg(sample ? QStringLiteral("加样设置") : QStringLiteral("试剂设置"))
                               .arg(index + 1);
    for (auto item = row.cbegin(); item != row.cend(); ++item)
    {
        if (!columns.contains(item.key()))
            return prefix + QStringLiteral("不支持字段 %1。").arg(item.key());
    }
    for (const QString &field : columns)
    {
        const QString text = row.value(field);
        if (field == QStringLiteral("sample_rate"))
            continue;
        // This belongs to existing sample selection, not an advanced-action edit.
        if (field == QStringLiteral("sample_option") && text.isEmpty())
            continue;
        if (field == QStringLiteral("sample_option") || field == QStringLiteral("calibration_option"))
        {
            if (text != QStringLiteral("0") && text != QStringLiteral("1"))
                return prefix + QStringLiteral("%1须为0或1。").arg(field);
            continue;
        }
        const QStringList enumValues = choices(field, sample);
        if (!enumValues.isEmpty())
        {
            if (!enumValues.contains(text))
                return prefix + QStringLiteral("%1未选择或无效。").arg(field);
            continue;
        }
        if (isName(field))
        {
            // No local cleaner is required when pre-cleaning is disabled.
            if (field == QStringLiteral("before_clean_name") &&
                row.value(QStringLiteral("before_clean_frequency")) == QStringLiteral("0") &&
                text.isEmpty())
                continue;
            if (!validReferenceName(text))
                return prefix + QStringLiteral("%1名称为空或含非法字符。").arg(field);
            if (field == QStringLiteral("before_clean_name") && text == QStringLiteral("none") &&
                row.value(QStringLiteral("before_clean_frequency")) != QStringLiteral("0"))
                return prefix + QStringLiteral("预清洗次数大于0时须指定清洗试剂。");
            continue;
        }
        if (!validNumber(text, false))
            return prefix + QStringLiteral("%1须为0至999的数值，最多1位小数，不能留空。").arg(field);
    }
    if (sample)
    {
        const QString mode = row.value(QStringLiteral("sample_add_mode"));
        const double sampleUl = row.value(QStringLiteral("sample_amount_ul")).toDouble();
        const double ibsUl = row.value(QStringLiteral("IBS_amount_ul")).toDouble();
        const double mixUl = row.value(QStringLiteral("mix_amount_ul")).toDouble();
        const double secondUl = row.value(QStringLiteral("second_IBS_amount_ul")).toDouble();
        const double actionUl = row.value(QStringLiteral("action_amount_ul")).toDouble();
        if (mode != QStringLiteral("none") && (sampleUl <= 0 || ibsUl <= 0))
            return prefix + QStringLiteral("稀释模式的样本量和缓冲液量1须大于0。");
        if ((mode == QStringLiteral("none") || mode == QStringLiteral("onedilute")) &&
            (mixUl != 0 || secondUl != 0))
            return prefix + QStringLiteral("当前稀释模式的反应量1和缓冲液量2须为0。");
        if (mode == QStringLiteral("predilute") && secondUl != 0)
            return prefix + QStringLiteral("预稀释模式的缓冲液量2须为0。");
        if ((mode == QStringLiteral("predilute") || mode == QStringLiteral("twodilute")) &&
            (mixUl <= 0 || mixUl >= sampleUl + ibsUl))
            return prefix + QStringLiteral("反应量1须大于0且小于样本量与缓冲液量1之和。");
        if (mode == QStringLiteral("twodilute") && secondUl <= 0)
            return prefix + QStringLiteral("二次稀释的缓冲液量2须大于0。");
        double expected = 0;
        if (mode == QStringLiteral("none"))
        {
            if (sampleUl != 0 && ibsUl != 0)
                return prefix + QStringLiteral("无稀释模式只能选样本量或缓冲液量中的一种。");
            expected = sampleUl + ibsUl;
        }
        else if (mode == QStringLiteral("onedilute"))
            expected = sampleUl + ibsUl;
        else if (mode == QStringLiteral("predilute"))
            expected = mixUl;
        else
            expected = mixUl + secondUl;
        if (std::abs(expected - actionUl) > 0.000001)
            return prefix + QStringLiteral("反应量2与当前稀释方式的各体积不一致。");
    }
    return {};
}
}

QStringList ProjectAdvancedEditor::scalarFields()
{
    return {QStringLiteral("min_test_time"), QStringLiteral("max_test_time"),
            QStringLiteral("stopTime"), QStringLiteral("sample_change_clean"),
            QStringLiteral("first_light"), QStringLiteral("second_light")};
}

QStringList ProjectAdvancedEditor::sampleColumns()
{
    return {QStringLiteral("sample_rate"), QStringLiteral("sample_add_mode"),
            QStringLiteral("IBS_amount_ul"), QStringLiteral("sample_amount_ul"),
            QStringLiteral("mix_amount_ul"), QStringLiteral("second_IBS_amount_ul"),
            QStringLiteral("action_amount_ul"), QStringLiteral("blender_type"),
            QStringLiteral("blender_time_sec"), QStringLiteral("calibration_option"),
            QStringLiteral("sample_option"), QStringLiteral("before_clean_frequency"),
            QStringLiteral("before_clean_amount_ul"), QStringLiteral("before_clean_name"),
            QStringLiteral("after_clean_frequency")};
}

QStringList ProjectAdvancedEditor::reagentColumns()
{
    return {QStringLiteral("reagent_name"), QStringLiteral("reagent_warmup_length"),
            QStringLiteral("reagent_amount_ul"), QStringLiteral("needle_select"),
            QStringLiteral("blender_type"), QStringLiteral("blender_time_sec"),
            QStringLiteral("before_clean_frequency"), QStringLiteral("before_clean_amount_ul"),
            QStringLiteral("before_clean_name"), QStringLiteral("after_clean_frequency")};
}

QString ProjectAdvancedEditor::validate(const ProjectAdvanced &value, const QStringList &rateLabels)
{
    if (!value.configured)
    {
        if (!value.fields.isEmpty() || !value.samples.isEmpty() || !value.reagents.isEmpty())
            return QStringLiteral("未配置的测试方案不能携带隐藏参数。");
        return {};
    }
    const QStringList scalarNames = scalarFields();
    for (auto field = value.fields.cbegin(); field != value.fields.cend(); ++field)
    {
        if (!scalarNames.contains(field.key()))
            return QStringLiteral("测试方案含未支持的字段 %1。").arg(field.key());
    }
    for (const QString &field : scalarNames.mid(0, 4))
    {
        if (!validNumber(value.fields.value(field), true))
            return QStringLiteral("%1须为0至999的整数秒，不能留空。").arg(field);
    }
    const int stop = value.fields.value(QStringLiteral("stopTime")).toInt();
    const int maximum = value.fields.value(QStringLiteral("max_test_time")).toInt();
    if (maximum < (stop < 40 ? 40 : stop + 8))
        return QStringLiteral("测试时间至少40秒；最小结束时间达到40秒时还须留出8秒。");
    const QStringList lights = {QStringLiteral("405nm"), QStringLiteral("570nm"),
                                QStringLiteral("660nm"), QStringLiteral("800nm")};
    if (!lights.contains(value.fields.value(QStringLiteral("first_light"))) ||
        !lights.contains(value.fields.value(QStringLiteral("second_light"))) ||
        value.fields.value(QStringLiteral("first_light")) == value.fields.value(QStringLiteral("second_light")))
        return QStringLiteral("主波长和副波长须选择原版候选中的两个不同波长。");
    // A scalar scheme may be saved before any action parameters are known.
    // Absence stays absent; once action rows are entered they must cover all
    // existing labels without guessing missing volumes for another row.
    if (!value.samples.isEmpty() && value.samples.size() != rateLabels.size())
        return QStringLiteral("加样参数须与既有稀释选项逐一对应；请重新读取项目。");
    for (int index = 0; index < value.samples.size(); ++index)
    {
        if (value.samples[index].value(QStringLiteral("sample_rate")) != rateLabels[index])
            return QStringLiteral("加样设置不能改变既有稀释选项名称或顺序。");
        const QString message = validateAction(value.samples[index], true, index);
        if (!message.isEmpty())
            return message;
    }
    if (value.reagents.size() > 64)
        return QStringLiteral("本地试剂动作最多64行；此为离线数据上限，非仪器动作能力。");
    for (int index = 0; index < value.reagents.size(); ++index)
    {
        const QString message = validateAction(value.reagents[index], false, index);
        if (!message.isEmpty())
            return message;
    }
    return {};
}

ProjectAdvancedEditor::ProjectAdvancedEditor(QWidget *root, QObject *parent)
    : QObject(parent), m_root(root),
      m_samples(root->findChild<QTableWidget *>(QStringLiteral("sampleRateTable"))),
      m_reagents(root->findChild<QTableWidget *>(QStringLiteral("reagentAddingTable")))
{
    for (const QString &name : scalarFields())
    {
        if (auto *edit = m_root->findChild<QLineEdit *>(name))
        {
            edit->setValidator(new QIntValidator(0, 999, edit));
            connect(edit, &QLineEdit::textChanged, this, [this]() { changed(); });
        }
        if (auto *combo = m_root->findChild<QComboBox *>(name))
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { changed(); });
    }
    // Existing row labels/defaults are owned by the already migrated sample
    // contract. This slice edits only their action details, without adding rates.
    for (const QString &name : {QStringLiteral("btnAddSampleRate"), QStringLiteral("btnDelSampleRate"),
                                QStringLiteral("cbxDefaultRate"), QStringLiteral("IBS_name"),
                                QStringLiteral("sampleRateDefaultTable")})
    {
        if (auto *widget = m_root->findChild<QWidget *>(name))
        {
            widget->setEnabled(false);
            widget->setToolTip(tr("既有稀释选项/默认值保持原设置，本页仅编辑本地动作参数。"));
        }
    }
    if (auto *diluent = m_root->findChild<QComboBox *>(QStringLiteral("IBS_name")))
        diluent->setCurrentIndex(-1);
    if (auto *button = m_root->findChild<QPushButton *>(QStringLiteral("btnAddReagent")))
        connect(button, &QPushButton::clicked, this,
                [this]()
                {
                    if (m_writable && m_reagents && m_reagents->rowCount() < 64)
                    {
                        populateRow(m_reagents, {}, false);
                        changed();
                    }
                });
    if (auto *button = m_root->findChild<QPushButton *>(QStringLiteral("btnDelReagent")))
        connect(button, &QPushButton::clicked, this,
                [this]()
                {
                    if (m_writable && m_reagents && m_reagents->currentRow() >= 0)
                    {
                        m_reagents->removeRow(m_reagents->currentRow());
                        changed();
                    }
                });
}

void ProjectAdvancedEditor::changed()
{
    if (!m_populating && m_writable)
    {
        refreshDefaultSample();
        emit edited();
    }
}

void ProjectAdvancedEditor::populateRow(QTableWidget *table, const ProjectActionFields &values, bool sample)
{
    if (!table)
        return;
    const QStringList columns = sample ? sampleColumns() : reagentColumns();
    const int row = table->rowCount();
    table->insertRow(row);
    for (int column = 0; column < columns.size(); ++column)
    {
        const QString field = columns[column];
        const QString value = values.value(field);
        QWidget *editor = nullptr;
        if (field == QStringLiteral("calibration_option") || field == QStringLiteral("sample_option"))
        {
            auto *check = new CusCheckBox(table);
            check->setCheckState(value == QStringLiteral("1") ? Qt::Checked : Qt::Unchecked);
            check->setText(tr("选用"));
            check->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
            if (field == QStringLiteral("sample_option"))
            {
                check->setProperty("advancedReadOnly", true);
                check->setEnabled(false);
                check->setToolTip(tr("样本稀释选项沿用已提交的项目设置，本页不能修改。"));
            }
            else
                connect(check, QOverload<bool>::of(&CusCheckBox::signalClicked), this, [this](bool) { changed(); });
            editor = check;
        }
        else if (!choices(field, sample).isEmpty() || isName(field))
        {
            auto *combo = new QComboBox(table);
            combo->setView(new QListView);
            if (isName(field))
            {
                // Only committed local catalog choices. A stale saved name is
                // retained visibly so validation reports it instead of silently
                // selecting another reagent; SQL worker rechecks before commit.
                combo->addItem(QStringLiteral("none"));
                combo->addItems(field == QStringLiteral("reagent_name") ? m_reagentNames : m_cleanerNames);
                if (!value.isEmpty() && value != QStringLiteral("none"))
                {
                    if (combo->findText(value) < 0)
                    {
                        combo->addItem(value);
                        combo->setToolTip(tr("已保存名称不在当前目录时须重新选择，保存时会复核引用。"));
                    }
                }
            }
            else
                combo->addItems(choices(field, sample));
            combo->setCurrentIndex(-1);
            if (!value.isEmpty())
            {
                if (field == QStringLiteral("needle_select"))
                {
                    combo->setItemText(0, tr("左试剂针"));
                    combo->setItemText(1, tr("右试剂针"));
                    combo->setItemData(0, QStringLiteral("1"));
                    combo->setItemData(1, QStringLiteral("2"));
                    combo->setCurrentIndex(value == QStringLiteral("1") ? 0 : value == QStringLiteral("2") ? 1 : -1);
                }
                else
                    combo->setCurrentText(value);
            }
            else if (field == QStringLiteral("needle_select"))
            {
                combo->setItemText(0, tr("左试剂针"));
                combo->setItemText(1, tr("右试剂针"));
                combo->setItemData(0, QStringLiteral("1"));
                combo->setItemData(1, QStringLiteral("2"));
            }
            combo->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
            connect(combo, &QComboBox::currentTextChanged, this, [this](const QString &) { changed(); });
            editor = combo;
        }
        else
        {
            auto *edit = new QLineEdit(value, table);
            edit->setAlignment(Qt::AlignCenter);
            edit->setContextMenuPolicy(Qt::NoContextMenu);
            edit->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 12));
            edit->setStyleSheet(QStringLiteral("border:1px solid #E4E8EB;border-radius:5px;padding:2px 4px;background-color:transparent;color:#444444;"));
            if (field == QStringLiteral("sample_rate") || field == QStringLiteral("sample_option"))
                edit->setReadOnly(true);
            else
            {
                auto *validator = new QDoubleValidator(0, 999, 1, edit);
                validator->setNotation(QDoubleValidator::StandardNotation);
                edit->setValidator(validator);
            }
            connect(edit, &QLineEdit::textChanged, this, [this]() { changed(); });
            editor = edit;
        }
        editor->setProperty("advancedField", field);
        table->setCellWidget(row, column, editor);
    }
    table->setRowHeight(row, 41);
}

ProjectActionFields ProjectAdvancedEditor::rowFields(QTableWidget *table, int row, bool sample) const
{
    ProjectActionFields values;
    const QStringList columns = sample ? sampleColumns() : reagentColumns();
    for (int column = 0; column < columns.size(); ++column)
    {
        auto *widget = table->cellWidget(row, column);
        if (auto *edit = qobject_cast<QLineEdit *>(widget))
            values.insert(columns[column], edit->text());
        else if (auto *combo = qobject_cast<QComboBox *>(widget))
            values.insert(columns[column], columns[column] == QStringLiteral("needle_select")
                                               ? combo->currentData().toString() : combo->currentText());
        else if (auto *check = qobject_cast<CusCheckBox *>(widget))
            values.insert(columns[column], check->isChecked() ? QStringLiteral("1") : QStringLiteral("0"));
    }
    return values;
}

void ProjectAdvancedEditor::showAdvanced(const ProjectAdvanced &value, const QStringList &rateLabels)
{
    m_populating = true;
    m_configured = value.configured;
    m_rateLabels = rateLabels;
    for (const QString &name : scalarFields())
    {
        if (auto *edit = m_root->findChild<QLineEdit *>(name))
            edit->setText(value.fields.value(name));
        if (auto *combo = m_root->findChild<QComboBox *>(name))
            combo->setCurrentIndex(combo->findText(value.fields.value(name)));
    }
    if (m_samples)
    {
        m_samples->setRowCount(0);
        for (const QString &rate : rateLabels)
        {
            ProjectActionFields row;
            for (const ProjectActionFields &stored : value.samples)
            {
                if (stored.value(QStringLiteral("sample_rate")) == rate)
                    row = stored;
            }
            row.insert(QStringLiteral("sample_rate"), rate);
            populateRow(m_samples, row, true);
        }
    }
    if (m_reagents)
    {
        m_reagents->setRowCount(0);
        for (const ProjectActionFields &row : value.reagents)
            populateRow(m_reagents, row, false);
    }
    refreshDefaultSample();
    m_populating = false;
    setWritable(m_writable);
}

ProjectAdvanced ProjectAdvancedEditor::advanced() const
{
    ProjectAdvanced value;
    value.configured = m_configured;
    for (const QString &name : scalarFields())
    {
        QString text;
        if (auto *edit = m_root->findChild<QLineEdit *>(name))
            text = edit->text();
        if (auto *combo = m_root->findChild<QComboBox *>(name))
            text = combo->currentText();
        value.fields.insert(name, text);
        if (!text.isEmpty())
            value.configured = true;
    }
    for (QTableWidget *table : {m_samples, m_reagents})
    {
        if (!table)
            continue;
        const bool sample = table == m_samples;
        for (int row = 0; row < table->rowCount(); ++row)
        {
            const ProjectActionFields values = rowFields(table, row, sample);
            bool rowConfigured = !sample;
            for (auto field = values.cbegin(); field != values.cend(); ++field)
            {
                if (field.key() != QStringLiteral("sample_rate") && field.key() != QStringLiteral("sample_option") &&
                    !(field.key() == QStringLiteral("calibration_option") && field.value() == QStringLiteral("0")) &&
                    !field.value().isEmpty())
                    rowConfigured = true;
            }
            if (rowConfigured)
            {
                if (sample)
                    value.samples.append(values);
                else
                    value.reagents.append(values);
                value.configured = true;
            }
        }
    }
    if (!value.configured)
        return ProjectAdvanced();
    return value;
}

void ProjectAdvancedEditor::setWritable(bool writable)
{
    m_writable = writable;
    for (const QString &name : scalarFields())
    {
        if (auto *edit = m_root->findChild<QLineEdit *>(name))
            edit->setReadOnly(!writable);
        if (auto *combo = m_root->findChild<QComboBox *>(name))
            combo->setEnabled(writable);
    }
    for (QTableWidget *table : {m_samples, m_reagents})
    {
        if (table)
        {
            table->setEnabled(writable);
            for (QWidget *editor : table->findChildren<QWidget *>())
            {
                if (editor->property("advancedReadOnly").toBool())
                    editor->setEnabled(false);
            }
        }
    }
    for (const QString &name : {QStringLiteral("btnAddReagent"), QStringLiteral("btnDelReagent")})
    {
        if (auto *button = m_root->findChild<QPushButton *>(name))
            button->setEnabled(writable);
    }
}

void ProjectAdvancedEditor::setReagentChoices(const QStringList &reagents, const QStringList &cleaners)
{
    m_reagentNames = reagents;
    m_cleanerNames = cleaners;
    const bool wasPopulating = m_populating;
    m_populating = true;
    for (QTableWidget *table : {m_samples, m_reagents})
    {
        if (!table)
            continue;
        for (QComboBox *combo : table->findChildren<QComboBox *>())
        {
            const QString field = combo->property("advancedField").toString();
            if (!isName(field))
                continue;
            const QString saved = combo->currentText();
            combo->clear();
            combo->addItem(QStringLiteral("none"));
            combo->addItems(field == QStringLiteral("reagent_name") ? m_reagentNames : m_cleanerNames);
            if (!saved.isEmpty() && combo->findText(saved) < 0)
                combo->addItem(saved);
            combo->setCurrentIndex(combo->findText(saved));
        }
    }
    m_populating = wasPopulating;
}

void ProjectAdvancedEditor::setSampleSelections(const QMap<QString, bool> &options,
                                               const QString &defaultLabel)
{
    const bool wasPopulating = m_populating;
    m_populating = true;
    m_defaultLabel = defaultLabel;
    if (m_samples)
    {
        for (int row = 0; row < m_samples->rowCount(); ++row)
        {
            auto *label = qobject_cast<QLineEdit *>(m_samples->cellWidget(row, 0));
            auto *check = qobject_cast<CusCheckBox *>(m_samples->cellWidget(row, 10));
            if (label && check)
            {
                check->setCheckState(options.value(label->text(), false) ? Qt::Checked : Qt::Unchecked);
                check->setEnabled(false);
            }
        }
    }
    if (auto *combo = m_root->findChild<QComboBox *>(QStringLiteral("cbxDefaultRate")))
    {
        combo->clear();
        for (const QString &label : m_rateLabels)
        {
            if (options.value(label, false))
                combo->addItem(label);
        }
        combo->setCurrentIndex(combo->findText(defaultLabel));
        combo->setEnabled(false);
    }
    refreshDefaultSample();
    m_populating = wasPopulating;
}

void ProjectAdvancedEditor::refreshDefaultSample()
{
    auto *mirror = m_root->findChild<QTableWidget *>(QStringLiteral("sampleRateDefaultTable"));
    if (!mirror)
        return;
    mirror->setRowCount(0);
    if (!m_samples || m_defaultLabel.isEmpty())
        return;
    int selectedRow = -1;
    for (int row = 0; row < m_samples->rowCount(); ++row)
    {
        auto *label = qobject_cast<QLineEdit *>(m_samples->cellWidget(row, 0));
        if (label && label->text() == m_defaultLabel)
            selectedRow = row;
    }
    if (selectedRow < 0)
        return;
    mirror->setRowCount(1);
    for (int column = 0; column < 10; ++column)
    {
        const int sourceColumn = column > 8 ? column + 2 : column;
        QWidget *source = m_samples->cellWidget(selectedRow, sourceColumn);
        if (auto *edit = qobject_cast<QLineEdit *>(source))
        {
            auto *copy = new QLineEdit(edit->text(), mirror);
            copy->setReadOnly(true);
            copy->setAlignment(Qt::AlignCenter);
            copy->setFont(edit->font());
            copy->setStyleSheet(edit->styleSheet());
            mirror->setCellWidget(0, column, copy);
        }
        else if (auto *combo = qobject_cast<QComboBox *>(source))
        {
            auto *copy = new QComboBox(mirror);
            copy->setView(new QListView);
            for (int index = 0; index < combo->count(); ++index)
                copy->addItem(combo->itemText(index), combo->itemData(index));
            copy->setCurrentIndex(combo->currentIndex());
            copy->setFont(combo->font());
            mirror->setCellWidget(0, column, copy);
        }
    }
    mirror->setRowHeight(0, 41);
    mirror->setEnabled(false);
}
