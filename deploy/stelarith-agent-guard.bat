@echo off
rem Stelarith agent guard - scheduled every 10 minutes.
rem ASCII only: cmd.exe decodes .bat as GBK, a UTF-8 char would be mangled.
rem
rem 2026-10-18: the logic moved to stelarith-agent-guard.ps1.
rem The old version matched the agent by PROCESS NAME, so a same-named instance
rem from a build directory (or a hung duplicate) silenced the guard while the
rem production agent stayed dead. See the .ps1 header for the rules now used.
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0stelarith-agent-guard.ps1"
exit /b %errorlevel%
