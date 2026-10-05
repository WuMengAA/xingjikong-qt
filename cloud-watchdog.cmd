@echo off
rem Stelarith cloud watchdog (port 8788). Runs every 5 min via scheduled task.
rem If nothing listens on 8788, relaunch the cloud service (detached).
rem Keep this file strictly ASCII + CRLF (same rule as run-cloud.cmd).
rem NOTE: `start /b "path.cmd"` mis-parses the quoted path as a window title,
rem so we launch node directly (same invocation run-cloud.cmd uses).

setlocal
cd /d "%~dp0"

rem 1) is something already listening on 8788?
netstat -ano | findstr /r /c:":8788 .*LISTENING" >nul 2>&1
if %errorlevel%==0 (
    rem cloud is up - nothing to do
    exit /b 0
)

rem 2) not listening: log it and start the cloud detached (node directly).
echo [%date% %time%] watchdog: 8788 not listening, restarting cloud >> "%~dp0\cloud-watchdog.log"
start "" /b "C:\Program Files\nodejs\node.exe" --env-file=.env src\index.js >> "%~dp0\cloud-service.log" 2>&1
exit /b 0
