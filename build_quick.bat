@echo off
cd /d "%~dp0"

REM Ninja remembers cl.exe but not the MSVC INCLUDE/LIB environment. A quick
REM build launched from Explorer or VS Code must initialise that environment
REM just like the configure script does.
set "VCVARS="
for %%V in (
    "C:\Program Files\Microsoft Visual Studio\18\Community"
    "C:\Program Files\Microsoft Visual Studio\18\Professional"
    "C:\Program Files\Microsoft Visual Studio\2022\Community"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional"
    "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
) do if not defined VCVARS if exist "%%~V\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~V\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS (
    echo Visual Studio C++ build tools were not found.
    exit /b 1
)
call "%VCVARS%" >nul

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
