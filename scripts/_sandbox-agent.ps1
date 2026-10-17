[CmdletBinding()]
param([string]$Port = '31600', [string]$DataDir)
# 星集控 · 沙箱被控端启动器（2026-10-17 整理版，替代上一轮散落的临时脚本）
# 隔离条件（逐条都有过坑，见 docs/P0-0-第三方推岛治理-加减归档-2026-10-17.md §六）：
#   · LOCALAPPDATA 指到沙箱目录，**必须先建好 xingjikong/ 子目录** ——
#     否则 agentEnvPath() 回退到 exe 同目录（build/agent.env）并往里写 STE_QT_*；
#   · STE_PLUGIN_PORT 独立端口；STE_PLUGIN_DIR 独立插件目录；
#   · STE_QT_WS_URL 指本地假云端（端口无人监听 = 启动即断线重试，最省事）；
#   · STE_QT_OTA_DISABLE=1 —— 否则一旦注册成功，OTA 可能把**生产安装目录**的包换掉；
#   · STE_QT_UID 以 ste-sbx- 开头 —— 这是 dev_act 指令（T 档验证设施）的执行前提。
$ErrorActionPreference = 'Stop'
if (-not $DataDir) { $DataDir = Join-Path $env:TEMP 'ste-t-sandbox' }
$root = 'D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt\build'

New-Item -ItemType Directory -Force -Path (Join-Path $DataDir 'xingjikong'), (Join-Path $DataDir 'appdata' 'Stelarith') | Out-Null

$env:LOCALAPPDATA = $DataDir
$env:APPDATA = Join-Path $DataDir 'appdata'
$env:STE_PLUGIN_PORT = $Port
$env:STE_PLUGIN_DIR = Join-Path $DataDir 'plugins'
$env:STE_QT_WS_URL = 'ws://127.0.0.1:18799'
$env:STE_QT_WS_TOKEN = 'dev-cloud-token'
$env:STE_QT_UID = 'ste-sbx-t'
$env:STE_QT_NAME = 'T 沙箱'
$env:STE_QT_OTA_DISABLE = '1'
$env:STE_QT_CHANNEL = 'sandbox'
$env:STE_QT_LOG = Join-Path $DataDir 'agent-qt.log'
# 沙箱级 agent.env（loadEnvFile 只设"尚未存在"的变量，真环境变量优先）
@("set STE_QT_OTA_DISABLE=1", "set STE_PLUGIN_PORT=$Port", "set STE_QT_UID=ste-sbx-t") |
  Set-Content -Path (Join-Path $DataDir 'agent.env') -Encoding utf8

$stdout = Join-Path $DataDir 'stdout.txt'
$stderr = Join-Path $DataDir 'stderr.txt'
# ⚠️ 起之前先确认端口没被占：本轮实测出现过"上一轮沙箱没死透"⇒ 新起的实例拿单例锁失败直接退出，
#    而测试连的其实是那个老进程（配置/版本都是旧的，结论全错）。
$busy = Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue
if ($busy) {
    $owner = Get-Process -Id $busy[0].OwningProcess -ErrorAction SilentlyContinue
    throw "端口 $Port 已被 pid=$($owner.Id)（$($owner.Path)）占用 —— 先确认是不是自己的沙箱实例再处理"
}
$p = Start-Process -FilePath (Join-Path $root 'stelarith-agent-qt.exe') -WorkingDirectory $root `
      -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
$p.Id | Set-Content (Join-Path $DataDir 'pid.txt')
# 起完再核一次：进程活着 + 端口确实归它
Start-Sleep -Seconds 2
$p.Refresh()
if ($p.HasExited) { throw "沙箱进程启动即退出（多半单例锁被占/端口冲突），看 $stderr" }
$own = (Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | Select-Object -First 1)
if (-not $own -or $own.OwningProcess -ne $p.Id) {
    throw "端口 $Port 不归 pid=$($p.Id)（实际 $(if ($own) { $own.OwningProcess } else { '无人监听' })）"
}
"START pid=$($p.Id) data=$DataDir port=$Port (verified)"
