@echo off
rem #171 管理器侧 sink 自检构建+运行脚本（纯 ASCII）
rem 复用 build-qt-viewer.bat 的同款 MSVC/Qt 工具链；产物为 headless 小测试，不碰被控端/桌面。
setlocal

set MSVC=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231
set SDK=C:\Program Files (x86)\Windows Kits\10
set SDKVER=10.0.26100.0
set QT=D:\QTqwq\6.12.0\msvc2022_64
set MSVCCL=%MSVC%\bin\Hostx64\x64

set PATH=%QT%\bin;%MSVCCL%;%SDK%\bin\%SDKVER%\x64;%PATH%
set INCLUDE=%MSVC%\include;%SDK%\Include\%SDKVER%\ucrt;%SDK%\Include\%SDKVER%\um;%SDK%\Include\%SDKVER%\shared;%SDK%\Include\%SDKVER%\winrt
set LIB=%MSVC%\lib\x64;%SDK%\Lib\%SDKVER%\ucrt\x64;%SDK%\Lib\%SDKVER%\um\x64
set QML2_IMPORT_PATH=%QT%\qml

set SRC=D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-viewer-qt\tests\rtc_surface_selfcheck
set OUT=%SRC%\build

if exist "%OUT%" rd /s /q "%OUT%"

cmake -S "%SRC%" -B "%OUT%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="%QT%" ^
  -DCMAKE_C_COMPILER="%MSVCCL%\cl.exe" ^
  -DCMAKE_CXX_COMPILER="%MSVCCL%\cl.exe" || exit /b 1

cmake --build "%OUT%" --config Release -j 2 || exit /b 1

echo [ok] build done, running selfcheck...
"%OUT%\rtc_surface_selfcheck.exe"
echo [selfcheck exit] %errorlevel%
