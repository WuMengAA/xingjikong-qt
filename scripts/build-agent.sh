#!/bin/bash
# 星集控被控端 · 编译（Git Bash 直连 cmake/ninja，不经过 cmd.exe）
#
# ⚠️ 为什么需要这个脚本（2026-10-06 实测，踩过的坑都在这）：
#   1. 本环境**跑不了 cmd.exe**（从 Bash/PowerShell 起 cmd 会被安全策略拦），
#      所以 build-qt-agent.bat 这条 vcvarsall → cmake 的路走不通。
#   2. 不跑 vcvarsall 就得自己喂 INCLUDE / LIB，否则 cl.exe 报
#      C1034/C1083「找不到 stdio.h / type_traits」。
#   3. **MSVC 的 include 里那批 C 运行头（stdio.h/stdlib.h/string.h/...）在本机缺失**，
#      它们实际由 Windows SDK 的 **UCRT**（Universal CRT）提供：
#          Windows Kits/10/Include/<ver>/ucrt   ← 头文件
#          Windows Kits/10/Lib/<ver>/ucrt/x64  ← 导入库 ucrt.lib
#      少了这两个路径，编译过不了（C1083）／链接过不了（LNK1104: ucrt.lib）。
#   4. Git Bash 会把 `/c/...` 这种 POSIX 路径传给 Win32 程序当成相对路径 →
#      INCLUDE/LIB 里**必须写 Windows 风格**（C:/... 或反斜杠），不能用 /c/...。
#   5. cmake / ninja 在 python 虚拟环境里（自写 .bat 必须把 Scripts 加进 PATH），
#      否则报 "cmake: 无法运行"。
#
# 用法：./scripts/build-agent.sh          编译
#       ./scripts/build-agent.sh --clean   先删 build 再配再编（工具链换了才需要）
set -euo pipefail

cd "$(dirname "$0")/.."
REPO="$(pwd)"

PY_ENV="/c/Users/Administrator/.workbuddy/binaries/python/envs/default/Scripts"
export PATH="$PY_ENV:$PATH"

MSVC="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDK_VER="10.0.26100.0"          # 本机唯一装的那版；ls "Windows Kits/10/Include" 可查

export INCLUDE="$MSVC/include;$SDK/Include/$SDK_VER/ucrt;$SDK/Include/$SDK_VER;$SDK/Include/$SDK_VER/um;$SDK/Include/$SDK_VER/shared;$SDK/Include/$SDK_VER/winrt"
export LIB="$MSVC/lib/x64;$SDK/Lib/$SDK_VER/ucrt/x64;$SDK/Lib/$SDK_VER/um/x64"

if [ "${1:-}" = "--clean" ]; then
    rm -rf "$REPO/build"
    cmake -S "$REPO" -B "$REPO/build" -G Ninja
fi

cmake --build "$REPO/build"
echo "[build-agent] OK → $REPO/build/stelarith-agent-qt.exe"
