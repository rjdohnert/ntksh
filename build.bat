@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
set BIN_DIR=%ROOT%bin
set SRC_DIR=%ROOT%src

if not exist "%BIN_DIR%" mkdir "%BIN_DIR%"

if exist "%BIN_DIR%\ksh.old" del /f /q "%BIN_DIR%\ksh.old" 2>nul
if exist "%BIN_DIR%\ksh.exe" ren "%BIN_DIR%\ksh.exe" "ksh.old" 2>nul
if exist "%ROOT%ksh.old" del /f /q "%ROOT%ksh.old" 2>nul
if exist "%ROOT%ksh.exe" ren "%ROOT%ksh.exe" "ksh.old" 2>nul

set CFLAGS=-O2 -Wall -Wextra -fms-extensions -municode -I"%ROOT%include"
set LIBS=-L"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64" -L"C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\ucrt\x64" -ladvapi32 -lshell32 -luser32 -lpsapi -liphlpapi -lws2_32 -lsetupapi -lcfgmgr32 -lrstrtmgr -lwinhttp

if /i "%1"=="amalgamate" goto build_amalgamate
if /i "%1"=="/amalgamate" goto build_amalgamate
if /i "%1"=="-amalgamate" goto build_amalgamate
if /i "%1"=="--amalgamate" goto build_amalgamate

echo [BUILD] Compiling modular ntksh sources...
clang %CFLAGS% -o "%BIN_DIR%\ksh.exe" ^
    "%SRC_DIR%\main.c" ^
    "%SRC_DIR%\core\*.c" ^
    "%SRC_DIR%\batch\*.c" ^
    "%SRC_DIR%\builtins\*.c" ^
    "%SRC_DIR%\help\*.c" ^
    %LIBS%
if errorlevel 1 goto error
goto finish

:build_amalgamate
echo [BUILD] Compiling single-file amalgamation src/ksh.c...
clang -O2 -Wall -Wextra -fms-extensions -municode -o "%BIN_DIR%\ksh.exe" "%SRC_DIR%\ksh.c" %LIBS%
if errorlevel 1 goto error

:finish
copy /Y "%BIN_DIR%\ksh.exe" "%ROOT%ksh.exe" >nul
echo [SUCCESS] Binary created: bin\ksh.exe and ksh.exe
exit /b 0

:error
echo [ERROR] Build failed!
exit /b 1
