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
function Invoke-Logged($exe, [string[]]$arguments, $directory, $log, [int]$timeoutSeconds=180) {
    if ($timeoutSeconds -le 0) { throw 'A finite positive child timeout is required' }
    $record = [ordered]@{ timeout_seconds=$timeoutSeconds; requested_executable=$exe;executable=$exe; arguments=$arguments; cwd=$directory;
        stdout_log=$log;stderr_log=($log+'.stderr.log');started_utc=[DateTime]::UtcNow.ToString('o') }
    Save-Json $record ($log + '.command.json')
    $jobCalled=$false
    try {
        $application=@(Get-Command -Name $exe -CommandType Application -ErrorAction Stop)
        if ($application.Count -ne 1) { throw 'A unique existing application executable is required' }
        $record.executable=[IO.Path]::GetFullPath($application[0].Source)
        Save-Json $record ($log + '.command.json')
        if (!('Sn100FrozenBuildJob' -as [type])) {
            # Loaded only by an explicitly executed controller, never by preparation.
            Add-Type -Path (Join-Path $PSScriptRoot 'native_job.cs')
        }
        $jobCalled=$true
        $native = [Sn100FrozenBuildJob]::Run($record.executable,$arguments,$directory,$log,$timeoutSeconds)
    } catch {
        $state=if($jobCalled){'helper_failed_exit_unobserved'}else{'not_started'}
        Save-Json ([ordered]@{exit_code=$null;state=$state;error=$_.Exception.Message;
            finished_utc=[DateTime]::UtcNow.ToString('o')}) ($log+'.exit.json')
        throw
    }
    $state = if (!$native.Started) { 'not_started' } elseif ($null -ne $native.ExitCode) { 'finished' } else { 'exit_unknown' }
    # Persist the real child result before any log/source/COFF/import parser.
    $exitRecord = [ordered]@{exit_code=$native.ExitCode;pid=$native.Pid;state=$state;stdout_log=$log;stderr_log=($log+'.stderr.log');
        timed_out=$native.TimedOut;owned_tree_killed=$native.OwnedTreeKilled;
        assigned_before_resume=$native.AssignedBeforeResume;owned_job_stopped=$native.OwnedJobStopped;
        active_processes_after=$native.ActiveProcessesAfter;cleanup_error=$native.CleanupError;error=$native.Error;
        finished_utc=[DateTime]::UtcNow.ToString('o')}
    Save-Json $exitRecord ($log + '.exit.json')
    Save-Json ([ordered]@{pid=$native.Pid;status=$state;exit_code=$native.ExitCode;
        owned_job_stopped=$native.OwnedJobStopped;started_utc=$record.started_utc;finished_utc=$exitRecord.finished_utc}) ($log + '.process.json')
    if ($native.Error -or $native.CleanupError -or !$native.OwnedJobStopped -or $native.TimedOut -or $native.OwnedTreeKilled -or $null -eq $native.ExitCode) {
        throw ('Owned native child failed or stop is unproven; original receipt: '+$log+'.exit.json')
    }
    return $native.ExitCode
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
