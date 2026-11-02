# -*- coding: utf-8 -*-
"""agent_panel 拆分等价性审计（2026-11-02 · 屎山整理 #3）。

四项断言：

  A. 覆盖：HEAD 里 agent_panel.cpp 的 5 个定义（buildSelfCheckItems / pluginRegistryUrl /
     buildPluginList / agentUiFill / agentUiWire）在「主文件 + agent_panel_data.cpp」
     两处合起来恰好都在。
  B. 逐字（数据层）：三个 builder + agentUiFill 的函数体与 HEAD **逐行相同**。
  C. 逐字（接线层）：agentUiWire 的函数体与 HEAD 逐行相同，**唯一允许的差异是 3 处
     跨 TU 改名**（s_storeEntries→agentPanelStoreEntries()、pluginRegistryUrl()→
     agentPanelPluginRegistryUrl()，以及为此新增的 #include 行不在函数体内）。
     这条必须精确放行、不多放行一个字符：把改名做成白名单正则，而不是"跳过含关键字
     的行" —— 后者会连带放过那几行上被改坏的其余内容。
  D. 无重复：同一函数不得在两处定义。

用法：python _audit_agent_panel_split.py
"""
import io
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-control-qt"
ORIG = "src/ui/agent_panel.cpp"
PARTS = [
    "src/ui/agent_panel.cpp",
    "src/ui/agent_panel_data.cpp",
]
NL = chr(10)

# 允许的跨 TU 改名（精确到表达式，不放行整行）
RENAMES = [
    ("s_storeEntries.insert(", "agentPanelStoreEntries().insert("),
    ("s_storeEntries.value(id)", "agentPanelStoreEntries().value(id)"),
    ("QUrl(pluginRegistryUrl())", "QUrl(agentPanelPluginRegistryUrl())"),
]

DATA_FUNCS = ["buildSelfCheckItems", "pluginRegistryUrl", "buildPluginList", "agentUiFill"]
WIRE_FUNCS = ["agentUiWire"]


def read_head(p):
    r = subprocess.run(["git", "-C", REPO, "show", "HEAD:" + p], capture_output=True)
    return r.stdout.decode("utf-8") if r.returncode == 0 else None


def read_wt(p):
    return io.open(os.path.join(REPO, p), encoding="utf-8").read()


def funcs(text):
    """{名: [函数源行]} —— 支持 `static 类型 名(` 与 `void 名(`（自由函数，无类限定）。"""
    lines = text.split(NL)
    out = {}
    for i, l in enumerate(lines):
        m = re.match(r'^(?:static\s+)?[A-Za-z_][\w:<>,\s\*&]*?\b(\w+)\([^;]*$', l)
        if not m:
            continue
        name = m.group(1)
        if name in ("if", "for", "while", "switch", "QObject", "QStringLiteral",
                    "QNetworkRequest", "QUrl", "QDir", "QFile", "QProcess", "return"):
            continue
        # 函数定义必须有形参列表关闭且不含分号（声明不算）
        j = i
        while j < len(lines) and not lines[j].rstrip().endswith("{"):
            if ";" in lines[j]:
                break
            j += 1
        else:
            pass
        if j >= len(lines) or not lines[j].rstrip().endswith("{"):
            continue
        k = j
        while k < len(lines) and lines[k].rstrip() != "}":
            k += 1
        out.setdefault(name, []).append(lines[i:k + 1])
    return out


def norm(body, allow_rename=False):
    out = []
    for ln in body:
        s = ln.rstrip()
        if allow_rename:
            for a, b in RENAMES:
                s = s.replace(b, a)
        out.append(s)
    return [x for x in out if x.strip()]


def main():
    head = read_head(ORIG)
    if head is None:
        print("✗ 读不到 HEAD:" + ORIG)
        return 1
    ref = funcs(head)
    print("HEAD %s：%d 行，%d 个自由函数定义：%s"
          % (ORIG, len(head.split(NL)), len(ref), sorted(ref)))

    now = {}
    for p in PARTS:
        txt = read_wt(p)
        fs = funcs(txt)
        print("  %-28s %4d 行  %d 函数：%s"
              % (p.split("/")[-1], len(txt.split(NL)), len(fs), sorted(fs)))
        for k, v in fs.items():
            now.setdefault(k, []).extend([(p, b) for b in v])

    ok = True
    target = DATA_FUNCS + WIRE_FUNCS

    # A + D
    missing = [k for k in target if k not in now]
    dup = {k: v for k, v in now.items() if len(v) > 1 and k in target}
    if missing:
        ok = False
        print(NL + "✗ A 丢失函数：%s" % missing)
    else:
        print(NL + "✓ A 覆盖：%d 个函数全部还在（数据层 4 + 接线层 1）" % len(target))
    if dup:
        ok = False
        print("✗ D 重复定义：%s" % {k: [p for p, _ in v] for k, v in dup.items()})
    else:
        print("✓ D 无重复：每个函数只在一处定义")

    # B 数据层逐字
    bad = []
    for k in DATA_FUNCS:
        if k not in ref or k not in now:
            continue
        if norm(now[k][0][1]) != norm(ref[k][0]):
            bad.append(k)
    if bad:
        ok = False
        print(NL + "✗ B 数据层函数体被改：%s" % bad)
    else:
        print("✓ B 逐字：数据层 %d 个函数体与 HEAD 完全一致" % len(DATA_FUNCS))

    # C 接线层逐字（放行 3 处改名）
    if "agentUiWire" in ref and "agentUiWire" in now:
        got = norm(now["agentUiWire"][0][1], allow_rename=True)
        exp = norm(ref["agentUiWire"][0])
        if got != exp:
            ok = False
            for x, y in zip(exp, got):
                if x != y:
                    print("✗ C 首个差异行：" + NL + "   HEAD: " + x[:120]
                          + NL + "   现在: " + y[:120])
                    break
            print("✗ C 接线层函数体与 HEAD 不一致（长度 %d vs %d）" % (len(exp), len(got)))
        else:
            print("✓ C 逐字：agentUiWire %d 行与 HEAD 一致，仅放行 3 处跨 TU 改名"
                  % len(exp))

    # 行数红线
    print("")
    for p in PARTS + ["src/ui/agent_panel_internal.h"]:
        n = len(read_wt(p).split(NL))
        lim = 300 if p.endswith(".h") else 600
        print("  %-32s %4d 行 / 红线 %d  %s"
              % (p.split("/")[-1], n, lim, "OK" if n <= lim else "超红线"))
        if n > lim:
            ok = False

    print(NL + ("审计通过 ✅" if ok else "审计失败 ❌"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
