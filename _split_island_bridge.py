# -*- coding: utf-8 -*-
"""island_bridge.cpp 域拆分（2026-11-02 · 屎山整理 #2）。

623 行 / 单一类 IslandBridge 的 40+ 个方法堆在一个文件。按职能切三刀：

  island_bridge_history.cpp   历史通知域（排序/筛选/已读/删除，157 行）
  island_bridge_annotate.cpp  批注域（岛上的笔，85 行）
  主文件保留：读侧属性、形态、任务表、鲸鱼、动作路由、sync

**搬家不改逻辑**：整块剪切，函数体一个字不动。

成员访问合法性：三段都是 IslandBridge 的成员函数实现，C++ 允许同一个类的成员函数
在不同 TU 里访问 private 成员（m_host / m_histCategory / m_annotHint 等），不需要
friend 也不需要改头文件可见性。

用法：python _split_island_bridge.py [--apply]
"""
import io
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")

REPO = "Stelarith-control-qt"
SRC = REPO + "/src/island/island_bridge.cpp"
NL = chr(10)

# 簇分配：函数名 → 目标文件
HISTORY = ["historyOrder", "history", "historyUnreadCount",
           "setHistoryCategoryFilter", "setHistoryKeyword",
           "showHistoryItem", "showHistoryById", "markHistoryRead",
           "markAllHistoryRead", "removeHistory", "clearHistory"]
ANNOTATE = ["annotateActive", "annotateTool", "annotatePage",
            "annotateHint", "annotateAction"]


def find_spans(lines):
    """返回 {函数名: (起始下标, 结束下标)}，含紧邻上方的连续 // 注释行。"""
    spans = {}
    for i, l in enumerate(lines):
        m = re.match(r'^[A-Za-z].*\bIslandBridge::(\w+)\(', l)
        if not m:
            continue
        name = m.group(1)
        j = i
        while j < len(lines) and lines[j].rstrip() != "}":
            j += 1
        if j >= len(lines):
            continue
        k = i
        # 往上吞紧邻的注释行（不跨空行），避免注释留在主文件变成孤儿
        while k > 0 and lines[k - 1].lstrip().startswith("//"):
            k -= 1
        spans[name] = (k, j)
    return spans


def main():
    apply_mode = "--apply" in sys.argv
    text = io.open(SRC, encoding="utf-8").read()
    lines = text.split(NL)
    spans = find_spans(lines)
    missing = [n for n in HISTORY + ANNOTATE if n not in spans]
    if missing:
        print("✗ 找不到函数：", missing)
        return 1

    def take(names):
        out, drop = [], set()
        for n in names:
            a, b = spans[n]
            out.append((a, b))
            drop.update(range(a, b + 1))
        return out, drop

    hist_spans, hist_drop = take(HISTORY)
    ann_spans, ann_drop = take(ANNOTATE)

    hist_body = NL.join(NL.join(lines[a:b + 1]).rstrip() for a, b in hist_spans)
    ann_body = NL.join(NL.join(lines[a:b + 1]).rstrip() for a, b in ann_spans)

    print("历史域 %d 个函数 / %d 行" % (len(HISTORY), sum(b - a + 1 for a, b in hist_spans)))
    print("批注域 %d 个函数 / %d 行" % (len(ANNOTATE), sum(b - a + 1 for a, b in ann_spans)))

    drop = hist_drop | ann_drop
    # 删除后清理连续多空行
    kept, blank = [], 0
    for i, l in enumerate(lines):
        if i in drop:
            continue
        if l.strip() == "":
            blank += 1
            if blank > 1:
                continue
        else:
            blank = 0
        kept.append(l)
    new_main = NL.join(kept)
    print("主文件 %d → %d 行" % (len(lines), len(kept)))

    HIST_HEAD = (
        '// 星集控被控端 · 灵动岛桥（历史通知域）\n'
        '// 2026-11-02 从 island_bridge.cpp 按职能切出（600 行红线拆分，搬家不改逻辑）。\n'
        '// 排序/筛选/已读/删除都在这里；★history() 与 showHistoryItem() 必须共用\n'
        '// historyOrder()，否则「看到的第 N 条」≠「点开的第 N 条」（2026-10-13/10-17 两次踩坑）。\n'
        '#include "island_bridge.h"\n'
        '#include "island.h"\n'
        '\n'
        '#include <algorithm>\n'
        '#include <QDateTime>\n'
        '\n')
    ANN_HEAD = (
        '// 星集控被控端 · 灵动岛桥（批注域）\n'
        '// 2026-11-02 从 island_bridge.cpp 按职能切出（600 行红线拆分，搬家不改逻辑）。\n'
        '// ⚠️ 岛上的「笔」是本机直调批注域，不走云端：老师点下去必须立刻有反应，\n'
        '//    绕云端（WS→云→WS）最快几百毫秒且断网就废。云端 annotate_* 留给管理端，\n'
        '//    两条路走同一个域，状态只有一份。\n'
        '#include "island_bridge.h"\n'
        '#include "island.h"\n'
        '#include "../annotate/annotate_domain.h"\n'
        '\n')

    print("")
    if not apply_mode:
        print("（dry-run：加 --apply 落盘）")
        return 0

    io.open(REPO + "/src/island/island_bridge_history.cpp", "w",
            encoding="utf-8", newline=NL).write(HIST_HEAD + hist_body + NL)
    io.open(REPO + "/src/island/island_bridge_annotate.cpp", "w",
            encoding="utf-8", newline=NL).write(ANN_HEAD + ann_body + NL)
    io.open(SRC, "w", encoding="utf-8", newline=NL).write(new_main)
    print("已写：island_bridge_history.cpp / island_bridge_annotate.cpp / island_bridge.cpp")
    return 0


if __name__ == "__main__":
    sys.exit(main())
