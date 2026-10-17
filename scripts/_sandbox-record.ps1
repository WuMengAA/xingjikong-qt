[CmdletBinding()]
param([string]$Port = '31600', [string]$CloudPort = '18799', [string]$OutFile, [string]$Label = 'baseline')
# 星集控 · T 档基线录制：把**旧调用路径**的一组代表性场景各跑一遍，每一步落一条岛状态串。
#   · 岛状态直出：插件 island_show 的回包带 islandSnapshot（这个进程这颗岛自己的状态）
#   · 面板类状态（文件传输/媒体/频谱）：沙箱连**假云端**注册，用 dev_act 指令触发
#     （dev_act 只在 STE_QT_UID=ste-sbx-* 的沙箱实例上可执行）
$ErrorActionPreference = 'Stop'
if (-not $OutFile) { $OutFile = Join-Path $env:TEMP "ste-task-$Label.json" }
Add-Type -Path 'D:\Stelarith\Stelarith-xingjikong-qt\scripts\_sandbox-client.cs'

$w = [Sb]::Connect($Port, "rec-$Label", '录制插件')
if (-not $w.Contains('"ok":true')) { throw "插件握手失败：$w" }

$fail = New-Object System.Collections.ArrayList
$steps = New-Object System.Collections.ArrayList
function Step($name, $reply) {
  $snap = [Sb]::Snap($reply)
  [void]$steps.Add([pscustomobject]@{ step = $name; snap = $snap })
  if (-not $snap) { [void]$fail.Add($name) }
  Start-Sleep -Milliseconds 300
}
function Jsn($o) { return ($o | ConvertTo-Json -Compress -Depth 5) }
function DevAct($kind, $extra) {
  $p = @{ kind = $kind }
  if ($extra) { foreach ($k in $extra.Keys) { $p[$k] = $extra[$k] } }
  return [Sb]::Instruct('ste-sbx-t', 'dev_act', (Jsn $p))
}
function Show($title, $desc, $icon, $form, $ms) {
  return [Sb]::Call('island_show', (Jsn @{ title = $title; desc = $desc; icon = $icon; form = $form; durationMs = $ms }))
}

# ── A. 通知类（形态 × 内容）─────────────────────────────
foreach ($f in 0, 1, 2, 3, 4) { Step "form$f.bell"   (Show "REC 形态$f" "描述$f" 'bell' $f 60000) }
foreach ($f in 0, 1, 2, 3, 4) { Step "form$f.status" (Show "REC 状态$f" '' 'check-circle' $f 60000) }
Step 'long.title' (Show ("REC 长标题" + ('长' * 300)) "描述" 'bell' 3 60000)
Step 'long.desc'  (Show 'REC 长正文' ('段' * 500) 'bell' 3 60000)

# ── B. notify 直通口 ──────────────────────────────────
Step 'notify.plain'    ([Sb]::Call('notify', (Jsn @{ kind='island'; title='REC notify 普通'; content='正文一段' })))
Step 'notify.confirm'  ([Sb]::Call('notify', (Jsn @{ kind='island'; title='REC notify 确认'; content='要确认'; flags=@{ confirm=$true } })))
Step 'notify.danmaku'  ([Sb]::Call('notify', (Jsn @{ kind='danmaku'; title='REC 弹幕'; content='弹幕正文' })))
Step 'notify.loop'     ([Sb]::Call('notify', (Jsn @{ kind='island'; title='REC 循环提醒'; content='每 30 秒'; flags=@{ loop=$true }; intervalSec=30 })))
Step 'notify.admin'    ([Sb]::Call('notify', (Jsn @{ kind='island'; title='REC 管理端'; content='来自管理端'; fromAdmin=$true })))

# ── C. 面板类状态：文件传输 / 媒体 / 频谱（经假云端 + dev_act）───────
# ⚠️ 拓扑别搞错（本轮踩的坑，改对之前一直"收不到回包"）：
#   管理端(ver) 连的是**云端**，插件 连的是**被控端本地插件口** —— 两条路不同端口。
#     · 插件 → ws://127.0.0.1:31600（被控端 PluginHost，只认 hello/register/api/result）
#     · 管理端 → ws://127.0.0.1:18799（假云端，认 auth/instruction；由它转发给被控端）
#   之前把 ConnectViewer 指到 31600 ⇒ 发 auth 给被 PluginHost 当未知类型静默忽略 ⇒ 永远没回包。
#   生产里同理：viewer 连 8788（云端），不是每台教室机的插件口。
$v = [Sb]::ConnectViewer($CloudPort, 'dev-cloud-token')
"viewer → 云端 $CloudPort : $v"
function DevAct($kind, $extra) {
  $p = @{ kind = $kind }
  if ($extra) { foreach ($k in $extra.Keys) { $p[$k] = $extra[$k] } }
  return [Sb]::Instruct('ste-sbx-t', 'dev_act', (Jsn $p))
}
$r = DevAct 'ft' @{ name='exam.zip'; bytes=1048576; total=4194304 }
"dev_act ft -> $r"
Start-Sleep -Milliseconds 300
Step 'panel.ft' (Show 'REC 传输中' '' 'download' 0 60000)
$r = DevAct 'media' @{ title='稻香'; totalMs=269000; next='晴天' }
"dev_act media -> $r"
Start-Sleep -Milliseconds 300
Step 'panel.ft+media' (Show 'REC 传输+播放' '' 'download' 0 60000)
$r = DevAct 'spectrum'
"dev_act spectrum -> $r"
Start-Sleep -Milliseconds 300
Step 'panels.all' (Show 'REC 面板全开' '' 'bell' 0 60000)

# ── D. 收起/空闲/历史 ─────────────────────────────────
$r = DevAct 'spectrum_off'
"dev_act spectrum_off -> $r"
$r = DevAct 'media_clear'
"dev_act media_clear -> $r"
$r = DevAct 'ft_clear'
"dev_act ft_clear -> $r"
Start-Sleep -Milliseconds 500
Step 'after.clear' (Show 'REC 清空面板' '' 'bell' 1 600)
Start-Sleep -Milliseconds 1500
Step 'idle.after-autoclose' (Show 'REC 此刻' '' 'bell' 0 60000)
Step 'replace.old' (Show 'REC 被顶掉的那条' '旧' 'bell' 0 60000)
Step 'replace.new' (Show 'REC 顶上来的一条' '新' 'bell' 0 60000)
[Sb]::Close()

$obj = [pscustomobject]@{ label = $Label; port = $Port; steps = $steps }
$obj | ConvertTo-Json -Depth 6 | Set-Content -Path $OutFile -Encoding utf8
"WROTE $OutFile steps=$($steps.Count) nosnap=$($fail.Count) $(if ($fail.Count) { '[' + ($fail -join ',') + ']' })"
