@echo off
setlocal

REM Dolphin Explorer — MinGW build (no Visual Studio required)
REM Requirements: Qt6 with MinGW, CMake, Ninja

set BUILD_DIR=build_mingw

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

cmake .. -G "Ninja" ^
    -DCMAKE_PREFIX_PATH="C:/Qt/6.7.3/mingw_64" ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_MAKE_PROGRAM="C:/Qt/Tools/Ninja/ninja.exe" ^
    -DCMAKE_CXX_COMPILER="C:/Qt/Tools/mingw1310_64/bin/g++.exe"

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

"C:/Qt/6.7.3/mingw_64/bin/windeployqt.exe" DolphinExplorer.exe

echo.
echo Build complete: %BUILD_DIR%\DolphinExplorer.exe
pause
