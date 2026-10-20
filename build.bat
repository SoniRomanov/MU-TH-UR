@echo off
rem Copyright (C) 2026 Soni Romanov
rem SPDX-License-Identifier: MIT
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if not errorlevel 1 goto compile
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto missing
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT goto missing
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b 1
:compile
if not exist build mkdir build
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\write-version.ps1 -OutputDirectory build\generated
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Ibuild\generated src\main.cpp /Fobuild\main.obj /Febuild\MU-TH-UR.exe /link /SUBSYSTEM:WINDOWS gdiplus.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /O2 /MT tests\console_tests.cpp /Fobuild\tests.obj /Febuild\console_tests.exe
if errorlevel 1 exit /b 1
build\console_tests.exe
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /O2 /MT /DUNICODE /D_UNICODE /DNOMINMAX /Ibuild\generated tests\render_tests.cpp /Fobuild\render_tests.obj /Febuild\render_tests.exe /link /SUBSYSTEM:CONSOLE gdiplus.lib user32.lib gdi32.lib
if errorlevel 1 exit /b 1
build\render_tests.exe
if errorlevel 1 exit /b 1
echo Build complete: build\MU-TH-UR.exe
copy /y LICENSE build\LICENSE >nul
if errorlevel 1 exit /b 1
copy /y README.md build\README.md >nul
if errorlevel 1 exit /b 1
exit /b 0
:missing
echo Install Visual Studio with Desktop development with C++, then run build.bat again.
exit /b 1
