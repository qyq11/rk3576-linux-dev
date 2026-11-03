param([Parameter(Mandatory=$true)][string]$SambaPassword)
$ErrorActionPreference = 'Stop'
$repoRoot = 'C:\Users\52400\Desktop\internship\seerain\20260907_RK3576_Linux'
$sharePath = '\\192.168.100.123\rkshare'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoardASambaNative {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct NETRESOURCE {
        public int dwScope, dwType, dwDisplayType, dwUsage;
        public string lpLocalName, lpRemoteName, lpComment, lpProvider;
    }
    [DllImport("mpr.dll", CharSet=CharSet.Unicode)]
    public static extern int WNetAddConnection2(ref NETRESOURCE resource, string password, string userName, int flags);
    [DllImport("mpr.dll", CharSet=CharSet.Unicode)]
    public static extern int WNetCancelConnection2(string name, int flags, bool force);
}
'@
$resource = [BoardASambaNative+NETRESOURCE]::new()
$resource.dwType = 1
$resource.lpRemoteName = $sharePath
$connectionResult = [BoardASambaNative]::WNetAddConnection2([ref]$resource, $SambaPassword, 'root', 0)
$SambaPassword = $null
"WINDOWS_SMB_CONNECT_RESULT=$connectionResult"
if ($connectionResult -ne 0) {
    throw [System.ComponentModel.Win32Exception]::new($connectionResult)
}
$checkName = '.samba_root_check_20261102_' + [guid]::NewGuid().ToString('N')
$checkDir = Join-Path $sharePath $checkName
$pcFile = Join-Path $checkDir 'pc_to_rk.txt'
$rkFile = Join-Path $checkDir 'rk_to_pc.txt'
$created = $false
try {
    $entryCount = @([System.IO.Directory]::EnumerateFileSystemEntries($sharePath)).Count
    "WINDOWS_SHARE_LIST=PASS entries=$entryCount"
    if ([System.IO.Directory]::Exists($checkDir)) { throw 'Temporary check directory already exists' }
    [System.IO.Directory]::CreateDirectory($checkDir) | Out-Null
    $created = $true
    [System.IO.File]::WriteAllText($pcFile, "PC_TO_RK_SAMBA_ROOT_20261102`n", [System.Text.UTF8Encoding]::new($false))
    $commandTemplate = @'
set -eu; test "$(tr -d "\000" </proc/device-tree/serial-number)" = 3bf8222fe1417f6a; test "$(cat /home/rkshare/__CHECK_NAME__/pc_to_rk.txt)" = PC_TO_RK_SAMBA_ROOT_20261102; printf "PC_TO_RK_CONTENT=PASS\n"; printf "RK_TO_PC_SAMBA_ROOT_20261102\n" > /home/rkshare/__CHECK_NAME__/rk_to_pc.txt; stat -c "%A %U:%G %n" /home/rkshare/__CHECK_NAME__ /home/rkshare/__CHECK_NAME__/pc_to_rk.txt; smbstatus -S
'@
    $command = $commandTemplate.Replace('__CHECK_NAME__', $checkName)
    & ssh -T -o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=yes -o HostKeyAlgorithms=ssh-ed25519 -o IdentitiesOnly=yes -i "$env:USERPROFILE\.ssh\id_ed25519" root@192.168.100.123 $command
    if ($LASTEXITCODE -ne 0) { throw 'Board A content check failed' }
    if ([System.IO.File]::ReadAllText($rkFile) -cne "RK_TO_PC_SAMBA_ROOT_20261102`n") {
        throw 'RK to Windows content mismatch'
    }
    'RK_TO_WINDOWS_CONTENT=PASS'
    'WINDOWS_ROOT_SMB_READ_WRITE=PASS'
} finally {
    if ($created) {
        if ([System.IO.File]::Exists($pcFile)) { [System.IO.File]::Delete($pcFile) }
        if ([System.IO.File]::Exists($rkFile)) { [System.IO.File]::Delete($rkFile) }
        [System.IO.Directory]::Delete($checkDir, $false)
        'OWN_TEMPORARY_FILES_REMOVED=PASS'
    }
    $cancelResult = [BoardASambaNative]::WNetCancelConnection2($sharePath, 0, $false)
    "OWN_TEMPORARY_SMB_CONNECTION_CLOSED=$cancelResult"
}
