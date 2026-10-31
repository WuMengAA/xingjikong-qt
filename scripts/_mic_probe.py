#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""麦克风真机实验驱动：编译 _mic_probe.cpp + microphone.cpp，跑真实音频设备。

用法：python scripts/_mic_probe.py
需要有真实录音设备（本机 waveIn 有 3 个）。
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD, "_objs")
SRC = os.path.join(ROOT, "scripts", "_mic_probe.cpp")
MIC = os.path.join(ROOT, "src", "microphone.cpp")
MIC_H = os.path.join(ROOT, "src", "microphone.h")
MOC_OUT = os.path.join(OUT_DIR, "_mic_probe_moc.cpp")
EXE = os.path.join(OUT_DIR, "_mic_probe.exe")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
MOC = os.path.join(QT, "bin", "moc.exe")
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"

DEFINES = ["-DQT_CORE_LIB", "-DQT_NO_DEBUG", "-DUNICODE", "-DWIN32", "-DWIN64",
           "-D_UNICODE", "-D_WIN64"]
FLAGS = ["/nologo", "/TP", "/std:c++17", "-MD", "/utf-8", "/Zc:__cplusplus",
         "/permissive-", "/EHsc"]
INCLUDES = ["/I", QT + r"\include", "/I", QT + r"\include\QtCore",
            "/I", os.path.join(ROOT, "src")]
LIBS = [QT + r"\lib\Qt6Core.lib", "winmm.lib", "user32.lib", "ole32.lib"]


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
    return e


def run(cmd):
    p = subprocess.run(cmd, env=env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return p.returncode, p.stdout.decode("gbk", "replace")


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("[1/2] 编译麦克风探针 ...")
    # Microphone 是有 Q_OBJECT 的类 ⇒ 必须 moc 它的头，否则链接期找不到
    # staticMetaObject / pcmReady / hardStop（实测踩过，同 agent_record_ui.h 的坑）
    rc, out = run([MOC, MIC_H, "-o", MOC_OUT])
    if rc != 0:
        print(out); print("FAIL moc 失败"); return 1
    objs = []
    for s in (SRC, MIC, MOC_OUT):
        o = os.path.join(OUT_DIR, os.path.basename(s) + ".micprobe.obj")
        rc, out = run([CL] + FLAGS + DEFINES + INCLUDES + ["/c", "/Fo" + o, s])
        if rc != 0:
            print(out)
            print("FAIL 编译失败：" + os.path.basename(s))
            return 1
        objs.append(o)
    rc, out = run([CL] + objs + ["/Fe:" + EXE, "/Fo:" + OUT_DIR + "\\", "/link"] + LIBS)
    if rc != 0 or not os.path.isfile(EXE):
        print(out)
        print("FAIL 链接失败")
        return 1

    print("[2/2] 对真实音频设备跑实验 ...")
    rc, out = run([EXE])
    for ln in out.splitlines():
        s = ln.strip()
        if s.startswith(("PASS", "FAIL", "SKIP", "waveIn", "===", "[结论]")):
            print("   ", s)
    if rc != 0:
        print(out)
        print("FAIL 有断言失败 rc=%d" % rc)
        return 1
    print("\nPASS 麦克风行为符合预期")
    return 0


if __name__ == "__main__":
    sys.exit(main())
