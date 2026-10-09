#!/usr/bin/env bash
# 星集控被控端 —— 打包安装包（NSIS CLI 版）。
#
# WHY THIS EXISTS (2026-10-06):
#   build-agent-installer.bat does exactly the same thing, but it needs cmd.exe.
#   On this machine neither Bash nor PowerShell can launch cmd.exe (the sandbox
#   blocks it as "bypasses command validation"), so the .bat cannot be run from
#   WorkBuddy at all. This script calls makensis.exe directly and needs no shell
#   other than Git Bash.
#
# WHY TMP/TEMP ARE SET EXPLICITLY:
#   Git Bash exports TMP=/tmp and TEMP=/tmp — an MSYS path. makensis.exe is a
#   native Win32 program: it reads %TEMP% literally and /tmp resolves somewhere
#   it cannot write, which surfaces as a modal
#       "Error writing temporary file. Make sure your temp folder is valid."
#   (that dialog is makensisw.exe's; the CLI compiler prints the same condition
#   and returns a non-zero exit code instead of popping a window).
#   Both compilers write stubs + redirected output into %TEMP%, so pointing it
#   at the real temp dir removes the whole failure mode.
#
# Usage: ./build-agent-installer.sh

set -uo pipefail
cd "$(dirname "$0")" || exit 3

NSIS_EXE='C:/Program Files (x86)/NSIS/makensis.exe'
SETUP='dist/stelarith-agent-setup.exe'

# ---- 0) preconditions: installer.nsi does File /r "deploy\*.*" ----
#        A missing deploy\ compiles "successfully" and ships an EMPTY setup,
#        which is much harder to notice than a hard error.
if [ ! -d deploy ]; then
    echo "[FATAL] deploy/ is missing - run build-qt-agent.bat first"
    exit 3
fi
if [ ! -d dist ]; then
    echo "[FATAL] dist/ is missing - create it before packaging"
    exit 3
fi
if [ ! -f installer.nsi ]; then
    echo "[FATAL] installer.nsi not found (run this script from the project root)"
    exit 3
fi
if [ ! -f "$NSIS_EXE" ]; then
    echo "[FATAL] NSIS compiler not found at $NSIS_EXE"
    exit 3
fi

# ---- 1.5) 版本常量（installer.nsi 用 !include "build\version.nsh" 吃它）----
#       2026-10-07 起版本号唯一真源是 CMakeLists.txt 的 set(AGENT_VERSION ...)，
#       installer.nsi 不再手抄一份 —— 代价是这个文件必须先由 cmake 生成出来。
#       不查就编译，makensis 只会含糊地报 "Can't open build\version.nsh"，再白跑两分钟。
if [ ! -f build/version.nsh ]; then
    echo "[FATAL] build/version.nsh 不存在 —— 先跑一次 cmake 生成版本常量："
    echo "        cmake -G Ninja -B build -S .       （或完整构建一次 build-agent.sh）"
    exit 3
fi

#       ⚠️ 只查「存在」不够：改了 CMakeLists.txt 的 set(AGENT_VERSION ...) 却没重跑 cmake，
#          build/version.nsh 会留着旧值，makensis 不报错、直接把旧版本号编进安装包 ——
#          装完版本号跟 OTA 对不上，比「文件缺失」难查得多。所以再比一次 mtime。
if [ build/version.nsh -ot CMakeLists.txt ]; then
    echo "[FATAL] build/version.nsh 已过期（早于 CMakeLists.txt 的修改时间）"
    echo "        说明改过 set(AGENT_VERSION ...) 却没重新 configure —— 现在里面还是旧版本号。"
    echo "        重跑一次： cmake -G Ninja -B build -S ."
    exit 3
fi

# ---- 1) a temp folder that exists and is writable ----
TEMP_DIR="${USERPROFILE:-}/AppData/Local/Temp"
TEMP_DIR="${TEMP_DIR#/}"                 # USERPROFILE is already Windows-style
export TMP="$TEMP_DIR"
export TEMP="$TEMP_DIR"
mkdir -p "$TEMP_DIR" 2>/dev/null || true
echo "[tmp] TMP=${TMP//\//\\}"

probe="$TEMP_DIR/.nsis-tmpcheck"
rm -f "$probe" 2>/dev/null || true
if ! echo . >"$probe" 2>/dev/null; then
    echo "[FATAL] temp folder is not writable: $TEMP_DIR"
    echo "        That exact path must exist and be writable by the current user."
    echo "        In an elevated cmd:  mkdir \"$TEMP_DIR\""
    echo "                            icacls \"$TEMP_DIR\" /grant \"%USERNAME%:(OI)(OI)F\""
    exit 2
fi
rm -f "$probe" 2>/dev/null || true
echo "[step] temp folder writable OK"

# ---- 2) compile with the CLI compiler (never makensisw: no modal dialogs) ----
echo "[step] makensis installer.nsi  (this takes ~2 min: lzma solid over 215 MB)"
"$NSIS_EXE" installer.nsi
rc=$?

if [ "$rc" -eq 0 ]; then
    echo "[ok] installer written: $(pwd)/$SETUP"
else
    echo "[FAIL] makensis exited with $rc - see the lines above"
fi
echo "[exit] $rc"
exit "$rc"
