$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path
$taskQt = 'C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32'
$taskMake = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin/mingw32-make.exe'
$env:PATH = 'C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin;' + $taskQt + '/bin;' + $env:PATH
foreach ($taskName in @('renderer','history')) {
    $taskDir = Join-Path $PSScriptRoot ('windows/' + $taskName)
    New-Item -ItemType Directory -Force $taskDir | Out-Null
    Push-Location $taskDir
    try {
        & ($taskQt + '/bin/qmake.exe') (Join-Path $taskRoot ('06_programs/sn100_host/history/' + $taskName + '.pro')) -spec win32-g++ CONFIG+=release CONFIG-=debug *> (Join-Path $PSScriptRoot ('windows_' + $taskName + '_build.log'))
        if ($LASTEXITCODE -ne 0) { throw ('qmake failed ' + $taskName) }
        & $taskMake -j4 release *>> (Join-Path $PSScriptRoot ('windows_' + $taskName + '_build.log'))
        if ($LASTEXITCODE -ne 0) { throw ('build failed ' + $taskName) }
    } finally { Pop-Location }
}
