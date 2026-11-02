# ClassIsland 功能对标清单 · 星集控可移植项

> 依据：用户 2026-11-03 提供的 12 张 ClassIsland 真机截图（组件设置/高级设置/外观/提醒/上下课提醒/天气/自动化/插件市场）+ 本地代码逐条实读。
> 口径：本清单只回答「ClassIsland 有这个 → 我们有/没有/形似神不似」，**不构成开工指令**。按 AGENTS.md §10.8，结构性改动等点单。
> 判据一律以 grep 源码为准，不看提交信息（AGENTS.md §7.1）。

## 0. 一句话结论

ClassIsland 的强大不在「课程表」，而在**三层可组合性**：
**组件（UI 单元） × 提醒提供方（事件源） × 自动化（条件→动作）**。
我们的被控端目前只有**中层的影子**（`provider_registry` 有 8 个提供方、`automation_engine` 有 3 类触发），
最上层的「组件化主界面」和最下层的「组件级样式覆盖」基本是空的。

---

## 1. 全景对照表

图例：✅ 已有且可用 · 🟡 形似神不似（有壳缺内核）· ❌ 无

| # | ClassIsland 能力 | 我们 | 代码位置（实读） | 差距要点 |
|---|---|---|---|---|
| 1 | **主界面 = 组件容器**（堆叠容器/天气简报/时钟/课程表，拖拽排序、多主界面行） | ❌ | 无 | 我们的主界面是 `IslandRoot.qml` 单件写死，不是可编排的容器 |
| 2 | **组件库**（从库拖到主界面） | ❌ | 无 | 无组件注册表、无布局持久化 |
| 3 | **组件设置：个性化覆盖**（每个组件可单独覆盖字体/颜色/不透明度/圆角/边距/对齐/宽度） | ❌ | 无 | 我们只有全局 `Theme`，没有「实例级覆盖」这一层 |
| 4 | **按规则隐藏组件**（规则满足时自动隐藏，且不释放资源） | ❌ | 无 | 关键设计：「隐藏≠卸载」，避免反复重建 |
| 5 | **多配置方案（Profiles）**（右上角 Default 下拉，可多套、可复制） | ❌ | `schedule/profile.h` 只是 ClassIsland 档案的**读取**，不是我们的配置方案 | 我们有 `QSettings` 全局单套，切不了「教室 A/教室 B」两套完整配置 |
| 6 | **界面缩放**（滑块，主界面整体缩放） | ❌ | `island.cpp` 只有内联 `dscale` 计算 | 设计端 `TodayScheduleRow.qml` 用 `屏高/720` 硬算，没有用户可调项 |
| 7 | **背景色 / 不透明度 / 圆角半径 / 行间隔 / 分体主界面** | 🟡 | `IslandSurface.qml` 有圆角与底色，但硬编码令牌 | 全部是设计师定值，用户改不了 |
| 8 | **应用主题（明/暗/跟随）+ 主题色来源（系统/自定义）** | 🟡 | `AgentSettings::lightMode()` 只有明暗二值 | 无「跟随系统」，无主题色来源选择 |
| 9 | **自定义字体 / 字重** | ❌ | 无 | 真机用的是 HarmonyOS Sans SC，我们全靠系统默认 |
| 10 | **提醒总开关 + 提供方优先级（拖动排序）** | ✅ | `src/notify/provider_registry.cpp`（8 提供方 × 通道两级，含上移/下移/恢复默认） | 用户 2026-11-01 已裁决默认序：管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件 > 考试。**这块我们比 ClassIsland 更严**（有 channel 二级、有持久化） |
| 11 | **提醒提供方逐项开关** | ✅ | `SecNotifyProviders.qml` | — |
| 12 | **上下课提醒：课前提醒（室内/室外分设时间）** | 🟡 | `provider_registry.cpp:84` 只有 `class.upcoming`「上课前若干秒提醒」 | 我们**没有室外课概念**（ClassIsland 按科目类型区分室内 60s / 室外 600s） |
| 13 | **上下课提醒：可自定义提醒文字 / 遮罩文字** | ❌ | 无（文案写死） | ClassIsland 四组文案（室内提示/室内遮罩/户外提示/户外遮罩）全部用户可改 |
| 14 | **上课时提醒（提示已上课）** | 🟡 | `class.started` 有事件，无「遮罩」 | — |
| 15 | **提醒语音（EdgeTTS 多说话人 + 音量 + 试听文本）** | ✅ | `src/system/edge_tts.h`（`zh-CN-XiaoxiaoNeural` 默认）+ `src/tts/tts_speaker.cpp` | 我们**只硬编码一个音色**，ClassIsland 有说话人下拉 + 音量 + 测试朗读 |
| 16 | **提醒强调特效（全屏强调）** | 🟡 | 有全屏通知（`notify` 三档 severity） | 不是「可独立开关的特效层」 |
| 17 | **提醒音效（自定义音效文件 + 音量）** | 🟡 | `src/media/audio_player.{h,cpp}` / `whale_audio.cpp` | 无可选音效库、无自定义文件、无独立音量 |
| 18 | **置顶提醒（强制显示并置顶主界面）** | 🟡 | `annotate_domain.cpp:144` 有 `WindowStaysOnTopHint`（批注层） | 提醒层没有「置顶」选项 |
| 19 | **提醒高级设置可覆盖全局**（来源级覆盖） | ❌ | 无 | 我们是「提供方开关」一维，ClassIsland 是「开关 → 高级设置 → 覆盖全局」三层 |
| 20 | **天气：实时 + 预警 + 分钟级降水倒计时 + 日出日落** | ❌ | 全库 grep `weather`/`日出` = **0 处**（仅一处测试串提到「天气转凉」） | 整块空白 |
| 21 | **天气：位置（选城市 / 坐标定位）+ 图标包（纯文本/彩色）** | ❌ | 无 | — |
| 22 | **地震预警（中国地震台网 API，含烈度阈值/自动定位/预警动作）** | ❌ | 无 | 整块空白；ClassIsland 标注「**用户需自备 API Key，且仅供非商业用途**」 |
| 23 | **自动化：条件 → 动作** | 🟡 | `src/automation/automation_engine.{h,cpp}` | 见 §3 详表 |
| 24 | **AI 生成 AllIsland 贴心提醒**（每日简报/新闻汇总） | ❌ | 无 | 依赖外部 AI 服务 |
| 25 | **考试模式（考前提醒/结束提醒/听力自动播放）** | ✅ | `src/system/exam_mode.{h,cpp}`（start(minutes, blacklist)/stop/timeout/killBlacklisted） | 我们**比 ClassIsland 更硬**（会真的杀黑名单进程） |
| 26 | **插件市场（搜索/已安装/下载量/星标/项目主页/依赖项）** | 🟡 | `pluginhost.{h,cpp}` + 云端 `/api/plugins` 读 `plugins.json` | 见 §4；**我们没有依赖项、没有版本冲突解析、没有权限展示** |
| 27 | **插件：Web 媒体信息（SMTC）** | ✅ | `src/system/smtc_monitor.{h,cpp}`（appId/appName/title/artist/album/coverPath + play/pause/next/prev/seek） | 已有且不弱于 Medialsand |
| 28 | **插件：以管理员身份自启动** | 🟡 | `src/system/singleinstance` + 计划任务 | 见 `stelarith-win-autostart-ops` skill；无「需要管理员」的显式语义 |
| 29 | **插件：FancyAnima（CPU 渲染高性能特效）** | 🟡 | `src/system/danmaku.cpp`（QWidget + QPainter 自绘） | 方向一致但未组件化，且**属待去 QWidget 清单** |
| 30 | **插件：ClassGuard 下课助手（检测 USB 未拔 / 指定软件仍在运行）** | 🟡 | `src/modules/usbmodule.cpp` 有 USB 插拔事件；无「下课后的守护检查」 | 事件有、编排无 |
| 31 | **插件：TCP/UDP 课表发送器** | ❌ | `src/wire/frame_builder.cpp` 是自用协议 | 无对外课表广播接口 |
| 32 | **组件附加信息：时间点持续时间/剩余时间/精确倒计时** | 🟡 | `TodayScheduleModel` 有 `ProgressRole` + 1s 时钟 | 「显示哪一种附加信息」不可选 |

---

## 2. 我们比 ClassIsland 强的地方（不要盲目抄）

这三处是我们已经领先的，抄 ClassIsland 反而会退步：

1. **提醒提供方的二级结构**（提供方 → 通道）+ 未知名一律放行。
   ClassIsland 是一维列表，新提供方接入要做全表重排；我们新增提供方只加一行。
2. **考试模式真的会杀进程**（`killBlacklisted`），ClassIsland 的考试模式偏「提醒」。
3. **云端集控闭环**（多租户 / 课表下发 / 回执 / OTA 渠道晋升）。ClassIsland 是纯单机。

---

## 3. 自动化：我们的实现边界（逐条实读）

`automation_engine.cpp`：

| 维度 | 我们已支持 | ClassIsland 有而我们没有 |
|---|---|---|
| 触发 `trigger.type` | `time`（每天 HH:mm）· `interval`（每 N 分钟）· `event`（boot / idle / classStart / classEnd / screenLock / screenUnlock） | 前台窗口类名 / 前台窗口标题 / 前台窗口状态 / 前台窗口进程 / **科目是** / **下节课科目是** / **上节课科目是** / 当前时间状态 / 当前天气 / 明天天气 / 存在气象预警 / 距离降水开始(结束)还剩 / 是否日出日落 |
| 条件 `condition` | `days`（1=周一..7=周日）· `after`/`before`（时间窗，**含跨午夜处理**） | 无极性组合（我们有 after/before 但没有「非」） |
| 动作 `action.kind` | notify / set_volume / mute / unmute / lock / screenshot / launch_app | 生成 AI 贴心提醒 / 更丰富的组件级动作 |
| 其他 | `upsertFromRemote`（云端下发规则）、`runNow`、`engineLog`、`nextRunMs` 预计算、跳过已过去的多个周期 | 可视化条件组的「与/或/非」嵌套 |

**结论**：我们的自动化引擎骨架是对的（有 nextRunMs 预计算、有跨午夜、有云端下发），
缺的是**条件词汇表**。补词表的成本低（`evaluateCondition` 加分支），但「科目是/上节课科目是」
这类条件依赖课表事件总线 —— 恰好是我们本轮刚建好的 `ScheduleEventBus`。

### 3.1 ★ 一个被低估的既有资产：前台窗口条件其实已具备

ClassIsland 的触发器列表里有一整组「前台窗口类名/标题/状态/进程」，看着像要新写 Win32 枚举。
**其实我们已有原始能力**：

- `Stelarith-control-qt/src/core/proc_inspect.h`
  - `QHash<quint32, QString> snapshotProcessNames()` —— pid → 进程名
  - `const WinCache &visibleWindowsCached()` —— 含 `at`（快照时刻）/ `names` / `wins`（`QVector<QPair<HWND, quint32>>` 可见窗口 + 归属 pid）
  - `QString windowTitleOf(HWND)` / `int memMbOf(quint32)`
- 注释写明这是 main 拆分 #13 迁出的「纯 Win32 只读快照，零 main 全局依赖」，`policy/policy_engine.cpp:16` 已在用。

**所以「前台窗口进程是 X」这条条件 = 取 `GetForegroundWindow()` → 在 `visibleWindowsCached().wins` 里查 pid → `snapshotProcessNames()[pid]`**，
不产生新模块、不碰 main.cpp、不新增门禁债。缺的只是 `evaluateCondition` 里一个分支。

⚠️ 但 `visibleWindowsCached()` 是**带 TTL 的缓存**（`kWinCacheTtlMs`）。自动化规则 1s 一拍，
若每条规则都触发一次枚举，教室里 30 条规则会把 CPU 拉满 —— 必须先看清 TTL 与失效策略再动手。

---

## 4. 插件市场：差距在「安装契约」，不在 UI

ClassIsland 市场暴露的字段：下载量（15.4K）、星标（25）、版本（1.1.2.1）、作者、**项目主页**、
**依赖项**（独立 tab）、加载状态（已加载徽章）。

我们的 `PluginManifest` 已有 `id/name/version/author/description/minAgentVersion/permissions/runtime/entry/actions`，
`PluginHost` 已有 `installAndLaunch / enable / uninstall / loadEnabled / snapshot`。

缺：
- **依赖项**（`dependencies[]`）：无 → 无法表达「本插件需要 X 插件 ≥1.2」
- **卸载时被别人依赖** 的检测 → 会留下坏链
- **UI 展示权限**：`kKnownPermissions` 有 6 项，但设置页不展示「本插件要 cmd:exec」的效果说明
- **项目主页 URL** 字段

---

## 5. 按「性价比」排的移植建议（**待点单，不是开工单**）

> 判据：①是否复用已有底座 ②是否会产生新门禁债 ③是否用户可感知。

### 第一梯队（底座已在，只差一层壳）

| 项 | 理由 | 预估落点 |
|---|---|---|
| **A. 上下课文案可配置** | 现状 4 组文案全写死；只需在 `AgentSettings` 加 4 个字符串 + 设置页 4 个输入框 | `src/core/AgentSettings.h` + `SecNotify.qml` |
| **B. 室外课区分** | ⚠️ 我们连字段都没有：`Profile::Subject` 只有 id/name/simplifiedName/teacher/room，grep `outdoor`/`室外` 全库 0 处。比 A 多一步 | `src/schedule/profile.h` 加字段 → 云端课表下发 → `provider_registry` 加 `class.upcoming.outdoor` |
| **C. EdgeTTS 音色可选** | `speak(text, voice)` 第二参**已经存在**，只差 UI | `SecNotify.qml` + `AgentSettings` |
| **D. 提醒音效库** | `audio_player` 已在，只需内置 3-5 个 wav + 选择项 | `src/media/` |
| **E. 自动化「前台窗口」条件组** | **底座已在**（见 §3.1）：`core/proc_inspect.h` 三件套可拼出「窗口进程/标题」，新代码 ≈ `evaluateCondition` 一个分支 | `automation_engine.cpp` + 读 `proc_inspect` |
| **F. 自动化「科目」条件组** | `evaluateCondition` 加 `subjectIs` / `prevSubjectIs` / `nextSubjectIs`，数据来自本轮刚建的 `ScheduleEventBus` | `automation_engine.cpp` |

### 第二梯队（新地基，价值高但成本高）

| 项 | 风险 |
|---|---|
| **G. 配置方案（多套 Profiles）** | 需要把 `QSettings` 全局单套改成「命名档案 + 活动档案指针」。**迁移期必须双读**，否则老教室升级后设置全丢 —— 这正是 0.6.61 那次「升级方式」教训的同类风险 |
| **H. 组件化主界面** | 与 `IslandRoot.qml` 3209 行（台账 3220，只降不升）现状正面冲突。必须先把岛拆成「容器 + 组件注册表」，是典型的大动作 |
| **I. 天气/降水/日出日落** | 纯新增，无历史包袱；但需要外部 API Key 与缓存策略（ClassIsland 也注明要自备） |

### 第三梯队（明确不建议）

- **地震预警**：ClassIsland 自己标注「非商业用途、作者不作准确性承诺」。集控场景下误报惊吓整班，责任面太大。
- **AI 贴心提醒**：依赖外部大模型，离线教室直接失效，且文案不可控。
- **FancyAnima 式 CPU 自绘特效**：与 §9「QtShadcn 唯一图形库、禁 CPU 渲染」红线直接冲突。

---

## 6. 三条「抄了会后悔」的坑（来自真机截图细节）

1. **「按规则隐藏」不释放资源**（m09735 截图原文：「注意此处的隐藏不会释放组件资源」）。
   ClassIsland 明确说明这是**设计取舍**——为了隐藏后能秒开。我们若实现组件化，
   必须同样区分 `visible=false` 与 `destroy()`，否则教室机上「一节课切四次，内存涨四次」。

2. **组件设置会覆盖「全局」与「主界面行」两级**（m09735 三处原文：「将覆盖【外观】设置和主界面行中设置的字体大小」）。
   这是个**三层覆盖链**（全局 < 行 < 组件）。我们现在的 `Theme` 是单例单层，
   真要做组件化，覆盖链必须先设计，否则会出现「改了组件字体，全局字号跟着变」的连锁 bug。

3. **「此处的设置可以在档案设置中被附加设置覆盖」**（m09733 顶部蓝条）。
   ClassIsland 连「配置方案」都能被更上层的附加设置覆盖。这套四级覆盖如果真的照搬，
   排查「为什么我的设置不生效」会变成噩梦。**建议我们只做两级（全局 < 实例），不做归档覆盖。**

---

## 7. 与现有架构的接缝（新增能力该放哪）

按 AGENTS.md §10.2「新代码必须进职能目录」：

| 新能力 | 落点 | 理由 |
|---|---|---|
| 上下课文案 / 音色 / 音效 | `src/core/AgentSettings.{h,cpp}` | 已有 settings 单一真源 |
| 室外课判定 | `src/notify/provider_registry.cpp` | 提供方就在这里注册 |
| 自动化新条件 | `src/automation/automation_engine.cpp` | 已有 `evaluateCondition` |
| 天气/降水 | 新建 `src/weather/` | 全新域，不塞 main |
| 配置方案 | 新建 `src/domain/profile_domain.{h,cpp}` | 与 `settings_domain` 平级 |
| 组件注册表 | 新建 `src/island/components/` | 岛是唯一枢纽（`stelarith-project-overview` skill 已判定） |

---

## 8. 未验证 / 存疑项（如实记账）

1. 截图 m09733 顶部「**档案设置中的附加设置**」在 ClassIsland 源码中的确切层级未核（本文只据 UI 文案推断为三级覆盖链）。
2. 「**按照设定规则隐藏**」的规则语言（是否支持与/或/非）截图未展开，无法判断复杂度。
3. 组件「**固定宽度 / 宽度限制（最小/最大）**」是否与「行内自适应」冲突，截图未体现。
4. 我们的 `dscale = 屏高/720` 在 4K 教室机上的实际观感未经真机验证（只在 1080p/720p 探针里跑过）。
5. 截图里的 ClassIsland 版本为 **2.1.0.1**，与本机 `D:/Classlsland/data/Profiles/Default.json` 的档案格式是否同一代未核。
