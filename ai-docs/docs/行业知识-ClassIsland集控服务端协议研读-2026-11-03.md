# 行业知识 · ClassIsland 集控服务端协议（Cyrene_MSP）研读

> 研究对象：ClassIsland 的**集控服务端协议**（客户端侧 gRPC 实现），而非客户端组件本身。
> 源码根：`D:\_ci_src\ClassIsland`
> 核心文件：
> - `ClassIsland/Services/Management/ManagementServerConnection.cs`（gRPC 客户端协议实现，392 行）
> - `ClassIsland/Services/Management/ServerlessConnection.cs`（无服务器模式）
> - `ClassIsland/Services/Management/ManagementService.cs`（接入、重载、退管编排）
> - `ClassIsland.Shared/Protobuf/**`（.proto 消息与服务定义）
> - `ClassIsland.Shared/Models/Management/**`（manifest 及关联模型）
>
> 标注约定：✅=已读源码确认 ｜ 🟡=合理推断 ｜ ❓=未读到/服务端未开源，无法确认。

---

## 30 秒电梯演讲

ClassIsland 的集控叫 **Cyrene_MSP**（Cyrene Management Server Protocol，代号 "CMSP"），协议版本 **2.0.0.0** ✅。它是一套 **gRPC 双向流**协议：客户端先走 HTTP/REST 拉取一份「清单（manifest）」，再通过 gRPC 长连接与服务器握手鉴权、双向下发/上报命令、10 秒一次心跳保活。鉴权核心是 **PGP/GPG 公钥Challenge-Response**——服务器下发的 `ServerKey.asc` 公钥用于加密客户端生成的随机 challenge，服务器能正确解密回传即证明"持有私钥"，从而让客户端确认服务器身份（防钓鱼）。配置下发采用**清单+版本号拉取（pull）**模型：manifest 只指向各资源（课表/时间表/科目/策略/凭据/组件/默认设置）的 URL 与版本号，客户端按版本差异增量拉取 JSON 文件合并。这与星集控当前"云端 WebSocket 推送指令 + REST `/api/*`"的 push 模型有明显差异（详见对照表）。

---

## 一、协议全貌

### 1.1 名称与版本

| 项 | 值 | 来源 |
|---|---|---|
| 协议名 | `Cyrene_MSP`（别称 CMSP） | ✅ `ProtocolName` 常量 |
| 协议版本 | `2.0.0.0` | ✅ `ProtocolVersion` 常量 |
| 传输 | gRPC（HTTP/2 双向流）+ 旁路 REST/JSON | ✅ proto `service` 定义与 `WebRequestHelper.GetJson` |
| 鉴权 | PGP 公钥 challenge + session | ✅ `BeginHandshake` 实现 |
| 核心客户端标识 | `cuid`（=`ClientGuid`，一个 `Guid`） | ✅ `ClientGuid` / `GetMetadata` |

### 1.2 两种接入模式

协议定义了两套并存的连接方式，由 `ManagementServerKind` 枚举决定 ✅：

- **`Serverless`（无服务器）**：客户端只通过 HTTP 拉取一份静态 JSON manifest（`ManifestUrlTemplate`），**没有 gRPC、没有命令流、没有握手**。本质是把 manifest 文件托管在任意静态文件服务器（甚至 GitHub Pages）上。✅
- **`ManagementServer`（集控服务器）**：完整 gRPC 协议，含注册、握手、命令双向流、心跳、审计上报。✅

> 🟡 本研读重点在 `ManagementServer` 这一套（题目点名的 `ManagementServerConnection.cs`）。`Serverless` 是降级/轻量形态，仅 `GetManifest()` 生效，命令流为空（`CommandReceived` 永不触发）。

### 1.3 传输与端点

gRPC 服务地址由 `ManagementSettings.ManagementServerGrpc` 提供（独立字段，区别于 REST 的 `ManagementServer`）✅。`GrpcChannel.ForAddress(...)` 未显式配置 TLS 证书——🟡 推断为明文 HTTP/2 或依赖服务器侧 TLS 终止，**代码层面未见客户端 mTLS/自签校验**（仅见 PGP 应用层鉴权，非传输层加密）。❓ 服务端是否强制 h2c/TLS 未读到。

gRPC 服务清单（✅ 来自 `Protobuf/Service/*.proto`）：

| 服务 | RPC | 流型 | 用途 |
|---|---|---|---|
| `ClientRegister` | `Register` | 一元 | 实例注册，返回服务器公钥 |
| `ClientRegister` | `UnRegister` | 一元 | 注销 |
| `Handshake` | `BeginHandshake` | 一元 | 发起握手，提交加密 challenge |
| `Handshake` | `CompleteHandshake` | 一元 | 确认/建立 session |
| `ClientCommandDeliver` | `ListenCommand` | **双向流** | 命令下发与心跳 |
| `ConfigUpload` | `UploadConfig` | 一元 | 客户端按服务器请求回传配置 |
| `Audit` | `LogEvent` | 一元 | 审计事件上报 |

旁路 REST/JSON 端点（✅ 来自 `ManifestUrl` 构造与 `GetJsonAsync`）：

- `GET {Host}/api/v1/client/{cuid}/manifest` —— 拉清单（唯一在 `ManagementServerConnection` 写死的 REST 端点）✅
- `GET {SourceUrl}` —— manifest 中各资源源（Policy/ClassPlan/TimeLayout/Subjects/Credential/Component/DefaultSettings）的 JSON 文件，URL 模板支持 `{cuid}`/`{id}`/`{host}` 替换 ✅

---

## 二、握手与鉴权（PGP 公钥 + session）

### 2.1 握手时序（ASCII）

```
Client                                              Server (gRPC)
  |                                                     |
  |  ① Register(ClientUid, ClientId, ClientMac)          |
  |  ----------------------------------------------->   |
  |     Retcode, Message, ServerPublicKey(ASCII)         |
  |  <-----------------------------------------------    |  ← 服务端下发 PGP 公钥，客户端落盘 ServerKey.asc
  |                                                     |
  |  ② BeginHandshake(                                   |
  |       ChallengeTokenEncrypted = PGP_Encrypt(         |
  |           Random64Bytes, ServerPublicKey),           |
  |       ClientMac, ClientUid, RequestedServerKeyId)    |
  |  ----------------------------------------------->   |
  |     Retcode, ChallengeTokenDecrypted, ServerPubKey   |
  |  <-----------------------------------------------    |  ← 服务器用私钥解密并回传明文 challenge
  |                                                     |
  |  ③ 客户端校验 ChallengeTokenDecrypted == challenge   |
  |     · 不一致 → 视为不可信服务器，断开不再重试        |
  |     · 一致   → 信任                                  |
  |                                                     |
  |  ④ CompleteHandshake(Accepted=true)                  |
  |  ----------------------------------------------->   |
  |     Retcode, SessionId                               |
  |  <-----------------------------------------------    |  ← 建立 session
  |                                                     |
  |  ⑤ ListenCommand(stream) 打开双向流                  |
  |  ===============================================>   |
  |     （此后命令/心跳均走此流）                        |
```

> ✅ 以上时序逐字段对应 `BeginHandshake` / `ManagementServerConnection.BeginHandshake()` / `ListenCommands()`。

### 2.2 鉴权要点

- **服务器身份验证靠 PGP 非对称加密**：客户端用服务端下发的公钥加密一段 64 字节随机 challenge（Base64）✅；服务端若持有对应私钥，能解密并回传明文，客户端比对一致即确认"对方是私钥持有者"✅。这是**应用层**的身份证明，**不是传输层加密** 🟡。
- **`RequestedServerKeyId`**：客户端在 Begin 时上报自己期望的服务器密钥 ID，支持密钥轮换/多密钥 🟡（来自 `key.KeyId` 取自本地已存公钥）。
- **`session` 元数据**：握手成功后拿到 `SessionId`，之后所有 gRPC 调用的 metadata 都带 `cuid` / `protocol_name` / `protocol_version` / `session` ✅（`GetMetadata()`）。未建会话时除"会话外"调用（`outOfSession=true`，仅 Register/BeginHandshake 用）外均抛异常 ✅。
- **`cuid` 是稳定设备标识**：一个 `Guid`，注册/握手/每帧 metadata 都用它；`ClientMac` 取首个非回环、Up 状态、含 IPv4 单播地址的网卡 MAC（大写）✅。
- ❓ 服务端如何校验客户端（是否有客户端密钥/令牌）**源码未体现**——`Register` 只传 uid/id/mac，没有客户端密钥字段；推断服务器侧可能按 `cuid` 白名单或 `ClassIdentity` 授权，但属服务端逻辑，客户端侧读不到。

---

## 三、Manifest 拉取端点与结构

### 3.1 拉取流程

1. REST `GET /api/v1/client/{cuid}/manifest` 返回 `ManagementManifest` JSON ✅。
2. 客户端校验 `Manifest.CoreVersion.Major` 与本地 `IAppHost.CoreVersion.Major` **主版本一致**，否则弹"核心版本不兼容"并拒绝加入（防数据损坏）✅。
3. 落盘 `ManagementManifestPath = .../Management/Manifest.json` ✅。
4. 按 manifest 中各 `*Source` 的版本号与本地 `Versions.json` 比对，仅拉取"有更新"的资源 ✅（增量，见 3.3）。

### 3.2 Manifest 结构树（管理对象清单）

```
ManagementManifest                                    (✅ 来自 ManagementManifest.cs)
├─ ClassPlanSource        : ReVersionString   课表源
├─ TimeLayoutSource       : ReVersionString   时间表源
├─ SubjectsSource         : ReVersionString   科目源
├─ DefaultSettingsSource  : ReVersionString   默认设置源
├─ PolicySource           : ReVersionString   策略源
├─ ComponentsSource       : ReVersionString   组件设置源
├─ CredentialSource       : ReVersionString   凭据设置源
├─ ServerKind             : ManagementServerKind  服务器类型（Serverless/ManagementServer）
├─ OrganizationName       : string            组织名称（加入时展示给用户）
└─ CoreVersion            : Version            集控核心版本（主版本不兼容则拒绝）

ReVersionString  (✅)  —— "可版本化字符串"
├─ Value    : string?   资源 URL（指向 JSON 文件）
└─ Version  : int       版本号（用于增量比对）
```

`ReVersionString.IsNewerAndNotNull(version)`：当 `Value` 非空且 `Version > version` 时返回 true——这是整套增量拉取的判定核心 ✅。

> 🟡 注意 manifest **本身不含资源内容**，只含"指向资源的 URL + 版本号"。真正的课表/策略等是各自独立的 JSON，由对应 Service（ProfileService / SettingsService / ComponentsService / ManagementService）按版本拉取。

### 3.3 Manifest 能管理的对象（与星集控对照）

| 管理对象 | ClassIsland 字段 | 实际落地的资源 | 应用方 |
|---|---|---|---|
| 课表 | `ClassPlanSource` | `_management-profile.json` 的 ClassPlans/ClassPlanGroups | ProfileService ✅ |
| 时间表 | `TimeLayoutSource` | 同上 TimeLayouts | ProfileService ✅ |
| 科目 | `SubjectsSource` | 同上 Subjects | ProfileService ✅ |
| 默认设置 | `DefaultSettingsSource` | 默认 AppSettings 覆盖 | SettingsService ✅ |
| 策略（限制） | `PolicySource` | `Policy.json`（8 个禁用开关 + 是否允许退管） | ManagementService ✅ |
| 组件设置 | `ComponentsSource` | `Components.json` | ComponentsService ✅ |
| 凭据 | `CredentialSource` | `Credentials.json`（用户/管理员凭据 + 多级授权等级） | ManagementService ✅ |

> 每个对象都是"带版本号的 URL 指针"，客户端在 `ReloadManagementAsync` / 各 Service 初始化时按 `IsNewerAndNotNull` 增量拉取并写本地缓存。✅

---

## 四、命令下发机制（双向流）

### 4.1 命令流图

```
          ClientCommandDeliver.ListenCommand (双向流)
   Client ──────────────────────────────────────────── Server
   请求流 (ScReq)           响应流 (ScRsp)
   ┌─────────────┐         ┌──────────────────────┐
   │ Type=Ping   │ ──────>  │ (心跳，服务器回 Pong) │
   │ (10s 一次)  │         │ Type=Pong            │
   └─────────────┘         └──────────────────────┘
                             ┌──────────────────────┐
   请求流空闲                 │ Type=SendNotification│ ──> 客户端弹提醒
                             │ Type=DataUpdated     │ ──> 客户端 ReloadManagement
                             │ Type=RestartApp      │ ──> 客户端重启
                             │ Type=GetClientConfig │ ──> 客户端回传配置(ConfigUpload)
                             └──────────────────────┘
```

- **双向流消息体** ✅：
  - `ClientCommandDeliverScReq { CommandTypes Type=2; bytes Payload=3; }`
  - `ClientCommandDeliverScRsp { Retcode RetCode=1; CommandTypes Type=2; bytes Payload=3; }`
- **`CommandTypes` 枚举** ✅（节选）：`Ping=10` / `Pong=11` / `RestartApp=101` / `SendNotification=102` / `DataUpdated=103` / `GetClientConfig=104`。
- **分发**：客户端 `ListenCommands` 循环读响应流，`Pong` 直接丢弃，非 `Success` 则记警告跳过，否则触发 `CommandReceived` 事件；各订阅者按 `Type` 处理 ✅。

### 4.2 具体命令语义

- **`SendNotification`（102）** ✅：payload 是 `SendNotification` protobuf，含 `MessageMask`/`MessageContent`/左右角标图标/`IsEmergency`/语音/特效/声音/置顶/`DurationSeconds`/`RepeatCounts`。`ManagementNotificationProvider` 解析后弹"集控提醒"（支持滚动文字、紧急置顶、语音播报）。
- **`DataUpdated`（103）** ✅：`ManagementService.ConnectionOnCommandReceived` 收到后调 `ReloadManagementAsync()`——重新拉 manifest 并按版本增量更新所有资源。这是服务器"推一下，客户端来拉"的**拉模型触发信号**。
- **`RestartApp`（101）** ✅：直接 `AppBase.Current.Restart(true)` 重启客户端。
- **`GetClientConfig`（104）** ✅：payload 是 `GetClientConfig { RequestGuid; ConfigTypes ConfigType }`；客户端按 `ConfigType`（`AppSettings`/`Profile`/`CurrentComponent`/`CurrentAutomation`/`Logs`/`PluginList`）序列化本地配置，通过 `ConfigUpload.UploadConfig` 回传。这是**服务器主动拉取客户端信息**的通道。

---

## 五、心跳与保活 / 断线重连

### 5.1 心跳（✅）

- 计时器 `CommandConnectionAliveTimer`，**间隔 10 秒**（`TimeSpan.FromSeconds(10)`）✅。
- 每次 Tick 向命令流请求流写一条 `ClientCommandDeliverScReq { Type = Ping }` ✅。
- 服务器回 `Pong`（客户端读流时直接 `continue` 忽略）✅。
- 若写心跳抛异常 → 取消命令流 token、停计时器，并触发断线重连逻辑 ✅。

### 5.2 断线重连（✅）

- `ListenCommands` 的 `catch` 块：捕获非取消异常后，清空 `Channel`/`CurrentSessionId`/`CommandListeningCall`，并启一个 **`Interval = 30000`（30 秒）一次性 Timer**，到点 `Task.Run(ListenCommands)` 重连 ✅。
- 重连时**重新建 channel → 重新 BeginHandshake**（再次走完整 PGP 握手，拿新 session）✅。
- 心跳失败后同样走取消 + 重连路径 ✅。
- "服务器密钥验证失败（不可信）"分支：**不再重试**（`return`，不进 catch 的重连循环）✅。

> 🟡 推断：30 秒固定退避、无指数抖动，高并发断网时可能有重连风暴；但属客户端实现细节，未读到服务端限流策略。

---

## 六、审计与凭据（辅助通道）

- **审计** ✅：`Audit.LogEvent(AuditScReq { AuditEvents Event; bytes Payload; int64 TimestampUtc })`，客户端在关键事件（授权成功/失败、设置更新、课表更新、崩溃、启停、插件装卸等，见 `AuditEvents` 枚举）上报。`InstallAuditHooks()` 在源码中是**空方法**（`// 空`）——🟡 推断审计埋点尚未全部接上或延后实现。
- **凭据模型** ✅：`ManagementCredentialConfig` 含 `UserCredential`/`AdminCredential` 及 8 个 `AuthorizeLevel`（编辑授权设置/策略/档案/设置、退管、退应用、换课等）。退出集控需 `AuthorizeByLevel(CredentialConfig.ExitManagementAuthorizeLevel)` 通过且 `Policy.AllowExitManagement` 为真 ✅。
- **策略模型** ✅：`ManagementPolicy` 是 8 个布尔开关（禁用课表/时间表/科目/档案/设置编辑、禁用启动画面自定义/调试菜单/彩蛋、是否允许退管），属于"限制客户端行为"的管控面，星集控当前无对应概念。

---

## 七、与星集控现行协议对照表

> 星集控现行协议依据本项目 `ai-docs` 既有文档与 AGENTS.md（云端 8788 WebSocket + REST `/api/*`、`island_panel` 指令）🟡 对照；具体字段以星集控代码为准，下表标注差异性质。

| 维度 | ClassIsland（Cyrene_MSP） | 星集控（现行） | 差异性质 |
|---|---|---|---|
| 传输 | gRPC 双向流（HTTP/2）+ 旁路 REST/JSON 拉清单 | WebSocket（云端 8788）+ REST `/api/*` | 传输层不同：gRPC 强类型流 vs WS 文本帧 |
| 协议版本管理 | `protocol_name`+`protocol_version` 走 metadata 强校验 | 🟡 版本号在 OTA/客户端自报，无独立协议握手版本 | CI 有显式协议版本常量 |
| 鉴权 | PGP 公钥 challenge-response 验证**服务器**身份 + session | 云端令牌（生产 64 字符强随机 `CLOUD_WS_TOKEN`），WS 连接带 token | 方向相反：CI 验证服务器、星集控验证客户端/设备 |
| 配置下发模型 | **Pull（拉）**：manifest 指 URL+版本，客户端按版本增量拉 JSON 合并 | 🟡 多为 **Push（推）**：云端主动下发指令/配置经 WS | 核心差异：拉 vs 推 |
| 触发更新 | 服务器发 `DataUpdated` 命令 → 客户端来拉 | 云端直接推新配置/指令 | CI 是"推信号触发拉" |
| 命令模型 | `CommandTypes` 枚举 + protobuf 强类型 payload（通知/重启/拉配置/数据更新） | `island_panel` 指令 + 业务 action（notify/remote/song 等） | 都支持通知/重启；CI 内置"回传配置"通道 |
| 心跳 | 10s Ping/Pong 双向流内帧 | 🟡 WS 应用层心跳/保活（具体间隔见星集控代码） | 机制类似，承载通道不同 |
| 断线重连 | 30s 固定退避 + 重新完整握手 | 🟡 客户端看门狗/重连（具体见星集控实现） | 都重连，CI 退避固定 |
| 版本兼容闸 | `CoreVersion.Major` 不一致直接拒绝加入 | 🟡 靠 OTA 版本号约束 | CI 在协议层就卡版本 |
| 资源粒度 | 课表/时间表/科目/设置/策略/组件/凭据 7 类独立可版本化源 | 🟡 指令/配置混合经 island_panel 与 `/api/*` | CI 的资源版本化更细 |
| 限制策略 | `ManagementPolicy` 8 开关限制客户端编辑 | 🟡 无直接对应（靠下发配置达成） | CI 有专门"管控策略"面 |
| 无服务器模式 | `Serverless`：纯静态 JSON，无 gRPC | ❓ 星集控无对应轻量静态模式 | CI 独有降级形态 |

---

## 八、关键结论（对星集控的启示 🟡）

1. **"清单 + 版本号 + 增量拉取"是 CI 配置下发的骨架**，比纯指令推送更利于离线/弱网与回滚——星集控的课表/配置下发可借鉴"资源 URL + 版本号"而非每次全量推。
2. **应用层用 PGP 验证服务器身份**是个有意思的反向思路：星集控当前默认信任云端（设备持令牌），CI 则让设备先验证"服务器确实持有私钥"，防中间人钓鱼。
3. **`DataUpdated` 信号触发拉取**是一种"推拉结合"——服务器不必知道客户端状态即可广播"有更新"，客户端自决是否拉、拉多少（按版本差）。这比星集控"云端直接推全量"对带宽更友好。
4. **`GetClientConfig` 反向采集通道**值得注意：集控不仅能下发，还能按需拉取客户端配置/日志/插件列表——星集控的"回执"更偏执行确认，缺"按需采集"能力。

---

## 九、未读到 / 待查证清单（❓）

- ❓ **服务端实现**：`ManagementServer` 的 gRPC 服务端（如何存 `cuid`、如何签发/轮换 `ServerKey.asc`、如何校验客户端身份、命令如何编排下发）未读到——ClassIsland 仓库是**客户端**代码，服务端大概率在另一仓库或未开源。
- ❓ **具体加密细节**：PGP 加密用 `PgpCore` 库，`EncryptAsync` 的具体算法（AES/RSA 封装）、是否签名、压缩与否未展开；传输层（gRPC 是否走 TLS h2c）代码未显式配置，需抓包或服务端确认。
- ❓ **`ConfigUpload` / `Audit` 的服务端消费逻辑**：客户端已实现上报，但服务端如何处理这些回传数据未知。
- ❓ **`UnRegister` 的实际调用时机与效果**。
- ❓ **manifest 各 `*Source` URL 的服务端组织格式**：客户端只消费 JSON，服务端如何生成/版本化这些文件未读到。
- ❓ **`RequestedServerKeyId` 的密钥轮换实战流程**：客户端上报期望密钥 ID，但服务端多密钥管理未读到。
- 🟡 **星集控现行协议细节**对照基于本项目既有文档与 AGENTS.md 推断，未在本轮重新逐行核对星集控 `Stelarith-cloud` 源码；如需精确字段级对照，应另行 grep 云端 `ws`/REST 实现。
- ❓ **gRPC 错误码/重试语义**：proto `Retcode` 枚举只定义了 `Success=200`/`ServerInternalError=500`/`InvalidRequest=404`/`HandshakeClientRejected=1001`/`Registered=10001`/`ClientNotFound=10002`，其余命令相关 retcode 留空，服务端扩展未知。

---

## 附：核心源码行号速查（✅ 锚点）

| 内容 | 文件 | 行 |
|---|---|---|
| `ProtocolName`/`ProtocolVersion` 常量 | ManagementServerConnection.cs | 42-45 |
| `GetMetadata`（cuid/protocol/session） | ManagementServerConnection.cs | 88-102 |
| `RegisterAsync`（注册+落公钥+拉 manifest） | ManagementServerConnection.cs | 163-178 |
| `BeginHandshake`（PGP challenge） | ManagementServerConnection.cs | 207-258 |
| `ListenCommands`（双向流循环） | ManagementServerConnection.cs | 260-336 |
| 心跳 10s + Ping | ManagementServerConnection.cs | 64-67, 180-205 |
| 断线 30s 重连 | ManagementServerConnection.cs | 319-335 |
| `GetClientConfig` 回传 | ManagementServerConnection.cs | 126-160 |
| `ManagementManifest` 结构 | ManagementManifest.cs | 全文件 |
| `ReVersionString`（版本增量核心） | ReVersionString.cs | 全文件 |
| `ReloadManagementAsync`（按版本拉取） | ManagementService.cs | 158-207 |
| 命令分发（RestartApp/DataUpdated） | ManagementService.cs | 120-134 |
| `ManagementPolicy` 8 开关 | ManagementPolicy.cs | 全文件 |
| `CommandTypes` 枚举 | Protobuf/Enum/CommandTypes.proto | 全文件 |
| `ClientCommandDeliver` 双向流服务 | Protobuf/Service/ClientCommandDeliver.proto | 全文件 |
| `SendNotification` 命令体 | Protobuf/Command/SendNotification.proto | 全文件 |
