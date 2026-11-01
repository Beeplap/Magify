@echo off
setlocal
pushd "%~dp0magify"
call build.bat
set "MAGIFY_BUILD_RESULT=%ERRORLEVEL%"
popd
if not "%MAGIFY_BUILD_RESULT%"=="0" exit /b %MAGIFY_BUILD_RESULT%

if not exist "%~dp0build" mkdir "%~dp0build"
copy /y "%~dp0magify\build\magify.exe" "%~dp0build\magify.exe" >nul
if errorlevel 1 exit /b 1
echo Built %~dp0build\magify.exe
exit /b 0
