// Local controller helper. A suspended child is assigned before it can create
// descendants; the unnamed job is the only process group this helper may stop.
using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

// Windows populates the complete ABI structures, including fields this helper
// does not inspect. Their zero defaults and exact layout are intentional.
#pragma warning disable 0649

public static class Sn100FrozenBuildJob
{
    public sealed class Receipt
    {
        public int? Pid;
        public int? ExitCode;
        public bool Started, TimedOut, AssignedBeforeResume, OwnedTreeKilled, OwnedJobStopped;
        public uint? ActiveProcessesAfter;
        public string Error, CleanupError;
    }
    [StructLayout(LayoutKind.Sequential)] struct Security { public int Length; public IntPtr Descriptor; public int Inherit; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct Startup
    {
        public int cb; public string reserved, desktop, title;
        public int x,y,xSize,ySize,xChars,yChars,fill,flags;
        public short show,reserved2; public IntPtr reservedPtr,input,output,error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process,thread; public uint pid,tid; }
    [StructLayout(LayoutKind.Sequential)] struct BasicLimits
    {
        public long processTime,jobTime; public uint flags;
        public UIntPtr minWorking,maxWorking; public uint activeLimit; public UIntPtr affinity;
        public uint priority,scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct IoCounts { public ulong readOps,writeOps,otherOps,readBytes,writeBytes,otherBytes; }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits
    { public BasicLimits basic; public IoCounts io; public UIntPtr processMemory,jobMemory,peakProcess,peakJob; }
    [StructLayout(LayoutKind.Sequential)] struct Accounting
    { public long user,kernel,periodUser,periodKernel; public uint faults,total,active,terminated; }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attrs,string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int kind,ref ExtendedLimits info,uint length);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int kind,out Accounting info,uint length,IntPtr returned);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateJobObject(IntPtr job,uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool TerminateProcess(IntPtr process,uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint ms);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFile(string path,uint access,uint sharing,ref Security attrs,uint mode,uint flags,IntPtr template);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool CreateProcess(string exe,StringBuilder command,IntPtr processAttrs,IntPtr threadAttrs,bool inherit,uint flags,IntPtr environment,string cwd,ref Startup startup,out ProcessInfo info);
    static void Check(bool ok) { if(!ok) throw new Win32Exception(Marshal.GetLastWin32Error()); }
    static bool Valid(IntPtr handle) { return handle!=IntPtr.Zero && handle!=new IntPtr(-1); }
    static uint Active(IntPtr job)
    {
        Accounting value;Check(QueryInformationJobObject(job,1,out value,(uint)Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero));return value.active;
    }
    static string Quote(string value)
    {
        var result=new StringBuilder("\"");int slashes=0;
        foreach(char c in value)
        {
            if(c=='\\'){++slashes;continue;}
            result.Append('\\',c=='\"'?slashes*2+1:slashes);result.Append(c);slashes=0;
        }
        result.Append('\\',slashes*2);return result.Append('"').ToString();
    }
    public static Receipt Run(string exe,string[] arguments,string cwd,string log,int seconds)
    {
        var result=new Receipt();IntPtr job=IntPtr.Zero,output=IntPtr.Zero,errorOutput=IntPtr.Zero,input=IntPtr.Zero;
        ProcessInfo child=new ProcessInfo();bool assigned=false;
        try
        {
            if(seconds<=0)throw new ArgumentOutOfRangeException("seconds");
            if(!Path.IsPathFullyQualified(exe)||!File.Exists(exe))throw new ArgumentException("Existing absolute executable required","exe");
            job=CreateJobObject(IntPtr.Zero,null);Check(Valid(job));
            var limits=new ExtendedLimits();limits.basic.flags=0x2000; // KILL_ON_JOB_CLOSE; no breakaway.
            Check(SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(typeof(ExtendedLimits))));
            var security=new Security{Length=Marshal.SizeOf(typeof(Security)),Inherit=1};
            output=CreateFile(log,0x40000000,1,ref security,2,0x80,IntPtr.Zero);Check(Valid(output));
            errorOutput=CreateFile(log+".stderr.log",0x40000000,1,ref security,2,0x80,IntPtr.Zero);Check(Valid(errorOutput));
            input=CreateFile("NUL",0x80000000,1|2,ref security,3,0x80,IntPtr.Zero);Check(Valid(input));
            var start=new Startup{cb=Marshal.SizeOf(typeof(Startup)),flags=0x100,input=input,output=output,error=errorOutput};
            var command=new StringBuilder(Quote(exe));foreach(string argument in arguments)command.Append(' ').Append(Quote(argument));
            Check(CreateProcess(exe,command,IntPtr.Zero,IntPtr.Zero,true,0x08000004,IntPtr.Zero,cwd,ref start,out child)); // NO_WINDOW|SUSPENDED
            result.Started=true;result.Pid=unchecked((int)child.pid);
            Check(AssignProcessToJobObject(job,child.process));assigned=true;result.AssignedBeforeResume=true;
            if(ResumeThread(child.thread)==0xffffffff)throw new Win32Exception(Marshal.GetLastWin32Error());
            uint waited=WaitForSingleObject(child.process,checked((uint)seconds*1000));
            if(waited==0x102){result.TimedOut=true;result.Error="Owned build job exceeded finite timeout";}
            else if(waited!=0)throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        catch(Exception error){result.Error=error.GetType().Name+": "+error.Message;}
        finally
        {
            try
            {
                if(result.Started)
                {
                    if(assigned)
                    {
                        if(Active(job)>0){Check(TerminateJobObject(job,1));result.OwnedTreeKilled=true;}
                    }
                    else Check(TerminateProcess(child.process,1)); // Still suspended, no descendants possible.
                    if(WaitForSingleObject(child.process,10000)!=0)throw new Exception("Owned process exit remains unknown after cleanup");
                    uint code;Check(GetExitCodeProcess(child.process,out code));result.ExitCode=unchecked((int)code);
                }
                if(Valid(job))
                {
                    var cleanupClock=Stopwatch.StartNew();
                    while(Active(job)>0 && cleanupClock.ElapsedMilliseconds<10000)Thread.Sleep(20);
                    result.ActiveProcessesAfter=Active(job);result.OwnedJobStopped=result.ActiveProcessesAfter==0;
                }
            }
            catch(Exception error){result.CleanupError=error.GetType().Name+": "+error.Message;}
            // Closing this unnamed job also kills only its own still-owned children.
            if(Valid(job))CloseHandle(job);if(Valid(child.thread))CloseHandle(child.thread);
            if(Valid(child.process))CloseHandle(child.process);if(Valid(input))CloseHandle(input);
            if(Valid(errorOutput))CloseHandle(errorOutput);if(Valid(output))CloseHandle(output);
        }
        return result;
    }
}
