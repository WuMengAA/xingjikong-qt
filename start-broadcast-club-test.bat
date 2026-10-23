@echo off
chcp 65001 >nul
set PYTHONUTF8=1
title BroadcastClub Local Test
cd /d D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud

set PY=C:\Users\Administrator\.workbuddy\binaries\python\versions\3.14.3\python.exe
if not exist "%PY%" set PY=python

echo ============================================================
echo   Broadcast Club local test  (one-click)
echo   Auto-starts: local cloud + mock VoiceHub + control panel
echo   Login account is printed below; panel pops up by itself
echo   When done, press Ctrl-C to stop and clean up
echo ============================================================
echo.

"%PY%" scripts/_dev_broadcast_club_harness.py

echo.
echo ============================================================
echo   Stopped and cleaned. Press any key to close this window.
echo ============================================================
pause >nul
