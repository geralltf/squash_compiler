# Minimal Win32 debugger: launches a target under DEBUG_ONLY_THIS_PROCESS,
# runs it to the first unhandled exception (or exit), prints the faulting
# RIP, exception code, and the module+offset it resolves to.
param(
    [Parameter(Mandatory=$true)][string]$ExePath
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public class MiniDbg {
    [StructLayout(LayoutKind.Sequential)]
    public struct STARTUPINFO {
        public int cb; public IntPtr lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2;
        public IntPtr hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct PROCESS_INFORMATION {
        public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId;
    }

    // DEBUG_EVENT: dwDebugEventCode(4) + dwProcessId(4) + dwThreadId(4), then
    // a union starting at offset 16 (8-byte aligned since it holds pointers).
    // The union's largest member is EXCEPTION_DEBUG_INFO (an EXCEPTION_RECORD
    // — which itself ends in a 15-element ULONG_PTR[] array, 120 bytes alone
    // — plus a trailing DWORD), totalling ~160 bytes, so the whole DEBUG_EVENT
    // is ~176 bytes. A too-small declared Size here means Windows writes the
    // real, full-size struct past the end of the buffer .NET allocates for
    // it — this previously caused an AccessViolation inside the marshaling
    // layer itself (not a normal, catchable .NET exception at our call site).
    // Rounded up generously for safety margin.
    [StructLayout(LayoutKind.Explicit, Size=224)]
    public struct DEBUG_EVENT {
        [FieldOffset(0)]  public uint dwDebugEventCode;
        [FieldOffset(4)]  public int dwProcessId;
        [FieldOffset(8)]  public int dwThreadId;
        // EXCEPTION_DEBUG_INFO starts here (offset 16, after 8-byte alignment pad)
        [FieldOffset(16)] public uint ExceptionCode;
        [FieldOffset(20)] public uint ExceptionFlags;
        [FieldOffset(24)] public IntPtr ExceptionRecordNext;
        [FieldOffset(32)] public IntPtr ExceptionAddress;
        [FieldOffset(40)] public uint NumberParameters;
    }

    public const uint DEBUG_ONLY_THIS_PROCESS = 0x00000002;
    public const uint DEBUG_PROCESS = 0x00000001;
    public const uint EXCEPTION_DEBUG_EVENT = 1;
    public const uint CREATE_PROCESS_DEBUG_EVENT = 3;
    public const uint EXIT_PROCESS_DEBUG_EVENT = 5;
    public const uint LOAD_DLL_DEBUG_EVENT = 6;
    public const uint DBG_CONTINUE = 0x00010002;
    public const uint DBG_EXCEPTION_NOT_HANDLED = 0x80010001;
    public const uint CONTEXT_FULL = 0x00100000 | 0x1 | 0x2 | 0x4; // amd64 CONTEXT_FULL

    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Auto)]
    public static extern bool CreateProcess(string lpApplicationName, string lpCommandLine,
        IntPtr lpProcessAttributes, IntPtr lpThreadAttributes, bool bInheritHandles,
        uint dwCreationFlags, IntPtr lpEnvironment, string lpCurrentDirectory,
        ref STARTUPINFO lpStartupInfo, out PROCESS_INFORMATION lpProcessInformation);

    [DllImport("kernel32.dll")]
    public static extern bool WaitForDebugEvent(ref DEBUG_EVENT lpDebugEvent, uint dwMilliseconds);

    [DllImport("kernel32.dll")]
    public static extern bool ContinueDebugEvent(int dwProcessId, int dwThreadId, uint dwContinueStatus);

    [DllImport("kernel32.dll")]
    public static extern IntPtr OpenThread(uint dwDesiredAccess, bool bInheritHandle, int dwThreadId);

    // Raw-pointer form: we manage a 16-byte-aligned unmanaged buffer
    // ourselves instead of marshaling a CONTEXT struct (x64 CONTEXT requires
    // 16-byte alignment and is easy to mis-size by hand — see the offset
    // map computed in the calling script).
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool GetThreadContext(IntPtr hThread, IntPtr lpContext);

    [DllImport("kernel32.dll")]
    public static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll")]
    public static extern bool TerminateProcess(IntPtr hProcess, uint uExitCode);

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

$si = New-Object MiniDbg+STARTUPINFO
$si.cb = [System.Runtime.InteropServices.Marshal]::SizeOf($si)
$pi = New-Object MiniDbg+PROCESS_INFORMATION

$cmdline = "`"$ExePath`""
$ok = [MiniDbg]::CreateProcess($ExePath, $cmdline, [IntPtr]::Zero, [IntPtr]::Zero, $false,
    [MiniDbg]::DEBUG_ONLY_THIS_PROCESS, [IntPtr]::Zero, [System.IO.Path]::GetDirectoryName($ExePath), [ref]$si, [ref]$pi)

if (-not $ok) {
    Write-Host "CreateProcess failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
    exit 1
}

Write-Host "Launched PID=$($pi.dwProcessId)"
$modules = @{}
$deadline = [DateTime]::Now.AddSeconds(30)

while ($true) {
    if ([DateTime]::Now -gt $deadline) { Write-Host "TIMEOUT waiting for debug event"; [MiniDbg]::TerminateProcess($pi.hProcess, 1) | Out-Null; break }
    $ev = New-Object MiniDbg+DEBUG_EVENT
    $got = [MiniDbg]::WaitForDebugEvent([ref]$ev, 2000)
    if (-not $got) { continue }

    if ($ev.dwDebugEventCode -eq [MiniDbg]::EXCEPTION_DEBUG_EVENT) {
        $addr = $ev.ExceptionAddress.ToInt64()
        Write-Host ("EXCEPTION code=0x{0:X8} addr=0x{1:X16}" -f $ev.ExceptionCode, $addr)

        $hThread = [MiniDbg]::OpenThread(0x1F03FF, $false, $ev.dwThreadId)  # THREAD_ALL_ACCESS

        # x64 CONTEXT is 1232 bytes and must be 16-byte aligned. Offsets (see
        # winnt.h): ContextFlags=48, Rax=120, Rcx=128, Rdx=136, Rbx=144,
        # Rsp=152, Rbp=160, Rsi=168, Rdi=176, R8=184, R9=192, Rip=248.
        $ctxSize = 1232
        $rawPtr = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($ctxSize + 16)
        $aligned = ([int64]$rawPtr + 15) -band (-16)
        $ctxPtr = [IntPtr]$aligned
        for ($o = 0; $o -lt $ctxSize; $o += 8) {
            [System.Runtime.InteropServices.Marshal]::WriteInt64($ctxPtr, $o, 0)
        }
        [System.Runtime.InteropServices.Marshal]::WriteInt32($ctxPtr, 48, [int][MiniDbg]::CONTEXT_FULL)

        $gotCtx = [MiniDbg]::GetThreadContext($hThread, $ctxPtr)
        if ($gotCtx) {
            $rip = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 248)
            $rax = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 120)
            $rcx = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 128)
            $rdx = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 136)
            $rbx = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 144)
            $rsp = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 152)
            $rbp = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 160)
            $rsi = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 168)
            $rdi = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 176)
            $r8  = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 184)
            $r9  = [System.Runtime.InteropServices.Marshal]::ReadInt64($ctxPtr, 192)
            Write-Host ("RIP=0x{0:X16} RAX=0x{1:X16} RCX=0x{2:X16} RDX=0x{3:X16} RBX=0x{4:X16}" -f $rip,$rax,$rcx,$rdx,$rbx)
            Write-Host ("RSI=0x{0:X16} RDI=0x{1:X16} R8=0x{2:X16} R9=0x{3:X16} RSP=0x{4:X16} RBP=0x{5:X16}" -f $rsi,$rdi,$r8,$r9,$rsp,$rbp)

            function Read-Qword([IntPtr]$hProc, [int64]$addr) {
                $buf = New-Object byte[] 8
                $nread = [IntPtr]::Zero
                $ok2 = [MiniDbg]::ReadProcessMemory($hProc, [IntPtr]$addr, $buf, 8, [ref]$nread)
                if (-not $ok2) { return $null }
                return [System.BitConverter]::ToInt64($buf, 0)
            }
            $hProcForRead = $pi.hProcess
            Write-Host "--- stack at RSP (for call-through-null: [rsp] is the true return addr) ---"
            for ($off = 0; $off -le 0x30; $off += 8) {
                $v = Read-Qword $hProcForRead ($rsp + $off)
                if ($v -ne $null) {
                    Write-Host ("  [rsp+0x{0:X}] = 0x{1:X16}" -f $off, $v)
                }
            }
            Write-Host "--- stack/frame dump around RBP ---"
            for ($off = -0x80; $off -le 0x10; $off += 8) {
                $v = Read-Qword $hProcForRead ($rbp + $off)
                if ($v -ne $null) {
                    Write-Host ("  [rbp{0}{1:X}] = 0x{2:X16}" -f $(if($off -ge 0){"+"}else{"-"}), [Math]::Abs($off), $v)
                }
            }

            # [rbp-0x50] holds SDL_GetError's local 'error' pointer (SDL_error*).
            # Dump the SDL_error struct it points to: info[0](24B: error/str/len),
            # info[1](24B), current(4B @ +0x30), realloc_func(@0x38), free_func(@0x40).
            $errorPtr = Read-Qword $hProcForRead ($rbp - 0x50)
            if ($errorPtr -ne $null -and $errorPtr -ne 0) {
                Write-Host ("--- SDL_error struct dump at error=0x{0:X16} ---" -f $errorPtr)
                Write-Host ("  info[0].error=0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 0)))
                Write-Host ("  info[0].str  =0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 8)))
                Write-Host ("  info[0].len  =0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 16)))
                Write-Host ("  info[1].error=0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 24)))
                Write-Host ("  info[1].str  =0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 32)))
                Write-Host ("  info[1].len  =0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 40)))
                Write-Host ("  current(+0x30, as qword)=0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 48)))
                Write-Host ("  realloc_func(+0x38)     =0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 56)))
                Write-Host ("  free_func(+0x40)        =0x{0:X16}" -f (Read-Qword $hProcForRead ($errorPtr + 64)))

                function Read-Str([IntPtr]$hProc, [int64]$addr, [int]$maxlen) {
                    $buf = New-Object byte[] $maxlen
                    $nread = [IntPtr]::Zero
                    $ok3 = [MiniDbg]::ReadProcessMemory($hProc, [IntPtr]$addr, $buf, $maxlen, [ref]$nread)
                    if (-not $ok3) { return "<unreadable>" }
                    $z = [Array]::IndexOf($buf, [byte]0)
                    if ($z -lt 0) { $z = $maxlen }
                    return [System.Text.Encoding]::ASCII.GetString($buf, 0, $z)
                }
                $strPtr = Read-Qword $hProcForRead ($errorPtr + 8)
                if ($strPtr -ne $null -and $strPtr -ne 0) {
                    Write-Host ("--- string at info[0].str=0x{0:X16} ---" -f $strPtr)
                    Write-Host ("  content: {0}" -f (Read-Str $hProcForRead $strPtr 200))
                }

                Write-Host "--- raw hex dump, error_ptr-0x10 to error_ptr+0xC0 ---"
                for ($off = -0x10; $off -lt 0xC0; $off += 16) {
                    $line = New-Object byte[] 16
                    $nr = [IntPtr]::Zero
                    [MiniDbg]::ReadProcessMemory($hProcForRead, [IntPtr]($errorPtr + $off), $line, 16, [ref]$nr) | Out-Null
                    $hex = ($line | ForEach-Object { $_.ToString("X2") }) -join " "
                    $ascii = ($line | ForEach-Object { if ($_ -ge 32 -and $_ -le 126) { [char]$_ } else { "." } }) -join ""
                    Write-Host ("  +0x{0:X3}: {1}  {2}" -f $off, $hex, $ascii)
                }
            }
        } else {
            Write-Host "GetThreadContext failed: $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())"
        }
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($rawPtr)
        [MiniDbg]::CloseHandle($hThread) | Out-Null

        # Resolve the return address at [rbp+8] against loaded modules too —
        # tells us which function made the call that led to the crash,
        # useful when the crash RIP itself is a wild jump outside all modules.
        $retAddr = Read-Qword $pi.hProcess ($rbp + 8)
        if ($retAddr -ne $null) {
            Write-Host ("Return address at [rbp+8] = 0x{0:X16}" -f $retAddr)
        }

        # Resolve module + offset
        $hProc = $pi.hProcess
        $needed = 0
        $arr = New-Object IntPtr[] 256
        [MiniDbg]::EnumProcessModules($hProc, $arr, [uint32]($arr.Length * 8), [ref]$needed) | Out-Null
        $count = [Math]::Floor($needed / 8)
        for ($i = 0; $i -lt $count; $i++) {
            $hMod = $arr[$i]
            $mi = New-Object MiniDbg+MODULEINFO
            [MiniDbg]::GetModuleInformation($hProc, $hMod, [ref]$mi, 28) | Out-Null
            $base = $mi.lpBaseOfDll.ToInt64()
            $size2 = $mi.SizeOfImage
            if ($retAddr -ne $null -and $retAddr -ge $base -and $retAddr -lt ($base + $size2)) {
                $sb2 = New-Object System.Text.StringBuilder 512
                [MiniDbg]::GetModuleFileNameEx($hProc, $hMod, $sb2, 512) | Out-Null
                Write-Host ("-> return addr in module {0} base=0x{1:X16} offset=0x{2:X}" -f $sb2.ToString(), $base, ($retAddr - $base))
            }
            $size = $mi.SizeOfImage
            if ($addr -ge $base -and $addr -lt ($base + $size)) {
                $sb = New-Object System.Text.StringBuilder 512
                [MiniDbg]::GetModuleFileNameEx($hProc, $hMod, $sb, 512) | Out-Null
                Write-Host ("-> module {0} base=0x{1:X16} offset=0x{2:X}" -f $sb.ToString(), $base, ($addr - $base))
            }
        }

        # Let it crash for real (pass exception through) then exit
        [MiniDbg]::ContinueDebugEvent($ev.dwProcessId, $ev.dwThreadId, [MiniDbg]::DBG_EXCEPTION_NOT_HANDLED) | Out-Null
        continue
    }
    elseif ($ev.dwDebugEventCode -eq [MiniDbg]::EXIT_PROCESS_DEBUG_EVENT) {
        Write-Host "Process exited"
        [MiniDbg]::ContinueDebugEvent($ev.dwProcessId, $ev.dwThreadId, [MiniDbg]::DBG_CONTINUE) | Out-Null
        break
    }
    else {
        [MiniDbg]::ContinueDebugEvent($ev.dwProcessId, $ev.dwThreadId, [MiniDbg]::DBG_CONTINUE) | Out-Null
    }
}

[MiniDbg]::CloseHandle($pi.hThread) | Out-Null
[MiniDbg]::CloseHandle($pi.hProcess) | Out-Null
