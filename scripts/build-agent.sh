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
#   6. **clean build（删掉 build/ 再配）会配置失败**，因为 configure 阶段需要的东西
#      增量构建靠 CMakeCache.txt 缓存蒙混过去了：
#        · Qt 找不到 → 必须 -DCMAKE_PREFIX_PATH 指到 Qt 6.12.0；
#        · mt.exe（清单嵌入）/ rc.exe（.rc 资源）找不到 → 必须显式给
#          CMAKE_MT / CMAKE_RC_COMPILER 路径，且把 MSVC/SDK 的 bin 加进 PATH
#          （cmake 内部 try_compile / 资源编译会直接调 cl/rc/mt）；
#        · 编译器 cl.exe 找不到 → 必须显式 -DCMAKE_C(XX)_COMPILER。
#      下面统一用 configure() 收敛全部 -D 参数，--clean 与「缓存丢失自动补配」复用。
#
# 用法：./scripts/build-agent.sh          编译（缓存在就增量，丢了自动补配）
#       ./scripts/build-agent.sh --clean   先删 build 再配再编（工具链换了才需要）
set -euo pipefail

cd "$(dirname "$0")/.."
REPO="$(pwd)"

PY_ENV="/c/Users/Administrator/.workbuddy/binaries/python/envs/default/Scripts"
export PATH="$PY_ENV:$PATH"

MSVC="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDK_VER="10.0.26100.0"          # 本机唯一装的那版；ls "Windows Kits/10/Include" 可查
QT="D:/QTqwq/6.12.0/msvc2022_64"   # 编译用 Qt（注意是 QTqwq，不是 Qt）

# 工具链 bin 进 PATH：cl/link/rc/mt 都靠它（clean 配置 / 资源编译 / 清单嵌入会直接调）
export PATH="$MSVC/bin/Hostx64/x64:$SDK/bin/$SDK_VER/x64:$PATH"
# UCRT + SDK 头/库（缺则 C1083 / LNK1104）
export INCLUDE="$MSVC/include;$SDK/Include/$SDK_VER/ucrt;$SDK/Include/$SDK_VER;$SDK/Include/$SDK_VER/um;$SDK/Include/$SDK_VER/shared;$SDK/Include/$SDK_VER/winrt"
export LIB="$MSVC/lib/x64;$SDK/Lib/$SDK_VER/ucrt/x64;$SDK/Lib/$SDK_VER/um/x64"

# 全部 -D 收敛到一处：增量构建靠缓存，clean / 缓存丢失时复用，避免配置失败
configure() {
  cmake -S "$REPO" -B "$REPO/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT" \
    -DCMAKE_C_COMPILER="$MSVC/bin/Hostx64/x64/cl.exe" \
    -DCMAKE_CXX_COMPILER="$MSVC/bin/Hostx64/x64/cl.exe" \
    -DCMAKE_RC_COMPILER="$SDK/bin/$SDK_VER/x64/rc.exe" \
    -DCMAKE_MT:FILEPATH="$SDK/bin/$SDK_VER/x64/mt.exe"
}

if [ "${1:-}" = "--clean" ]; then
    rm -rf "$REPO/build"
    configure
fi

# 增量构建前，若从未配置过（首次 / 缓存被清），补一次 configure，避免直接 --build 失败
if [ ! -f "$REPO/build/CMakeCache.txt" ]; then
    configure
fi

cmake --build "$REPO/build"
echo "[build-agent] OK → $REPO/build/stelarith-agent-qt.exe"
