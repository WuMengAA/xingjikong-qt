@echo off
chcp 65001 >nul
set PYTHONUTF8=1
title Local VoiceHub Stand-in

REM ===== Local VoiceHub Stand-in launcher (pure ASCII, no mojibake in cmd) =====
REM Starts scripts/_local_voicehub_standin.js and keeps it running.
REM Point any cloud instance at it via:
REM   STE_VOICEHUB_WRITE_BASE=http://127.0.0.1:8799
REM   STE_VOICEHUB_WRITE_KEY=dev-vh-key
REM (same key as below) -- this is the local substitute while real VoiceHub is down.

set STE_VOICEHUB_WRITE_KEY=dev-vh-key
set STE_VH_STANDIN_PORT=8799

cd /d D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud
if not exist scripts\_local_voicehub_standin.js (
  echo [ERROR] scripts/_local_voicehub_standin.js not found in this directory.
  pause
  exit /b 1
)

echo Local VoiceHub stand-in starting on http://127.0.0.1:8799
echo   GET  /api/songs/public
echo   POST /api/open/schedules/reorder   (x-api-key: dev-vh-key)
echo Keep this window open. Close it to stop the stand-in.
echo.

node scripts/_local_voicehub_standin.js
