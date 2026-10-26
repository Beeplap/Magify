@echo off
setlocal enabledelayedexpansion

echo =======================================================
echo          Building Magify (Pure C / Win32)
echo =======================================================

:: Locate MSVC vcvarsall.bat if cl is not in PATH
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
        echo [ERROR] MSVC vcvarsall.bat not found.
        exit /b 1
    )
)

:: Ensure output directories exist
if not exist "bin" mkdir bin
if not exist "build" mkdir build

:: Step 1: Compile Windows Resource file (Icon + Manifest + Version)
echo [1/3] Compiling resources...
rc.exe /nologo /fo build\resource.res resources\resource.rc
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Resource compilation failed.
    exit /b 1
)

:: Step 2: Compile and Link Magify
echo [2/3] Compiling C / C++ source files...
cl.exe /nologo /O2 /W3 /std:c11 /utf-8 ^
    /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    /Iinclude /Ithird_party ^
    src\main.c ^
    src\shell\mg_shell.c ^
    src\image\mg_image.c ^
    src\pdf\mg_pdf.c ^
    src\util\mg_arena.c ^
    third_party\miniz.c ^
    third_party\miniz_tdef.c ^
    third_party\miniz_tinfl.c ^
    third_party\miniz_zip.c ^
    /c /Fo"build\\"

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] C compilation failed.
    exit /b 1
)

:: Compile UI with C++ for GDI+ support
cl.exe /nologo /O2 /W3 /std:c++17 /EHsc /utf-8 ^
    /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS ^
    /Iinclude /Ithird_party ^
    src\ui\mg_ui.cpp ^
    /c /Fo"build\\mg_ui.obj"

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] UI compilation failed.
    exit /b 1
)

:: Step 3: Link into standalone Executable
echo [3/3] Linking into bin\magify.exe...
link.exe /nologo /OUT:bin\magify.exe ^
    build\main.obj ^
    build\mg_shell.obj ^
    build\mg_ui.obj ^
    build\mg_image.obj ^
    build\mg_pdf.obj ^
    build\mg_arena.obj ^
    build\miniz.obj ^
    build\miniz_tdef.obj ^
    build\miniz_tinfl.obj ^
    build\miniz_zip.obj ^
    build\resource.res ^
    /SUBSYSTEM:WINDOWS ^
    /MANIFEST:EMBED /MANIFESTINPUT:resources\app.manifest ^
    user32.lib gdi32.lib gdiplus.lib msimg32.lib shell32.lib ole32.lib shlwapi.lib advapi32.lib comdlg32.lib

if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Linking failed.
    exit /b 1
)

echo.
echo =======================================================
echo  ✨ Build Succeeded: bin\magify.exe
echo =======================================================
dir bin\magify.exe | findstr /i "magify.exe"
