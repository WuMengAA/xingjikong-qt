# 星集控手机 App · APK 打包脚本（2026-10-11）
# 保证步骤：Android 构建 → 拷 .so（viewer+OpenSSL）→ 强制 native 压缩 →
# gradle assembleRelease → apksigner 签名 → 发布件 dist/星集控-手机端-manager-arm64-release.apk
#
# ⚠️ 压缩配置为什么在脚本里：Qt androiddeployqt 每次都会把
# android-build/gradle.properties 的 legacyPackaging 重置回 false（Qt 模板不采纳
# QT_ANDROID_PACKAGE_SOURCE_DIR 下的 gradle.properties，实测 6.12 版本不走该机制）。
# 唯一可靠 = 打包时在 gradle 前把它刷成 true（native libs deflate → 77.9MB 变 31.4MB）。

param(
    [switch]$SkipConfigure   # 已有 build-android 且 qml 没变时可跳过 configure
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$bdir = Join-Path $root 'build-android'

# ── 环境 ──
$env:ANDROID_SDK_ROOT = 'D:\Android\Sdk'
$env:ANDROID_NDK_ROOT = 'D:\Android\Sdk\ndk\27.0.12077973'
$env:ANDROID_NDK_HOME = 'D:\Android\Sdk\ndk\27.0.12077973'
$env:ANDROID_JAVA_HOME = 'C:\Program Files\Java\jdk-17'
$env:JAVA_HOME = 'C:\Program Files\Java\jdk-17'
$env:PATH = 'D:\QTqwq\6.12.0\android_arm64_v8a\bin;D:\QTqwq\Tools\Ninja;' + $env:PATH
if (-not $env:STE_VIEWER_TOKEN_BUILD) {
    $envFile = 'D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud-ws\.env'
    $envLine = (Get-Content $envFile | Select-String '^CLOUD_VIEWER_TOKEN=' | Select-Object -First 1).Line
    $env:STE_VIEWER_TOKEN_BUILD = $envLine -replace '^CLOUD_VIEWER_TOKEN=',''
}
$cmake = 'C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Lib\site-packages\cmake\data\bin\cmake.exe'
$qtcmake = 'D:\QTqwq\6.12.0\android_arm64_v8a\bin\qt-cmake.bat'

# ── 1. configure（-SkipConfigure 时跳过；改过 qml/CMake/版本时必须 configure）──
if (-not $SkipConfigure) {
    Write-Host "== configure（qt-cmake）=="
    & $qtcmake -S $root -B $bdir -G Ninja -DCMAKE_BUILD_TYPE=Release -DANDROID_ABI=arm64-v8a *> "$env:TEMP\qtc-make.txt"
    if ($LASTEXITCODE -ne 0) { Get-Content "$env:TEMP\qtc-make.txt" | Select-Object -Last 5; exit 1 }
}

# ── 2. build（qml 改动会触发 qrc 重建；若上次 manual 改过要删 qrc 缓存则先删）──
Write-Host "== build =="
$b = & $cmake --build $bdir 2>&1
if ($LASTEXITCODE -ne 0) { $b | Select-String 'error:' | Select-Object -First 8; exit 1 }
Write-Host "  build OK"

# ── 3. 拷 .so（viewer + OpenSSL）──
$dstLib = Join-Path $bdir 'android-build\libs\arm64-v8a'
Copy-Item (Join-Path $bdir 'libviewer-qt_arm64-v8a.so') (Join-Path $dstLib 'libviewer-qt_arm64-v8a.so') -Force
$dir = 'D:\Stelarith\_probe\android_openssl-master\ssl_3\arm64-v8a'
Copy-Item (Join-Path $dir 'libcrypto_3.so') (Join-Path $dstLib 'libcrypto_3.so') -Force
Copy-Item (Join-Path $dir 'libssl_3.so') (Join-Path $dstLib 'libssl_3.so') -Force
Write-Host "  .so copied"

# ── 4. 强制 native 压缩（Qt 每轮重置 legacyPackaging=false）──
$gp = Join-Path $bdir 'android-build\gradle.properties'
$content = Get-Content $gp -Raw
$content = $content -replace '(?m)^legacyPackaging=.+$', 'legacyPackaging=true'
Set-Content $gp $content -Encoding UTF8
Write-Host "  gradle.properties → legacyPackaging=true"

# ── 5. gradle assembleRelease ──
Write-Host "== gradle assembleRelease =="
Remove-Item (Join-Path $bdir 'android-build\build\outputs\apk\release\*.apk') -Force -ErrorAction SilentlyContinue
Push-Location (Join-Path $bdir 'android-build')
$out = & .\gradlew.bat assembleRelease --rerun-tasks 2>&1
$stop = $LASTEXITCODE
Pop-Location
if ($stop -ne 0) { $out | Select-String 'BUILD FAILED|error' | Select-Object -First 6; exit 1 }
Write-Host "  gradle OK"

# ── 6. 签名 ──
$apkU = Join-Path $bdir 'android-build\build\outputs\apk\release\android-build-release-unsigned.apk'
$ks = Join-Path $root 'android\stelarith-release.keystore'
$final = Join-Path $root 'dist\星集控-手机端-manager-arm64-release.apk'
& 'D:\Android\Sdk\build-tools\34.0.0\apksigner.bat' sign --ks $ks --ks-pass pass:stelarith2026 --key-pass pass:stelarith2026 --ks-key-alias stelarith --out $final $apkU 2>&1 | Out-Null
Write-Host ("== 完成：APK " + [math]::Round((Get-Item $final).Length/1MB,1) + " MB @ " + (Get-Item $final).LastWriteTime.ToString('HH:mm:ss'))