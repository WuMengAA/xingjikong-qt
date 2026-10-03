@echo off
rem Stelarith controlled client launcher (TEST1 install)
rem Sets env vars then starts the agent in the current user session.
setlocal
set AGENT_DIR=%~dp0

rem Defaults; override by copying agent.env.example to agent.env and editing it.
set STE_QT_WS_URL=ws://127.0.0.1:8788/ws/agent
rem Token is intentionally left EMPTY. DO NOT put a placeholder here. (fix 2026-10-04)
rem Reason: the exe reads agent.env itself, and gives priority to env vars that already
rem exist - it will NOT overwrite them, see loadEnvFile in src/main.cpp. If a placeholder
rem is set here, the real token in agent.env gets skipped silently: the process starts and
rem screenshots fine but never connects, and the log misleadingly says cloud is down.
rem Empty here means agent.env wins. If agent.env has no token either, the exe fails
rem loudly with "FAIL: STE_QT_WS_TOKEN not configured" instead of pretending.
rem NOTE: this file must stay pure ASCII - cmd.exe parses .bat as GBK, so any
rem non-ASCII byte corrupts the script.
set STE_QT_WS_TOKEN=
set STE_QT_UID=TEST1
set STE_QT_SHOT_DIR=%AGENT_DIR%shots

if exist "%AGENT_DIR%agent.env" call "%AGENT_DIR%agent.env"
if not exist "%AGENT_DIR%shots" mkdir "%AGENT_DIR%shots"

start "" "%AGENT_DIR%stelarith-agent-qt.exe"
exit /b 0
