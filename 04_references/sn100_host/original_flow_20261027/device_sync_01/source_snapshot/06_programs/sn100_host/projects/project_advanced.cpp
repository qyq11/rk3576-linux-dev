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
#include <QSignalBlocker>
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

bool validReferenceName(const QString &text, bool originalProfile = false)
{
    if (text.isEmpty() || text != text.trimmed() || text.size() > 64)
        return false;
    for (const QChar character : text)
    {
        if (character.isNull() || character.category() == QChar::Other_Control ||
            (!originalProfile && character == QLatin1Char('/')) || character == QLatin1Char('\\'))
            return false;
    }
    return true;
}

QString actionFieldTitle(const QString &field)
{
    static const QMap<QString, QString> titles = {
        {QStringLiteral("sample_add_mode"), QStringLiteral("加样方式")},
        {QStringLiteral("IBS_amount_ul"), QStringLiteral("缓冲液量1（微升）")},
        {QStringLiteral("sample_amount_ul"), QStringLiteral("样本量（微升）")},
        {QStringLiteral("mix_amount_ul"), QStringLiteral("反应量1（微升）")},
        {QStringLiteral("second_IBS_amount_ul"), QStringLiteral("缓冲液量2（微升）")},
        {QStringLiteral("action_amount_ul"), QStringLiteral("反应量2（微升）")},
        {QStringLiteral("blender_type"), QStringLiteral("混匀方式")},
        {QStringLiteral("blender_time_sec"), QStringLiteral("混匀时间（秒）")},
        {QStringLiteral("sample_option"), QStringLiteral("样本选用状态")},
        {QStringLiteral("calibration_option"), QStringLiteral("校准选用状态")},
        {QStringLiteral("before_clean_frequency"), QStringLiteral("预清洗次数")},
        {QStringLiteral("before_clean_amount_ul"), QStringLiteral("清洗液量（微升）")},
        {QStringLiteral("before_clean_name"), QStringLiteral("清洗液")},
        {QStringLiteral("after_clean_frequency"), QStringLiteral("后清洗次数")},
        {QStringLiteral("reagent_name"), QStringLiteral("试剂")},
        {QStringLiteral("reagent_warmup_length"), QStringLiteral("经过时间（秒）")},
        {QStringLiteral("reagent_amount_ul"), QStringLiteral("试剂量（微升）")},
        {QStringLiteral("needle_select"), QStringLiteral("试剂针")}};
    return titles.value(field, field);
}

QString validateAction(const ProjectActionFields &row, bool sample, int index, bool originalProfile)
{
    const QStringList columns = sample ? ProjectAdvancedEditor::sampleColumns()
                                       : ProjectAdvancedEditor::reagentColumns();
    const QString prefix = QStringLiteral("%1第%2行：")
                               .arg(sample ? QStringLiteral("加样设置") : QStringLiteral("试剂设置"))
                               .arg(index + 1);
    for (auto item = row.cbegin(); item != row.cend(); ++item)
    {
        if (originalProfile && item.key() == QStringLiteral("_original_rowid"))
            continue; // 仅用于保留原行身份，不是可编辑参数或新增数据库列。
        if (!columns.contains(item.key()))
            return prefix + QStringLiteral("不支持字段 %1。").arg(item.key());
    }
    for (const QString &field : columns)
    {
        const QString text = row.value(field);
        if (field == QStringLiteral("sample_rate"))
            continue;
        // 旧资料可能没有重复保存选用状态；最终状态由项目的方案列表校验。
        if (field == QStringLiteral("sample_option") && text.isEmpty())
            continue;
        if (field == QStringLiteral("sample_option") || field == QStringLiteral("calibration_option"))
        {
            if (text != QStringLiteral("0") && text != QStringLiteral("1"))
                return prefix + QStringLiteral("%1须为0或1。").arg(actionFieldTitle(field));
            continue;
        }
        const QStringList enumValues = choices(field, sample);
        if (!enumValues.isEmpty())
        {
            if (!enumValues.contains(text))
                return prefix + QStringLiteral("%1未选择或无效。").arg(actionFieldTitle(field));
            continue;
        }
        if (isName(field))
        {
            // 不做预清洗时允许旧资料未指定清洗液。
            if (field == QStringLiteral("before_clean_name") &&
                row.value(QStringLiteral("before_clean_frequency")) == QStringLiteral("0") &&
                text.isEmpty())
                continue;
            if (!validReferenceName(text, originalProfile))
                return prefix + QStringLiteral("%1名称为空或含非法字符。").arg(actionFieldTitle(field));
            if (field == QStringLiteral("before_clean_name") && text == QStringLiteral("none") &&
                row.value(QStringLiteral("before_clean_frequency")) != QStringLiteral("0"))
                return prefix + QStringLiteral("预清洗次数大于0时须指定清洗试剂。");
            continue;
        }
        if (!validNumber(text, false))
            return prefix + QStringLiteral("%1须为0至999的数值，最多1位小数，不能留空。").arg(actionFieldTitle(field));
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
            QStringLiteral("first_light"), QStringLiteral("second_light"), QStringLiteral("IBS_name")};
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

QString ProjectAdvancedEditor::validate(const ProjectAdvanced &value, const QStringList &rateLabels,
                                        bool originalProfile)
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
    if (value.fields.contains(QStringLiteral("IBS_name")) &&
        !validReferenceName(value.fields.value(QStringLiteral("IBS_name")), originalProfile))
        return QStringLiteral("稀释液名称为空或含非法字符，请重新选择。");
    QSet<QString> seenLabels;
    if (rateLabels.size() > 64)
        return QStringLiteral("稀释方案最多64行；这是配置存储上限，不代表仪器动作能力。");
    for (const QString &label : rateLabels)
    {
        if (!validReferenceName(label, originalProfile))
            return originalProfile ? QStringLiteral("稀释方案名称须为1至64字，不能含首尾空白、控制字符或反斜杠；1/16等原库标签可以保留。")
                                   : QStringLiteral("稀释方案名称须为1至64字，不能含首尾空白、控制或路径字符。");
        if (seenLabels.contains(label))
            return QStringLiteral("稀释方案名称重复：%1。请修改名称。").arg(label);
        seenLabels.insert(label);
    }
    // 旧资料允许尚无动作参数；一旦填写，须完整覆盖方案列表，不能漏行。
    if (!value.samples.isEmpty() && value.samples.size() != rateLabels.size())
        return QStringLiteral("加样参数须与稀释方案逐行对应，不能遗漏方案。");
    for (int index = 0; index < value.samples.size(); ++index)
    {
        if (value.samples[index].value(QStringLiteral("sample_rate")) != rateLabels[index])
            return QStringLiteral("加样设置与稀释方案的名称或顺序不一致。");
        const QString message = validateAction(value.samples[index], true, index, originalProfile);
        if (!message.isEmpty())
            return message;
        const ProjectActionFields &row = value.samples[index];
        // 旧动作可能完全没有此字段，允许读取后补选；保存入口须拒绝缺稀释液的动作。
        if (value.fields.contains(QStringLiteral("IBS_name")) &&
            (row.value(QStringLiteral("IBS_amount_ul")).toDouble() > 0 ||
             row.value(QStringLiteral("second_IBS_amount_ul")).toDouble() > 0) &&
            (value.fields.value(QStringLiteral("IBS_name")).isEmpty() ||
             value.fields.value(QStringLiteral("IBS_name")) == QStringLiteral("none")))
            return QStringLiteral("第%1个稀释方案使用缓冲液，请先选择稀释液。").arg(index + 1);
    }
    if (value.reagents.size() > 64)
        return QStringLiteral("本地试剂动作最多64行；此为离线数据上限，非仪器动作能力。");
    for (int index = 0; index < value.reagents.size(); ++index)
    {
        const QString message = validateAction(value.reagents[index], false, index, originalProfile);
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
                    [this, name](int)
                    {
                        if (!m_populating && m_writable && name == QStringLiteral("IBS_name"))
                            m_diluentSpecified = true;
                        changed();
                    });
    }
    if (auto *button = m_root->findChild<QPushButton *>(QStringLiteral("btnAddSampleRate")))
    {
        connect(button, &QPushButton::clicked, this, [this]()
        {
            if (!m_writable || !m_samples || m_samples->rowCount() >= 64)
                return;
            populateRow(m_samples, {{QStringLiteral("sample_add_mode"), QStringLiteral("none")},
                                    {QStringLiteral("blender_type"), QStringLiteral("none")},
                                    {QStringLiteral("before_clean_frequency"), QStringLiteral("0")},
                                    {QStringLiteral("after_clean_frequency"), QStringLiteral("0")}}, true);
            m_samples->cellWidget(m_samples->rowCount() - 1, 0)->setProperty("sampleActionDraft", true);
            m_samples->selectRow(m_samples->rowCount() - 1);
            refreshDefaultChoices();
            changed();
        });
    }
    if (auto *button = m_root->findChild<QPushButton *>(QStringLiteral("btnDelSampleRate")))
        connect(button, &QPushButton::clicked, this, [this]()
        {
            if (!m_writable || !m_samples || m_samples->currentRow() < 0)
                return;
            const QString label = rowFields(m_samples, m_samples->currentRow(), true)
                                      .value(QStringLiteral("sample_rate"));
            if (label == m_defaultLabel)
                m_defaultLabel.clear();
            m_samples->removeRow(m_samples->currentRow());
            refreshDefaultChoices();
            changed();
        });
    if (auto *combo = m_root->findChild<QComboBox *>(QStringLiteral("cbxDefaultRate")))
        connect(combo, &QComboBox::currentTextChanged, this, [this](const QString &label)
        {
            if (m_populating || !m_writable)
                return;
            m_defaultLabel = label;
            refreshDefaultSample();
            changed();
        });
    if (auto *diluent = m_root->findChild<QComboBox *>(QStringLiteral("IBS_name")))
    {
        diluent->clear();
        diluent->addItem(QStringLiteral("none"));
    }
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
            connect(check, QOverload<bool>::of(&CusCheckBox::signalClicked), this,
                    [this, field](bool)
                    {
                        if (field == QStringLiteral("sample_option"))
                            refreshDefaultChoices();
                        changed();
                    });
            editor = check;
        }
        else if (!choices(field, sample).isEmpty() || isName(field))
        {
            auto *combo = new QComboBox(table);
            combo->setView(new QListView);
            if (isName(field))
            {
                // 仅使用已保存目录。过期名称仍显示，保存时明确报错，不能换成其他试剂。
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
                {
                    if (combo->findText(value) < 0)
                        combo->addItem(value);
                    combo->setCurrentText(value);
                }
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
            if (field == QStringLiteral("sample_rate"))
            {
                edit->setProperty("previousRateName", value);
                connect(edit, &QLineEdit::textChanged, this, [this, edit](const QString &label)
                {
                    const QString previous = edit->property("previousRateName").toString();
                    if (!previous.isEmpty() && m_defaultLabel == previous)
                        m_defaultLabel = label;
                    edit->setProperty("previousRateName", label);
                    refreshDefaultChoices();
                });
            }
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
        if (m_originalProfile && column == 0)
            editor->setProperty("originalRowId", values.value(QStringLiteral("_original_rowid")));
        table->setCellWidget(row, column, editor);
    }
    if (sample)
    {
        auto *blender = qobject_cast<QComboBox *>(table->cellWidget(row, 7));
        auto *blenderTime = qobject_cast<QLineEdit *>(table->cellWidget(row, 8));
        connect(blender, &QComboBox::currentTextChanged, blenderTime, [blenderTime](const QString &type)
        {
            // 原版changeItem()的混匀预置时间，仍允许使用者继续编辑。
            static const QMap<QString, QString> preset = {
                {QStringLiteral("none"), QStringLiteral("0")},
                {QStringLiteral("slow"), QStringLiteral("0.7")},
                {QStringLiteral("fast"), QStringLiteral("1.3")},
                {QStringLiteral("strong"), QStringLiteral("3.3")}};
            if (preset.contains(type))
                blenderTime->setText(preset.value(type));
        });
        // 原版在结束体积编辑时计算反应量2。非法输入保留，不用转换失败的0覆盖草稿。
        auto *action = qobject_cast<QLineEdit *>(table->cellWidget(row, 6));
        auto *mode = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
        QMap<int, QLineEdit *> volumes;
        for (int column : {2, 3, 4, 5})
            volumes.insert(column, qobject_cast<QLineEdit *>(table->cellWidget(row, column)));
        auto updateReaction = [volumes, action, mode]()
        {
            const QString modeName = mode->currentText();
            const QList<int> inputs = modeName == QStringLiteral("none") || modeName == QStringLiteral("onedilute")
                                          ? QList<int>{2, 3}
                                          : modeName == QStringLiteral("predilute") ? QList<int>{4} : QList<int>{4, 5};
            if (!choices(QStringLiteral("sample_add_mode"), true).contains(modeName))
                return;
            double total = 0;
            for (int column : inputs)
            {
                auto *edit = volumes.value(column);
                if (!edit || !validNumber(edit->text(), false))
                    return;
                total += edit->text().toDouble();
            }
            action->setText(QString::number(total, 'f', 1));
        };
        // 捕获本行控件而非行号，前面的行删除后仍能更新正确方案。
        for (int column : {2, 3, 4, 5, 6})
            if (auto *edit = qobject_cast<QLineEdit *>(table->cellWidget(row, column)))
                connect(edit, &QLineEdit::editingFinished, action, updateReaction);
        connect(mode, &QComboBox::currentTextChanged, action, [updateReaction](const QString &) { updateReaction(); });
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
    if (m_originalProfile && table->cellWidget(row, 0))
    {
        const QString originalRowId = table->cellWidget(row, 0)->property("originalRowId").toString();
        if (!originalRowId.isEmpty()) values.insert(QStringLiteral("_original_rowid"), originalRowId);
    }
    return values;
}

void ProjectAdvancedEditor::showAdvanced(const ProjectAdvanced &value, const QStringList &rateLabels)
{
    m_populating = true;
    m_configured = value.configured;
    m_diluentSpecified = value.fields.contains(QStringLiteral("IBS_name"));
    m_sampleActionsPresent = !value.samples.isEmpty();
    m_defaultLabel.clear();
    for (const QString &name : scalarFields())
    {
        if (auto *edit = m_root->findChild<QLineEdit *>(name))
            edit->setText(value.fields.value(name));
        if (auto *combo = m_root->findChild<QComboBox *>(name))
        {
            const QString saved = name == QStringLiteral("IBS_name") && !m_diluentSpecified
                                      ? QStringLiteral("none") : value.fields.value(name);
            if (!saved.isEmpty() && combo->findText(saved) < 0)
                combo->addItem(saved);
            combo->setCurrentIndex(combo->findText(saved));
        }
    }
    if (m_samples)
    {
        m_samples->setRowCount(0);
        if (value.samples.isEmpty())
        {
            for (const QString &rate : rateLabels)
                populateRow(m_samples, {{QStringLiteral("sample_rate"), rate}}, true);
        }
        else
            for (const ProjectActionFields &row : value.samples)
                populateRow(m_samples, row, true);
    }
    if (m_reagents)
    {
        m_reagents->setRowCount(0);
        for (const ProjectActionFields &row : value.reagents)
            populateRow(m_reagents, row, false);
    }
    refreshDefaultChoices();
    m_populating = false;
    setWritable(m_writable);
}

ProjectAdvanced ProjectAdvancedEditor::advanced() const
{
    ProjectAdvanced value;
    value.configured = m_configured;
    for (const QString &name : scalarFields())
    {
        // 旧库没有稀释液字段时不凭显示的none增加已提交参数。
        if (name == QStringLiteral("IBS_name") && !m_diluentSpecified)
            continue;
        QString text;
        if (auto *edit = m_root->findChild<QLineEdit *>(name))
            text = edit->text();
        if (auto *combo = m_root->findChild<QComboBox *>(name))
            text = combo->currentText();
        value.fields.insert(name, text);
        if (!text.isEmpty())
            value.configured = true;
    }
    QList<ProjectActionFields> sampleRows;
    bool sampleActionsPresent = m_sampleActionsPresent;
    if (m_samples)
    {
        for (int row = 0; row < m_samples->rowCount(); ++row)
        {
            const ProjectActionFields values = rowFields(m_samples, row, true);
            sampleRows.append(values);
            if (m_samples->cellWidget(row, 0)->property("sampleActionDraft").toBool())
                sampleActionsPresent = true;
            for (auto field = values.cbegin(); field != values.cend(); ++field)
                if (field.key() != QStringLiteral("sample_rate") && field.key() != QStringLiteral("sample_option") &&
                    !(field.key() == QStringLiteral("calibration_option") && field.value() == QStringLiteral("0")) &&
                    !field.value().isEmpty())
                    sampleActionsPresent = true;
        }
    }
    if (sampleActionsPresent && !sampleRows.isEmpty())
    {
        value.samples = sampleRows;
        value.configured = true;
    }
    if (m_reagents)
    {
        for (int row = 0; row < m_reagents->rowCount(); ++row)
            value.reagents.append(rowFields(m_reagents, row, false));
        value.configured = value.configured || !value.reagents.isEmpty();
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
        }
    }
    for (const QString &name : {QStringLiteral("btnAddReagent"), QStringLiteral("btnDelReagent"),
                               QStringLiteral("btnAddSampleRate"), QStringLiteral("btnDelSampleRate"),
                               QStringLiteral("cbxDefaultRate"), QStringLiteral("sampleRateDefaultTable")})
    {
        if (auto *widget = m_root->findChild<QWidget *>(name))
            widget->setEnabled(writable);
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
    refreshDefaultSample();
    m_populating = wasPopulating;
}

void ProjectAdvancedEditor::setDiluentChoices(const QStringList &names)
{
    auto *combo = m_root->findChild<QComboBox *>(QStringLiteral("IBS_name"));
    if (!combo)
        return;
    const QSignalBlocker blocker(combo);
    const QString saved = combo->currentText();
    combo->clear();
    combo->addItem(QStringLiteral("none"));
    for (const QString &name : names)
        if (combo->findText(name) < 0)
            combo->addItem(name);
    if (!saved.isEmpty() && combo->findText(saved) < 0)
        combo->addItem(saved);
    combo->setCurrentIndex(combo->findText(saved));
    combo->setToolTip(tr("只能保存有效的缓冲稀释液；过期名称须重新选择。"));
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
            }
        }
    }
    refreshDefaultChoices();
    m_populating = wasPopulating;
}

void ProjectAdvancedEditor::setOriginalProfile(bool original)
{
    m_originalProfile = original;
}

QStringList ProjectAdvancedEditor::sampleRateLabels() const
{
    QStringList labels;
    if (m_samples)
        for (int row = 0; row < m_samples->rowCount(); ++row)
            labels.append(rowFields(m_samples, row, true).value(QStringLiteral("sample_rate")));
    return labels;
}

QMap<QString, bool> ProjectAdvancedEditor::sampleSelections() const
{
    QMap<QString, bool> options;
    if (m_samples)
        for (int row = 0; row < m_samples->rowCount(); ++row)
        {
            const ProjectActionFields values = rowFields(m_samples, row, true);
            options.insert(values.value(QStringLiteral("sample_rate")),
                           values.value(QStringLiteral("sample_option")) == QStringLiteral("1"));
        }
    return options;
}

QString ProjectAdvancedEditor::defaultSampleLabel() const
{
    return m_defaultLabel;
}

QString ProjectAdvancedEditor::validateSampleSelections() const
{
    const QStringList labels = sampleRateLabels();
    // 空列表沿用旧库的“尚未配置”；非空列表必须明确选用且只有一个默认。
    if (labels.isEmpty())
        return {};
    if (labels.size() > 64)
        return tr("稀释方案最多64行。");
    QSet<QString> seen;
    for (const QString &label : labels)
    {
        if (!validReferenceName(label, m_originalProfile))
            return m_originalProfile ? tr("稀释方案名称须为1至64字，不能含首尾空白、控制字符或反斜杠；1/16等原库标签可以保留。")
                                     : tr("稀释方案名称须为1至64字，不能含首尾空白、控制或路径字符。");
        if (seen.contains(label))
            return tr("稀释方案名称重复：%1。请修改名称。").arg(label);
        seen.insert(label);
    }
    if (!labels.contains(m_defaultLabel) || !sampleSelections().value(m_defaultLabel, false))
        return tr("请选用至少一个稀释方案，并从中明确选择默认方案。");
    return {};
}

void ProjectAdvancedEditor::refreshDefaultChoices()
{
    auto *combo = m_root->findChild<QComboBox *>(QStringLiteral("cbxDefaultRate"));
    if (!combo)
        return;
    const QSignalBlocker blocker(combo);
    combo->clear();
    if (m_samples)
        for (int row = 0; row < m_samples->rowCount(); ++row)
        {
            const ProjectActionFields values = rowFields(m_samples, row, true);
            const QString label = values.value(QStringLiteral("sample_rate"));
            if (!label.isEmpty() && values.value(QStringLiteral("sample_option")) == QStringLiteral("1") &&
                combo->findText(label) < 0)
                combo->addItem(label);
        }
    // 原FDP资料存在“默认但未选用”的旧值。读取时显示原值，不能静默改库；
    // 用户修改方案后仍须重新选择有效默认，保存规则由原库实际变更部分核对。
    if (m_originalProfile && m_populating && !m_defaultLabel.isEmpty() &&
        combo->findText(m_defaultLabel) < 0)
    {
        combo->addItem(m_defaultLabel);
        combo->setToolTip(tr("当前显示原库保存的默认方案；若它未选用，修改方案时须明确重选。"));
    }
    combo->setCurrentIndex(combo->findText(m_defaultLabel));
    // 不擅自改成第一行。删除或取消选用默认后，保存会要求使用者重新选择。
    if (combo->currentIndex() < 0)
        m_defaultLabel.clear();
    refreshDefaultSample();
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
            copy->setReadOnly(column == 0);
            copy->setAlignment(Qt::AlignCenter);
            copy->setFont(edit->font());
            copy->setStyleSheet(edit->styleSheet());
            if (column > 0)
            {
                auto *validator = new QDoubleValidator(0, 999, 1, copy);
                validator->setNotation(QDoubleValidator::StandardNotation);
                copy->setValidator(validator);
                connect(copy, &QLineEdit::textChanged, edit, &QLineEdit::setText);
                connect(copy, &QLineEdit::editingFinished, edit, &QLineEdit::editingFinished);
            }
            connect(edit, &QLineEdit::textChanged, copy, &QLineEdit::setText);
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
            connect(copy, QOverload<int>::of(&QComboBox::currentIndexChanged), combo, &QComboBox::setCurrentIndex);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), copy, &QComboBox::setCurrentIndex);
            mirror->setCellWidget(0, column, copy);
        }
    }
    mirror->setRowHeight(0, 41);
    mirror->setEnabled(m_writable);
}
