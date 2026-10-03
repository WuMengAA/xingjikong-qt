# 星集控被控端 · 开机自启安装器（TEST1 装机用，本机不执行）
#
# 为什么是「登录触发器计划任务」而不是真正的 Windows Service：
#   真正的 Service 跑在 Session0，抓不到交互桌面，无法对当前用户会话做
#   LockWorkStation / SendInput / 截屏（会指向空白的 Session0 桌面）。
#   登录触发器任务以登录用户身份在其自身会话里启动，代理才能锁屏、注入输入、抓真帧。
#   这是有意为之，不是偷懒。
#
# 用法（在 TEST1 上，管理员 PowerShell）：
#   cd <deploy 目录>
#   .\install-autostart.ps1
# 卸载：
#   .\install-autostart.ps1 -Uninstall

param(
    [switch]$Uninstall,
    # 安装目录。NSIS 安装器会把它的 $INSTDIR 传进来；不传时用默认值（行为与以前一致）
    [string]$Dest = 'C:\Program Files\Stelarith',
    # 文件已由安装器放好时跳过复制（避免"复制自己到自己"）
    [switch]$SkipCopy,
    # 源目录，默认＝脚本所在目录
    [string]$Source = ''
)

$ErrorActionPreference = 'Stop'
$TaskName = 'StelarithAgentQt'
$Deploy = if ($Source) { $Source } else { $PSScriptRoot }

if ($Uninstall) {
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
    Write-Host "[ok] 已卸载计划任务 $TaskName"
    exit 0
}

# 1) 复制装机包到固定目录（含 Qt 运行时，无需在 TEST1 装 Qt）
if ($SkipCopy) {
    Write-Host "[skip] -SkipCopy：文件已由安装器放好，不再复制"
} else {
    if (-not (Test-Path $Dest)) { New-Item -ItemType Directory -Path $Dest | Out-Null }
    Copy-Item -Path "$Deploy\*" -Destination $Dest -Recurse -Force
    Write-Host "[ok] 已复制到 $Dest"
}

# 2) 建登录触发器计划任务：交互会话、最高权限（让 SeShutdownPrivilege 生效）
# ⚠️ 2026-10-03 修：这里原来写的是 -DontStopIfGoingToBattery（少 "On"、少复数），
#    PowerShell 没有这个参数名，脚本一跑就 NamedParameterNotFound 中止 ——
#    也就是说「自启」此前**从未真正装成功过**。正确名是 -DontStopIfGoingOnBatteries。
# ⚠️ 2026-10-03 实测修正：原来这里是 `cmd.exe /c start-agent.bat` ——
#    在计划任务上下文里那一层 cmd **会卡住不退出**（任务永远 Running，被控端根本没被拉起）。
#    现在**直接拉 exe**，配置由 exe 自己读同目录的 agent.env（见被控端 loadEnvFile）。
#    自启链路越短越可靠：任务 → exe，中间不留任何批处理。
$action = New-ScheduledTaskAction -Execute "$Dest\stelarith-agent-qt.exe" -WorkingDirectory $Dest
$trigger = New-ScheduledTaskTrigger -AtLogOn
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -Hidden:$false -ExecutionTimeLimit ([TimeSpan]::Zero)
# 以登录用户身份、交互会话运行（关键：不能是 Session0）。
# 主体验证历程（都踩过，别回退）：
#   v1: `GroupId 'S-1-5-32-545' + LogonType Interactive` → PowerShell 参数集冲突(AmbiguousParameterSet)
#   v2: `UserId $currentUser + LogonType Interactive` → 抓当前用户，但 MSI 以 SYSTEM 装时无交互用户可取，
#       于是 New-ScheduledTaskPrincipal 抛错 → 安装整体回滚 1603。
#   v3(现在): `GroupId 'S-1-5-32-545'(Users) + RunLevel Highest`，**不传 LogonType**——
#       用「用户组」做主体是「按登录用户跑交互任务」的经典做法：任务对组内每个登录用户都在其交互会话跑，
#       谁登录都行（教室机换账号也生效），且注册时不依赖具体用户名（无人值守/ SYSTEM 下装都稳）。
# 经测：GroupId 参数集里只得带 RunLevel，带 LogonType 又会回 AmbiguousParameterSet。
$principal = New-ScheduledTaskPrincipal -GroupId 'S-1-5-32-545' -RunLevel Highest

# 先清旧的，保证幂等
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal | Out-Null
Write-Host "[ok] 已创建计划任务 $TaskName（登录时启动，交互会话）"

Write-Host ''
Write-Host '下一步：编辑 C:\Program Files\Stelarith\agent.env，填 TEST1 的 STE_QT_WS_TOKEN / STE_QT_UID'
Write-Host '然后注销重登录（或手动跑一次 start-agent.bat）验证托盘出现、云端看到 TEST1 在线。'

