# ============================================================
# 星集控 · 教室机被控端装机脚本（云端托管版 2026-10-05）
#
# 怎么用（到教室机双击面板生成的 bat 就行，本来不需要碰命令行）：
#   bat 会先下载本脚本 → 装 → 配好 uid/云端地址/令牌 → 自启起来。
# 本脚本只被 bat 调用，用户侧不需要手敲任何命令。
#
# 幂等：重复跑会先卸旧实例、覆写配置，不会装出两份。
# 需要管理员权限（写 Program Files + 建登录触发的计划任务）。
#
# ⚠️ 编码：本文件存为 UTF-8 with BOM，PowerShell 5.1 才会按 UTF-8 解码中文提示。
# ============================================================

param(
    [Parameter(Mandatory = $true)][string]$Uid,          # 这台教室机的唯一代号
    [Parameter(Mandatory = $true)][string]$Cloud,        # wss://.../ws/agent
    [Parameter(Mandatory = $true)][string]$Token,        # 与云端 .env 的 CLOUD_WS_TOKEN 一致
    [Parameter(Mandatory = $true)][string]$InstallerUrl, # 安装包下载地址（云端 assets/pkg/）
    [string]$Installer,                                   # 本地已有安装包则优先用（离线装机用）
    [string]$InstallDir = "C:\Program Files\Stelarith",
    [string]$Sha256,                                     # 给了就校验，不一致拒绝安装
    [switch]$WhatIf                                      # 只打印将要做什么，不真装
)

chcp 65001 | Out-Null
$ErrorActionPreference = "Stop"

function Info($m) { Write-Host "[*] $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[OK] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[!] $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[X] $m" -ForegroundColor Red; exit 1 }

# ---- 0) 管理员权限（写 Program Files / 建计划任务都需要）----
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Die "需要管理员权限：请右键 bat 选择“以管理员身份运行”后重试。"
}

# ---- 1) 参数自检 ----
if ($Uid -notmatch '^(class|admin)_[a-z0-9]+_\d{4}_\d{1,3}$') {
    Warn "uid '$Uid' 不符合命名规范 class_<校简称>_<届>_<班号>（例 class_xlzx_2028_08）。"
}
if ($Uid -cne $Uid.ToLower()) { Warn "uid 含大写字母，云端按原样存，建议全小写。" }

Write-Host ""
Info "目标 uid   = $Uid"
Info "云端地址   = $Cloud"
Info ("令牌        = <{0} 字符，不打印>" -f $Token.Length)
Info "安装包     = $InstallerUrl"
Info "安装目录   = $InstallDir"
Write-Host ""

if ($WhatIf) { Warn "-WhatIf 干跑：没做任何改动。"; exit 0 }

# ---- 2) 拿到安装包：本地有就用本地（离线装机），否则从云端下 ----
function Ensure-Installer([string]$Local, [string]$Url, [string]$ExpectSha) {
    if ($Local) {
        $c = Resolve-Path $Local -ErrorAction SilentlyContinue
        if ($c) { Ok "用本地安装包：$c"; return $c.Path }
    }
    $tmp = Join-Path $env:TEMP ("stelarith-agent-setup-" + [guid]::NewGuid().ToString('n') + ".exe")
    Info "正在从云端下载安装包（约 17 MB，请连着校园网）…"
    for ($i = 1; $i -le 3; $i++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing -TimeoutSec 300
            break
        }
        catch {
            Warn "第 $i/3 次下载失败：$($_.Exception.Message)"
            if ($i -eq 3) { Die "下载安装包连续失败三次。请确认这台机器能打开 https://control.245959623.xyz，或改用 U 盘离线装机（把 exe 放到 bat 同一目录后重跑）。" }
            Start-Sleep -Seconds 3
        }
    }
    $f = Get-Item $tmp -ErrorAction SilentlyContinue
    if (-not $f -or $f.Length -lt 1024000) {
        Die "下载到的文件不到 1 MB，八成是下到了网页错误页 —— 请让管理员检查云端 assets\pkg 里有没有这个安装包。"
    }
    if ($ExpectSha) {
        $actual = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $ExpectSha.ToLower()) { Die "安装包 sha256 不一致（期望 $ExpectSha / 实际 $actual），拒绝安装。" }
        Ok "安装包 sha256 校验通过"
    }
    return $tmp
}

$pkg = Ensure-Installer -Local $Installer -Url $InstallerUrl -ExpectSha $Sha256

# ---- 3) 停掉正在跑的旧实例 ----
Info "停止旧的被控端进程（若有）…"
Get-Process -Name 'stelarith-agent-qt','stelarith-guard','stelarith-agent' -ErrorAction SilentlyContinue |
    ForEach-Object { try { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue; Ok "已停 PID $($_.Id) ($($_.ProcessName))" } catch {} }
Start-Sleep -Milliseconds 800

# ---- 4) 静默安装（NSIS /S）----
Info "静默安装（约 10-40 秒，这期间别动窗口）…"
$p = Start-Process -FilePath $pkg -ArgumentList "/S" -Wait -PassThru
if ($p.ExitCode -ne 0) { Warn "安装器返回 $($p.ExitCode)（非 0 不一定是失败，下面会校验产物）" } else { Ok "安装器完成" }

$exe = Join-Path $InstallDir "stelarith-agent-qt.exe"
if (-not (Test-Path $exe)) { Die "装完仍找不到 $exe —— 安装没成功，请看上面的输出。" }
Ok "主程序就位：$exe ($((Get-Item $exe).Length) 字节)"

# ---- 5) 写 agent.env（把这台机器的真配置落盘）----
Info "写 agent.env …"
$envFile = Join-Path $InstallDir "agent.env"
$shotDir = Join-Path $InstallDir "shots"
if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Path $shotDir -Force | Out-Null }
@(
    "rem 由云端装机面板生成（$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')）—— 改这里之后重启自启任务才生效。",
    "set STE_QT_WS_URL=$Cloud",
    "set STE_QT_WS_TOKEN=$Token",
    "set STE_QT_UID=$Uid",
    "set STE_QT_SHOT_DIR=$shotDir"
) | Set-Content -Path $envFile -Encoding ASCII
Ok "已写 $envFile"

# ---- 6) 重启登录触发的任务，让新配置生效 ----
$task = 'StelarithAgentQt'
if (Get-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue) {
    Info "重启计划任务 $task …"
    Stop-ScheduledTask -TaskName $task -ErrorAction SilentlyContinue
    Start-ScheduledTask  -TaskName $task -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 3
} else {
    Warn "计划任务 $task 不存在 —— 安装器本应创建；若没有，手工跑 `"$InstallDir\install-autostart.ps1 -SkipCopy`"。"
}

# ---- 7) 自检 ----
Info "自检 …"
$proc = Get-Process -Name 'stelarith-agent-qt' -ErrorAction SilentlyContinue
if ($proc) { Ok "被控端在跑：PID $($proc.Id -join ','), 内存 $([math]::Round(($proc | Select-Object -First 1).WorkingSet64/1MB,1)) MB" }
else { Warn "没看到被控端进程 —— 可能被杀软拦了，或被单例锁挡住（先确认没有其他实例在跑）。" }

$logFile = Join-Path $env:LOCALAPPDATA "xingjikong\agent-qt.log"
if (Test-Path $logFile) { Info "日志尾部（$logFile）："; Get-Content $logFile -Tail 6 | ForEach-Object { "      $_" } }

Write-Host ""
Ok "装机完成。"
Write-Host "    台账请记一行： uid=$Uid  班级=<班>  位置=<教室>  装机日期=$(Get-Date -Format 'yyyy-MM-dd')"
Write-Host ""
Write-Host "    看这台上来没有：管理面板 → 设备列表（等 1-2 分钟）"
Write-Host ""
