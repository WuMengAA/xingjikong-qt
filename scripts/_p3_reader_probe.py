#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""P3 记录读取「错误态」探针驱动。

守的是旧 readJsonlDir 把三种原因折叠成一个空数组的真 bug（详见 p3_reader.h 注释）。
用法：python scripts/_p3_reader_probe.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD, "_objs")
SRC = os.path.join(ROOT, "scripts", "_p3_reader_probe.cpp")
P3READER = os.path.join(ROOT, "src", "p3log", "p3_reader.cpp")
P3LOG = os.path.join(ROOT, "src", "p3log", "p3_log.cpp")
EXE = os.path.join(OUT_DIR, "_p3_reader_probe.exe")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"

DEFINES = ["-DQT_CORE_LIB", "-DQT_GUI_LIB", "-DQT_WIDGETS_LIB", "-DQT_NO_DEBUG",
           "-DUNICODE", "-DWIN32", "-DWIN64", "-D_UNICODE", "-D_WIN64"]
FLAGS = ["/nologo", "/TP", "/std:c++17", "-MD", "/utf-8", "/Zc:__cplusplus",
         "/permissive-", "/EHsc"]
INCLUDES = ["/I", QT + r"\include", "/I", QT + r"\include\QtCore",
            "/I", QT + r"\include\QtGui", "/I", QT + r"\include\QtWidgets",
            "/I", os.path.join(ROOT, "src", "p3log"), "/I", os.path.join(ROOT, "src")]
LIBS = [QT + r"\lib\Qt6Widgets.lib", QT + r"\lib\Qt6Gui.lib",
        QT + r"\lib\Qt6Core.lib", "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib"]


def env():
    e = dict(os.environ)
    e["LIB"] = ";".join([MSVC + r"\lib\x64",
                         SDK + r"\lib\10.0.26100.0\ucrt\x64",
                         SDK + r"\lib\10.0.26100.0\um\x64"])
    e["INCLUDE"] = ";".join([MSVC + r"\include",
                             SDK + r"\include\10.0.26100.0\ucrt",
                             SDK + r"\include\10.0.26100.0\um",
                             SDK + r"\include\10.0.26100.0\shared",
                             QT + r"\include"])
    e["PATH"] = QT + r"\bin;" + e.get("PATH", "")
    e["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    return e


def run(cmd):
    p = subprocess.run(cmd, env=env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode("gbk", "replace")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("[1/2] 编译探针 + p3_reader + p3_log ...")
    objs = []
    srcs = [SRC, P3READER, P3LOG]
    for s in srcs:
        o = os.path.join(OUT_DIR, os.path.basename(s) + ".p3probe.obj")
        rc, out = run([CL] + FLAGS + DEFINES + INCLUDES + ["/c", "/Fo" + o, s])
        if rc != 0:
            print(out)
            print("FAIL 编译失败：" + os.path.basename(s))
            return 1
        objs.append(o)
    # .lib 必须在 /link 之后
    rc, out = run([CL] + objs + ["/Fe:" + EXE, "/Fo:" + OUT_DIR + "\\", "/link"] + LIBS)
    if rc != 0 or not os.path.isfile(EXE):
        print(out)
        print("FAIL 链接失败")
        return 1

    print("[2/2] 运行断言 ...")
    rc, out = run([EXE])
    for ln in out.splitlines():
        if any(k in ln for k in ("PASS", "FAIL", "[结论]")):
            print("   ", ln.strip())
    if rc != 0:
        print(out)
        print("FAIL 断言失败 rc=%d" % rc)
        return 1
    print("\nPASS 记录读取诊断分类正确")
    return 0


if __name__ == "__main__":
    sys.exit(main())
