# 用板B的本机加密凭据登录共享，核对双向文件内容，保留可供资源管理器使用的连接。
$ErrorActionPreference='Stop'
$repoRoot='C:/Users/52400/Desktop/internship/seerain/20260907_RK3576_Linux'
$sharePath='\\192.168.3.59\rkshare'
$credential=Import-Clixml -LiteralPath 'C:/Users/52400/.rk3576-local/credentials/board_b/root.credential.clixml'
if ($credential.UserName -cne 'root') { throw '凭据账户不是root。' }
$secret=$credential.GetNetworkCredential().Password
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class BoardBSambaNative {
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct NETRESOURCE {
        public int dwScope, dwType, dwDisplayType, dwUsage;
        public string lpLocalName, lpRemoteName, lpComment, lpProvider;
    }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    public struct CREDENTIAL {
        public uint Flags, Type;
        public string TargetName, Comment;
        public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
        public uint CredentialBlobSize;
        public IntPtr CredentialBlob;
        public uint Persist, AttributeCount;
        public IntPtr Attributes;
        public string TargetAlias, UserName;
    }
    [DllImport("mpr.dll", CharSet=CharSet.Unicode)]
    public static extern int WNetAddConnection2(ref NETRESOURCE resource, string password, string userName, int flags);
    [DllImport("mpr.dll", CharSet=CharSet.Unicode)]
    public static extern int WNetCancelConnection2(string name, int flags, bool force);
    [DllImport("advapi32.dll", EntryPoint="CredWriteW", CharSet=CharSet.Unicode, SetLastError=true)]
    public static extern bool CredWrite(ref CREDENTIAL credential, uint flags);
}
'@
foreach($remotePath in @($sharePath,'\\192.168.3.59\IPC$')) {
    $result=[BoardBSambaNative]::WNetCancelConnection2($remotePath,0,$false)
    "OLD_TARGET_CONNECTION_CLOSE_RESULT=$result"
    if ($result -notin @(0,2250)) { throw [ComponentModel.Win32Exception]::new($result) }
}
$resource=[BoardBSambaNative+NETRESOURCE]::new()
$resource.dwType=1
$resource.lpRemoteName=$sharePath
$result=[BoardBSambaNative]::WNetAddConnection2([ref]$resource,$secret,'root',0)
$secret=$null
$credential=$null
"WINDOWS_ROOT_SMB_CONNECT_RESULT=$result"
if ($result -ne 0) { throw [ComponentModel.Win32Exception]::new($result) }
$checkName='.samba_board_b_wifi_check_20261102_'+[guid]::NewGuid().ToString('N')
$checkDir=Join-Path $sharePath $checkName
$pcFile=Join-Path $checkDir 'pc_to_rk.txt'
$rkFile=Join-Path $checkDir 'rk_to_pc.txt'
$created=$false
try {
    $entryCount=@([IO.Directory]::EnumerateFileSystemEntries($sharePath)).Count
    "WINDOWS_SHARE_LIST=PASS entries=$entryCount"
    if ([IO.Directory]::Exists($checkDir)) { throw '本轮临时检查目录已存在。' }
    [IO.Directory]::CreateDirectory($checkDir) | Out-Null
    $created=$true
    [IO.File]::WriteAllText($pcFile,"PC_TO_RK_BOARD_B_20261102`n",[Text.UTF8Encoding]::new($false))
    $commandTemplate=@'
set -eu; test "$(tr -d "\000" </proc/device-tree/serial-number)" = 8314b1e8ffd0321f; test "$(cat /home/rkshare/__CHECK__/pc_to_rk.txt)" = PC_TO_RK_BOARD_B_20261102; printf "WINDOWS_TO_BOARD_B_CONTENT=PASS\n"; printf "RK_TO_PC_BOARD_B_20261102\n" > /home/rkshare/__CHECK__/rk_to_pc.txt; stat -c "%A %U:%G %n" /home/rkshare/__CHECK__ /home/rkshare/__CHECK__/pc_to_rk.txt; smbstatus -S
'@
    $command=$commandTemplate.Replace('__CHECK__',$checkName)
    & ssh -T -o BatchMode=yes -o ConnectTimeout=6 -o StrictHostKeyChecking=yes -o "UserKnownHostsFile=$repoRoot/temp/sn100_host/board_b/20261102_deploy/known_hosts" -o HostKeyAlias=192.168.100.123 root@192.168.3.59 $command
    if ($LASTEXITCODE -ne 0) { throw '板B核对失败。' }
    if ([IO.File]::ReadAllText($rkFile) -cne "RK_TO_PC_BOARD_B_20261102`n") { throw '板B到Windows文件内容不一致。' }
    'BOARD_B_TO_WINDOWS_CONTENT=PASS'
    'WINDOWS_ROOT_SMB_WIFI_READ_WRITE=PASS'
    if (-not [IO.File]::Exists((Join-Path $sharePath 'programs/sn100_shell/current/sn100_shell'))) { throw '共享中未找到上位机主程序。' }
    'APPLICATION_EXECUTABLE_VISIBLE_IN_SHARE=1'
} finally {
    if ($created) {
        if ([IO.File]::Exists($pcFile)) { [IO.File]::Delete($pcFile) }
        if ([IO.File]::Exists($rkFile)) { [IO.File]::Delete($rkFile) }
        [IO.Directory]::Delete($checkDir,$false)
        'ONLY_OWN_TEST_FILES_REMOVED=1'
    }
}
'ROOT_SMB_CONNECTION_LEFT_AVAILABLE=1'
