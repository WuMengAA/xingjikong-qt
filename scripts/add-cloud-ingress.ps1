param(
    # 你的 Cloudflare API Token（需权限：Account > Cloudflare Tunnel > Edit，Zone > DNS > Edit）。
    # 也可先设环境变量：  $env:CF_API_TOKEN = "cfat_xxx"
    [string]$ApiToken = $env:CF_API_TOKEN,

    [string]$AccountId = "ed7cc9ac1be3bea41051bb4e56ef1279",
    [string]$TunnelId  = "9d95ef14-54c4-4637-894b-0aa91ead6bf6",
    [string]$Zone      = "245959623.xyz",

    # 云端对外域名（教室机 / 管理端 / 手机都连它）
    [string]$CloudHost = "control",
    [int]   $CloudPort = 8788,

    [switch]$SkipDns,
    [switch]$WhatIfOnly
)

# ASCII only on purpose (cmd/console friendly).
# 作用：给现有 cloudflared 隧道**追加**一条 <CloudHost>.<Zone> -> http://localhost:8788 的路由，
#       并保留隧道里原有的其它规则（panel / 通配 等一概不动）。幂等：重复跑不会重复加。

$ErrorActionPreference = "Stop"
$API = "https://api.cloudflare.com/client/v4"
$HDR = @{ Authorization = "Bearer $ApiToken"; "Content-Type" = "application/json" }

function Info($m) { Write-Host "[*] $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "[OK] $m" -ForegroundColor Green }
function Warn($m) { Write-Host "[!] $m" -ForegroundColor Yellow }
function Die($m)  { Write-Host "[X] $m" -ForegroundColor Red; exit 1 }

function Invoke-CF {
    param([string]$Method, [string]$Path, $Body, [switch]$AllowFail)
    $uri = "$API$Path"
    try {
        if ($null -ne $Body) {
            $json = $Body | ConvertTo-Json -Depth 20 -Compress
            return Invoke-RestMethod -Method $Method -Uri $uri -Headers $HDR -Body $json
        }
        return Invoke-RestMethod -Method $Method -Uri $uri -Headers $HDR
    } catch {
        $detail = $null
        try {
            $resp = $_.Exception.Response
            if ($resp) { $reader = New-Object System.IO.StreamReader($resp.GetResponseStream()); $detail = $reader.ReadToEnd(); $reader.Close() }
        } catch { }
        if (-not $detail) { $detail = $_.ErrorDetails.Message }
        if (-not $detail) { $detail = $_.Exception.Message }
        $code = "?"
        try { $code = $_.Exception.Response.StatusCode.value__ } catch { }
        if ($AllowFail) { Warn "$Method $Path -> HTTP $code : $detail"; return $null }
        Die "$Method $Path -> HTTP $code : $detail"
    }
}

if (-not $ApiToken) { Die "没有 token。用 -ApiToken <TOKEN> 或先设 `$env:CF_API_TOKEN" }

Info "校验 token ..."
$v = Invoke-CF GET "/user/tokens/verify" $null -AllowFail
if ($v -and $v.result -and $v.result.status -eq "active") { Ok "token 有效（user-owned）" }
else {
    $v2 = Invoke-CF GET "/accounts/$AccountId/tokens/verify" $null -AllowFail
    if ($v2 -and $v2.result -and $v2.result.status -eq "active") { Ok "token 有效（account-owned）" }
    else { Warn "verify 未确认 token 有效；仍会尝试真实调用" }
}

Info "读取隧道 $TunnelId 的现有 ingress ..."
$cur = Invoke-CF GET "/accounts/$AccountId/cfd_tunnel/$TunnelId/configurations"
$existing = @()
if ($cur.result -and $cur.result.config -and $cur.result.config.ingress) { $existing = @($cur.result.config.ingress) }
Ok "现有规则 $($existing.Count) 条"

$cloudFqdn = "$CloudHost.$Zone"
$catchAll = $null
# cloudflared 的 ingress 是【从上往下、先命中先生效】；通配规则 *.zone 会吞掉所有子域，
# 所以云端规则必须插在**第一条通配规则之前**，否则永远匹配不到（干跑时抓到过这个 bug）。
$before = New-Object System.Collections.ArrayList
$after  = New-Object System.Collections.ArrayList
$seenWild = $false
foreach ($r in $existing) {
    $h = $null
    try { $h = $r.hostname } catch { }
    if (-not $h) {
        if ($null -eq $catchAll) { $catchAll = [ordered]@{ service = "http_status:404" } }
        continue
    }
    if ($h -eq $cloudFqdn) { Warn "丢弃旧的 $h 规则（下面按正确顺序重建）"; continue }
    if ($h -like '*`**') { $seenWild = $true }
    if ($seenWild) { [void]$after.Add($r) } else { [void]$before.Add($r) }
}
if (-not $catchAll) { $catchAll = [ordered]@{ service = "http_status:404" }; Warn "无 catch-all，补一条 http_status:404" }

$merged = New-Object System.Collections.ArrayList
foreach ($r in $before) { [void]$merged.Add($r) }
[void]$merged.Add([ordered]@{ hostname = $cloudFqdn; service = "http://localhost:$CloudPort" })
foreach ($r in $after) { [void]$merged.Add($r) }
[void]$merged.Add($catchAll)

Write-Host ""
Info "合并后的 ingress（顺序有意义）："
$i = 0
foreach ($r in $merged) {
    $line = "    {0,2}. " -f $i
    $h = $null; try { $h = $r.hostname } catch { }
    $p = $null; try { $p = $r.path } catch { }
    if ($h) { $line += "$h  " }
    if ($p) { $line += "path=$p  " }
    $line += "-> $($r.service)"
    Write-Host $line
    $i++
}
Write-Host ""

# 干跑时把真正要提交的 JSON 打出来 —— 眼见为实，避免"显示对、传的不对"。
if ($WhatIfOnly) {
    Info "将提交的 JSON（-WhatIfOnly 干跑，未推送）："
    Write-Host (@{ config = @{ ingress = @($merged); "warp-routing" = @{ enabled = $false } } } | ConvertTo-Json -Depth 20)
    Write-Host ""
}

if ($WhatIfOnly) { Warn "-WhatIfOnly：未推送（干跑）。"; exit 0 }

Info "推送合并后的 ingress ..."
$cfg = @{ config = @{ ingress = @($merged); "warp-routing" = @{ enabled = $false } } }
Invoke-CF PUT "/accounts/$AccountId/cfd_tunnel/$TunnelId/configurations" $cfg | Out-Null
Ok "ingress 已推送"

if (-not $SkipDns) {
    $zr = Invoke-CF GET "/zones?name=$Zone" $null -AllowFail
    if ($zr -and $zr.result -and $zr.result.Count -gt 0) {
        $zoneId = $zr.result[0].id
        $cnameTarget = "$TunnelId.cfargotunnel.com"
        $q = Invoke-CF GET "/zones/$zoneId/dns_records?name=$cloudFqdn" $null -AllowFail
        if ($q -and $q.result -and $q.result.Count -gt 0) {
            Ok "DNS 已存在：$cloudFqdn -> $($q.result[0].content) (proxied=$($q.result[0].proxied))"
        } else {
            Warn "DNS 缺失，创建 CNAME $cloudFqdn -> $cnameTarget ..."
            $c = Invoke-CF POST "/zones/$zoneId/dns_records" @{ type="CNAME"; name=$cloudFqdn; content=$cnameTarget; proxied=$true } -AllowFail
            if ($c) { Ok "已创建" } else { Warn "创建失败（token 缺 Zone:DNS:Edit）" }
        }
    } else {
        Warn "读不到 zone（token 缺 Zone:Read）—— 请手工在后台加：CNAME $cloudFqdn -> $cnameTarget (proxied)"
    }
}

Write-Host ""
Ok "完成。连接器约 30s 内生效（无需重启 cloudflared）。"
Write-Host ""
Write-Host "接着改这三处地址（其余照旧）："
Write-Host "    被控端 agent.env :  set STE_QT_WS_URL=wss://$cloudFqdn/ws/agent"
Write-Host "    管理端 viewer.env:  set `"STE_VIEWER_URL=wss://$cloudFqdn/ws/viewer`""
Write-Host "    OTA 清单 ota.json:  url = https://$cloudFqdn/ota/<安装包名>"
Write-Host ""
Write-Host "从外网验证："
Write-Host "    curl -H `"Authorization: Bearer <CLOUD_VIEWER_TOKEN>`" https://$cloudFqdn/api/devices"
