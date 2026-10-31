#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""AgentShell 全实例化探针驱动：moc → cl → QQuickWidget 起真壳 → 断言 → 截图。

与 _agent_shell_probe.qml（qml.exe）的分工见 _agent_ui_probe.cpp 顶部说明：
qml.exe 注入不了 context property，页只能报 ReferenceError；本探针用
setContextProperty 注入**照真实签名伪造的 AgentUi**，5 个页全部真实例化。

用法：python scripts/_agent_ui_probe.py
退出：探针 10=全过 20=有失败 30=壳没起来；本脚本按 rc 转 0/1。
"""
import os
import io
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
SRC = os.path.join(ROOT, "src")
OUT_DIR = os.path.join(BUILD, "_objs")
PROBE_SRC = os.path.join(ROOT, "scripts", "_agent_ui_probe.cpp")
# 2026-10-28：伪件退役 —— 直接 moc 产品真件 agent_ui.h。
# 理由：AgentUi 是 header-only、构造零依赖，可直接实例化；伪件 FakeAgentUi 缺
# 模块三的 20+ 个新属性（settingsTab/settingsSections/tabXxx/结果色…），
# 加载设置页必然满屏 ReferenceError。真件的只读展示串（connStateColor 等）
# 也顺带被验到，不用在伪件里复刻一遍再漂移。
AGENT_UI_H = os.path.join(SRC, "agent_ui.h")
MOC_OUT = os.path.join(OUT_DIR, "_agent_ui_moc.cpp")
# 2026-10-31 拆分第一刀：AgentUi 的基类（记录/自检/插件/时段）也要 moc，理由见 main() 内注释
AGENT_RECORD_UI_H = os.path.join(SRC, "agent_record_ui.h")
MOC_RECORD_OUT = os.path.join(OUT_DIR, "_agent_record_ui_moc.cpp")
REPORT = os.path.join(OUT_DIR, "_agent_ui_probe_report.txt")
# 2026-10-30：单张截图升级为逐屏截图（页 ×6 + 设置 7 分类 + 广播浮层 + 广播机 7 页），
# 落 OUT_DIR/_shots/NN_<id>.png。为什么不覆盖旧图：换新核验要能左右比较。
SHOTS = os.path.join(OUT_DIR, "_shots")

MSVC = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Tools\MSVC\14.51.36231"
SDK = r"C:\Program Files (x86)\Windows Kits\10"
QT = r"D:\QTqwq\6.12.0\msvc2022_64"
CL = MSVC + r"\bin\Hostx64\x64\cl.exe"
MOC = QT + r"\bin\moc.exe"
RCC = QT + r"\bin\rcc.exe"
ICONS_DIR = os.path.join(ROOT, "resources", "icons")

DEFINES = ["-DQT_CORE_LIB", "-DQT_GUI_LIB", "-DQT_NETWORK_LIB", "-DQT_NO_DEBUG",
           "-DQT_OPENGL_LIB", "-DQT_QML_LIB", "-DQT_QUICKWIDGETS_LIB", "-DQT_QUICK_LIB",
           "-DQT_SVG_LIB", "-DQT_WIDGETS_LIB", "-DUNICODE", "-DWIN32", "-DWIN64",
           "-D_ENABLE_EXTENDED_ALIGNED_STORAGE", "-D_UNICODE", "-D_WIN64"]
FLAGS = ["/nologo", "/TP", "/std:c++17", "-MD", "/utf-8", "/Zc:__cplusplus",
         "/permissive-", "/EHsc"]
SHADCN_ROOT = os.path.join(ROOT, "..", "third_party", "qtshadcn")
SHADCN_LIB = os.path.join(BUILD, "_qtshadcn", "src", "QtShadcn.lib")
INCLUDES = ["/I", os.path.join(ROOT, "scripts"), "/I", SRC,
            "/I", os.path.join(SHADCN_ROOT, "src"),
            "/I", os.path.join(SHADCN_ROOT, "src", "core"),
            "/I", QT + r"\include", "/I", QT + r"\include\QtCore",
            "/I", QT + r"\include\QtGui", "/I", QT + r"\include\QtWidgets",
            "/I", QT + r"\include\QtQml", "/I", QT + r"\include\QtQuick",
            "/I", QT + r"\include\QtQuickWidgets", "/I", QT + r"\include\QtSvg",
            "/I", QT + r"\include\QtNetwork", "/I", QT + r"\include\QtOpenGL"]

LIBS = [SHADCN_LIB, QT + r"\lib\Qt6QuickWidgets.lib", QT + r"\lib\Qt6Quick.lib", QT + r"\lib\Qt6Qml.lib",
        QT + r"\lib\Qt6QuickControls2.lib",
        os.path.join(BUILD, "_qtshadcn", "src", "QtShadcn.lib"),
        QT + r"\lib\Qt6Svg.lib", QT + r"\lib\Qt6Gui.lib", QT + r"\lib\Qt6Widgets.lib",
        QT + r"\lib\Qt6Network.lib", QT + r"\lib\Qt6Core.lib",
        QT + r"\lib\Qt6OpenGL.lib", "user32.lib", "gdi32.lib"]


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
    e["PATH"] = QT + r"\bin;" + os.path.join(BUILD, "_qtshadcn", "src") + ";" + e.get("PATH", "")
    e["QT_ASSUME_STDERR_HAS_CONSOLE"] = "1"
    # QtShadcn 是 static QML 模块：QML 引擎靠 import 路径找 qmldir；且必须 Basic 风格
    # （非 Basic 会吃掉 App* 组件对 background/contentItem 的覆写 ⇒ 样式静默失效）。
    e["QML_IMPORT_PATH"] = os.path.join(BUILD, "_qtshadcn", "src")
    e["QT_QUICK_CONTROLS_STYLE"] = "Basic"
    return e


def run(cmd, **kw):
    p = subprocess.run(cmd, env=env(), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)
    for ln in p.stdout.decode("gbk", "replace").splitlines():
        low = ln.lower()
        if "error" in low:
            print(ln)
    return p.returncode


def build_icon_qrc():
    """把 resources/icons/*.svg 打成 qrc 并用 rcc 编成 cpp。

    产品侧是 CMake `qt_add_resources(PREFIX "/icons")`，图标编进 exe ⇒ QML 的
    `qrc:/icons/<name>.svg` 有图。探针用等价 qrc 复刻，否则 SvgIcon 全空白：
    2026-10-28 实测截图里左侧 7 个分类行、底部导航图标**全是空块**，很容易被
    误判成"分类菜单没做图标"。（viewer 的 class_center 探针同一坑，已同样修法。）
    """
    if not os.path.isdir(ICONS_DIR):
        return None
    files = sorted(fn for fn in os.listdir(ICONS_DIR) if fn.endswith(".svg"))
    if not files:
        return None
    lines = ['<RCC>', '  <qresource prefix="/icons">']
    for fn in files:
        full = os.path.join(ICONS_DIR, fn).replace("\\", "/")
        lines.append('    <file alias="%s">%s</file>' % (fn, full))
    lines += ['  </qresource>', '</RCC>', '']
    qrc = os.path.join(OUT_DIR, "probe_icons.qrc")
    io.open(qrc, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
    cpp = os.path.join(OUT_DIR, "qrc_probe_icons.cpp")
    if run([RCC, "-name", "probe_icons", "-o", cpp, qrc]) != 0 or not os.path.isfile(cpp):
        return None
    return cpp


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    # 1) moc（头文件输入：moc 会 #include 回头文件，生成的 TU 才自洽）
    # ⚠️ 2026-10-31 拆分第一刀后必须**两个头都 moc**：AgentUi 继承 AgentRecordUi，
    #    基类的 Q_OBJECT 产物（staticMetaObject / 信号实现）不在 agent_ui.h 的 moc 里。
    #    只 moc 派生头 ⇒ 链接期 11 个 undefined（LNK2001/LNK2019），实测确认。
    for hdr, out in ((AGENT_RECORD_UI_H, MOC_RECORD_OUT), (AGENT_UI_H, MOC_OUT)):
        if run([MOC, hdr, "-o", out]) != 0:
            print("moc 失败：" + os.path.basename(hdr))
            return 2

    # 2) 编译：探针本体 + moc + qrc 图标 + SvgIconProvider（自绘，Qt only）
    qrc_cpp = build_icon_qrc()
    if not qrc_cpp:
        print("rcc 失败（resources/icons 缺失？）")
        return 2
    objs = []
    for s in [PROBE_SRC, MOC_OUT, MOC_RECORD_OUT, qrc_cpp, SRC + r"\svgiconprovider.cpp"]:
        o = os.path.join(OUT_DIR, os.path.basename(s) + ".obj")
        if run([CL] + FLAGS + DEFINES + INCLUDES + ["/c", "/Fo" + o, s]) != 0:
            print("编译失败：", s)
            return 2
        objs.append(o)

    # 3) 链接（绝不带 /TP：会把 .lib 当源码编）
    exe = os.path.join(OUT_DIR, "_agent_ui_probe.exe")
    if run([CL, "/nologo", "/EHsc", "/Fe" + exe] + objs + LIBS) != 0:
        print("链接失败")
        return 2

    # 4) 跑（cwd=_objs：平台插件按 exe 目录/cwd 找，且报告/截图落这里）
    # ⚠️ 2026-10-31：不再 os.remove 旧报告/旧图 —— 环境的安全删除守卫会拦 bulk delete，
    #    整个探针就卡在这一行（runs 0 秒、报告还是上一版的）。旧报告由探针自己
    #    WriteOnly 覆写；旧图按同名覆盖，多余的旧图留着不影响判读。
    p = subprocess.run([exe], env=env(), cwd=OUT_DIR,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print("探针退出码:", p.returncode, "（10=全过 20=有失败 30=壳没起来）")
    if os.path.exists(REPORT):
        with open(REPORT, encoding="utf-8") as f:
            print(f.read())
    else:
        print("（没生成报告，子进程输出如下）")
        print(p.stdout.decode("utf-8", "replace")[:4000])
    if os.path.isdir(SHOTS):
        pngs = sorted(fn for fn in os.listdir(SHOTS) if fn.endswith(".png"))
        print("截图 %d 张 → %s" % (len(pngs), SHOTS))
        for fn in pngs:
            print("   ", fn)
    return 0 if p.returncode == 10 else 1


if __name__ == "__main__":
    sys.exit(main())
