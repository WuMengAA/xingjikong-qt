' Stelarith cloud watchdog - hidden launcher (2026-10-22).
'
' WHY THIS FILE EXISTS:
'   The scheduled task "StelarithCloudWatchdog" used to run cloud-watchdog.cmd
'   directly. Task Scheduler allocates a CONSOLE window for any .bat/.cmd
'   action, so a black window flashed on screen every 5 minutes (task setting
'   Hidden only hides it in the scheduler UI, not on the desktop).
'
' WHY IT WORKS:
'   wscript.exe is a GUI-subsystem host: it never allocates a console, so there
'   is no window to show - not even briefly. Window style 0 = SW_HIDE keeps the
'   child cmd (netstat/findstr) invisible too.
'
' WHY bWaitOnReturn = True:
'   If wscript exits immediately, Task Scheduler considers the task finished
'   and reaps the process tree, killing the watchdog mid-run.
'
' The .cmd keeps all the logic (8788 probe, detached relaunch) - this file is
' only a console-free shell, exactly like stelarith-agent-guard-hidden.vbs.
'
' ASCII ONLY on purpose: this file is read by WSH with the system ANSI codepage.

Option Explicit
Dim sh, fso, scriptDir, cmdPath, rc

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

' Same directory as this .vbs - the task action points at both side by side.
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
cmdPath = fso.BuildPath(scriptDir, "cloud-watchdog.cmd")

If Not fso.FileExists(cmdPath) Then
    ' Nothing to guard with; fail loudly in the task's Last Run Result.
    WScript.Quit 2
End If

rc = sh.Run("cmd /c """ & cmdPath & """", 0, True)
WScript.Quit rc
