#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""班级域探针：构建 + 离屏运行（#460 · 2026-10-23 · viewer ClassService 数据面取证）。

与 scripts/_interact_probe.py 的分工：那个是 QML 级探针（真 QML 引擎 + 场景截图），
这个只测 C++ 数据面（ClassService + typed Models + 缩略图 provider），不起 QML
引擎，编译链接轻一个量级。

三个实现要点：
  1. moc 复用主构建产物（build/viewer-qt_autogen/<hash>/moc_class_*.cpp）——
     自己跑 moc 等于第二套口径，冒"探针元对象与主构建不一致"的鬼。头文件比
     moc 新时直接报错退出（防止探到陈旧元对象，那种失败现象极诡异）。
  2. 只链 src/class/ 四个 TU + 探针自己：viewerbackend 不链（class_service.cpp
     只引用一个 logf 符号，探针 cpp 自带桩）—— 模块独立性正是 #460 的设计目标，
     探针把它当成验收项。
  3. JPEG 靠 Qt imageformats 插件：QT_PLUGIN_PATH 指安装目录 + offscreen 平台，
     与 agent 缩略图探针同款环境。

前提：先跑过 python scripts/_build_viewer.py。
用法：python scripts/_class_probe.py
"""
import os
import glob
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OBJDIR = os.path.join(BUILD, "_class_objs")
RUNDIR = os.path.join(BUILD, "_class_run")
PROBE_SRC = os.path.join(ROOT, "scripts", "_class_probe.cpp")
CLASS_SRC = os.path.join(ROOT, "src", "class")
AUTOGEN_GLOB = os.path.join(BUILD, "viewer-qt_autogen", "*", "moc_class_service.cpp")

QT = r"D:\QTqwq\6.12.0\msvc2022_64"
MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
SDKVER = "10.0.26100.0"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"
LINK = MSVC + r"\bin\Hostx64\x64\link.exe"

INCS = []
for sub in ["", r"\QtCore", r"\QtGui", r"\QtQml", r"\QtQuick", r"\QtQmlMeta",
            r"\QtQmlModels", r"\QtNetwork"]:
    INCS += ["/I", QT + r"\include" + sub]
INCS += ["/I", os.path.join(ROOT, "src")]

FLAGS = ["/nologo", "/TP", "/utf-8", "/Zc:__cplusplus", "/permissive-", "-MD",
         "/std:c++17", "/EHsc"] + INCS

LIBS = ["qt6quick.lib", "qt6qml.lib", "qt6qmlmeta.lib", "qt6qmlmodels.lib",
        "qt6gui.lib", "qt6network.lib", "qt6core.lib",
        "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "winmm.lib"]

RUNTIME_DLLS = [
    "Qt6Core.dll", "Qt6Gui.dll", "Qt6Qml.dll", "Qt6QmlMeta.dll", "Qt6QmlModels.dll",
    "Qt6Quick.dll", "Qt6Network.dll", "Qt6OpenGL.dll",
]
PLUGIN_DIRS = ["platforms", "imageformats"]


def env():
    e = dict(os.environ)
    e["LIB"] = ";".join([MSVC + r"\lib\x64",
                         SDK + r"\lib" + "\\" + SDKVER + r"\ucrt\x64",
                         SDK + r"\lib" + "\\" + SDKVER + r"\um\x64"])
    # MSVC 标准库头（type_traits 之流）只认 INCLUDE 环境变量 —— /I 只补 Qt 与
    # src；漏了它就是一堆 C1083（agent 缩略图探针那轮同坑，别再踩第三次）。
    e["INCLUDE"] = ";".join([MSVC + r"\include",
                             SDK + r"\include" + "\\" + SDKVER + r"\ucrt",
                             SDK + r"\include" + "\\" + SDKVER + r"\um",
                             SDK + r"\include" + "\\" + SDKVER + r"\shared"])
    e["PATH"] = QT + r"\bin;" + MSVC + r"\bin\Hostx64\x64;" + e.get("PATH", "")
    return e


def run(cmd, cwd=None, enc="gbk"):
    # enc：cl/link 的告警是 GBK；探针 exe 的输出是 UTF-8（/utf-8 编的窄串）。
    p = subprocess.run(cmd, env=env(), cwd=cwd,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    txt = p.stdout.decode(enc, "replace")
    for ln in txt.splitlines():
        low = ln.lower()
        if "error" in low or "fatal" in low or "cannot open" in low:
            print("  ", ln)
    return p.returncode, txt


def find_moc_dir():
    """取含 moc_class_*.cpp 的最新 autogen 目录；并校验不比头文件旧。"""
    cands = [os.path.dirname(p) for p in glob.glob(AUTOGEN_GLOB)]
    if not cands:
        print("MISSING moc 产物 -> 先跑 python scripts/_build_viewer.py")
        return None
    d = max(cands, key=os.path.getmtime)
    # 只对含 Q_OBJECT 的头要 moc（class_thumb_provider.h 是纯虚图像提供器，
    # 不是 QObject 子类，moc 不产物——别按"每个 .h 一个 moc"想当然）。
    for h in glob.glob(os.path.join(CLASS_SRC, "*.h")):
        with open(h, "r", encoding="utf-8", errors="replace") as f:
            if "Q_OBJECT" not in f.read():
                continue
        moc = os.path.join(d, "moc_" + os.path.basename(h).replace(".h", ".cpp"))
        if not os.path.isfile(moc):
            print("MISSING moc:", moc, "-> 先跑 python scripts/_build_viewer.py")
            return None
        if os.path.getmtime(moc) < os.path.getmtime(h):
            print("STALE moc：%s 比 %s 旧 -> 先重跑 python scripts/_build_viewer.py"
                  % (moc, h))
            return None
    return d


def stage():
    os.makedirs(RUNDIR, exist_ok=True)
    src_bin = os.path.join(QT, "bin")
    for dll in RUNTIME_DLLS:
        s = os.path.join(src_bin, dll)
        d = os.path.join(RUNDIR, dll)
        if not os.path.exists(s):
            print("MISSING QT DLL:", s)
            return False
        if not os.path.exists(d) or os.path.getsize(s) != os.path.getsize(d):
            shutil.copy2(s, d)
    src_plugins = os.path.join(QT, "plugins")
    for sub in PLUGIN_DIRS:
        s_dir = os.path.join(src_plugins, sub)
        if not os.path.isdir(s_dir):
            continue
        d_dir = os.path.join(RUNDIR, sub)
        os.makedirs(d_dir, exist_ok=True)
        for fn in os.listdir(s_dir):
            if not fn.endswith(".dll"):
                continue
            s = os.path.join(s_dir, fn)
            d = os.path.join(d_dir, fn)
            if not os.path.exists(d) or os.path.getsize(s) != os.path.getsize(d):
                shutil.copy2(s, d)
    return True


def main():
    os.makedirs(OBJDIR, exist_ok=True)
    # RUNDIR 必须在 link **之前**建好：link.exe 写一个不存在的目录报的是
    # LNK1104「无法打开文件」（现象像被占用，极误导）——agent 探针那轮同坑。
    os.makedirs(RUNDIR, exist_ok=True)
    moc_dir = find_moc_dir()
    if not moc_dir:
        return 2

    # 编译清单：探针 + class 全部 TU + 每个 Q_OBJECT 头对应的 moc
    # （moc 清单按"头文件里有没有 Q_OBJECT"推导，别再写死 —— 加了 class_admin
    #   之后写死清单会漏 moc，症状是 link 期 undefined vtable，极难看出根因）
    srcs = [PROBE_SRC]
    # class_admin*.cpp 排除在外：它们回调 ViewerBackend::httpBaseUrl / viewerToken
    # （网络写操作层），而本探针刻意不链 viewerbackend —— 模块独立性本身就是 #460 的验收项。
    # 2026-10-25：class_admin.cpp 拆出 class_admin_import.cpp，排除规则改成前缀匹配，
    # 否则新 TU 会被 glob 进来、链接期报一堆 ViewerBackend 未定义（现象极像真 bug）。
    srcs += [p for p in sorted(glob.glob(os.path.join(CLASS_SRC, "*.cpp")))
             if not os.path.basename(p).startswith("class_admin")]
    for h in sorted(glob.glob(os.path.join(CLASS_SRC, "*.h"))):
        base = os.path.basename(h)[:-2]
        if base == "class_admin":        # 与上面同一理由：不链它的 TU，也就不要它的 moc
            continue
        with open(h, "r", encoding="utf-8", errors="replace") as f:
            if "Q_OBJECT" not in f.read():
                continue
        srcs.append(os.path.join(moc_dir, "moc_" + base + ".cpp"))

    objs = []
    for s in srcs:
        obj = os.path.join(OBJDIR, os.path.splitext(os.path.basename(s))[0] + ".obj")
        objs.append(obj)
        if os.path.exists(obj) and os.path.getmtime(obj) >= os.path.getmtime(s):
            continue
        rc, _ = run([CL] + FLAGS + ["/Fo" + obj, "/c", s])
        if rc:
            print("compile FAILED:", s)
            return 2

    exe = os.path.join(RUNDIR, "_class_probe.exe")
    rc, _ = run([LINK, "/nologo", "/SUBSYSTEM:CONSOLE", "/LIBPATH:" + QT + r"\lib"] +
                LIBS + ["/OUT:" + exe] + objs)
    if rc:
        print("link FAILED")
        return 2

    if not stage():
        print("STAGE FAILED")
        return 2

    penv = env()
    penv["QT_PLUGIN_PATH"] = QT + r"\plugins"
    penv["QT_QPA_PLATFORM"] = "offscreen"

    p = subprocess.run([exe], cwd=RUNDIR, env=penv,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    txt = p.stdout.decode("utf-8", "replace")
    for ln in txt.splitlines():
        if ln.strip():
            print(" ", ln)
    print("PROBE EXIT=%d" % p.returncode)
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
