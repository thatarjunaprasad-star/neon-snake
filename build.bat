@echo off
rem Build script for NEON SNAKE.
rem
rem The WinLibs toolchain lives under a path containing spaces, which breaks
rem the GCC linker when it injects default-manifest.o (a UAC manifest object
rem that console apps don't need). specs.txt is a copy of the built-in specs
rem with that one clause removed; passing it via -specs sidesteps the bug.
setlocal
set "GXX=C:\mingw64\bin\g++.exe"
if not exist "%GXX%" set "GXX=C:\Users\Programmer Arjuna\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe"
"%GXX%" -specs="%~dp0specs.txt" -std=c++17 -O2 -Wall -Wextra "%~dp0snake.cpp" -o "%~dp0snake.exe"
if errorlevel 1 (
    echo BUILD FAILED
    exit /b 1
)
echo BUILD OK: %~dp0snake.exe
