@echo off
setlocal
cd /d "%~dp0"
echo ========================================================
echo   ntksh (KornShell for Windows) Installer Launcher
echo ========================================================
echo.

:: Check for Administrator elevation
net session >nul 2>&1
if %errorLevel% == 0 (
    echo [*] Running with Administrator privileges...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1"
) else (
    echo [*] Requesting Administrator elevation...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& { Start-Process powershell.exe -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%~dp0install.ps1""' -Verb RunAs }"
)

endlocal
