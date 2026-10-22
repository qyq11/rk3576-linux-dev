param(
    [ValidateSet('build','run','debug-check')][string]$Action = 'build',
    [string]$QtRoot = 'C:\Qt5.14\Qt5.14.2\5.14.2\mingw73_64',
    [string]$CompilerRoot = 'C:\Qt5.14\Qt5.14.2\Tools\mingw730_64'
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$outputDir = Join-Path $projectRoot 'temp\qt_demo\windows_debug_x64'
$qmake = Join-Path $QtRoot 'bin\qmake.exe'
$make = Join-Path $CompilerRoot 'bin\mingw32-make.exe'
$gdb = Join-Path $CompilerRoot 'bin\gdb.exe'
foreach ($required in @($qmake,$make,$gdb)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required tool not found: $required" }
}
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$previousPath = $env:Path
$previousQpa = $env:QT_QPA_PLATFORM
$previousPlugins = $env:QT_PLUGIN_PATH
$previousPlatformPlugins = $env:QT_QPA_PLATFORM_PLUGIN_PATH
try {
    $env:Path = "$QtRoot\bin;$CompilerRoot\bin;$previousPath"
    $env:QT_QPA_PLATFORM = 'windows'
    $env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = Join-Path $QtRoot 'plugins\platforms'
    Push-Location -LiteralPath $outputDir
    try {
        & $qmake (Join-Path $PSScriptRoot 'qt_demo.pro') -spec win32-g++ 'CONFIG+=debug' 'CONFIG-=release'
        if ($LASTEXITCODE -ne 0) { throw "qmake failed: $LASTEXITCODE" }
        & $make '-j4'
        if ($LASTEXITCODE -ne 0) { throw "Windows build failed: $LASTEXITCODE" }
        $exe = Join-Path $outputDir 'debug\qt_demo.exe'
        if (-not (Test-Path -LiteralPath $exe)) { throw "No Debug executable produced: $exe" }
        Write-Output "WINDOWS_DEBUG_EXE=$exe"
        if ($Action -eq 'run') {
            & $exe --state (Join-Path $outputDir 'state.json') --snapshot (Join-Path $outputDir 'render.png')
            if ($LASTEXITCODE -ne 0) { throw "Application exited: $LASTEXITCODE" }
        } elseif ($Action -eq 'debug-check') {
            $commands = @'
set pagination off
set confirm off
set print pretty on
break main
run --timeout 10 --state state.json --snapshot render.png
printf "\nDEBUG_BREAKPOINT_REACHED\n"
print argc
bt
next
printf "\nDEBUG_SINGLE_STEP_COMPLETE\n"
continue
quit
'@
            [System.IO.File]::WriteAllText((Join-Path $outputDir 'verify.gdb'), $commands, [System.Text.Encoding]::ASCII)
            & $gdb --batch -x verify.gdb $exe 2>&1 | Tee-Object -FilePath (Join-Path $outputDir 'debugger.log')
            if ($LASTEXITCODE -ne 0) { throw "GDB verification failed: $LASTEXITCODE" }
        }
    } finally { Pop-Location }
} finally {
    $env:Path = $previousPath
    $env:QT_QPA_PLATFORM = $previousQpa
    $env:QT_PLUGIN_PATH = $previousPlugins
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $previousPlatformPlugins
}
