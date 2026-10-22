[CmdletBinding()]
param(
    [ValidatePattern('^COM\d+$')]
    [string]$PortName = 'COM18',
    [int]$BaudRate = 1500000
)

$ErrorActionPreference = 'Stop'
$testPrefix = Join-Path $PSScriptRoot ((Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '_' + $PortName + '_双向验证')
$testMarker = 'UART_BIDIR_OK_3576_' + (Get-Date -Format 'HHmmss')
$testCommand = "printf '\n$testMarker\n'; tty; uname -m"
$testResult = [ordered]@{
    StartedAt = [DateTimeOffset]::Now.ToString('o')
    Port = $PortName
    BaudRate = $BaudRate
    Format = '8N1'
    FlowControl = 'None'
    Command = $testCommand
    ExpectedMarker = $testMarker
    Opened = $false
    TransmittedByteCount = 0
    ReceivedByteCount = 0
    Passed = $false
    Error = $null
}
$testEvents = [System.Collections.Generic.List[string]]::new()
$testReceived = [System.IO.MemoryStream]::new()
$testPort = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$testPort.Handshake = [System.IO.Ports.Handshake]::None
$testPort.DtrEnable = $false
$testPort.RtsEnable = $false
$testPort.ReadTimeout = 200
$testPort.WriteTimeout = 1000

try {
    $testPort.Open()
    $testResult.Opened = $true
    $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) OPEN $PortName $BaudRate 8N1 flow=None")
    $testTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $testBuffer = [byte[]]::new(8192)
    $testEnterSent = $false
    $testCommandSent = $false
    while ($testTimer.Elapsed.TotalSeconds -lt 8) {
        if (-not $testEnterSent) {
            $testPort.Write([byte[]]@(13), 0, 1)
            $testEnterSent = $true
            $testResult.TransmittedByteCount += 1
            $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) TX Enter")
        }
        if (-not $testCommandSent -and $testTimer.Elapsed.TotalSeconds -ge 1) {
            $testPayload = [System.Text.Encoding]::ASCII.GetBytes($testCommand + "`r")
            $testPort.Write($testPayload, 0, $testPayload.Length)
            $testCommandSent = $true
            $testResult.TransmittedByteCount += $testPayload.Length
            $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) TX $testCommand")
        }
        if ($testPort.BytesToRead -gt 0) {
            $testCount = $testPort.Read($testBuffer, 0, [Math]::Min($testBuffer.Length, $testPort.BytesToRead))
            $testReceived.Write($testBuffer, 0, $testCount)
            $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) RX $testCount bytes")
        }
        Start-Sleep -Milliseconds 25
    }
}
catch {
    $testResult.Error = $_.Exception.GetBaseException().Message
    $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) ERROR $($testResult.Error)")
}
finally {
    try { if ($testPort.IsOpen) { $testPort.Close() } }
    finally { $testPort.Dispose() }
    $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) CLOSED")
}

$testBytes = $testReceived.ToArray()
$testReceived.Dispose()
$testText = [System.Text.Encoding]::UTF8.GetString($testBytes)
$testCleanText = [regex]::Replace($testText, '\x1B\[[0-?]*[ -/]*[@-~]', '').Replace("`r", '')
$testLines = $testCleanText -split "`n"
$testResult.ReceivedByteCount = $testBytes.Length
$testResult.MarkerReturnedAsOutputLine = $testLines -contains $testMarker
$testResult.SerialTTYReturnedAsOutputLine = $testLines -contains '/dev/ttyFIQ0'
$testResult.ArchitectureReturnedAsOutputLine = $testLines -contains 'aarch64'
$testResult.Passed = $null -eq $testResult.Error -and $testResult.MarkerReturnedAsOutputLine -and $testResult.SerialTTYReturnedAsOutputLine -and $testResult.ArchitectureReturnedAsOutputLine
$testResult.CompletedAt = [DateTimeOffset]::Now.ToString('o')
$testResult.ReceivedText = $testCleanText
$testResult.ResultFile = $testPrefix + '.json'
$testResult.LogFile = $testPrefix + '.log'
[System.IO.File]::WriteAllBytes($testPrefix + '.rx.bin', $testBytes)
[System.IO.File]::WriteAllText($testPrefix + '.rx.txt', $testCleanText, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllLines($testResult.LogFile, $testEvents, [System.Text.UTF8Encoding]::new($false))
$testJson = $testResult | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($testResult.ResultFile, $testJson, [System.Text.UTF8Encoding]::new($false))
Write-Output $testJson
