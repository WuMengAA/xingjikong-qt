# 星集控 · 编假云端（T 档验证设施）
# 为什么单独一个构建脚本：pwsh 的 Add-Type 不带 HttpListener/WebSockets 引用，
# PowerShell 反过来加载 BCL dll 也不可靠（实测 CS1069/CS0012）。用 Roslyn 显式引用
# 本机 dotnet 运行时目录下的全部 System*.dll，一次编成 exe，之后互相独立。
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$csc = 'C:\Program Files\dotnet\sdk'
$sdk = Get-ChildItem $csc -Directory | Sort-Object Name -Descending | Select-Object -First 1
$roslyn = Join-Path $sdk.FullName 'Roslyn\bincore\csc.dll'
$runtime = (Get-ChildItem 'C:\Program Files\dotnet\shared\Microsoft.NETCore.App' -Directory |
            Sort-Object Name -Descending | Select-Object -First 1).FullName
if (-not (Test-Path $roslyn)) { throw "找不到 Roslyn：$roslyn" }
# PE 头 → 数据目录第 15 项（CLI Header）RVA≠0 ⇒ 托管程序集
function Test-ManagedAssembly([string]$path) {
    try {
        $fs = [IO.File]::OpenRead($path)
        try {
            $br = New-Object IO.BinaryReader($fs)
            $fs.Position = 0x3C; $peOff = $br.ReadInt32()
            $fs.Position = $peOff + 4 + 2        # 跳过 PE\0\0 + Machine
            $numSections = $br.ReadUInt16(); $null = $br.ReadUInt16(); $null = $br.ReadUInt32()
            $null = $br.ReadUInt16(); $optSize = $br.ReadUInt16()
            $optStart = $peOff + 24
            $fs.Position = $optStart; $magic = $br.ReadUInt16()
            $ddStart = if ($magic -eq 0x20b) { $optStart + 112 } else { $optStart + 96 }   # PE32+ / PE32
            $fs.Position = $ddStart + 14 * 8
            return ($br.ReadUInt32() -ne 0)
        } finally { $fs.Close() }
    } catch { return $false }
}
# ⚠️ 运行时目录里混着原生 dll（如 System.IO.Compression.Native.dll），不能一起当引用喂给 csc
#    —— 实测 CS0009「PE 映像不包含任何托管元数据」。先读 PE 头的 CLI 目录项，只留托管程序集。
$refs = Get-ChildItem $runtime -Filter 'System*.dll' | Where-Object { Test-ManagedAssembly $_.FullName } |
        ForEach-Object { "-r:`"$($_.FullName)`"" }
$refs += "-r:`"$(Join-Path $runtime 'netstandard.dll')`""
$out = Join-Path $root '_probe\fake-cloud\fake-cloud.dll'
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
& dotnet $roslyn /nologo /target:exe /out:"$out" @refs (Join-Path $PSScriptRoot '_sandbox-fakecloud.cs')
if ($LASTEXITCODE -ne 0) { throw "编译失败 rc=$LASTEXITCODE" }
Set-Content -Path (Join-Path (Split-Path $out) 'fake-cloud.runtimeconfig.json') -Encoding utf8 -Value @'
{ "runtimeOptions": { "tfm": "net10.0", "framework": { "name": "Microsoft.NETCore.App", "version": "10.0.0" } } }
'@
"OK $out"
