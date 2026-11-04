#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""课表网格坐标探针（2026-11-04）—— 验 row 是「本天第几节」不是全局时间点序号。

背景：生产档案有 3 张 TimeLayout（周一~四 12 节 / 周五 8 节 / 周日 3 节）。
旧实现把三张表的上课段全展开成 23 行全局表 ⇒ 周一~四落偶数行、周五落奇数行，
每列一半行是空洞，行头时间在两表之间反复跳。这正是「课表顺序节次有误」。

自己编一只只链 ScheduleModel 的控制台壳（不需要 Qt Quick），拿生产档案实跑断言。
编译分三步（probe.cpp / moc_schedule_model.cpp 各自 /c 再 link）——
一把梭会把 moc 生成的 staticMetaObject 编出错（C2737），分开就没这问题。

退出码：10 全过 / 20 有断言失败 / 其它=构建失败。
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT = os.path.join(BUILD, "_sched_grid_probe")
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
VSCFG = os.path.join(BUILD, "CMakeFiles", "viewer-qt.dir")

MSVC_BIN = os.path.join(MSVC, "bin", "Hostx64", "x64")
SDK_BIN = os.path.join(SDK, "bin", "10.0.26100.0", "x64")
CL = os.path.join(MSVC_BIN, "cl.exe")
LINK = os.path.join(MSVC_BIN, "link.exe")

# 生产档案（本机 ClassIsland 真实档案优先，退到我们下发那份）
CANDIDATES = [
    r"C:\Users\Administrator\AppData\Local\ClassIsland\data\Profiles\Default.json",
    r"D:\Classlsland\data\Profiles\Default.json",
]


def msvc_env():
    """这个 shell 没有 MSVC 的 INCLUDE/LIB —— 不设就 fatal C1083 找不到 type_traits。"""
    e = dict(os.environ)
    e["LIB"] = ";".join([
        os.path.join(MSVC, "lib", "x64"),
        os.path.join(SDK, "lib", "10.0.26100.0", "ucrt", "x64"),
        os.path.join(SDK, "lib", "10.0.26100.0", "um", "x64"),
    ])
    e["INCLUDE"] = ";".join([
        os.path.join(MSVC, "include"),
        os.path.join(SDK, "include", "10.0.26100.0", "ucrt"),
        os.path.join(SDK, "include", "10.0.26100.0", "um"),
        os.path.join(SDK, "include", "10.0.26100.0", "shared"),
        os.path.join(SDK, "include", "10.0.26100.0", "winrt"),
        os.path.join(SDK, "include", "10.0.26100.0", "cppwinrt"),
    ])
    e["PATH"] = ";".join([MSVC_BIN, SDK_BIN, e.get("PATH", "")])
    return e


CFLAGS = [
    "/nologo", "/c", "/std:c++17", "/Zc:__cplusplus", "/EHsc", "/MD", "/O2", "/utf-8",
    "/permissive-", "/Zm1200", "/bigobj",
    "/DUNICODE", "/D_UNICODE", "/DQT_CORE_LIB", "/DQT_GUI_LIB", "/DQT_NO_DEBUG",
]
INCS = [
    "/I", os.path.join(ROOT, "src", "schedule"),
    "/I", os.path.join(ROOT, "src"),
    "/I", os.path.join(QT, "include"),
    "/I", os.path.join(QT, "include", "QtCore"),
    "/I", os.path.join(QT, "include", "QtGui"),
]


def obj(name):
    hits = glob.glob(os.path.join(VSCFG, "src", "**", name), recursive=True)
    if not hits:
        sys.exit("缺 obj: " + name + "（先构建 viewer）")
    return hits[0]


def run(cmd, env):
    r = subprocess.run(cmd, cwd=OUT, capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
    if r.returncode != 0:
        print("CMD FAILED:", " ".join(cmd[:4]))
        print(r.stdout[-2500:])
        print(r.stderr[-1500:])
    return r.returncode


def main():
    os.makedirs(OUT, exist_ok=True)
    profile = next((p for p in CANDIDATES if os.path.isfile(p)), None)
    if not profile:
        print("找不到档案，退出")
        return 1

    env = msvc_env()

    # ① 编探针壳
    if run([CL] + CFLAGS + INCS + [os.path.join(ROOT, "scripts", "_sched_grid_probe.cpp"),
                                   "/Fo:" + os.path.join(OUT, "_sched_grid_probe.obj")], env):
        return 1

    # ② 编 moc（单独一步，避免 staticMetaObject 的 C2737）
    moc = glob.glob(os.path.join(BUILD, "**", "moc_schedule_model.cpp"), recursive=True)
    moc_obj = os.path.join(OUT, "moc_schedule_model.obj")
    if moc:
        if run([CL] + CFLAGS + INCS + [moc[0], "/Fo:" + moc_obj], env):
            return 1
    else:
        moc_obj = None

    # ③ link
    exe = os.path.join(OUT, "_sched_grid_probe.exe")
    objs = [os.path.join(OUT, "_sched_grid_probe.obj"), obj("schedule_model.cpp.obj")]
    if moc_obj:
        objs.append(moc_obj)
    if run([LINK, "/NOLOGO", "/SUBSYSTEM:CONSOLE", "/OUT:" + exe]
           + objs
           + ["/LIBPATH:" + os.path.join(QT, "lib"), "Qt6Core.lib", "Qt6Gui.lib"], env):
        return 1

    env["PATH"] = os.path.join(QT, "bin") + ";" + env.get("PATH", "")
    p = subprocess.run([exe, profile], env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    print(p.stdout)
    if p.stderr.strip():
        print(p.stderr[-2000:])
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
