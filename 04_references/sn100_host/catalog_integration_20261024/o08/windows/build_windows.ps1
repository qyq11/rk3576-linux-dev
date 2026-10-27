param([ValidateSet('o07','final')][string]$Phase='o07')
$ErrorActionPreference='Stop'
$workspace=(Resolve-Path (Join-Path $PSScriptRoot '../../../../')).Path
$program=Join-Path $workspace '06_programs/sn100_host'
$integration=Join-Path $workspace 'temp/sn100_host/catalog_integration'
$logs=Join-Path $workspace 'temp/sn100_host/logs'
$qtBin='C:/Qt5.14/Qt5.14.2/5.14.2/mingw73_32/bin'
$compilerBin='C:/Qt5.14/Qt5.14.2/Tools/mingw730_32/bin'
$qmake=Join-Path $qtBin 'qmake.exe'
$make=Join-Path $compilerBin 'mingw32-make.exe'
$savedPath=$env:Path
$env:Path=$qtBin+';'+$compilerBin+';'+$savedPath
New-Item -ItemType Directory -Force $logs | Out-Null
$results=@()

function Get-ProjectInputs([string]$BuildDirectory)
{
    $content=Get-Content -LiteralPath (Join-Path $BuildDirectory 'Makefile') -Raw
    $pattern='(?i)(?:[A-Z]:/|(?:\.\./)+)[^\s"]+?\.(?:cpp|h|ui|qrc|pri|pro)\b'
    $paths=@([regex]::Matches($content,$pattern) | ForEach-Object {
        $candidate=[IO.Path]::GetFullPath((Join-Path $BuildDirectory $_.Value))
        if($candidate.StartsWith($program+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {$candidate}
    } | Sort-Object -Unique)
    if($paths.Count -eq 0) {throw 'No formal project inputs found in the generated Makefile.'}
    # Hash the actual embedded payloads as well as the .qrc descriptions.
    $resourcePaths=@($paths | Where-Object {[IO.Path]::GetExtension($_) -eq '.qrc'} | ForEach-Object {
        $resourceDirectory=[IO.Path]::GetDirectoryName($_)
        [xml]$resourceXml=Get-Content -LiteralPath $_ -Raw
        foreach($entry in $resourceXml.SelectNodes('//file'))
        {
            $resourcePath=[IO.Path]::GetFullPath((Join-Path $resourceDirectory $entry.InnerText))
            if(-not $resourcePath.StartsWith($program+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $resourcePath -PathType Leaf)) {throw "Invalid formal resource input: $resourcePath"}
            $resourcePath
        }
    })
    $paths=@($paths+$resourcePaths | Sort-Object -Unique)
    return @($paths | ForEach-Object {[pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
}

function Get-PEMachine([string]$Path)
{
    $stream=[IO.File]::OpenRead($Path)
    $reader=[IO.BinaryReader]::new($stream)
    try
    {
        if($reader.ReadUInt16() -ne 0x5A4D) {throw 'Missing MZ header.'}
        $stream.Position=0x3C
        $peOffset=$reader.ReadUInt32()
        $stream.Position=$peOffset
        if($reader.ReadUInt32() -ne 0x00004550) {throw 'Missing PE signature.'}
        $machine=$reader.ReadUInt16()
        $stream.Position=$peOffset+24
        $optionalMagic=$reader.ReadUInt16()
        if($machine -ne 0x14C -or $optionalMagic -ne 0x10B) {throw ('Unexpected PE architecture: machine={0:X4}, optional={1:X4}' -f $machine,$optionalMagic)}
        return 'PE32 / i386 / Machine=0x014C / OptionalMagic=0x010B'
    }
    finally {$reader.Dispose();$stream.Dispose()}
}

try
{
    if($Phase -eq 'final')
    {
        foreach($name in @('sn100_shell.exe','sn100_report_renderer.exe'))
        {
            $oldPair=Join-Path $PSScriptRoot $name
            if(Test-Path -LiteralPath $oldPair -PathType Leaf) {Remove-Item -LiteralPath $oldPair}
        }
    }
    foreach($target in @(
        @{name='shell';project='shell/shell.pro';directory='windows_shell';executable='sn100_shell.exe'},
        @{name='renderer';project='history/renderer.pro';directory='windows_renderer';executable='sn100_report_renderer.exe'}))
    {
        $build=Join-Path $integration $target.directory
        $binaryDirectory=Join-Path $build 'bin'
        New-Item -ItemType Directory -Force $build,$binaryDirectory | Out-Null
        $executable=Join-Path $binaryDirectory $target.executable
        $resolvedExecutable=[IO.Path]::GetFullPath($executable)
        if(-not $resolvedExecutable.StartsWith([IO.Path]::GetFullPath($build)+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {throw 'Generated executable left its assigned build directory.'}
        # Only this known generated executable is removed. Failed compilation
        # cannot leave an old executable available for pairing.
        if(Test-Path -LiteralPath $resolvedExecutable) {Remove-Item -LiteralPath $resolvedExecutable}
        $qmakeLog=Join-Path $logs ('windows_catalog_{0}_{1}_qmake.log' -f $Phase,$target.name)
        $makeLog=Join-Path $logs ('windows_catalog_{0}_{1}_make.log' -f $Phase,$target.name)
        $result=[ordered]@{target=$target.name;phase=$Phase;project=$target.project;qmake_exit=$null;make_exit=$null;status='started';qmake_log=$qmakeLog;make_log=$makeLog}
        $results+=,$result
        Push-Location $build
        try
        {
            & $qmake '-nocache' (Join-Path $program $target.project) '-spec' 'win32-g++' 'CONFIG+=release' 'CONFIG-=debug' 'CONFIG-=debug_and_release' ('DESTDIR='+$binaryDirectory.Replace('\','/')) *> $qmakeLog
            $result.qmake_exit=$LASTEXITCODE
            if($result.qmake_exit -ne 0) {throw "qmake failed for $($target.name): $($result.qmake_exit)"}
            $inputs=Get-ProjectInputs $build
            $result.input_manifest=Join-Path $build ('inputs_'+$Phase+'.json')
            $inputs | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $result.input_manifest -Encoding utf8
            if($Phase -eq 'final') {& $make '-B' '-j2' *> $makeLog}
            else {& $make '-j2' *> $makeLog}
            $result.make_exit=$LASTEXITCODE
            if($result.make_exit -ne 0) {throw "make failed for $($target.name): $($result.make_exit)"}
            foreach($inputFile in $inputs)
            {
                if((Get-FileHash -LiteralPath $inputFile.path -Algorithm SHA256).Hash -cne $inputFile.sha256) {throw "Source changed during compilation: $($inputFile.path)"}
            }
            if(-not (Test-Path -LiteralPath $executable -PathType Leaf)) {throw "Fresh executable missing: $executable"}
            $result.architecture=Get-PEMachine $executable
            $result.executable=$executable
            $result.bytes=(Get-Item -LiteralPath $executable).Length
            $result.sha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
            $result.status='passed'
            Write-Output ($target.name+': qmake=0 make=0 '+$result.architecture+' SHA256='+$result.sha256)
        }
        finally {Pop-Location}
    }
    foreach($result in $results)
    {
        $snapshot=Get-Content -LiteralPath $result.input_manifest -Raw | ConvertFrom-Json
        foreach($inputFile in $snapshot)
        {
            if((Get-FileHash -LiteralPath $inputFile.path -Algorithm SHA256).Hash -cne $inputFile.sha256) {throw "Source changed after its target completed: $($inputFile.path)"}
        }
    }
    foreach($result in $results)
    {
        $paired=Join-Path $PSScriptRoot ([IO.Path]::GetFileName($result.executable))
        Copy-Item -LiteralPath $result.executable -Destination $paired -Force
        if((Get-FileHash -LiteralPath $paired -Algorithm SHA256).Hash -cne $result.sha256) {throw 'Paired executable checksum mismatch.'}
        $result.paired_executable=$paired
    }
    $results | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $PSScriptRoot ('windows_'+$Phase+'_result.json')) -Encoding utf8
}
catch
{
    if($results.Count -gt 0) {$results[-1].status='failed';$results[-1].failure=$_.Exception.Message}
    $results | ConvertTo-Json -Depth 7 | Set-Content -LiteralPath (Join-Path $PSScriptRoot ('windows_'+$Phase+'_result.json')) -Encoding utf8
    Write-Output ('WINDOWS_BUILD_FAILED: '+$_.Exception.Message)
    exit 1
}
finally {$env:Path=$savedPath}
