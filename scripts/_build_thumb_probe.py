#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""缩略图上报探针（#459）独立编译 + 运行。

为什么单独一个脚本：ThumbnailReporter 的闸门（就绪/黑帧/降质/预算）必须
离线可验，但 agent 本体是 GUI 应用（WinMain + QML + 单实例守卫），没法在
离屏自检里单独驱动它的 tick。于是照 tests/island_demo.cpp 的范式编一个
console 小程序：注入合成帧 + 记录式 sender，直接驱动 tick()。

编译纪律同 scripts/_c1obj.py：**不改任何编译开关** —— 从
`ninja -t commands` 抓目标文件的真实 cl/link 命令行，只替换源文件名、
/Fo 输出与 obj 列表。/utf-8、/permissive-、-DQT_*_LIB、/external:I 一个
不补不删，保证与真实构建口径一致（漏 /utf-8 ⇒ MSVC 按 GBK 解中文注释、
行号错位，看着像代码错，其实是开关没开——踩过）。

用法：
  python scripts/_build_thumb_probe.py          # 编译 + 跑
  python scripts/_build_thumb_probe.py --no-run # 只编译
退出码：探针自身退出码（0=全过）；编译/链接失败返回 2。
"""
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
OUTDIR = os.path.join(BUILD, "_thumbprobe")
NINJA = r"C:\Users\Administrator\.workbuddy\binaries\python\envs\default\Scripts\ninja.exe"
MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"

# 探针 TU：probe 本体 + 被测模块 + 默认帧源依赖的两个编译单元（链接符号需要，
# 探针不调默认源，但 ThumbnailReporter.cpp 引用了 ScreenCapture/pickTargetDisplay）
SRCS = [
    "tests/thumbnail_probe.cpp",
    "src/media/ThumbnailReporter.cpp",
    "src/screen_capture.cpp",
    "src/screen_capture_dxgi.cpp",
]
TEMPLATE = "src/media/ThumbnailReporter.cpp"   # 编译命令模板源（第一个真实 TU）


def tgt_of(rel):
    """src/foo.cpp -> CMakeFiles/stelarith-agent-qt.dir/src/foo.cpp.obj（ninja target 名）"""
    return "CMakeFiles/stelarith-agent-qt.dir/%s.obj" % rel.replace("/", "\\")


def ninja_commands(tgt):
    p = subprocess.run([NINJA, "-C", BUILD, "-t", "commands", tgt],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace") if isinstance(p.stdout, bytes) else str(p.stdout)
    return [ln.rstrip() for ln in out.splitlines() if "cl.exe" in ln or "link.exe" in ln]


def build_env():
    """cl/link 都要的 MSVC 环境（_build_agent.py 同款，别只补 LIB——
    cl 的 /external:I 只有 Qt 路径，MSVC 自己的 include（type_traits 等）
    全靠环境 INCLUDE；漏了就是 C1083 全线假故障）。"""
    env = dict(os.environ)
    env["LIB"] = ";".join([MSVC + r"\lib\x64",
                           SDK + r"\lib\10.0.26100.0\ucrt\x64",
                           SDK + r"\lib\10.0.26100.0\um\x64"])
    env["INCLUDE"] = ";".join([MSVC + r"\include",
                               SDK + r"\include\10.0.26100.0\ucrt",
                               SDK + r"\include\10.0.26100.0\um",
                               SDK + r"\include\10.0.26100.0\shared",
                               SDK + r"\include\10.0.26100.0\winrt",
                               SDK + r"\include\10.0.26100.0\cppwinrt"])
    return env


def main():
    os.makedirs(OUTDIR, exist_ok=True)

    # 1) 编译 4 个 TU（命令模板取 ThumbnailReporter.cpp 的真实 cl 命令）
    tmpl_cmds = ninja_commands(tgt_of(TEMPLATE))
    if not tmpl_cmds:
        print("!! 抓不到 %s 的 cl 命令——先跑过 python scripts/_build_agent.py 再试" % TEMPLATE)
        return 2
    tmpl = tmpl_cmds[-1]
    objs = []
    for rel in SRCS:
        obj = os.path.join(OUTDIR, rel.replace("/", "_").replace(".cpp", ".obj"))
        cmd = tmpl
        # 源文件路径：命令行里模板源以绝对路径出现
        src_abs = os.path.join(ROOT, TEMPLATE).replace("/", "\\")
        cmd = cmd.replace(src_abs, os.path.join(ROOT, rel).replace("/", "\\"))
        # /Fo /Fd 换成探针 obj（replacement 用 lambda：路径里的反斜杠会被
        # re.sub 当转义，直接传串会抛 bad escape）
        cmd = re.sub(r"/Fo\S+", lambda m: "/Fo" + obj.replace("/", "\\"), cmd, count=1)
        cmd = re.sub(r"/Fd\S+", lambda m: "/Fd" + os.path.join(OUTDIR, "probe.pdb").replace("/", "\\"),
                     cmd, count=1)
        p = subprocess.run(cmd, env=build_env(), cwd=BUILD)
        if p.returncode != 0:
            print("!! 编译失败：%s" % rel)
            return 2
        objs.append(obj)
        print("[cl] %s -> %s" % (rel, os.path.basename(obj)))

    # 2) 链接：agent 的真实链接是 cmd → cmake -E vs_link_exe → link.exe，
    #    obj/libs/flags 走 ninja 的 rspfile（构建后即删，磁盘上不存在）——
    #    所以从 build.ninja 解析：build 语句的 `|` 后是 libs，其下变量区是
    #    LINK_FLAGS；obj 列表换成探针 4 个。lib/flags 一个不补不删。
    link_exe = (r"C:\PROGRA~2\Microsoft Visual Studio\18\BUILDT~1\VC\Tools\MSVC"
                r"\1451~1.362\bin\Hostx64\x64\link.exe")
    ninja_txt = open(os.path.join(BUILD, "build.ninja"), encoding="utf-8",
                     errors="replace").read()
    m = re.search(r"^build stelarith-agent-qt\.exe: [^\n]*$", ninja_txt, re.M)
    if not m:
        print("!! build.ninja 里找不到 agent 链接语句")
        return 2
    stmt = m.group(0)
    libs_part = stmt.split("|", 1)[1].split("||", 1)[0]   # order-only 之前
    libs = [t for t in libs_part.split() if t.lower().endswith(".lib")]
    libs = [t.replace("$:", ":") for t in libs]           # ninja 转义还原
    # 变量区：build 语句行之后、下一个 build/rule 之前的缩进行
    after = ninja_txt[m.end():]
    nxt = re.search(r"^(?:build|rule|pool) ", after, re.M)
    varbody = after[:nxt.start()] if nxt else after
    fm = re.search(r"^  LINK_FLAGS = (.*)$", varbody, re.M)
    link_flags = fm.group(1).strip() if fm else ""
    # 系统库（d3d11/dxgi/ole32/user32/gdi32）走 LINK_LIBRARIES 变量，不在显式 inputs 里
    lm = re.search(r"^  LINK_LIBRARIES = (.*)$", varbody, re.M)
    extra_libs = shlex.split(lm.group(1).strip(), posix=False) if lm else []
    cmd = [link_exe, "/nologo"]
    if link_flags:
        cmd += shlex.split(link_flags, posix=False)
    cmd += [o.replace("/", "\\") for o in objs] + libs
    cmd += [t.replace("$:", ":") for t in extra_libs]
    cmd += ["/out:" + os.path.join(BUILD, "thumbnail_probe.exe"),
            "/implib:" + os.path.join(OUTDIR, "probe.lib"),
            "/pdb:" + os.path.join(OUTDIR, "probe.pdb"),
            "/subsystem:console"]
    p = subprocess.run(cmd, env=build_env(), cwd=BUILD)
    if p.returncode != 0:
        print("!! 链接失败")
        return 2
    print("[link] build/thumbnail_probe.exe")

    if "--no-run" in sys.argv:
        return 0

    # 3) 跑：exe 与 Qt6*.dll 同在 build/（dll 搜索目录 = exe 目录）；
    #    imageformats 插件（qjpeg.dll）在 Qt 安装目录 ⇒ QT_PLUGIN_PATH 指过去
    #    （probe main 里也 qputenv 了一份，双保险）。
    env = dict(os.environ)
    env["QT_PLUGIN_PATH"] = os.path.join(QT, "plugins")
    exe = os.path.join(BUILD, "thumbnail_probe.exe")
    p = subprocess.run([exe], env=env, cwd=BUILD)
    print("[run] 退出码 %d" % p.returncode)
    return p.returncode


if __name__ == "__main__":
    sys.exit(main())
