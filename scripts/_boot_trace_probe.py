#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""启动耗时探针（2026-11-03 · 用户工作笔记序号 3「启动慢」）。

## 这个探针回答什么问题

用户报「启动慢」。启动链是**同步长链**（QApplication → 域 init → 托盘 → 灵动岛 → 面板懒建），
不量就动手等于猜。本探针用 `BootTrace::mark` 打出的 `[boot]` 行，区分两类完全不同的慢：

  1. **进程起来到托盘显形**（用户看着桌面等）—— 实测 ~107ms，不慢；
  2. **点开控制面板那一下**（用户点完等白屏）—— 实测 1660ms，这才是"慢"。

## 三种模式

| 模式 | 命令 | 说明 |
|---|---|---|
| `boot`    | `--mode boot`    | 只启动，量启动链各段 |
| `panel`   | `--mode panel`   | 启动 + 6s 后自动开面板，量**预热后**首开 |
| `noprewarm` | `--mode noprewarm` | 同 panel，但 `STE_QT_PANEL_PREWARM=0`，量**未预热**首开（反向验证） |

`panel` 与 `noprewarm` 的差值就是预热的收益。2026-11-03 实测：1660ms → 221ms。

## 隔离纪律（为什么这么起实例）

- `LOCALAPPDATA` 指到临时目录 ⇒ 数据目录/日志/QSettings 全部隔离，**绝不碰生产**
  （生产 `%LOCALAPPDATA%\\xingjikong\\agent-qt.log` 是现场排查的第一手证据，覆盖掉它等于毁证）。
  数据目录内容从生产**复制**一份（`copytree`），这样量到的是真实体量（课表/插件/配置齐全），
  而不是空目录下的理想值 —— 空目录测出来的启动时间没有参考意义。
- `STE_ALLOW_MULTI=1` ⇒ 跳过单实例守卫（生产实例正在跑，否则新实例直接退出、量不到任何东西）。
- `STE_QT_WS_URL` 指到 `127.0.0.1:9`（discard 端口）⇒ 连不上云端，不产生真实流量。
- `STE_QT_OTA_AUTO=0` ⇒ 不自动装更新（探针不许改变本机状态）。

## 退出码

  0 = 全部关键阶段都打到了标记；3 = 缺阶段（构建产物过期/崩溃/被单例拦下）。

用法：
    python Stelarith-control-qt/scripts/_boot_trace_probe.py --mode panel
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
EXE = os.path.join(PROJ, "build", "stelarith-agent-qt.exe")

# 启动链上必须出现的阶段（顺序即执行顺序）。缺任何一个都说明那一段没跑到。
EXPECTED_BOOT = [
    "env+class-identity",
    "domains-sched-notify-timesync",
    "tray-show",
    "island-idle",
]

# 数据目录里**不要**复制的：日志（会被新实例写爆）、截图/媒体/收件箱（体积大且与启动无关）
SKIP = ("agent-qt.log*", "agent-qt-deploy.log", "shots", "media", "incoming")


def build_env(tmp: str, mode: str) -> dict:
    env = dict(os.environ)
    env["LOCALAPPDATA"] = tmp
    env["STE_ALLOW_MULTI"] = "1"                   # 生产实例在跑，必须跳过单例守卫
    env["STE_QT_UID"] = "probe-boot"
    env["STE_QT_WS_URL"] = "ws://127.0.0.1:9/ws/agent"
    env["STE_QT_WS_TOKEN"] = "probe"
    env["STE_QT_OTA_AUTO"] = "0"
    if mode in ("panel", "noprewarm"):
        env["STE_QT_SHELL_AUTOOPEN"] = "1"
        # 必须排在预热之后（预热 1.5s），否则量到的仍是未预热首开 —— 首版踩过：
        # AUTOOPEN 默认 1.5s 与预热同时触发，读数看起来"预热无效"，纯属测量顺序问题。
        env["STE_QT_SHELL_AUTOOPEN_MS"] = "6000"
    if mode == "noprewarm":
        env["STE_QT_PANEL_PREWARM"] = "0"          # 反向验证：关掉预热
    return env


def run(mode: str, seconds: float) -> int:
    if not os.path.exists(EXE):
        print("✗ 找不到 %s（先跑 _build_agent.py）" % EXE)
        return 3

    real = os.path.join(os.environ.get("LOCALAPPDATA", ""), "xingjikong")
    tmp = tempfile.mkdtemp(prefix="boot-trace-")
    if os.path.isdir(real):
        shutil.copytree(real, os.path.join(tmp, "xingjikong"),
                        ignore=shutil.ignore_patterns(*SKIP))
        note = "（数据目录已从生产复制：真实体量）"
    else:
        os.makedirs(os.path.join(tmp, "xingjikong"), exist_ok=True)
        note = "（生产数据目录不存在，用空目录：读数偏乐观）"

    env = build_env(tmp, mode)
    t0 = time.time()
    p = subprocess.Popen([EXE], cwd=os.path.dirname(EXE), env=env,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines: list[tuple[float, str]] = []

    def reader():
        assert p.stdout is not None
        for raw in p.stdout:
            lines.append((time.time() - t0, raw.decode("utf-8", "replace").rstrip()))

    threading.Thread(target=reader, daemon=True).start()
    time.sleep(seconds)
    p.terminate()
    try:
        p.wait(8)
    except Exception:
        p.kill()

    print("=== 模式 %s %s ===" % (mode, note))
    print("%10s  %s" % ("墙钟", "日志"))
    for t, s in lines:
        if "[boot]" in s or "[agent-shell]" in s:
            print("%8.0f ms  %s" % (t * 1000, s))

    got = {k: None for k in EXPECTED_BOOT}
    panel_open = panel_prewarm = None
    for t, s in lines:
        if "[boot]" not in s:
            continue
        for k in EXPECTED_BOOT:
            if k in s and got[k] is None:
                got[k] = t * 1000
        if "panel-open" in s:
            panel_open = t * 1000
        if "panel-prewarm" in s:
            panel_prewarm = t * 1000

    print("")
    missing = [k for k, v in got.items() if v is None]
    if missing:
        print("✗ 缺阶段标记：%s（构建产物过期？被单例拦下？）" % ", ".join(missing))
        return 3

    print("✓ 启动链完整：托盘 %.0f ms / 岛 %.0f ms"
          % (got["tray-show"], got["island-idle"]))
    if mode in ("panel", "noprewarm"):
        if panel_open is None:
            print("✗ 面板没打开（AUTOOPEN 未生效？）")
            return 3
        # ⚠️ 这里打的是**墙钟时刻**（进程启动后第几秒打开的面板），不是耗时。
        #    真正的耗时看上面日志行里的「构造+解析 N / 显示 M」——
        #    那两个数才是用户感知的白屏时长。用墙钟冒充耗时会误导判读（首版就写错过）。
        print("  面板于墙钟 %.0f ms 打开%s —— 首开耗时看上行「构造+解析/显示」"
              % (panel_open, "（预热于 %.0f ms）" % panel_prewarm if panel_prewarm
                 else "（**未预热**）"))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="启动耗时探针（BootTrace 埋点判读）")
    ap.add_argument("--mode", choices=("boot", "panel", "noprewarm"), default="panel")
    ap.add_argument("--seconds", type=float, default=0, help="观察时长（默认按模式给）")
    a = ap.parse_args()
    secs = a.seconds or {"boot": 10, "panel": 16, "noprewarm": 16}[a.mode]
    return run(a.mode, secs)


if __name__ == "__main__":
    sys.exit(main())
