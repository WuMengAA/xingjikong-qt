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

# ---- 0b) TLS 后端体检（2026-10-13 生产事故修复）--------------------------------
# 事故：学生机 DESKTOP-I15OM55 上 Qt 的 availableBackends() 只剩 "cert-only"
#   ⇒ TLS 完全不可用 ⇒ ① wss:// 连不上（表现为"被控端连不上地址"）
#                    ② HTTPS 请求全 "HTTP 0"（TLS initialization failed）。
# 根因：windeployqt 只部署它认为够用的 certonly + schannel，**不拷 openssl 后端**
#   （实测 `windeployqt --list relative` 只列这 2 个）；而 schannel 在部分机器
#   （杀软拦截 / 系统策略 / LTSC 精简）上会加载失败 ⇒ 退化成只剩 cert-only。
# 处置：出包前强制三个后端齐全。openssl 是第二条路径（配 build/ 里已有的
#   libssl-3-x64.dll / libcrypto-3-x64.dll，installer.iss 已点名）。
#   补不齐就 FATAL —— 宁可不出包，也不出"装上去连不上云端"的半残包。
QT_TLS_DIR="D:/QTqwq/6.12.0/msvc2022_64/plugins/tls"
mkdir -p build/tls
for b in qcertonlybackend.dll qschannelbackend.dll qopensslbackend.dll; do
    if [ ! -f "build/tls/$b" ]; then
        if [ -f "$QT_TLS_DIR/$b" ]; then
            cp -f "$QT_TLS_DIR/$b" build/tls/ && echo "[tls] 补入 $b（windeployqt 漏拷）"
        else
            echo "[FATAL] TLS 后端缺失且无法补齐：build/tls/$b"
            echo "        Qt 源目录也没有：$QT_TLS_DIR/$b —— 检查 Qt 安装是否完整"
            exit 3
        fi
    fi
done
echo "[tls] 后端齐全：$(ls build/tls/*.dll | xargs -n1 basename | tr '\n' ' ')"

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
