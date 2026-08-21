@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM Repeats tests until one fails or the repeat count is exhausted, to catch
REM intermittent/flaky failures that a single ctest run won't reliably show.
REM
REM Usage:
REM   stress_test.bat                          -- whole suite, 50 repeats
REM   stress_test.bat 200                      -- whole suite, 200 repeats
REM   stress_test.bat 200 "NodeGraph,Waterfall" -- only matching tests, 200 repeats
cd /d "%~dp0"

if not exist build_mingw (
    echo build_mingw was not found.
    echo Run build_mingw.bat once to configure the Ninja build directory.
    exit /b 1
)

set "REPEATS=%~1"
if "%REPEATS%"=="" set "REPEATS=50"
for /f "delims=0123456789" %%A in ("%REPEATS%") do (
    echo Repeat count must be a positive integer.
    exit /b 2
)
if %REPEATS% LSS 1 (
    echo Repeat count must be a positive integer.
    exit /b 2
)

set "FILTER="
shift
:collect_filters
if "%~1"=="" goto filters_collected
set "PART=%~1"
set "PART=!PART:,=|!"
if defined FILTER (set "FILTER=!FILTER!|!PART!") else set "FILTER=!PART!"
shift
goto collect_filters
:filters_collected

cd build_mingw

if "!FILTER!"=="" (
    echo Stress-testing the full suite: up to %REPEATS% repeats per test, stopping at the first failure...
    ctest --parallel 1 --repeat until-fail:%REPEATS% --output-on-failure
) else (
    echo Stress-testing tests matching "!FILTER!": up to %REPEATS% repeats, stopping at the first failure...
    ctest --parallel 1 -R "!FILTER!" --repeat until-fail:%REPEATS% --output-on-failure
)

if errorlevel 1 (
    echo.
    echo STRESS TEST FAILED - a test broke during a repeat run. See output above
    echo for the exact iteration and failure. Do not dismiss this as "just flaky"
    echo without repeating it enough times to be confident either way.
    exit /b 1
)

echo.
echo All selected tests passed %REPEATS% times in a row.
exit /b 0
