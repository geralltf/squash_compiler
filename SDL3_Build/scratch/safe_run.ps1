# Safe-launch helper: runs an exe with a hard timeout and GUARANTEED kill
# verification, to avoid the earlier session's RAM-exhaustion incident
# (Git Bash `timeout` does not reliably kill a native Windows process
# blocked deep in a kernel wait).
param(
    [Parameter(Mandatory=$true)][string]$ExePath,
    [int]$TimeoutMs = 8000
)
$proc = Start-Process -FilePath $ExePath -PassThru -NoNewWindow -RedirectStandardOutput "$ExePath.out.txt" -RedirectStandardError "$ExePath.err.txt"
$finished = $proc.WaitForExit($TimeoutMs)
if (-not $finished) {
    Write-Output "TIMED OUT after ${TimeoutMs}ms - killing PID $($proc.Id)"
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 300
    $stillThere = Get-Process -Id $proc.Id -ErrorAction SilentlyContinue
    if ($stillThere) {
        Write-Output "WARNING: process $($proc.Id) still alive after Stop-Process!"
    } else {
        Write-Output "Confirmed killed."
    }
    Write-Output "RESULT: HANG"
} else {
    Write-Output "RESULT: EXITED code=$($proc.ExitCode)"
}
if (Test-Path "$ExePath.out.txt") { Write-Output "--- stdout ---"; Get-Content "$ExePath.out.txt" }
if (Test-Path "$ExePath.err.txt") { Write-Output "--- stderr ---"; Get-Content "$ExePath.err.txt" }
