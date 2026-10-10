@echo off
rem Build the Inno Setup installer for stelarith-agent-qt.
rem PURE ASCII only: Chinese chars in a .bat get GBK-decoded by cmd.exe and swallow
rem adjacent newlines, silently breaking lines.
rem
rem Usage: build-agent-installer.bat
rem        (run it after build-qt-agent.bat; it only re-packages, it does not compile)
rem
rem WHY INNO SETUP (2026-10-10): migrated from NSIS to Inno Setup (installer.iss).
rem   Inno gives per-user default dir ({localappdata}), modern Windows 11 wizard,
rem   and a cleaner [Code] section for stop-old-process / write-minimal-agent.env /
rem   uninstall-reverse steps. ISCC.exe is the CLI compiler - failures print and set
rem   an exit code (no modal dialog hiding the cause).
setlocal

set ISCC=C:\Users\Administrator\AppData\Local\Programs\Inno Setup 6\ISCC.exe

rem ---- 0) preconditions ----
if not exist "deploy\" (
    echo [FATAL] deploy\ is missing - run build-qt-agent.bat first
    exit /b 3
)
if not exist "dist\" (
    echo [FATAL] dist\ is missing - create it before packaging
    exit /b 3
)
if not exist "installer.iss" (
    echo [FATAL] installer.iss not found (run this script from the project root)
    exit /b 3
)
if not exist "%ISCC%" (
    echo [FATAL] Inno Setup compiler not found at %ISCC%
    exit /b 3
)

rem ---- 1) version from build\version.nsh (唯一真源＝CMakeLists AGENT_VERSION) ----
if not exist "build\version.nsh" (
    echo [FATAL] build\version.nsh missing - run cmake first to generate it
    exit /b 3
)
for /f "tokens=3 delims= " %%a in ('findstr /C:"!define VER" build\version.nsh') do set VER=%%~a
if "%VER%"=="" (
    echo [FATAL] cannot parse VER from build\version.nsh
    exit /b 3
)
echo [version] %VER%

rem ---- 2) temp folder known-good (ISCC writes stubs + output into %TEMP%) ----
set TMP=C:\Users\Administrator\AppData\Local\Temp
set TEMP=C:\Users\Administrator\AppData\Local\Temp
if not exist "%TMP%" mkdir "%TMP%" 2>nul
echo [tmp] TMP=%TMP%

rem ---- 3) compile (version passed via env; ISCC reads it through GetEnv) ----
set STE_INSTALLER_VERSION=%VER%
"%ISCC%" installer.iss
set RC=%errorlevel%

if "%RC%"=="0" (
    echo [ok] installer written: %CD%\dist\stelarith-agent-setup-%VER%.exe
) else (
    echo [FAIL] ISCC exited with %RC% - see the lines above
)
echo [exit] %RC%
exit /b %RC%
