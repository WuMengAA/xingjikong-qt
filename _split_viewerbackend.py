# -*- coding: utf-8 -*-
"""viewerbackend.cpp 按域拆分（2026-11-02 · 屎山整理 #6）。

## 为什么能这么拆
ViewerBackend 的 99 个成员函数全堆在 2995 行一个 TU 里。C++ 允许**同一类的成员函数
分布在不同 TU**（private 照访，不需要 friend、不需要改头文件可见性）—— 这条在
island_bridge（623→382）与 agent_panel（646→388）已经验证过两次。

## 选块原则（与 registry 媒体域同一条）
只挑**叶子域**：不与其他簇共享中间状态的那些函数。按功能边界找，而不是按行数切。
本次六个域：
  · vnc      远控链路（建立/鉴权/帧注入/断开）—— 自成一体，只碰 m_vnc* 成员
  · terminal 远程终端（开/输/关）—— 只碰 m_term*
  · qmlpatch QML 热更（检查/解压 zip）—— 纯函数 + m_patch*
  · proc     进程黑名单 + 开机自启/快捷方式 —— 系统集成，与网络无关
  · diag     诊断与反馈 + 事件流水拉取 —— 只读快照，无副作用链

## 审计
搬完由 `_audit_viewerbackend_split.py` 做 A 覆盖 / B 逐字 / C 无重复 / D 行数。

用法：python _split_viewerbackend.py [--apply]
"""
import io
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-viewer-qt"
MAIN = "src/viewerbackend.cpp"
NL = chr(10)

# 域 → (新文件名, [函数名...])
DOMAINS = {
    "vnc": ("src/vnc/viewerbackend_vnc.cpp", [
        "setVncImageProvider", "setH264ImageProvider", "setVncState",
        "beginTakeover", "requestVncAccessAuth", "sendVncAuth", "submitVncPassword",
        "startVnc", "stopVnc", "onVncData", "onVncFrame",
    ]),
    "terminal": ("src/terminal/viewerbackend_terminal.cpp", [
        "setTermState", "sendTermAction", "termOpen", "termInput", "termClose",
    ]),
    "qmlpatch": ("src/patch/viewerbackend_qmlpatch.cpp", [
        "checkQmlPatch", "unzipStore",
    ]),
    "proc": ("src/sys/viewerbackend_proc.cpp", [
        "procBlacklist", "addProcToBlacklist", "removeProcFromBlacklist", "killBlacklisted",
        "autoStartEnabled", "setAutoStart", "desktopShortcutExists", "setDesktopShortcut",
    ]),
    "diag": ("src/diag/viewerbackend_diag.cpp", [
        "viewerLogTail", "diagnosticsSummary", "copyToClipboard", "openLogFolder",
        "requestDiagFeedback", "requestTodaySchedule", "fetchEvents", "fetchActionRanking",
    ]),
}

# 每个域把自己需要的 include 写在文件头（宁可多写，漏了就是 C1083 或隐式声明）
HEADERS = {
    "vnc": [
        '#include "viewerbackend.h"',
        '#include "vnc_client.h"',        # VncClient 真实类型
        '#include "vnc_image_provider.h"',
        '#include "H264ImageProvider.h"',
        "#include <QJsonObject>",
        "#include <QJsonValue>",
        "#include <QTimer>",
        "#include <QUrl>",
    ],
    "terminal": [
        '#include "viewerbackend.h"',
        "#include <QJsonObject>",
    ],
    "qmlpatch": [
        '#include "viewerbackend.h"',
        "#include <QDir>",
        "#include <QFile>",
        "#include <QFileInfo>",
    ],
    "proc": [
        '#include "viewerbackend.h"',
        '#include "winlauncher.h"',       # steOpenUrlExternal / 静默启动
        "#include <QDir>",
        "#include <QFileInfo>",
        "#include <QProcess>",
        "#include <QSettings>",
        "#include <QStandardPaths>",
    ],
    "diag": [
        '#include "viewerbackend.h"',
        "#include <QClipboard>",
        "#include <QDateTime>",
        "#include <QDesktopServices>",
        "#include <QFile>",
        "#include <QFileInfo>",
        "#include <QGuiApplication>",
        "#include <QJsonArray>",
        "#include <QJsonDocument>",
        "#include <QJsonObject>",
        "#include <QNetworkAccessManager>",
        "#include <QNetworkReply>",
        "#include <QStandardPaths>",
        "#include <QSysInfo>",
        "#include <QUrl>",
    ],
}

TITLES = {
    "vnc": "远控 VNC 链路（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）",
    "terminal": "远程终端（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）",
    "qmlpatch": "QML 热更补丁（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）",
    "proc": "进程黑名单 + 开机自启 / 桌面快捷方式（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）",
    "diag": "诊断与反馈 + 事件流水（2026-11-02 · 屎山整理 #6，从 viewerbackend.cpp 搬出）",
}

NOTES = {
    "vnc": "// 搬出理由：远控是本文件里最独立的一条链 —— 建立连接、鉴权、帧注入、断开，\n"
           "// 全程只碰 m_vnc* / m_vncState 这组成员，不参与设备表、不参与下发指令。",
    "terminal": "// 搬出理由：终端只有开/输/关三个动作 + 一个状态机，与远控并列但互不依赖。",
    "qmlpatch": "// 搬出理由：热更补丁是纯文件操作（下载完成后的落盘与解压），不碰网络。",
    "proc": "// 搬出理由：系统集成层（Win32 进程 / 注册表自启 / 快捷方式），与云端链路无关。\n"
            "// 单独成文件的价值：这类代码在本机调试时最容易被安全软件拦，隔离后有据可查。",
    "diag": "// 搬出理由：诊断只读快照（日志尾部 / 系统信息 / 连接状态）+ 两个 HTTP 拉取，\n"
            "// 一次性产物，不改任何业务状态 —— 正是「产出数据」那一侧。",
}


def read_main():
    return io.open(os.path.join(REPO, MAIN), encoding="utf-8").read()


def find_funcs(lines):
    """返回 {name: (start, end)}，1-based 闭区间；识别 `Type ViewerBackend::name(`。"""
    out = {}
    for i, l in enumerate(lines):
        m = re.match(r'^(?:[\w:<>,*&\s]+?)\bViewerBackend::(\w+)\s*\(', l)
        if not m:
            continue
        d = 0
        j = i
        while j < len(lines):
            d += lines[j].count("{") - lines[j].count("}")
            if d == 0 and j > i:
                break
            j += 1
        out.setdefault(m.group(1), (i, j))
    return out


def with_leading_comments(lines, start):
    """把函数紧邻上方的连续注释/空行一起带走，避免注释变孤儿。"""
    s = start
    while s > 0:
        t = lines[s - 1].strip()
        if t.startswith("//") or t.startswith("*") or t.startswith("/*"):
            s -= 1
        elif not t and s >= 2 and (lines[s - 2].strip().startswith("//")
                                   or lines[s - 2].strip().startswith("*")):
            s -= 1
        else:
            break
    return s


def main():
    apply = "--apply" in sys.argv
    src = read_main()
    lines = src.split(NL)
    total = len(lines)
    print("源文件 %s：%d 行" % (MAIN, total))

    found = find_funcs(lines)
    print("识别成员函数 %d 个" % len(found))

    # 校验：所有要搬的函数都必须存在
    plan = []
    missing = []
    for dom, (path, names) in DOMAINS.items():
        for n in names:
            if n not in found:
                missing.append((dom, n))
    if missing:
        print("✗ 找不到函数：%s" % missing)
        return 1

    # 计算要删的行区间（含上方注释），先收集再统一删除，避免行号漂移
    spans = {}
    for dom, (path, names) in DOMAINS.items():
        for n in names:
            s, e = found[n]
            spans[n] = (with_leading_comments(lines, s), e + 1)

    # 重叠检查：两个域不得抢同一行
    ordered = sorted(spans.items(), key=lambda kv: kv[1][0])
    for (n1, (a1, b1)), (n2, (a2, b2)) in zip(ordered, ordered[1:]):
        if a2 < b1:
            print("✗ 函数区间重叠：%s(%d-%d) 与 %s(%d-%d)" % (n1, a1 + 1, b1 + 1, n2, a2 + 1, b2 + 1))
            return 1

    # 生成各域文件
    outs = {}
    for dom, (path, names) in DOMAINS.items():
        body = []
        for n in names:
            a, b = spans[n]
            chunk = lines[a:b]
            while chunk and not chunk[-1].strip():
                chunk.pop()
            body.append(NL.join(chunk))
        txt = NL.join(
            ["// " + TITLES[dom], "//"] +
            [("// " + x) if x else "//" for x in NOTES[dom].split(NL)] +
            [""] + HEADERS[dom] + ["", NL.join(body).rstrip(), ""])
        outs[path] = txt

    # 主文件：删除所有已搬区间
    drop = [False] * total
    for _n, (a, b) in spans.items():
        for i in range(a, b):
            drop[i] = True
    kept = [l for i, l in enumerate(lines) if not drop[i]]
    main_txt = NL.join(kept)

    # 压缩因删除产生的连续空行（3 个以上 → 2 个）
    main_txt = re.sub(NL + "{4,}", NL * 3, main_txt)
    while main_txt.endswith(NL * 3):
        main_txt = main_txt[:-1]

    moved = sum(b - a for a, b in spans.values())
    print(NL + "计划搬走 %d 个函数 / %d 行：" % (len(spans), moved))
    for dom, (path, names) in DOMAINS.items():
        n = len(outs[path].split(NL))
        print("  %-46s %4d 行  %d 函数" % (path, n, len(names)))
        if n > 600:
            print("✗ %s 超红线 600" % path)
            return 1
    print("  %-46s %4d 行 → %4d 行" % (MAIN, total, len(main_txt.split(NL))))

    if not apply:
        print(NL + "（dry-run：加 --apply）")
        return 0

    io.open(os.path.join(REPO, MAIN), "w", encoding="utf-8", newline=NL).write(main_txt)
    for path, txt in outs.items():
        full = os.path.join(REPO, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        io.open(full, "w", encoding="utf-8", newline=NL).write(txt)
    print(NL + "已写入 %d 个文件" % (len(outs) + 1))
    for path in outs:
        print("   " + path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
