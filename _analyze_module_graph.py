# -*- coding: utf-8 -*-
"""大文件拆分前的**依赖聚类分析**（2026-11-02 起，用于 registry.js 这类"上帝模块"）。

## 为什么需要它
registry.js 1510 行、80+ 个导出、20 多个模块级可变状态。凭"看着像一组"去切，
切完必然出现两种事故：
  ① 循环 import —— A 依赖 B、B 也依赖 A，ESM 能在函数调用期容忍，但只要有
     一处**模块初始化期**用到对方（如 `const X = foo()`），就变成 undefined；
  ② 状态被复制 —— 把 `const devices = new Map()` 两边各写一份，两份各记各的，
     表现是"在线设备时有时无"，而且不报错。

本脚本做三件事：
  1. 把文件切成「顶层声明单元」：每个 function / const / let 及其紧邻的上方注释块；
  2. 对每个单元找出它引用到的**其他单元**（跨单元边）；
  3. 用「去除某条边是否会让图断开」的思路近似找**天然缝**（弱连通分量 + 度数分析），
     并按候选切分打印：每个候选域包含哪些单元、哪些外部单元被它依赖、
     以及**双向依赖**（真正的循环风险）在哪。

只读分析，不改文件。

用法：python _analyze_module_graph.py Stelarith-cloud/src/registry.js
"""
import io
import os
import re
import sys
from collections import defaultdict

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.abspath(__file__))
NL = chr(10)

# 顶层声明：function X( / export function X( / const X = / let X = / export const
DECL_RE = re.compile(
    r'^(?:export\s+)?(?:async\s+)?function\s+(\w+)\s*\(|'
    r'^(?:export\s+)?(?:const|let|var)\s+(\w+)\s*='
)


def strip_comments_and_strings(src):
    """把注释与字符串内容清空但**保留换行**，这样行号仍然对得上。"""
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != NL:
                out.append(" ")
                i += 1
        elif c == "/" and i + 1 < n and src[i + 1] == "*":
            out.append("  ")
            i += 2
            while i < n and not (src[i] == "*" and i + 1 < n and src[i + 1] == "/"):
                out.append(NL if src[i] == NL else " ")
                i += 1
            if i < n:
                out.append("  ")
                i += 2
        elif c in "'\"`":
            q = c
            out.append(" ")
            i += 1
            while i < n:
                if src[i] == "\\":
                    out.append("  ")
                    i += 2
                    continue
                if src[i] == q:
                    out.append(" ")
                    i += 1
                    break
                out.append(NL if src[i] == NL else " ")
                i += 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def top_level_units(raw):
    """返回 [(name, kind, start_line, end_line, text)]，行号 1-based。"""
    clean = strip_comments_and_strings(raw)
    raw_lines = raw.split(NL)
    clean_lines = clean.split(NL)
    units = []
    depth = 0
    i = 0
    while i < len(clean_lines):
        line = clean_lines[i]
        stripped = line.strip()
        # 只在顶层（depth==0）认声明头
        if depth == 0:
            m = DECL_RE.match(stripped)
            if m and not stripped.startswith("//"):
                name = m.group(1) or m.group(2)
                kind = "fn" if m.group(1) else (
                    "state" if re.match(r'^(?:export\s+)?(?:const|let|var)\s+\w+\s*=\s*(new\s+)?(Map|Set|Array)', stripped)
                    or re.match(r'^(?:export\s+)?(?:const|let|var)\s+\w+\s*=\s*[\[\{]', stripped)
                    or re.match(r'^(?:export\s+)?let\s', stripped)
                    else "const")
                start = i
                # 找到这个单元的结束：大括号/圆括号配平回到 0 且行尾是 ; 或 }
                d = 0
                j = i
                started = False
                while j < len(clean_lines):
                    s = clean_lines[j]
                    for ch in s:
                        if ch in "({[":
                            d += 1
                            started = True
                        elif ch in ")}]":
                            d -= 1
                    if started and d <= 0:
                        break
                    if not started and s.rstrip().endswith(";"):
                        break
                    j += 1
                units.append((name, kind, start + 1, j + 1,
                              NL.join(raw_lines[start:j + 1])))
                i = j + 1
                continue
        depth += line.count("{") + line.count("(") + line.count("[")
        depth -= line.count("}") + line.count(")") + line.count("]")
        i += 1
    return units


def main():
    if len(sys.argv) < 2:
        print("用法：python _analyze_module_graph.py <file.js>")
        return 2
    path = sys.argv[1]
    raw = io.open(os.path.join(ROOT, path), encoding="utf-8").read()
    units = top_level_units(raw)
    names = {u[0] for u in units}
    print("%s：%d 行，%d 个顶层单元" % (path, len(raw.split(NL)), len(units)))
    print("")

    # 跨单元引用图
    edges = defaultdict(set)   # 单元 -> 它引用的其他单元
    for name, kind, s, e, text in units:
        clean = strip_comments_and_strings(text)
        body = clean[clean.find(NL):] if NL in clean else ""   # 去掉声明头那行（避免自引用）
        # 既算 name( / name. / name[ 这类"使用"，也算裸引用（如 knownDevices.size 在
        # 模板串外被直接用）。首版只认前者，导致 devices/knownDevices 这些真·枢纽
        # 出度算成 0，把他们误排进"最安全的叶子候选" —— 那是会切出两份状态的切法。
        used = re.findall(r'(?<![.\w$])([A-Za-z_$][\w$]*)', body)
        for u in used:
            if u in names and u != name:
                edges[name].add(u)

    # 弱连通分量（无向）
    adj = defaultdict(set)
    for a, bs in edges.items():
        for b in bs:
            adj[a].add(b)
            adj[b].add(a)
    seen = set()
    comps = []
    for u in names:
        if u in seen:
            continue
        stack = [u]
        seen.add(u)
        comp = []
        while stack:
            x = stack.pop()
            comp.append(x)
            for y in adj[x]:
                if y not in seen:
                    seen.add(y)
                    stack.append(y)
        comps.append(sorted(comp))
    comps.sort(key=len, reverse=True)

    print("弱连通分量：%d 个（最大 %d 个单元）" % (len(comps), len(comps[0]) if comps else 0))
    for c in comps[:5]:
        print("   [%2d] %s" % (len(c), ", ".join(c[:14]) + (" …" if len(c) > 14 else "")))
    print("")

    # 出/入度：入度低、出度可控的叶子单元是"最容易先搬走"的
    indeg = defaultdict(int)
    for a, bs in edges.items():
        for b in bs:
            indeg[b] += 1
    print("按「入度低 + 出度低」排序的叶子候选（先搬这些最安全）：")
    scored = sorted(names, key=lambda x: (indeg[x] + len(edges[x]), x))
    for u in scored[:14]:
        print("   %-30s 入度%2d 出度%2d  %s"
              % (u, indeg[u], len(edges[u]),
                 ("→ " + ", ".join(sorted(edges[u])[:6])) if edges[u] else "(无依赖)"))
    print("")

    # 双向依赖（循环）风险
    print("双向依赖（真循环风险）：")
    dup = 0
    for a, bs in edges.items():
        for b in bs:
            if a in edges.get(b, ()) and a < b:
                dup += 1
                print("   ↺ %s ⇄ %s" % (a, b))
    if not dup:
        print("   无")
    print("")

    # 逐行区间表，供人工按业务域划切
    print("单元清单（行区间 · 类型 · 引用数）：")
    for name, kind, s, e, _t in units:
        print("   %5d-%-5d %-6s %-30s →%d  ←%d"
              % (s, e, kind, name, len(edges[name]), indeg[name]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
