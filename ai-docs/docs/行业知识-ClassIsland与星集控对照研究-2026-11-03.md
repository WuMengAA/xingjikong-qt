# ClassIsland 与星集控（Stelarith）对照研究

> 信源等级：✅ = 已读源码（ClassIsland 源码在 `D:\_ci_src\ClassIsland`，星集控仓库一手）；🟡 = 合理推断；❓ = 联网受限（HTTP 402）未查证。
> 本篇是"竞品/同类开源项目深挖"——把 ClassIsland 的真实代码结构读出来，与星集控灵动岛做工程级对照。**不是营销对比，是架构互鉴**。

---

## 0 · 电梯演讲

两者都解决同一类问题：**让一块屏幕（学生机 / 教室大屏）在"不打扰"的前提下，常驻显示有用的信息，并按规则自动反应**。但取向相反：

```
   ClassIsland ───────────── 学生/教师机"桌面小组件" ─────────────► 单机为主 + 集控服务器补强
   Stelarith 灵动岛 ─────── 教室大屏"集控状态带 + 轻交互入口" ────► 云端集控为主 + 本机域计算
```

| | ClassIsland | 星集控 Stelarith 灵动岛 |
|---|---|---|
| 宿主 OS | Windows（WPF/Avalonia 系，.NET） | Windows（Qt 6 / QML） |
| 目标屏 | 学生机 / 教师机桌面 | 教室大屏（被控端）+ 老师机（管理端） |
| 信息带 | 桌面顶栏"组件"（可拖拽编排） | 大屏顶端灵动岛（面板展开） |
| 远程能力 | 较轻（集控服务器下发配置） | 重（VNC 巡课 / 媒体 / 批注 / 锁屏 / 音量 / OTA） |
| 架构立场 | XAML 绑定 + ViewModel | C++ 大脑 + QML 皮肤（§10.7/§10.9 纯净 UI） |

---

## 1 · ClassIsland 组件清单（✅ 来自 `ClassIsland/Controls/Components/*.cs`）

源码里实打实存在的组件（11 个）：

| 组件 | 文件 | 语义 |
|------|------|------|
| ClockComponent | `ClockComponent.axaml.cs` | 时钟 |
| CountDownComponent | `CountDownComponent.axaml.cs` | 倒计时 |
| DateComponent | `DateComponent.axaml.cs` | 日期 |
| ScheduleComponent | `ScheduleComponent.axaml.cs` | 课表 |
| GroupComponent | `GroupComponent.axaml.cs` | 分组容器 |
| RollingComponent | `RollingComponent.axaml.cs` | 滚动文字 |
| SlideComponent | `SlideComponent.axaml.cs` | 幻灯片 |
| StackComponent | `StackComponent.axaml.cs` | 堆叠容器 |
| TextComponent | `TextComponent.axaml.cs` | 文本 |
| SeparatorComponent | `SeparatorComponent.axaml.cs` | 分隔符 |
| WeatherComponent | `WeatherComponent.axaml.cs` | 天气 |

> 观察：ClassIsland 的组件更偏**学生机桌面美观与信息密度**（天气、滚动文字、幻灯片、文本、堆叠容器），且组件分"原子组件"（Text/Clock/Date/CountDown/Weather/Schedule）和"容器组件"（Group/Stack/Separator/Rolling/Slide）——容器负责排版，原子负责内容。**这是星集控灵动岛可以借鉴的"组件组合"思路**（当前灵动岛 8 面是平铺的，没有嵌套容器）。

---

## 2 · ClassIsland 自动化引擎（✅ 来自 `Services/Automation/*` + `Models/Automation/*`）

这是 ClassIsland **明显比灵动岛成熟**的一块——它有完整的"触发器→动作"规则引擎：

**触发器（Triggers，13 种）**：
`AppStartup` / `AppStopping` / `Cron`（定时） / `CurrentTimeStateChanged`（时间状态切换） / `OnAfterSchool`（放学） / `OnBreakingTime`（课间） / `OnClass`（上课） / `PreTimePoint`（时间点前） / `RulesetChanged` / `Signal`（信号） / `TrayMenu`（托盘菜单） / `Uri`（URI 唤起）。

**动作（Actions，7 种）**：
`AppQuit` / `AppRestart` / `ModifyAppSettings` / `Notification` / `Run`（跑程序） / `Sleep`（睡眠） / `WeatherNotification`。

**结构**：`TriggerBase` + `ActionBase` 抽象基类（`ClassIsland.Core/Abstractions/Automation/`），规则集（Ruleset）把若干 Trigger 配若干 Action。**这正好对应星集控灵动岛面 6「自动化」**——但星集控的自动化是"本机规则列表"（requiresLive，无规则不显示），源码细节未深挖（🟡）。

```
   ClassIsland 自动化范式
   ┌────────────┐   触发   ┌────────────┐   执行   ┌────────────┐
   │ Trigger    │ ───────▶│ Ruleset   │──────────▶│ Action     │
   │ (时间/信号/ │         │ (与/或)   │           │ (通知/运行/ │
   │  课表状态) │         │           │           │  退出/睡眠)│
   └────────────┘         └────────────┘           └────────────┘
```

> **互鉴点**：星集控的"灵动岛自动化"若要从"本机规则列表"升级到"触发器引擎"，ClassIsland 的 `TriggerBase`/`ActionBase` + `Ruleset` 三件套是现成的范式样板——而且它已经踩过"时间状态切换触发"（OnClass/OnBreakingTime）这种**课表驱动**的坑，正是教室场景最需要的。

---

## 3 · 星集控灵动岛组件清单（✅ 来自 `src/island/island_layout.cpp`）

8 个面（id 0/7/1/4/5/6 + 8/9 默认关；id 2/3 废弃不复用）：

| id | 标签 | 内容 | 数据来源 |
|----|------|------|----------|
| 0 | 概 | 概览（兜底面） | 通知/状态聚合 |
| 7 | 今 | 今日课表 | ScheduleModel |
| 1 | 媒 | 媒体进度 | MediaSession（无则隐） |
| 4 | 笔 | 批注 | IslandBridge |
| 5 | 设 | 音量设置 | IslandBridge |
| 6 | 自 | 自动化 | 本机规则（无则隐） |
| 8 | 期 | 日期周次 | PanelDomain::buildDate（默认关） |
| 9 | 倒 | 倒计时 | PanelDomain::buildCountdown（默认关） |

**对照结论**：
- **重叠面**：时钟/日期（星集控 8/9 期倒、ClassIsland Clock/Date/CountDown）、课表（星集控 7、ClassIsland Schedule）、自动化（星集控 6、ClassIsland Ruleset）三者同构。
- **星集控独有（集控向）**：媒体(1)、批注(4)、音量(5)——这些是"老师用管理端遥控教室大屏"才需要的，ClassIsland 作为学生机小组件没有。
- **ClassIsland 独有（单机美化向）**：天气、滚动文字、幻灯片、文本、堆叠/分组容器——学生机桌面信息密度与美观向，星集控不需要（大屏以教学信息为主）。

---

## 4 · 架构立场对照（✅ 两边都读到了）

| 维度 | ClassIsland | 星集控 |
|------|-------------|--------|
| UI 框架 | Avalonia/XAML | Qt/QML |
| 数据绑定 | XAML Binding + ViewModel | `QAbstractListModel` / `Q_INVOKABLE` + context property |
| 纯净 UI 纪律 | MVVM（VM 算、View 绑） | §10.9 封杀 QML 连网/JSON.parse，脏活全 C++ |
| 下发通道 | 集控服务器 HTTP 轮询 + 插件 | WebSocket 带令牌 + 指令白名单（6 域分派） |
| 可扩展性 | 插件系统（`PluginBase`）+ 组件市场（`IMarketplaceItem*`） | 域内扩（域文件） + 灵动岛面增 |
| 容器组件 | Group/Stack/Rolling/Slide（排版容器） | 当前无嵌套容器（平铺 8 面） |

> **互鉴点（双向）**：
> - 星集控 → ClassIsland：纯净 UI 的"强类型数据契约"（§10.7）值得 ClassIsland 参考——它用 XAML Binding 但 ViewModel 仍可能把原始对象丢给 View。
> - ClassIsland → 星集控：①**容器组件**（Group/Stack）让面板可嵌套组合，灵动岛平铺 8 面可借鉴；②**触发器引擎**（TriggerBase/ActionBase/Ruleset）是灵动岛"自动化"面的升级样板。

---

## 5 · 可视化：能力矩阵（✅ 实测对照）

```
           单机美化向 ◄──────── 能力重心 ────────► 集控远控向
   ClassIsland ●─────────────────────────○ 星集控
   天气/滚动/幻灯               课表/时钟/倒计时        媒体/批注/音量/VNC巡课

   组件丰富度：  ClassIsland 11 ▓▓▓▓▓▓▓▓▓▓  星集控 8 ▓▓▓▓▓▓▓░
   自动化引擎：  ClassIsland ●●●●●●● 星集控 ●●░░░░░（本机规则，无触发器引擎）
   远程远控：    ClassIsland ●░░░░░░ 星集控 ●●●●●●●（VNC/媒体/锁屏/OTA）
   纯净UI纪律：  ClassIsland ●●●░░ 星集控 ●●●●●●（§10.7/§10.9 成文铁律）
   插件/市场：   ClassIsland ●●●●● 星集控 ●░░░░（域内扩为主）
```

---

## 6 · 未覆盖 / 待查证（诚实标注）

- ❓ ClassIsland 集控服务器（ClassIsland 集控）的协议细节与星集控 WebSocket 的差异（联网 402，未读其服务器源码）。
- 🟡 星集控"自动化"面（面 6）的源码细节——是否已有 Trigger/Action 雏形，还是仅规则列表（本次只读到灵动岛面清单，未深挖 automation 域）。
- ❓ ClassIsland 最新版本号、组件市场实际条目数（联网 402）。
- 🟡 两者在"大规模（千台级）下发"下的性能/限流对比（星集控有 CCProtect 限流中间件踩坑经验，ClassIsland 未读）。
- ❓ 星集控是否计划引入容器组件（Group/Stack 式嵌套）——当前灵动岛为平铺。

---

## 7 · 一句话收束

ClassIsland 是"学生机桌面的精致信息组件 + 触发器自动化"的开源范本；星集控是"教室大屏的集控状态带 + 重远控"的工程范本。**两者在时钟/日期/课表/自动化四个面上同构，在媒体/批注/音量/VNC 上星集控独大，在天气/滚动/幻灯片/容器组件上 ClassIsland 独大**。互鉴价值最高的两处：星集控的强类型纯净 UI 纪律、ClassIsland 的触发器引擎与容器组件。
