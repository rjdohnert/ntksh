<#
.SYNOPSIS
    Build script for ntksh (Windows NT KornShell).
.PARAMETER Amalgamate
    Build from the single-file amalgamation src/ksh.c (SQLite-style).
.PARAMETER GenerateAmalgamation
    Regenerate src/ksh.c using tools/amalgamate.py before building.
#>
[CmdletBinding()]
param(
    [switch]$Amalgamate,
    [switch]$GenerateAmalgamation,
    [switch]$ASan,
    [switch]$Sign
)

$ErrorActionPreference = 'Stop'
$rootDir = $PSScriptRoot
$binDir = Join-Path $rootDir 'bin'
$srcDir = Join-Path $rootDir 'src'

if (-not (Test-Path $binDir)) {
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
}

if ($GenerateAmalgamation) {
    Write-Host "[TOOL] Regenerating single-file amalgamation src/ksh.c..." -ForegroundColor Cyan
    if (Get-Command python -ErrorAction SilentlyContinue) {
        & python (Join-Path $rootDir 'tools\amalgamate.py')
    } else {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $rootDir 'tools\amalgamate.ps1')
    }
}

# Rotate running binaries if currently locked
foreach ($target in @((Join-Path $binDir 'ksh.exe'), (Join-Path $rootDir 'ksh.exe'))) {
    if (Test-Path $target) {
        $old = "$target.old"
        Remove-Item $old -Force -ErrorAction SilentlyContinue
        try { Move-Item -LiteralPath $target -Destination $old -Force -ErrorAction SilentlyContinue } catch {}
    }
}

$outputExe = Join-Path $binDir 'ksh.exe'
$libs = @('-ladvapi32', '-lshell32', '-luser32', '-lpsapi', '-liphlpapi', '-lws2_32', '-lsetupapi', '-lcfgmgr32', '-lrstrtmgr', '-lwinhttp')

# Enterprise Exploit Mitigations
$secFlags = @(
    '-fstack-protector-strong',
    '-Xlinker', '/GUARD:CF',
    '-Xlinker', '/NXCOMPAT',
    '-Xlinker', '/DYNAMICBASE',
    '-Xlinker', '/HIGHENTROPYVA',
    '-Xlinker', '/CETCOMPAT',
    '-Xlinker', '/MANIFEST:EMBED',
    '-Xlinker', "/MANIFESTINPUT:$srcDir\ksh.manifest"
)

if ($ASan) {
    Write-Host "[BUILD] AddressSanitizer & UndefinedBehaviorSanitizer instrumentation enabled..." -ForegroundColor Yellow
    $sanFlags = @('-fsanitize=address,undefined', '-g', '-fno-omit-frame-pointer')
    $secFlags = $sanFlags

    # Ensure ASan runtime DLL is copied next to output binaries so child processes can load it
    $asanDll = Join-Path $rootDir 'clang_rt.asan_dynamic-x86_64.dll'
    if (Test-Path $asanDll) {
        Copy-Item -LiteralPath $asanDll -Destination (Join-Path $binDir 'clang_rt.asan_dynamic-x86_64.dll') -Force
    }
}

if ($Amalgamate) {
    Write-Host "[BUILD] Compiling from single-file amalgamation src/ksh.c (with exploit mitigations)..." -ForegroundColor Green
    $cflags = @('-O2', '-Wall', '-Wextra', '-fms-extensions', '-municode') + $secFlags
    $sources = @((Join-Path $srcDir 'ksh.c'))
    $argsList = $cflags + @('-o', $outputExe) + $sources + $libs
    & clang @argsList
} else {
    Write-Host "[BUILD] Compiling modular ntksh sources (with exploit mitigations)..." -ForegroundColor Green
    $cflags = @('-O2', '-Wall', '-Wextra', '-fms-extensions', '-municode', "-I$rootDir\include") + $secFlags
    $sources = @(
        (Join-Path $srcDir 'main.c'),
        (Get-ChildItem -Path (Join-Path $srcDir 'core\*.c') | ForEach-Object { $_.FullName }),
        (Get-ChildItem -Path (Join-Path $srcDir 'batch\*.c') | ForEach-Object { $_.FullName }),
        (Get-ChildItem -Path (Join-Path $srcDir 'builtins\*.c') | ForEach-Object { $_.FullName }),
        (Get-ChildItem -Path (Join-Path $srcDir 'help\*.c') | ForEach-Object { $_.FullName })
    )
    $argsList = $cflags + @('-o', $outputExe) + $sources + $libs
    & clang @argsList
}

if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath $outputExe -Destination (Join-Path $rootDir 'ksh.exe') -Force
if (Test-Path (Join-Path $rootDir 'installer')) {
    Copy-Item -LiteralPath $outputExe -Destination (Join-Path $rootDir 'installer\ksh.exe') -Force
    if (Test-Path (Join-Path $rootDir 'user_guide.md')) {
        Copy-Item -LiteralPath (Join-Path $rootDir 'user_guide.md') -Destination (Join-Path $rootDir 'installer\user_guide.md') -Force
    }
}
Write-Host "[SUCCESS] Binary created: bin\ksh.exe, ksh.exe, and installer\ksh.exe" -ForegroundColor Green

if ($Sign) {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $rootDir 'tools\sign.ps1')
}
