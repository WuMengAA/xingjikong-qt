# 星集控 · AI 分工与协作约定（AGENTS.md）

> 生效：**2026-10-05** ｜ 适用：**所有在本目录工作的 AI 会话**（WorkBuddy / DeepSeek Harness / 未来任何 agent）
> 读取时机：**开工前先读本文件**；收工前回写第 6 节「当前指针」。

---

## 0. 一句话

**一条线一个 AI，物理隔离优先；不得不同目录时，靠本文档 + git 交接 —— 两个 agent 同时改同一个文件 = 事故。**

（2026-10-05 就发生过一次：两个会话同时改 `Stelarith-control-qt\src\main.cpp`，靠 git merge 才收的场。）

### 📛 main.cpp 禁令（机制强制，不是商量）

`Stelarith-control-qt\src\main.cpp` 曾 9026 行、74 个全局、113 个 static 函数，
业务和装配搅在一起，策略层没法单测（用户原话：「你也没有真正前后端分离」）。

**五条，违反就退回重做：**

1. **只做装配**：全局定义、对象构造、信号接线、进程生命周期。
   业务判断一律进 `src/<domain>/`（`src/noise/`、`src/tts/`、`src/ota/`、`src/policy/` 都是现成例子）。
2. **行数只降不升**：`node Stelarith-control-qt/scripts/check-main-size.mjs` 比台账，多一行就 exit 1。
   拆完跑 `--update` 下调；**禁止上调台账**。
3. **一刀一轮，拆干净**：认定一个簇，就整块搬走、当轮拆完、当轮提交。
   不许"先加一点下次再拆"，也不许把一个簇切成十几刀慢慢磨——那是拖延，不是拆分。
4. **提交前必跑门禁**：`.git/hooks/pre-commit` 会拦。动了 main.cpp 就强制校验，不过就拒。
   `--no-verify` 跳过视为违规。
5. **分层**：域只算数；域服务只定策略；应用服务抓帧/序列化；只有传输与装配层知道 WS。
   `connect(域信号, ws, []{ sendEvent(...) })` 视为违规。

> 现状：2026-10-31 已拆到 **4443 行**（峰值 9026，降 50.8%），16 个域模块。
> 新代码不知道放哪，先翻 `src/` 下已有目录；实在没有就新建一个，别塞 main。
> 依据见 `docs\main拆分方案-2026-10-30.md`。

---

## 1. 分工表（谁管哪条线）

| 线 | 工程 / 目录 | 归属 | 边界 |
|---|---|---|---|
| **Qt 线** | `Stelarith-xingjikong-qt\{Stelarith-cloud, Stelarith-control-qt, Stelarith-viewer-qt}` | **DeepSeek Harness**（2026-10-06 00:33 接手；此前 WorkBuddy） | 云端中转 8788 + 教室机被控端 + 老师机管理端 |
| **站点线** | `D:\Stelarith\Stelarith-website\stelarith`（8090） | **DeepSeek Harness** | 网页 / 集控面板 / 账号 / 站点 API |
| CIMS 线 | （**已退役**） | **无人** | 不要动 8096/8097/8098；隧道里指向 8096 的规则已删 |
| 其它工程 | Music / voicehub / canteen / esp32 / cims-eval … | **都不属于本项目** | **谁都不许碰**（改别人工程=最严重违规） |

> **2026-10-06 00:33 分工变更（用户裁决）**：Qt 线从 WorkBuddy 转到 DeepSeek Harness ——
> 「你做被控端得了」。转手依据：`D:\Stelarith\Stelarith-xingjikong-qt\.workbuddy\memory\2026-10-05.md`
> （1042 行工作日志，最后段落 = 全屏通知三改 / 单实例守卫 / 静态区停重绘）。
> WorkBuddy 最后一次提交：standalone 仓 `0f0b066`（0.6.0 版本收敛，2026-10-06 00:14）。

> **2026-10-28 23:xx 双轨并行约定（用户裁决，修正"接管=WorkBuddy 退役"的理解）**：
> WorkBuddy **不退役、可能继续使用**；双方**统一在 DeepSeek Harness（DSH）工作区与环境要求下并行工作**。
> ① **工作区唯一** = `D:\Stelarith\Stelarith-xingjikong-qt`，双方都在此仓库工作；
> ② **环境要求对双方一致**：受管 Python（生产动作）、禁 cmd/PowerShell 落地、门禁九件、提交路径限定（永不 `-A`）、加减归档全账、QML 纯净封杀令；
> ③ **记忆/skill 已接入 DSH**：16 个星集控 skill 复制至 `C:\Users\Administrator\.dsh\skills\`（catalog 已生效），DSH 核心记忆已接管用户/环境/项目三节；WorkBuddy 源 `C:\Users\Administrator\.workbuddy\` **保留只读，未移动未删除**；
> ④ **记忆真源仍为** 项目 `.workbuddy\memory\MEMORY.md` + 每日 daily log（双方共用，防双轨漂移）；
> ⑤ **并行防撞车**：任何一方开工前按 §7 自检（`git status --short` + 近 15 分钟 mtime），发现对方在动 → 停手登记，不覆盖、不"改完再合并"。
> 依据：用户原话「有可能还会使用 work Buddy 所以统一在 deepseek 工作区和环境要求下并行工作」。

> 判断"这算不算我的线"：**看工程目录属不属于上表**。不确定 → 先问，别动手。

---

## 2. 铁律（违反即事故）

1. **只改自己线内的工程**。跨线改动**必须先登记**（写进本文件或对话），得到确认再动。
2. **同一文件禁止两人同时开**。开工前先 `git status --short` + 看文件 mtime；
   **发现对方刚动过 → 立刻停手**，不要"我改完你再合并"。
3. **不删非本次自建的文件**；批量删前先备份 + 逐条列出清单 + 等人确认。
4. **本机在跑的服务不重启不 kill**：云端(8788) / 站点(8090) / 在线被控端(`C:\Program Files\Stelarith\`)。
   要验证就**另起隔离实例**（假 uid，指本地）或用隔离端口/DB，验完只杀自己那个。
5. **每批次收工**：`git commit`（写清改了什么/为什么/验证了什么/**没验证什么**）+ 回写「当前指针」。

---

## 3. 交接机制

- **git 是唯一交接面**。commit message 必须含：改动清单、行为差异、验证证据（命令+数字）、**未验证项**。
- **指挥台（项目唯一真源）**：`Stelarith-xingjikong-qt\docs\星集控-指挥台-唯一真源-2026-10-02.md`（2026-10-11 从 `D:\Stelarith\cl\` 迁入，旧路径已删）
  —— 既有铁律：**开工先读、收工回写「当前指针」、上一节没回写下一节不许开工**。
- **根仓库结构**：`D:\Stelarith\Stelarith-xingjikong-qt` 是 git 仓库，三个子工程是 **gitlink**。
  **各线只提交自己那个子仓**，别去动根仓库里别人的指针。

---

## 4. 共享触达点（改这些之前必须打招呼）

| 共享物 | 现状 | 规则 |
|---|---|---|
| **Cloudflare 隧道 ingress**（决定谁上公网） | 隧道 `9d95ef14-…`，zone `245959623.xyz` | **改前先报**。⚠️ 顺序敏感：**新路由必须排在通配规则之前**，否则被静默吞掉。脚本：`Stelarith-xingjikong-qt\scripts\add-cloud-ingress.ps1`（幂等，支持 `-WhatIfOnly` / `-DropHostname`） |
| **命名规范** | 已定 | 被控端 `class_<校简称>_<届>_<班号>`（例 `class_xlzx_2028_08`）；管理端 `admin_xlzx_2028_08`；每台唯一、全小写 |
| 云端令牌 | Qt 线持有 | 改一处**必须成对**改客户端（见 `docs\配置与凭据清单.md`） |
| `D:\Stelarith\_deploy\` | 旧线包已清 | 新产物只认 `Stelarith-control-qt\dist\stelarith-agent-setup.exe` |

---

## 5. 发现冲突怎么办

1. **文件被对方改了（未提交）** → **停手、不覆盖**；登记 + 等人裁决。
2. **双方都已提交** → 用 **git merge**（有共同祖先时），**绝不用"拿我的文件覆盖你的"**。
3. **拿不准** → 停。宁可少做一步，不要制造一次覆盖。

---

## 6. 当前指针放哪（**本文件不存指针**）

⚠️ 项目《指挥台》有明文铁律：**「往别处写＝造第二套真源」**。
所以**指针只有一处**：`Stelarith-xingjikong-qt\docs\星集控-指挥台-唯一真源-2026-10-02.md` 的「当前指针」表。
本文件**只放分工与规则**，不复制状态 —— 要看"此刻在办什么"去指挥台看。

**Qt 线最近一次回写内容（供指挥台收录）**：
> 2026-10-05 · Qt 线（WorkBuddy）：云端已上公网 `control.245959623.xyz`；
> OTA / 三端版本统一 0.5.0 / 旧位置成果合并 / 存量清理 / 上线地基（脚本+手册）全部完成。
> **下一步**：换生产令牌 → 装管理端 → 逐台装教室机。详见 `docs\星集控-上线进展报告-2026-10-05.md`。

（本条**已同步写入指挥台**才算数；只写在这里不算回写。）

---

## 7. 快速自检（开工前 30 秒）

```bash
git -C <你的工程> status --short          # 有没有别人未提交的改动
find <你的工程>/src -newermt '-15 minutes' -type f   # 最近 15 分钟有没有人在写
```
两条任一有东西 → **先停，问清楚再动**。

### 7.1 判断「某能力是否已实现」—— 必须 grep blob，不许读提交信息（2026-10-06 加）

```bash
git -C <你的工程> show HEAD:src/main.cpp | Select-String '关键字' | Measure-Object
```

**为什么**：mono 仓 `d5202ea` 提交信息写「紧急通知三档配色 + 不自动关闭」，
但 blob 内 `QPalette::Window` / `m_confirmBtn` / `paintEvent` 全部 0 处 —— 提交信息名不副实。
一条 `Select-String` 就抓到了；只读提交信息会误判成「已修好」，据此跳过真修复。
（详见 §8.1）

### 7.2 跨仓合并前，先确认对方没在动（2026-10-06 加）

```bash
git -C <另一仓> log -3 --format='%h %ad %s' --date=format:'%H:%M'
ls -t <另一仓>/src | head -3       # 或看 mtime
```

**为什么**：本轮 00:33 开始把 standalone 合进 mono，00:48 发现 WorkBuddy 同时在 standalone
做了反向合并（`474cdd5`）。两边结果恰好一致，但撞车本身就是 §0 警告的「两个 agent 同改一个文件」。
上次开工先查这两个仓的 `git log -3` + 文件 mtime，是成本最低的防撞车手段。

---

## 8. 已知冲突登记（发现即登记，不自动处置）

> 本节是**登记簿**，不是状态指针（指针只有一处，见 §6）。
> 登记 = 把"这里有坑"写下来，让下一个会话别重踩。**裁决权永远在人**。

### 8.1 `control-qt` 双仓分叉（2026-10-06 00:15 登记 · DeepSeek Harness）

**状态：✅ 已闭环（2026-10-06 00:55）** — 裁决见文末。保留原始登记以备查。

**现象**：`Stelarith-control-qt` 存在两个独立 git 仓，分叉于 `2335281`：

| | `D:\Stelarith\Stelarith-control-qt`（standalone） | `Stelarith-xingjikong-qt\Stelarith-control-qt`（mono 仓，gitlink 指向） |
|---|---|---|
| HEAD | `6e42550` | `d5202ea` |
| 独有提交 | **5** 个 | 7 个 |

**实质差异（逐 blob 核对，非看提交信息）**：

- standalone 独有 **`0733bf2` 单实例守卫 Global\+Local\+QLockFile 三道**（新增 `src/singleinstance.{h,cpp}`）。mono 仓**至今没有这两个文件**，仍停在 `Local\` 版 —— 跨会话（Session 0 计划任务 vs Session 1 手启）双开失效的老 bug。
- standalone `6f5d99c` 是**真正生效**的全屏通知实现（`setAutoFillBackground` + `QPalette::Window` + `m_confirmBtn` + `paintEvent` 圆角）。
- mono `d5202ea` 提交信息写「紧急通知三档配色 + 不自动关闭」，但 blob 内 `QPalette::Window=0 / m_confirmBtn=0 / paintEvent=0` —— 仍是失效的 `setStyleSheet` 背景写法。**提交信息虚报内容**，与 §7 自检规则同源风险。
- 两仓**各自实现了一遍** OTA + 版本号单一真源（mono `87006a1`+`0a2680b`，standalone `fc632ee`）—— 语义重复，merge 后需人工定哪份为准。

**根因**：mono 仓 `7b81b0a` 提交信息称「并入旧位置成果」，但**实际未并全**（`singleinstance.*` 从未进入 mono 仓）。指挥台 10-05 指针也据此写成"已并入"。

**当前裁决（2026-10-06 00:55 闭环）**：

1. **✅ standalone → mono 已合并**：`git merge -X theirs FETCH_HEAD`（FETCH_HEAD=`0f0b066`），
   mono 仓提交 **`e9333ad`**，根仓 gitlink 由 `d5202ea` → `e9333ad`（根仓提交 **`246ed1c`**）。
   auto-merge 成功、冲突标记 0。`singleinstance.*` 首次进入 mono 仓。
2. **✅ 0.6.0 版本收敛成立**：`kAppVersion = "0.6.0"` 与 `installer.nsi VER = "0.6.0"`
   已一致入库（`474cdd5`），公网 `GET /api/devices` 返回 `version="0.6.0"` 已确认。
3. **⚠️ `d5202ea` 提交信息虚报 —— 不改写历史**。理由：rewrite 共享历史会破坏两边仓的
   共同祖先，且该提交仍可作为"合并前状态"的锚点存在。改法是把正确内容合进去
   （已完成），并在此处永久记录"这条提交信息名不副实"。**以后判断能力是否存在一律
   grep blob，不信提交信息。**

**双向合并结果**：standalone 侧 WorkBuddy 同日 `474cdd5` 做了反向合并（mono→standalone，
只改 4 文件：`.gitignore` / MSI / wixpdb / `gen-wxs.mjs`）。两边现已字节一致：
`main.cpp` / `singleinstance.{h,cpp}` / `CMakeLists.txt` / `installer.nsi` blob 哈希相同，
`dist/` 三个二进制 SHA1 相同。**双仓分叉已消除，standalone 为唯一真源**
（WorkBuddy 在三个 mono 子仓各放了一个 `DEPRECATED-勿再修改-2026-10-06.md`）。

**验证证据（本轮实跑，非推演）**：

- CMake 配置 exit 0；`cmake --build Release` exit 0；`singleinstance.cpp` 全新编译成功
  （`[2/4] Building CXX object ... singleinstance.cpp.obj`），产物 340,480 B。
- blob 核对：`QPalette::Window=1`、`m_confirmBtn=6`、`paintEvent=5`、`self_update=6`、
  `singleinstance=3`、`QApplication=3`、`kAppVersion="0.6.0"` 全在。
- **单实例三道闸实测**：PID 6664 拿锁成功（连云端、注册 v1、托盘就绪、推帧）；
  PID 31528 被拒，stderr 打印「已有另一个实例占着互斥体 `Global\StelarithAgentQt_Singleton`
  → 本次启动退出」并弹出原生 MessageBox「星集控 · 被控端」；全部实例退出后锁释放，
  新实例可重新获取锁。
- **通知三态实测**（云端 `/api/instructions` → TEST1）：`severity=urgent` / `remind` /
  `emergency_confirm` 三条全 HTTP 200；云端事件 `3595/3596/3597`
  「设备真实执行回执 action=notify result=done」。
- **生产状态核实**：`CLOUD_WS_TOKEN` 已是 64 字符强随机（不再是 `dev-cloud-token`），
  公网 `control.245959623.xyz/api/devices` 无令牌→401、带令牌→200。

**未验证项（如实记账）**：

- **跨会话（Session 0 计划任务 vs Session 1 手启）双开那条没验到**：计划任务
  `StelarithAgentQt` 处于 Ready 未运行，本机只有一个交互会话可比对。
  同会话双开已验通；跨会话是 `Global\` 命名空间的设计目的，但缺实证。
- **通知视觉未截图核验**：只确认 agent 收到并回执 done，红/绿底色与确认按钮的
  实际渲染需肉眼在屏幕上看。
- `deploy/translations/` 30 个 `.qm` 删除后未跑干净机实装。
- OTA 下载→安装→重启全链路从未验通（`latest:null` 从未发布过版本）。
- 本轮起的测试实例已停，`C:\Program Files\Stelarith\` 生产实例未运行（未动，遵守铁律 4）。

**教训（写进以后的自检）**：判断"某改动是否存在"时**必须 grep blob 本体**，不能信提交信息。
本次靠 `Select-String 'QPalette::Window'` 一条命令就发现 mono `d5202ea` 名不副实 ——
只读提交信息会误判成"已修好"。已写进 §7 自检。

**另一条流程教训**：合并前应先确认**对方是否已经在动**。本轮 00:33 开始合并，
00:48 发现 WorkBuddy 同时做了反向合并（`474cdd5`）——结果是对的，但撞车本身就是
§0 警告的"两个 agent 同时改同一个文件"。下次跨仓操作前先 `git log -3` + 看文件 mtime。

**已执行的清理**（仅 mono 仓**工作区未提交**改动，均非入库内容，已备份确认后再删）：

- 丢弃 `src/main.cpp` 61 行未提交改动 —— 与 standalone `6f5d99c` **重复劳动**（同一段 palette 背景 + 确认按钮 + paintEvent）
- 删 `test-notify.cpp`（7325 B）—— 独立通知预览测试壳；用户 2026-10-05 原话「为什么要浪费时间重新写一个？」
- 删 `build-test/`（7 项）—— 配套构建目录
- 删 `_probe/{fix-bg,fix-notify,insert-paint,ws-test}.*` —— 本轮探测脚本

**§7 自检规则已扩充（2026-10-06 本轮加的）**：判断"某能力是否已实现"时**必须 grep blob 本体**，
不能读提交信息。已把 `Select-String` 一条命令加进自检清单 —— 只读提交信息会误判成"已修好"。

---

### 8.2 `class_admin.cpp` 被外部修正一行 include（2026-10-24 09:50 登记 · WorkBuddy）

**现象**：本会话 Write `Stelarith-viewer-qt/src/class/class_admin.cpp` 时第 2 行是
`#include "viewerbackend.h"`（无注释）；构建失败（C1083 找不到头）后读文件，
该行已变成 `#include "../viewerbackend.h"   // logf / ViewerBackend（与其他 src/class 文件同口径）`。
其余 91 行与本会话写入逐字一致。文件是**未跟踪新文件**（无历史版本可 diff），
构建脚本 `_build_viewer.py` 无任何 patch 源码逻辑 ⇒ 写入者无法确证
（疑似平台在网络错误恢复间隙的自动修正，无证据）。

**处置（§5 精神：不覆盖、登记、继续）**：保留该修正（`../` 正是子目录 TU 的
正确写法，覆盖回去=把正确改错）；本次不覆盖、不追写。**若后续发现同一文件
再次出现非本会话改动，立即停手并上报用户。**

---

## 9. 全局 UI 构建红线（2026-10-22 立 · 用户点单「全局统一」）

> **目的**：不让 AI 一个页面一个页面地"猜"样式。所有星集控 QML 界面必须长在
> **同一套骨架 + 同一套零件 + 同一套规则**里。本节对所有 AI 会话生效，
> **优先级高于任何页面临时发挥**；与上文铁律冲突时，UI 相关以本节为准。
>
> 落地背景：外部规范（Theme.qml 单例 / AppButton / AppCard / AppDialog / AppTopBar /
> MainWindow 骨架）在本项目**逐一映射到既有单一真源**，不新建第二套——
> 两份真源必然漂移，那正是"全局统一"要消灭的东西。

### 9.1 全局页面骨架（只有三种，不许发明第四种）

| 规范骨架 | 本项目唯一实现 | 说明 |
|---|---|---|
| **主框架 MainWindow** | `Stelarith-viewer-qt/qml/shell/MainWindow.qml` | 管理端唯一应用外壳：240 侧栏 + 64 顶栏 + 内容区（边缘 padding 24）+ 32 状态条 + SideSheet/Toast 上层 |
| **弹窗遮罩 Modal Overlay** | `Stelarith-shared-qml/ui/md3/Md3Dialog.qml`（居中模态）+ `qml/shell/SideSheet.qml`（右侧抽屉） | 模态一律 Md3Dialog；设备详情抽屉用 SideSheet |
| **OOBE 独立窗口** | `Stelarith-control-qt/qml/oobe/`（被控端首次引导） | 无侧栏，居中流式；管理端 `qml/layout/OobeConfig.qml` 同理 |

**新页面 = 在 `qml/pages/` 新增一个文件 + 在 MainWindow 里挂载（StackLayout/Loader）。**
禁止：自绘侧栏/顶栏/状态条、新建第二个应用外壳、在页面里另起一套布局。

### 9.2 设计令牌（单一真源 `Stelarith-shared-qml/ui/Md3.qml`）

- 颜色 / 形状 / 间距 / 字阶 / 动效**全部从 Md3 取**。深浅两实例由宿主经
  `ctx.window.th`（或 `md3:` 属性）注入——**禁止新建第二个 Theme/令牌文件**
  （旧 Theme.qml 已退役并入 Md3.qml；外部规范的 Theme.qml Singleton 形态不采用：
  应用需要深浅两套并存，单例持一份值做不到，详见 Md3.qml 头注释）。
- **零硬编码**：颜色、字号、圆角、间距禁止写字面量。`md3` 可能为 null 的组件用
  `md3 ? md3.x : <浅色令牌值>` 兜底，**兜底值必须等于令牌浅色值**（防止"兜底比令牌好看"
  被故意触发）。兜底是容错不是默认路径——宿主必须传令牌。
- **骨架结构尺寸**（侧栏 240 / 顶栏 64 / 导航项 56 / 状态条 32 / Toast 偏移 28 等）
  以组件内 `readonly property` **命名常量**集中声明，禁止散落魔法数。
- **间距刻度只有 4/8/16/24/32**（spXS/spS/spM/spL/spXL）。既有布局里不在刻度上的
  微调值（6/12/20）legacy 保留但**不得新增**；新代码一律取刻度值。

### 9.3 全局组件库（只能用这些；严禁原生控件）

| 规范名 | 本项目组件（真源） | 规格 |
|---|---|---|
| AppButton | `ui/md3/Md3Btn.qml` | 高 40、默认药丸（shape.full）；filled/tonal/text 为主 |
| AppCard | `ui/md3/Md3Card.qml` | 圆角 16（shape.lg）、elevated 变体带极微弥散阴影、默认无边框 |
| AppDialog | `ui/md3/Md3Dialog.qml` | 圆角 28、50% 黑底、居中、进场缩放+淡入动效 |
| AppTopBar | `qml/shell/TopBar.qml` | 高 64、surface 底、标题 titleM(16) |
| Chip | `ui/md3/Md3Chip.qml` | 筛选 chip 一律用它，与集控/监控页同语言 |
| Snackbar | `ui/md3/Md3Snackbar.qml` 或 inverse 令牌自绘 Toast | — |

- **严禁直接使用** QtQuick.Controls 的 `Button` / `TextField` / `Switch` 默认样式；
  一律用上表组件。既有特许：TopBar 班级下拉用原生 `Menu`（项目没有自带下拉框，
  2026-10-21 决议），新增场景先问再扩。
- **按钮圆角例外（已登记）**：监控中心与 ActionBar 的方角按钮（radius 12）是用户
  2026-10-19 明确点单；**新代码默认药丸**，要方角必须在注释里引用本条。

### 9.4 排版与色彩法则

- **层级排版**：卡片标题 `titleM`(16 加粗) / 正文 `bodyM`(14) / 辅助 `bodyS`(12) 或
  `body13`(13，项目"正"字号) / 读数 `labelS`(11)。禁止所有字一样大。
- **无边框原则**：严禁 `border.width` / `border.color`。区块隔离靠
  `surface ↔ surfaceContainer` 色差 + 圆角 + 极微投影。既有特许（改前先读注释）：
  DeviceCard 选中描边、Md3Chip 未选描边、Sidebar/TopBar 发丝分隔线（分隔线不是边框）。
- 品牌点缀「终末地工业青」= 令牌 `cAccent`，禁止再写 `#2FB8C6`。

### 9.5 动效法则

- 颜色变化必须 `Behavior on color { ColorAnimation { duration: md3.durShort } }`
  （时长走令牌，不写死数字）。
- 弹窗弹出必须背景变暗 + 微小缩放（Md3Dialog 已内置），**禁止生硬闪现**。
- hover/press 用 MD3 状态层（onSurface @ stHover/stPress）叠透明度，不改底色——
  改底色是"塑料感"主因（DeviceCard 注释有完整说明）。

### 9.6 执行与门禁

- 文案门禁：`node Stelarith-shared-qml/scripts/test-ui-terms.mjs`（禁用词进仓库即红）。
- UI 合规靠 **review + 运行时探针**：qmllint 查不出"给组件传了不存在的属性"导致的
  整页崩（2026-10-21 事故，见 TopBar.qml 注释）——**改完必须真跑截图**。
- 只改 QML 不改 C++ 时：`viewer_qml_sync` / `agent_qml_sync` 同步 build/qml，
  否则跑的是旧件。

---

## 10. 工程架构红线（2026-10-22 立 · 用户点单《全局统一设计规范》）

> **适用范围**：所有在本目录写 C++ / QML / 云端 JS 的 AI 会话。
> **与 §9 的分工**：**§9 管"界面长什么样"，§10 管"代码怎么组织"**。
> 两者冲突时——**样式问题听 §9，结构问题听 §10**。
>
> **落地前提（先看这段，别重建轮子）**：外部规范里要求新建的东西，本项目**大半已有唯一真源**：
> QML 模块化引用（`import "components"` 等，见 10.3）**已全面落地**；
> 组件库（AppButton/AppCard/AppDialog）**已映射到 `Md3*` 单一真源**（见 §9.3）。
> **在 §10 框架下新增能力 = 扩充既有真源，不是另起一套。两份真源必然漂移。**

### 10.1 命名表（照抄，不许自由发挥）

| 类型 | 风格 | 本项目实测 | 处置 |
|---|---|---|---|
| C++ 类名 | PascalCase | ✅ 已遵守（`VncClient` / `ScheduleModel` / `FileDialogs`） | 维持 |
| C++ **文件名** | PascalCase | ✗ 历史大量 snake_case / 全小写（`screen_capture.cpp`、`edge_tts.cpp`、`viewerbackend.cpp`、`vnc_client.cpp`） | **新文件一律 PascalCase 且与类名一致；老文件不重命名**——重命名会切断 git 历史与 blame，收益不抵成本 |
| C++ 变量 / 函数 | camelCase | ✅ 已遵守（`stelarithHideChildConsole`、`isPlayAllowedNow`） | 维持 |
| C++ 成员变量 | `m_` 前缀 | ✅ 已遵守 | 维持 |
| QML 组件文件 | PascalCase | ✅ 已遵守（`Md3Card.qml` / `Md3Chip.qml`） | 维持 |
| QML 页面 | PascalCase + `Page` | ✅ 已遵守（`ControlPage` / `AgentSettingsPage`） | 维持 |
| QML 属性 / 函数 | camelCase | ✅ 已遵守 | 维持 |
| 云端路由 | kebab-case | ✅ 已遵守（`/api/device/activate`、`/api/interact/song-requests`） | 维持 |

### 10.2 目录按职能归档（已建雏形，继续往里搬）

现状：`Stelarith-control-qt/src/` 已有 `modules/`（插件模块）、`plugin/`（SDK 宿主）、`schedule/`（课表）三个职能目录；
**但主体（ws 客户端 / ffmpeg 媒体 / OTA / VNC）仍全部堆在 `main.cpp` 一个文件里**。

- 新代码**必须**进职能目录（`src/<domain>/xxx.{h,cpp}`），不许再往根目录或 `main.cpp` 里加文件。
- 拆 `main.cpp` 的原则（见 10.8）：**搬家不改逻辑**——每搬一个域，构建 + 离屏自检绿灯 + 单独提交。

### 10.3 QML 引用：已模块化，禁止退回相对路径

**现状：全库已用"目录即模块"**——`main.cpp` 里 `addImportPath` 注册后，任意深度的 QML 直接写
`import "components"` / `import "components/md3"` / `import "island"` / `import "schedule"` / `import "pages"`。
全库最深引用仅 **2 跳**（`../../components`），**没有任何 `../../../` 灾难**。

- ✅ 继续用目录导入。禁写三级以上相对路径；见到有人写 `../../../` 直接打回。
- 组件真源只有 **shared-qml 构建期拷入的 `qml/components/`** 一处，**禁止新建第二个组件目录**。

### 10.4 C++ 前向声明（改头文件不再引发全量重编译）

- **头文件里只要能用指针 / 引用 / 值参声明解决，就必须 `class Foo;` 前向声明，禁止 `#include "Foo.h"`。**
- `#include "Foo.h"` 只允许出现在 `.cpp` 里。
- 例外：继承（`class A : public B`）、模板实例化、inline 函数体内用到成员——这三样必须 include。
- 现状：viewer 侧已有 29 处前向声明，但**未成文、靠自觉**；本条生效后按此执行。

### 10.5 文件规模红线（有门禁，不是口号）

| 类型 | 红线 | 来源 |
|---|---|---|
| `.h` | 300 行 | 规范 |
| `.cpp` | 600 行 | 规范 |
| `.qml` | 200 行 | 规范 |
| `.js` / `.mjs` | 600 行 | 本项目自定（规范未覆盖云端） |

**门禁**：`node Stelarith-shared-qml/scripts/test-file-size.mjs`
（本机安全策略偶会拦 node 直跑 —— 被拦时用受管 Python `subprocess` 包装，同 `publish-release` 的既定做法）

三段式判定，**不是一刀切判红**（一刀切会让门禁第一天就全红，结局必然是被关掉）：

1. 白名单内 + 行数**没变胖** → warn，允许存在；
2. 白名单内 + **比登记时还长了 1 行** → **error**（烂地基上不许再叠叠乐）；
3. 白名单外 + 超线 → **error**（新代码一律从严）。

白名单 = 拆分台账 `Stelarith-shared-qml/scripts/fixtures/oversize-legacy.json`（2026-10-22 登记 **46 项**，
每项带 `plan` 与 P1/P2/P3 优先级）。**拆完一项就从台账里删一项**——台账只许变短，不许变长。

### 10.6 禁上帝类

出现下列任一信号即为上帝类，**必须按业务拆**（`ClassService` / `DeviceService` / `AudioService` 这种按域拆，
不是按"helper / util / manager"这种横切词拆）：

- 单文件 `Q_INVOKABLE` > 30，或顶层定义（函数/类/命名空间）> 60。

**已挂号（白名单内，按 P1 优先拆）**：

| 文件 | 信号 |
|---|---|
| `Stelarith-control-qt/src/main.cpp` | 8142 行 / 217 个顶层定义 |
| `Stelarith-viewer-qt/src/viewerbackend.{h,cpp}` | 971 行头文件 / 61 个 `Q_INVOKABLE` |
| `Stelarith-control-qt/src/island.cpp` | 2446 行 / 67 个顶层定义 |
| `Stelarith-shared-qml/island/IslandRoot.qml` | 3095 行（两端同源，拆前先同步） |

### 10.7 数据契约：C++ 喂给 QML 的列表必须强类型

- **列表数据一律 `QAbstractListModel` / `QAbstractTableModel` 子类 + `roleNames()`，
  严禁把 `QVariantMap` / `QJsonObject` / `QVariantList` 当模型传给 QML。**
- 现状（2026-10-22 实测）：`control-qt` **0 个 Model**，`main.cpp` 里 **108 处** `QVariantMap`/`QJsonObject` 直喂 QML；
  `viewer` 全端 138 个 `Q_INVOKABLE`。**这是当前最疼的一条**——QML 侧无类型、无补全、无编译期检查，
  改一个字段名要全库猜哪里在读。
- **2026-10-27 QML 侧审计（见 §10.9）**：全库 QML/JS 搜 `WebSocket`/`XMLHttpRequest`/`fetch`/`JSON.parse`/`encodeURIComponent` ——
  结论：QML **几乎不直连网**。MonitorPage/ControlPage 里的 "WebSocket" 仅是注释（VNC 走 C++ `image://vnc/live` provider）；
  CameraBindDialog/NotifyDialog 走 `backend.sendAction(...)`（C++ 桥）。**唯一真·QML 侧违例是
  `Stelarith-viewer-qt/qml/pages/class/ClassTimetablePane.qml` 两处 `JSON.parse`**，解析 C++ 经信号 / `readTextFile`
  丢来的**原始 JSON 字符串**——这正好印证本条：C++ 把原始 JSON 丢给 QML 解析，正是 §10.7 要消灭的反模式。
  修法：C++ 改传强类型 Model / 对象（归入 §10.8 步 2/3）。SvgIcon.qml 的 `encodeURIComponent` 仅用于拼
  `image://svgicon/` URI（颜色 tint 给 image provider），非网络、非业务，属可接受。
- **正解样板（照抄它）**：`Stelarith-viewer-qt/src/schedule/{schedule,subject,timeslot}_model.h`
  —— 它们就是这个规范第 5 条要求的形态，已经跑在产线上。
- 迁移顺序：从**设备清单**这一个最高频列表开始，新旧字段双轨过渡，不一次性切。

### 10.8 破局路线（大动作等点单，不许擅自开工）

| 步 | 内容 | 状态 |
|---|---|---|
| 0 | 立规矩（§10 + 规模门禁 + 46 项台账） | ✅ **已完成（2026-10-22）** |
| 1 | 拆 `main.cpp`：按指令域搬家（media / ota / devices / ws），只搬不改逻辑，每域一提交 | 待点单 |
| 2 | 设备清单 `QVariantMap` → `QAbstractListModel`，双轨过渡 | 待点单 |
| 3 | `viewerbackend` 按业务拆 Service；`Main.qml` / `CameraCenter.qml` 抽子组件 | 待点单 |
| 4 | QML 侧审计 + 封杀令（§10.9）落地：加 `qml-no-network` 门禁（禁 `XMLHttpRequest` / `WebSocket` / `fetch` / `JSON.parse`） | 审计✅ 2026-10-27；门禁待点单 |
| 5 | `ClassTimetablePane.qml` 去 `JSON.parse`：C++ 改传强类型 Model/对象（并入步 2/3） | 待点单 |

**纪律**：第 1/2/3 步都是结构性改动，**开工前必须拿到明确点单**（哪个域、哪个文件、要不要同步发版）。
自己顺手发现的问题只登记、只上报，不动手。

### 10.9 QML 纯净 UI 铁律 · 封杀令（2026-10-27 立 · 用户点单）

> **一句话**：**QML 只画图、只接收点击；所有脏活累活（网络 / JSON 解析 / Win32 / DB / 日志）都在 C++。**
> 与 §10.7 是同一枚硬币的两面：§10.7 管「C++ 怎么把数据喂给 QML」（强类型），本条管「QML 自己不许干什么」。

**绝对边界**
- **C++（大脑与神经）**：WebSocket/HTTP 通信、JSON/Protobuf 解析、VNC 数据包、Win32 API（杀进程/抓屏）、
  本地 DB 读写、日志。**绝不允许阻塞 UI 线程**（耗时任务进子线程）。
- **QML（皮肤与五官）**：只「看」和「点」。把 C++ 经 `Q_PROPERTY` 传来的数据画出来；用户点击呼叫 C++ 的
  `Q_INVOKABLE`。**不解析数据、不连网、不判断业务逻辑。**

**【QML 架构红线指令 / 封杀令】—— 写任何 QML 时强制生效**
1. **纯净 UI 原则**：QML 文件中**绝对禁止**出现 `XMLHttpRequest`、`WebSocket`、`fetch` 等任何网络请求代码。
   （`image://<provider>/...` 这种 C++ 图像提供者是允许的，它不是网络。）
2. **绝对禁止处理业务逻辑**：QML 不允许解析原始 JSON 字符串（`JSON.parse`/`JSON.stringify` 处理业务数据），
   不允许做复杂数据计算。所有计算在 C++ 完成后，通过强类型属性（`Q_PROPERTY`）或数据模型
   （`QAbstractListModel`）传给 QML。
3. **统一入口**：QML 的交互（点击、滑动）**只能**调用 C++ 注入的 `Q_INVOKABLE` 方法（如 `backend.sendAction(...)`）。
4. **禁止阻塞**：C++ 传给 QML 的数据处理必须轻量；所有耗时任务（读文件、网络请求）必须在 C++ 子线程完成，
   QML 侧不得出现 `while`/同步等待/同步 IO。

**正确骨架（照抄）**
- 状态展示：`Q_PROPERTY(Type name READ ... NOTIFY ...)` → QML 直接绑 `backend.name`，变化自动刷新。
- 用户动作：`Q_INVOKABLE void sendXxx(...)` → QML `onClicked: backend.sendXxx()`，**只发指令、不管结果**。
- 列表数据：`QAbstractListModel` 子类 + `roleNames()`，QML `ListView`/`GridView` 直接 `model.deviceName`；
  C++ 数据变 → `dataChanged` → QML 瞬时重绘。**严禁**把 `QVariantMap`/`QJsonObject`/`QVariantList` 当模型传（见 §10.7）。

**2026-10-27 审计结论**：本库 QML 已约 90% 合规（仅 `ClassTimetablePane.qml` 两处 `JSON.parse` 解析 C++ 丢来的原始
JSON 字符串，属 §10.7/§10.9 要消灭的反模式，见 §10.8 步 5）。后续靠 §10.8 步 4 的 `qml-no-network` 门禁守住，不靠人肉 review。
