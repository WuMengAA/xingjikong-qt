#!/usr/bin/env python3
"""星集控 · 语音 P1（0.6.47-rc.2）生产部署 —— 分步、可回滚、默认 dry-run。

为什么是 Pandas 之外还要这个脚本：
  发版当天要做三件互不相干的事（换教室机 exe / 重启云端 / 发布 OTA），
  每件的风险面不同，必须能**单独授权、单独回滚**，不能一把梭。

安全设计：
  · 默认 **dry-run**：只打印将要做什么 + 当前指纹，不改任何东西。
  · 每步一个开关，互不牵连：--agent / --cloud / --publish。
  · 换文件一律"先改名备份、再拷新的、再 sha256 比对"，失败自动回滚备份。
  · 停服务只按 **PID** 停（云端先用 8788 监听者反查），不用 taskkill /IM。
  · 全程不调 cmd.exe / PowerShell（进程用 psutil、任务用 pywin32 COM）。

用法：
  python scripts/deploy-voice-rc2.py                        # dry-run，看计划
  python scripts/deploy-voice-rc2.py --agent --apply        # 只换教室机 agent
  python scripts/deploy-voice-rc2.py --cloud --apply        # 只重启云端
  python scripts/deploy-voice-rc2.py --publish --apply      # 只拷包 + 写 ota.json
  python scripts/deploy-voice-rc2.py --agent --cloud --publish --apply   # 全套

回滚：
  · agent ：把 <安装目录>\\stelarith-agent-qt.exe.bak-<时间戳> 改回原名，重启进程。
  · cloud ：git -C Stelarith-cloud 无改动时直接重启即可；改了代码则先 git stash/回退。
  · ota   ：ota.json.bak-<时间戳> 改回原名（云端每请求现读，立刻生效）。
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import socket
import sys
import time
import urllib.request

VER = "0.6.47-rc.2"
WS = r"D:\Stelarith\Stelarith-xingjikong-qt"
CTRL = os.path.join(WS, "Stelarith-control-qt")
VIEW = os.path.join(WS, "Stelarith-viewer-qt")
CLOUD = os.path.join(WS, "Stelarith-cloud")

PROD_DIR = r"C:\Program Files\Stelarith"
PROD_EXE = os.path.join(PROD_DIR, "stelarith-agent-qt.exe")

AGENT_PKG = os.path.join(CTRL, "dist", f"stelarith-agent-setup-{VER}.exe")
VIEWER_PKG = os.path.join(VIEW, "dist", f"stelarith-viewer-setup-{VER}.exe")
APK = os.path.join(VIEW, "dist", "星集控-手机端-manager-arm64-release.apk")

OTA = os.path.join(CLOUD, "ota.json")
PKG_DIR = os.path.join(CLOUD, "assets", "pkg")
CLOUD_PORT = 8788

TS = time.strftime("%Y%m%d-%H%M%S")


def sha256f(p: str) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def say(msg: str) -> None:
    print(msg, flush=True)


def port_owner(port: int):
    """反查监听某端口的进程（避免 taskkill /IM 误杀同名的其它 node）。"""
    import psutil
    for c in psutil.net_connections(kind="inet"):
        if c.status == psutil.CONN_LISTEN and c.laddr and c.laddr.port == port and c.pid:
            return c.pid
    return None


def agent_pids():
    import psutil
    out = []
    for p in psutil.process_iter(["pid", "exe"]):
        try:
            if p.info["exe"] and os.path.normcase(p.info["exe"]) == os.path.normcase(PROD_EXE):
                out.append(p.info["pid"])
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


def task_com():
    import win32com.client
    svc = win32com.client.Dispatch("Schedule.Service")
    svc.Connect()
    return svc.GetFolder("\\")


# ─────────────────────────── 预检 ───────────────────────────
def preflight() -> bool:
    ok = True
    say("== 预检 ==")
    now = time.time()
    for label, p in (("agent 安装包", AGENT_PKG), ("viewer 安装包", VIEWER_PKG), ("APK", APK)):
        if os.path.exists(p):
            age_h = (now - os.path.getmtime(p)) / 3600
            fresh = age_h <= 2
            flag = "OK " if fresh else "旧!"
            say(f"  [{flag}] {label}: {os.path.basename(p)} "
                f"{os.path.getsize(p)/1024/1024:.1f} MB  产出于 {age_h:.1f} 小时前")
            # ⚠️ APK/安装包不新鲜 = 极可能把上次的旧包发出去（同名、体积接近，肉眼看不出）
            if not fresh:
                say(f"        ↑ 超过 2 小时，先重出包再发版")
                ok = False
        else:
            say(f"  [缺] {label}: {p}")
            ok = False
    if os.path.exists(PROD_EXE):
        say(f"  生产 agent: {os.path.getsize(PROD_EXE)} B sha256={sha256f(PROD_EXE)[:16]}…")
    else:
        say(f"  [警告] 生产 agent 不存在: {PROD_EXE}（教室机没装过？）")
    p = port_owner(CLOUD_PORT)
    say(f"  云端 8788 监听者: pid={p}")
    say(f"  生产 agent 进程: {agent_pids() or '未运行'}")
    return ok


# ─────────────────────────── 步骤 1：换 agent ───────────────────────────
def step_agent(apply: bool) -> bool:
    say("\n== 步骤 1：教室机 agent 换 0.6.47-rc.2 ==")
    src = os.path.join(CTRL, "build", "stelarith-agent-qt.exe")
    if not os.path.exists(src):
        say(f"  [失败] 找不到构建产物 {src}")
        return False
    want = sha256f(src)
    say(f"  源: {src}\n      sha256={want}")
    if not apply:
        say("  [dry-run] 将：停 AgentGuard 任务 → 按 PID 停 agent → 备份旧 exe → 拷新 exe → 校验 sha256 → 拉起 → 等上线")
        return True

    folder = task_com()
    try:
        t = folder.GetTask("StelarithAgentGuard")
        t.Stop()
        say("  AgentGuard 任务已停（防止换文件期间被守护动作干扰）")
    except Exception as e:
        say(f"  AgentGuard 停止跳过: {e}")

    for pid in agent_pids():
        try:
            import psutil
            psutil.Process(pid).terminate()
            say(f"  已终止 agent pid={pid}")
        except Exception as e:
            say(f"  终止 pid={pid} 失败: {e}")
    time.sleep(2)

    bak = f"{PROD_EXE}.bak-{TS}"
    try:
        if os.path.exists(PROD_EXE):
            os.replace(PROD_EXE, bak)
            say(f"  旧 exe 已备份 → {os.path.basename(bak)}")
        shutil.copy2(src, PROD_EXE)
        got = sha256f(PROD_EXE)
        if got != want:
            raise RuntimeError(f"拷贝后 sha256 不符：{got} != {want}")
        say(f"  新 exe 已就位，sha256 校验通过")
    except Exception as e:
        say(f"  [失败] {e} —— 回滚")
        if os.path.exists(bak):
            os.replace(bak, PROD_EXE)
            say("  已回滚旧 exe")
        return False

    # 拉起（隐藏、脱离本进程）
    DETACHED = 0x00000008
    NO_WINDOW = 0x08000000
    try:
        import subprocess
        subprocess.Popen([PROD_EXE], cwd=PROD_DIR,
                         creationflags=DETACHED | NO_WINDOW,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        say("  已拉起 agent（隐藏窗口）")
    except Exception as e:
        say(f"  [失败] 拉起失败: {e}")
        return False

    for i in range(20):
        time.sleep(1)
        if agent_pids():
            say(f"  agent 已运行 pid={agent_pids()}（{(i+1)}s）")
            return True
    say("  [警告] 20s 内没看到 agent 进程，请人工确认")
    return False


# ─────────────────────────── 步骤 2：重启云端 ───────────────────────────
def step_cloud(apply: bool) -> bool:
    say("\n== 步骤 2：重启云端（让 Opus 路由生效）==")
    pid = port_owner(CLOUD_PORT)
    if not pid:
        say("  [失败] 8788 没人在听，先确认云端在跑")
        return False
    say(f"  当前云端 pid={pid}")
    if not apply:
        say("  [dry-run] 将：停 StelarithCloud 任务 → 按 PID 停 node → 启任务 → 等 8788 回听 → 探 /api/public/ota")
        return True

    folder = task_com()
    for name in ("StelarithCloud", "StelarithCloudWatchdog"):
        try:
            folder.GetTask(name).Stop()
            say(f"  任务 {name} 已停")
        except Exception as e:
            say(f"  任务 {name} 停止跳过: {e}")

    import psutil
    try:
        psutil.Process(pid).terminate()
        psutil.Process(pid).wait(timeout=10)
        say(f"  已终止 node pid={pid}")
    except Exception as e:
        say(f"  终止失败: {e}")
        return False

    started = False
    for name in ("StelarithCloud", "StelarithCloudWatchdog"):
        try:
            folder.GetTask(name).Run("")
            say(f"  任务 {name} 已启动")
            started = True
            break
        except Exception as e:
            say(f"  任务 {name} 启动失败: {e}")
    if not started:
        say("  [失败] 计划任务都起不来，需要人工启动")
        return False

    for i in range(30):
        time.sleep(1)
        if port_owner(CLOUD_PORT):
            say(f"  云端已回听 8788（{(i+1)}s）pid={port_owner(CLOUD_PORT)}")
            try:
                body = urllib.request.urlopen(
                    f"http://127.0.0.1:{CLOUD_PORT}/api/public/ota?product=agent", timeout=5).read()
                j = json.loads(body)
                say(f"  /api/public/ota → latest={j.get('latest',{}).get('version')}")
            except Exception as e:
                say(f"  [警告] OTA 端点探测失败: {e}")
            return True
    say("  [警告] 30s 内 8788 未回听")
    return False


# ─────────────────────────── 步骤 3：发布 OTA ───────────────────────────
def step_publish(apply: bool) -> bool:
    say("\n== 步骤 3：拷安装包 + 写 ota.json（全量铺 0.6.47-rc.2）==")
    old = json.load(open(OTA, encoding="utf-8"))
    base = old["products"]["agent"]["url"].rsplit("/", 1)[0]      # 复用现有 URL 基址
    say(f"  当前 ota.json: agent={old['products']['agent']['version']} viewer={old['products']['viewer']['version']}")
    say(f"  URL 基址: {base}")

    plan = {}
    for prod, pkg in (("agent", AGENT_PKG), ("viewer", VIEWER_PKG)):
        if not os.path.exists(pkg):
            say(f"  [失败] 缺包: {pkg}")
            return False
        name = os.path.basename(pkg)
        plan[prod] = {
            "version": VER,
            "url": f"{base}/{name}",
            "sha256": sha256f(pkg),
            "size": os.path.getsize(pkg),
            "notes": "v0.6.47-rc.2 · 语音对讲 P1：Opus 编解码全链路（agent/viewer/cloud），"
                     "教室 60 台出口 10.12→1.96 Mbps；L1 隔离验证 14/14",
            "mandatory": False,
        }
        say(f"  {prod}: {name} {plan[prod]['size']/1024/1024:.1f} MB sha256={plan[prod]['sha256'][:16]}…")

    if not apply:
        say("  [dry-run] 将：拷两个安装包到 assets/pkg → 备份 ota.json → 写入新清单（agent+viewer）→ 回读校验")
        return True

    os.makedirs(PKG_DIR, exist_ok=True)
    for prod, pkg in (("agent", AGENT_PKG), ("viewer", VIEWER_PKG)):
        dst = os.path.join(PKG_DIR, os.path.basename(pkg))
        shutil.copy2(pkg, dst)
        if sha256f(dst) != plan[prod]["sha256"]:
            say(f"  [失败] {prod} 拷到 assets 后 sha256 不符")
            return False
        say(f"  已拷入 assets/pkg: {os.path.basename(dst)}")

    bak = f"{OTA}.bak-{TS}"
    shutil.copy2(OTA, bak)
    new = dict(old)
    new["generatedAt"] = time.strftime("%Y-%m-%dT%H:%M:%S.000Z", time.gmtime())
    new["generatedBy"] = f"deploy-voice-rc2.py ({TS}) · 语音 P1 Opus 全链路"
    new["products"] = plan
    # ⚠️ UTF-8 无 BOM（有 BOM 会被 JSON.parse 拒）
    with open(OTA, "w", encoding="utf-8", newline="\n") as f:
        json.dump(new, f, ensure_ascii=False, indent=2)
        f.write("\n")

    back = json.load(open(OTA, encoding="utf-8"))
    for prod in ("agent", "viewer"):
        if back["products"][prod]["version"] != VER:
            say("  [失败] 回读版本不符")
            os.replace(bak, OTA)
            return False
    say(f"  ota.json 已更新并回读校验通过（备份: {os.path.basename(bak)}）")

    try:
        j = json.loads(urllib.request.urlopen(
            f"http://127.0.0.1:{CLOUD_PORT}/api/public/ota?product=agent", timeout=5).read())
        say(f"  线上探针: agent latest={j.get('latest', {}).get('version')} size={j.get('latest', {}).get('size')}")
    except Exception as e:
        say(f"  [警告] 线上探针失败（不影响写入结果）: {e}")
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--agent", action="store_true", help="换教室机 agent exe")
    ap.add_argument("--cloud", action="store_true", help="重启云端")
    ap.add_argument("--publish", action="store_true", help="拷包 + 写 ota.json")
    ap.add_argument("--apply", action="store_true", help="真的执行（不带则 dry-run）")
    a = ap.parse_args()
    if not (a.agent or a.cloud or a.publish):
        say("请至少指定一步：--agent / --cloud / --publish（默认 dry-run）")
        return 2

    if not preflight():
        say("\n预检不通过，中止。")
        return 3

    ok = True
    if a.agent:
        ok &= step_agent(a.apply)
    if a.cloud:
        ok &= step_cloud(a.apply)
    if a.publish:
        ok &= step_publish(a.apply)

    say("\n== 结果 ==")
    say("  全部成功" if ok else "  有步骤失败，见上方输出（回滚办法见脚本头注释）")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
