#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""从 ClassIsland 档案恢复「班级课表 + 作息」到我们自己的云端（2026-10-25）

背景：CIMS 退役前，课表是 CIMS 的 `cp_files / tl_files / sub_files` 资源集；
退役后课表回落到星集控云端的 `class_timetable`（每班一份 profile）。
本脚本把老师手里的 ClassIsland 官方档案（默认 `D:\\Classlsland\\data\\Profiles\\Default.json`）
按课表群导入到指定班级，**归一化在云端做**（`/api/class/timetable-import`），
本脚本只负责：读档案 → 选群 → 发请求 → 回读校验 → 打报告。

为什么不让脚本自己解析：规则只有一处真源（src/class-timetable.js），
脚本再实现一遍就是第二套口径，两端必然漂。

用法：
    python scripts/restore-classisland-timetable.py                     # 单班环境：自动选唯一班 + 唯一群
    python scripts/restore-classisland-timetable.py --group "1班课表群"
    python scripts/restore-classisland-timetable.py --class cls-xxxx --group 8班课表群 --dry-run
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import urllib.error
import urllib.request

DEFAULT_PROFILE = r"D:\Classlsland\data\Profiles\Default.json"
DEFAULT_BASE = "http://localhost:8788"
ENV_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "Stelarith-cloud", ".env")


def read_token() -> str:
    """从云端 .env 取 CLOUD_VIEWER_TOKEN（也接受环境变量覆盖）。绝不打印。"""
    tok = os.environ.get("CLOUD_VIEWER_TOKEN", "").strip()
    if tok:
        return tok
    try:
        with open(ENV_FILE, "r", encoding="utf-8", errors="replace") as f:
            for ln in f:
                m = re.match(r"\s*CLOUD_VIEWER_TOKEN\s*=\s*(\S+)\s*$", ln)
                if m:
                    return m.group(1).strip().strip('"').strip("'")
    except OSError as e:
        print(f"读不到 .env：{e}")
    return ""


def post(base: str, path: str, token: str, payload: dict) -> dict:
    req = urllib.request.Request(
        base + path,
        data=json.dumps(payload, ensure_ascii=False).encode("utf-8"),
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.loads(r.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")
        try:
            d = json.loads(body)
        except Exception:
            d = {"ok": False, "error": f"HTTP {e.code}: {body[:200]}"}
        d.setdefault("ok", False)
        return d


def get(base: str, path: str, token: str) -> dict:
    req = urllib.request.Request(base + path, headers={"Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        return {"ok": False, "error": f"HTTP {e.code}"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", default=DEFAULT_PROFILE)
    ap.add_argument("--class", dest="cls", default="")
    ap.add_argument("--group", default="")
    ap.add_argument("--base-url", default=DEFAULT_BASE)
    ap.add_argument("--dry-run", action="store_true", help="只预览群与目标班，不写库")
    a = ap.parse_args()

    if not os.path.isfile(a.profile):
        print(f"档案不存在：{a.profile}")
        return 2
    with open(a.profile, "r", encoding="utf-8-sig") as f:
        profile = json.load(f)

    token = read_token()
    if not token:
        print("拿不到 CLOUD_VIEWER_TOKEN（.env 或环境变量）")
        return 2

    # ① 目标班
    classes = (get(a.base_url, "/api/classes", token).get("classes") or [])
    if not classes:
        print("云端还没有任何班级 —— 先在管理端建班再恢复课表")
        return 3
    if a.cls:
        hit = next((c for c in classes if c.get("code") == a.cls), None)
        if not hit:
            print(f"云端没有这个班：{a.cls}")
            return 3
    elif len(classes) == 1:
        hit = classes[0]
    else:
        print("云端有多个班，请用 --class 指定：")
        for c in classes:
            print(f"  {c.get('code')}  {c.get('name')}")
        return 3
    code, name = hit.get("code"), hit.get("name")

    # ② 群清单（云端算，客户端不重复实现解析）
    g = post(a.base_url, "/api/class/timetable-groups", token, {"profile": profile})
    if not g.get("ok"):
        print(f"读档案失败：{g.get('error')}")
        return 4
    groups = g.get("groups") or []
    print(f"档案 {os.path.basename(a.profile)} → 课表群 {len(groups)} 个：")
    for x in groups:
        print(f"  - {x.get('name')}  ({x.get('id','')[:8]}…, {x.get('days')} 天)")

    pick = None
    if a.group:
        key = a.group.strip().lower()
        pick = (next((x for x in groups if x.get("id", "").lower() == key), None)
                or next((x for x in groups if x.get("id", "").lower().startswith(key)), None)
                or next((x for x in groups if x.get("name", "").lower() == key), None)
                or next((x for x in groups if key in x.get("name", "").lower()), None))
        if not pick:
            print(f"没有这个课表群：{a.group}")
            return 4
    elif len(groups) == 1:
        pick = groups[0]
    else:
        print("档案含多个课表群，请用 --group 指定（不猜：猜错=把别班课表挂到这个班）")
        return 4

    print(f"\n目标：{name} ({code})  ←  {pick.get('name')}")
    if a.dry_run:
        print("dry-run：不写库")
        return 0

    # ③ 导入
    r = post(a.base_url, "/api/class/timetable-import", token,
             {"classCode": code, "profile": profile, "group": pick.get("id")})
    if not r.get("ok"):
        print(f"导入失败：{r.get('error')}")
        return 5

    # ④ 回读校验（不信回执，信库里读出来的）
    back = post(a.base_url, "/api/class/timetable", token, {"classCode": code})
    prof = back.get("profile") or {}
    days = prof.get("days") or []
    print(f"\n✅ 已写入并回读：群={prof.get('group', {}).get('name')} 天={len(days)} "
          f"节次合计={sum(len(d.get('lessons') or []) for d in days)} "
          f"作息={len(prof.get('layouts') or [])} 份 "
          f"字节={r.get('bytes')} 更新于={back.get('updatedAt')}")
    for d in days:
        subs = [x.get("subject", "") for x in (d.get("lessons") or [])]
        print(f"  {d.get('name')}: {len(subs)} 节  {' / '.join(subs)}")
    return 0 if days else 6


if __name__ == "__main__":
    sys.exit(main())
