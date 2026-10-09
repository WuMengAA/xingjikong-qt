#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""管理端增量构建（带 MSVC 环境，2026-10-08）。

为什么要有它：这个 shell 里没有 MSVC 的 INCLUDE / LIB，直接跑
    ninja -C build viewer-qt
会以 `qglobal.h(13): fatal C1083 无法打开 <type_traits>` 全线失败 ——
看着像 Qt 装坏了，其实是找不到 MSVC 的 include 目录。
同被控端的 scripts/_build_agent.py：全编与单编用**同一套编译开关**，验证口径一致。

为什么不走 build-qt-viewer.bat：那个脚本开头 `rd /s /q build` 会全量重建（约 7 分钟），
改动只有几个 .cpp 时没必要，而且会连带把 build/qml 下的运行时 QML 一起重造。

用法：
  python scripts/_build_viewer.py              # 增量编
  python scripts/_build_viewer.py --clean      # 先清目标再编（排障用）
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
NINJA = r"C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts\ninja.exe"
MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"

TARGET = "viewer-qt"


def build_env():
    env = dict(os.environ)
    env["LIB"] = ";".join([MSVC + r"\lib\x64",
                           SDK + r"\lib\10.0.26100.0\ucrt\x64",
                           SDK + r"\lib\10.0.26100.0\um\x64"])
    # ⚠️ shared 必须带上：um\windows.h 会 `#include <winapifamily.h>`，
    # 而这个文件只在 SDK 的 **shared\** 里（um\ 下没有）。
    env["INCLUDE"] = ";".join([MSVC + r"\include",
                               SDK + r"\include\10.0.26100.0\ucrt",
                               SDK + r"\include\10.0.26100.0\um",
                               SDK + r"\include\10.0.26100.0\shared",
                               SDK + r"\include\10.0.26100.0\winrt",
                               SDK + r"\include\10.0.26100.0\cppwinrt"])
    return env


def main():
    argv = [a for a in sys.argv[1:] if a != "--clean"]
    cmd = [NINJA, "-C", BUILD, "-j", "4", TARGET] + argv
    if "--clean" in sys.argv:
        cmd = [NINJA, "-C", BUILD, "-t", "clean"] + cmd[2:]
    p = subprocess.run(cmd, env=build_env())
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
