# -*- coding: utf-8 -*-
"""台账补登记：把「已进入 HEAD 但台账没跟上」的行数补齐（2026-11-02）。

## 与 --update-whitelist 的区别（这条必须讲清楚）
`node scripts/test-file-size.mjs --update-whitelist` 会把**工作区当前值**写进台账，
包括别人尚未提交、正在膨胀的改动 —— 那等于给漂移发许可，门禁从此对这文件失效。

本脚本只写 **HEAD 值**：
   · HEAD 行数 ＞ 台账值 → 补登记到 HEAD（这是既成事实，不是我批的；注释写明来源）
   · 工作区 ＞ HEAD     → **不动**，仍然让门禁红着报出来（那才是真该处理的新增）

用法：python _sync_ledger_to_head.py [--apply]
"""
import argparse
import io
import json
import os
import subprocess
import sys
from datetime import date

sys.stdout.reconfigure(encoding="utf-8")

ROOT = os.path.dirname(os.path.abspath(__file__))
WL = os.path.join(ROOT, "Stelarith-shared-qml", "scripts", "fixtures", "oversize-legacy.json")
NL = chr(10)
TODAY = "2026-11-02"


def head_blob(rel):
    proj, inner = rel.split("/", 1)
    repo = os.path.join(ROOT, proj)
    r = subprocess.run(["git", "-C", repo, "show", "HEAD:" + inner], capture_output=True)
    if r.returncode == 0:
        return r.stdout.decode("utf-8", "replace")
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
    return r2.stdout.decode("utf-8", "replace") if r2.returncode == 0 else None


def work_lines(rel):
    p = os.path.join(ROOT, rel)
    if not os.path.exists(p):
        return None
    return io.open(p, encoding="utf-8", errors="replace").read().count("\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apply", action="store_true")
    args = ap.parse_args()

    data = json.loads(io.open(WL, encoding="utf-8").read())
    files = data["files"]
    synced, pending = [], []

    for rel, meta in files.items():
        if not isinstance(meta, dict) or "lines" not in meta:
            continue
        blob = head_blob(rel)
        if blob is None:
            continue
        h = blob.count("\n")
        reg = meta["lines"]
        w = work_lines(rel)
        if h > reg:
            note = ("%s 补登记 %d→%d：HEAD 已在此行数（非本批批准，为登记滞后补齐）；"
                    "本批未改该文件内容" % (TODAY, reg, h))
            synced.append((rel, reg, h, w, note))
        if w is not None and w > h:
            pending.append((rel, h, w))

    if synced:
        print("%s %d 条台账补登记到 HEAD 值：" % ("已写" if args.apply else "待写", len(synced)))
        for rel, reg, h, w, _n in synced:
            flag = "  ⚠ 工作区仍高于 HEAD(%d)，门禁会继续报" % w if (w or 0) > h else ""
            print("   %-58s %4d → %4d%s" % (rel.split("/", 1)[1][:56], reg, h, flag))
    else:
        print("✓ 台账与 HEAD 一致，无需补登记")

    if pending:
        print("")
        print("⚠ %d 个文件工作区 > HEAD（**不回写**，让门禁继续报真新增）：" % len(pending))
        for rel, h, w in pending:
            print("   %-58s HEAD=%d 工作区=%d (+%d)" % (rel.split("/", 1)[1][:56], h, w, w - h))

    if args.apply and synced:
        for rel, _reg, h, _w, note in synced:
            files[rel]["lines"] = h
            plan = files[rel].get("plan", "")
            files[rel]["plan"] = (plan + " | " + note) if plan else note
        data["updated"] = TODAY
        io.open(WL, "w", encoding="utf-8", newline=NL).write(
            json.dumps(data, ensure_ascii=False, indent=2) + NL)
        print(NL + "已写回台账")
    elif synced:
        print(NL + "（dry-run：加 --apply）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
