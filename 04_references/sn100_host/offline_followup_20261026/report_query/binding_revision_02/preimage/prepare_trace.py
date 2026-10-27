"""Prepare an observational test copy; never compile or launch an application."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
WORK = Path(__file__).resolve().parent
SOURCE = ROOT / '06_programs/sn100_host/tests/integrated_report_test.cpp'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def replace_once(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Expected exactly one source anchor: ' + old[:90])
    return text.replace(old, new, 1)

if sha(SOURCE) != '7b500eb86676ffb954ac5e3fd7ea39b6edf17c7700b093488ac82840e39c3a7e':
    raise RuntimeError('Reviewed test source changed; re-audit before preparing')
target = WORK / 'source'
target.mkdir(exist_ok=False)
text = SOURCE.read_text(encoding='utf-8')
text = replace_once(text, '#include "ui/frmhospinfosetting.h"',
    '#include "ui/frmhospinfosetting.h"\n#include "../report_query_trace.h"')
text = replace_once(text, '        host.show();\n        hospitalThread.start();',
    '        LegacyQueryTrace::watch(page, history, directory);\n        host.show();\n        hospitalThread.start();')
start = text.index('bool click(QAbstractButton *button)\n{')
end = text.index('\nbool paste(', start)
text = text[:start] + '''bool click(QAbstractButton *button)
{
    LegacyQueryTrace::event(QStringLiteral("click_before"), {{QStringLiteral("button"), LegacyQueryTrace::widget(button)}});
    if (!button || !button->isEnabled()) return false;
    reveal(button);
    if (!button->isVisible()) return false;
    QSignalSpy clicked(button, &QAbstractButton::clicked);
    QTest::mouseClick(button, Qt::LeftButton);
    LegacyQueryTrace::event(QStringLiteral("click_after"), {{QStringLiteral("button"), LegacyQueryTrace::widget(button)},
        {QStringLiteral("clicked_count"), clicked.count()}});
    return true;
}
''' + text[end:]
start = text.index('bool selectDate(QDialog *dialog, const char *buttonName, const char *editName)\n{')
end = text.index('\nclass Session', start)
text = text[:start] + '''bool selectDate(QDialog *dialog, const char *buttonName, const char *editName)
{
    const QString field = QString::fromLatin1(editName);
    const auto trace = [&](const QString &stage, QJsonObject data = {}) {
        data.insert(QStringLiteral("field"), field);
        LegacyQueryTrace::event(stage, data);
    };
    auto *edit = control<QLineEdit>(dialog, editName);
    trace(QStringLiteral("date_begin"), {{QStringLiteral("text"), edit ? edit->text() : QString()}});
    if (!edit) { trace(QStringLiteral("date_missing_edit")); return false; }
    if (edit->text() == QStringLiteral("2026/10/23")) { trace(QStringLiteral("date_already_target")); return true; }
    if (!click(control<QAbstractButton>(dialog, buttonName))) { trace(QStringLiteral("date_button_failed")); return false; }
    QCalendarWidget *calendar = nullptr;
    if (!waitUntil([&]()
    {
        for (auto *candidate : dialog->findChildren<QCalendarWidget *>())
            if (candidate->isVisible()) { calendar = candidate; return true; }
        return false;
    })) { trace(QStringLiteral("date_calendar_not_visible")); return false; }
    QObject::connect(calendar, &QCalendarWidget::clicked, dialog, [field](QDate date) {
        LegacyQueryTrace::event(QStringLiteral("calendar_clicked"), {{QStringLiteral("field"), field},
            {QStringLiteral("date"), date.toString(Qt::ISODate)}});
    });
    QObject::connect(edit, &QLineEdit::textChanged, dialog, [field](QString value) {
        LegacyQueryTrace::event(QStringLiteral("date_text_changed"), {{QStringLiteral("field"), field}, {QStringLiteral("value"), value}});
    });
    calendar->setCurrentPage(2026, 10);
    QCoreApplication::processEvents();
    auto *grid = control<QTableView>(calendar, "qt_calendar_calendarview");
    if (!grid) { trace(QStringLiteral("date_missing_grid")); return false; }
    for (int row = 1; row < grid->model()->rowCount(); ++row)
        for (int column = 0; column < grid->model()->columnCount(); ++column)
        {
            const auto index = grid->model()->index(row, column);
            if (index.data().toString() == QStringLiteral("23") && (index.flags() & Qt::ItemIsSelectable))
            {
                const QPoint point = grid->visualRect(index).center();
                trace(QStringLiteral("date_target_before_click"), {{QStringLiteral("row"), row}, {QStringLiteral("column"), column},
                    {QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()},
                    {QStringLiteral("viewport_contains"), grid->viewport()->rect().contains(point)},
                    {QStringLiteral("visible_region_contains"), grid->viewport()->visibleRegion().contains(point)},
                    {QStringLiteral("shown_month"), calendar->monthShown()}, {QStringLiteral("shown_year"), calendar->yearShown()},
                    {QStringLiteral("selected"), calendar->selectedDate().toString(Qt::ISODate)}});
                QTest::mouseClick(grid->viewport(), Qt::LeftButton, Qt::NoModifier, point);
                const bool ok = waitUntil([&]() { return edit->text() == QStringLiteral("2026/10/23"); });
                trace(QStringLiteral("date_wait_finished"), {{QStringLiteral("ok"), ok}, {QStringLiteral("actual_text"), edit->text()}});
                return ok;
            }
        }
    trace(QStringLiteral("date_no_target_cell"));
    return false;
}
''' + text[end:]
start = text.index('    bool query()\n    {')
end = text.index('\n    QTableWidget *table()', start)
text = text[:start] + '''    bool query()
    {
        const auto failure = [&](const char *stage) {
            LegacyQueryTrace::event(QString::fromLatin1(stage), {{QStringLiteral("directory"), directory},
                {QStringLiteral("busy"), page->isBusy()}, {QStringLiteral("status"), page->property("lastStatus").toString()}});
            return false;
        };
        operations->clear();
        LegacyQueryTrace::event(QStringLiteral("query_helper_begin"), {{QStringLiteral("directory"), directory}});
        if (!click(control<QAbstractButton>(page, "btnQuery"))) return failure("query_click_failed");
        auto *dialog = control<QDialog>(page, "MaskDlgQueryReport");
        if (!dialog || !waitUntil([&]() { return dialog->isVisible(); })) return failure("query_dialog_failed");
        if (!selectDate(dialog, "btnCalendarStart", "ldtStartDate") || !selectDate(dialog, "btnCalendarEnd", "ldtEndDate"))
            return failure("query_date_failed");
        LegacyQueryTrace::event(QStringLiteral("query_before_save"), {{QStringLiteral("start"), control<QLineEdit>(dialog, "ldtStartDate")->text()},
            {QStringLiteral("end"), control<QLineEdit>(dialog, "ldtEndDate")->text()},
            {QStringLiteral("sample"), control<QLineEdit>(dialog, "ldtSampleID")->text()},
            {QStringLiteral("barcode"), control<QLineEdit>(dialog, "ldtSampleCode")->text()}});
        if (!click(control<QAbstractButton>(dialog, "btnSave"))) return failure("query_save_click_failed");
        const bool ok = waitUntil([&]() { return !page->isBusy() && finished(*operations, QStringLiteral("query")); });
        LegacyQueryTrace::event(QStringLiteral("query_helper_finished"), {{QStringLiteral("ok"), ok},
            {QStringLiteral("directory"), directory}, {QStringLiteral("busy"), page->isBusy()},
            {QStringLiteral("operations_count"), operations->count()}, {QStringLiteral("status"), page->property("lastStatus").toString()}});
        return ok;
    }
''' + text[end:]
output = target / 'integrated_report_test.cpp'
output.write_text(text, encoding='utf-8', newline='\n')
# Keep the reviewed object's compiler/include/link handling in a separate copy.
builder = ROOT / '06_programs/sn100_host/tests/build_shell_drivers.py'
build_text = builder.read_text(encoding='utf-8')
wsl_root = '/mnt/c/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
build_text = replace_once(build_text, 'program = Path(__file__).resolve().parents[1]',
    'program = Path(' + repr(wsl_root + '/06_programs/sn100_host') + ')')
build_text = replace_once(build_text,
    "source = (program / independent.get(name, 'tests/' + name + '.cpp')).resolve(strict=True)",
    'source = Path(' + repr(wsl_root + '/' + output.relative_to(ROOT).as_posix()) + ').resolve(strict=True)')
build_text = build_text.replace('source.relative_to(program).as_posix()', 'source.as_posix()')
(WORK / 'build_probe.py').write_text(build_text, encoding='utf-8', newline='\n')
(WORK / 'prepared_sources.json').write_text(json.dumps({
    'original_test': str(SOURCE), 'original_sha256': sha(SOURCE),
    'trace_test': str(output), 'trace_sha256': sha(output),
    'trace_header_sha256': sha(WORK / 'report_query_trace.h'),
    'base_builder_sha256': sha(builder), 'prepared_builder_sha256': sha(WORK / 'build_probe.py'),
    'production_modified': False, 'tests_assertions_or_wait_limit_modified': False,
    'observer_effect': 'JSON append and observers may change scheduling; a pass does not identify the old failure cause',
    'compile_or_gui_executed': False}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Prepared diagnostic sources only; no compile/GUI executed')
