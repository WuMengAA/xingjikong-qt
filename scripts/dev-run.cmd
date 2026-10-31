@echo off
rem ============================================================================
rem  Agent DEV launcher -- double-click this file.
rem
rem  It will:
rem    1. Incremental build of the agent
rem    2. Sync qml/ + shared components into build/qml/
rem    3. Stop any running instance (its global mutex blocks a second one)
rem    4. Launch the dev build with live UTF-8 logs in this window
rem
rem  Close the popup window = end this debug session.
rem  (Kept pure ASCII on purpose: .cmd with CJK breaks under some codepages.)
rem ============================================================================
cd /d "%~dp0"
chcp 65001 >nul 2>&1
setlocal

set "PY=C:\Users\Administrator\.workbuddy\binaries\python\envs\default\python.exe"
if not exist "%PY%" set "PY=C:\Users\Administrator\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe"

echo Building and starting the DEV agent, please wait...
echo (first run compiles, may take a while)
echo.
"%PY%" "%~dp0_dev_run.py" --yes
echo.
echo ============================================================
echo  Dev instance exited. Press any key to close.
echo  Production build: C:\Program Files\Stelarith\stelarith-agent-qt.exe
echo ============================================================
pause >nul
