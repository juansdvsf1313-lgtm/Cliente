@echo off
set VCPKG_ROOT=D:\vcpkg
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d D:\Canary_OTClient_src
cmake --preset windows-release
if errorlevel 1 exit /b 1
cmake --build --preset windows-release
