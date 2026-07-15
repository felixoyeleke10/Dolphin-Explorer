@echo off
setlocal

REM Dolphin Explorer - MSVC build using Ninja (Qt 6.7.x msvc2019_64)
REM Auto-detects the local Visual Studio + Qt install so the SAME script works
REM on every dev machine (VS 18 Community, VS 2022 Community/BuildTools, ...).
REM Do NOT hard-code machine-specific toolchain paths here again - that breaks
REM the build for every other machine on the next pull.
REM
REM Raster support needs GDAL via vcpkg (one-time, into %USERPROFILE%\vcpkg):
REM   git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
REM   %USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
REM   %USERPROFILE%\vcpkg\vcpkg install "gdal[core,png,jpeg]:x64-windows"
REM launch.bat deploys the GDAL DLLs + PROJ/GDAL data next to the exe.

set BUILD_DIR=build_mingw

REM ---- Visual Studio: first install that has vcvars64.bat wins --------------
set "VSROOT="
for %%V in (
    "C:\Program Files\Microsoft Visual Studio\18\Community"
    "C:\Program Files\Microsoft Visual Studio\18\Professional"
    "C:\Program Files\Microsoft Visual Studio\2022\Community"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
) do (
    if not defined VSROOT if exist "%%~V\VC\Auxiliary\Build\vcvars64.bat" set "VSROOT=%%~V"
)
if not defined VSROOT (
    echo Could not find a Visual Studio install with vcvars64.bat.
    if not defined DOLPHIN_NONINTERACTIVE pause
    exit /b 1
)
echo Visual Studio: %VSROOT%

REM ---- Qt: newest known 6.7.x msvc kit wins ----------------------------------
set "QT_DIR="
for %%Q in ("C:/Qt/6.7.3/msvc2019_64" "C:/Qt/6.7.2/msvc2019_64") do (
    if not defined QT_DIR if exist "%%~Q/bin/windeployqt.exe" set "QT_DIR=%%~Q"
)
if not defined QT_DIR (
    echo Could not find Qt 6.7.x msvc2019_64 under C:\Qt.
    if not defined DOLPHIN_NONINTERACTIVE pause
    exit /b 1
)
echo Qt:            %QT_DIR%

REM ---- Ninja: prefer the copy shipped inside the detected VS -----------------
set "NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%NINJA%" set "NINJA=C:\Qt\Tools\Ninja\ninja.exe"
if not exist "%NINJA%" (
    echo Could not find ninja.exe in Visual Studio or C:\Qt\Tools\Ninja.
    if not defined DOLPHIN_NONINTERACTIVE pause
    exit /b 1
)
set "NINJA=%NINJA:\=/%"
echo Ninja:         %NINJA%

REM GDAL (+ PROJ, libtiff, ...) provided by vcpkg. Forward slashes for CMake.
set "VCPKG_INSTALLED=%USERPROFILE:\=/%/vcpkg/installed/x64-windows"

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"

REM CMAKE_CXX_COMPILER=cl resolves from the vcvars PATH - never hard-code a
REM versioned MSVC bin path (it changes with every toolset update).
cmake .. -G "Ninja" ^
    -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%;%VCPKG_INSTALLED%" ^
    -DCMAKE_BUILD_TYPE=Debug

if errorlevel 1 (
    echo CMake configure failed.
    if not defined DOLPHIN_NONINTERACTIVE pause
    exit /b 1
)

cmake --build . --parallel

if errorlevel 1 (
    echo Build failed.
    if not defined DOLPHIN_NONINTERACTIVE pause
    exit /b 1
)

"%QT_DIR%/bin/windeployqt.exe" DolphinExplorer.exe

echo.
echo Build complete: %BUILD_DIR%\DolphinExplorer.exe
if not defined DOLPHIN_NONINTERACTIVE pause
