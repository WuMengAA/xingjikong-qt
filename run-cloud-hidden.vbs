' Stelarith cloud - hidden launcher (2026-10-22).
'
' WHY THIS FILE EXISTS:
'   The scheduled task "StelarithCloud" used to run run-cloud.cmd directly.
'   Task Scheduler allocates a CONSOLE window for any .bat/.cmd action, so the
'   cloud's console window sat on the teacher's screen for as long as the
'   service ran (task setting Hidden only hides it in the scheduler UI).
'
' WHY IT WORKS:
'   wscript.exe is a GUI-subsystem host: it never allocates a console, so there
'   is no window to show - not even briefly. Window style 0 = SW_HIDE keeps the
'   child cmd invisible too.
'
' WHY bWaitOnReturn = True:
'   The task must stay "Running" while the cloud lives; if wscript exits at
'   once, Task Scheduler reaps the whole tree and kills the cloud mid-start.
'
' The .cmd keeps all the logic (cd, --env-file, log redirection) - this file is
' only a console-free shell, exactly like stelarith-agent-guard-hidden.vbs.
'
' ASCII ONLY on purpose: this file is read by WSH with the system ANSI codepage.

Option Explicit
Dim sh, fso, scriptDir, cmdPath, rc

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

' Same directory as this .vbs - the task action points at both side by side.
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
cmdPath = fso.BuildPath(scriptDir, "run-cloud.cmd")

If Not fso.FileExists(cmdPath) Then
    ' Nothing to launch; fail loudly in the task's Last Run Result.
    WScript.Quit 2
End If

rc = sh.Run("cmd /c """ & cmdPath & """", 0, True)
WScript.Quit rc
