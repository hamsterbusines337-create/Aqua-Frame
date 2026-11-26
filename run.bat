@echo off
setlocal
if not exist "%~dp0build\Release\AquaFrame.exe" (
  echo Run setup.bat and build.bat first.
  exit /b 1
)
start "" "%~dp0build\Release\AquaFrame.exe"
