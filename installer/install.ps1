# ==============================================================================
# ntksh Installer for Windows
# Installs ksh.exe to C:\Program Files\ntksh\bin\
# Adds ntksh to system PATH
# Configures Windows Terminal profile automatically (via Fragments & settings.json)
# ==============================================================================

[CmdletBinding()]
param(
    [switch]$Unattended
)

# Ensure administrator privileges
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[*] Requesting Administrator privileges to install to Program Files..." -ForegroundColor Cyan
    $scriptPath = $MyInvocation.MyCommand.Path
    if (-not $scriptPath) {
        $scriptPath = Join-Path $PSScriptRoot "install.ps1"
    }
    $argsList = "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`""
    if ($Unattended) { $argsList += " -Unattended" }
    
    try {
        Start-Process -FilePath "powershell.exe" -ArgumentList $argsList -Verb RunAs
        exit 0
    } catch {
        Write-Error "Administrator elevation was cancelled or failed: $_"
        exit 1
    }
}

$SourceDir    = $PSScriptRoot
$InstallRoot  = "C:\Program Files\ntksh"
$BinDir       = Join-Path $InstallRoot "bin"
$ExeTarget    = Join-Path $BinDir "ksh.exe"
$DocTarget    = Join-Path $InstallRoot "user_guide.md"
$UninstTarget = Join-Path $InstallRoot "uninstall.ps1"

Write-Host "`n========================================================" -ForegroundColor Green
Write-Host "         ntksh (KornShell for Windows) Installer         " -ForegroundColor White
Write-Host "========================================================`n" -ForegroundColor Green

# 1. Create Target Directories
Write-Host "[1/5] Creating installation directories..." -ForegroundColor Cyan
if (-not (Test-Path -Path $BinDir -ErrorAction SilentlyContinue)) {
    New-Item -ItemType Directory -Path $BinDir -Force -ErrorAction SilentlyContinue | Out-Null
}
Write-Host "      Target directory: $BinDir" -ForegroundColor Gray

# 2. Copy Executable and Documentation
Write-Host "[2/5] Copying binaries and documentation..." -ForegroundColor Cyan
$KshExeSource = Join-Path $SourceDir "ksh.exe"
$UserGuideSource = Join-Path $SourceDir "user_guide.md"

if (-not (Test-Path -Path $KshExeSource -ErrorAction SilentlyContinue)) {
    $KshExeSource = Join-Path (Split-Path $SourceDir -Parent) "bin\ksh.exe"
}
if (-not (Test-Path -Path $UserGuideSource -ErrorAction SilentlyContinue)) {
    $UserGuideSource = Join-Path (Split-Path $SourceDir -Parent) "user_guide.md"
}

if (-not (Test-Path -Path $KshExeSource -ErrorAction SilentlyContinue)) {
    Write-Error "Source ksh.exe not found at $KshExeSource!"
    exit 1
}

Copy-Item -Path $KshExeSource -Destination $ExeTarget -Force
Write-Host "      Installed: $ExeTarget" -ForegroundColor Gray

if (Test-Path -Path $UserGuideSource -ErrorAction SilentlyContinue) {
    Copy-Item -Path $UserGuideSource -Destination $DocTarget -Force
    Write-Host "      Installed: $DocTarget" -ForegroundColor Gray
}

# Copy uninstaller to InstallRoot
$UninstallerSource = Join-Path $SourceDir "uninstall.ps1"
if (Test-Path -Path $UninstallerSource -ErrorAction SilentlyContinue) {
    Copy-Item -Path $UninstallerSource -Destination $UninstTarget -Force
}

# 3. Add to System PATH
Write-Host "[3/5] Updating system PATH environment variable..." -ForegroundColor Cyan
try {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", [EnvironmentVariableTarget]::Machine)
    $pathParts = @($machinePath -split ';' | Where-Object { [string]::IsNullOrWhiteSpace($_) -eq $false })
    
    $alreadyInPath = $false
    foreach ($part in $pathParts) {
        if ($part.TrimEnd('\') -eq $BinDir.TrimEnd('\')) {
            $alreadyInPath = $true
            break
        }
    }

    if (-not $alreadyInPath) {
        $newPath = ($pathParts + @($BinDir)) -join ';'
        [Environment]::SetEnvironmentVariable("Path", $newPath, [EnvironmentVariableTarget]::Machine)
        $env:PATH = "$env:PATH;$BinDir"
        Write-Host "      Added '$BinDir' to system PATH." -ForegroundColor Green
    } else {
        Write-Host "      '$BinDir' is already present in system PATH." -ForegroundColor Gray
    }

    # Broadcast environment change
    $HWND_BROADCAST = [IntPtr]0xffff
    $WM_SETTINGCHANGE = 0x001a
    $result = [IntPtr]::Zero
    $type = Add-Type -Namespace Win32 -Name NativeMethods -MemberDefinition @'
        [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Auto)]
        public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out IntPtr lpdwResult);
'@ -PassThru -ErrorAction SilentlyContinue

    if ($type) {
        [Win32.NativeMethods]::SendMessageTimeout($HWND_BROADCAST, $WM_SETTINGCHANGE, [UIntPtr]::Zero, "Environment", 2, 5000, [ref]$result) | Out-Null
    }
} catch {
    Write-Warning "Could not update machine PATH. Adding to user PATH instead: $_"
    $userPath = [Environment]::GetEnvironmentVariable("Path", [EnvironmentVariableTarget]::User)
    if ($userPath -notlike "*$BinDir*") {
        [Environment]::SetEnvironmentVariable("Path", "$userPath;$BinDir", [EnvironmentVariableTarget]::User)
    }
}

# 4. Configure Windows Terminal Profile
Write-Host "[4/5] Configuring Windows Terminal profiles..." -ForegroundColor Cyan

$terminalConfigured = $false

# 4A. Deploy official Microsoft Windows Terminal JSON Fragments
# Windows Terminal automatically loads profile fragments from these folders on startup.
$fragmentDirs = [System.Collections.Generic.List[string]]::new()
$fragmentDirs.Add("C:\ProgramData\Microsoft\Windows Terminal\Fragments\ntksh")

if ($env:LOCALAPPDATA) {
    $fragmentDirs.Add("$env:LOCALAPPDATA\Microsoft\Windows Terminal\Fragments\ntksh")
}

$userDirs = @()
try {
    $userDirs = Get-ChildItem -Path "C:\Users" -Directory -ErrorAction SilentlyContinue | Where-Object { 
        $_.Name -notin @('Public', 'Default', 'Default User', 'All Users') 
    }
    foreach ($u in $userDirs) {
        $fragmentDirs.Add("$($u.FullName)\AppData\Local\Microsoft\Windows Terminal\Fragments\ntksh")
    }
} catch {}

$fragmentContent = @"
{
  "profiles": [
    {
      "name": "ntksh (KornShell)",
      "commandline": "\"$ExeTarget\"",
      "icon": "\"$ExeTarget\"",
      "startingDirectory": "%USERPROFILE%",
      "hidden": false
    }
  ]
}
"@

foreach ($fDir in $fragmentDirs) {
    try {
        if (-not (Test-Path -Path $fDir -ErrorAction SilentlyContinue)) {
            New-Item -ItemType Directory -Path $fDir -Force -ErrorAction SilentlyContinue | Out-Null
        }
        $fFile = Join-Path $fDir "ntksh.json"
        [System.IO.File]::WriteAllText($fFile, $fragmentContent, [System.Text.Encoding]::UTF8)
        $terminalConfigured = $true
        Write-Host "      Created Windows Terminal Fragment: $fFile" -ForegroundColor Green
    } catch {
        # Ignore individual access denied on other user directories
    }
}

# 4B. Direct settings.json Profile Injection (targeted insertion preserving all comments and format)
function Update-TerminalSettingsFile {
    param([string]$SettingsPath)

    try {
        if (-not (Test-Path -Path $SettingsPath -ErrorAction SilentlyContinue)) { return $false }
        $raw = [System.IO.File]::ReadAllText($SettingsPath, [System.Text.Encoding]::UTF8)
        if ([string]::IsNullOrWhiteSpace($raw)) { return $false }

        if ($raw -like "*ksh.exe*") {
            Write-Host "      ntksh profile already present in: $SettingsPath" -ForegroundColor Gray
            return $true
        }

        # Backup settings.json
        try {
            Copy-Item -Path $SettingsPath -Destination "$SettingsPath.bak" -Force -ErrorAction SilentlyContinue
        } catch {}

        $profileItem = @"
            {
                "guid": "{9f19a0a4-d13c-47bc-877f-1d8f7b2c62c3}",
                "name": "ntksh (KornShell)",
                "commandline": "\"$ExeTarget\"",
                "icon": "\"$ExeTarget\"",
                "startingDirectory": "%USERPROFILE%",
                "hidden": false
            }
"@

        $updated = $false

        # Match modern Windows Terminal: "list": [
        $regexList = [System.Text.RegularExpressions.Regex]::Match($raw, '("list"\s*:\s*\[)')
        if ($regexList.Success) {
            $insertIdx = $regexList.Index + $regexList.Length
            $afterBracket = $raw.Substring($insertIdx)
            $isEmpty = $afterBracket -match '^\s*\]'
            $comma = if ($isEmpty) { "" } else { "," }
            $newRaw = $raw.Substring(0, $insertIdx) + "`r`n" + $profileItem + $comma + $afterBracket
            [System.IO.File]::WriteAllText($SettingsPath, $newRaw, [System.Text.Encoding]::UTF8)
            $updated = $true
        } else {
            # Match legacy Windows Terminal: "profiles": [
            $regexProfiles = [System.Text.RegularExpressions.Regex]::Match($raw, '("profiles"\s*:\s*\[)')
            if ($regexProfiles.Success) {
                $insertIdx = $regexProfiles.Index + $regexProfiles.Length
                $afterBracket = $raw.Substring($insertIdx)
                $isEmpty = $afterBracket -match '^\s*\]'
                $comma = if ($isEmpty) { "" } else { "," }
                $newRaw = $raw.Substring(0, $insertIdx) + "`r`n" + $profileItem + $comma + $afterBracket
                [System.IO.File]::WriteAllText($SettingsPath, $newRaw, [System.Text.Encoding]::UTF8)
                $updated = $true
            }
        }

        if ($updated) {
            Write-Host "      Injected ntksh profile into: $SettingsPath" -ForegroundColor Green
            return $true
        }
    } catch {
        Write-Warning "Could not update $SettingsPath`: $_"
    }
    return $false
}

$potentialSettingsPaths = [System.Collections.Generic.List[string]]::new()

if ($env:LOCALAPPDATA) {
    $potentialSettingsPaths.Add("$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json")
    $potentialSettingsPaths.Add("$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json")
    $potentialSettingsPaths.Add("$env:LOCALAPPDATA\Packages\Microsoft.WindowsTerminalCanary_8wekyb3d8bbwe\LocalState\settings.json")
    $potentialSettingsPaths.Add("$env:LOCALAPPDATA\Microsoft\Windows Terminal\settings.json")
}

foreach ($u in $userDirs) {
    $uPath = $u.FullName
    $potentialSettingsPaths.Add("$uPath\AppData\Local\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json")
    $potentialSettingsPaths.Add("$uPath\AppData\Local\Packages\Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe\LocalState\settings.json")
    $potentialSettingsPaths.Add("$uPath\AppData\Local\Packages\Microsoft.WindowsTerminalCanary_8wekyb3d8bbwe\LocalState\settings.json")
    $potentialSettingsPaths.Add("$uPath\AppData\Local\Microsoft\Windows Terminal\settings.json")
}

$processedPaths = @{}
foreach ($path in $potentialSettingsPaths) {
    $key = $path.ToLowerInvariant()
    if (-not $processedPaths.ContainsKey($key)) {
        $processedPaths[$key] = $true
        if (Update-TerminalSettingsFile -SettingsPath $path) {
            $terminalConfigured = $true
        }
    }
}

if (-not $terminalConfigured) {
    Write-Host "      Windows Terminal was not detected on this system." -ForegroundColor Gray
}

# 5. Verification
Write-Host "[5/5] Verifying installed binary..." -ForegroundColor Cyan
if (Test-Path -Path $ExeTarget -ErrorAction SilentlyContinue) {
    $verOutput = & "$ExeTarget" --version 2>&1
    Write-Host "      $verOutput" -ForegroundColor Green
    Write-Host "`n[SUCCESS] ntksh has been successfully installed to:" -ForegroundColor Green
    Write-Host "          $ExeTarget" -ForegroundColor White
    Write-Host "          Documentation: $DocTarget" -ForegroundColor White
    Write-Host "          System PATH: Updated" -ForegroundColor White
    if ($terminalConfigured) {
        Write-Host "          Windows Terminal: Profile Configured (Fragments + Settings)" -ForegroundColor White
    }
} else {
    Write-Error "Installation verification failed! Binary not found at $ExeTarget"
    exit 1
}

Write-Host "`nYou can launch ntksh by typing 'ksh' in any new PowerShell, CMD, or Windows Terminal window.`n" -ForegroundColor Yellow

if (-not $Unattended) {
    Write-Host "Press any key to exit..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
}
