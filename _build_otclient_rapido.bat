@echo off
rem Compilacion rapida: las dependencias de vcpkg ya estan instaladas en
rem build\windows-release\vcpkg_installed, asi que se salta "vcpkg install".
set VCPKG_ROOT=D:\vcpkg
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
cd /d D:\Canary_OTClient_src
cmake --preset windows-release -DVCPKG_MANIFEST_INSTALL=OFF ^
  -DCMAKE_C_FLAGS="/arch:AVX2 /Gy /Gw" ^
  -DCMAKE_CXX_FLAGS="/arch:AVX2 /Gy /Gw" ^
  -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="/O2 /Ob3 /DNDEBUG /Z7"
if errorlevel 1 exit /b 1
cmake --build --preset windows-release
