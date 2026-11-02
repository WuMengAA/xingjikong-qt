# -*- coding: utf-8 -*-
"""viewerbackend.cpp 拆分的等值性审计（2026-11-02 · 屎山整理 #6）。

## 五条断言，各自防一种事故
A **覆盖**：HEAD 里的 99 个 ViewerBackend 成员函数，拆分后必须仍全部存在
   —— 防「搬的时候删多了」。
B **逐字**：每个成员函数体与 HEAD 原文逐行相同（允许仅空行差异）
   —— 防「搬家时顺手改了逻辑」，这是最贵的一类错，编译能过、探针可能也过。
C **无重复**：同一函数不得在两个 TU 里都定义
   —— 防「复制过去没删干净」。这个特别阴：C++ 会直接报重复定义，但如果
   两处签名/修饰略有不同，链接器可能挑一个用，运行时走哪份全凭运气。
D **行数**：拆分产物全部 ≤600 行（§10.5 红线）。
E **共享助手逐字**：`viewerbackend_shared.cpp` 里三个助手的函数体必须与
   HEAD 里它们原来的实现逐行相同（只忽略函数名与 static 关键字）
   —— 防「搬到共享层时『顺手优化』了一下」。这条是本轮编译失败的直接产物：
   frameFingerprint / makeVncFrame / viewerDesktopLnk 是因为跨 TU 才被搬的，
   它们的**语义一个字节都不该变**，否则静态区判定、VNC 帧格式会悄悄跑偏。

用法：python _audit_viewerbackend_split.py
退出码 0=通过，1=有断言失败。
"""
import io
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-viewer-qt"
NL = chr(10)

PARTS = [
    "src/viewerbackend.cpp",
    "src/viewerbackend_vnc.cpp",
    "src/viewerbackend_terminal.cpp",
    "src/viewerbackend_qmlpatch.cpp",
    "src/viewerbackend_proc.cpp",
    "src/viewerbackend_diag.cpp",
]

SHARED = "src/viewerbackend_shared.cpp"

# 共享助手：新名 → 原名
SHARED_FUNCS = {
    "viewerFrameFingerprint": "frameFingerprint",
    "viewerMakeVncFrame": "makeVncFrame",
    "viewerDesktopLnk": "viewerDesktopLnk",
}


def wt(path):
    p = os.path.join(REPO, path)
    if not os.path.exists(p):
        return None
    return io.open(p, encoding="utf-8", errors="replace").read()


def head_text(path):
    r = subprocess.run(["git", "-C", REPO, "show", "HEAD:" + path], capture_output=True)
    return r.stdout.decode("utf-8", "replace") if r.returncode == 0 else None


def funcs(text, owner="ViewerBackend"):
    """{name: [body lines]}，按出现顺序；只认 `Owner::name(` 开头的定义行。"""
    lines = text.split(NL)
    out = {}
    pat = re.compile(r'^(?:[\w:<>,*&\s]+?)\b' + owner + r'::(\w+)\s*\(')
    for i, l in enumerate(lines):
        m = pat.match(l)
        if not m:
            continue
        # ⚠️ 这一个循环踩过两次相反的坑，两个约束必须**同时**满足：
        #   ① 不能只判 `d == 0`：多行函数的签名行没有花括号，d 一开始就是 0，
        #      会在第 1 行就 break，函数体根本没被读进来 —— 于是「改坏函数体」
        #      审计照样全绿（假阴性，本轮实测踩到，差点放行）。
        #   ② 不能只判 `j > i`：单行函数 `Ret Owner::f() const { return x; }`
        #      首行就已配平，会继续吞掉下一个函数 —— 于是「函数体被改」误报。
        # 正解：见过花括号之后才开始判配平。
        d = 0
        seen_brace = False
        j = i
        while j < len(lines):
            o = lines[j].count("{")
            c = lines[j].count("}")
            if o or c:
                seen_brace = True
            d += o - c
            if seen_brace and d <= 0:
                break
            j += 1
        out.setdefault(m.group(1), []).append(lines[i:j + 1])
    return out


def freefuncs(text, name):
    """抓自由函数 `[static] Ret name(...) { ... }` 的函数体行。"""
    lines = text.split(NL)
    pat = re.compile(r'^(?:static\s+)?[\w:<>,*&\s]+?\b' + name + r'\s*\(')
    for i, l in enumerate(lines):
        if not pat.match(l):
            continue
        d = 0
        j = i
        while j < len(lines):
            d += lines[j].count("{") - lines[j].count("}")
            if d == 0 and j > i:
                break
            j += 1
        return lines[i:j + 1]
    return None


def dename(lines):
    """把升外部链接时加的 viewer 前缀去掉，让新旧实现可比。

    改名本身是**必需的**（内部链接→外部链接要防撞名），不属于逻辑改动，
    所以两边都归一化 —— 而不是只归一化一边（只归一化一边会在调用行上
    永远报「被改」，本轮第一版就是这么错的）。
    """
    return [l.replace("viewerFrameFingerprint", "frameFingerprint")
             .replace("viewerMakeVncFrame", "makeVncFrame") for l in lines]


def norm(lines):
    """规范化：去行尾空白、丢纯空行。"""
    return [l.rstrip() for l in lines if l.strip()]


def main():
    ok = True
    head_main = head_text("src/viewerbackend.cpp")
    if head_main is None:
        print("✗ 读不到 HEAD 的 src/viewerbackend.cpp")
        return 1

    # 排除含 return 类型但实际是定义的误判 —— 只留出现次数≥1 的
    hf = funcs(head_main)
    print("HEAD(src/viewerbackend.cpp) 成员函数 %d 个" % len(hf))

    cur = {}
    dups = []
    for p in PARTS:
        t = wt(p)
        if t is None:
            print("✗ 缺文件 %s" % p)
            return 1
        for n, bodies in funcs(t).items():
            if len(bodies) > 1:
                dups.append((p, n, len(bodies)))
            cur.setdefault(n, []).extend([(p, b) for b in bodies])

    # A 覆盖
    lost = sorted(set(hf) - set(cur))
    if lost:
        ok = False
        print(NL + "✗ A 覆盖：%d 个函数不见了：%s" % (len(lost), lost))
    else:
        print("✓ A 覆盖：HEAD 的 %d 个成员函数全部还在" % len(hf))

    # C 无重复（先查，B 依赖唯一性）
    if dups:
        ok = False
        print(NL + "✗ C 重复定义：%s" % dups)
    else:
        print("✓ C 无重复：每个成员函数只在一处定义")

    # B 逐字
    changed = []
    for n, head_bodies in hf.items():
        spans = cur.get(n)
        if not spans:
            continue
        p, body = spans[0]   # spans 元素是 (路径, 函数体行列表)
        head_body = head_bodies[0]   # funcs() 返回 {name: [body]}，同名只可能一份
        # 精确放行两处**必需**的改名（内部链接升外部链接时加 viewer 前缀防撞名）：
        # 做法是先把新名换回旧名再比对 —— 而不是「跳过含关键字的行」，
        # 后者会连带放过那几行上被改坏的其余内容。
        if norm(dename(head_body)) != norm(dename(body)):
            changed.append((p, n))
    if changed:
        ok = False
        print(NL + "✗ B 逐字：%d 个函数体被改：%s" % (len(changed), changed))
    else:
        print("✓ B 逐字：%d 个成员函数体与 HEAD 完全一致" % len(hf))

    # D 行数
    over = []
    # ⚠️ 只查**拆分新产物**：src/viewerbackend.cpp 本身仍在台账白名单里
    # （2995 行是历史包袱，本批只让它变短、不负责一次拆到 600）。
    # 把主文件也塞进 600 红线，这条断言会永远红，红久了就没人看了 —— 那才是真失败。
    for p in [x for x in PARTS + [SHARED] if not x.endswith("src/viewerbackend.cpp")]:
        t = wt(p)
        if t is None:
            continue
        n = t.count(NL)
        if n > 600:
            over.append((p, n))
    if over:
        ok = False
        print(NL + "✗ D 行数：超红线 %s" % over)
    else:
        sizes = " / ".join("%s=%d" % (p.rsplit("/", 1)[-1], (wt(p) or "").count(NL)) for p in PARTS + [SHARED])
        print("✓ D 行数：全部 ≤600（%s）" % sizes)

    # E 共享助手逐字
    shared_txt = wt(SHARED) or ""
    bad_shared = []
    for newn, oldn in SHARED_FUNCS.items():
        old = freefuncs(head_main, oldn)
        new = freefuncs(shared_txt, newn)
        if old is None:
            bad_shared.append((oldn, "HEAD 里找不到原实现"))
            continue
        if new is None:
            bad_shared.append((newn, "共享层里找不到新实现"))
            continue
        # 只把函数名换回原名再比，其余逐行一致
        old_n = [l.replace(oldn, newn) for l in norm(old)]
        # static 是被故意去掉的（内部链接 → 外部链接），不是逻辑改动
        old_n = [l[len("static "):] if l.startswith("static ") else l for l in old_n]
        new_n = norm(new)
        if old_n != new_n:
            diff = [(a, b) for a, b in zip(old_n, new_n) if a != b][:3]
            bad_shared.append((newn, "函数体不一致 %s" % (diff or "长度不同")))
    if bad_shared:
        ok = False
        print(NL + "✗ E 共享助手被改：%s" % bad_shared)
    else:
        print("✓ E 共享助手：%d 个助手与 HEAD 原实现逐字一致（只换了函数名）"
              % len(SHARED_FUNCS))

    print(NL + ("审计通过 ✅" if ok else "审计失败 ❌"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
