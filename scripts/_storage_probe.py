#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""存储用量三分类探针（2026-11-02 · 不入库）。

为什么要有它：线上截图显示「配置/数据/日志」三栏都是 532.5 MB、合计 1.56 GB
（正好三倍）。这是两个叠加的错：
  ① cfgDir 与 logDir 其实都在 dataDir 这棵树里，"配置/数据/日志"不是互斥分类；
  ② 去重用 `logDir.startsWith(dataDir)`，而 dataDir 是 `/` 拼、absolutePath 给 `\`
     ⇒ 恒 false ⇒ 日志被重复计入总账。
本探针直接链 settings_domain.cpp 的真实现，在临时目录里造出可控结构，断言：
  A 三栏互斥（config+data+logs 恰等于目录真实占用）
  B 配置栏只算 dataDir 根下的文件
  C 日志栏只算 logs/ 与该级 *.log
  D dataDir("/" 拼) 与 logDir("\" 给) 混用时分隔符不影响结论（本 bug 的直接回归）
  E logDir 指向 dataDir 之外时日志仍被计入且不重复
  F 数据目录不存在时不崩、不报假数

退出码：0=全过 / 1=有 FAIL。
"""
import os
import shutil
import subprocess
import sys
import tempfile

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUT_DIR = os.path.join(BUILD, "_storage_probe")
SRC = os.path.join(ROOT, "scripts", "_storage_probe.cpp")
MOD = os.path.join(ROOT, "src", "domain", "settings_domain.cpp")
EXE = os.path.join(OUT_DIR, "_storage_probe.exe")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"

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
            "/I" + os.path.join(ROOT, "src", "domain"),
            "/I" + os.path.join(ROOT, "src", "core")]
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


def build():
    os.makedirs(OUT_DIR, exist_ok=True)
    obj_mod = os.path.join(OUT_DIR, "settings_domain.obj")
    obj_src = os.path.join(OUT_DIR, "_storage_probe.obj")
    for cmd in ([CL] + FLAGS + DEFINES + INCLUDES + ["/c", MOD, "/Fo:" + obj_mod],
                [CL] + FLAGS + DEFINES + INCLUDES + ["/c", SRC, "/Fo:" + obj_src],
                [CL, "/nologo", obj_src, obj_mod, "/Fe:" + EXE] + LIBS):
        r = subprocess.run(cmd, env=env(), capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if r.returncode != 0:
            print("[BUILD FAIL] " + " ".join(cmd[:3]))
            print(r.stdout[-3000:])
            print(r.stderr[-3000:])
            return False
    return True


def mk(path, size):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"x" * size)


def main():
    if not build():
        return 1
    e = env()
    e["QT_QPA_PLATFORM"] = "offscreen"
    r = subprocess.run([EXE], env=e, capture_output=True, text=True,
                       encoding="utf-8", errors="replace", timeout=180)
    out = (r.stdout or "") + (r.stderr or "")
    print(out)
    print("EXITCODE=%d" % r.returncode)
    return 0 if r.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
