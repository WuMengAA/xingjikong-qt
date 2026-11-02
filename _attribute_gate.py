# -*- coding: utf-8 -*-
"""门禁违规归因：这次搬家到底该背几项？（2026-11-02）

## 为什么必须做这一步
搬家后门禁报 14 项 error。但其中**大部分是别的线在途变胖**（index.js +155、
registry.js +99、viewerbackend +9…），跟「文件从 src/ 挪到 src/island/」毫无关系。
如果不加区分地"承认现状"，就会把别人未提交的膨胀写进台账 —— 那是给漂移发许可；
反过来若错误地回滚台账，又会让门禁从此看不见这几个文件。

## 判据（三项必须同时成立才算「搬家留下的」）
  1. 该文件在 HEAD 里的行数 ≠ 台账登记值 → **漂移早于本次搬家**，不是我们造成的；
  2. 工作区行数 ≈ HEAD 行数（差 0） → 本次搬家没改行数（include 是行内改写）；
  3. 若 HEAD 行数 == 台账值，而工作区更多 → 才是本次或他线新增。

用法：python _attribute_gate.py
"""
import io
import json
import os
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.abspath(__file__))
WL = os.path.join(ROOT, "Stelarith-shared-qml", "scripts", "fixtures", "oversize-legacy.json")
PROJECTS = ["Stelarith-control-qt", "Stelarith-viewer-qt", "Stelarith-cloud", "Stelarith-shared-qml"]


def head_lines(rel):
    """文件在 git HEAD 里的行数；不在 git 里返回 None。

    ⚠️ 搬家后台账 key 是新路径，而 HEAD 里还是旧路径 —— 直接 `git show HEAD:<新路径>`
    必定失败，会把「同一个文件改了个位置」误判成「新文件」。所以失败时按 basename
    去 HEAD 的树里找唯一命中，再取行数。
    """
    proj = rel.split("/", 1)[0]
    inner = rel.split("/", 1)[1]
    repo = os.path.join(ROOT, proj)
    r = subprocess.run(["git", "-C", repo, "show", "HEAD:" + inner], capture_output=True)
    if r.returncode == 0:
        return r.stdout.decode("utf-8", "replace").count("\n")
    bn = inner.rsplit("/", 1)[-1]
    ls = subprocess.run(["git", "-C", repo, "ls-tree", "-r", "--name-only", "HEAD"],
                        capture_output=True)
    if ls.returncode != 0:
        return None
    hits = [p for p in ls.stdout.decode("utf-8", "replace").split("\n")
            if p.rsplit("/", 1)[-1] == bn]
    if len(hits) != 1:
        return None
    r2 = subprocess.run(["git", "-C", repo, "show", "HEAD:" + hits[0]], capture_output=True)
    if r2.returncode != 0:
        return None
    return r2.stdout.decode("utf-8", "replace").count("\n")


def work_lines(rel):
    p = os.path.join(ROOT, rel)
    if not os.path.exists(p):
        return None
    return io.open(p, encoding="utf-8", errors="replace").read().count("\n")


def main():
    data = json.loads(io.open(WL, encoding="utf-8").read())
    rows = []
    for rel, meta in data["files"].items():
        if not isinstance(meta, dict) or "lines" not in meta:
            continue
        w = work_lines(rel)
        if w is None:
            continue
        reg = meta["lines"]
        h = head_lines(rel)
        if w <= reg:
            continue
        rows.append((rel, reg, h, w))

    if not rows:
        print("✓ 台账内无变胖项")
        return 0

    print("%-62s %7s %7s %7s  %s" % ("文件", "台账", "HEAD", "工作区", "归因"))
    print("-" * 100)
    mine = 0
    for rel, reg, h, w in sorted(rows, key=lambda r: -(r[3] - r[1])):
        if h is None:
            why = "不在 git（新文件）"
        elif h > reg:
            why = "漂移早于本次（HEAD 已 %d）" % (h - reg)
        elif w > h:
            why = "★ 本次/HHEAD 之后 +%d" % (w - h)
            mine += 1
        else:
            why = "未归因"
        print("%-62s %7d %7s %7d  %s" % (rel.split("/", 1)[1][:60], reg,
                                         h if h is not None else "-", w, why))
    print("")
    print("其中「HEAD 之后新增」的：%d 项" % mine)
    return 0


if __name__ == "__main__":
    sys.exit(main())
