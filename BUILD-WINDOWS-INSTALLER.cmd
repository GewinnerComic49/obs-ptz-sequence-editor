@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if errorlevel 1 (
  echo Open this script in the x64 Native Tools Command Prompt for VS 2022.
  exit /b 1
)
where pwsh >nul 2>nul
if errorlevel 1 (
  echo PowerShell 7.2 or newer is required.
  exit /b 1
)
where cmake >nul 2>nul
if errorlevel 1 (
  echo CMake 3.28 or newer is required.
  exit /b 1
)
set CI=1
pwsh -NoProfile -File .github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
if errorlevel 1 exit /b 1
pwsh -NoProfile -File .github\scripts\Package-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
if errorlevel 1 exit /b 1
echo Installer: release\obs-ptz-0.19.2-windows-x64-Installer.exe
endlocal
