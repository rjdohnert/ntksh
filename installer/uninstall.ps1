# ==============================================================================
# ntksh Uninstaller for Windows
# Removes C:\Program Files\ntksh\
# Cleans system PATH
# Removes Windows Terminal profile
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$Unattended
)

$ErrorActionPreference = 'Stop'

# Ensure administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[*] Requesting Administrator privileges to uninstall..." -ForegroundColor Cyan
    $scriptPath = $MyInvocation.MyCommand.Path
    if (-not $scriptPath) {
        $scriptPath = Join-Path $PSScriptRoot "uninstall.ps1"
    }
    $argsList = "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`""
    if ($Unattended) { $argsList += " -Unattended" }
    
    Start-Process -FilePath "powershell.exe" -ArgumentList $argsList -Verb RunAs
    exit 0
}

$InstallRoot = "C:\Program Files\ntksh"
$BinDir      = Join-Path $InstallRoot "bin"

Write-Host "`n========================================================" -ForegroundColor Yellow
Write-Host "         ntksh (KornShell for Windows) Uninstaller       " -ForegroundColor White
Write-Host "========================================================`n" -ForegroundColor Yellow

# 1. Clean System PATH
Write-Host "[1/3] Removing from System PATH..." -ForegroundColor Cyan
try {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", [EnvironmentVariableTarget]::Machine)
    $pathParts = @($machinePath -split ';' | Where-Object { 
        [string]::IsNullOrWhiteSpace($_) -eq $false -and 
        $_.TrimEnd('\') -ne $BinDir.TrimEnd('\') 
    })
    $newPath = $pathParts -join ';'
    [Environment]::SetEnvironmentVariable("Path", $newPath, [EnvironmentVariableTarget]::Machine)
    Write-Host "      Cleaned System PATH." -ForegroundColor Gray
} catch {
    Write-Warning "Could not update machine PATH: $_"
}

# 2. Clean Windows Terminal Profiles & Fragments
Write-Host "[2/3] Cleaning Windows Terminal profiles and fragments..." -ForegroundColor Cyan

# 2A. Remove JSON Fragments
$fragmentDirs = @(
    "C:\ProgramData\Microsoft\Windows Terminal\Fragments\ntksh",
    "$env:LOCALAPPDATA\Microsoft\Windows Terminal\Fragments\ntksh"
)
try {
    $userProfiles = Get-ChildItem -Path "C:\Users" -Directory -ErrorAction SilentlyContinue | Where-Object { 
        $_.Name -notin @('Public', 'Default', 'Default User', 'All Users') 
    }
    foreach ($u in $userProfiles) {
        $fragmentDirs += "$($u.FullName)\AppData\Local\Microsoft\Windows Terminal\Fragments\ntksh"
    }
} catch {}

foreach ($fDir in $fragmentDirs) {
    try {
        if (Test-Path -Path $fDir -ErrorAction SilentlyContinue) {
            Remove-Item -Path $fDir -Recurse -Force -ErrorAction SilentlyContinue
            Write-Host "      Removed Terminal Fragment folder: $fDir" -ForegroundColor Gray
        }
    } catch {}
}

# 2B. Clean settings.json files
function Remove-TerminalProfile {
    param([string]$SettingsPath)
    if (-not (Test-Path -Path $SettingsPath -ErrorAction SilentlyContinue)) { return }
    try {
        $raw = [System.IO.File]::ReadAllText($SettingsPath, [System.Text.Encoding]::UTF8)
        if ($raw -notlike "*ksh.exe*" -and $raw -notlike "*ntksh*") { return }

        $changed = $false
        # Try JSON parse first
        try {
            $clean = $raw -replace '(?m)^\s*//.*$', ''
            $clean = $clean -replace '/\*[\s\S]*?\*/', ''
            $json = $clean | ConvertFrom-Json
            if ($json) {
                if ($json.profiles.PSObject.Properties['list']) {
                    $filtered = @($json.profiles.list | Where-Object { 
                        $_.name -ne "ntksh" -and 
                        $_.name -ne "ntksh (KornShell)" -and 
                        $_.commandline -notlike "*ksh.exe*" 
                    })
                    if ($filtered.Count -ne $json.profiles.list.Count) {
                        $json.profiles.list = $filtered
                        $changed = $true
                    }
                } elseif ($json.PSObject.Properties['profiles'] -and $json.profiles -is [System.Collections.IEnumerable]) {
                    $filtered = @($json.profiles | Where-Object { 
                        $_.name -ne "ntksh" -and 
                        $_.name -ne "ntksh (KornShell)" -and 
                        $_.commandline -notlike "*ksh.exe*" 
                    })
                    if ($filtered.Count -ne $json.profiles.Count) {
                        $json.profiles = $filtered
                        $changed = $true
                    }
                }
                if ($changed) {
                    $newJson = $json | ConvertTo-Json -Depth 32
                    [System.IO.File]::WriteAllText($SettingsPath, $newJson, [System.Text.Encoding]::UTF8)
                    Write-Host "      Removed ntksh profile from: $SettingsPath" -ForegroundColor Gray
                    return
                }
            }
        } catch {}

        # Regex fallback to preserve comments if JSON parse failed or had custom syntax
        $regexBlock = '(?s)\s*\{\s*"guid"\s*:\s*"\{9f19a0a4-d13c-47bc-877f-1d8f7b2c62c3\}".*?\},?'
        if ($raw -match $regexBlock) {
            $cleaned = [System.Text.RegularExpressions.Regex]::Replace($raw, $regexBlock, '')
            [System.IO.File]::WriteAllText($SettingsPath, $cleaned, [System.Text.Encoding]::UTF8)
            Write-Host "      Removed ntksh profile via regex from: $SettingsPath" -ForegroundColor Gray
        }
    } catch {
        # Ignore individual terminal profile cleanup errors on uninstall
    }
}

$potentialPaths = @(
    "$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json",
    "$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json",
    "$env:LOCALAPPDATA\Microsoft\Windows Terminal\settings.json"
)
$userProfiles = Get-ChildItem -Path "C:\Users" -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -notin @('Public', 'Default', 'Default User', 'All Users') }
foreach ($u in $userProfiles) {
    $potentialPaths += "$($u.FullName)\AppData\Local\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json"
    $potentialPaths += "$($u.FullName)\AppData\Local\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json"
    $potentialPaths += "$($u.FullName)\AppData\Local\Microsoft\Windows Terminal\settings.json"
}

$processed = @{}
foreach ($p in $potentialPaths) {
    if (-not $processed.ContainsKey($p.ToLower())) {
        $processed[$p.ToLower()] = $true
        Remove-TerminalProfile -SettingsPath $p
    }
}

# 3. Remove Program Files directory
Write-Host "[3/3] Removing files from $InstallRoot..." -ForegroundColor Cyan
if (Test-Path $InstallRoot) {
    # If running uninstall.ps1 directly from InstallRoot, we spawn a brief helper cmd to delete the folder
    $cmdDel = "cmd.exe /c timeout /t 1 /nobreak >nul & rmdir /s /q `"$InstallRoot`""
    Start-Process -FilePath "cmd.exe" -ArgumentList "/c timeout /t 1 /nobreak >nul & rmdir /s /q `"$InstallRoot`"" -WindowStyle Hidden
    Write-Host "      Queued removal of '$InstallRoot'." -ForegroundColor Gray
}

Write-Host "`n[SUCCESS] ntksh has been completely uninstalled from this system.`n" -ForegroundColor Green

if (-not $Unattended) {
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}
