' Stelarith agent guard - hidden launcher (2026-10-22).
'
' WHY THIS FILE EXISTS:
'   The scheduled task used to run stelarith-agent-guard.bat directly. Task
'   Scheduler allocates a CONSOLE window for any .bat/.cmd action, so a black
'   window popped up every 10 minutes and stayed until the shell exited
'   (task setting Hidden=true only hides it in the scheduler UI, not on screen).
'
' WHY IT WORKS:
'   wscript.exe is a GUI-subsystem host: it never allocates a console, so there
'   is no window to show - not even briefly. Window style 0 = hidden child.
'
' WHY bWaitOnReturn = True:
'   If wscript launches the child and exits immediately, Task Scheduler considers
'   the task finished and reaps the process tree, killing the guard mid-run
'   (the P2P signaling task hit exactly this). Waiting keeps the task alive for
'   the whole guard run; the guard itself is short (seconds), so this is cheap.
'
' ASCII ONLY on purpose: this file is read by WSH with the system ANSI codepage.

Option Explicit
Dim sh, fso, scriptDir, ps1, rc

Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")

' Same directory as this .vbs - the installer drops both files side by side.
scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
ps1 = fso.BuildPath(scriptDir, "stelarith-agent-guard.ps1")

If Not fso.FileExists(ps1) Then
    ' Nothing to guard with; fail loudly in the task's Last Run Result.
    WScript.Quit 2
End If

rc = sh.Run("powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File """ & ps1 & """", 0, True)
WScript.Quit rc
