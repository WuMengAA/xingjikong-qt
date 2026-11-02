# -*- coding: utf-8 -*-
"""class-admin 拆分等价性审计（2026-11-02 · 屎山整理 #4）。

JS 没有编译期，搬错一个 `await`、漏一个 `return` 都是运行时才炸的静默错误。
所以四项断言全部是**机器逐字比对**：

  A. 覆盖：HEAD 的 class-admin.js 里每个 `function X(`，在「主文件 + 4 个域文件」
     合起来**恰好出现一次**。
  B. 逐字：每个函数体逐行与 HEAD 相同（含注释；只允许行尾空白与空行差异）。
  C. 无重复：同一函数不得在两处定义。
  D. 导出面：HEAD 里所有 `export function X` 的名字，新代码里必须仍可从
     `class-admin.js` 导入（index.js 只 import 了 2 个，但别的调用方/测试可能更多，
     拆文件不该逼调用方改）。用正则抓 re-export 块的名字集合核对。

用法：python _audit_classadmin_split.py
"""
import io
import os
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-cloud"
ORIG = "src/class-admin.js"
PARTS = [
    "src/class-admin.js",
    "src/class-admin-shared.js",
    "src/class-admin-classes.js",
    "src/class-admin-students.js",
    "src/class-admin-attendance.js",
]
NL = chr(10)


def read_head(p):
    r = subprocess.run(["git", "-C", REPO, "show", "HEAD:" + p], capture_output=True)
    return r.stdout.decode("utf-8") if r.returncode == 0 else None


def read_wt(p):
    return io.open(os.path.join(REPO, p), encoding="utf-8").read()


def funcs(text):
    lines = text.split(NL)
    out = {}
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


def main():
    head = read_head(ORIG)
    if head is None:
        print("✗ 读不到 HEAD:" + ORIG)
        return 1
    ref = funcs(head)
    print("HEAD %s：%d 行，%d 个函数" % (ORIG, len(head.split(NL)), len(ref)))

    now = {}
    for p in PARTS:
        txt = read_wt(p)
        fs = funcs(txt)
        print("  %-34s %4d 行 %2d 函数"
              % (p.split("/")[-1], len(txt.split(NL)), len(fs)))
        for k, v in fs.items():
            now.setdefault(k, []).extend([(p, b) for b in v])

    ok = True
    missing = [k for k in ref if k not in now]
    if missing:
        ok = False
        print(NL + "✗ A 丢失函数 %d：%s" % (len(missing), missing))
    else:
        print(NL + "✓ A 覆盖：HEAD 的 %d 个函数全部还在" % len(ref))

    dup = {k: [p for p, _ in v] for k, v in now.items() if len(v) > 1}
    if dup:
        ok = False
        print("✗ C 重复定义 %d：%s" % (len(dup), dup))
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
        for k, p in bad:
            print("   %-26s in %s" % (k, p.split("/")[-1]))
        # 打首个差异行，方便定位
        k, _p = bad[0]
        a, b = norm(ref[k][0]), norm(now[k][0][1])
        for x, y in zip(a, b):
            if x != y:
                print("   首个差异：" + NL + "     HEAD: " + x[:140]
                      + NL + "     现在: " + y[:140])
                break
    else:
        print("✓ B 逐字：%d 个函数体与 HEAD 完全一致" % len(ref))

    # D 导出面
    exported = set(re.findall(r'^export\s+function\s+(\w+)', head, re.M))
    main_txt = read_wt(ORIG)
    reexported = set()
    # 两种导出形态都要认：
    #   `export { a, b } from './x.js'`  —— 转出别处符号（不引入本地作用域）
    #   `export { a, b };`               —— 转出本文件已有的绑定（import 进来的也算）
    # 首版只认前者，改成本地绑定再导出后 D 项误报 27 个；这个误报本身是对的提醒：
    # 「导出面」是 API 契约，审计必须跟着语法一起演进，不能固化成一种写法。
    for block in re.findall(r'export\s*\{([^}]*)\}\s*(?:from\s*[\'"][^\'"]+[\'"])?', main_txt, re.S):
        for n in block.split(","):
            n = n.strip().split(" as ")[-1].strip()
            if n:
                reexported.add(n)
    # 直接在本文件 `export function` 定义的同样算「仍可导入」—— 规则不能只认 re-export，
    # 否则 handleClassAdminApi（留在主文件的路由分发）会被误判成丢失。
    still_defined = set(re.findall(r'^export\s+function\s+(\w+)', main_txt, re.M))
    available = reexported | still_defined
    lost = exported - available
    if lost:
        ok = False
        print(NL + "✗ D 导出面丢失 %d：%s（index.js 或测试会 import 失败）"
              % (len(lost), sorted(lost)))
    else:
        print("✓ D 导出面：HEAD 的 %d 个 export 全部仍可从 class-admin.js 导入"
              % len(exported))

    print("")
    for p in PARTS:
        n = len(read_wt(p).split(NL))
        print("  %-34s %4d 行 / 红线 600  %s"
              % (p.split("/")[-1], n, "OK" if n <= 600 else "超红线"))
        if n > 600:
            ok = False

    print(NL + ("审计通过 ✅" if ok else "审计失败 ❌"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
