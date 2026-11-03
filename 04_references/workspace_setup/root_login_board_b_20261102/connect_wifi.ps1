# 只读取电脑当前503网络的保存配置，在内存取出密码，经加密SSH标准输入使用。
$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoardBWifiNative {
    [DllImport("wlanapi.dll")]
    public static extern uint WlanOpenHandle(uint clientVersion, IntPtr reserved, out uint negotiatedVersion, out IntPtr handle);
    [DllImport("wlanapi.dll", CharSet=CharSet.Unicode)]
    public static extern uint WlanGetProfile(IntPtr handle, ref Guid interfaceGuid, string profileName, IntPtr reserved, out IntPtr profileXml, ref uint flags, out uint grantedAccess);
    [DllImport("wlanapi.dll")]
    public static extern void WlanFreeMemory(IntPtr memory);
    [DllImport("wlanapi.dll")]
    public static extern uint WlanCloseHandle(IntPtr handle, IntPtr reserved);
}
'@
$handle=[IntPtr]::Zero
$xmlPointer=[IntPtr]::Zero
$secret=$null
$process=$null
try {
    [uint32]$version=0
    $result=[BoardBWifiNative]::WlanOpenHandle(2,[IntPtr]::Zero,[ref]$version,[ref]$handle)
    if ($result -ne 0) { throw [ComponentModel.Win32Exception]::new([int]$result) }
    $interface=[guid]'ee64b3b7-fd48-45be-b9de-d51591de8e23'
    [uint32]$flags=4
    [uint32]$access=0
    $result=[BoardBWifiNative]::WlanGetProfile($handle,[ref]$interface,'503',[IntPtr]::Zero,[ref]$xmlPointer,[ref]$flags,[ref]$access)
    if ($result -ne 0) { throw [ComponentModel.Win32Exception]::new([int]$result) }
    [xml]$profileXml=[Runtime.InteropServices.Marshal]::PtrToStringUni($xmlPointer)
    $networkName=$profileXml.SelectSingleNode("//*[local-name()='SSIDConfig']/*[local-name()='SSID']/*[local-name()='name']").InnerText
    $protection=$profileXml.SelectSingleNode("//*[local-name()='sharedKey']/*[local-name()='protected']").InnerText
    if ($networkName -cne '503' -or $protection -cne 'false') { throw '本机网络配置不能直接用于目标WiFi。' }
    $secret=$profileXml.SelectSingleNode("//*[local-name()='sharedKey']/*[local-name()='keyMaterial']").InnerText
    if ([string]::IsNullOrEmpty($secret) -or $secret.Contains("`r") -or $secret.Contains("`n")) { throw 'WiFi密码读取格式异常。' }
    'CURRENT_WINDOWS_WIFI_PROFILE_READ=503'
    $start=[Diagnostics.ProcessStartInfo]::new('C:/Windows/System32/OpenSSH/ssh.exe')
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardInput=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    foreach($argument in @('-T','-o','BatchMode=yes','-o','ConnectTimeout=8','-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile=C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux/temp/sn100_host/board_b/20261102_deploy/known_hosts','root@192.168.100.123','bash /run/board_b_connect_wifi_20261102.sh')) {
        $start.ArgumentList.Add($argument)
    }
    $process=[Diagnostics.Process]::new()
    $process.StartInfo=$start
    [void]$process.Start()
    $stdout=$process.StandardOutput.ReadToEndAsync()
    $stderr=$process.StandardError.ReadToEndAsync()
    # NetworkManager需要“密码字段名:值”这一行；不生成临时密码文件或带密码命令。
    $process.StandardInput.Write('802-11-wireless-security.psk:'+$secret+"`n")
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(50000)) { $process.Kill($true); throw 'WiFi连接等待超时。' }
    $safe=($stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult()).Replace($secret,'[REDACTED]')
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'wifi_connect.log'),$safe,[Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $PSScriptRoot 'wifi_connect_exit.txt'),[string]$process.ExitCode)
    if ($process.ExitCode -ne 0) { throw 'WiFi连接失败，已保存不含密码的输出。' }
    $safe
} finally {
    if ($process) {
        if (-not $process.HasExited) { $process.Kill($true) }
        $process.Dispose()
    }
    if ($xmlPointer -ne [IntPtr]::Zero) { [BoardBWifiNative]::WlanFreeMemory($xmlPointer) }
    if ($handle -ne [IntPtr]::Zero) { [void][BoardBWifiNative]::WlanCloseHandle($handle,[IntPtr]::Zero) }
    $profileXml=$null
    $secret=$null
}
