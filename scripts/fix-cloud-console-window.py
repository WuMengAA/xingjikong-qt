#!/usr/bin/env python3
r"""星集控 · 根治云端两个计划任务的 cmd 弹窗（2026-10-22）。

问题（2026-10-22 16:00 诊断实证）：
  `StelarithCloud`（开机常驻）与 `StelarithCloudWatchdog`（每 5 分钟）的动作
  分别是 run-cloud.cmd / cloud-watchdog.cmd。Task Scheduler 给 .bat/.cmd 动作
  **必分配一个 cmd.exe 控制台窗口**（任务设 Hidden 只隐调度器 UI，不隐桌面），
  于是：
    · 云端 node 握着控制台 → 黑窗一直挂在桌上（cmd 退出也不关窗，子进程占着）；
    · 看门狗每 5 分钟跑一次 → 黑窗闪一次。
  2026-10-22 已用同法修好 AgentGuard / P2P-Signaling（wscript+vbs，零弹窗），
  漏了云端这两个 —— 用户看到的弹窗主要来自它们。

修法（与 guard 完全同构、已验证的机制）：
  动作换成 `wscript.exe <同目录 *-hidden.vbs>`。wscript 是 GUI 子系统宿主，
  天生不分配控制台；VBS 用窗口样式 0（SW_HIDE）启动 cmd 并**等待返回**
  （不等的话 Task Scheduler 会立刻回收子进程树，把任务掐了）。
  .cmd 逻辑一行不改 —— vbs 只是壳。

关于重启云端：
  诊断发现当前 node 是**孤儿**（父 cmd 已死、任务实例已结束），它仍握着
  11:15 那个可见控制台。要消窗必须让这个 node 退出、再由（已改为隐藏动作的）
  任务重新拉起。停机约数秒，WS 客户端自动重连；拉起失败会自动回滚动作并
  重试，看门狗（同样已改隐藏）5 分钟内兜底。

安全：
  · 只改两个任务的动作 + 杀一个孤儿 node + 重启任务；.cmd / .js 逻辑零改动。
  · 改前打印现状，改后回读校验；拉起失败自动还原动作。
  · 默认 dry-run，--apply 才落盘。

用法（受管 Python，venv 里有 psutil/pywin32）：
  python scripts/fix-cloud-console-window.py            # 看现状与计划
  python scripts/fix-cloud-console-window.py --apply    # 执行（含重启 + 验证）
  python scripts/fix-cloud-console-window.py --verify   # 只验证（不落地）
  python scripts/fix-cloud-console-window.py --rollback # 还原为 .cmd 动作
"""
from __future__ import annotations

import argparse
import ctypes
import os
import subprocess
import sys
import time
from pathlib import Path

import psutil

CLOUD_DIR = Path(__file__).resolve().parent.parent
PORT = 8788
VBS = {
    "StelarithCloud": "run-cloud-hidden.vbs",
    "StelarithCloudWatchdog": "cloud-watchdog-hidden.vbs",
}
CMD = {
    "StelarithCloud": "run-cloud.cmd",
    "StelarithCloudWatchdog": "cloud-watchdog.cmd",
}
WSCRIPT = r"C:\Windows\System32\wscript.exe"


# ── 基础工具 ────────────────────────────────────────────────────────────
def log(msg: str) -> None:
    print(msg, flush=True)


def sh(args: list[str], timeout: int = 60) -> tuple[int, str]:
    try:
        p = subprocess.run(args, capture_output=True, timeout=timeout)
        out = (p.stdout + p.stderr).decode("utf-8", "replace")
        if not out.strip():
            out = (p.stdout + p.stderr).decode("gbk", "replace")
        return p.returncode, out
    except Exception as e:  # noqa: BLE001
        return -1, f"[err] {e}"


def task_folder():
    import win32com.client  # noqa: PLC0415

    svc = win32com.client.Dispatch("Schedule.Service")
    svc.Connect()
    return svc.GetFolder("\\")


def show_task(folder, name: str) -> str:
    t = folder.GetTask(name)
    d = t.Definition
    path = d.Actions[0].Path
    args = d.Actions[0].Arguments
    log(f"  动作: {path} {args}".rstrip())
    log(f"  Hidden={d.Settings.Hidden}  上次结果={t.LastTaskResult}")
    return f"{path} {args}".strip()


def set_action(folder, name: str, exe: str, args: str, workdir: str) -> None:
    """保留 Principal/Settings/Triggers，只换动作。"""
    t = folder.GetTask(name)
    d = t.Definition
    # ⚠️ 晚绑定 COM 的集合方法是 Remove(index)（1 基），不是 RemoveAt。
    while d.Actions.Count > 0:
        d.Actions.Remove(1)
    act = d.Actions.Create(0)  # 0 = TASK_ACTION_EXEC
    act.Path = exe
    act.Arguments = args
    act.WorkingDirectory = workdir
    folder.RegisterTaskDefinition(name, d, 4, "", "", 0)


def port_up() -> bool:
    import socket

    s = socket.socket()
    s.settimeout(1.0)
    try:
        s.connect(("127.0.0.1", PORT))
        return True
    except OSError:
        return False
    finally:
        s.close()


def cloud_node_pids() -> list[int]:
    """当前跑着 src\\index.js 的云端 node 进程。"""
    out = []
    for p in psutil.process_iter(["pid", "name", "cmdline"]):
        try:
            if (p.info["name"] or "").lower() != "node.exe":
                continue
            cl = " ".join(p.info["cmdline"] or [])
            if "index.js" in cl and "env-file" in cl:
                out.append(p.info["pid"])
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


def console_window_hidden(pid: int) -> tuple[bool | None, str]:
    """附到 pid 的控制台，返回 (是否安全无可见窗, 说明)。三态：

      (True, ...)  = 安全：无控制台，或控制台窗存在但不可见
      (False, ...) = 有可见控制台窗（弹窗实锤）
      (None, ...)  = **不可判定**（AttachConsole ACCESS_DENIED 等）——
                     2026-10-22 实证：SYSTEM 进程的控制台从本会话 AttachConsole
                     恒 ACCESS_DENIED（对照组同样失败），此路对 SYSTEM 任务无效，
                     只能当佐证，不能当通过条件。
    """
    k32 = ctypes.windll.kernel32
    u32 = ctypes.windll.user32
    if not k32.AttachConsole(pid):
        return None, f"不可判定 (AttachConsole GetLastError={k32.GetLastError()})"
    try:
        hwnd = u32.GetConsoleWindow()
        if not hwnd:
            return True, "无控制台窗"
        if u32.IsWindowVisible(hwnd):
            return False, "VISIBLE console window!"
        return True, "console window hidden (SW_HIDE)"
    finally:
        k32.FreeConsole()


def new_procs_since(t0: float) -> list[dict]:
    out = []
    for p in psutil.process_iter(["pid", "ppid", "name", "cmdline", "create_time"]):
        try:
            ct = p.info["create_time"] or 0
            if ct < t0 - 0.5:
                continue
            out.append({
                "pid": p.info["pid"],
                "ppid": p.info["ppid"],
                "name": (p.info["name"] or "").lower(),
                "cmd": " ".join(p.info["cmdline"] or []),
            })
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return out


def wait_port(timeout: float = 40.0) -> bool:
    t0 = time.time()
    while time.time() - t0 < timeout:
        if port_up():
            return True
        time.sleep(0.5)
    return False


# ── 验证 ────────────────────────────────────────────────────────────────
def verify_watchdog(folder) -> bool:
    log("\n== 验证看门狗：15ms 密集采样 + 手动跑一次 ==")
    t0 = time.time()
    folder.GetTask("StelarithCloudWatchdog").Run("")
    seen: dict[int, dict] = {}
    wscript_pids: set[int] = set()
    # 看门狗端口通时 ~0.5s 就跑完，100ms 轮询会漏；15ms 密集采样 + create_time 过滤
    while time.time() - t0 < 12:
        for p in psutil.process_iter(["pid", "ppid", "name", "cmdline", "create_time"]):
            try:
                if (p.info["create_time"] or 0) < t0 - 1.0:
                    continue
                nm = (p.info["name"] or "").lower()
                if nm not in ("wscript.exe", "cmd.exe", "netstat.exe", "findstr.exe"):
                    continue
                pid = p.info["pid"]
                if pid in seen:
                    continue
                pr = {"pid": pid, "ppid": p.info["ppid"], "name": nm,
                      "cmd": " ".join(p.info["cmdline"] or [])}
                seen[pid] = pr
                log(f"  · 新进程 {nm:<14} pid={pid:<7} ppid={pr['ppid']:<7} {pr['cmd'][:80]}")
                if nm == "wscript.exe":
                    wscript_pids.add(pid)
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
        time.sleep(0.015)
    # 采集结束后再判定：vbs 壳起的 cmd 其 ppid 必在 wscript_pids 里；
    # 调度器直拉的 cmd（旧 .cmd 行为）ppid 是 svchost，不会在。
    bad = [pr for pr in seen.values()
           if pr["name"] == "cmd.exe" and pr["ppid"] not in wscript_pids]
    t = folder.GetTask("StelarithCloudWatchdog")
    log(f"  任务上次结果={t.LastTaskResult}（0 = 成功）")
    ok = True
    for pr in bad:
        log(f"  ❌ 非 wscript 直拉的 cmd.exe pid={pr['pid']} ppid={pr['ppid']} {pr['cmd'][:80]}")
    if bad:
        ok = False
    if not wscript_pids:
        log("  ❌ 没抓到 wscript.exe —— vbs 壳没跑起来？")
        ok = False
    if t.LastTaskResult not in (0, 267009):  # 0=成功 267009=运行中
        log(f"  ❌ 任务结果码异常: {t.LastTaskResult}")
        ok = False
    # 对 vbs 壳里的 cmd 做控制台可见性抽查（多半已退出，抓不到就算）
    for pr in seen.values():
        if pr["name"] == "cmd.exe" and pr["ppid"] in wscript_pids:
            if psutil.pid_exists(pr["pid"]):
                safe, why = console_window_hidden(pr["pid"])
                log(f"  · vbs 壳 cmd（pid={pr['pid']}）控制台: {why}")
                if safe is False:
                    ok = False
            else:
                log("  · vbs 壳 cmd 已退出（快进程）；可见性由云端 node 同机制佐证")
            break
    if ok:
        log("  ✅ 看门狗只起 wscript（GUI 宿主无控制台），无调度器直拉 cmd")
    return ok


def verify_cloud(folder, old_pid: int | None) -> bool:
    log("\n== 验证云端：端口 / 进程链 / 控制台可见性 ==")
    ok = True
    if old_pid:
        alive = psutil.pid_exists(old_pid)
        log(f"  旧孤儿 node（pid={old_pid}）: {'还在（异常）' if alive else '已退出 ✅'}")
        if alive:
            ok = False
    pids = cloud_node_pids()
    if not pids:
        log("  ❌ 找不到云端 node 进程")
        return False
    for pid in pids:
        try:
            p = psutil.Process(pid)
            cl = " ".join(p.cmdline())
            log(f"  云端 node pid={pid} ppid={p.ppid()}")
            log(f"    cmd: {cl[:150]}")
            safe, why = console_window_hidden(pid)
            log(f"    控制台: {why} {'✅' if safe else ('⚠️ 不可判定' if safe is None else '❌')}")
            if safe is False:
                ok = False
        except (psutil.NoSuchProcess, psutil.AccessDenied) as e:
            log(f"  [warn] pid={pid}: {e}")
    if not port_up():
        log("  ❌ 8788 未监听")
        ok = False
    else:
        log("  ✅ 8788 在听")
    return ok


# ── 主流程 ──────────────────────────────────────────────────────────────
def apply(folder) -> bool:
    log("== 改前现状 ==")
    for name in VBS:
        log(f"■ {name}")
        show_task(folder, name)

    old_pids = cloud_node_pids()
    old_pid = old_pids[0] if old_pids else None
    log(f"\n当前云端 node: {old_pids or '（无）'}")

    log("\n== 改动作 ==")
    for name, vbs in VBS.items():
        vbs_path = CLOUD_DIR / vbs
        if not vbs_path.exists():
            log(f"  [失败] 缺 {vbs_path}")
            return False
        set_action(folder, name, WSCRIPT, f'"{vbs_path}"', str(CLOUD_DIR))
        log(f"  {name} → wscript.exe \"{vbs_path}\"")

    log("\n== 回读校验 ==")
    for name in VBS:
        log(f"■ {name}")
        show_task(folder, name)

    # 重启云端任务：先结束旧实例（可能已死），杀掉孤儿 node（它握着可见控制台），
    # 再用新动作拉起。
    log("\n== 重启 StelarithCloud（让可见控制台消失、以隐藏动作拉起）==")
    sh(["schtasks", "/End", "/TN", "StelarithCloud"])
    time.sleep(1)
    if old_pid and psutil.pid_exists(old_pid):
        log(f"  杀孤儿 node pid={old_pid}（父 cmd 已死、任务实例已结束，它握着旧控制台）")
        try:
            psutil.Process(old_pid).kill()
            psutil.Process(old_pid).wait(timeout=5)
        except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.TimeoutExpired) as e:
            log(f"  [warn] kill: {e}")
    rc, out = sh(["schtasks", "/Run", "/TN", "StelarithCloud"])
    log(f"  /Run rc={rc} {out.strip()[:80]}")
    if not wait_port():
        log("  ⚠️ 40s 没等到 8788，再试一次 /Run")
        sh(["schtasks", "/Run", "/TN", "StelarithCloud"])
    if not wait_port():
        log("  ❌ 云端没起来 —— 回滚动作并重新拉起")
        for name, cmd in CMD.items():
            set_action(folder, name, str(CLOUD_DIR / cmd), "", str(CLOUD_DIR))
        sh(["schtasks", "/Run", "/TN", "StelarithCloud"])
        wait_port(30)
        return False

    ok_cloud = verify_cloud(folder, old_pid)
    ok_watch = verify_watchdog(folder)
    log("\n== 总结 ==")
    log(f"  云端: {'✅' if ok_cloud else '❌'}   看门狗: {'✅' if ok_watch else '❌'}")
    return ok_cloud and ok_watch


def rollback(folder) -> None:
    log("== 还原为 .cmd 直拉动作 ==")
    for name, cmd in CMD.items():
        set_action(folder, name, str(CLOUD_DIR / cmd), "", str(CLOUD_DIR))
        log(f"  {name} → {CLOUD_DIR / cmd}")
    sh(["schtasks", "/Run", "/TN", "StelarithCloud"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--rollback", action="store_true")
    a = ap.parse_args()

    folder = task_folder()
    if a.rollback:
        rollback(folder)
        return 0

    log("== 现状 ==")
    for name in VBS:
        log(f"■ {name}")
        show_task(folder, name)
    log(f"端口 {PORT}: {'在听' if port_up() else '不在听'}")
    log(f"云端 node: {cloud_node_pids() or '（无）'}")

    if a.verify:
        ok_watch = verify_watchdog(folder)
        ok_cloud = verify_cloud(folder, None)
        return 0 if (ok_watch and ok_cloud) else 1

    if not a.apply:
        log("\n[dry-run] 未改动。计划：")
        for name, vbs in VBS.items():
            log(f"  {name}: {CMD[name]} → wscript.exe {CLOUD_DIR / vbs}")
        log("  然后重启 StelarithCloud 任务（杀孤儿 node + 隐藏动作拉起）并验证两者。")
        return 0

    return 0 if apply(folder) else 1


if __name__ == "__main__":
    sys.exit(main())
