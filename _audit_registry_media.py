# -*- coding: utf-8 -*-
"""registry 媒体域拆分审计（2026-11-02 · 屎山整理 #5）。

JS 拆分最阴的错误不是"语法错"（node --check 能抓），而是：
  · 少搬一个函数 → 运行时 import 报 "does not provide an export"，整个云端起不来；
  · 多搬一份（复制没删） → 两边各有一份，调用方走到哪份全凭引用关系，行为不定；
  · 搬的时候手滑改了实现 → 只有真发一帧语音/屏幕才会看见。

所以三项机器断言 + 两项循环安全断言：

  A. 覆盖：HEAD registry.js 里每个顶层 function，在「主文件 + registry-media.js」
     合起来恰好出现一次。
  B. 逐字：每个被搬函数的函数体逐行与 HEAD 相同（只允许行尾空白差异）。
  C. 无重复：同一函数名不得两处定义。
  D. 循环安全①：registry-media.js 顶层不得出现任何函数调用（除了 import 与声明）——
     顶层调用会在 ESM 循环里拿到 TDZ 错误。
  E. 循环安全②：registry.js 顶层不得调用 registry-media 导出的函数。

用法：python _audit_registry_media.py
"""
import io
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-cloud"
ORIG = "src/registry.js"
PARTS = ["src/registry.js", "src/registry-media.js"]
NL = chr(10)

MEDIA_EXPORTS = [
    "broadcastAudioToAgents", "broadcastScreenToAgents", "duplexActive",
    "isAudioBroadcasting", "isScreenBroadcasting", "sendAudioToTeacher",
    "setAudioBroadcasting", "setScreenBroadcasting",
]


def head(p):
    r = subprocess.run(["git", "-C", REPO, "show", "HEAD:" + p], capture_output=True)
    return r.stdout.decode("utf-8") if r.returncode == 0 else None


def wt(p):
    return io.open(os.path.join(REPO, p), encoding="utf-8").read()


def funcs(text):
    """返回 {name: [body_lines...]}；只认顶层声明（行首无缩进）。"""
    out = {}
    lines = text.split(NL)
    for i, l in enumerate(lines):
        m = re.match(r'^(?:export\s+)?function\s+(\w+)\s*\(', l)
        if not m:
            continue
        d = 0
        j = i
        while j < len(lines):
            d += lines[j].count("{") - lines[j].count("}")
            if d == 0 and j > i:
                break
            j += 1
        out.setdefault(m.group(1), []).append(lines[i:j + 1])
    return out


def norm(body):
    return [x.rstrip() for x in body if x.strip()]


def top_statements(text):
    """顶层（缩进 0）非声明、非注释的行 —— 这些是循环导入里最危险的东西。"""
    out = []
    for no, ln in enumerate(text.split(NL), 1):
        if not ln or ln[0] in " \t}])":
            continue
        t = ln.strip()
        if not t or t.startswith("//") or t.startswith("*") or t.startswith("/*"):
            continue
        if re.match(r'^(import|export)\b', t):
            continue
        if re.match(r'^(function|const|let|var|class)\b', t):
            continue
        out.append((no, t[:110]))
    return out


def main():
    ref_txt = head(ORIG)
    if ref_txt is None:
        print("✗ 读不到 HEAD:" + ORIG)
        return 1
    ref = funcs(ref_txt)
    print("HEAD %s：%d 行 / %d 个顶层函数" % (ORIG, len(ref_txt.split(NL)), len(ref)))

    now = {}
    for p in PARTS:
        txt = wt(p)
        fs = funcs(txt)
        print("  %-24s %4d 行 %3d 函数" % (p.split("/")[-1], len(txt.split(NL)), len(fs)))
        for k, v in fs.items():
            now.setdefault(k, []).extend([(p, b) for b in v])

    ok = True

    missing = [k for k in ref if k not in now]
    if missing:
        ok = False
        print(NL + "✗ A 丢失 %d 个函数：%s" % (len(missing), sorted(missing)[:12]))
    else:
        print(NL + "✓ A 覆盖：HEAD 的 %d 个顶层函数全部还在" % len(ref))

    dup = {k: [p for p, _b in v] for k, v in now.items() if len(v) > 1}
    if dup:
        ok = False
        print("✗ C 重复定义 %d：%s" % (len(dup), list(dup.items())[:5]))
    else:
        print("✓ C 无重复：每个函数只在一处定义")

    bad = []
    for k, v in ref.items():
        if k not in now or len(now[k]) != 1:
            continue
        if norm(now[k][0][1]) != norm(v[0]):
            bad.append((k, now[k][0][0]))
    if bad:
        ok = False
        print(NL + "✗ B 函数体被改 %d：" % len(bad))
        for k, p in bad[:8]:
            print("   %-28s in %s" % (k, p.split("/")[-1]))
        k, _p = bad[0]
        a, b = norm(ref[k][0]), norm(now[k][0][1])
        for x, y in zip(a, b):
            if x != y:
                print("   首个差异：" + NL + "     HEAD: " + x[:130]
                      + NL + "     现在: " + y[:130])
                break
    else:
        print("✓ B 逐字：%d 个函数体与 HEAD 完全一致" % len(ref))

    # F. 顶层常量：脚本只搬函数时会漏掉同文件里的 const（PUBLISH_TYPES 就是这么丢的）
    ref_consts = set(re.findall(r'^(?:const|let|var)\s+(\w+)\s*=', ref_txt, re.M))
    now_consts = set()
    for p in PARTS:
        now_consts |= set(re.findall(r'^(?:const|let|var)\s+(\w+)\s*=',
                                     wt(p), re.M))
    lost_consts = ref_consts - now_consts
    if lost_consts:
        ok = False
        print(NL + "✗ F 顶层常量丢失：%s" % sorted(lost_consts))
    else:
        print("✓ F 顶层常量：HEAD 的 %d 个 const/let/var 全在" % len(ref_consts))

    # D. 循环安全①
    ts = top_statements(wt("src/registry-media.js"))
    if ts:
        ok = False
        print(NL + "✗ D registry-media.js 顶层有可执行语句（ESM 循环下会 TDZ 崩）：")
        for no, t in ts[:8]:
            print("   :%d %s" % (no, t))
    else:
        print("✓ D 循环安全①：registry-media.js 顶层只有 import 与声明")

    # E. 循环安全②
    main_txt = wt(ORIG)
    calls = []
    for no, ln in enumerate(main_txt.split(NL), 1):
        if not ln or ln[0] in " \t}])":
            continue
        for nm in MEDIA_EXPORTS:
            if re.search(r'\b%s\s*\(' % nm, ln):
                calls.append((no, nm, ln.strip()[:100]))
    if calls:
        ok = False
        print(NL + "✗ E registry.js 顶层调用了 registry-media 的函数：")
        for no, nm, t in calls[:8]:
            print("   :%d [%s] %s" % (no, nm, t))
    else:
        print("✓ E 循环安全②：registry.js 顶层未调用媒体域函数")

    print("")
    for p in PARTS:
        n = len(wt(p).split(NL))
        print("  %-24s %4d 行" % (p.split("/")[-1], n))

    print(NL + ("审计通过 ✅" if ok else "审计失败 ❌"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
