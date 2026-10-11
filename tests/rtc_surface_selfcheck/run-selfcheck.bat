@echo off
set PATH=D:\QTqwq\6.12.0\msvc2022_64\bin;%PATH%
set QML2_IMPORT_PATH=D:\QTqwq\6.12.0\msvc2022_64\qml
D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-viewer-qt\tests\rtc_surface_selfcheck\build\rtc_surface_selfcheck.exe > D:\tmp\selfcheck.out 2>&1
echo RC=%errorlevel%
