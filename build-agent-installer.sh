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
