# 加减归档 · 灵动岛顶替 ClassIsland 第 1 步：EndTime 绝对结束时刻（2026-11-01）

> 提交：`d2e8ca6`（control-qt）｜ 根仓指针 `5cd33df` ｜ 净 **+70 / −0**
> 用户点单：「星集控被控端灵动岛 class island 顶替，源码学习」→「**不移植，不复制，开工**」
> 口径：**只学机制，代码全部自有实现**（ClassIsland 为 GPL-3.0，未复制其任何代码）。

---

## 1. 新增文件

无。

## 2. 修改文件

| 文件 | 行数 | 改了什么 |
|---|---|---|
| `src/island.h` | +12 | 新增 `setEndTimeForNext(qint64 epochMs)` 一次性注入位 + 成员 `m_endHintMs`（照 `setChannelForNext` 既有范式） |
| `src/island.cpp` | +9 | `showIsland` 入口消费：EndTime 优先于 `durationMs`，换算成剩余时长走既有自动收起链 |
| `src/notify/notify_entry.cpp` | +6 | 云端路径支持 `flags.endAt`（epoch 毫秒），只对**纯展示**生效 |
| `scripts/_island_qml_probe.cpp` | +43 | 新增探针模式 `--endat <msFromNow>` |

## 3. 删除文件

无。

## 4. 行为差异

| 场景 | 以前 | 现在 |
|---|---|---|
| 「挂到某个时刻为止」（如挂到上课/下课） | 只能估算秒数，估小了提前消失、估大了占屏 | 给**绝对结束时刻**即可，到点准收 |
| 云端/管理端下发 | 只能 `seconds`（相对） | 新增 `flags.endAt`（绝对），与 `seconds` **互斥、EndTime 优先** |
| 需确认 / 紧急通知 | 常驻等学生点 | **不变**（`flags.endAt` 只对纯展示生效 —— 可点的东西不能自己跑掉） |
| `durationMs` 语义 | 唯一计时口径 | 仍是默认口径；EndTime 存在时被覆盖（不叠加） |

**三条边界（写进 `island.h` 注释，防以后踩）**：
1. **一次性**：showIsland 消费后清零，否则下一条会继承上一条的结束时刻；
2. **已过点钳 1s**：不钳成 0 —— 0 在岛上代表「常驻不自动收」，语义相反；
3. **上限 24h**：防调用方把「秒」当「毫秒」传，写错一个量级不至于挂出永不消失。

## 5. 验证证据（实跑，含反向对照）

| 验证 | 结果 |
|---|---|
| `--endat 5000`（`durationMs` 故意只给 **1500**） | `t=500ms shown=1` ｜ `t=2700ms **shown=1**`（证明 1500 被 EndTime 覆盖）｜ `t=6200ms idle=1`（到绝对时刻才收）｜ **rc=0** |
| **反向对照**（临时删掉 `island.cpp` 的 EndTime 消费块重编） | `t=2700ms shown=0`、**rc=2** ⇒ 探针确实在测该能力，非恒真 |
| 回归 `--autoclose 1500`（U 盘 15s 修复） | rc=0（未被本次改动破坏） |
| 回归 `--actions`（常驻交互） | 仍常驻，`_actions.count=2` |
| 回归 `--expand --face 5` | `errors=0`、`face=5` |
| U 盘模块探针 | PASS 19 / FAIL 0 |
| 闸 `check-main-size.mjs` | PASS：`src/main.cpp 4443 行 ≤ 台账 4443`（**未膨胀**，本批零 main.cpp 改动） |
| 构建 | `_build_agent.py` exit 0 |

## 6. 未验证声明

- **未部署生产**、未跑真机；探针为 offscreen 环境。
- **云端 `flags.endAt` 链路只有代码走查，无探针覆盖**：island 探针 stub 了 `islandShow`，而 `notify_entry.cpp` 不在其链接集内（依赖 `g_tray` 等 main 侧符号）。要端到端验，需在隔离实例里发一条 `notify` 指令。
- EndTime 与**时间校正**（`TimesyncDomain` 的 skew）未联测 —— 剩余时长按系统钟算；若系统钟被 NTP 大跳，已排期的收起时刻不会跟着变（需下次 show 才重算）。
- 未接任何内部调用方（课表提醒仍用固定 6s）—— 那是第 2/3 步的事；本批只把能力做实，避免死代码（能力已由云端 `flags.endAt` 一个真实入口消费）。

## 7. 下一步（第 2 步，已排好但未开工）

提供方/渠道注册制 + 设置视图。待用户确认两项默认值：
1. 提供方清单（我初拟 8 个：课程/考试/对讲/文件/U盘/自动化/插件/管理端）；
2. 默认优先级序（我建议：考试 > 管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件）。

落点已定：`src/notify/provider_registry.{h,cpp}`，直接建在 `Task` 已有的 `source`/`kind`/`tag` 三字段之上（不需新造概念）。
