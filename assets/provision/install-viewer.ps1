# ============================================================
# 星集控 · 管理端（老师机）装机脚本（云端托管版 2026-10-05）
#
# 和教室机那支 install-agent.ps1 是一对：那边装被控端，这边装管理端（viewer）。
# 到老师机只需要双击管理台生成的 bat，本来不该碰命令行。
#
# 与教室机的两点不同（别照抄错了）：
#   ① 安装包是 MSI（绿色部署那套散文件太胖，车载进 U 盘发不动），
#      所以配置**不能**写"exe 同目录的 viewer.env"——MSI 不会写那个目录。
#      改用**用户级环境变量** STE_VIEWER_URL / STE_VIEWER_TOKEN：
#      viewer 的 loadEnvFile 里有一行 `qgetenv(key) 非空就跳过`，真环境变量优先级更高，
#      所以环境变量一设，双击就能连，跟装前装后目录长啥样无关。
#   ② 管理端不占设备 uid（它是看的人，不是被控的机器），所以这里没有 -Uid 参数。
#
# 需要管理员权限（MSI perMachine + 写用户环境变量 + 建计划任务）。
# ⚠️ 编码：UTF-8 with BOM，PowerShell 5.1 才会按 UTF-8 解码中文提示。
# ============================================================

param(
    [Parameter(Mandatory = $true)][string]$Cloud,        # wss://.../ws/viewer
    [Parameter(Mandatory = $true)][string]$Token,        # 与云端 .env 的 CLOUD_VIEWER_TOKEN 一致
    [Parameter(Mandatory = $true)][string]$InstallerUrl, # MSI 下载地址（云端 assets/pkg/）
    [string]$Installer,                                   # 本地已有 MSI 则优先用（离线装机用）
    [string]$InstallDir = "C:\Program Files\Stelarith Viewer",
    [string]$Sha256,                                     # 给了就校验，不一致拒绝安装
    [switch]$Uninstall,
    [switch]$WhatIf                                      # 只打印将要做什么，不真装
)

chcp 65001 | Out-Null
$ErrorActionPreference = "Stop"

function Info($m) { Write-Host "[*] $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[OK] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[!] $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[X] $m" -ForegroundColor Red; exit 1 }

$TaskName = 'StelarithViewerQt'
$Exe = Join-Path $InstallDir 'viewer-qt.exe'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Die "需要管理员权限：请右键 bat 选择“以管理员身份运行”后重试。"
}

# ---- 卸载（先走这条路，方便反复重装调试）----
if ($Uninstall) {
    Info "卸载管理端 ..."
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    [Environment]::SetEnvironmentVariable('STE_VIEWER_URL', $null, 'User')
    [Environment]::SetEnvironmentVariable('STE_VIEWER_TOKEN', $null, 'User')
    if (Test-Path $InstallDir) {
        Info "删除安装目录 $InstallDir ..."
        Remove-Item -Path $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Get-Process -Name 'viewer-qt' -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
    Ok "已卸载（MSI 本体没删，想彻底删就再跑一次安装器）"
    exit 0
}

Write-Host ""
Info "管理端地址 = $Cloud"
Info ("令牌        = <{0} 字符，不打印>" -f $Token.Length)
Info "安装包     = $InstallerUrl"
Info "安装目录   = $InstallDir"
Write-Host ""

if ($WhatIf) { Warn "-WhatIf 干跑：没做任何改动。"; exit 0 }

# ---- 1) 拿到 MSI：本地有就用本地（离线装机），否则从云端下 ----
function Ensure-Package([string]$Local, [string]$Url, [string]$ExpectSha) {
    if ($Local) {
        $c = Resolve-Path $Local -ErrorAction SilentlyContinue
        if ($c) { Ok "用本地安装包：$c"; return $c.Path }
    }
    $tmp = Join-Path $env:TEMP ("stelarith-viewer-msi-" + [guid]::NewGuid().ToString('n') + ".msi")
    Info "正在从云端下载管理端安装包（约 35 MB，请连着校园网）…"
    for ($i = 1; $i -le 3; $i++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing -TimeoutSec 600
            break
        }
        catch {
            Warn "第 $i/3 次下载失败：$($_.Exception.Message)"
            if ($i -eq 3) { Die "连续下载失败三次。请确认能打开 https://control.245959623.xyz，或把 MSI 放到 bat 同一目录后重跑。" }
            Start-Sleep -Seconds 3
        }
    }
    $f = Get-Item $tmp -ErrorAction SilentlyContinue
    if (-not $f -or $f.Length -lt 1024000) { Die "下载到的文件不到 1 MB，八成是下到了网页错误页。" }
    if ($ExpectSha) {
        $actual = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $ExpectSha.ToLower()) { Die "MSI 的 sha256 不一致（期望 $ExpectSha / 实际 $actual），拒绝安装。" }
        Ok "MSI sha256 校验通过"
    }
    return $tmp
}

$pkg = Ensure-Package -Local $Installer -Url $InstallerUrl -ExpectSha $Sha256

# ---- 2) 静默安装 MSI（perMachine）----
Info "静默安装 MSI（约 30-90 秒，这期间别动窗口）…"
$p = Start-Process -FilePath 'msiexec.exe' -ArgumentList "/i `"$pkg`" /qn /norestart" -Wait -PassThru
# 0=成功，3010/1641=需要重启但安装本身成了，1603=失败
if ($p.ExitCode -eq 0 -or $p.ExitCode -eq 3010 -or $p.ExitCode -eq 1641) { Ok "MSI 安装完成（返回码 $($p.ExitCode)）" }
elseif ($p.ExitCode -eq 1603) { Die "MSI 安装失败（1603，常见原因：已经装了更高版本 / 权限不足 / 之前的安装残留）" }
else { Warn "MSI 返回 $($p.ExitCode)（非 0 不一定是失败，下面会校验产物）" }

if (-not (Test-Path $Exe)) { Die "装完仍找不到 $Exe —— 安装没成功，请看上面的输出。" }
Ok "主程序就位：$Exe ($((Get-Item $Exe).Length) 字节)"

# ---- 3) 写用户级环境变量（浏览器侧 /ws/viewer 用，双击与自启都能继承）----
Info "写 STE_VIEWER_URL / STE_VIEWER_TOKEN（用户级）…"
[Environment]::SetEnvironmentVariable('STE_VIEWER_URL', $Cloud, 'User')
[Environment]::SetEnvironmentVariable('STE_VIEWER_TOKEN', $Token, 'User')
$chk = [Environment]::GetEnvironmentVariable('STE_VIEWER_URL', 'User')
if ($chk -ne $Cloud) { Warn "环境变量写了但读回来对不上（'$chk'），可能是组策略把用户环境变量锁了。" }
else { Ok "环境变量就位（当前登录用户）" }

# ---- 4) 建登录触发的计划任务 ----
Info "注册开机自启任务 $TaskName ..."
# 与教室机同一套主体写法（install-autostart.ps1 血泪史 v1/v2/v3）：
# 用 Users 组做主体 + RunLevel Highest，**不传 LogonType**（传了会 AmbiguousParameterSet），
# 任务对组内每个登录用户都在其交互会话里跑 —— 谁登录都行，且无人值守下注册也稳。
$action = New-ScheduledTaskAction -Execute $Exe -WorkingDirectory $InstallDir
$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -Hidden:$false -ExecutionTimeLimit ([TimeSpan]::Zero)
$principal = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest

Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal | Out-Null
Ok "自启任务已注册（登录时启动，交互会话）"

# ---- 5) 自检 ----
Info "自检 ..."
$t = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($t) {
    $state = (Get-ScheduledTask -TaskName $TaskName).State
    if ($state -eq 'Ready') { Ok "自启任务状态：Ready" } else { Warn "自启任务状态：$state（正常应该是 Ready）" }
} else { Warn "自启任务没找到 —— 上面注册那步大概率没成。" }

$proc = Get-Process -Name 'viewer-qt' -ErrorAction SilentlyContinue
if ($proc) { Ok "管理端已在跑：PID $($proc.Id -join ',')" }

# 双击验证要看不到画面才准，这里只确认进程/任务，真连没连上以管理台为准
Write-Host ""
Write-Host "桌面应出现「星集控管理端」图标；想立刻看，手动跑一次："
Write-Host "    `"$Exe`""
Write-Host ""
Write-Host "    看它连上没有：管理台 → 设备列表（管理端是 viewer 通道，不占教室机设备位）"
Write-Host ""
Ok "装机完成。"
