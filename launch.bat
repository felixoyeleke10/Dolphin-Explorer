@echo off
setlocal

set "EXE=DolphinExplorer.exe"
set "BUILD=%~dp0build_mingw"
set "MINGW=C:\Qt\Tools\mingw1310_64\bin"

rem Check exe exists
if not exist "%BUILD%\%EXE%" (
    echo [ERROR] %EXE% not found in build_mingw.
    echo         Run build_mingw.bat first.
    pause
    exit /b 1
)

rem Kill any running instance
taskkill /F /IM "%EXE%" >nul 2>&1

rem Ensure MinGW runtime DLLs are present
copy /Y "%MINGW%\libstdc++-6.dll"     "%BUILD%\" >nul
copy /Y "%MINGW%\libgcc_s_seh-1.dll"  "%BUILD%\" >nul
copy /Y "%MINGW%\libwinpthread-1.dll" "%BUILD%\" >nul

rem Launch from the build directory
start "" /D "%BUILD%" "%BUILD%\%EXE%"
