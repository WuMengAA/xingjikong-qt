#!/usr/bin/env python3
"""语音对讲 L2 现场验证 · 云端会话统计（只读，不改任何东西）。

数据来源：云端 events.log（JSONL）。语音会话**结束时**云端一次性入账：
    {"msg":"语音对讲结束","seconds":N,"frames":N,"kib":N,"uplinkFrames":N,"uplinkKib":N}
据此可算出：
    · 平均包长 = kib*1024/frames   ← **判定 Opus 是否真的生效的硬指标**
        裸 PCM：640 B/帧（20ms@16k/mono/16bit） → 平均包长 ≈ 640±5
        Opus  ：31–107 B/包（16 kbps 档典型 ~40）→ 平均包长 ≈ 40±20
    · 下行帧率  = frames / seconds（应 ≈ 50，明显偏低说明采集/发送被拖慢）
    · 出口码率  = kib*8/seconds（kbps）—— 这是"60 台 fan-out 会不会炸"的原始量

用法：
    python audio_session_report.py                 # 默认读生产工作区 events.log
    python audio_session_report.py <events.log> [更多文件…]
    python audio_session_report.py --since 2026-10-22T00:00:00

退出码：0 = 至少有一条会话且全部判定 OK；1 = 有会话但存在异常；2 = 没找到会话记录。
"""
from __future__ import annotations

import glob
import json
import os
import sys

DEFAULT = r"D:\Stelarith\Stelarith-xingjikong-qt\Stelarith-cloud\events.log"

# 判定阈值（与方案 §5 P1 验收口径一致）
PCM_BYTES_PER_FRAME = 640
PCM_TOLERANCE = 40          # 640±40 都算"确实是裸 PCM"
OPUS_MAX_BYTES = 120        # Opus 16kbps 档单包上限（含偶发大会话包）


def collect(paths: list[str]) -> list[dict]:
    rows: list[dict] = []
    for p in paths:
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        ev = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if ev.get("msg") != "语音对讲结束":
                        continue
                    if "frames" not in ev:      # 没开就收到 stop 的空记录
                        continue
                    ev["_file"] = os.path.basename(p)
                    rows.append(ev)
        except FileNotFoundError:
            continue
    rows.sort(key=lambda r: r.get("at", ""))
    return rows


def judge(pkt: float, fps: float, frames: int) -> tuple[str, str]:
    # ⚠️ frames=0（老师点了开始但没说话/没发帧）**不能算通过**——没有样本就没有结论。
    if frames <= 2:
        return "无数据 ⚪", "整段只有 ≤2 帧，样本不足，重测（说满 5 秒以上）"
    if pkt <= OPUS_MAX_BYTES:
        verdict = "OPUS ✅"
        note = "平均包长落在 Opus 区间"
    elif abs(pkt - PCM_BYTES_PER_FRAME) <= PCM_TOLERANCE:
        verdict = "PCM ⚠️"
        note = "平均包长≈640B ⇒ 这一端仍在发裸 PCM（旧版本 / 编码失败回落）"
    else:
        verdict = "未知 ❓"
        note = f"平均包长 {pkt:.0f}B 不在 PCM/Opus 任一区间，需查帧头或抓包"
    if fps < 35:
        note += f"；帧率 {fps:.1f}/s 明显低于 50（采集被拖慢或丢帧）"
    return verdict, note


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    since = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--since=")), None)

    paths = args or [DEFAULT]
    if not args:
        # 生产上 events.log 会轮转，带时间戳的归档一起读
        paths = [DEFAULT] + sorted(glob.glob(DEFAULT + ".*"))
    rows = collect(paths)
    if since:
        rows = [r for r in rows if r.get("at", "") >= since]

    if not rows:
        print(f"没找到带统计的「语音对讲结束」记录（读的是: {', '.join(paths)}）")
        print("提示：要先在老师机点「开始讲话」→ 说几句 → 点「停止」，云端才会入账一条。")
        return 2

    print(f"共 {len(rows)} 次语音会话（按时间升序）\n")
    print(f"{'结束时刻':<21}{'时长':>6}{'下行帧率':>9}{'下行包长':>9}{'下行码率':>9}"
          f"{'上行帧率':>9}{'上行包长':>9}  判定")
    print("-" * 100)

    bad = 0
    noData = 0
    for r in rows:
        sec = max(1, int(r.get("seconds") or 1))
        fr = int(r.get("frames") or 0)
        kib = float(r.get("kib") or 0)
        ufr = int(r.get("uplinkFrames") or 0)
        ukib = float(r.get("uplinkKib") or 0)

        fps = fr / sec
        pkt = (kib * 1024 / fr) if fr else 0.0
        kbps = (kib * 8 / sec) if sec else 0.0
        ufps = ufr / sec
        upkt = (ukib * 1024 / ufr) if ufr else 0.0

        verdict, note = judge(pkt, fps, fr)
        if "⚠️" in verdict or "❓" in verdict:
            bad += 1
        elif "⚪" in verdict:
            noData += 1

        at = (r.get("at") or "")[:19].replace("T", " ")
        print(f"{at:<21}{sec:>5}s{fps:>8.1f}{pkt:>8.0f}B{kbps:>8.1f}k"
              f"{ufps:>9.1f}{upkt:>8.0f}B  {verdict}")
        if note:
            print(f"{'':<21}└ {note}")

    print("\n════ 判定口径 ════")
    print(f"  下行包长 ≤{OPUS_MAX_BYTES}B = Opus 生效；≈{PCM_BYTES_PER_FRAME}B = 仍是裸 PCM")
    print("  下行帧率应 ≈ 50/s（20ms 一帧）；持续 <35 说明采集/发送被拖慢")
    print("  60 台 fan-out 出口 ≈ 下行码率 × 60（Opus 16k 期望 ≤2 Mbps）")
    print(f"\n结论：{len(rows) - bad - noData} 次正常 / {bad} 次异常 / {noData} 次样本不足")
    return 0 if (bad == 0 and noData == 0) else 1


if __name__ == "__main__":
    sys.exit(main())
