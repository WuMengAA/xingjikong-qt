#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""从 ClassIsland 档案恢复「班级课表 + 作息」到我们自己的云端（2026-10-25）

背景：CIMS 退役前，课表是 CIMS 的 `cp_files / tl_files / sub_files` 资源集；
退役后课表回落到星集控云端的 `class_timetable`（每班一份 profile）。
本脚本把老师手里的 ClassIsland 官方档案按课表群导入到对应班级，
**归一化在云端做**（`/api/class/timetable-import`），本脚本只负责：
读档案 → 选群 → 发请求 → 回读校验 → 打报告。

为什么不让脚本自己解析：规则只有一处真源（src/class-timetable.js），
脚本再实现一遍就是第二套口径，两端必然漂。

⚠️ 关于「几班」：完整档案里 12 个课表群**全叫「新课表群」**，靠名字分不出谁是谁。
   权威口径（CIMS `docs/phase4-班级课表导入验证报告.md`）= 按 ClassPlans 首次出现顺序
   依次 1班…12班，且报告逐班列出了周一课表。云端 `timetable-groups` 已按此规则回
   `label`（几班）+ `inferred`（是否推断）+ `nameSource`；本脚本**直接用 label**，
   不再自己数顺序 —— 数错一位就是把 3 班课表挂到 4 班。

用法：
    # 整校恢复（1~12 班，缺班自动建）：默认挑 12 群的完整档案
    python scripts/restore-classisland-timetable.py --all --dry-run
    python scripts/restore-classisland-timetable.py --all

    # 单班
    python scripts/restore-classisland-timetable.py --class cls-xxxx --group "1班课表群"
    python scripts/restore-classisland-timetable.py --class cls-xxxx --group 3班

    # 指定档案
    python scripts/restore-classisland-timetable.py --all --profile "D:\\...\\Default.json.pre-stelarith-...json"
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
import urllib.error
import urllib.request

PROFILE_DIR = r"D:\Classlsland\data\Profiles"
# 交付到教室机的那份只剩被用到的群；完整档案在备份里（12 群 × 6 天）。
# 备份命名不带 .json 后缀（Default.json.pre-stelarith-20260915-001850），所以**不能按
# 文件名猜**，要打开数一下有几个群 —— 名字里带 .json 的 glob 会静默选回 2 群的那份。
PROFILE_GLOB = os.path.join(PROFILE_DIR, "Default.json*")
DEFAULT_PROFILE = os.path.join(PROFILE_DIR, "Default.json")
DEFAULT_BASE = "http://localhost:8788"
ENV_FILE = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "Stelarith-cloud", ".env")


def count_groups(path: str) -> int:
    """数这份档案有几个「真有日课表」的课表群；读不动就返回 0。"""
    try:
        with open(path, "r", encoding="utf-8-sig") as f:
            d = json.load(f)
        plans = d.get("ClassPlans") or {}
        seen = set()
        for p in plans.values():
            if isinstance(p, dict) and p.get("Name"):
                seen.add(str(p.get("AssociatedGroup") or ""))
        seen.discard("")
        return len(seen)
    except Exception:
        return 0


def pick_profile(explicit: str, want_all: bool) -> str:
    """--all 选「群最多」的那份档案（12 群）并打印依据；单班模式仍用现役 Default.json。"""
    if explicit:
        return explicit
    if not want_all:
        return DEFAULT_PROFILE
    best, best_n = DEFAULT_PROFILE, count_groups(DEFAULT_PROFILE) if os.path.isfile(DEFAULT_PROFILE) else 0
    for p in sorted(glob.glob(PROFILE_GLOB)):
        if not os.path.isfile(p):
            continue
        n = count_groups(p)
        if n > best_n or (n == best_n and os.path.getmtime(p) > os.path.getmtime(best)):
            best, best_n = p, n
    if best != DEFAULT_PROFILE:
        print(f"选用完整档案：{os.path.basename(best)}（{best_n} 个课表群）")
    return best


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
        with urllib.request.urlopen(req, timeout=60) as r:
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


def import_one(base: str, token: str, profile: dict, group: dict, code: str) -> dict:
    """导一个群到指定班，并回读校验（不信回执，信库里读出来的）。"""
    r = post(base, "/api/class/timetable-import", token,
             {"classCode": code, "profile": profile, "group": group.get("id") or group.get("label")})
    if not r.get("ok"):
        return {"ok": False, "error": r.get("error"), "label": group.get("label")}

    back = post(base, "/api/class/timetable", token, {"classCode": code})
    prof = back.get("profile") or {}
    days = prof.get("days") or []
    mon = next((d for d in days if d.get("dow") == 1), None)
    lessons = mon.get("lessons") if mon else None
    first = lessons[0] if lessons else {}
    g = prof.get("group") or {}
    return {
        "ok": bool(days),
        "label": group.get("label"),
        "group": g.get("label") or g.get("name"),
        "days": len(days),
        "total": sum(len(d.get("lessons") or []) for d in days),
        "mon": len(lessons or []),
        "firstStart": first.get("start", ""),
        "firstEnd": first.get("end", ""),
        "firstSubject": first.get("subject", ""),
        "layouts": len(prof.get("layouts") or []),
        "bytes": r.get("bytes"),
    }


def run_all(base: str, token: str, profile: dict, groups: list, classes: list,
            dry_run: bool, create_missing: bool) -> int:
    by_name = {str(c.get("name") or "").strip(): c for c in classes}
    plan, errors = [], []
    for g in groups:
        label = str(g.get("label") or "").strip()
        if not label:
            errors.append(f"群 {g.get('id','')[:8]} 没有班名标签")
            continue
        hit = by_name.get(label)
        plan.append((label, g, hit.get("code") if hit else "", bool(hit)))

    print(f"\n整校恢复计划（{len(plan)} 个班）：")
    for label, g, code, exists in plan:
        src = "复用 " + code if exists else ("新建" if create_missing else "缺班·跳过")
        print(f"  {label:<5} ← 群 {g.get('id','')[:8]}… ({g.get('days')} 天)   {src}")
    if dry_run:
        print("\ndry-run：不写库")
        return 0

    ok_rows, fail_rows = [], []
    for label, g, code, exists in plan:
        if not exists:
            if not create_missing:
                fail_rows.append((label, "班级不存在且未允许新建"))
                continue
            c = post(base, "/api/class/create", token, {"className": label})
            if not c.get("ok"):
                fail_rows.append((label, f"建班失败：{c.get('error')}"))
                continue
            code = c.get("classCode")
            print(f"  + 已建班 {label} → {code}")
        r = import_one(base, token, profile, g, code)
        if r.get("ok"):
            ok_rows.append(r)
        else:
            fail_rows.append((label, r.get("error") or "导入后回读为空"))

    print("\n结果：")
    print(f"  {'班':<5}{'群':<8}{'天':<4}{'节':<5}{'周一':<16}{'作息':<5}字节")
    for r in ok_rows:
        print(f"  {str(r['label']):<5}{str(r['group']):<8}{r['days']:<4}{r['total']:<5}"
              f"{r['firstSubject'] + ' ' + r['firstStart'] + '-' + r['firstEnd']:<16}"
              f"{r['layouts']:<5}{r.get('bytes')}")
    # 该校作息固定：周一第 1 节 07:25-08:05、全周 59 节 —— 对不上就是导错了群
    wrong = [r for r in ok_rows if r["firstStart"] != "07:25" or r["total"] != 59]
    good = [r for r in ok_rows if r not in wrong]
    print(f"\n  ✅ {len(good)} 班课表与作息对齐（周一第1节 07:25-08:05、全周 59 节）")
    if wrong:
        print(f"  ⚠️ {len(wrong)} 班数据不符预期：")
        for r in wrong:
            print(f"     {r['label']}: 周一第1节 {r['firstStart']}-{r['firstEnd']}，全周 {r['total']} 节")
    if fail_rows:
        print(f"  ✗ {len(fail_rows)} 班失败：")
        for label, why in fail_rows:
            print(f"     {label}: {why}")
    return 0 if not fail_rows and not wrong else 5


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--profile", default="")
    ap.add_argument("--class", dest="cls", default="")
    ap.add_argument("--group", default="")
    ap.add_argument("--base-url", default=DEFAULT_BASE)
    ap.add_argument("--all", action="store_true", help="整校恢复：每个课表群 → 同名班级（1班…12班）")
    ap.add_argument("--no-create", action="store_true", help="--all 时缺班不新建，只报出来")
    ap.add_argument("--dry-run", action="store_true", help="只预览群与目标班，不写库")
    a = ap.parse_args()

    path = pick_profile(a.profile, a.all)
    if not os.path.isfile(path):
        print(f"档案不存在：{path}")
        return 2
    with open(path, "r", encoding="utf-8-sig") as f:
        profile = json.load(f)

    token = read_token()
    if not token:
        print("拿不到 CLOUD_VIEWER_TOKEN（.env 或环境变量）")
        return 2

    # 群清单（云端算，客户端不重复实现解析；label 已按首次出现序推断出「几班」）
    g = post(a.base_url, "/api/class/timetable-groups", token, {"profile": profile})
    if not g.get("ok"):
        print(f"读档案失败：{g.get('error')}")
        return 4
    groups = g.get("groups") or []
    print(f"档案 {os.path.basename(path)} → 课表群 {len(groups)} 个"
          f"（班名来源：{g.get('nameSource')}）")
    for x in groups:
        tag = "（推断，请核对）" if x.get("inferred") else ""
        print(f"  - {x.get('label')} ← {x.get('name')}  "
              f"({x.get('id','')[:8]}…, {x.get('days')} 天){tag}")

    classes = (get(a.base_url, "/api/classes", token).get("classes") or [])

    if a.all:
        return run_all(a.base_url, token, profile, groups, classes,
                       a.dry_run, not a.no_create)

    # ── 单班模式 ──
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
        print("云端有多个班，请用 --class 指定（或用 --all 整校恢复）：")
        for c in classes:
            print(f"  {c.get('code')}  {c.get('name')}")
        return 3
    code, name = hit.get("code"), hit.get("name")

    pick = None
    if a.group:
        key = a.group.strip().lower()
        pick = (next((x for x in groups if x.get("id", "").lower() == key), None)
                or next((x for x in groups if x.get("id", "").lower().startswith(key)), None)
                or next((x for x in groups if str(x.get("label", "")).lower() == key), None)
                or next((x for x in groups if x.get("name", "").lower() == key), None)
                or next((x for x in groups if key in x.get("name", "").lower()), None))
        if not pick:
            print(f"没有这个课表群：{a.group}")
            return 4
    elif len(groups) == 1:
        pick = groups[0]
    else:
        print("档案含多个课表群，请用 --group 指定（或用 --all 整校恢复）")
        return 4

    print(f"\n目标：{name} ({code})  ←  {pick.get('label')}（{pick.get('name')}）")
    if a.dry_run:
        print("dry-run：不写库")
        return 0

    r = import_one(a.base_url, token, profile, pick, code)
    if not r.get("ok"):
        print(f"导入失败：{r.get('error')}")
        return 5
    print(f"\n✅ 已写入并回读：群={r.get('group')} 天={r.get('days')} 节次合计={r.get('total')} "
          f"作息={r.get('layouts')} 份 字节={r.get('bytes')}")
    print(f"   周一第1节 {r.get('firstSubject')} {r.get('firstStart')}-{r.get('firstEnd')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
