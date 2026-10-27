function Save-Json($value, $path) { $value | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding utf8 }
function Get-FileRecord($path) {
    $item = Get-Item -LiteralPath $path
    return [ordered]@{ path = $item.FullName.Replace('\','/'); size = $item.Length; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
}
function Get-SourceSnapshot {
    $files = & rg --files --hidden --no-ignore $sourceRoot
    if ($LASTEXITCODE -ne 0) { throw 'rg source inventory failed' }
    return @($files | Sort-Object | ForEach-Object { Get-FileRecord $_ })
}
function Invoke-Logged($exe, [string[]]$arguments, $directory, $log) {
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $exe
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $start.WorkingDirectory = $directory
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $record = [ordered]@{ executable=$exe; arguments=$arguments; cwd=$directory; started_utc=[DateTime]::UtcNow.ToString('o') }
    Save-Json $record ($log + '.command.json')
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $start
    $started = $false
    $actualExit = $null
    $stdout = $null
    $stderr = $null
    $failureMessage = $null
    $ownedTreeKilled = $false
    $cleanupError = $null
    try {
        if (!$process.Start()) { throw "Cannot start native build process: $exe" }
        $started = $true
        Save-Json ([ordered]@{pid=$process.Id;status='running';started_utc=$record.started_utc}) ($log + '.process.json')
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
    } catch {
        $failureMessage = $_.Exception.Message
        throw
    } finally {
        # Only the process tree started by this invocation is eligible for
        # cleanup. An interrupted parser/wait must not lose the child receipt.
        try {
            if ($started -and !$process.HasExited) {
                $process.Kill($true)
                $ownedTreeKilled = $true
                if (!$process.WaitForExit(10000)) { throw 'Owned native child exit still unknown after cleanup' }
            }
        } catch { $cleanupError = $_.Exception.Message }
        $childPid = if ($started) { $process.Id } else { $null }
        if ($started -and $process.HasExited) { $actualExit = $process.ExitCode }
        $state = if (!$started) { 'not_started' } elseif ($process.HasExited) { 'finished' } else { 'exit_unknown' }
        Save-Json ([ordered]@{exit_code=$actualExit;pid=$childPid;state=$state;
            owned_tree_killed=$ownedTreeKilled;cleanup_error=$cleanupError;error=$failureMessage;
            finished_utc=[DateTime]::UtcNow.ToString('o')}) ($log + '.exit.json')
        if ($null -ne $stdout -and $null -ne $stderr -and $state -eq 'finished') {
            $text = "[stdout]`r`n" + $stdout.Result + "`r`n[stderr]`r`n" + $stderr.Result
            [IO.File]::WriteAllText($log, $text, (New-Object Text.UTF8Encoding($false)))
        }
        Save-Json ([ordered]@{pid=$childPid;status=$state;exit_code=$actualExit;
            started_utc=$record.started_utc;finished_utc=[DateTime]::UtcNow.ToString('o')}) ($log + '.process.json')
        $process.Dispose()
    }
    return $actualExit
}

function Get-MakefileSources($dir) {
    $found = @{}
    foreach ($makefile in @('Makefile','Makefile.Release')) {
        $path = Join-Path $dir $makefile
        if (!(Test-Path -LiteralPath $path)) { continue }
        $body = Get-Content -LiteralPath $path -Raw
        foreach ($match in [regex]::Matches($body, '[^\s\x22]+')) {
            $token = $match.Value.TrimEnd(':').Trim('"')
            if ($token -eq '\' -or $token.Contains('$') -or $token.StartsWith('-')) { continue }
            try {
                $candidate = if ([IO.Path]::IsPathRooted($token)) { [IO.Path]::GetFullPath($token) } else { [IO.Path]::GetFullPath((Join-Path $dir $token)) }
                if ($candidate.StartsWith([IO.Path]::GetFullPath($sourceRoot) + '\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
                    $found[$candidate.Replace('\','/')] = $true
                }
            } catch { }
        }
    }
    return @($found.Keys | Sort-Object)
}
function Get-PeRecord($path) {
    $record = Get-FileRecord $path
    $bytes = [IO.File]::ReadAllBytes($path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a -or [BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550) { throw "Invalid PE: $path" }
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    $optional = [BitConverter]::ToUInt16($bytes, $peOffset + 24)
    $record.machine_hex = ('0x{0:x4}' -f $machine)
    $record.optional_header_hex = ('0x{0:x4}' -f $optional)
    $record.architecture = if ($machine -eq 0x14c -and $optional -eq 0x10b) { 'PE32 / Intel i386 / 32-bit Windows' } else { 'unexpected' }
    if ($machine -ne 0x14c -or $optional -ne 0x10b) { throw "Wrong architecture: $path" }
    return $record
}
