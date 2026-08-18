param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [int]$WaitSeconds = 3,
    [int]$OnlyTid = 0,
    [int]$Samples = 1
)

Add-Type @"
using System;
using System.Runtime.InteropServices;

public class HangDbg {
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenThread(uint dwDesiredAccess, bool bInheritHandle, uint dwThreadId);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint SuspendThread(IntPtr hThread);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint ResumeThread(IntPtr hThread);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetThreadContext(IntPtr hThread, IntPtr lpContext);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out IntPtr lpNumberOfBytesRead);
    public const uint THREAD_ALL_ACCESS = 0x1F03FF;
    public const uint PROCESS_VM_READ = 0x0010;
    public const uint PROCESS_QUERY_INFORMATION = 0x0400;
    public const uint CONTEXT_FULL = 0x00100000 | 0x1 | 0x2 | 0x4;
}
"@

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ExePath
$psi.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Host "Launched PID=$($proc.Id)"
Start-Sleep -Seconds $WaitSeconds

$proc.Refresh()
$modBase = $proc.MainModule.BaseAddress.ToInt64()
$modSize = $proc.MainModule.ModuleMemorySize
Write-Host ("MainModule base=0x{0:X16} size=0x{1:X}" -f $modBase, $modSize)
Write-Host "Thread count: $($proc.Threads.Count)"

for ($s = 0; $s -lt $Samples; $s++) {
if ($s -gt 0) { Start-Sleep -Milliseconds 400 }
foreach ($t in $proc.Threads) {
    $tid = [uint32]$t.Id
    if ($OnlyTid -ne 0 -and $tid -ne $OnlyTid) { continue }
    $hThread = [HangDbg]::OpenThread([HangDbg]::THREAD_ALL_ACCESS, $false, $tid)
    if ($hThread -eq [IntPtr]::Zero) { Write-Host "tid=$tid OpenThread failed"; continue }
    [HangDbg]::SuspendThread($hThread) | Out-Null

    $ctxSize = 1232
    $rawPtr = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($ctxSize + 16)
    $aligned = ([int64]$rawPtr + 15) -band (-16)
    $ctxPtr = [IntPtr]$aligned
    for ($o = 0; $o -lt $ctxSize; $o += 8) {
        [System.Runtime.InteropServices.Marshal]::WriteInt64($ctxPtr, $o, 0)
    }
    [System.Runtime.InteropServices.Marshal]::WriteInt32($ctxPtr, 48, [int][HangDbg]::CONTEXT_FULL)
    $gotCtx = [HangDbg]::GetThreadContext($hThread, $ctxPtr)
    if ($gotCtx) {
        $rip = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 248)
        $rsp = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 152)
        $rbp = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 160)
        Write-Host ("tid={0,-8} RIP=0x{1:X16} RSP=0x{2:X16} RBP=0x{3:X16}" -f $tid, $rip, $rsp, $rbp)

        # Scan up the stack for return addresses that land inside our own
        # module (ImageBase 0x140000000 .. +ImageSize) -- tells us which of
        # OUR functions is blocked waiting on the kernel, even though RIP
        # itself is deep inside ntdll/kernel32.
        $modLo = $modBase
        $modHi = $modBase + $modSize
        $found = 0
        for ($qi = 0; $qi -lt 4096; $qi++) {
            $addr = $rsp + ($qi * 8)
            $buf = New-Object byte[] 8
            $nread = [IntPtr]::Zero
            $ok = [HangDbg]::ReadProcessMemory($proc.Handle, [IntPtr]$addr, $buf, 8, [ref]$nread)
            if (-not $ok) { continue }
            $v = [System.BitConverter]::ToInt64($buf, 0)
            if ($v -ge $modLo -and $v -lt $modHi) {
                Write-Host ("    [rsp+0x{0:X}] -> module offset 0x{1:X}" -f ($qi*8), ($v - $modLo))
                $found++
                if ($found -ge 6) { break }
            }
        }
    } else {
        Write-Host "tid=$tid GetThreadContext failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    }
    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($rawPtr)
    [HangDbg]::ResumeThread($hThread) | Out-Null
    [HangDbg]::CloseHandle($hThread) | Out-Null
}
}

Write-Host "Killing PID=$($proc.Id)"
try { $proc.Kill() } catch {}
Start-Sleep -Milliseconds 300
