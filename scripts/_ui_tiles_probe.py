#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""三件新组件（AppErrorState / AppSkeleton / AppStatTile）的真实渲染探针。

为什么要有它：qmllint 对「给组件传了不存在的属性」「Layout 用法错」报 0 Error，
运行时却是整页不加载（AGENTS.md §9.6：TopBar 收了不存在的属性，qmllint 全绿、页面崩）。
本脚本编译一个最小 QGuiApplication host，加载 _ui_tiles_probe.qml 并离屏 grabWindow，
据此回答：**这三件到底能不能真的画出来**，以及画出来长什么样。

用法：python scripts/_ui_tiles_probe.py
退出：0 全过 / 1 有失败
"""
import io
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = os.path.dirname(ROOT)                      # monorepo 根
BUILD = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD, "_objs")
PROBE_SRC = os.path.join(ROOT, "scripts", "_ui_tiles_probe.cpp")
PROBE_QML = os.path.join(ROOT, "scripts", "_ui_tiles_probe.qml")
EXE = os.path.join(OUT_DIR, "_ui_tiles_probe.exe")
SHOT = os.path.join(OUT_DIR, "_ui_tiles_probe.png")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"

DEFINES = ["-DQT_CORE_LIB", "-DQT_GUI_LIB", "-DQT_QML_LIB", "-DQT_QUICK_LIB",
           "-DQT_NO_DEBUG", "-DUNICODE", "-DWIN32", "-DWIN64",
           "-D_ENABLE_EXTENDED_ALIGNED_STORAGE", "-D_UNICODE", "-D_WIN64"]
FLAGS = ["/nologo", "/TP", "/std:c++17", "-MD", "/utf-8", "/Zc:__cplusplus",
         "/permissive-", "/EHsc", "/W3"]
INCLUDES = ["/I", QT + r"\include", "/I", QT + r"\include\QtCore",
            "/I", QT + r"\include\QtGui", "/I", QT + r"\include\QtQml",
            "/I", QT + r"\include\QtQuick", "/I", QT + r"\include\QtQuickControls2"]
LIBS = [QT + r"\lib\Qt6Quick.lib", QT + r"\lib\Qt6Qml.lib",
        QT + r"\lib\Qt6QuickControls2.lib",
        QT + r"\lib\Qt6Gui.lib", QT + r"\lib\Qt6Core.lib",
        "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib"]


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
    e["QT_QUICK_CONTROLS_STYLE"] = "Basic"   # 项目铁律：非 Basic 会让 App* 的覆写静默失效
    return e


def run(cmd):
    p = subprocess.run(cmd, env=env(), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    txt = p.stdout.decode("gbk", "replace")
    return p.returncode, txt


def main():
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)

    print("[1/3] 编译探针 host ...")
    # ⚠️ .lib 必须放在 /link 之后：放前面 cl 会把它们当源文件解析
    #    （实测报 "C2018 不允许使用字符 U+a8" + "C1083 无法打开源文件 user32.lib"）
    rc, out = run([CL] + FLAGS + DEFINES + INCLUDES +
                  [PROBE_SRC, "/Fe:" + EXE, "/Fo:" + OUT_DIR + "\\",
                   "/link"] + LIBS)
    if rc != 0 or not os.path.isfile(EXE):
        print(out)
        print("FAIL 编译失败")
        return 1
    print("      ok ->", EXE)

    print("[2/3] 离屏渲染 ...")
    if os.path.isfile(SHOT):
        os.remove(SHOT)
    # 探针 QML 里 import "../ui" —— 但 QML 在 scripts/ 下，../ui 指向 control-qt/ui（不存在）。
    # 所以把探针 QML 的 import 路径改为绝对：这里用 QML_IMPORT_PATH 提供 shared-qml 根，
    # 并让 .qml 内改用相对 scripts 的位置（见 _ui_tiles_probe.qml 顶部说明）。
    e = env()
    e["QML_IMPORT_PATH"] = os.path.join(BASE, "Stelarith-shared-qml")
    p = subprocess.run([EXE, PROBE_QML, SHOT], env=e,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode("gbk", "replace")
    for ln in out.splitlines():
        if any(k in ln for k in ("[probe]", "Error", "error", "FAILED", "警告")):
            print("     ", ln)
    if p.returncode != 0:
        print(out)
        print("FAIL 渲染失败 rc=%d" % p.returncode)
        return 1

    print("[3/3] 校验输出 ...")
    if not os.path.isfile(SHOT):
        print("FAIL 没有产出截图")
        return 1
    size = os.path.getsize(SHOT)
    print("      截图 %s (%.1f KB)" % (SHOT, size / 1024.0))
    if size < 3000:
        print("FAIL 截图过小，疑似空白")
        return 1
    print("PASS 三件组件渲染通过")
    return 0


if __name__ == "__main__":
    sys.exit(main())
