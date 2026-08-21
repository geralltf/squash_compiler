# squash compiler installer (Windows / PowerShell).
#
# Mirrors install.sh's own steps and reasoning exactly:
#   1. Build squash from source with the real system compiler (never a
#      prior squash binary -- every install starts from a known-good,
#      non-self-hosted provenance chain).
#   2. Verify the signed provenance manifest if one exists (best-effort,
#      warns loudly rather than silently continuing on a bad signature).
#   3. Print real version/platform info for whatever gets installed.
#   4. Copy the binary to an install directory and offer to add it to
#      PATH via the user's own PATH environment variable (not the
#      machine-wide one -- that needs admin rights this script
#      deliberately never asks for), without silently editing anything
#      you didn't confirm.
#
# Requires an MSVC-compatible C compiler on PATH (cl.exe from a Visual
# Studio "Developer PowerShell", or a MinGW-w64 gcc) -- same "always
# build with a real, independent system compiler" rule as install.sh.

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$InstallDir = if ($env:SQUASH_INSTALL_DIR) { $env:SQUASH_INSTALL_DIR } else { Join-Path $env:LOCALAPPDATA "squash\bin" }
$BinName = "squash.exe"

Write-Host "=== squash compiler installer ===" -ForegroundColor Cyan

# --- Version / provenance info up front ---
Push-Location $ScriptDir
try {
    $GitCommit = (git rev-parse --short HEAD 2>$null); if (-not $GitCommit) { $GitCommit = "unknown" }
    $GitDescribe = (git describe --tags --always --dirty 2>$null); if (-not $GitDescribe) { $GitDescribe = "unknown" }
} finally {
    Pop-Location
}
$PlatformOS = "Windows " + [System.Environment]::OSVersion.Version.ToString()
$PlatformArch = $env:PROCESSOR_ARCHITECTURE

Write-Host "Source:   $ScriptDir"
Write-Host "Version:  $GitDescribe (commit $GitCommit)"
Write-Host "Platform: $PlatformOS $PlatformArch"
Write-Host ""

# --- Find a real, independent C compiler ---
$CC = $null
$CCKind = $null
if (Get-Command cl.exe -ErrorAction SilentlyContinue) { $CC = "cl.exe"; $CCKind = "msvc" }
elseif (Get-Command gcc.exe -ErrorAction SilentlyContinue) { $CC = "gcc.exe"; $CCKind = "gcc" }
elseif (Get-Command gcc -ErrorAction SilentlyContinue) { $CC = "gcc"; $CCKind = "gcc" }
else {
    Write-Host "ERROR: no C compiler found on PATH." -ForegroundColor Red
    Write-Host "Either run this from a Visual Studio 'Developer PowerShell' (provides cl.exe)"
    Write-Host "or install MinGW-w64 (provides gcc.exe), then re-run."
    exit 1
}
Write-Host "Building with: $CC ($CCKind)"

$SrcFiles = @("compiler.c","assembler.c","ast.c","codegen.c","arm64_asm.c","codegen_arm64.c",
              "lexer.c","parser_new4.c","pe_builder.c","elf_builder.c","macho_builder.c",
              "symtable.c","linker.c","winlinker.c","objfile.c","implib.c","diag.c")

$BuildTmp = Join-Path $env:TEMP ("squash_install_build_" + [guid]::NewGuid().ToString("N") + ".exe")
Push-Location $ScriptDir
try {
    if ($CCKind -eq "msvc") {
        $paths = $SrcFiles | ForEach-Object { Join-Path $ScriptDir $_ }
        & cl.exe /nologo /W0 /Fe:$BuildTmp $paths /I. 2>&1 | Out-File -FilePath "$BuildTmp.log"
    } else {
        & $CC -o $BuildTmp $SrcFiles -I. -w 2>&1 | Out-File -FilePath "$BuildTmp.log"
    }
} finally {
    Pop-Location
}
if (-not (Test-Path $BuildTmp)) {
    Write-Host "ERROR: build failed. See $BuildTmp.log" -ForegroundColor Red
    Get-Content "$BuildTmp.log" | Write-Host
    exit 1
}
$Hash = (Get-FileHash $BuildTmp -Algorithm SHA256).Hash.ToLower()
Write-Host "Build OK. sha256=$Hash"

# --- Signature verification, if a manifest+key and gpg are present (best-effort, non-fatal) ---
$Manifest = Join-Path $ScriptDir "tools\keys\last_self_verify_manifest.txt"
$PubKey = Join-Path $ScriptDir "tools\keys\squash-release-signing-pubkey.asc"
$Gpg = Get-Command gpg -ErrorAction SilentlyContinue
if ((Test-Path "$Manifest.asc") -and (Test-Path $PubKey) -and $Gpg) {
    $TmpGnupgHome = Join-Path $env:TEMP ("squash_gpg_verify_" + [guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Path $TmpGnupgHome | Out-Null
    try {
        & gpg --homedir $TmpGnupgHome --batch --quiet --import $PubKey 2>$null
        & gpg --homedir $TmpGnupgHome --batch --verify "$Manifest.asc" $Manifest 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Host "Provenance manifest signature: VALID" -ForegroundColor Green
        } else {
            Write-Host "*** WARNING: provenance manifest signature check FAILED. ***" -ForegroundColor Yellow
            Write-Host "*** This does not stop the install, but the self-hosting verification"
            Write-Host "*** record for this checkout is not trustworthy. Investigate before relying on it."
        }
    } finally {
        Remove-Item -Recurse -Force $TmpGnupgHome -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "(No signed provenance manifest found, or gpg not on PATH -- run tools/self_verify.sh to generate one.)"
}
Write-Host ""

# --- Install ---
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
$DestPath = Join-Path $InstallDir $BinName
Copy-Item -Path $BuildTmp -Destination $DestPath -Force
Remove-Item -Force $BuildTmp, "$BuildTmp.log" -ErrorAction SilentlyContinue
Write-Host "Installed: $DestPath"

# --- PATH (per-user, never machine-wide -- no admin rights needed or requested) ---
$UserPath = [System.Environment]::GetEnvironmentVariable("Path", "User")
if ($UserPath -and ($UserPath -split ";" | Where-Object { $_.TrimEnd('\') -eq $InstallDir.TrimEnd('\') })) {
    Write-Host "$InstallDir is already on your user PATH."
} else {
    Write-Host ""
    Write-Host "$InstallDir is NOT currently on your PATH."
    if ([Environment]::UserInteractive -and -not $env:SQUASH_NONINTERACTIVE) {
        $ans = Read-Host "Add it to your PATH (user-level, no admin needed)? [y/N]"
    } else {
        $ans = "n"
        Write-Host "(non-interactive session -- skipping the PATH prompt; answer manually below)"
    }
    if ($ans -eq "y" -or $ans -eq "Y") {
        $NewPath = if ($UserPath) { "$UserPath;$InstallDir" } else { $InstallDir }
        [System.Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
        Write-Host "Added to your user PATH. Open a new PowerShell window for it to take effect."
    } else {
        Write-Host "Skipped. Add this yourself if you want it on PATH:"
        Write-Host "  [System.Environment]::SetEnvironmentVariable('Path', `$env:Path + ';$InstallDir', 'User')"
    }
}

Write-Host ""
Write-Host "Done. Try: $DestPath -windows -64 yourfile.c -o yourprogram.exe"
