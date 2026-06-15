@echo off
setlocal

REM Dolphin Explorer — MSVC build using Ninja (Qt 6.7.2 msvc2019_64)
REM Requirements: Visual Studio 2022 Build Tools, Qt 6.7.2 msvc2019_64, CMake

set BUILD_DIR=build_mingw
set QT_DIR=C:/Qt/6.7.3/msvc2019_64
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set NINJA="C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
REM Do NOT name this CL — that is a reserved MSVC env var that gets prepended to every cl.exe invocation as extra args.
set VS_CL="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\cl.exe"

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

call %VCVARS%

cmake .. -G "Ninja" ^
    -DCMAKE_MAKE_PROGRAM=%NINJA% ^
    -DCMAKE_CXX_COMPILER=%VS_CL% ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
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
