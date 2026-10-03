@echo off
setlocal
cd /d "%~dp0"

rem Stelarith cloud WebSocket server launcher (port 8788).
rem Called by scheduled task "StelarithCloud", trigger At startup, run as SYSTEM.
rem Keep this file strictly ASCII + CRLF (a non-ASCII byte is decoded as GBK and a
rem trailing half-character swallows the next line, which then runs as a bogus command).
rem The cloud reads its tokens from .env in this folder via node --env-file.

"C:\Program Files\nodejs\node.exe" --env-file=.env src\index.js >> "%~dp0cloud-service.log" 2>&1
