@echo off
rem 编译灵动岛独立演示（2026-10-07，展示给用户看效果）
call "C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts\activate.bat" >nul 2>&1
set MSVC=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKVER=10.0.26100.0
set QT=D:\Qt\6.8.1\msvc2022_64
set MSVCCL=%MSVC%\bin\Hostx64\x64
set PATH=%QT%\bin;%MSVCCL%;%SDK%\bin\%SDKVER%\x64;%PATH%
set INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt
set LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64
mkdir "%TEMP%\island_demo_build" >nul 2>&1
cd /d "%TEMP%\island_demo_build"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT%" -DCMAKE_C_COMPILER="%MSVCCL%\cl.exe" -DCMAKE_CXX_COMPILER="%MSVCCL%\cl.exe" "D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt\tests\island-demo-cmake" || exit /b 1
cmake --build . --config Release || exit /b 1
echo [exit] %errorlevel%