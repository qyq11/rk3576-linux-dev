[CmdletBinding()]
param(
    [ValidatePattern('^COM\d+$')]
    [string]$PortName = 'COM18',
    [ValidateRange(1, 4000000)]
    [int]$BaudRate = 1500000,
    [ValidateRange(3, 180)]
    [int]$Seconds = 15,
    [switch]$SendEnter
)

$ErrorActionPreference = 'Stop'
$testPrefix = Join-Path $PSScriptRoot ((Get-Date -Format 'yyyyMMdd_HHmmss_fff') + '_' + $PortName + '_' + $BaudRate)
$testEvents = [System.Collections.Generic.List[string]]::new()
$testReceived = [System.IO.MemoryStream]::new()
$testPort = [System.IO.Ports.SerialPort]::new($PortName, $BaudRate, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$testPort.Handshake = [System.IO.Ports.Handshake]::None
$testPort.DtrEnable = $false
$testPort.RtsEnable = $false
$testPort.ReadTimeout = 200
$testPort.WriteTimeout = 1000
$testPort.ReadBufferSize = 65536
$testResult = [ordered]@{
    StartedAt = [DateTimeOffset]::Now.ToString('o')
    Port = $PortName
    RequestedBaudRate = $BaudRate
    Format = '8N1'
    FlowControl = 'None'
    Opened = $false
    TransmittedByteCount = 0
    ReceivedByteCount = 0
    Error = $null
}

try {
    $testPort.Open()
    $testResult.Opened = $testPort.IsOpen
    $testResult.AcceptedBaudRate = $testPort.BaudRate
    $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) OPEN $PortName $BaudRate 8N1 flow=None DTR=False RTS=False")
    Write-Output "OPENED $PortName at $BaudRate baud; listening for $Seconds seconds."
    $testTimer = [System.Diagnostics.Stopwatch]::StartNew()
    $testBuffer = [byte[]]::new(8192)
    $testNextEnter = 0.0
    $testEnterCount = 0

    while ($testTimer.Elapsed.TotalSeconds -lt $Seconds) {
        if ($SendEnter -and $testEnterCount -lt 3 -and $testTimer.Elapsed.TotalSeconds -ge $testNextEnter) {
            $testPort.Write([byte[]]@(13), 0, 1)
            $testResult.TransmittedByteCount += 1
            $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) TX 0D (Enter)")
            $testEnterCount += 1
            $testNextEnter += 3.0
        }

        if ($testPort.BytesToRead -gt 0) {
            $testCount = $testPort.Read($testBuffer, 0, [Math]::Min($testBuffer.Length, $testPort.BytesToRead))
            if ($testCount -gt 0) {
                $testReceived.Write($testBuffer, 0, $testCount)
                $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) RX $testCount bytes")
            }
        }
        Start-Sleep -Milliseconds 25
    }
    $testTimer.Stop()
}
catch {
    $testResult.Error = $_.Exception.GetBaseException().Message
    $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) ERROR $($testResult.Error)")
}
finally {
    try {
        if ($testPort.IsOpen) { $testPort.Close() }
    }
    finally {
        $testPort.Dispose()
    }
    $testEvents.Add("$([DateTimeOffset]::Now.ToString('o')) CLOSED")
}

$testBytes = $testReceived.ToArray()
$testReceived.Dispose()
$testResult.ReceivedByteCount = $testBytes.Length
$testResult.CompletedAt = [DateTimeOffset]::Now.ToString('o')
$testResult.Outcome = if ($testResult.Error) { 'OpenOrIOError' } elseif ($testBytes.Length -eq 0) { 'NoDataReceived' } else { 'DataReceived' }
if ($testBytes.Length -gt 0) {
    [System.IO.File]::WriteAllBytes($testPrefix + '.rx.bin', $testBytes)
    $testText = [System.Text.Encoding]::UTF8.GetString($testBytes)
    $testResult.ReceivedPreview = $testText.Substring([Math]::Max(0, $testText.Length - 2048))
    $testResult.ReceivedHexPrefix = [BitConverter]::ToString($testBytes, 0, [Math]::Min(64, $testBytes.Length))
}
$testResult.ResultFile = $testPrefix + '.json'
$testResult.LogFile = $testPrefix + '.log'
$testJson = $testResult | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText($testResult.ResultFile, $testJson, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllLines($testResult.LogFile, $testEvents, [System.Text.UTF8Encoding]::new($false))
Write-Output $testJson
