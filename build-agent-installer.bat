@echo off
rem Build the NSIS installer for stelarith-agent-qt.
rem PURE ASCII only: Chinese chars in a .bat get GBK-decoded by cmd.exe and swallow
rem adjacent newlines, silently breaking lines.
rem
rem WHY THIS EXISTS (2026-10-05, user hit "NSIS Error - Error writing temporary file"):
rem   The GUI compiler (makensisw.exe) writes its redirected-output temp file into
rem   %TEMP%. When that folder is not usable it shows a modal "确定" dialog that
rem   hides the real cause -- nothing is printed, the exit code is meaningless.
rem   Two fixes baked in here:
rem     1) force TMP/TEMP to a known-good folder (the sandbox / some shells hand the
rem        process a temp path that is cleaned, missing, or not writable for kids);
rem     2) use the CLI compiler (makensis.exe) instead of makensisw.exe -> failures
rem        print to the console AND set an exit code, instead of popping a dialog.
rem
rem Usage: build-agent-installer.bat
rem        (run it after build-qt-agent.bat; it only re-packages, it does not compile)

setlocal

set NSIS=C:\Program Files (x86)\NSIS
set SETUP=dist\stelarith-agent-setup.exe

rem ---- 0) precondition: the deploy tree and the output folder must exist ----
rem      installer.nsi does File /r deploy\* -- no deploy -> the compile "succeeds"
rem      but ships an empty setup, which is much harder to notice than a hard error.
if not exist "deploy\" (
    echo [FATAL] deploy\ is missing - run build-qt-agent.bat first
    exit /b 3
)
if not exist "dist\" (
    echo [FATAL] dist\ is missing - create it before packaging
    exit /b 3
)

rem ---- 1) temp folder that is known to exist and be writable ----
set TMP=C:\Users\Administrator\AppData\Local\Temp
set TEMP=C:\Users\Administrator\AppData\Local\Temp

if not exist "%TMP%" mkdir "%TMP%" 2>nul
echo [tmp] TMP=%TMP%

rem ---- 2) sanity check: can it actually write there? ----
echo . > "%TMP%\.nsis-tmpcheck" 2>nul
if errorlevel 1 (
    echo [FATAL] temp folder is not writable: %TMP%
    echo         Fix it first, e.g. in an administrator cmd:
    echo             mkdir "%TMP%"
    echo             icacls "%TMP%" /grant Users\(OI)(OI)F
    exit /b 2
)
del "%TMP%\.nsis-tmpcheck" >nul 2>&1

rem ---- 3) compile ----
"%NSIS%\makensis.exe" installer.nsi
set RC=%errorlevel%

if "%RC%"=="0" (
    echo [ok] installer written: %CD%\%SETUP%
) else (
    echo [FAIL] makensis exited with %RC% - see the lines above
)
echo [exit] %RC%
exit /b %RC%
