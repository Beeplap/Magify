@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo          Magify — Standalone Installer Setup
echo =======================================================
echo.

if "%1"=="--uninstall" goto do_uninstall
if "%1"=="/u" goto do_uninstall
if "%1"=="-u" goto do_uninstall

set "INSTALL_DIR=%LOCALAPPDATA%\Programs\Magify"

:: Find magify.exe
set "SRC_EXE=%~dp0magify\build\magify.exe"
if not exist "!SRC_EXE!" (
    if exist "%~dp0build\magify.exe" (
        set "SRC_EXE=%~dp0build\magify.exe"
    ) else if exist "%~dp0magify.exe" (
        set "SRC_EXE=%~dp0magify.exe"
    ) else (
        echo [INFO] magify.exe not found. Building now...
        if exist "%~dp0magify\build.bat" (
            pushd "%~dp0magify"
            call build.bat
            popd
            set "SRC_EXE=%~dp0magify\build\magify.exe"
        ) else if exist "%~dp0build.bat" (
            call "%~dp0build.bat"
            set "SRC_EXE=%~dp0build\magify.exe"
        )
    )
)

if not exist "!SRC_EXE!" (
    echo [ERROR] Could not find or build magify.exe!
    exit /b 1
)

echo [INFO] Stopping any running Magify processes...
taskkill /f /im magify.exe >nul 2>nul

echo [INFO] Deploying to !INSTALL_DIR!...
if not exist "!INSTALL_DIR!" mkdir "!INSTALL_DIR!"
copy /y "!SRC_EXE!" "!INSTALL_DIR!\magify.exe" >nul
if exist "%~dp0magify\build\pdfium.dll" copy /y "%~dp0magify\build\pdfium.dll" "!INSTALL_DIR!\pdfium.dll" >nul
if exist "%~dp0build\pdfium.dll" copy /y "%~dp0build\pdfium.dll" "!INSTALL_DIR!\pdfium.dll" >nul
if exist "%~dp0pdfium.dll" copy /y "%~dp0pdfium.dll" "!INSTALL_DIR!\pdfium.dll" >nul
if exist "%~dp0magify\assets\magify.ico" copy /y "%~dp0magify\assets\magify.ico" "!INSTALL_DIR!\magify.ico" >nul
if exist "%~dp0assets\magify.ico" copy /y "%~dp0assets\magify.ico" "!INSTALL_DIR!\magify.ico" >nul

echo [INFO] Registering Windows 11 Modern Context Menu and Auto-Start...
"!INSTALL_DIR!\magify.exe" --install

echo [INFO] Starting Magify Background Daemon...
start "" "!INSTALL_DIR!\magify.exe" --background

echo [INFO] Creating Desktop and Start Menu shortcuts...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ws = New-Object -ComObject WScript.Shell; " ^
    "$dDir = [Environment]::GetFolderPath('Desktop'); " ^
    "if (-not (Test-Path $dDir)) { New-Item -ItemType Directory -Path $dDir -Force | Out-Null }; " ^
    "$dPath = $dDir + '\Magify.lnk'; " ^
    "$s1 = $ws.CreateShortcut($dPath); " ^
    "$s1.TargetPath = '!INSTALL_DIR!\magify.exe'; " ^
    "$s1.WorkingDirectory = '!INSTALL_DIR!'; " ^
    "$s1.Description = 'Magify - 120fps Instant Radial Utilities'; " ^
    "$s1.IconLocation = '!INSTALL_DIR!\magify.exe,0'; " ^
    "$s1.Save(); " ^
    "$smDir = [Environment]::GetFolderPath('StartMenu') + '\Programs'; " ^
    "if (-not (Test-Path $smDir)) { New-Item -ItemType Directory -Path $smDir -Force | Out-Null }; " ^
    "$s2 = $ws.CreateShortcut($smDir + '\Magify.lnk'); " ^
    "$s2.TargetPath = '!INSTALL_DIR!\magify.exe'; " ^
    "$s2.WorkingDirectory = '!INSTALL_DIR!'; " ^
    "$s2.Description = 'Magify - 120fps Instant Radial Utilities'; " ^
    "$s2.IconLocation = '!INSTALL_DIR!\magify.exe,0'; " ^
    "$s2.Save();"

echo.
echo =======================================================
echo  ✨ Magify Installed Successfully!
echo.
echo  Binary:     !INSTALL_DIR!\magify.exe
echo  Menu:       Windows 11 Right-Click Context Menu (Active)
echo  Shortcuts:  Desktop and Start Menu Created
echo =======================================================
exit /b 0

:do_uninstall
set "INSTALL_DIR=%LOCALAPPDATA%\Programs\Magify"
echo [INFO] Uninstalling Magify...
taskkill /f /im magify.exe >nul 2>nul
if exist "!INSTALL_DIR!\magify.exe" (
    "!INSTALL_DIR!\magify.exe" --uninstall
)
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$dPath = [Environment]::GetFolderPath('Desktop') + '\Magify.lnk'; " ^
    "if (Test-Path $dPath) { Remove-Item $dPath -Force }; " ^
    "$smPath = [Environment]::GetFolderPath('StartMenu') + '\Programs\Magify.lnk'; " ^
    "if (Test-Path $smPath) { Remove-Item $smPath -Force };"
if exist "!INSTALL_DIR!" (
    rmdir /s /q "!INSTALL_DIR!" >nul 2>nul
)
echo [INFO] Magify uninstalled cleanly.
exit /b 0
