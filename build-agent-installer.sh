#!/usr/bin/env bash
# 星集控被控端 —— 打包安装包（Inno Setup CLI 版，2026-10-10 从 NSIS 迁移）。
#
# WHY THIS EXISTS: build-agent-installer.bat does the same, but needs cmd.exe.
#   On this machine neither Bash nor PowerShell can launch cmd.exe (the sandbox
#   blocks it as "bypasses command validation"), so the .bat cannot be run from
#   WorkBuddy at all. This script calls ISCC.exe directly and needs no shell
#   other than Git Bash.
#
# WHY TMP/TEMP ARE SET EXPLICITLY: Git Bash exports TMP=/tmp (an MSYS path).
#   ISCC.exe is a native Win32 program: it reads %TEMP% literally and /tmp resolves
#   somewhere it cannot write. Pointing it at the real temp dir removes that.
#
# Usage: ./build-agent-installer.sh

set -uo pipefail
cd "$(dirname "$0")" || exit 3

ISCC_EXE='/c/Users/Administrator/AppData/Local/Programs/Inno Setup 6/ISCC.exe'

# ---- 0) preconditions ----
if [ ! -d deploy ]; then
    echo "[FATAL] deploy/ is missing - run build-qt-agent.bat first"
    exit 3
fi
if [ ! -d dist ]; then
    echo "[FATAL] dist/ is missing - create it before packaging"
    exit 3
fi
if [ ! -f installer.iss ]; then
    echo "[FATAL] installer.iss not found (run this script from the project root)"
    exit 3
fi
if [ ! -f "$ISCC_EXE" ]; then
    echo "[FATAL] Inno Setup compiler not found at $ISCC_EXE"
    exit 3
fi

# ---- 1) version from build/version.nsh (唯一真源＝CMakeLists AGENT_VERSION) ----
if [ ! -f build/version.nsh ]; then
    echo "[FATAL] build/version.nsh missing - run cmake first to generate it"
    exit 3
fi
if [ build/version.nsh -ot CMakeLists.txt ]; then
    echo "[FATAL] build/version.nsh is stale (older than CMakeLists.txt)"
    echo "        rerun: cmake -G Ninja -B build -S ."
    exit 3
fi
VER=$(grep -oP '!define\s+VER\s+"\K[^"]+' build/version.nsh)
if [ -z "$VER" ]; then
    echo "[FATAL] cannot parse VER from build/version.nsh"
    exit 3
fi
echo "[version] $VER"

# ---- 2) temp folder ----
TEMP_DIR="${USERPROFILE:-}/AppData/Local/Temp"
TEMP_DIR="${TEMP_DIR#/}"
export TMP="$TEMP_DIR"
export TEMP="$TEMP_DIR"
mkdir -p "$TEMP_DIR" 2>/dev/null || true
echo "[tmp] TMP=${TMP//\//\\}"

# ---- 3) compile (version via env; ISCC reads it through GetEnv) ----
echo "[step] ISCC installer.iss  (takes ~1 min: lzma2 over ~93 MB)"
STE_INSTALLER_VERSION="$VER" "$ISCC_EXE" installer.iss
rc=$?

if [ "$rc" -eq 0 ]; then
    echo "[ok] installer written: $(pwd)/dist/stelarith-agent-setup-$VER.exe"
else
    echo "[FAIL] ISCC exited with $rc - see the lines above"
fi
echo "[exit] $rc"
exit "$rc"
