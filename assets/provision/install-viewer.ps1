# ============================================================
# 星集控 · 管理端（老师机）装机脚本（云端托管版 2026-10-05 · 绿色版 zip）
#
# 和教室机那支 install-agent.ps1 是一对：那边装被控端，这边装管理端（viewer）。
# 到老师机只需要双击管理台生成的 bat，本来不该碰命令行。
#
# 打包形态：**绿色版 zip**（不是 MSI）。
#   MSI 方案有个死结：viewer 是读环境变量/同目录配置的 Qt 程序，MSI 不写那个目录，
#   装完还要补一步写 viewer.env，绕；而且 MSI 打包 Qt WebEngine（+170MB）还要改 wxs。
#   绿色版 zip 就是"整个部署目录解压即用"，无注册表、无卸载项、U 盘能直接跑，
#   换机器拷走即用 —— 对管理端（老师机就 1 台）是更贴的形态。
#   配置走**用户级环境变量** STE_VIEWER_URL / STE_VIEWER_TOKEN：
#   viewer 的 loadEnvFile 里 qgetenv(key) 非空就跳过，真环境变量优先级更高，双击就能连。
#
# 需要管理员权限（建登录自启任务）。绿色版本身不需要管理员，
# 但如果跳过自启只想先跑起来，把下面 -WithAutostart 换成 $false 重跑即可。
# ⚠️ 编码：UTF-8 with BOM，PowerShell 5.1 才会按 UTF-8 解码中文提示。
# ============================================================

param(
    [Parameter(Mandatory = $true)][string]$Cloud,        # wss://.../ws/viewer
    [Parameter(Mandatory = $true)][string]$Token,        # 与云端 .env 的 CLOUD_VIEWER_TOKEN 一致
    [Parameter(Mandatory = $true)][string]$InstallerUrl, # zip 下载地址（云端 assets/pkg/）
    [string]$Installer,                                   # 本地已有 zip 则优先用（离线装机用）
    [string]$InstallDir = "C:\Program Files\Stelarith Viewer",
    [string]$Sha256,                                     # 给了就校验，不一致拒绝安装
    [switch]$Uninstall,
    [switch]$WithAutostart,                              # 默认 off；bat 会显式传 on，管理员环境才建任务
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
$Shortcut = Join-Path ([Environment]::GetFolderPath('Desktop')) '星集控管理端.lnk'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Die "需要管理员权限：请右键 bat 选择“以管理员身份运行”后重试。"
}

# ---- 卸载（绿色版没有 MSI 本体问题，直接删干净）----
if ($Uninstall) {
    Info "卸载管理端 ..."
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    [Environment]::SetEnvironmentVariable('STE_VIEWER_URL', $null, 'User')
    [Environment]::SetEnvironmentVariable('STE_VIEWER_TOKEN', $null, 'User')
    if (Test-Path $Shortcut) { Remove-Item $Shortcut -Force }
    Get-Process -Name 'viewer-qt' -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
    if (Test-Path $InstallDir) {
        Info "删除安装目录 $InstallDir ..."
        Remove-Item -Path $InstallDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    Ok "已卸载干净（目录/环境变量/自启/快捷方式都清了）"
    exit 0
}

Write-Host ""
Info "管理端地址 = $Cloud"
Info ("令牌        = <{0} 字符，不打印>" -f $Token.Length)
Info "安装包     = $InstallerUrl"
Info "安装目录   = $InstallDir"
Write-Host ""

if ($WhatIf) { Warn "-WhatIf 干跑：没做任何改动。"; exit 0 }

# ---- 1) 拿到 zip：本地有就用本地（离线装机），否则从云端下 ----
function Ensure-Package([string]$Local, [string]$Url, [string]$ExpectSha) {
    if ($Local) {
        $c = Resolve-Path $Local -ErrorAction SilentlyContinue
        if ($c) { Ok "用本地安装包：$c"; return $c.Path }
    }
    $tmp = Join-Path $env:TEMP ("stelarith-viewer-" + [guid]::NewGuid().ToString('n') + ".zip")
    Info "正在从云端下载管理端（约 140 MB，请连着校园网）…"
    for ($i = 1; $i -le 3; $i++) {
        try {
            Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing -TimeoutSec 900
            break
        }
        catch {
            Warn "第 $i/3 次下载失败：$($_.Exception.Message)"
            if ($i -eq 3) { Die "连续下载失败三次。请确认能打开 https://control.245959623.xyz，或把 zip 放到 bat 同一目录后重跑。" }
            Start-Sleep -Seconds 3
        }
    }
    $f = Get-Item $tmp -ErrorAction SilentlyContinue
    if (-not $f -or $f.Length -lt 1024000) { Die "下载到的文件不到 1 MB，八成是下到了网页错误页。" }
    if ($ExpectSha) {
        $actual = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
        if ($actual -ne $ExpectSha.ToLower()) { Die "zip 的 sha256 不一致（期望 $ExpectSha / 实际 $actual），拒绝安装。" }
        Ok "zip sha256 校验通过"
    }
    return $tmp
}

$pkg = Ensure-Package -Local $Installer -Url $InstallerUrl -ExpectSha $Sha256

# ---- 2) 解压到安装目录（绿色版，解压即用）----
Info "解压到 $InstallDir ..."
if (-not (Test-Path $InstallDir)) { New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null }
$tmpExtract = Join-Path $env:TEMP ("stelarith-viewer-x-" + [guid]::NewGuid().ToString('n'))
Expand-Archive -Path $pkg -DestinationPath $tmpExtract -Force
if (-not (Test-Path (Join-Path $tmpExtract 'viewer-qt.exe'))) {
    # zip 可能是"套了一层目录"打的（deploy/ 整个目录打进去）。找一找。
    $found = Get-ChildItem -Path $tmpExtract -Recurse -Filter 'viewer-qt.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { $tmpExtract = $found.DirectoryName } else { Die "zip 里找不到 viewer-qt.exe —— 安装包结构不对。" }
}
# 覆盖式拷贝（旧的先不删，避免中途失败把老安装弄坏；最后再清一次）
Copy-Item -Path (Join-Path $tmpExtract '*') -Destination $InstallDir -Recurse -Force
if (-not (Test-Path $Exe)) { Die "解压后仍找不到 $Exe —— 安装没成功，请看上面的输出。" }
Ok "主程序就位：$Exe ($((Get-Item $Exe).Length) 字节)"
Remove-Item -Path $tmpExtract -Recurse -Force -ErrorAction SilentlyContinue

# ---- 3) 写用户级环境变量（双击与自启都能继承）----
Info "写 STE_VIEWER_URL / STE_VIEWER_TOKEN（用户级）…"
[Environment]::SetEnvironmentVariable('STE_VIEWER_URL', $Cloud, 'User')
[Environment]::SetEnvironmentVariable('STE_VIEWER_TOKEN', $Token, 'User')
$chk = [Environment]::GetEnvironmentVariable('STE_VIEWER_URL', 'User')
if ($chk -ne $Cloud) { Warn "环境变量写了但读回来对不上（'$chk'），可能是组策略把用户环境变量锁了。" }
else { Ok "环境变量就位（当前登录用户）" }

# ---- 4) 桌面快捷方式（绿色版没有开始菜单，快捷方式是老师唯一入口）----
if (Test-Path $Shortcut) { Remove-Item $Shortcut -Force }
$ws = New-Object -ComObject WScript.Shell
$lnk = $ws.CreateShortcut($Shortcut)
$lnk.TargetPath = $Exe
$lnk.WorkingDirectory = $InstallDir
$lnk.Save()
Ok "桌面快捷方式已建：星集控管理端.lnk"

# ---- 5) 开机自启（bat 显式要求时才建；纯跑一次不需要）----
if ($WithAutostart) {
    Info "注册开机自启任务 $TaskName ..."
    # 与教室机同一套主体写法（install-autostart.ps1 血泪史 v1/v2/v3）：
    # Users 组主体 + RunLevel Highest，**不传 LogonType**（传了 AmbiguousParameterSet）。
    $action = New-ScheduledTaskAction -Execute $Exe -WorkingDirectory $InstallDir
    $trigger = New-ScheduledTaskTrigger -AtLogOn
    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -Hidden:$false -ExecutionTimeLimit ([TimeSpan]::Zero)
    $principal = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
        -Settings $settings -Principal $principal | Out-Null
    Ok "自启任务已注册（登录时启动）"
} else {
    Warn "没建自启任务（-WithAutostart 未给）—— 老师机建议重跑带自启，或手动加开机启动。"
}

# ---- 6) 自检 ----
Info "自检 ..."
if ($WithAutostart) {
    $t = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    if ($t) {
        $state = $t.State
        if ($state -eq 'Ready') { Ok "自启任务状态：Ready" } else { Warn "自启任务状态：$state（正常应该是 Ready）" }
    } else { Warn "自启任务没找到 —— 上面注册那步大概率没成。" }
}
$proc = Get-Process -Name 'viewer-qt' -ErrorAction SilentlyContinue
if ($proc) { Ok "管理端已在跑：PID $($proc.Id -join ',')" }

Write-Host ""
Write-Host "桌面双击「星集控管理端」即可打开。想立刻验证："
Write-Host "    `"$Exe`""
Write-Host ""
Write-Host "    看它连上没有：管理台 → 设备列表（管理端是 viewer 通道，不占教室机设备位）"
Write-Host ""
Ok "装机完成。"
