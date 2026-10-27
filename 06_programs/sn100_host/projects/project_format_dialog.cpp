#include "project_format_dialog.h"
#include "cuscheckbox.h"
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSet>
#include <QStringList>
#include <QTableWidget>
#include <QVariant>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
enum ResultColumn
{
    TypeColumn, NameColumn, DecimalsColumn, UnitColumn, MainColumn, UsedColumn, DisplayColumn,
    TestLowColumn, TestHighColumn, NormalLowColumn, NormalHighColumn, LisCodeColumn, ColumnCount
};
const QStringList slotNames{QStringLiteral("Original"), QStringLiteral("Calibration"),
                            QStringLiteral("Rate"), QStringLiteral("INR")};
QComboBox *choice(QWidget *parent)
{
    auto *box = new QComboBox(parent);
    box->setView(new QListView);
    return box;
}
QComboBox *selectionChoice(QWidget *parent, int value)
{
    auto *box = choice(parent);
    box->addItem(QObject::tr("未确定"), -1);
    box->addItem(QObject::tr("否"), 0);
    box->addItem(QObject::tr("是"), 1);
    box->setCurrentIndex(box->findData(value));
    return box;
}
}
ProjectFormatDialog::ProjectFormatDialog(ProjectsStore *store, qint64 projectId, QString databasePath,
    AccountSession session, std::function<qint64()> nextRequest, QWidget *parent)
    : QDialog(parent), m_store(store), m_projectId(projectId), m_databasePath(std::move(databasePath)),
      m_session(std::move(session)), m_nextRequest(std::move(nextRequest))
{
    setObjectName(QStringLiteral("projectCurrentFormatDialog"));
    setWindowTitle(tr("配置结果显示"));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1160, 590);
    auto *layout = new QVBoxLayout(this);
    m_message = new QLabel(tr("正在读取所选项目已保存的配置。"), this);
    m_message->setObjectName(QStringLiteral("formatStatus"));
    m_message->setWordWrap(true);
    layout->addWidget(m_message);
    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("currentFormatTable"));
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("结果类型/槽号"), tr("结果名称"), tr("小数位"), tr("单位/倍率"),
        tr("主结果"), tr("选用"), tr("显示"), tr("测试下限"), tr("测试上限"), tr("正常下限"), tr("正常上限"), tr("传输代号")});
    m_table->setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 9));
    m_table->horizontalHeader()->setDefaultSectionSize(100);
    m_table->setColumnWidth(TypeColumn, 200); m_table->setColumnWidth(UnitColumn, 170);
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table);
    auto *buttons = new QHBoxLayout;
    m_add = new QPushButton(tr("新增结果槽"), this); m_add->setObjectName(QStringLiteral("formatAdd"));
    m_remove = new QPushButton(tr("删除选中行"), this); m_remove->setObjectName(QStringLiteral("formatRemove"));
    m_reload = new QPushButton(tr("重新读取"), this); m_reload->setObjectName(QStringLiteral("formatReload"));
    m_save = new QPushButton(tr("保存显示配置"), this); m_save->setObjectName(QStringLiteral("formatSave"));
    auto *cancel = new QPushButton(tr("取消"), this); cancel->setObjectName(QStringLiteral("formatCancel"));
    for (auto *button : {m_add, m_remove, m_reload, m_save, cancel})
    { button->setAutoDefault(false); buttons->addWidget(button); }
    layout->addLayout(buttons);
    connect(m_add, &QPushButton::clicked, this, [this]()
    {
        if (!ProjectsStore::canEdit(m_session) || m_reading || m_saving || !m_snapshot.valid()) return;
        if (m_table->rowCount() >= 4) { message(tr("配置须恰好四槽；请先处理已有行。")); return; }
        addRow(nullptr, nullptr); changed();
    });
    connect(m_remove, &QPushButton::clicked, this, [this]()
    {
        if (!ProjectsStore::canEdit(m_session) || m_reading || m_saving || !m_snapshot.valid()) return;
        const int row = m_table->currentRow();
        if (row >= 0 && discardConfirmed(tr("从本次草稿移除选中结果行？保存前不会修改已存配置。")))
        { m_table->removeRow(row); changed(); }
    });
    connect(cancel, &QPushButton::clicked, this, &ProjectFormatDialog::reject);
    connect(m_reload, &QPushButton::clicked, this, &ProjectFormatDialog::reloadRequested);
    connect(m_save, &QPushButton::clicked, this, &ProjectFormatDialog::saveDraft);
    connect(store, &ProjectsStore::currentProjectFormatReady, this,
        [this](qint64 request, ProjectRecord project, ProjectCurrentFormatSnapshot snapshot)
        {
            if (!m_reading || request != m_readRequest) return;
            m_reading = false; emit operationBusyChanged(false);
            if (project.id != m_projectId || !snapshot.valid() || snapshot.fingerprint().size() != 64 ||
                QDir::cleanPath(snapshot.databasePath()) != m_databasePath)
            { failed(tr("所选项目或资料源不匹配；未采用该读取结果。")); return; }
            populate(project, snapshot);
        }, Qt::QueuedConnection);
    connect(store, &ProjectsStore::succeeded, this,
        [this](qint64 request, const QString &operation, qint64 id)
        {
            if (!m_saving || request != m_saveRequest || operation != QStringLiteral("replaceCurrentResultFormat")) return;
            m_saving = false; emit operationBusyChanged(false);
            if (id != m_projectId) { failed(tr("保存回执项目不匹配；未认定当前草稿已保存，请重新读取。")); return; }
            m_dirty = false; emit draftChanged();
            emit committed(id); // Only a real successful Store receipt closes the editor.
            QDialog::accept();
        }, Qt::QueuedConnection);
    connect(store, &ProjectsStore::error, this,
        [this](qint64 request, const QString &operation, const QString &error)
        {
            const bool readError = m_reading && request == m_readRequest && operation == QStringLiteral("loadCurrentProjectFormat");
            const bool writeError = m_saving && request == m_saveRequest && operation == QStringLiteral("replaceCurrentResultFormat");
            if (!readError && !writeError) return;
            m_reading = m_saving = false; emit operationBusyChanged(false);
            failed(error + tr(" 草稿保留；重试前请明确重新读取并确认替换草稿。"));
        }, Qt::QueuedConnection);
    connect(store, &ProjectsStore::closed, this, [this]()
    {
        const bool pending = m_saving;
        m_reading = m_saving = false; m_available = false;
        emit operationBusyChanged(false);
        failed(pending ? tr("资料源已关闭，保存结果未获确认；未推定已提交或取消。保留草稿，重新打开资料源后核查。")
                       : tr("项目资料源已关闭；保留草稿，不能继续保存。"));
    }, Qt::QueuedConnection);
    connect(store, &ProjectsStore::ready, this, [this](const QString &path)
    {
        const bool same = QFileInfo(path).canonicalFilePath() == m_databasePath;
        invalidateContext(same ? tr("项目资料源已重新打开，请明确重新读取。") : tr("项目资料源已切换，请关闭后重新进入。"), same);
    }, Qt::QueuedConnection);
    connect(store, &QObject::destroyed, this, [this]()
    {
        m_reading = m_saving = false; m_available = false;
        emit operationBusyChanged(false); failed(tr("项目存储已销毁；保存状态不可推定，草稿未作成功处理。"));
    });
    updateActions();
}
void ProjectFormatDialog::message(const QString &text)
{
    m_message->setText(text); setProperty("lastStatus", text);
}
void ProjectFormatDialog::failed(const QString &text)
{
    m_snapshot = ProjectCurrentFormatSnapshot();
    message(text); emit configurationInvalidated(text); updateActions();
}
void ProjectFormatDialog::invalidateContext(const QString &reason, bool sourceAvailable)
{
    m_snapshot = ProjectCurrentFormatSnapshot(); m_available = sourceAvailable;
    m_reading = false; m_readRequest = 0;
    if (!m_saving) emit operationBusyChanged(false);
    message(reason); emit configurationInvalidated(reason); updateActions();
}
void ProjectFormatDialog::setSession(AccountSession session)
{
    m_session = std::move(session);
    invalidateContext(ProjectsStore::canEdit(m_session)
        ? tr("登录身份已更新；草稿保留，请重新读取后编辑。")
        : tr("当前身份仅可查看；草稿未保存，不会自动提交或丢弃。"), m_available);
}
void ProjectFormatDialog::startRead()
{
    if (m_saving || m_reading) return;
    if (!m_store || !m_available || !m_nextRequest || m_databasePath.isEmpty())
    { failed(tr("项目资料源不可用，请关闭后核查。")); return; }
    m_snapshot = ProjectCurrentFormatSnapshot(); m_readRequest = m_nextRequest();
    if (m_readRequest <= 0) { failed(tr("编辑上下文已关闭。")); return; }
    m_reading = true; updateActions(); emit operationBusyChanged(true);
    message(tr("正在读取已保存的项目和显示配置。"));
    if (!QMetaObject::invokeMethod(m_store.data(), "loadCurrentProjectFormat", Qt::QueuedConnection,
        Q_ARG(qint64, m_readRequest), Q_ARG(qint64, m_projectId)))
    {
        m_reading = false; emit operationBusyChanged(false);
        failed(tr("读取请求未投递，未使用旧页面配置。"));
    }
}
void ProjectFormatDialog::populate(ProjectRecord project, ProjectCurrentFormatSnapshot snapshot)
{
    if (project.results.size() > 16)
    { failed(tr("现有结果行过多，本编辑器不截断或猜测四槽；请先核查项目来源。")); return; }
    QScopedValueRollback<bool> guard(m_populating, true);
    m_record = std::move(project); m_snapshot = std::move(snapshot);
    m_table->setRowCount(0);
    for (const auto &result : m_record.results)
    {
        const ProjectCurrentFormatEntry *selection = nullptr;
        for (const auto &entry : m_snapshot.entries())
            if (entry.projectId == m_projectId && entry.resultTypeName == result.type) { selection = &entry; break; }
        addRow(&result, selection);
    }
    m_dirty = false; emit draftChanged();
    setWindowTitle(tr("配置结果显示 · %1%2").arg(m_record.name,
        ProjectsStore::canEdit(m_session) ? QString() : tr("（只读）")));
    message(tr("仅保存当前结果显示配置，其它项目参数保持已提交值。Time须明确选择槽位；缺槽、类型/槽号、选用和显示未确定时不能保存。"));
    updateActions();
}
void ProjectFormatDialog::addRow(const ProjectResult *result, const ProjectCurrentFormatEntry *selection)
{
    const int row = m_table->rowCount(); m_table->insertRow(row); m_table->setRowHeight(row, 41);
    const int storedSlot = result ? slotNames.indexOf(result->type) + 1 : 0;
    const bool confirmed = result && selection && selection->selectionKnown &&
        storedSlot >= 1 && storedSlot <= 4 && selection->sourceResultId == storedSlot;
    auto *type = choice(m_table);
    QString unconfirmedType = tr("请选择结果类型及槽号");
    if (result) unconfirmedType = storedSlot == 0
        ? tr("%1（未映射，需明确选择）").arg(result->type)
        : tr("%1（原类型；槽号未确认）").arg(result->type);
    type->addItem(unconfirmedType, QString());
    for (int index = 0; index < slotNames.size(); ++index)
        type->addItem(tr("%1（槽%2）").arg(slotNames[index]).arg(index + 1), slotNames[index]);
    type->setCurrentIndex(confirmed ? storedSlot : 0);
    m_table->setCellWidget(row, TypeColumn, type);
    connect(type, QOverload<int>::of(&QComboBox::activated), this, [this](int) { changed(); });
    auto addText = [this, row](int column, const QString &value, int length)
    {
        auto *edit = new QLineEdit(value, m_table); edit->setAlignment(Qt::AlignCenter);
        if (length) edit->setMaxLength(qMax(length, value.size())); // Preserve an overlong committed value until explicit correction.
        m_table->setCellWidget(row, column, edit);
        connect(edit, &QLineEdit::textEdited, this, [this]() { changed(); });
    };
    addText(NameColumn, result ? result->name : QString(), 16);
    auto *decimals = choice(m_table); decimals->addItem(tr("未确定"), -1);
    for (int n = 1; n <= 6; ++n) decimals->addItem(QString::number(n), n);
    const int precisionIndex = result ? decimals->findData(result->decimals) : 0;
    if (precisionIndex < 0) decimals->addItem(tr("原精度无效：%1").arg(result->decimals), -1);
    decimals->setCurrentIndex(precisionIndex < 0 ? decimals->count() - 1 : precisionIndex);
    m_table->setCellWidget(row, DecimalsColumn, decimals);
    connect(decimals, QOverload<int>::of(&QComboBox::activated), this, [this](int) { changed(); });
    auto *unit = choice(m_table); unit->addItem(tr("请选择已存单位"), QString());
    for (const auto &value : m_snapshot.units())
        unit->addItem(tr("%1（倍率%2）").arg(value.name, QString::number(value.storedRatio, 'g', 17)), value.name);
    int unitIndex = result ? unit->findData(result->unit) : 0;
    if (unitIndex < 0) { unit->addItem(tr("%1（原单位未在字典中）").arg(result->unit), result->unit); unitIndex = unit->count() - 1; }
    unit->setCurrentIndex(unitIndex); m_table->setCellWidget(row, UnitColumn, unit);
    connect(unit, QOverload<int>::of(&QComboBox::activated), this, [this](int) { changed(); });
    auto *main = new CusCheckBox(m_table); main->setText(tr("主"));
    main->setCheckState(result && result->mainResult ? Qt::Checked : Qt::Unchecked);
    m_table->setCellWidget(row, MainColumn, main);
    connect(main, QOverload<bool>::of(&CusCheckBox::signalClicked), this, [this, main](bool checked)
    {
        if (checked) for (int i = 0; i < m_table->rowCount(); ++i)
            if (auto *other = qobject_cast<CusCheckBox *>(m_table->cellWidget(i, MainColumn)))
                if (other != main) other->setCheckState(Qt::Unchecked);
        changed();
    });
    for (int column : {UsedColumn, DisplayColumn})
    {
        const int value = confirmed ? int(column == UsedColumn ? selection->used : selection->display) : -1;
        auto *box = selectionChoice(m_table, value); m_table->setCellWidget(row, column, box);
        connect(box, QOverload<int>::of(&QComboBox::activated), this, [this](int) { changed(); });
    }
    for (int column = TestLowColumn; column <= NormalHighColumn; ++column)
    {
        QString value;
        if (result)
        {
            const double values[]{result->testLow, result->testHigh, result->normalLow, result->normalHigh};
            value = QString::number(values[column - TestLowColumn], 'g', 17);
        }
        addText(column, value, 0);
    }
    addText(LisCodeColumn, result ? result->lisCode : QString(), 8);
    updateActions();
}
void ProjectFormatDialog::changed()
{
    if (m_populating || m_reading || m_saving || !ProjectsStore::canEdit(m_session)) return;
    m_dirty = true; emit draftChanged(); updateActions();
}
void ProjectFormatDialog::updateActions()
{
    const bool editable = m_available && !m_reading && !m_saving && m_snapshot.valid() && ProjectsStore::canEdit(m_session);
    for (int row = 0; row < m_table->rowCount(); ++row)
        for (int column = 0; column < m_table->columnCount(); ++column)
        {
            auto *widget = m_table->cellWidget(row, column);
            if (auto *edit = qobject_cast<QLineEdit *>(widget)) edit->setReadOnly(!editable);
            else if (widget) widget->setEnabled(editable);
        }
    m_add->setEnabled(editable && m_table->rowCount() < 4); m_remove->setEnabled(editable);
    m_save->setEnabled(editable && m_dirty);
    m_reload->setEnabled(m_available && m_store && !m_reading && !m_saving);
}
void ProjectFormatDialog::saveDraft()
{
    if (!m_store || !m_available || !m_nextRequest || !m_dirty || m_reading || m_saving || !m_snapshot.valid() || !ProjectsStore::canEdit(m_session)) return;
    if (m_table->rowCount() != 4) { message(tr("须明确配置Original/Calibration/Rate/INR四槽，未自动补齐。")); return; }
    ProjectResults configurations; ProjectCurrentFormatSelections selections; QSet<QString> seen;
    for (int row = 0; row < 4; ++row)
    {
        auto combo = [this, row](int column) { return qobject_cast<QComboBox *>(m_table->cellWidget(row, column)); };
        auto text = [this, row](int column) { return qobject_cast<QLineEdit *>(m_table->cellWidget(row, column))->text(); };
        ProjectResult result; result.type = combo(TypeColumn)->currentData().toString();
        if (!slotNames.contains(result.type) || seen.contains(result.type))
        { message(tr("结果类型未明确或重复；Time没有自动别名。")); return; }
        seen.insert(result.type); result.name = text(NameColumn); result.decimals = combo(DecimalsColumn)->currentData().toInt();
        result.unit = combo(UnitColumn)->currentData().toString();
        result.mainResult = qobject_cast<CusCheckBox *>(m_table->cellWidget(row, MainColumn))->isChecked();
        ProjectCurrentFormatSelection selection; selection.resultType = result.type;
        selection.resultId = slotNames.indexOf(result.type) + 1;
        selection.used = combo(UsedColumn)->currentData().toInt(); selection.display = combo(DisplayColumn)->currentData().toInt();
        if (selection.used < 0 || selection.display < 0 || result.decimals < 1 || result.decimals > 6)
        { message(tr("各槽精度、选用和显示须明确选择，不能使用未知默认值。")); return; }
        const auto unit = std::find_if(m_snapshot.units().cbegin(), m_snapshot.units().cend(),
            [&result](const ProjectCurrentFormatUnit &value) { return value.name == result.unit; });
        if (unit == m_snapshot.units().cend() || !std::isfinite(unit->storedRatio) || unit->storedRatio < 1 ||
            unit->storedRatio > std::numeric_limits<int>::max() || std::floor(unit->storedRatio) != unit->storedRatio)
        { message(tr("请选择真实已存、倍率为正整数的单位；未默认或截断倍率。")); return; }
        double *bounds[]{&result.testLow, &result.testHigh, &result.normalLow, &result.normalHigh};
        for (int column = TestLowColumn; column <= NormalHighColumn; ++column)
        {
            bool ok = false; const QString value = text(column).trimmed(); const double number = value.toDouble(&ok);
            if (value.isEmpty() || !ok || !std::isfinite(number))
            { message(tr("范围须为明确有效数字，不能把空输入当零。")); return; }
            *bounds[column - TestLowColumn] = number;
        }
        result.lisCode = text(LisCodeColumn); configurations.append(result); selections.append(selection);
    }
    ProjectRecord replacement = m_record; replacement.results = configurations;
    ProjectUnits units;
    for (const auto &unit : m_snapshot.units()) { ProjectUnit value; value.name = unit.name; value.ratio = unit.storedRatio; units.append(value); }
    const QString error = ProjectsStore::validate(replacement, units);
    if (!error.isEmpty()) { message(error); return; }
    m_saveRequest = m_nextRequest(); if (m_saveRequest <= 0) { failed(tr("编辑上下文已关闭。")); return; }
    m_saving = true; updateActions(); emit operationBusyChanged(true); message(tr("正在保存显示配置，请等待真实保存结果。"));
    if (!QMetaObject::invokeMethod(m_store.data(), "replaceCurrentResultFormat", Qt::QueuedConnection,
        Q_ARG(qint64, m_saveRequest), Q_ARG(qint64, m_projectId), Q_ARG(QString, m_snapshot.fingerprint()),
        Q_ARG(ProjectResults, configurations), Q_ARG(ProjectCurrentFormatSelections, selections)))
    { m_saving = false; emit operationBusyChanged(false); failed(tr("保存请求未投递，草稿保留。")); }
}
bool ProjectFormatDialog::discardConfirmed(const QString &text)
{
    QMessageBox question(QMessageBox::Question, tr("配置结果显示"), text,
        QMessageBox::Discard | QMessageBox::Cancel, this);
    question.setDefaultButton(QMessageBox::Cancel);
    return question.exec() == QMessageBox::Discard;
}
void ProjectFormatDialog::reloadRequested()
{
    if (m_reading || m_saving) return;
    if (m_dirty && !discardConfirmed(tr("放弃本次显示配置草稿，重新读取当前已保存记录？"))) return;
    startRead();
}
bool ProjectFormatDialog::mayClose()
{
    if (m_saving) { message(tr("保存处理中，须等待真实回执后才能关闭。")); return false; }
    return !m_dirty || discardConfirmed(tr("放弃未保存的显示配置草稿？"));
}
void ProjectFormatDialog::reject()
{
    if (!m_closeApproved && !mayClose()) return;
    m_reading = false; m_readRequest = 0; emit operationBusyChanged(false);
    QDialog::reject();
}
void ProjectFormatDialog::accept() { saveDraft(); }
void ProjectFormatDialog::closeEvent(QCloseEvent *event)
{
    if (!mayClose()) { event->ignore(); return; }
    QScopedValueRollback<bool> guard(m_closeApproved, true);
    QDialog::closeEvent(event);
}
