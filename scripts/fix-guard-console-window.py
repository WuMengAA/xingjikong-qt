#!/usr/bin/env python3
r"""星集控 · 根治被控端 guard 的 cmd 弹窗（2026-10-22）。

问题：
  `StelarithAgentGuard` 计划任务的动作是 `stelarith-agent-guard.bat`。
  Task Scheduler 给 .bat/.cmd 动作**必分配一个 cmd.exe 控制台窗口**——
  任务计划器日志里能直接看到 `实例 "C:\\Windows\\SYSTEM32\\cmd.exe"`。
  任务设置里的 Hidden=true 只隐藏调度器 UI，不隐藏这个窗口，
  于是每 10 分钟闪一个黑窗（`closeOnExit=graceful` 只能让它 ~2 秒后自关，不能阻止它出现）。

修法：
  动作换成 `wscript.exe "…\stelarith-agent-guard-hidden.vbs"`。
  wscript 是 GUI 子系统宿主，**天生不分配控制台**；VBS 里用窗口样式 0 启动 PowerShell
  并**等待返回**（不等的话 Task Scheduler 会立刻回收子进程树，guard 跑不完）。

安全：
  · 只改这一个任务的动作 + 拷一个 .vbs 文件；不动 guard 的判定逻辑（ps1 一行不改）。
  · 改前打印现状，改后回读校验；失败时把动作还原。
  · 默认 dry-run，`--apply` 才落盘。

用法：
  python scripts/fix-guard-console-window.py            # 看现状与计划
  python scripts/fix-guard-console-window.py --apply    # 执行
  python scripts/fix-guard-console-window.py --apply --verify   # 执行后跑一次任务并数窗口
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import sys
import time

TASK = "StelarithAgentGuard"
PROD_DIR = r"C:\Program Files\Stelarith"
VBS_NAME = "stelarith-agent-guard-hidden.vbs"
VBS_SRC = os.path.join(r"D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-control-qt\deploy", VBS_NAME)
VBS_DST = os.path.join(PROD_DIR, VBS_NAME)

# Windows Terminal 的控制台窗口类名（默认终端下 cmd/powershell 都挂在这个类上）
WT_CLASS = "CASCADIA_HOSTING_WINDOW_CLASS"
CONSOLE_CLASS = "ConsoleWindowClass"


def count_windows() -> tuple[int, int]:
    """返回 (WT 控制台窗口数, 经典 conhost 窗口数)，只数可见的顶层窗。"""
    user32 = ctypes.windll.user32
    wt_n = con_n = 0
    buf = ctypes.create_unicode_buffer(256)

    EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)

    def cb(hwnd, _lparam):
        nonlocal wt_n, con_n
        if not user32.IsWindowVisible(hwnd):
            return True
        user32.GetClassNameW(hwnd, buf, 256)
        cls = buf.value
        if cls == WT_CLASS:
            wt_n += 1
        elif cls == CONSOLE_CLASS:
            con_n += 1
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    return wt_n, con_n


def task_folder():
    import win32com.client
    svc = win32com.client.Dispatch("Schedule.Service")
    svc.Connect()
    return svc.GetFolder("\\")


def show_task(folder) -> str:
    t = folder.GetTask(TASK)
    d = t.Definition
    path = d.Actions[0].Path
    args = d.Actions[0].Arguments
    print(f"  动作: {path} {args}".rstrip())
    print(f"  Hidden={d.Settings.Hidden}  上次结果={t.LastTaskResult}")
    return path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()

    print("== 现状 ==")
    folder = task_folder()
    old_path = show_task(folder)
    wt0, con0 = count_windows()
    print(f"  可见控制台窗口: WT={wt0} conhost={con0}")

    if not os.path.exists(VBS_SRC):
        print(f"  [失败] 找不到 {VBS_SRC}")
        return 3
    print(f"\n== 计划 ==")
    print(f"  1) 拷 {VBS_NAME} → {PROD_DIR}")
    print(f"  2) 任务动作: {old_path} → wscript.exe \"{VBS_DST}\"")

    if not a.apply:
        print("\n[dry-run] 未改动。加 --apply 执行。")
        return 0

    shutil.copy2(VBS_SRC, VBS_DST)
    print(f"  已拷入 {VBS_DST}")

    t = folder.GetTask(TASK)
    d = t.Definition
    # 保留原有 Principal/Settings/Triggers，只换动作
    # ⚠️ 晚绑定 COM 的集合方法名是 Remove(index)（1 基），不是 RemoveAt —— 写错会
    #    AttributeError: <unknown>.RemoveAt。先清空再从尾部往前删。
    while d.Actions.Count > 0:
        d.Actions.Remove(1)
    act = d.Actions.Create(0)          # 0 = TASK_ACTION_EXEC
    act.Path = r"C:\Windows\System32\wscript.exe"
    act.Arguments = f'"{VBS_DST}"'
    act.WorkingDirectory = PROD_DIR
    # 4 = TASK_CREATE_OR_UPDATE; 保持原 principal（Users 组 + HighestAvailable）
    folder.RegisterTaskDefinition(TASK, d, 4, "", "", 0)
    print("  动作已更新")

    print("\n== 回读校验 ==")
    show_task(folder)

    if a.verify:
        print("\n== 实跑一次任务，采样进程与窗口 ==")
        # ⚠️ 判据用**进程**而不是窗口：本会话的 EnumWindows 看不到用户桌面上的窗
        #    （实测恒 0），但"有没有起 cmd.exe"是可靠信号 —— 旧方案 Task Scheduler
        #    直接拉 cmd.exe，新方案只有 wscript + 隐藏的 powershell。
        import psutil
        t0 = time.time()
        before = count_windows()
        folder.GetTask(TASK).Run("")
        print(f"  已触发（t0），起始窗口 WT={before[0]} conhost={before[1]}")

        seen = {}
        peak = before
        for _ in range(150):          # 15 秒 × 100ms
            time.sleep(0.1)
            for p in psutil.process_iter(["name", "create_time"]):
                try:
                    n = (p.info["name"] or "").lower()
                    if n in ("cmd.exe", "wscript.exe", "powershell.exe", "conhost.exe") \
                            and (p.info["create_time"] or 0) >= t0 - 0.5:
                        seen.setdefault(n, p.pid)
                except (psutil.NoSuchProcess, psutil.AccessDenied):
                    continue
            now = count_windows()
            if sum(now) > sum(peak):
                peak = now

        print(f"  本次任务期间出现的进程: {seen or '（无）'}")
        print(f"  峰值窗口 WT={peak[0]} conhost={peak[1]}")
        t2 = folder.GetTask(TASK)
        print(f"  任务上次结果={t2.LastTaskResult}（0 = 成功）")

        if "cmd.exe" in seen:
            print("  ❌ 仍起了 cmd.exe —— 动作没换成功或还有别的触发源")
            return 1
        if "wscript.exe" not in seen:
            print("  ⚠️ 没看到 wscript.exe：任务可能秒退（VBS 未等待？）或触发失败")
            return 1
        print("  ✅ 没有 cmd.exe，只有 wscript（宿主无控制台）→ 零弹窗")
    return 0


if __name__ == "__main__":
    sys.exit(main())
