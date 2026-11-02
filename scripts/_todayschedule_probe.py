#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""今日课表三件套运行时断言（2026-11-02 · 去 QWidget 化收尾）。

复刻 _mainwindow_probe.py 的壳机制：用同一只 _theme_probe.exe（已链接 QtShadcn.lib），
跑 _todayschedule_probe.qml。QML 必须放 build/qml/Stelarith/（那里才有 panels/ + components/）。

用法：python scripts/_todayschedule_probe.py
退出码：0 全过 / 1 构建或加载失败 / 3 断言失败 / 其他 = 壳退出码
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
RUNDIR = os.path.join(BUILD, "_theme_probe_run")
EXE = os.path.join(RUNDIR, "_theme_probe.exe")

QML = os.path.join(ROOT, "scripts", "_todayschedule_probe.qml")
QML_DST = os.path.join(BUILD, "qml", "Stelarith", "_todayschedule_probe.qml")

QT = r"D:\QTqwq\6.12.0\msvc2022_64"
QTSHADCN_LIBDIR = os.path.join(BUILD, "_qtshadcn", "src")


def env():
    e = dict(os.environ)
    e["PATH"] = os.path.join(QT, "bin") + ";" + e.get("PATH", "")
    e["QT_QML_IMPORT_PATH"] = os.path.join(QT, "qml")
    e["QT_QPA_PLATFORM_PLUGIN_PATH"] = os.path.join(QT, "plugins", "platforms")
    e["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    return e


def main():
    if not os.path.isfile(EXE):
        print("缺 _theme_probe.exe —— 先跑 python scripts/_theme_probe.py")
        return 1

    os.makedirs(os.path.dirname(QML_DST), exist_ok=True)
    shutil.copy2(QML, QML_DST)

    # ⚠️ QtShadcn 是「静态链接进壳 + qmldir optional plugin」的双形态模块：
    # 壳里 Q_IMPORT_PLUGIN 提供类型注册，但 import QtShadcn 时引擎仍按 qmldir
    # 尝试加载 QtShadcnplugin.dll。_theme_probe_run/ 原本没有这份 dll ⇒
    # 「无法加载库 …找不到指定的模块」（2026-11-02 实测）。_mainwindow_probe
    # 不炸是因为它没实例化任何 import QtShadcn 的组件。把 dll 拷到 exe 旁即解。
    run_dir = os.path.dirname(EXE)
    for dll in ("QtShadcnplugin.dll", "QtShadcn.dll"):
        src = os.path.join(BUILD, "qml", "Stelarith", "QtShadcn", dll)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(run_dir, dll))
    src = os.path.join(BUILD, "QtShadcn.dll")
    if os.path.isfile(src):
        shutil.copy2(src, os.path.join(run_dir, "QtShadcn.dll"))

    e = env()
    e["QML_IMPORT_PATH"] = QTSHADCN_LIBDIR
    p = subprocess.run([EXE, "-platform", "offscreen", QML_DST],
                       env=e, cwd=ROOT, timeout=120)
    if p.returncode == 0:
        print("[probe] 全部通过")
    elif p.returncode == 3:
        print("[probe] 有断言失败")
    elif p.returncode == 1:
        print("[probe] 加载失败")
    else:
        print("[probe] exit=" + str(p.returncode))
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
