@echo off
setlocal

REM Dolphin Explorer — MSVC build using Ninja (Qt 6.7.2 msvc2019_64)
REM Requirements: Visual Studio 2022 Build Tools, Qt 6.7.2 msvc2019_64, CMake
REM Raster support needs GDAL via vcpkg (one-time, into %USERPROFILE%\vcpkg):
REM   git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
REM   %USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
REM   %USERPROFILE%\vcpkg\vcpkg install "gdal[core,png,jpeg]:x64-windows"
REM launch.bat deploys the GDAL DLLs + PROJ/GDAL data next to the exe.

set BUILD_DIR=build_mingw
set QT_DIR=C:/Qt/6.7.2/msvc2019_64
REM GDAL (+ PROJ, libtiff, …) provided by vcpkg. Forward slashes for CMAKE_PREFIX_PATH.
set "VCPKG_INSTALLED=%USERPROFILE:\=/%/vcpkg/installed/x64-windows"
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set NINJA="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
REM Do NOT name this CL — that is a reserved MSVC env var that gets prepended to every cl.exe invocation as extra args.
set VS_CL="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe"

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

call %VCVARS%

cmake .. -G "Ninja" ^
    -DCMAKE_MAKE_PROGRAM=%NINJA% ^
    -DCMAKE_CXX_COMPILER=%VS_CL% ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%;%VCPKG_INSTALLED%" ^
    -DCMAKE_BUILD_TYPE=Debug

if errorlevel 1 (
    echo CMake configure failed.
    pause
    exit /b 1
)

cmake --build . --parallel

if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

"%QT_DIR%/bin/windeployqt.exe" DolphinExplorer.exe

echo.
echo Build complete: %BUILD_DIR%\DolphinExplorer.exe
pause
