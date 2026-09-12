@echo off
setlocal DisableDelayedExpansion
title Shipwright-MP - Resume Existing Build
rem The execution-policy argument applies only to this one PowerShell process.
rem It does not change system-wide policy or disable antivirus/SmartScreen.
set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "PS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
if not exist "%~dp0build-helper\WindowsHelper.ps1" (
  echo Helper files are missing. Right-click the downloaded ZIP and choose Extract All.
  echo Keep all files and folders together. Do not run this inside the ZIP viewer.
  pause
  exit /b 1
)
"%PS%" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-helper\WindowsHelper.ps1" -Mode Build -Resume
set "RESULT=%ERRORLEVEL%"
echo.
if not "%RESULT%"=="0" echo The task stopped. See BUILD_LOGS\LATEST.txt or the error shown above.
echo This window stays open so you can read the result.
pause
exit /b %RESULT%
