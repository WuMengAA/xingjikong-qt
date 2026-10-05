# Build script: Qt 6.8.1 + MSVC (VS 18 BuildTools) + Ninja + Windows SDK 10.0.26100.0
# Usage: pwsh -File build.ps1 [-Clean]

param([switch]$Clean)

$ErrorActionPreference = 'Stop'

$Proj   = $PSScriptRoot
$Build  = Join-Path $Proj 'build'
$Qt     = 'D:\Qt\6.8.1\msvc2022_64'
$Msvc   = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231'
$Sdk    = 'C:\Program Files (x86)\Windows Kits\10'
$SdkVer = '10.0.26100.0'
$Ninja  = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$CMake  = 'C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Lib\site-packages\cmake\data\bin\cmake.exe'

foreach ($p in @($Qt, $Msvc, $Sdk, $Ninja, $CMake)) {
    if (-not (Test-Path $p)) { throw "Missing dependency: $p" }
}

# MSVC toolchain env (CMake VS generator can't find VS 18 BuildTools -> use Ninja)
$env:QTDIR   = $Qt
$env:INCLUDE = "$Qt\include;$Qt\include\QtCore;$Qt\include\QtQml;$Qt\include\QtQuick;$Qt\include\QtGui;$Qt\include\QtNetwork;$Msvc\include;$Sdk\Include\$SdkVer\ucrt;$Sdk\Include\$SdkVer\um;$Sdk\Include\$SdkVer\shared"
$env:LIB     = "$Qt\lib;$Msvc\lib\x64;$Sdk\Lib\$SdkVer\ucrt\x64;$Sdk\Lib\$SdkVer\um\x64"
$env:PATH    = "$Qt\bin;$Msvc\bin\Hostx64\x64;$Sdk\bin\$SdkVer\x64;$env:PATH"

if ($Clean -and (Test-Path $Build)) { Remove-Item $Build -Recurse -Force }

if (-not (Test-Path (Join-Path $Build 'build.ninja'))) {
    Write-Host '=== CMake configure ===' -ForegroundColor Cyan
    & $CMake -S $Proj -B $Build -G Ninja `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_PREFIX_PATH=$Qt `
        -DCMAKE_MAKE_PROGRAM="$Ninja" `
        -DCMAKE_C_COMPILER=cl `
        -DCMAKE_CXX_COMPILER=cl
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
}

Write-Host '=== Compile ===' -ForegroundColor Cyan
$out = & $CMake --build $Build --config Release 2>&1
$code = $LASTEXITCODE
$out | Where-Object { $_ -match 'error|FAILED|Linking CXX' -and $_ -notmatch 'note|include' } | Select-Object -Last 25
if ($code -ne 0) { throw "Build failed (exit=$code)" }

$exe = Join-Path $Build 'stelarith-schedule-qt.exe'
Write-Host '=== Deploy Qt runtime ===' -ForegroundColor Cyan
& "$Qt\bin\windeployqt.exe" --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime $exe | Out-Null

Write-Host "OK: $exe" -ForegroundColor Green
