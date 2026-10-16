# 管理端审批面板 · 真桥运行时验证 —— 外加一个「只在真跑时才会暴露」的 bug · 加减归档

> 日期：2026-10-16 ｜ 工程：**Stelarith-viewer-qt**（viewer）｜ 关联：组件6「管理端审批面板」
> 触发（用户点单原文）：
> 「InteractBridge 的 C++ 运行时没跑。它依赖的 4 个 HTTP 端点契约层已全绿，但真要实例化
> 真桥得链接 viewerbackend.cpp（QWebSocket/QWebEngine/WebRTC/audio）单独做。」
> ⇒ 本轮即「单独做」的落地，并且**做出来的第一件事就抓到一个真机级 bug**。
> 相关前序：`docs/灵动岛扩展-组件7与管理端审批面板-加减归档-2026-10-15.md`

---

## 一、概述

上一轮把审批面板做完了：UI（`InteractDialog`）、数据桥（`InteractBridge`）、离屏取证（假桥 + 5 图）。
但**数据桥本身从没真跑过** —— 取证用的 `_probe_bridge.qml` 是**假桥**，只验证"面板长什么样"。

本轮补上真桥运行时：

| # | 事项 | 结果 |
|---|------|------|
| 1 | 让真 `InteractBridge` + 真 `ViewerBackend` 连真实云端跑起来 | ✅ 探针 9 通过 / 0 失败 |
| 2 | 结果 | ⚠️ **抓到一个真机级 bug**（见第五节），已修 |

**方法**：不重编整个 viewer（会撞 C1060，且 `viewer-qt.exe` 被桌面上在跑的实例占着会 LNK1104），
而是沿用本仓既有 `scripts/_linknew.py` 的思路 —— **复用 `viewer-qt` 已编好的全部 obj，
只把 `main.cpp.obj` 换成探针 obj**，链出一个"真 viewer 壳"。
于是 `InteractBridge` / `ViewerBackend` 都是**真实现**，没有替换、没有打桩、没有条件编译。

---

## 二、新增文件

| 文件 | 行数 | 职责 | 入库 |
|------|------|------|------|
| `scripts/_interact_rt_probe.cpp` | 176 | 真桥运行时探针：构造真 `ViewerBackend`（`start()` 读 env）→ 真 `InteractBridge` → `reload()` 三轮请求 → `handleBlessing(approve)` → 断言 + 反向对照 | ✅ 入 |
| `scripts/_interact_rt_probe.py` | 227 | 驱动：编探针 obj → 解析 `build.ninja` 拿 viewer 的 obj 列表与链接变量 → 走响应文件 `link.exe` → 造待审数据 → offscreen 运行 | ❌ **不入**（本仓 `.gitignore:34` 约定 `scripts/_*.py` 探针驱动不入库，与 `_interact_probe.py` 同款） |

与既有 `scripts/_interact_probe.cpp` 的分工（**不是替代**）：

| | `_interact_probe.cpp`（10-15 已有） | `_interact_rt_probe.cpp`（本轮） |
|---|---|---|
| 桥 | **假桥** `_probe_bridge.qml` | **真桥** `InteractBridge` + 真 `ViewerBackend` |
| 需要云端 | 否 | **是**（真 HTTP + 真 WS） |
| 验的是 | 面板渲染 / 主题 / 布局（5 张图） | 「面板点下去，云端到底收没收到、回没回来」 |

产物：`build/_interact_run/_interact_rt_probe.exe`（3,244,032 B）
留证：`D:/Stelarith/_probe/rt-probe-20261016/rt-probe-result.txt`

---

## 三、修改既有文件

`git show --stat 078277b` = **4 files changed, 207 insertions(+), 2 deletions(-)**

| 文件 | diff | 改了什么 |
|------|------|----------|
| `src/interactbridge.cpp` | +2/−2 | **修 bug**：`reload()` / `postHandle()` 两处 `m_backend->cloudUrl()` → **`httpBaseUrl()`** |
| `src/viewerbackend.h` | +16 | 新增 `QString httpBaseUrl() const;` 声明（含为什么要有它的长注释） |
| `src/viewerbackend.cpp` | +13 | 新增 `ViewerBackend::httpBaseUrl()` 实现（那段派生的**单一真源**） |
| `scripts/_interact_rt_probe.cpp` | +176 | 新增探针（见上） |

---

## 四、新增接口：`ViewerBackend::httpBaseUrl()`

```cpp
QString ViewerBackend::httpBaseUrl() const
{
    QString httpUrl = m_url;                                  // m_url 是 WebSocket 地址
    httpUrl.replace(QLatin1String("wss://"), QLatin1String("https://"))
           .replace(QLatin1String("ws://"),  QLatin1String("http://"));
    const int wsIdx = httpUrl.indexOf(QLatin1String("/ws/")); // 截掉 /ws/viewer 那段路径
    if (wsIdx > 0) httpUrl.truncate(wsIdx);
    return httpUrl;
}
```

- **非虚函数、不改类布局** ⇒ 除 `viewerbackend.cpp` / `interactbridge.cpp` 外**无需重编**其它目标文件
  （实测：只重编 2 个 obj，用 `ninja -j1 <精确 obj 目标>`，避开重编整棵依赖树与 C1060）。
- 语义：`wss://host/ws/viewer` → `https://host`；`ws://host/ws/viewer` → `http://host`。

---

## 五、本轮抓到的 bug（核心产出）

### 5.1 现象

审批面板在**真机/真云端**上**永远拉不到数据**（`state=error`，三轮 HTTP 请求全发不出去），
而上一轮的"真实 HTTP 端到端"却是**全绿**的。

### 5.2 根因

`InteractBridge::reload()` / `postHandle()` 里：

```cpp
const QString base = m_backend->cloudUrl();          // ← 这里
QNetworkRequest req(QUrl(base + "/api/interact/song-requests"));
```

而 `ViewerBackend::cloudUrl()` 返回的是 **WebSocket 地址**：

```cpp
static const QString kDefaultViewerUrl = QStringLiteral("wss://control.245959623.xyz/ws/viewer");
```

⇒ 实际拼出来的是：

```
wss://control.245959623.xyz/ws/viewer/api/interact/song-requests     ← scheme 是 wss
```

`QNetworkAccessManager` **只认 http/https**，`ws`/`wss` 根本不会发出去 ⇒ 三轮请求全部失败。

**怎么漏的**：`viewerbackend.cpp` 自己走 `/api/*` 的那 **5 处**（`fetchEvents` / `requestClasses` /
`fetchRecordings` / …）**都**先做了这步派生（源码里明确写着注释「m_url 是 ws://host:port/ws/viewer，
派生 http://host:port/api/recordings」），唯独新写的 `InteractBridge` 漏了 —— 因为它直接抄了
`cloudUrl()` 这个"看起来就是地址"的 getter。

**为什么上一轮的端到端没发现**：那一轮我用 Node `curl` 式直连云端 HTTP（`http://127.0.0.1:8992/...`），
**绕过了 InteractBridge 的 URL 派生** ⇒ 契约层全绿，但真正的客户端拼 URL 这一步没人验。
**这正是"不跑真桥就看不到"的一类问题。**

### 5.3 修法

不是给 `InteractBridge` 再抄一份派生（那就成了第 6 份），而是**把派生收成单一真源**
`ViewerBackend::httpBaseUrl()`，`InteractBridge` 两处改用它。

⚠️ **那 5 处旧代码本轮未动**（现行为正确、且 viewer 有并行工作在进行中，避免扩大冲突面）。
建议后续单独一笔:"5 处旧调用点收敛到 `httpBaseUrl()`"，见第八节。

---

## 六、验证证据

### 6.1 探针输出（隔离云端 `127.0.0.1:8992` + `offscreen`，**生产 8788 全程未动**）

```
[rt-probe] cloudUrl    = ws://127.0.0.1:8992/ws/viewer
[rt-probe] viewerToken= (非空，长度 16)
  [PASS] ViewerBackend 从 env 读到云端地址（start() 生效）
  [PASS] ViewerBackend 从 env 读到管理端令牌
[rt-probe] 对照·修复前拼法 = ws://127.0.0.1:8992/ws/viewer/api/interact/song-requests   (scheme=ws)
  [PASS] 对照：修复前拼法 scheme 非 http(s) ⇒ QNAM 发不出去 ⇒ 面板在真机拉不到数据
[rt-probe] 修复后拼法 = http://127.0.0.1:8992/api/interact/song-requests   (scheme=http)
[rt-probe] dataChanged #2  state=ok  songs=2  blessings=1
  [PASS] reload 收口到 state=ok（m_pendingReplies 三轮全 200）
  [PASS] 点歌待审队列非空（GET /api/interact/song-requests） — 2
  [PASS] 祝福待审队列非空（GET /api/interact/blessings） — 1
  [PASS] 反馈汇总非空（GET /api/interact/feedback/summary） likeTotal>=2
  [PASS] 条目带 classCode（按班过滤依据，QML 面板据此显示班级）
[rt-probe] 调 handleBlessing(id=4, approve) → POST /api/interact/handle
[rt-probe] dataChanged #3  state=ok  songs=2  blessings=0
  [PASS] HTTP 审批 handle 生效（本地条目转 approved 或已移出 pending 列表）

══ 结果: 9 通过 / 0 失败 ══
```

**反向对照是零产品改动拿到的**：探针用**真 `ViewerBackend` 的真实地址**重现"修复前的拼法"，
断言它产出的 scheme 不是 http(s) —— 所以这条断言锁的是「旧拼法不可能工作」这个**事实**，
与当前是否已修复无关。

### 6.2 WS 也真通（顺带证明）

```
[viewer] 已连上云端 ws://127.0.0.1:8992/ws/viewer，正在鉴权…
[viewer] 云端鉴权通过（mode=token）
[viewer] 设备表更新：在线 4 台，离线台账 0 台（共 4 条）
[viewer] 云端确认订阅 rtprobe01
[viewer] 收到未处理的云端消息 type=blessing-handled
```

### 6.3 头文件改动的编译面验证

改了 `viewerbackend.h` ⇒ 所有 include 它的 TU 在正式构建时会重编。抽样重编最敏感的两个：

```
[1/18] Automatic MOC and UIC for target viewer-qt
[2/17] Running AUTOMOC file extraction for target viewer-qt
  ⇒ rc=0，updater.cpp.obj(312,461 B) / mocs_compilation.cpp.obj(1,910,129 B) 均已刷新
```

（`ninja -j1`，无 C1060。）

### 6.4 造数据的正确姿势（本轮踩出来的）

待审数据**只能经被控端 WS 上报**（HTTP 那边只有读取/审批端点）：

```
agent WS 连 ws://…/ws/agent → register → song_request / feedback / blessing
  ⇒ seed <- {"type":"song-request-ack","payload":{"ok":true,"id":2,"state":"pending"}}
```

---

## 七、排障实录（本轮踩的 9 个坑）

1. **`cl` 编译探针报 `C1189: Qt requires a C++17 compiler`** —— 我拼编译命令时只带了 `DEFINES`/`INCLUDES`，
   **漏了 viewer 自己的 `FLAGS`**（含 `/Zc:__cplusplus`）。同一次漏掉的还有 `/utf-8`（症状：`warning C4819`
   代码页 936 无法表示中文）。⇒ 从 `build.ninja` 抄 `DEFINES/INCLUDES/FLAGS` 三件套，一件都不能少。
2. **`link.exe` 报「文件名、目录名或卷标语法不正确」**(rc=255) —— 把 74 个 obj + 带空格的 lib 路径
   拼成一条长命令行，被 `cmd.exe` 拒。⇒ 改**响应文件** `@_interact_rt.rsp`（CMake 自己也是这么干的）。
3. **`LNK1104: 无法打开文件"|.obj"`** —— 我从 `build.ninja` 用正则抓 `build viewer-qt.exe: …` **整行**，
   把 ninja 的 `|`(implicit) 和 `||`(order-only) 依赖段也当 obj 了。⇒ 在 `|` 处截断。
   （正确结构：段0 = 47 个 obj + `app.rc.res`；段1(19) = lib 列表；段2(7) = order-only 目标名。）
4. **`LNK2001: 无法解析的外部符号 logf`** —— `logf` 定义在 `main.cpp:135`，而 main 被我顶替了，
   但 `oauthlogin/updater/ldc_rtc_bridge/viewerbackend*.cpp` 都在引用它。⇒ 探针里补一个实现
   （不需要落盘日志，打到 stdout 即可）。
5. **探针的 `qInfo()` 一条都看不到** —— Windows 上 Qt 默认把 `qInfo/qDebug` 交给 `OutputDebugString`
   （stderr 不是控制台时），而输出是被驱动脚本用管道捕获的。⇒ 探针自己 `qInstallMessageHandler` 写 stdout。
   （当时只有 `logf` 的输出可见，一度以为程序没跑起来。）
6. **探针 exe 需要一堆 Qt DLL** —— `_interact_run/` 里原先只有离屏探针用的那几个模块；
   真 viewer 壳还要 `Qt6Widgets` / `Qt6WebSockets` 等。⇒ 从 `D:\QTqwq\6.12.0\msvc2022_64\bin` 拷
   `Qt6*.dll` 全量 + platforms/tls 插件（154 个 DLL）。
7. **seed 上报被回 `AUTH_REQUIRED`** —— 我拿管理端地址（`/ws/viewer`）去发 `register`，
   而那条通道等的是 `auth`。⇒ 派生被控端地址 `/ws/agent`。
8. **seed 的中文输出全是乱码** —— node 输出是 **UTF-8**，我用 `gbk` 解码了。
   （注意区别：探针 exe 的 `logf` 走 `toLocal8Bit()` = 本机编码 GBK，那边 gbk 解码才对。）
9. **`UID` 是 bash 只读变量**（上一轮踩的，同源教训）—— 脚本里 `UID=xxx` **静默失败**，
   值沿用系统 uid。⇒ 测试脚本变量名加前缀（`RUID`/`AT`/`VT`）。

---

## 八、未做 / 建议（明说）

1. **那 5 处旧派生未收敛**：`viewerbackend.cpp` 的 `fetchEvents` / `requestClasses` / `fetchRecordings` /
   `requestStorage` / 另一处，各自仍留着同款 4 行派生。现行为正确，但仍是"抄了 5 份"，
   建议单独一笔收敛到 `httpBaseUrl()`（本轮不动是为了不与并行工作撞面）。
2. **未走真 viewer GUI**：本轮是"真 viewer 壳 + offscreen"，手动点面板那一下仍未在 GUI 上验过。
   本机红线（生产 agent/viewer 在跑）不变，仍转真机清单。
3. **未发布 OTA**：viewer 工作区版本已在 `52505ef` 里 bump 到 `0.6.35`（那是 webrtc 修复那笔做的），
   本修复叠加其上 ⇒ **出包前按需再 bump**。
4. **探针驱动不入库**（`.gitignore:34` 约定）。若希望它随仓库走，需要用户点单改约定
   （或加 `!scripts/_interact_rt_probe.py` 白名单 —— 与 control 侧鲸鱼素材那次同款处置）。

---

## 九、提交记录

| 仓 | commit | 说明 | push |
|----|--------|------|------|
| `Stelarith-viewer-qt` | `078277b` | fix(viewer): 审批面板在真机上永远拉不到数据 —— InteractBridge 未把 ws:// 派生成 http:// | ✅ `1dc9740..078277b` |

> 注：`outer/master` 原先停在 `1dc9740`，而本地已在 `52505ef`（并行会话的 webrtc 修复，已提交未推）。
> 本次 push 一并把它补齐（内容未改动）。
