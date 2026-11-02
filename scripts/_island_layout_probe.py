#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""灵动岛组件布局模型探针（2026-11-03 · 入库，因为它守的是一条会静默漂移的契约）。

为什么要有它：`faceModel` 从 IslandRoot.qml 的写死数组搬到 C++ `IslandLayoutModel`
之后，出现了一个新的双真源风险 ——
  · C++ `builtinRows()` 是默认序；
  · `IslandRoot.qml` 里还留着一份**回落默认序**（viewer 的岛预览没有 islandLayout
    上下文属性，走的就是那份）。
两处漂移不会报错，表现只是「预览里顺序和真机不一样」——现场几乎不可能复现。
本探针的 A 断言就是把这两份逐项对账（顺序写死在这里，任一边改了就红）。

跑法（仓库根或本目录都行）：
    python Stelarith-control-qt/scripts/_island_layout_probe.py
退出码：0=全过 / 1=有 FAIL / 2=构建失败。
"""
import os
import subprocess
import sys

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD, "_island_layout_probe")
SRC = os.path.join(ROOT, "scripts", "_island_layout_probe.cpp")
MOD = os.path.join(ROOT, "src", "island", "island_layout.cpp")
HDR = os.path.join(ROOT, "src", "island", "island_layout.h")
EXE = os.path.join(OUT_DIR, "_island_layout_probe.exe")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"
MOC = QT + r"\bin\moc.exe"

FLAGS = ["/nologo", "/TP", "/std:c++17", "/permissive-", "/utf-8",
         "/Zc:__cplusplus", "/EHsc", "-D_UNICODE", "-DUNICODE", "/O1", "/MD"]
DEFINES = ["-DQT_CORE_LIB", "-DQT_NO_DEBUG", "-DWIN32", "-DWIN64"]
INCLUDES = ["/I" + MSVC + r"\include",
            "/I" + SDK + r"\include\10.0.26100.0\ucrt",
            "/I" + SDK + r"\include\10.0.26100.0\um",
            "/I" + SDK + r"\include\10.0.26100.0\shared",
            "/I" + QT + r"\include",
            "/I" + QT + r"\include\QtCore",
            "/I" + os.path.join(ROOT, "src"),
            "/I" + os.path.join(ROOT, "src", "island")]
LIBS = [QT + r"\lib\Qt6Core.lib"]


def env():
    e = dict(os.environ)
    e["LIB"] = ";".join([MSVC + r"\lib\x64",
                         SDK + r"\lib\10.0.26100.0\ucrt\x64",
                         SDK + r"\lib\10.0.26100.0\um\x64",
                         QT + r"\lib"])
    e["INCLUDE"] = ";".join([MSVC + r"\include",
                             SDK + r"\include\10.0.26100.0\ucrt",
                             SDK + r"\include\10.0.26100.0\um",
                             SDK + r"\include\10.0.26100.0\shared",
                             QT + r"\include"])
    e["PATH"] = QT + r"\bin;" + e.get("PATH", "")
    return e


def run(cmd):
    r = subprocess.run(cmd, env=env(), capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print("[FAIL] " + os.path.basename(cmd[0]))
        print((r.stdout or "")[-3000:])
        print((r.stderr or "")[-3000:])
        return False
    return True


def build():
    os.makedirs(OUT_DIR, exist_ok=True)
    moc_out = os.path.join(OUT_DIR, "moc_island_layout.cpp")
    obj_moc = os.path.join(OUT_DIR, "moc_island_layout.obj")
    obj_mod = os.path.join(OUT_DIR, "island_layout.obj")
    obj_src = os.path.join(OUT_DIR, "_island_layout_probe.obj")
    steps = [
        # Q_OBJECT 必须过 moc，否则 vtable 缺元对象符号（LNK2001）
        [MOC, HDR, "-o", moc_out],
        [CL] + FLAGS + DEFINES + INCLUDES + [moc_out, "/c", "/Fo:" + obj_moc],
        # moc 产物里 include 的是 "island_layout.h"，而 cpp 里是裸文件名 —— 两个都在 /I 里
        [CL] + FLAGS + DEFINES + INCLUDES + [MOD, "/c", "/Fo:" + obj_mod],
        [CL] + FLAGS + DEFINES + INCLUDES + [SRC, "/c", "/Fo:" + obj_src],
        [CL, "/nologo", obj_src, obj_mod, obj_moc, "/Fe:" + EXE] + LIBS,
    ]
    for cmd in steps:
        if not run(cmd):
            return False
    return True


def main():
    if not build():
        return 2
    e = env()
    e["QT_QPA_PLATFORM"] = "offscreen"
    r = subprocess.run([EXE], env=e, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=180)
    print((r.stdout or "") + (r.stderr or ""))
    print("EXITCODE=%d" % r.returncode)
    return 0 if r.returncode == 10 else 1


if __name__ == "__main__":
    sys.exit(main())
