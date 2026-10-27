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
        $_.Extension -in @('.o','.cpp','.h') -or $_.Name -eq 'Makefile' -or $_.Name.StartsWith('object_script.')
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
function Get-LinkObjectMap([string]$log,[string]$directory,$rules,$commands,[string]$expectedTarget) {
    if (!$expectedTarget -or ![IO.Path]::GetExtension($expectedTarget).Equals('.exe',[StringComparison]::OrdinalIgnoreCase)) { throw 'Explicit expected executable target required' }
    $expectedOutput=if([IO.Path]::IsPathRooted($expectedTarget)){[IO.Path]::GetFullPath($expectedTarget)}else{[IO.Path]::GetFullPath((Join-Path $directory $expectedTarget))}
    $lines=@()
    foreach ($line in (Get-Content -LiteralPath $log)) {
        if ($line -notmatch '^(?:[^\s]*[/\\])?g\+\+(?:\.exe)?\s+') { continue }
        $tokens=@([regex]::Matches($line,'"[^"]*"|\S+') | ForEach-Object { $_.Value.Trim('"') })
        # These modes produce objects, assembly, preprocessed text or dependency
        # output. Their -o argument is never evidence of a linked executable.
        if (@($tokens | Where-Object { $_ -cin @('-c','-E','-S','-M','-MM','-fsyntax-only','-###') }).Count) { continue }
        $outputs=@()
        for ($index=1;$index-lt $tokens.Count;++$index) {
            if ($tokens[$index] -ceq '-o') {
                if ($index+1-ge $tokens.Count) { throw 'Compiler output option has no value' }
                ++$index
                $value=$tokens[$index]
                $outputs+=if([IO.Path]::IsPathRooted($value)){[IO.Path]::GetFullPath($value)}else{[IO.Path]::GetFullPath((Join-Path $directory $value))}
            }
        }
        $targetOutputs=@($outputs | Where-Object { [string]::Equals($_,$expectedOutput,[StringComparison]::OrdinalIgnoreCase) })
        if (!$targetOutputs.Count) { continue }
        if ($outputs.Count-ne 1 -or $targetOutputs.Count-ne 1) { throw 'Ambiguous output options for expected linked target' }
        $lines+=$line
    }
    if ($lines.Count -ne 1) { throw 'Expected one actual final linker command for this target' }
    $arguments=@([regex]::Matches($lines[0],'"[^"]*"|\S+') | ForEach-Object { $_.Value.Trim('"') })
    $expanded=@();$responses=@()
    foreach ($argument in $arguments) {
        if ($argument.StartsWith('@')) {
            $path=[IO.Path]::GetFullPath((Join-Path $directory $argument.Substring(1)))
            if (!$path.StartsWith([IO.Path]::GetFullPath($directory)+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Link response escaped this fresh target directory' }
            $responses+=Get-FileRecord $path
            $expanded+=@([regex]::Matches((Get-Content -LiteralPath $path -Raw),'"[^"]*"|\S+') | ForEach-Object { $_.Value.Trim('"') })
        } else { $expanded+=$argument }
    }
    $objects=@($expanded | Where-Object { $_.EndsWith('.o') } | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $directory $_)).Replace('\','/') })
    $expected=@($rules | ForEach-Object { $_.object } | Sort-Object)
    if ($objects.Count -ne $expected.Count -or @($objects | Sort-Object -Unique).Count -ne $expected.Count -or
        (($objects | Sort-Object) -join "`n") -cne ($expected -join "`n")) { throw 'Actual linked objects differ from exactly this target fresh rules' }
    return [ordered]@{expected_output=$expectedOutput;command=$lines[0];response_inputs=$responses;objects=@($objects | ForEach-Object {
        $object=$_;$source=@($commands | Where-Object { $_.object -ceq $object })
        if ($source.Count -ne 1) { throw 'Linked object has no unique fresh source' }
        [ordered]@{object=$object;source=$source[0].source;sha256=(Get-FileRecord $object).sha256}
    })}
}
