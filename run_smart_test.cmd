@echo off
setlocal
set LEVEL=%1
set CAT=%2
set MAX=%3

if "%LEVEL%"=="" set LEVEL=1
if "%CAT%"=="" set CAT=All
if "%MAX%"=="" set MAX=3

echo Running Smart Test for Level %LEVEL%, Category %CAT%, MaxObjects %MAX%...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Run-SmartTest.ps1" -Level %LEVEL% -Category %CAT% -MaxObjects %MAX%
endlocal
