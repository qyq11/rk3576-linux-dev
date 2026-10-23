$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
& (Join-Path $PSScriptRoot 'build_windows_pdf.ps1')
$taskPython = 'C:/Users/52400/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$taskRenderer = Join-Path $PSScriptRoot 'windows/renderer/sn100_report_renderer.exe'
if (!(Test-Path -LiteralPath $taskRenderer)) { $taskRenderer = Join-Path $PSScriptRoot 'windows/renderer/release/sn100_report_renderer.exe' }
$taskValidator = Join-Path $taskRoot '06_programs/sn100_host/tests/verify_history_pdf_text.py'
$taskOutput = Join-Path $PSScriptRoot 'windows_reports_tuned_final'
New-Item -ItemType Directory $taskOutput | Out-Null
$env:QT_QPA_FONTDIR = Join-Path $taskRoot 'temp/sn100_host/history/windows_report_compare/noto_font_source'
foreach ($taskCase in @('one_page_alias_pairs_quotes_nonbmp','five_pages_64_rows','latin_ligatures','decomposed_acute','precomposed_acute','both_acute_forms')) {
    $taskSnapshot = Join-Path $PSScriptRoot ('exact_final/' + $taskCase + '.json')
    $taskPdf = Join-Path $taskOutput ($taskCase + '.pdf')
    & $taskRenderer $taskSnapshot $taskPdf *> (Join-Path $taskOutput ($taskCase + '_renderer.txt'))
    if ($LASTEXITCODE -ne 0) { throw ('Windows Noto rendering failed ' + $taskCase) }
    $taskPages = if ($taskCase -eq 'five_pages_64_rows') { 5 } else { 1 }
    & $taskPython $taskValidator --pdf $taskPdf --snapshot $taskSnapshot --expected-pages $taskPages --output (Join-Path $taskOutput ($taskCase + '_text.json'))
    if ($LASTEXITCODE -ne 0) { throw ('Windows Noto exact text failed ' + $taskCase) }
}
Remove-Item Env:QT_QPA_FONTDIR
foreach ($taskCase in @('values','64_tasks')) {
    $taskSnapshotName = if ($taskCase -eq 'values') { 'printable7_snapshot.json' } else { 'rows64_snapshot.json' }
    $taskSnapshot = Join-Path $taskRoot ('04_references/sn100_host/offline_report_20261023/reports/' + $taskSnapshotName)
    $taskPdf = Join-Path $taskOutput ('system_' + $taskCase + '.pdf')
    & $taskRenderer $taskSnapshot $taskPdf *> (Join-Path $taskOutput ('system_' + $taskCase + '_renderer.txt'))
    if ($LASTEXITCODE -ne 0) { throw ('Windows system font rendering failed ' + $taskCase) }
    $taskPages = if ($taskCase -eq 'values') { 1 } else { 5 }
    & $taskPython $taskValidator --pdf $taskPdf --snapshot $taskSnapshot --expected-pages $taskPages --output (Join-Path $taskOutput ('system_' + $taskCase + '_text.json'))
    if ($LASTEXITCODE -ne 0) { throw ('Windows system font exact text failed ' + $taskCase) }
}

