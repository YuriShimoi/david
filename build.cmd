@echo off
rem Builds David on Windows from a VS 2022 dev environment. Usage: build.cmd [win-debug|win-release]
setlocal
set PRESET=%1
if "%PRESET%"=="" set PRESET=win-debug
rem vcpkg: DAVID_VCPKG, else your own VCPKG_ROOT, else a vcpkg clone next to the repo, else the copy bundled with VS
if "%DAVID_VCPKG%"=="" set DAVID_VCPKG=%VCPKG_ROOT%
if "%DAVID_VCPKG%"=="" if exist "%~dp0..\vcpkg\scripts\buildsystems\vcpkg.cmake" set DAVID_VCPKG=%~dp0..\vcpkg
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0^,18.0^) -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
rem vcvars points VCPKG_ROOT at the copy bundled with VS; keep the chosen one when there is one
if not "%DAVID_VCPKG%"=="" set VCPKG_ROOT=%DAVID_VCPKG%
cmake --preset %PRESET% || exit /b 1
cmake --build --preset %PRESET%
