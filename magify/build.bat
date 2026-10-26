@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo         Building Magify (Pure C11 / Win32)
echo =======================================================

if not exist "build" mkdir build

where gcc >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Detected GCC toolchain.
    echo [1/2] Compiling resources with windres...
    windres -I. -Iassets assets\magify.rc -O coff -o build\magify.res
    if !ERRORLEVEL! NEQ 0 (
        echo [ERROR] Resource compilation failed.
        exit /b 1
    )

    echo [2/2] Compiling with GCC...
    gcc -std=c11 -O3 -Wall -Wextra -municode -mwindows ^
        -Iinclude -Ithird_party ^
        src\main.c src\ipc.c src\registry.c src\com_extension.c src\ui_overlay.c ^
        src\ops_common.c src\ops_image.c src\ops_pdf.c ^
        build\magify.res ^
        -lgdi32 -luser32 -lshell32 -lole32 -luuid -lwindowscodecs -lshlwapi -ldwmapi -lcomctl32 -lcomdlg32 -ladvapi32 -lgdiplus ^
        -o build\magify.exe

    if !ERRORLEVEL! NEQ 0 (
        echo [ERROR] GCC compilation failed.
        exit /b 1
    )
    goto build_ok
)

:: Otherwise use MSVC
where cl >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    set "VS_PATH=C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    if not exist "!VS_PATH!" (
        set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    )
    if exist "!VS_PATH!" (
        echo [INFO] Initializing MSVC x64 build environment...
        call "!VS_PATH!" x64 >nul
    ) else (
        echo [ERROR] Neither GCC nor MSVC vcvarsall.bat found.
        exit /b 1
    )
)

echo [1/2] Compiling resources with rc.exe...
rc.exe /nologo /fo build\magify.res assets\magify.rc
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Resource compilation failed.
    exit /b 1
)

echo [2/2] Compiling C source files with cl.exe...
cl.exe /nologo /O2 /W4 /WX /std:c11 /utf-8 ^
    /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    /Iinclude /Ithird_party ^
    src\main.c src\ipc.c src\registry.c src\com_extension.c src\ui_overlay.c ^
    src\ops_common.c src\ops_image.c src\ops_pdf.c ^
    build\magify.res ^
    /link /SUBSYSTEM:WINDOWS /OUT:build\magify.exe ^
    user32.lib gdi32.lib shell32.lib ole32.lib uuid.lib windowscodecs.lib shlwapi.lib dwmapi.lib comctl32.lib comdlg32.lib advapi32.lib gdiplus.lib

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Linking failed.
    exit /b 1
)

:build_ok
echo.
echo =======================================================
echo  ✨ Build Succeeded with ZERO warnings: build\magify.exe
echo =======================================================
dir build\magify.exe | findstr /i "magify.exe"
