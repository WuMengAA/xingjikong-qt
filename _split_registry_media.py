# -*- coding: utf-8 -*-
"""registry.js 拆分 · 第一批：媒体 fan-out 域（2026-11-02 · 屎山整理 #5）。

## 为什么先拆这两块
`registry.js` 1510 行里有 99 个顶层单元。用 `_analyze_module_graph.py` 算依赖后：
`devices` 被 28 处引用、`pushEvent` 被 24 处引用 —— 这两个是全局枢纽，
谁先动谁踩雷。**唯一干净的两个叶子域**是语音对讲（133 行）与屏幕广播（67 行）：
它们只用到 `devices`（遍历下发）和 `pushEvent`（记账），不碰 classes/viewers/results/seq。

## 循环依赖这件事
`registry-media.js` 要从 `registry.js` 取 `pushEvent` 与设备表 —— 这是一个 ESM 循环。
**它是安全的**，前提严格：两边都只有 `export function`（声明提升、活绑定），
且模块顶层**不调用**对方任何东西。搬动脚本会断言这两条：
  ① 被搬的块里没有顶层可执行语句（只有 function / const 字面量）；
  ② registry.js 顶层没有调用被搬函数。
搬完必须跑真回归 —— 审计全绿但 `import` 跑不通，在 class-admin 那次已经吃过一次。

用法：python _split_registry_media.py [--apply]
"""
import io
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-cloud"
MAIN = "src/registry.js"
MEDIA = "src/registry-media.js"
NL = chr(10)

# 区块边界（按 HEAD 行号，1-based，闭区间）：语音对讲 / 屏幕广播
BLOCKS = [
    ("audio", 1310, 1443),
    ("screen", 1444, 1510),
]


def rd(p):
    return io.open(os.path.join(REPO, p), encoding="utf-8").read()


def main():
    apply = "--apply" in sys.argv
    src = rd(MAIN)
    lines = src.split(NL)
    total = len(lines)

    picked = []
    for name, a, b in BLOCKS:
        if b > total:
            print("✗ %s 行号越界（文件 %d 行）" % (name, total))
            return 1
        picked.append((name, a - 1, b, lines[a - 1:b]))

    # ① 顶层可执行性检查：块内除了 function 声明 / 注释 / const 字面量 / 空行，
    #    不允许出现别的顶层语句（顶层调用会在循环里拿到未初始化的绑定）
    bad = []
    for name, _s, _e, body in picked:
        depth = 0
        for ln in body:
            t = ln.strip()
            if not t or t.startswith("//") or t.startswith("*") or t.startswith("/*"):
                continue
            if depth == 0:
                if not re.match(
                    r'^(export\s+)?function\s+\w+'
                    r'|^(const|let|var)\s+\w+\s*='
                    r'|^[})\];]', t):
                    bad.append((name, t[:100]))
            depth += ln.count("{") - ln.count("}")
    if bad:
        print("✗ 块内有非声明顶层语句，不能安全循环：")
        for n, t in bad[:8]:
            print("   [%s] %s" % (n, t))
        return 1
    print("✓ ① 两块内无顶层可执行语句（只有函数声明与字面量常量）")

    # ② 主文件顶层不得调用被搬函数
    moved_names = set()
    for _n, _s, _e, body in picked:
        for ln in body:
            m = re.match(r'^(?:export\s+)?function\s+(\w+)', ln)
            if m:
                moved_names.add(m.group(1))
    print("   将搬走 %d 个函数：%s" % (len(moved_names), sorted(moved_names)))

    kept = [l for i, l in enumerate(lines)
            if not any(s <= i < e for _n, s, e, _b in picked)]
    # 主文件顶层（缩进 0）对被搬函数的调用
    top_calls = []
    for i, ln in enumerate(kept):
        if ln.startswith(" ") or ln.startswith("\t"):
            continue
        for nm in moved_names:
            if re.search(r'\b%s\s*\(' % nm, ln):
                top_calls.append((i + 1, ln.strip()[:100]))
    if top_calls:
        print("✗ ② 主文件顶层调用了被搬函数：")
        for no, t in top_calls[:8]:
            print("   :%d %s" % (no, t))
        return 1
    print("✓ ② 主文件顶层未调用任何被搬函数")

    media_body = []
    for _n, _s, _e, body in picked:
        media_body.extend(body)

    # 主文件里被搬走的块要留一段指向说明，并把导出的函数 re-export 回来，
    # 否则 index.js / class-events.js 等 5 处 import 全断。
    reexport = "export { " + ", ".join(sorted(moved_names)) + " } from './registry-media.js';"
    # devices 是私有 Map，媒体域要遍历下发。不给裸变量而给访问器：
    # 跨模块暴露可变 Map 后谁都能 clear()，一清就全线设备掉线且零报错。
    accessor = NL.join([
        "",
        "// 2026-11-02 拆出 registry-media.js 后新增：媒体 fan-out 需要遍历在线设备表。",
        "// 给访问器而不是 export const devices —— 后者会让任何模块都能 devices.clear()，",
        "// 一清就是全线设备集体掉线且不报错。Map 本身引用语义不变，调用方照样 for...of。",
        "export function registryDevices() { return devices; }",
    ])

    media_txt = NL.join([
        "// 媒体 fan-out：语音对讲 + 屏幕广播（2026-11-02 · 屎山整理 #5，从 registry.js 搬出）",
        "//",
        "// 为什么是这两个：它们是 registry.js 里仅有的两个**叶子域** —— 只用设备表遍历下发",
        "// 与 pushEvent 记账，不碰 classes / viewers / results / seq。搬走它们不动任何状态所有权。",
        "//",
        "// ⚠️ 与 registry.js 构成 ESM 循环导入。安全的两个前提（搬动脚本做了断言）：",
        "//   ① 两边都只有 export function（声明提升 + 活绑定），本文件顶层不调用 registry 任何东西；",
        "//   ② registry.js 顶层也不调用本文件任何东西。任何人在本文件顶层加一句调用，",
        "//      都会在冷启动时拿到 TDZ 错误 —— 那正是「整个云端起不来」的样子，务必别加。",
        "import { wrapOutgoing, makeFrame } from './protocol.js';",
        "import { pushEvent, registryDevices } from './registry.js';",
        "",
        NL.join(media_body).rstrip(),
        "",
    ])

    main_txt = NL.join(kept).rstrip() + NL + accessor + NL + NL + reexport + NL

    print("")
    print("  registry.js        %4d 行 → %4d 行" % (total, len(main_txt.split(NL))))
    print("  registry-media.js  %4d 行（新建）" % len(media_txt.split(NL)))
    # registry.js 本来就在超线白名单里（1510 行），门禁对它只要求「不许变胖」。
    # 所以这里只对**新建**文件卡 600，主文件卡「必须比原来短」。
    if len(main_txt.split(NL)) >= total:
        print("✗ registry.js 没变短（%d → %d）" % (total, len(main_txt.split(NL))))
        return 1
    if len(media_txt.split(NL)) > 600:
        print("✗ registry-media.js %d 行超红线 600" % len(media_txt.split(NL)))
        return 1

    if not apply:
        print(NL + "（dry-run：加 --apply）")
        return 0

    io.open(os.path.join(REPO, MAIN), "w", encoding="utf-8", newline=NL).write(main_txt)
    io.open(os.path.join(REPO, MEDIA), "w", encoding="utf-8", newline=NL).write(media_txt)
    print(NL + "已写入 %s / %s" % (MAIN, MEDIA))
    return 0


if __name__ == "__main__":
    sys.exit(main())
