# ClassIsland 源码学习 · 通知/岛模型 与 星集控灵动岛「顶替」差距

> 日期：2026-11-01 ｜ 研究对象：`ClassIsland/ClassIsland`（master，GPL-3.0，C# / Avalonia）
> 目的：星集控被控端的灵动岛要**顶替 ClassIsland** 的位置（老师上课在讲台机上看的那块），
> 先把对方的通知体系源码读透，再判断我们缺什么、怎么补。
> ⚠️ 本文所有结论都标注了源码依据；**没有依据的猜测一律标「推断」**。

---

## 0 一句话结论

ClassIsland 的提醒是**五层**：`提供方(Provider) → 渠道(Channel) → 请求(Request) → 优先队列(Host) → 播放票据(Ticket)`，
一条提醒含**两段内容**（遮罩 Mask + 正文 Overlay），时长支持**「相对 Duration」或「绝对 EndTime」**。

我们现在的岛是**一条内容直接盖上屏**：`showIsland(title, desc, icon, durationMs, form)`，
有 `Task` 表（含 priority 字段）但源码注释明写**「本步仅记录，未参与仲裁」**，渠道只是一次性 hint。

⇒ 要顶替，缺的核心是三件：**① 提供方/渠道注册制；② 优先级真正参与仲裁（排队/抢占）；③ EndTime 绝对结束时刻**。
（这三件按「小→中→大」排，第一步一天内可做完。）

---

## 1 源码地图（读过的文件）

| # | 文件（GitHub master） | 职责 |
|---|---|---|
| 1 | `ClassIsland.Core/Abstractions/Services/NotificationProviders/INotificationSender.cs` | 发送方契约：`ShowNotification` / `...Async` / `ShowChainedNotifications(...)` |
| 2 | `.../NotificationProviderBase.cs` | 提供方基类：特性注册 + 自动登记 + 每方设置 + 获取渠道 |
| 3 | `.../NotificationChannel.cs` | 渠道：提供方下的子通道，自身也是 `INotificationSender`，有独立设置 |
| 4 | `ClassIsland.Core/Models/Notification/NotificationRequest.cs` | 请求：Mask/Overlay 双内容 + 取消/完成双令牌 + 本次覆盖设置 + 链式指针 |
| 5 | `ClassIsland.Core/Models/Notification/NotificationContent.cs` | 内容：`Duration` / `EndTime` / `SpeechContent` / `IsSpeechEnabled` + 三个内容模板工厂 |
| 6 | `ClassIsland.Core/Models/Notification/NotificationPlayingTicket.cs` | 票据：`ProcessMask` + `ProcessOverlay` 两个 `Func<Task>` + 取消完成源 |
| 7 | `ClassIsland.Core/Models/Notification/NotificationPlayingSessionInfo.cs` | 会话：累计播放时长 / 显式结束时刻 / 计时器（支持暂停续播） |
| 8 | `ClassIsland.Shared/Models/Notification/NotificationSettings.cs` | 提醒设置：总开关/语音/音效/置顶 + **`IsSettingsEnabled`（级联开关）** |
| 9 | `ClassIsland.Shared/Models/Notification/NotificationPriority.cs` | 排序键（提供方优先级索引、入队序号、强插、是否已播放） |
| 10 | `ClassIsland/Services/NotificationHostService.cs` | 主机：优先队列、先推后队、链式、屏蔽去重、打断重排闸门 |
| 11 | `ClassIsland/Services/NotificationWorkerService.cs` | 工人：把请求拆成 Mask 段 / Overlay 段两次会话，计时 + 语音 + 音效 |
| 12 | `ClassIsland/Services/NotificationProviders/ClassNotificationProvider.cs` | 真实用例：上下课提醒（三渠道 + 双图标遮罩 + EndTime=上课时刻 + 按科目覆盖设置） |
| 13 | `ClassIsland/Models/Settings.cs` | 全局设置：四张提供方表 + 渠道设置表 + 总闸/音效/语音/音量 |

---

## 2 逐层拆解

### 2.1 层一 · 提供方（Provider）= 「谁在发提醒」

用**特性（Attribute）**声明元数据，构造函数里**自动登记**——不需要任何中央注册表手写一遍：

```csharp
[NotificationProviderInfo("08F0D9C3-…", "上下课提醒", "\uE958", "在准备上课、上课和下课时发出醒目提醒…")]
[NotificationChannelInfo(PrepareOnClassChannelId, "准备上课提醒", "\ue21a", description:"在上课前指定时间发出提醒。")]
public class ClassNotificationProvider : NotificationProviderBase<ClassNotificationSettings>
```

基类替你做的事（`NotificationProviderBase` 构造函数）：

```csharp
Info = NotificationProviderRegistryService.RegisteredProviders.FirstOrDefault(x => x.ProviderType == GetType());
Name = info.Name; Description = info.Description; ProviderGuid = info.Guid;
IconElement = info.UseBitmapIcon ? /* Bitmap */ : new FluentIcon { Glyph = info.IconGlyph };
if (!autoRegister) return;
__NotificationHostService.RegisterNotificationProvider(this);   // ← 自注册
```

**要点**：提供方 = 有 GUID、有名字、有图标、**有自己的一份设置类型**（泛型版 `NotificationProviderBase<TSettings>`）。

### 2.2 层二 · 渠道（Channel）= 「提供方里的哪个场景」

一个提供方下可以挂多个渠道，每个渠道有独立 GUID / 名称 / 图标 / 描述，**还能有独立设置控件**：

```csharp
[NotificationChannelInfo(PrepareOnClassChannelId, "准备上课提醒", "\ue21a", …)]
[NotificationChannelInfo(OnClassChannelId,       "上课提醒",     "\ue479", …)]
[NotificationChannelInfo(OnBreakingChannelId,    "下课提醒",     "\ue4c3", …)]
```

渠道自己也是发送方，所以调用点可以精确到渠道：

```csharp
Channel(OnBreakingChannelId).ShowNotification(new NotificationRequest { … });
```

> **这就是之前裁决里说的「通知提供者」模型的骨架**：不是「来源名黑白名单」，而是
> **提供方 → 渠道 两级**，每一级都可单独开关、单独配设置、单独排优先级。

### 2.3 层三 · 请求（Request）= 一条提醒的数据

```csharp
public NotificationContent  MaskContent    { get; set; }   // [Required] 遮罩（必有）
public NotificationContent? OverlayContent { get; set; }   // 正文（可空 ⇒ 只闪遮罩）
public Guid ChannelId { get; set; }
public NotificationSettings RequestNotificationSettings { get; set; }  // 本次覆盖设置
public CancellationToken        CancellationToken  => CancelTokenSource.Token;      // 取消
public CancellationToken        CompletedToken     => CompletedTokenSource.Token;   // 完成
internal NotificationRequest?   ChainedNextRequest { get; set; }   // 链式下一节
public NotificationState        State { get; internal set; }
public double                   LeftProgress { get; internal set; }  // 剩余进度（UI 画环用）
```

内容对象（`NotificationContent`）的计时语义是**关键设计**：

```csharp
public TimeSpan Duration { get; set; } = TimeSpan.FromSeconds(5);   // 相对时长
public DateTime? EndTime { get; set; }                               // 绝对结束时刻（优先级更高）
// 注释原话：此部分的显示时长。当 EndTime 不为 null 时，此项不起作用。
```

⇒ 「上到上课为止」不是靠算秒数，而是**直接给结束时刻**（`ClassNotificationProvider` 就这么用）：

```csharp
EndTime = new DateTime(DateOnly.FromDateTime(now), TimeOnly.FromTimeSpan(LessonsService.NextClassTimeLayoutItem.StartTime))
```

### 2.4 层四 · 主机（Host）= 排队与仲裁

数据：`PriorityQueue<NotificationRequest, NotificationPriority>`，排序键四元组：

```csharp
new NotificationPriority(
    Settings.NotificationProvidersPriority.IndexOf(request.NotificationSourceGuid.ToString()), // ① 提供方优先级
    request.InitialQueueIndex,                                                                  // ② 入队序号 FIFO
    request.IsPriorityOverride,                                                                 // ③ 强插
    isPlayed);                                                                                  // ④ 是否重排回来
```

调度策略：**先推给空闲消费者，没人接才入队**：

```csharp
if (pushNotifications && PushNotificationRequests([CreateTicket(request)])) return;
EnqueueNotification(request, isPlayed);      // 没有消费者接收 → 才排队
```

链式提醒：头/尾用 `ChainedHeadRequest` / `ChainedNextRequest` 串链，**根令牌**级联取消：

```csharp
var rootCancellationTokenSource = CancellationTokenSource.CreateLinkedTokenSource(requests.Select(x => x.CancellationTokenSource.Token).ToArray());
rootCancellationTokenSource.Token.Register(() => { foreach (var r in requests.Where(x => !x.CancellationToken.IsCancellationRequested)) r.CancellationTokenSource.Cancel(); });
```

打断-重排（最有含金量的一段）：票据被取消（被高优先抢占）时，**等移交完成再把请求重新入队**，
并用 `_pendingRescheduleCount` 当派发闸门，避免交接期状态竞争：

```csharp
ticket.CancellationToken.Register(async () => {
    …
    Interlocked.Increment(ref _pendingRescheduleCount);
    try { await ticket.CancellationCompletedCompletionSource.Task; }   // 等移交完成
    finally { Interlocked.Decrement(ref _pendingRescheduleCount); }
    if (request.CancellationToken.IsCancellationRequested || request.CompletedToken.IsCancellationRequested) return;
    EnqueueNotification(request, true);                               // 重新入队（标记已播放）
    PopRequestsToConsumers();
});
private bool CanDispatchRequests => _pendingRescheduleCount <= 0;
```

### 2.5 层五 · 票据与工人（Ticket / Worker）= 真正上屏与计时

一个请求被拆成**两个可独立执行的任务**：

```csharp
public required Func<Task> ProcessMask    { get; init; }   // 遮罩段
public required Func<Task> ProcessOverlay { get; init; }   // 正文段
```

执行顺序与结束条件（`ProcessNotificationSessionCore`）：

```csharp
var duration = SetupNotificationSessionTiming(id, content, session);   // EndTime 优先 → 折成 duration
request.State = NotificationState.Playing;
// 语音 / 音效只在第一段播一次（session.HasSoundsPlayed 守卫）
await Task.Delay(duration, cancellationToken);
if (request.OverlayContent == null || !isMask) {                        // 正文段结束 = 整条完成
    request.State = NotificationState.Completed;
    await request.CompletedTokenSource.CancelAsync();
}
```

计时支持「暂停续播」：`session.SessionPlayedTime + session.TimingStopwatch.Elapsed`（被打断后重排，接着播剩余时长，而不是从头再来）。

### 2.6 设置的级联（很重要，且容易被忽略）

一条提醒最终用哪份设置，**按 Channel → Provider → Request 顺序取第一个「开了 IsSettingsEnabled」的**：

```csharp
foreach (var i in new List<NotificationSettings?>([request.ChannelSettings, request.ProviderSettings, request.RequestNotificationSettings])
             .OfType<NotificationSettings>().Where(i => i.IsSettingsEnabled)) { settings = i; break; }
```

`NotificationSettings` 只有 7 个字段：总开关 / 语音 / 提醒效果 / 音效 / 音效路径 / 置顶 / `IsSettingsEnabled`。

全局另有四张表（`Settings.cs`）：

| 表 | 类型 | 作用 |
|---|---|---|
| `NotificationProvidersEnableStates` | `Dictionary<providerGuid, bool>` | 每个提供方的开关 |
| `NotificationProvidersPriority` | `Collection<providerGuid>` | **优先级顺序（索引越小越优先）** |
| `NotificationProvidersSettings` | `Dictionary<providerGuid, object>` | 每个提供方的自定义设置 |
| `NotificationProvidersNotifySettings` | `Dictionary<providerGuid, NotificationSettings>` | 每个提供方的提醒高级设置 |
| `NotificationChannelsNotifySettings` | `Dictionary<channelGuid, NotificationSettings>` | 每个**渠道**的提醒高级设置 |

⇒ 设置界面要做的「提供者视图」= 上面这张表 + 拖动排序 + 每行展开三块（开关 / 设置 / 高级提醒设置）。

---

## 3 真实用例：上下课提醒（值得照抄的编排）

`ClassNotificationProvider` 的三条时间线：

| 时机 | 渠道 | 遮罩（Mask） | 正文（Overlay） |
|---|---|---|---|
| 上课前 N 秒（默认 60） | 准备上课提醒 | 双图标遮罩，语音「距上课还剩 M 分钟」，**3 秒** | 下节课信息卡，`EndTime = 上课时刻` |
| 上课那一刻 | 上课提醒 | 双图标遮罩（`ClassOnMaskText`） | 无 |
| 下课那一刻 | 下课提醒 | 双图标遮罩，时长 5s（下面没课）或 2s | 下节课卡，10~20s |

两个细节值得学：

1. **链式**：准备提醒 + 上课提醒一次提交（`ShowChainedNotifications([prepare, onClass])`）⇒ 点掉/取消一个，后续整链一起取消，不会出现「准备提醒被关掉了但上课提醒过一会儿又冒出来」。
2. **状态防重**：`IsClassPreparingNotified` / `IsClassOnNotified` 两个 bool，且**只在真上课时才清**——
   注释写得很清楚：「考虑到可能出现提醒过程中时间提前，导致提醒播放结束时还没上课导致重复提醒」。
3. **按科目覆盖**：`GetAttachedSettingsByPriority(ProviderGuid, 下一节课科目, 时间轴项, 课表, 时间表)` ——
   支持「某节课单独设提醒文案/提前量」，且 `IsAttachSettingsEnabled` 才生效。

---

## 4 我们 vs ClassIsland（逐项对照，现状都 grep 过）

| 维度 | ClassIsland | 星集控灵动岛（现状） | 差距 |
|---|---|---|---|
| 提醒来源 | 提供方对象（GUID + 特性注册 + 自注册） | 调用点直接调 `showIsland/notifyFromParams`；内置模块（usb/voicehub）+ 插件 | **缺注册制**：没有统一「提供方表」，无法列出/开关/排序 |
| 子场景 | 渠道（Channel，独立 GUID + 独立设置） | `setChannelForNext("课程提醒")` **一次性 hint**，只是给岛画个标签（`main.cpp:4208/4231`） | **缺注册制渠道**；标签 ≠ 可开关可配置的渠道 |
| 内容结构 | Mask 段 + Overlay 段（可只闪遮罩） | 单条 `title/desc/icon`（`IslandRoot` 的 face 体系是另一回事） | 我们不需要全屏遮罩，但「先闪一下 → 展开详情」可映射 |
| 时长 | `Duration` **或** `EndTime`（绝对时刻优先） | 只有 `durationMs`（相对） | **缺 EndTime**（「上到上课为止」算不出来，只能估秒数） |
| 排队 | 优先队列 + 提供方优先级 + 入队序号 + 强插 | 新内容**直接顶掉**旧内容（`showIsland` 里的归档守卫） | **无排队**：低优先的「正在传文件」会被一条低价值通知顶掉 |
| 优先级 | 真参与仲裁（排序键第 1 位） | `Task.priority`（1..10）**仅记录，注释明写未参与仲裁**（`island.h:140`） | 字段在、逻辑没接 |
| 抢占/回队 | 票据打断后**移交完成再重排**，带闸门防竞争 | 无 | 缺（但单窗口场景可先做简化版） |
| 链式 | `ChainedHead/Next` + 根令牌级联取消 | 无（我们靠 `tag` 合并，`notice` 同 tag 顶掉） | 语义不同：我们是「覆盖」，他们是「排队」 |
| 每方设置级联 | Channel > Provider > Request > 全局（`IsSettingsEnabled` 开关） | 模块级私有设置（`modules/<id>/<key>`）+ 全局开关 | 缺「按提供方/渠道覆盖全局」的层级 |
| 计时进度 | `LeftProgress` 1Hz 刷新，画进度环 | 无（有 `expireAtMs` 但不对外） | 缺（岛上有进度条位置可用） |
| 音效/语音 | 提醒设置里 5 个开关 + 音效文件 + 音量 | 岛音效（`WhaleAudio`，默认关）+ TTS 音量 75/50 序列 | 方向一致，粒度不同 |

---

## 5 顶替路径（建议分三步，每步可独立验收、可单独叫停）

### 第 1 步（小 · 半天~1 天）· EndTime 计时

**为什么先做这个**：我们刚修完「U 盘提示 15s 自动关闭」（`island.cpp` 的 auto-close），
计时入口已经收敛在 `showIsland(durationMs)` 一处，扩成「时长 or 结束时刻」成本最低、收益立刻可见
（上下课提醒终于能「一直挂到上课」而不是猜 60 秒）。

- 落点：`IslandOverlay` 新增 `qint64 endAtMs`（0 = 不启用），`showIsland` 重载或在 `Task` 上加字段；
  `m_autoClose` 到点前按 `endAtMs - now` 计算。
- 验收：探针（`--autoclose` 家族再加一个 `--endat <epochMs>`）断言「到时刻就收」，含跨该时刻后 1~2 秒仍在播的反例。
- 风险：低（单点，不动别的调用方）。

### 第 2 步（中 · 2~3 天）· 提供方 / 渠道注册制 + 设置视图

**为什么第二步**：`kind` 字符串硬编码（`island/fullscreen/banner/mini/danmaku/loop`）是当前最脏的地基，
再往上加任何「按来源配置」都会变成在它上面叠补丁（用户明令禁止）。

- 落点：`src/notify/` 新建 `provider_registry.{h,cpp}`：
  - `struct NotifyProvider { QString id; QString name; QString icon; bool enabledByDefault; QList<NotifyChannel> channels; }`
  - 内置提供方：`课程/考试/对讲/文件/设备(U盘)/自动化/插件/管理端`
  - 持久化：`QSettings notify/providers/<id>/{enabled,priority}`、`notify/channels/<id>/{enabled,settings...}`

> **好消息（本次实测）**：`island.h` 的 `Task` 结构**已经**有 `source`（`self / admin / plugin:<id> / peer:<device>`）、
> `kind`（`notice/alert/exam/voice/file/media/spectrum`）、`tag`（同类去重键）三个字段 ——
> 提供方/渠道注册制可以**直接建立在这三件之上**（`source`+`kind` → 提供方/渠道，`tag` → 合并键），
> 不需要新造一套并行概念。缺的只是「注册表 + 开关/排序/设置的持久化 + 设置页」。
- 设置页：被控端「通知」分类下加「提供者」列表（开关 + 上下移排序 + 展开设置），
  **数据由 C++ 算好给 QML**（守 §10.9 QML 纯图形铁律）。
- 验收：门禁 + 探针断言「禁用某提供方 ⇒ 它发的岛提示被丢弃并回执 dropped」。

### 第 3 步（大 · 1~2 周）· 排队与仲裁

**先做最小可用版**，别一上来就照抄打断重排：

1. 队列长度 N（建议 4）+ 三层优先（抢占 / 排队 / 合并）；
2. `priority` 真正参与：高优先**抢占**当前内容（旧内容进「暂停栈」），播完回来续播；
3. 同 `tag` 合并（我们已有 `upsertTask`），跨 tag 排队；
4. 打断-回队做成「同步版」：先存快照再换内容，不做异步移交闸门
   （ClassIsland 那套闸门是为**多消费者**设计的；我们单窗口用不上，照抄只会引入竞争）。

- 验收：新增探针 `--queue` 断言「低优先在播 → 高优先到达 → 高优先先上 → 播完低优先续播且剩余时长正确」。

### 明确「不照搬」的部分

| 不搬 | 理由 |
|---|---|
| Mask / Overlay 两段式 | 那是「全屏遮罩 + 正文」的投影场景；岛是**同一块地方换形态**（胶囊→展开）。可映射成「先闪后展开」，但不必做成两套会话 |
| `INotificationConsumer` 多消费者 + 票据移交 | 单窗口单消费者，加了只是空转 |
| Avalonia/C# 的特性反射注册 | 我们用注册表显式登记即可（`test-module-registry.mjs` 已经在盯模块注册，可扩到提供方） |

---

## 6 需要点单的决策点

1. **第 1 步（EndTime）现在做吗？** —— 我判断它最划算，且与刚修完的 auto-close 同一条链。
2. 提供方清单以谁为准？我初拟 8 个（课程/考试/对讲/文件/U盘/自动化/插件/管理端），需要你增删。
3. 优先级默认序：我建议 `考试 > 管理端 > 课程 > 对讲 > 文件 > U盘 > 自动化 > 插件`，请你定。
4. 「通知提供者」设置页放哪：被控端设置页「通知与音量」下新增一节，还是独立分类？（会动 `AgentPages.js` 页表）

---

## 7 参考源码链接（master）

- INotificationSender：`https://github.com/ClassIsland/ClassIsland/blob/master/ClassIsland.Core/Abstractions/Services/NotificationProviders/INotificationSender.cs`
- NotificationProviderBase：`.../NotificationProviderBase.cs`
- NotificationChannel：`.../NotificationChannel.cs`
- NotificationRequest：`https://github.com/ClassIsland/ClassIsland/blob/master/ClassIsland.Core/Models/Notification/NotificationRequest.cs`
- NotificationContent：`.../NotificationContent.cs`
- NotificationPlayingTicket：`.../NotificationPlayingTicket.cs`
- NotificationSettings：`https://github.com/ClassIsland/ClassIsland/blob/master/ClassIsland.Shared/Models/Notification/NotificationSettings.cs`
- NotificationHostService：`https://github.com/ClassIsland/ClassIsland/blob/master/ClassIsland/Services/NotificationHostService.cs`
- NotificationWorkerService：`.../NotificationWorkerService.cs`
- ClassNotificationProvider：`.../NotificationProviders/ClassNotificationProvider.cs`
- Settings：`.../ClassIsland/Models/Settings.cs`

> 许可提示：ClassIsland 为 **GPL-3.0**。本文只做**机制学习与自有实现**，未复制其代码；
> 若后续要直接移植代码片段，需先确认与本项目许可的兼容性（这点必须由人裁决，我不擅自引 GPL 代码入库）。
