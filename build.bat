@echo off
setlocal

REM Dolphin Explorer — Windows build script
REM Requirements: CMake 3.20+, Qt6, MSVC or MinGW

set BUILD_DIR=build

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

REM Adjust -DCMAKE_PREFIX_PATH to your Qt6 installation
cmake .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="C:/Qt/6.7.0/msvc2019_64" ^
    -DCMAKE_BUILD_TYPE=Debug

if errorlevel 1 (
    echo.
    echo CMake configure failed.
    echo Check that Qt6 is installed and CMAKE_PREFIX_PATH is correct.
    pause
    exit /b 1
)

cmake --build . --config Debug --parallel

if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

echo.
echo Build complete: %BUILD_DIR%\Debug\DolphinExplorer.exe
pause
