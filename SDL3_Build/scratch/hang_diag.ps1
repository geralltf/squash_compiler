# Hang-diagnosis tool, sibling to minidebug.ps1 (which only handles crash
# exceptions). Launches a target as a PLAIN process (no debugger attached --
# avoids perturbing WinMM/thread timing), waits up to $TimeoutMs, then for
# EVERY thread in the process: suspends it, reads its CONTEXT (RIP/RSP/RBP),
# resolves RIP against loaded modules, and prints it -- so a real hang can be
# diagnosed (which thread, stuck where) without ever needing a manual kill or
# a hard reset. The process is unconditionally terminated at the end whether
# it hung or exited on its own, same guarantee as safe_run.ps1.
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [int]$TimeoutMs = 8000
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class HangDiag {
    [StructLayout(LayoutKind.Sequential)]
    public struct THREADENTRY32 {
        public uint dwSize, cntUsage, th32ThreadID, th32OwnerProcessID;
        public int tpBasePri, tpDeltaPri; public uint dwFlags;
    }

    public const uint TH32CS_SNAPTHREAD = 0x00000004;
    public const uint THREAD_SUSPEND_RESUME = 0x0002;
    public const uint THREAD_GET_CONTEXT = 0x0008;
    public const uint THREAD_QUERY_INFORMATION = 0x0040;
    public const uint CONTEXT_FULL = 0x00100000 | 0x1 | 0x2 | 0x4;

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr CreateToolhelp32Snapshot(uint dwFlags, uint th32ProcessID);
    [DllImport("kernel32.dll")]
    public static extern bool Thread32First(IntPtr hSnapshot, ref THREADENTRY32 lpte);
    [DllImport("kernel32.dll")]
    public static extern bool Thread32Next(IntPtr hSnapshot, ref THREADENTRY32 lpte);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr OpenThread(uint dwDesiredAccess, bool bInheritHandle, uint dwThreadId);
    [DllImport("kernel32.dll")]
    public static extern uint SuspendThread(IntPtr hThread);
    [DllImport("kernel32.dll")]
    public static extern uint ResumeThread(IntPtr hThread);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetThreadContext(IntPtr hThread, IntPtr lpContext);
    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);
    [DllImport("kernel32.dll")]
    public static extern bool TerminateProcess(IntPtr hProcess, uint uExitCode);
    [DllImport("kernel32.dll")]
    public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

    [DllImport("psapi.dll", SetLastError=true)]
    public static extern bool EnumProcessModules(IntPtr hProcess, [Out] IntPtr[] lphModule, uint cb, out uint lpcbNeeded);
    [DllImport("psapi.dll", CharSet=CharSet.Auto)]
    public static extern uint GetModuleFileNameEx(IntPtr hProcess, IntPtr hModule, StringBuilder lpFilename, uint nSize);
    [StructLayout(LayoutKind.Sequential)]
    public struct MODULEINFO { public IntPtr lpBaseOfDll; public uint SizeOfImage; public IntPtr EntryPoint; }
    [DllImport("psapi.dll", SetLastError=true)]
    public static extern bool GetModuleInformation(IntPtr hProcess, IntPtr hModule, out MODULEINFO lpmodinfo, uint cb);

    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool ReadProcessMemory(IntPtr hProcess, IntPtr lpBaseAddress, byte[] lpBuffer, int nSize, out IntPtr lpNumberOfBytesRead);
}
"@

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $ExePath
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Host "Launched PID=$($proc.Id)"

$finished = $proc.WaitForExit($TimeoutMs)
if ($finished) {
    Write-Host "Process exited on its own, exit code=$($proc.ExitCode)"
    Write-Host "--- stdout ---"
    Write-Host $proc.StandardOutput.ReadToEnd()
    Write-Host "--- stderr ---"
    Write-Host $proc.StandardError.ReadToEnd()
    exit 0
}

Write-Host "TIMED OUT after ${TimeoutMs}ms -- diagnosing hang before killing PID $($proc.Id)"
$hProc = [HangDiag]::OpenProcess(0x1F0FFF, $false, [uint32]$proc.Id)  # PROCESS_ALL_ACCESS

# Resolve module base/size table once.
$modules = @()
$needed = 0
$arr = New-Object IntPtr[] 256
[HangDiag]::EnumProcessModules($hProc, $arr, [uint32]($arr.Length * 8), [ref]$needed) | Out-Null
$count = [Math]::Floor($needed / 8)
for ($i = 0; $i -lt $count; $i++) {
    $hMod = $arr[$i]
    $mi = New-Object HangDiag+MODULEINFO
    [HangDiag]::GetModuleInformation($hProc, $hMod, [ref]$mi, 28) | Out-Null
    $sb = New-Object System.Text.StringBuilder 512
    [HangDiag]::GetModuleFileNameEx($hProc, $hMod, $sb, 512) | Out-Null
    $modules += [PSCustomObject]@{ Name = $sb.ToString(); Base = $mi.lpBaseOfDll.ToInt64(); Size = $mi.SizeOfImage }
}

function Resolve-Addr([int64]$addr) {
    foreach ($m in $modules) {
        if ($addr -ge $m.Base -and $addr -lt ($m.Base + $m.Size)) {
            return ("{0}+0x{1:X}" -f (Split-Path $m.Name -Leaf), ($addr - $m.Base))
        }
    }
    return "<unresolved>"
}

$snap = [HangDiag]::CreateToolhelp32Snapshot([HangDiag]::TH32CS_SNAPTHREAD, 0)
$te = New-Object HangDiag+THREADENTRY32
$te.dwSize = [System.Runtime.InteropServices.Marshal]::SizeOf($te)
$ok = [HangDiag]::Thread32First($snap, [ref]$te)
$threadCount = 0
while ($ok) {
    if ($te.th32OwnerProcessID -eq $proc.Id) {
        $threadCount++
        $hThread = [HangDiag]::OpenThread([HangDiag]::THREAD_SUSPEND_RESUME -bor [HangDiag]::THREAD_GET_CONTEXT -bor [HangDiag]::THREAD_QUERY_INFORMATION, $false, $te.th32ThreadID)
        if ($hThread -ne [IntPtr]::Zero) {
            [HangDiag]::SuspendThread($hThread) | Out-Null
            $ctxSize = 1232
            $rawPtr = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($ctxSize + 16)
            $aligned = ([int64]$rawPtr + 15) -band (-16)
            $ctxPtr = [IntPtr]$aligned
            for ($o = 0; $o -lt $ctxSize; $o += 8) { [System.Runtime.InteropServices.Marshal]::WriteInt64($ctxPtr, $o, 0) }
            [System.Runtime.InteropServices.Marshal]::WriteInt32($ctxPtr, 48, [int][HangDiag]::CONTEXT_FULL)
            $gotCtx = [HangDiag]::GetThreadContext($hThread, $ctxPtr)
            if ($gotCtx) {
                $rip = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 248)
                $rsp = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 152)
                $rbp = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 160)
                $resolved = Resolve-Addr $rip
                Write-Host ("Thread {0}: RIP=0x{1:X16} ({2}) RSP=0x{3:X16} RBP=0x{4:X16}" -f $te.th32ThreadID, $rip, $resolved, $rsp, $rbp)

                # Walk a few return addresses up the stack (naive: just read qwords
                # above RSP and report any that resolve into a known module --
                # not a real frame-pointer walk, but often good enough to see
                # which function called into the stuck one).
                for ($off = 0; $off -le 0x100; $off += 8) {
                    $buf = New-Object byte[] 8
                    $nread = [IntPtr]::Zero
                    $rok = [HangDiag]::ReadProcessMemory($hProc, [IntPtr]($rsp + $off), $buf, 8, [ref]$nread)
                    if ($rok) {
                        $val = [System.BitConverter]::ToInt64($buf, 0)
                        $r2 = Resolve-Addr $val
                        if ($r2 -ne "<unresolved>") {
                            Write-Host ("    [rsp+0x{0:X}] = 0x{1:X16} ({2})" -f $off, $val, $r2)
                        }
                    }
                }
            } else {
                Write-Host ("Thread {0}: GetThreadContext failed" -f $te.th32ThreadID)
            }
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($rawPtr)
            [HangDiag]::ResumeThread($hThread) | Out-Null
            [HangDiag]::CloseHandle($hThread) | Out-Null
        }
    }
    $ok = [HangDiag]::Thread32Next($snap, [ref]$te)
}
[HangDiag]::CloseHandle($snap) | Out-Null
Write-Host "Total threads examined: $threadCount"

[HangDiag]::TerminateProcess($hProc, 1) | Out-Null
$killed = $false
for ($retry = 0; $retry -lt 10; $retry++) {
    Start-Sleep -Milliseconds 300
    $stillThere = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if (-not $stillThere) { $killed = $true; break }
}
if ($killed) { Write-Host "Confirmed killed." }
else { Write-Host "WARNING: process $($proc.Id) still alive after TerminateProcess (waited 3s)!" }

Write-Host "--- stdout (whatever was buffered before kill) ---"
try { Write-Host $proc.StandardOutput.ReadToEnd() } catch {}
Write-Host "--- stderr (whatever was buffered before kill) ---"
try { Write-Host $proc.StandardError.ReadToEnd() } catch {}
[HangDiag]::CloseHandle($hProc) | Out-Null
