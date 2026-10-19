# Stelarith agent guard (scheduled every 10 minutes).
#
# WHY THIS FILE EXISTS (2026-10-18 incident):
#   The old guard judged "is the agent running" by PROCESS NAME (tasklist|findstr),
#   so a same-named instance from a build directory (or a hung duplicate) kept the
#   guard quiet while the PRODUCTION agent was dead for 35 minutes.
#   It also could not tell "process alive but frozen" from "healthy".
#
# RULES
#   1. Production is identified by FULL PATH, never by name.
#   2. The single-instance mutex (Global\StelarithAgentQt_Singleton) is machine-wide,
#      so ANY same-named process blocks production from starting -> foreign
#      instances must be cleared before we launch.
#   3. A production process that owns no plugin port and burned zero CPU for 5s
#      (after the 5 min boot grace) is considered frozen -> restart it.
#   4. Never roll anything back by ourselves; never touch files.
#
# ASCII ONLY on purpose: Windows PowerShell 5.1 reads a BOM-less file as ANSI/GBK,
# so any non-ASCII byte corrupts the script (same lesson as the .bat files).

$ErrorActionPreference = 'Continue'

$Prod   = 'C:\Program Files\Stelarith\stelarith-agent-qt.exe'
$Port   = 26789
$LogDir = 'C:\ProgramData\Stelarith'
$Log    = Join-Path $LogDir 'guard.log'

function Write-GuardLog([string]$msg) {
    try {
        if (-not (Test-Path $LogDir)) { New-Item -ItemType Directory -Path $LogDir -Force | Out-Null }
        if (Test-Path $Log) {
            if ((Get-Item $Log).Length -gt 512KB) { Move-Item $Log ($Log + '.1') -Force }
        }
        $line = '{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
        Add-Content -Path $Log -Value $line -Encoding ASCII
    } catch { }
}

function Get-AgentProcs {
    @(Get-CimInstance Win32_Process -Filter "Name='stelarith-agent-qt.exe'" -ErrorAction SilentlyContinue)
}

function Get-PortOwner {
    try {
        $c = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($c) { return [int]$c.OwningProcess }
    } catch { }
    return 0
}

function Get-CpuTicks($procId) {
    $p = Get-CimInstance Win32_Process -Filter "ProcessId=$procId" -ErrorAction SilentlyContinue
    if (-not $p) { return -1 }
    return [double]$p.UserModeTime + [double]$p.KernelModeTime
}

function Stop-Foreign($proc) {
    Write-GuardLog ("FOREIGN pid={0} path={1} -> kill (holds the machine-wide singleton mutex)" -f $proc.ProcessId, $proc.ExecutablePath)
    try { Stop-Process -Id $proc.ProcessId -Force -ErrorAction Stop } catch {
        Write-GuardLog ("kill failed pid={0}: {1}" -f $proc.ProcessId, $_.Exception.Message)
    }
}

$procs     = Get-AgentProcs
$prodProcs = @($procs | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $Prod) })
$foreign   = @($procs | Where-Object { -not $_.ExecutablePath -or ($_.ExecutablePath -ine $Prod) })
$owner     = Get-PortOwner

# ---------- case A: production is running ----------
if ($prodProcs.Count -gt 0) {
    $pp = $prodProcs[0]

    foreach ($f in $foreign) {
        if ($owner -eq [int]$f.ProcessId) { Stop-Foreign $f }          # it stole the plugin port
        else { Write-GuardLog ("FOREIGN pid={0} path={1} running alongside (left alone)" -f $f.ProcessId, $f.ExecutablePath) }
    }

    $ownerOk = ($owner -eq [int]$pp.ProcessId)
    if ($ownerOk) {
        Write-GuardLog ("OK prod pid={0} owns {1}" -f $pp.ProcessId, $Port)
        exit 0
    }

    $ageSec = ((Get-Date) - [datetime]$pp.CreationDate).TotalSeconds
    if ($ageSec -lt 300) {
        Write-GuardLog ("OK prod pid={0} booting ({1:N0}s, port not up yet)" -f $pp.ProcessId, $ageSec)
        exit 0
    }
    if ($owner -ne 0) {
        # someone else owns the port; production is still alive -> do not kill it
        Write-GuardLog ("WAIT prod pid={0} alive, port owned by pid={1}" -f $pp.ProcessId, $owner)
        exit 0
    }

    $c1 = Get-CpuTicks $pp.ProcessId
    Start-Sleep -Seconds 5
    $c2 = Get-CpuTicks $pp.ProcessId
    if ($c1 -lt 0 -or $c2 -lt 0) { exit 0 }
    if (($c2 - $c1) -gt 0) {
        Write-GuardLog ("OK prod pid={0} alive, no port yet but burning cpu" -f $pp.ProcessId)
        exit 0
    }

    Write-GuardLog ("FROZEN prod pid={0} age={1:N0}s no-port zero-cpu -> restart" -f $pp.ProcessId, $ageSec)
    try { Stop-Process -Id $pp.ProcessId -Force -ErrorAction Stop } catch { }
    Start-Sleep -Seconds 3
    # fall through to launch
}

# ---------- case B: production is not running ----------
$procs     = Get-AgentProcs
$prodProcs = @($procs | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $Prod) })
if ($prodProcs.Count -gt 0) { exit 0 }                 # raced with a manual start

foreach ($f in $procs) { Stop-Foreign $f }

# give the OTA handover / a letting-go instance 5s before deciding again
Start-Sleep -Seconds 5
$procs = Get-AgentProcs
if ($procs.Count -gt 0) {
    $prodProcs = @($procs | Where-Object { $_.ExecutablePath -and ($_.ExecutablePath -ieq $Prod) })
    if ($prodProcs.Count -gt 0) { exit 0 }
    foreach ($f in $procs) { Stop-Foreign $f }
    Start-Sleep -Seconds 2
}

# NOTE (2026-10-20): do NOT set STE_QT_CHANNEL here. The agent resolves its own
#   channel at startup (env -> channel.txt -> stable; see resolvedChannel /
#   persistChannel in control-qt src/main.cpp), so a guard-launched process keeps
#   whatever track the machine is already on. The previous line hardcoded 'gray'
#   here, which silently moved every guard-relaunched machine onto the gray
#   track (the logon trigger task does not set the variable, and the env value
#   takes priority over the persisted channel.txt).
Write-GuardLog 'LAUNCH production agent (channel left to the agent: env -> channel.txt -> stable)'
try {
    Start-Process -FilePath $Prod -ErrorAction Stop | Out-Null
} catch {
    Write-GuardLog ('launch failed: ' + $_.Exception.Message)
    exit 1
}
exit 0
