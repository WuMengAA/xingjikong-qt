@echo off
rem Stelarith Qt Agent build script.
rem NOTE: keep this file PURE ASCII. Chinese chars in a .bat get decoded as GBK
rem by cmd.exe and swallow adjacent newlines / chars, silently breaking lines.
rem Same manual MSVC/SDK env as before (vcvars64 is blocked by policy).
setlocal

set MSVC=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKVER=10.0.26100.0
set QT=D:\Qt\6.8.1\msvc2022_64
rem cl.exe lives at <MSVC>\bin\Hostx64\x64\cl.exe (the extra x64 level matters)
set MSVCCL=%MSVC%\bin\Hostx64\x64

call "C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts\activate.bat" 2>nul

set PATH=%QT%\bin;%MSVCCL%;%SDK%\bin\%SDKVER%\x64;%PATH%
rem SDK headers/libs are NOT in the SDK root: stddef.h lives under <SDKVER>\ucrt,
rem the Win32 API headers under <SDKVER>\um. Pointing INCLUDE at the SDK root only
rem compiles until the first #include <stddef.h> and then dies with
rem   fatal error C1083: cannot open include file: 'stddef.h'
rem (fix 2026-10-04 - this bit us on the first real recompile after the env parser fix)
set INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt
set LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64

cmake --version
ninja --version
rem Do NOT probe cl.exe by running "cl /?": observed to HANG in this sandbox
rem (process starts, 0s CPU, never returns) and it blocks the whole build.
rem Existence checks are enough and never block.
if exist "%MSVCCL%\cl.exe" echo [ok] cl.exe found
if exist "%SDK%\bin\%SDKVER%\x64\rc.exe" echo [ok] rc.exe found

cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER="%MSVCCL%\cl.exe" ^
  -DCMAKE_CXX_COMPILER="%MSVCCL%\cl.exe" || exit /b 1

cmake --build build --config Release
echo [exit] %errorlevel%
exit /b %errorlevel%
