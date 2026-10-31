#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""agent_ui.h 拆分前的可行性实验：Q_PROPERTY 跨基类 + NOTIFY 双发。

必须先跑这个再动拆分，理由见 _agent_ui_split_probe.cpp 顶部：
若基类/派生类同名 infoChanged 只发一个，基类属性绑定会静默不刷新。

用法：python scripts/_agent_ui_split_probe.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD, "_objs")
SRC = os.path.join(ROOT, "scripts", "_agent_ui_split_probe.cpp")
HDR = os.path.join(ROOT, "scripts", "_agent_ui_split_probe.h")
MOC_OUT = os.path.join(OUT_DIR, "_agent_ui_split_probe_moc.cpp")
EXE = os.path.join(OUT_DIR, "_agent_ui_split_probe.exe")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"
MOC = QT + r"\bin\moc.exe"

DEFINES = ["-DQT_CORE_LIB", "-DQT_GUI_LIB", "-DQT_QML_LIB", "-DQT_QUICK_LIB",
           "-DQT_NO_DEBUG", "-DUNICODE", "-DWIN32", "-DWIN64", "-D_UNICODE", "-D_WIN64"]
FLAGS = ["/nologo", "/TP", "/std:c++17", "-MD", "/utf-8", "/Zc:__cplusplus",
         "/permissive-", "/EHsc"]
INCLUDES = ["/I", QT + r"\include", "/I", QT + r"\include\QtCore",
            "/I", QT + r"\include\QtGui", "/I", QT + r"\include\QtQml",
            "/I", QT + r"\include\QtQuick", "/I", OUT_DIR]
LIBS = [QT + r"\lib\Qt6Quick.lib", QT + r"\lib\Qt6Qml.lib",
        QT + r"\lib\Qt6Gui.lib", QT + r"\lib\Qt6Core.lib",
        "user32.lib", "gdi32.lib", "ole32.lib"]


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
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    print("[1/3] moc ...")
    # 类声明在 _agent_ui_split_probe.h，moc 必须对着头文件跑
    rc, out = run([MOC, HDR, "-o", MOC_OUT])
    if rc != 0:
        print(out)
        print("FAIL moc 失败")
        return 1

    print("[2/3] 编译链接 ...")
    # .lib 必须在 /link 之后（踩过：放前面会被当源文件）
    rc, out = run([CL] + FLAGS + DEFINES + INCLUDES +
                  [SRC, MOC_OUT, "/Fe:" + EXE, "/Fo:" + OUT_DIR + "\\",
                   "/link"] + LIBS)
    if rc != 0 or not os.path.isfile(EXE):
        print(out)
        print("FAIL 编译失败")
        return 1

    print("[3/3] 运行 ...")
    rc, out = run([EXE])
    for ln in out.splitlines():
        if any(k in ln for k in ("PASS", "FAIL", "[结论]", "Q1", "Q2", "Q3")):
            print("   ", ln.strip())
    if rc != 0:
        print(out)
        print("FAIL 实验未通过 rc=%d" % rc)
        return 1
    print("\nPASS 拆分方式可行")
    return 0


if __name__ == "__main__":
    sys.exit(main())
