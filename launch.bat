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

rem Ensure GDAL (+ PROJ, libtiff, …) DLLs and CRS/driver data are present (Debug).
set "GDAL_VCPKG=%USERPROFILE%\vcpkg\installed\x64-windows"
if exist "%GDAL_VCPKG%\debug\bin" copy /Y "%GDAL_VCPKG%\debug\bin\*.dll" "%BUILD%\" >nul
if exist "%GDAL_VCPKG%\share\gdal" (
    if not exist "%BUILD%\gdal-data" mkdir "%BUILD%\gdal-data"
    xcopy /Y /E /Q "%GDAL_VCPKG%\share\gdal\*" "%BUILD%\gdal-data\" >nul
)
if exist "%GDAL_VCPKG%\share\proj" (
    if not exist "%BUILD%\proj-data" mkdir "%BUILD%\proj-data"
    xcopy /Y /E /Q "%GDAL_VCPKG%\share\proj\*" "%BUILD%\proj-data\" >nul
)

rem Launch from the build directory
start "" /D "%BUILD%" "%BUILD%\%EXE%"
