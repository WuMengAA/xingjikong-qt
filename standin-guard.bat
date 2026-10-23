@echo off
setlocal
cd /d "%~dp0"
netstat -ano | findstr /r /c:":8799 .*LISTENING" >nul 2>&1
if %errorlevel%==0 exit /b 0
set STE_VOICEHUB_WRITE_KEY=dev-vh-key
set STE_VH_STANDIN_PORT=8799
start "" /b "C:\Program Files\nodejs\node.exe" "%~dp0scripts\_local_voicehub_standin.js" >> "%~dp0standin.log" 2>&1
exit /b 0
