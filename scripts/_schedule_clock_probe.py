#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""课表构造探针驱动（2026-11-03 · 不入库）。

直接链 src/schedule/schedule_clock.cpp 的真实实现，喂一份「真机形态」档案，
断言周一 12 节 / 周日 3 节 / 两天不混表 / 简化模型仍按时间排序。

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
OUT_DIR = os.path.join(BUILD, "_schedule_probe")
SRC = os.path.join(ROOT, "scripts", "_schedule_clock_probe.cpp")
CLOCK = os.path.join(ROOT, "src", "schedule", "schedule_clock.cpp")
REPO = os.path.join(ROOT, "src", "schedule", "profile_repository.cpp")
EXE = os.path.join(OUT_DIR, "_schedule_clock_probe.exe")

MSVC = r"C:\\Program Files (x86)\\Microsoft Visual Studio\\18\\BuildTools\\VC\\Tools\\MSVC\\14.51.36231"
SDK = r"C:\\Program Files (x86)\\Windows Kits\\10"
QT = r"D:\\QTqwq\\6.12.0\\msvc2022_64"
CL = MSVC + r"\\bin\\Hostx64\\x64\\cl.exe"

FLAGS = ["/nologo", "/TP", "/std:c++17", "/permissive-", "/utf-8",
         "/Zc:__cplusplus", "/EHsc", "-D_UNICODE", "-DUNICODE", "/O1", "/MD"]
DEFINES = ["-DQT_CORE_LIB", "-DQT_NO_DEBUG", "-DWIN32", "-DWIN64"]


def src_dirs():
    dirs = [os.path.join(ROOT, "src")]
    base = os.path.join(ROOT, "src")
    for name in sorted(os.listdir(base)):
        p = os.path.join(base, name)
        if os.path.isdir(p):
            dirs.append(p)
    return dirs


INCLUDES = ["/I" + MSVC + r"\\include",
            "/I" + SDK + r"\\include\\10.0.26100.0\\ucrt",
            "/I" + SDK + r"\\include\\10.0.26100.0\\um",
            "/I" + SDK + r"\\include\\10.0.26100.0\\shared",
            "/I" + QT + r"\\include",
            "/I" + QT + r"\\include\\QtCore"] + ["/I" + d for d in src_dirs()]

LIBS = [QT + r"\\lib\\Qt6Core.lib"]


def env():
    e = dict(os.environ)
    e["LIB"] = ";".join([MSVC + r"\\lib\\x64",
                         SDK + r"\\lib\\10.0.26100.0\\ucrt\\x64",
                         SDK + r"\\lib\\10.0.26100.0\\um\\x64",
                         QT + r"\\lib"])
    e["PATH"] = QT + r"\\bin;" + MSVC + r"\\bin\\Hostx64\\x64;" + e.get("PATH", "")
    e["PYTHONIOENCODING"] = "utf-8"
    return e


def build():
    os.makedirs(OUT_DIR, exist_ok=True)
    # schedule_clock.cpp 有 Q_OBJECT，需要 moc
    mo = os.path.join(OUT_DIR, "moc_schedule_clock.cpp")
    moc = os.path.join(QT, "bin", "moc.exe")
    r = subprocess.run([moc, os.path.join(ROOT, "src", "schedule", "schedule_clock.h"),
                        "-o", mo],
                       capture_output=True, text=True, encoding="utf-8", errors="replace",
                       env=env())
    if r.returncode != 0:
        print("moc 失败：", r.stdout, r.stderr)
        return False
    objs = []
    hdr = os.path.join(ROOT, "src", "schedule", "schedule_clock.h")
    for i, src in enumerate([SRC, CLOCK, REPO, mo]):
        obj = os.path.join(OUT_DIR, "p%d.obj" % i)
        # moc 产物不自己 #include 头文件（它只声明生成 QMetaObject 代码），
        # 单独编译就得强制包含，否则 ScheduleClock::LessonInfo 这类**嵌套类型**全部未定义。
        extra = ["/FI" + hdr] if src == mo else []
        cmd = [CL] + FLAGS + DEFINES + INCLUDES + extra + ["/c", src, "/Fo" + obj]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace", env=env())
        if r.returncode != 0:
            print("编译失败：", src)
            print(r.stdout[-4000:])
            print(r.stderr[-4000:])
            return False
        objs.append(obj)
    link = [CL] + objs + ["/Fe" + EXE] + LIBS + ["/link", "/SUBSYSTEM:CONSOLE"]
    r = subprocess.run(link, capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=env())
    if r.returncode != 0:
        print("链接失败：")
        print(r.stdout[-4000:])
        return False
    return True


def main():
    if not build():
        print("PROBE BUILD FAILED")
        return 2
    e = env()
    e["QT_QPA_PLATFORM"] = "offscreen"
    argv = [EXE]
    # 可选：--real <档案路径>（真机落盘档案逐日核对）
    if "--real" in sys.argv:
        i = sys.argv.index("--real")
        argv += ["--real", sys.argv[i + 1]]
    r = subprocess.run(argv, capture_output=True, text=True, encoding="utf-8",
                       errors="replace", env=e, cwd=OUT_DIR)
    print(r.stdout)
    if r.stderr.strip():
        print("--- stderr ---")
        print(r.stderr[-2000:])
    code = r.returncode
    print("PROBE EXIT=%d" % code)
    return 0 if code == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
