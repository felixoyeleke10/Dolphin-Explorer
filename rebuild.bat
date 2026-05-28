@echo off
cd /d C:\Users\Felix\Documents\Dolphin-Explorer\build_mingw
cmake --build . --parallel
echo EXIT_CODE=%ERRORLEVEL%
