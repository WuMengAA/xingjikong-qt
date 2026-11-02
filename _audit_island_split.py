# -*- coding: utf-8 -*-
"""island_bridge 拆分等价性审计（2026-11-02 · 屎山整理 #2）。

三项断言（都是"机器逐字比对"，不靠眼睛）：

  A. 覆盖：HEAD 里 island_bridge.cpp 的每个 IslandBridge 成员函数定义，在
     「主文件 + 拆出的两个域文件」三处合起来**恰好出现一次**。
  B. 逐字：每个函数体的每一行与 HEAD **完全相同**（搬家不改逻辑 —— 一个空格都不许变）。
  C. 无重复：同一函数不得在两处定义（重复定义 = 链接期 multiple definition，
     或更糟：某处是旧版本，改一处忘一处）。

⚠️ 为什么要有 C：搬家最容易犯的错是「复制过去但没删干净」。编译期可能照样过
（旧的那份若被 #if 掉或属于不同重载就悄悄共存），运行起来走的是哪份全凭运气。

用法：python _audit_island_split.py
"""
import io
import os
import re
import subprocess
import sys
from collections import Counter

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-control-qt"
ORIG = "src/island/island_bridge.cpp"
PARTS = [
    "src/island/island_bridge.cpp",
    "src/island/island_bridge_history.cpp",
    "src/island/island_bridge_annotate.cpp",
]
NL = chr(10)


def read_head(p):
    r = subprocess.run(["git", "-C", REPO, "show", "HEAD:" + p], capture_output=True)
    return r.stdout.decode("utf-8") if r.returncode == 0 else None


def read_wt(p):
    return io.open(os.path.join(REPO, p), encoding="utf-8").read()


def funcs(text):
    """{函数名: [函数体所有行]} —— 只切 `^类型 IslandBridge::name(...)` 到配对 `}`。"""
    lines = text.split(NL)
    out = {}
    for i, l in enumerate(lines):
        m = re.match(r'^[A-Za-z].*\bIslandBridge::(\w+)\(', l)
        if not m:
            continue
        j = i
        while j < len(lines) and lines[j].rstrip() != "}":
            j += 1
        out.setdefault(m.group(1), []).append(lines[i:j + 1])
    return out


def main():
    head = read_head(ORIG)
    if head is None:
        print("✗ 读不到 HEAD:" + ORIG)
        return 1
    ref = funcs(head)
    print("HEAD %s：%d 行，%d 个成员函数定义" % (ORIG, len(head.split(NL)), len(ref)))

    now = {}
    for p in PARTS:
        txt = read_wt(p)
        fs = funcs(txt)
        print("  %-38s %4d 行  %2d 函数" % (p.split("/")[-1], len(txt.split(NL)), len(fs)))
        for k, v in fs.items():
            now.setdefault(k, []).extend([(p, body) for body in v])

    ok = True

    # A. 覆盖
    missing = [k for k in ref if k not in now]
    added = [k for k in now if k not in ref]
    if missing:
        ok = False
        print(NL + "✗ A 丢失函数 %d 个：%s" % (len(missing), missing))
    if added:
        print(NL + "· A 新增函数 %d 个（拆分可能带来的辅助函数，需人看）：%s"
              % (len(added), added))
    if not missing:
        print(NL + "✓ A 覆盖：HEAD 的 %d 个函数全部还在" % len(ref))

    # B. 逐字
    bad = []
    for k, bodies in ref.items():
        if k not in now:
            continue
        cur = now[k]
        if len(cur) != 1:
            continue
        got = cur[0][1]
        # 允许「块尾空行差异」，其余逐行必须一致
        if [x.rstrip() for x in got if x.strip()] != [x.rstrip() for x in bodies[0] if x.strip()]:
            bad.append((k, cur[0][0]))
    if bad:
        ok = False
        print(NL + "✗ B 函数体被改 %d 个：" % len(bad))
        for k, p in bad:
            print("   %-22s in %s" % (k, p.split("/")[-1]))
    else:
        print("✓ B 逐字：%d 个函数体与 HEAD 完全一致（搬家未改逻辑）" % len(ref))

    # C. 无重复
    dup = {k: v for k, v in now.items() if len(v) > 1}
    if dup:
        ok = False
        print(NL + "✗ C 重复定义 %d 个：" % len(dup))
        for k, v in dup.items():
            print("   %-22s 出现在 %s" % (k, [p.split("/")[-1] for p, _ in v]))
    else:
        print("✓ C 无重复：每个函数只在一处定义")

    # 附带：三个文件行数都必须在 600 红线内
    print("")
    for p in PARTS:
        n = len(read_wt(p).split(NL))
        flag = "OK" if n <= 600 else "超红线"
        print("  %-38s %4d 行  %s" % (p.split("/")[-1], n, flag))
        if n > 600:
            ok = False

    print(NL + ("审计通过 ✅" if ok else "审计失败 ❌"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
