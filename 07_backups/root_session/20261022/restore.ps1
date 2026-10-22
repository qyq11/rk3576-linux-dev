$ErrorActionPreference = 'Stop'
if (Get-Process -Name WindTerm -ErrorAction SilentlyContinue) { throw 'Close WindTerm first.' }
$snapshot = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'before.json') -Raw | ConvertFrom-Json
$sessionFile = $snapshot.source
$rawBefore = [System.IO.File]::ReadAllText($sessionFile)
$sessions = @($rawBefore | ConvertFrom-Json)
$targets = @($sessions | Where-Object { $_.'session.uuid' -eq $snapshot.sessionUuid })
if ($targets.Count -ne 1) { throw 'Target session not found or not unique.' }
$target = $targets[0]
if ($target.'session.autoExecution' -ne 'sudo -n -i \r' -or $target.'session.autoExecutionDelay' -ne 1) { throw 'Automatic-command settings changed; review before restoring.' }
foreach ($field in $snapshot.fields) {
    if ($field.existed) { $target | Add-Member -NotePropertyName $field.name -NotePropertyValue $field.value -Force }
    else { $target.PSObject.Properties.Remove($field.name) }
}
$rawAfter = ConvertTo-Json -InputObject $sessions -Depth 100
if ([System.IO.File]::ReadAllText($sessionFile) -cne $rawBefore) { throw 'Settings changed during preparation.' }
[System.IO.File]::WriteAllText($sessionFile, $rawAfter + "`n", [System.Text.UTF8Encoding]::new($false))
Write-Output 'RESTORED_AUTOMATIC_COMMAND_FIELDS_ONLY'
