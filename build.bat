@echo off
setlocal
cmake -S "%~dp0." -B "%~dp0build" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build "%~dp0build" --config Release --parallel
exit /b %errorlevel%
