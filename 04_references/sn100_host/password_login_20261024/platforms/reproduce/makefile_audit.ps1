function Resolve-InputPath([string]$token,[string]$directory) {
    $token=$token.Trim().Trim('"').TrimEnd(':')
    if (!$token -or $token -eq '\' -or $token.Contains('$') -or $token.StartsWith('-')) { return $null }
    try {
        $candidate=if ([IO.Path]::IsPathRooted($token)) { [IO.Path]::GetFullPath($token) } else { [IO.Path]::GetFullPath((Join-Path $directory $token)) }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return $candidate.Replace('\','/') }
    } catch { }
    return $null
}
function Get-MakeFields([string]$directory) {
    $body=[regex]::Replace((Get-Content -LiteralPath (Join-Path $directory 'Makefile') -Raw),'\\\r?\n',' ')
    $result=[ordered]@{}
    foreach ($name in @('CC','CXX','DEFINES','CFLAGS','CXXFLAGS','INCPATH','LINKER','LFLAGS','LIBS','QMAKE')) {
        $match=[regex]::Match($body,'(?m)^'+$name+'\s*=\s*([^\r\n]*)')
        if (!$match.Success) { throw "Required Makefile field missing: $name" }
        $result[$name]=$match.Groups[1].Value.Trim()
    }
    return $result
}
function Get-ObjectRules([string]$directory) {
    $body=[regex]::Replace((Get-Content -LiteralPath (Join-Path $directory 'Makefile') -Raw),'\\\r?\n',' ')
    $result=@()
    foreach ($match in [regex]::Matches($body,'(?m)^([^\s:]+\.o):\s*([^\r\n]+)')) {
        $object=[IO.Path]::GetFullPath((Join-Path $directory $match.Groups[1].Value)).Replace('\','/')
        $inputs=@($match.Groups[2].Value.Split(' ',[StringSplitOptions]::RemoveEmptyEntries) | ForEach-Object { Resolve-InputPath $_ $directory } | Where-Object { $_ } | Sort-Object -Unique)
        $result += [ordered]@{object=$object;inputs=$inputs}
    }
    if (!$result.Count) { throw 'No object dependency rules parsed' }
    return $result
}
function Get-MakeExternalInputs([string]$directory) {
    $found=@{}
    $sourcePrefix=[IO.Path]::GetFullPath($sourceRoot).Replace('\','/')+'/'
    $directoryPrefix=[IO.Path]::GetFullPath($directory).Replace('\','/')+'/'
    $body=Get-Content -LiteralPath (Join-Path $directory 'Makefile') -Raw
    foreach ($match in [regex]::Matches($body,'[^\s\x22]+')) {
        $path=Resolve-InputPath $match.Value $directory
        if (!$path) { continue }
        if ($path.StartsWith($sourcePrefix,[StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($path.StartsWith($directoryPrefix,[StringComparison]::OrdinalIgnoreCase)) { continue }
        $found[$path]=$true
    }
    return @($found.Keys | Sort-Object)
}
function Get-BuildArtifacts([string]$directory) {
    return @(Get-ChildItem -LiteralPath $directory -Recurse -File | Where-Object {
        $_.Extension -in @('.o','.cpp','.h') -or $_.Name -eq 'Makefile'
    } | Sort-Object FullName | ForEach-Object {
        $record=Get-FileRecord $_.FullName
        $record.last_write_utc_ticks=$_.LastWriteTimeUtc.Ticks
        $record
    })
}
function Get-CompileCommands([string]$log,[string]$directory) {
    $result=@()
    foreach ($line in (Get-Content -LiteralPath $log)) {
        if ($line -notmatch '^(?:[^\s]*[/\\])?g\+\+(?:\.exe)?\s+-c\s') { continue }
        $match=[regex]::Match($line,'\s-o\s+([^\s]+\.o)\s+([^\r\n]+)$')
        if (!$match.Success) { throw "Cannot audit compiler output/source: $line" }
        $result += [ordered]@{
            command=$line
            object=[IO.Path]::GetFullPath((Join-Path $directory $match.Groups[1].Value)).Replace('\','/')
            source=(Resolve-InputPath $match.Groups[2].Value $directory)
        }
    }
    return $result
}
function Get-Map($records) {
    $result=@{}
    foreach ($record in $records) { $result[$record.path]=$record }
    return $result
}
