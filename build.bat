@echo off
rem Builds everything (Release, Win32) into build\out and runs the self-tests.
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
set CMAKE="%VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
cd /d "%~dp0"
%CMAKE% -S . -B build -G "Visual Studio 16 2019" -A Win32 || exit /b 1
%CMAKE% --build build --config Release || exit /b 1
build\WormsLoader\Release\wl_selftest.exe || exit /b 1
python WormsLoader\server\test_relay.py || exit /b 1
