@echo off
cd /d "%~dp0"

if not exist build_mingw (
    echo build_mingw was not found.
    echo Run build_mingw.bat once to configure the Ninja build directory.
    pause
    exit /b 1
)

cd build_mingw

echo Building...
cmake --build . --parallel
if errorlevel 1 goto :error

echo.
echo Build complete!
pause
exit /b 0

:error
echo Build failed!
pause
exit /b 1
