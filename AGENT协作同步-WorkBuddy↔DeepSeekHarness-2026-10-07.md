# 星集控 · Agent 双向同步记录（WorkBuddy ↔ DeepSeek Harness）

> 建立：**2026-10-07 15:35** ｜ 适用：**同一工作区（`D:\Stelarith\Stelarith-xingjikong-qt`）里同时开工的两个 agent**
> 上位契约：`AGENTS.md`（分工 / 铁律 / 交接机制）——**本文件不替代它，也不重复它**。
> 本文件只回答两件事：**① 我这边你直接能用的已知信息；② 我认为你必知、不知道就会踩的坑。**

**怎么读**：开工前先看 §3（我这边可复用）和 §4（必知坑）——这两节能让你少踩一半的坑；
开工后每次动到 §6 那几处重叠文件，先回来登记；收工前按 §7 补你那一侧。

⚠️ **本文件不存状态指针**（沿用 `AGENTS.md` §6 铁律：指针只有 `Stelarith-xingjikong-qt\docs\星集控-指挥台-唯一真源-2026-10-02.md` 一处）。
这里只存**「两边各自动了什么 + 已知事实 + 交接约定」**，要看"此刻在办什么"仍去指挥台。

---

## 0. 为什么要单开这一份

`AGENTS.md` §0 立的是「一条线一个 AI」——但 **2026-10-07 下午这个前提已经不成立了**：
两个 agent 同时在 Qt 线开工，而分工表（§1）还写着 Qt 线归 DeepSeek Harness，没人知道 WorkBuddy 也在这条线上。
分工表读起来像"那块是你的、这块是我的"，实际是"两块我们都在改"——**制度性不同步比某一次代码冲突更危险**。

所以单开一份：把"已知的"和"该让对方知道的"摊开，让两边开工前先看这一页。

---

## 1. 线的归属：现状与分歧（**需要人裁决，别让 agent 自己猜**）

| 项 | `AGENTS.md` §1 写的 | 实际情况（2026-10-07 下午实测） | 差 |
|---|---|---|---|
| Qt 线归属 | **DeepSeek Harness**（10-06 00:33 接手） | **两边都在动** | ❌ 分工表没跟上 |
| 依据 | 用户 10-06 00:33 裁决「你做被控端得了」 | 该裁决同时把 WorkBuddy 放到了被控端 | 两边解读各采一半 |
| 站点线（8090） | DeepSeek Harness | 本工作区**不涉及**（在 `D:\Stelarith\Stelarith-website\stelarith`） | ✅ 无争议 |

**结论先摆着**：现在这条线上有 **DeepSeek Harness（端侧功能 / 灵动岛 / 资源）** 与 **WorkBuddy（版本号体系 / 构建 / 发布）** 两条并行，
它们 **几乎不相交**——相交处只有 3 处（见 §6）。不相交的部分各干各的没人会误伤，
**只要把 §6 那 3 处排好，两边并行是安全的**；不需要先做"谁退场"的裁决。

> 建议（**只登记，未执行**）：在 `AGENTS.md` §1 分工表补一行 WorkBuddy 侧边界，或在 §7 自检里加一条「先读本文件」。
> 改 `AGENTS.md` 属跨方契约，我不擅自动，等人点头。

---

## 2. WorkBuddy 侧（本 agent）· 本轮做了什么

### 2.1 三端版本号命名对齐 DeepSeek Harness 同款

起因是用户一句「`0.6（大版本）.14（小版本）-rc1（？）-alpha（内部测试版本）`」：
查 `npm view @deepseek-ai/dsh versions` 证实 **`-alpha` 与 `-rc` 是两条并存的链，不是同一条改叫法**
（`0.1.2-alpha.2~5` → `0.1.2-rc.1` → `0.1.2`；`0.2.1-alpha.1` 与 `0.2.0-rc.2` 并存）。
上一版只认 `-rc.N`，会把 `0.6.23-alpha.1` 判成不合规 —— 这是漏的，本轮补齐。

**三形态 + 阶段序**：

```
X.Y.Z          正式版
X.Y.Z-rc.N     候选 / 公测（N 从 1 起）
X.Y.Z-alpha.N  内测（N 从 1 起）
阶段序：alpha(0) < rc(1) < 正式(2)
```

- alpha 转 rc **号归 1**（不是连号）；换核心（Z/Y 变）两条链的号都归 1
- 转正即剥离后缀，号作废；号从 1 起 ⇒ 挡掉 `alpha.0` / `rc.0` / 裸后缀 / 双后缀
- **成熟度标记仍不进版本号**：强制升级 = `ota.json` 的 `mandatory`，渠道 = `viewer.env`

⚠️ **本轮抓到一个真 bug（已修）**：C++ 侧 `compareVersion` 里"没有阶段后缀"的哨兵写了两套
（`parse()` 里 `-1`、调用处初值 `0`，而 `0` 正好是 `RANK_ALPHA`）⇒ 核心打平后 **「正式版 < 候选版」**：

```
compareVersion("0.6.23", "0.6.23-rc.1") → -1   // 本该 +1
```

**实际后果：rc 转正、云端发正式版时，被控端判成"已是最新" ⇒ 托盘不提示升级、自动升级不触发。**
JS 自检验不出来（两套实现各写一遍）。靠 `scripts/compare-version-cpp/run-probe.py` 把函数抽出来编译跑断言才抓到。

### 2.2 文件全账（全部未提交）

**A. 本轮（命名标准 + alpha 补齐）：**

| 文件 | 仓库 | 改了什么 |
|---|---|---|
| `scripts/version-compare.mjs` | 根仓（未跟踪） | **唯一一份 JS 比较实现**：`STAGE_RANK={'':2, rc:1, alpha:0}`、`STAGE_RE=/^(alpha\|rc)\.([1-9]\d*)$/`、`cmpVersion` / `parseVersion` / `isCandidate` / `isStable` |
| `scripts/test-version-naming.mjs` | 根仓（未跟踪） | 自检：`A` 命名合规组、`B` 比较语义组（含跨阶段）、`D` 真源组、`F` 发布脚本组 —— **51 条断言**（F1b 专防「阶段号回退成 `\d+`）|
| `Stelarith-control-qt/CMakeLists.txt` | 控 | 版本守门正则加 `alpha`、核心/阶段拆分、`AGENT_STAGE{,_N}`、message 打印（`git diff --stat`：+174 行） |
| `Stelarith-viewer-qt/CMakeLists.txt` | 管 | 同上（`VIEWER_STAGE{,_N}`） |
| `Stelarith-control-qt/src/main.cpp` | 控 | `compareVersion()` 重写为三形态 semver 子集 + 哨兵修正（1018 行起） |
| `Stelarith-cloud-ws/scripts/publish-release.mjs` | 云 | 两轮：① 正则放开 `-(alpha\|rc).N`；② **16:0x 收紧**成 `-(?:alpha\|rc)\.[1-9]\d*` —— 原先写 `\d+` 会把 `0.6.23-alpha.0` 写进线上清单，被控端解析阶段号要求 n≥1 ⇒ 当正式版 ⇒ 教室机不升级、之后再发 `0.6.23` 也判相等推不下去。**发版预演抓到的，详见规范文档 §12** |
| `docs/版本号命名规范-2026-10-07.md` | 根仓 | 规范文档 16.3 KB（§1–§3 三形态、§6 跨阶段比较、§10 bug 复盘、§11 新踩坑） |
| `scripts/compare-version-cpp/` | 根仓（未跟踪） | **C++ 侧回归工具**（2026-10-07 晚归位）：`run-probe.py` 从 `control-qt/src/main.cpp` 原样抽出 `compareVersion()` + `_shim_qt/`（QString 打桩）单独编译跑 19 条断言；产物落 `_build/`。**跑法**：`python scripts/compare-version-cpp/run-probe.py`。 |
| `scripts/preview-ota-publish.py` | 根仓（未跟踪） | **发版预演**（2026-10-07 16:0x）：指临时清单 + 临时资产目录跑 6 个 publish 用例（`alpha.1` → `rc.1` → 转正 `0.6.23`、阶段号 0 拒绝、裸后缀拒绝、`--allow-downgrade` 回滚通道）+ dry-run 不动清单 + 闭环 `check`，**生产 `ota.json` 跑前跑后同一份 sha256**。**跑法**：`python scripts/preview-ota-publish.py`，14 条断言。**跑前建议**：你要发版前先跑一次。**⚠️ 两个坑**：`CLOUD_OTA_MANIFEST_FILE` / `CLOUD_ASSETS_DIR` 只吃**相对路径**（脚本内部 `path.join(ROOT, …)`，给绝对路径会被拼成 `ROOT/C:/…`）；`ASSETS_DIR` 后面还会自动拼一个 `pkg`。
| `.gitignore` | 根仓 | +2：忽略 `scripts/compare-version-cpp/_build/`（抽离编译的**产物**）。⚠️ **订正**：2026-10-07 晚同步写这行时说「`_probe_cpp.py` 与 `_shim_qt/` 故意保留入库」是**错的** —— 它们当时只是工作区未跟踪文件，从未 `git add` 过，你那边拿不到；当晚已归位到 `scripts/compare-version-cpp/` 下，但**仍未提交**，入库要等你点头 |

**B. 工作区累计未提交（含此前几轮）：** 根仓（3 个 gitlink 漂移 + 记忆 + `docs/` 5 份归档）、控（`installer.nsi` / `build-agent-installer.sh` / `main.cpp`）、管（`AccountMenu.qml` / `make-portable.mjs` / `viewerbackend.h` / `main.cpp`）、云（`publish-release.mjs`）。

⚠️ **`control-qt/src/main.cpp` 是最高危那一处**：里面压着**他人在途的** `exam_mode`（50 / 296 行）与「大屏通知含 Island 形态」（1831 行起），
我的改动只在 `compareVersion`（1018 行起）。**整批提交会把别人的在途工作一起带走** ⇒ 一律未提交，等用户点单分批改。

### 2.3 验证证据（本轮实跑）

| 项 | 结果 |
|---|---|
| `node scripts/test-version-naming.mjs` | **50 通过 / 0 失败** |
| C++ `compareVersion` 抽离编译 + 跑断言 | 编译/链接 rc=0，**19 条全过** |
| agent 版本守门（`cmake -P` 四组用例） | `0.6.23-alpha.7` →「核心 0.6.23，阶段 alpha 7」；`0.6.23` → 阶段空；`0.6.23-alpha.0` → **exit 1 被拒** |
| viewer configure | `-- [viewer] 版本 0.6.23-rc.1（核心 0.6.23，阶段 rc 1）` |
| `publish-release.mjs check` | agent 0.6.22 / viewer 0.6.14 sha256 一致，退出码 0 |

### 2.4 没验到的（如实记账）

- **被控端整链**（`.rc` 真烧进 exe + 安装器真编）：卡在他人缺的 `src/oauthbind.cpp/h`
- **C++ 语义没在真机跑过**：只在抽离编译里跑断言，`scripts/compare-version-cpp/_shim_qt/` 是 QString 打桩（本机 Qt 6.8.1 编不过 VS18 的 MSVC 14.51）—— 能证明**逻辑对**，不能代替真机编译
- 真机装 / 卸 3 枚快捷方式（沙箱 COM 限制）
- C++ 与 JS 是**两份实现**，没有交叉断言把两边绑死 —— 这次 bug 就是这么漏的

---

## 3. 我这边你直接能用的已知信息

> 这些是我这轮工作里摸到/核实过的硬事实。**你开工前扫一遍，能省掉重复探查。**

### 3.1 构建与目录

- 工作树只有一份：`Stelarith-xingjikong-qt\{Stelarith-cloud-ws, Stelarith-control-qt, Stelarith-viewer-qt}`；
  `D:\Stelarith\` 下同名三份是**只读历史**，别删。根仓是 git 仓库，三个子工程是 **gitlink**。
- 构建：管理端 `viewer-qt\build-qt-viewer.bat`；被控端 `scripts\build-agent.sh`。
- **本机 Qt 是 `D:\Qt\6.8.1\msvc2022_64`，但它装不全** —— 缺 `Quick`（管理端）、`WebEngineWidgets`（被控端）。
  ⇒ 整份 `find_package` 会在版本段**之前**就失败，**configure 根本走不到版本校验**。
  要单独验版本段，只能把那段抽出来 `cmake -P` 跑（我这轮这么干的）。
- cmake/ninja 要用绝对路径：VS18 的 cmake 在 `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\...\CMake\bin\`，
  MSVC 工具集 `14.51.36231`，SDK `10.0.26100.0`。`LIB` 必须含 `ucrt\x64`（否则 LNK1104 ucrtd.lib），
  `PATH` 要含 SDK 的 `x64` 目录（rc.exe）。
- ⚠️ **`build/qml/` 是运行时加载的**（exe 不内联 QML）⇒ 想调 QML 效果**不必重编译**，直接改 `build/qml/Stelarith/*.qml` 就行
  —— 但**下次编译会被 POST_BUILD 覆盖**，出包前务必确认没有探针残留。
- ⚠️ **windeployqt 每次构建都会删 `build/platforms/qoffscreen.dll`** ⇒ 每次编译后重补，忘了就报找不到平台插件。

### 3.2 版本与发版链路

- 版本号**唯一真源 = 云端 `ota.json`**（被控端托盘 + 官网 `/download` 都读 `/api/public/ota`）⇒ 发版后站点免重构建。
- ⚠️ `ota.json` **必须 UTF-8 无 BOM**：带 BOM ⇒ 接口静默 `latest:null`，现象和"被控端说没新版"一模一样。
- 出包**一律走脚本**，别手工拼包：管理端 `viewer-qt/scripts/make-portable.mjs`（`--check <zip>` 只检查）、
  被控端 `scripts/make-portable-agent.mjs`（29 项硬校验）。
  0.6.0 手工包根层缺整个 QtWebEngine —— 照常起、看画面才炸，查了半天才发现是**包缺件**而非代码问题。
- ⚠️ `make-portable.mjs` 的 `EXTRA_FILES` 是「**build/ 优先、回落 deploy/**」：`viewer.env` / `start-viewer.cmd` 住 `deploy/`。
- ⚠️ 进包**文件名用 ASCII**（7z 列表按 GBK、node 按 UTF-8，中文名会出假阴性）。
- 被控端打包素材 = `deploy/`（DLL/插件/免安装件）+ `build/`（exe 与 **`resources/`、`translations/`**）；
  ⚠️ **这两个目录只由 windeployqt 产到 `build/`，`deploy/` 素材库里没有** ⇒ 手工拼包必然漏。

### 3.3 网络与地址（这节最值钱，地址错一次排查一天）

- **公网唯一入口是 443/wss**：`wss://control.245959623.xyz/ws/agent`（被控端）/ `ws/viewer`（管理端）。
  cloudflared 从本机 8788 回源，**8788 对外完全不通**（实测超时；443 一律 101）。
  ⇒ **给外部机器用的地址必须 wss + 不带端口**。
- **两个子域，不能互相推导**：云端 `control.` / 站点 `www.`；站点公网 443，8090 是**内网**端口。
  早期「云端地址 → 站点地址」的推导把域名和端口各错一次，正是"被控端连不上 website"的根因。
  **站点地址是一等配置，别猜。** 云端 `provision.js` 算的是对的地址，装机面板生成的 bat 用它 ⇒ 走面板装机不会错，**手填会错**。
- **两个令牌别用混**：`CLOUD_WS_TOKEN`（设备令牌，被控端连 WS）/ `CLOUD_VIEWER_TOKEN`（管理端，调 `/api/devices`）。用错吃 401。
  ️⚠️ 改一处**必须成对改客户端**（清单见 `docs\配置与凭据清单.md`）。
- 验发布/连通打**本机 `127.0.0.1:8788`**；`www.245959623.xyz/api/*` 一律 404 是正常的（那是站点域名）。

### 3.4 排障经验（能省你半天）

- ⚠️ **离屏自检的拦路虎是单实例守卫**（你常开着管理端）⇒ 开发开关
  `STE_ALLOW_MULTI=1 viewer-qt.exe -platform offscreen`。默认不设＝行为不变，**别在教室机上设**。
- ⚠️ **qmllint 只查语法**：查不出未声明属性、属性名错、Row 内横向锚点 ⇒ **只有真跑 exe 才暴露**。
- ⚠️ 日志/输出出口**一律 `toLocal8Bit()`**：源码字面量是 UTF-8，命令行和记事本按 GBK 解 ⇒ 乱码。
  被控端**没有 `logf`**（那是 `math.h` 的 `logf(float)`，编译 C2660）⇒ 日志用 `qInfo` + `qPrintable`。
- ⚠️ 自检拿不到 `viewer-run.log`（进程被 timeout 杀时 stdio 缓冲不刷）⇒ **改抓 stdout**（`logf` 内有 `fflush`），按 GBK 解码。
- **"缺资源类"故障的判据**（踩过三遍）：程序照常启动、照常连云端、**只在用到某功能时才崩/黑屏**
  ⇒ 先怀疑包缺件，别查代码。查崩溃的固定套路：事件日志 `Application Error`(1000) 看**出错模块 + 异常码 + 偏移**，
  偏移三次一致 ⇒ 同一个确定崩溃点；再用"进程开始时间"(FILETIME) 减崩溃时间算出**存活了几秒**。
- 报错文案**必须带系统原话**（`QFile::errorString()`）：只写"目标打不开"会把四种病藏成一个样。

### 3.5 管理端 UI 约定（动 QML 前看）

全部在 `Stelarith-viewer-qt\docs\UI规范-2026-10-07.md`（令牌表 / 字号阶 / 组件白名单 / 9 条禁止写法）。挑常踩的：

- **字号阶只有四档 `12 注 / 13 正 / 14 徽标 / 16 标题`**（+`20` 只给大数字），根窗口 `font.pixelSize: 13` 兜底，`10px` 档不许存在。
- ⚠️ **对比度是算出来的数**（WCAG 相对亮度），不是看出来的。改令牌前后各算一次。
  旧值 `fg3 #5A5A5A` 只有 2.87:1、`fg4 #3A3A3A` 1.74:1（连 3:1 都不到）；现值 `fg3 #8A8A8A` 5.73:1、`fg4 #707070` 4.00:1。
- ⚠️ **`inv` 当底色时字必须用 `win`**（近黑）；页面里写死 `"#111"` 当反白字色，亮色下字直接消失。
  ⚠️ 改令牌必须**全库搜一遍"用这个色值当底色的地方"** —— 反过来"提对比度"这个动作本身会让反白按钮变糊。
- ⚠️ **QML 锚点只能锚 Item**：`ApplicationWindow`/`Window` 继承 QWindow，**不是 Item**，没有 `horizontalCenter`，
  写了**不报错、静默失效**、控件退回 x=0。一律锚 `parent`。
- ⚠️ **抽组件时调用点属性名别和目标组件同名**（`InputField { th: th }` ⇒ 自引用 ⇒ `Binding loop detected`）。一律 `th: root.th`。
- ⚠️ `Layout.preferredWidth` **不是硬约束** ⇒ 内容更宽时会顶宽，从靠 `fillWidth` 的邻居身上抢。改布局前**先量，别猜**。
- ⚠️ 探针读非首屏控件，要**先把那一页切出来**再读（StackLayout 非当前页给负数占位 ⇒ 假警报）。
- 不许直接用 Qt 自带 `Button`/`TextField`（走系统调色板，是黑白界面里唯一会"自己变白"的控件），走 `Btn`/`InputField`。
- 通知参数只走 `qml/NotifyParams.js`（`scripts/test-notify-params.mjs` 16 条断言）：`kind/title/content/seconds?/flags{speech, severity, emergency_confirm?}`，**不发 notice_id**。

---

## 4. 我认为你必知、不知道就会踩的坑

> 按"你最可能撞上的"排序。每条都是**发生过事故**或**会造成静默错误**的。

### 4.1 🔴 你正在改的文件，行尾是 `CR CR LF`

`Stelarith-control-qt\CMakeLists.txt` 的真实行尾是 **`\r\r\n`**（CR CR LF，历史遗留，**不是普通 CRLF**）。
用 Edit 工具做多行替换会 **命中 0 次**；整篇重写/折叠成 `\n` 会把整文件变成 diff 且切分错。
**改法**：脚本按 `"\r\r\n"` split、只换行内容、再 `EOL.join` 回字节。

### 4.2 🔴 CMake 的正则引擎不吃非捕获组 `(?:...)`

写进 `if(... MATCHES "...")` 直接 `RegularExpression::compile(): ?+* follows nothing`，配置期就炸。
只能抽宽松的 `([^"]+)`，**格式校验另起一行**用 `string MATCHES` 做（捕获组、`?`、`\\.` 都支持）。
另外 **CMake 字符串里的字面 `\n` 会被展开成真换行** —— 两条 `string(REGEX REPLACE)` 别写在同一行。

### 4.3 🔴 二进制版本资源只认四段纯数字

`FILEVERSION` / `PRODUCTVERSION` 不接受 `0.6.23-rc.1`。推导规则固定：**核心三段 + 第 4 段永远是 `0`**，
字符串字段才是完整 `0.6.23-rc.1`。旧实现 `string(REPLACE "." ";")` 拆整体串会拆出 `"22-rc"` 这种段。
这是两端 CMake 版本块里**既有逻辑**，你动 `.rc` / 资源块时会被它管着。

### 4.4 🟠 被控端整链被他人缺文件卡住

`Stelarith-control-qt\src\oauthbind.cpp/h` 缺失 ⇒ **`cmake` 到不了 generate 之后**，`.rc` 烧不进 exe、安装器编不出来。
我这轮只验到 configure 阶段。你若在等整链产物，是同一个阻塞，不是你环境的问题。
（顺带：`oauthbind.cpp/h` 归位后，两端 `CMakeLists` 的 115/116 行源文件列表要一起看，见 §6。）

### 4.5 🟠 单实例守卫：改 `main.cpp` 前先知道这两条

- 被控端有 **Global + Local + QLockFile 三道**单实例守卫（`src/singleinstance.{h,cpp}`）。
  开发期想连开第二个实例调试 ⇒ `STE_ALLOW_MULTI=1`（只在开发机设）。
- ⚠️ **进程终止类功能的铁律（蓝屏事故换来的）**：`0x000000EF` 恒为 `CRITICAL_PROCESS_DIED`
  = 某个被内核标记 critical 的进程死了，**不是驱动崩溃**。杀 critical 进程＝自寻蓝屏。
  `tasklist /FO CSV /NH` 的名字**带 `.exe`** ⇒ 两侧必须走**同一个归一化函数**，差一个后缀＝保护名单整体失效。
  别用名字当安全边界：任务计划/服务/系统组件的判据用**镜像路径**（`%SystemRoot%` 下不动）和**会话**
  （`ProcessIdToSessionId`：Session 0 是机器的，不是学生的）。**问内核，别猜名单**：
  `NtQueryInformationProcess(h, ProcessBreakOnTermination = 29)` 就是内核同一个标志位；查询失败一律当保护。
- 被控端 `kProtectedSelf[]` 就在 `main.cpp` 557 行 —— 改它之前请先读完上面这段。

### 4.6 🟠 灵动岛现在有三套实现（你正在写的是第二套）

| 在哪 | 形态 | 状态 | 行位 |
|---|---|---|---|
| 被控端 `main.cpp`（10-04 大屏通知） | `NotifyWindow::Island` | 已在用 | 1861 / 1867 / 1993 / 2067 / 2132 / 3331 |
| 你新写的 `island.{h,cpp}`（14:20） | QWidget 独立类 | ✅ 已进 `CMakeLists` 115–116 行 | 87 行 `.h` + 240 行 `.cpp` |
| 管理端 `Main.qml` | QML `DynamicIsland` | 已在用 | 120（`islandStates`）/ 1412–1483 / 2733 / 3038 |

三套并存 = 改灵动岛的形状/配色/动画要**三个地方各改一次**，而且语义已分叉
（`island.cpp` 走 `setInteractive()` / `setLightMode()`，管理端 QML 走 `{k,t,d,p}` 状态数组）。
收敛成一套是**产品决策**，我不动。

### 4.7 🟠 `AcrylicPanel.qml` 目前是零引用

`qml/components/AcrylicPanel.qml`（96 行，15:26 落盘）里的 `AcrylicPanel` **全仓只有它自己第 9 行的用法注释，无引用点**。
别被"文件刚建好"误导成"已接线"。它的做法（本机 Qt6 `MultiEffect` 在虚拟显示器 + AMD 旧 DX9 上静默降级）
是**实测结论**：改用「缩小 → smooth 放大」近似高斯模糊。这个降级结论对管理端所有毛玻璃需求都成立。

### 4.8 🟡 跨树 `cp` 会带行尾

把另一棵树的 CRLF 文件覆盖进来，提交后 diff 显示"全文件都变了"，把真正的改动淹没
（0.6.14 的 `singleinstance.cpp` 265 行全变）。本仓 `core.autocrlf=true`、**仓库里存 LF**；
提交后若见"全文件 diff"先数 CRLF，用 `git add --renormalize <file>` 修回。
**改完大文件顺手 `git diff --stat` 看一眼行数是否合理。**

### 4.9 🟡 远端推送

- push 走 HTTPS（**不走 SSH**）；**本机 curl 到 github 恒 000**（schannel 吊销检查），git/python urllib 都通 ⇒ 验 API 用 python urllib。
- ⚠️ **GCM（`credential.helper=manager`）push 时弹交互挂死**（`ls-remote` 通 ≠ 凭据正常）。稳妥写法：
  `git -c credential.helper= push "https://<token>@github.com/<owner>/<repo>.git" HEAD:refs/heads/<分支>`
- ⚠️ push 不动先看**主邮箱 verified**（Settings → Emails）：未验证一律 403，而 `ls-remote` 不受影响。
- GitHub API `push_files` 推不全（二进制/gitlink）⇒ 只能走 git。**永远不用 `--force` 推。**

---

## 5. DeepSeek Harness 侧（**由我读盘观察，不做任何处置**）

以下不是你告诉我的，是我看 mtime / grep 推的。列出来只为对齐"你手上正开着哪些活"。

| 文件 | mtime | 是什么 | 接线 |
|---|---|---|---|
| `Stelarith-control-qt/src/island.h` / `island.cpp` | 14:20 / 14:21 | 灵动岛 **QWidget 实现**（不引 QML）：顶部居中胶囊 56px↔168px 缓动、默认交互穿透、深/浅色、`setInteractive()` | ✅ 已进 `CMakeLists.txt` 115–116 行 |
| `Stelarith-viewer-qt/qml/components/AcrylicPanel.qml` | **15:26** | Fluent 毛玻璃面板（零引用，见 §4.7） | ❌ 无引用点 |
| `Stelarith-control-qt/src/app.rc.in` + `src/app.ico` | 13:18 | 版本资源模板 + 图标 | ✅ `CMakeLists` 76/78 行 |
| `Stelarith-viewer-qt/src/app.rc.in` + `src/app.ico` | 13:18 | 同上 | ✅ `CMakeLists` 75/77 行 |
| `Stelarith-control-qt/src/version.nsh.in` | 14:29 | NSIS `!include "build\version.nsh"` 的源文件 | ✅ `CMakeLists` 84 行 |

---

## 6. 重叠面登记（**这张表是本次同步的核心**）

| # | 文件 | WorkBuddy 在碰 | DeepSeek Harness 在碰 | 风险 | 约定（建议，待人批） |
|---|---|---|---|---|---|
| **1** | `Stelarith-control-qt/CMakeLists.txt`（**全文仅 123 行**） | **版本守门 / 核心拆分 / 四段数字**：第 ~20–90 行 | **源文件列表**：115–116 行 `island.cpp/h` | 🔴 **同文件只隔 25 行**。两边都 commit 就整文件冲突；两边都写就是互相覆盖 | **版本段（≤100 行）归 WorkBuddy，源文件列表（>100 行）归 Harness**；新加东西先加在同一半，跨越第 100 行前**先在对话里说一声** |
| **2** | `Stelarith-control-qt/src/main.cpp`（4621 行） | `compareVersion` 1018 行起 | `exam_mode`（50/296）、通知 Island（1831 起） | 🔴 同文件多区间，`git merge` 能合但对**人**是灾难 | 我**只碰 1010–1100 行区间**；其他地方**一律只读**。要挪版本比较逻辑先问我 |
| **3** | 两端 `src/app.rc.in` + 版本资源块 | 四段数字推导（`RC_VERSION_QUAD`）、`version.nsh` 生成 | 图标接入（`app.ico` → 构建目录） | 🟡 同一段 CMake 里既有我的版本推导、又有你的资源接线 | 资源接线（`.rc` / `.ico` / nsh 生成）**归 Harness**；我只改 `.rc.in` 里的**版本字段**与它前面那段版本解析 |

| **4** | `Stelarith-control-qt/build-agent-installer.sh`（现 **100 行**、CRLF） | **版本守门第二段**：1.5 段内追加 `[ build/version.nsh -ot CMakeLists.txt ]` 过期检查（**第 59–67 行**，2026-10-07 晚 15:50） | **1.5 段本身 + 整条 installer 构建流程**：14:29 加的 `version.nsh` 存在性检查 | 🟠 **你在前、我在后，同一段先后追加** —— 我是在他的存在性检查**之后**追加的；他若整段重写会把我的 10 行抹掉，我若插到他前面会顶乱他的行号 | 版本链路的守门**一律追加在 1.5 段末尾**并**当场在本表登记**；他改构建流程前先 `git diff` 一眼 |

> 除此之外两边**基本不相交**：我动构建/发布/版本号，你动端侧功能/组件/资源。
> **这 4 处排好，两边并行就安全**。
>
> **§4 那处重叠的来龙去脉（供你判断要不要收口）**：他 14:29 加的守门只查 `version.nsh` **存不存在**；
> 我 15:50 追加的查它**过不过期**（改了 `set(AGENT_VERSION ...)` 却没重跑 cmake ⇒ nsh 留旧值 ⇒ makensis 不报错、
> 把旧版本号静默编进安装包 ⇒ 装完版本跟 OTA 对不上）。前者防"缺文件"，后者防"错版本"，
> 缺一不可 —— 所以这一处我动了、动完即登记。**要不要把两条并成一条、或整个挪进 CMake，你说了算，我没有再动它。**

---

## 7. 交接协议（两边照办）

**开工前 30 秒（比 `AGENTS.md` §7 多看一条）：**

```bash
# 1) 看对方有没有在动（AGENTS.md §7 原有两条）
git -C <你的工程> status --short
# 2) 最近 30 分钟谁碰过文件 —— 重点看"不是我改但刚被改"的
find <工程>/src -newermt '-30 minutes' -type f
# 3) 【新增】本同步面 §6 那 3 处重叠文件，扫一眼有没有刚被改的
```

**收工前：**

1. 改动落在 §6 三处之一 → **必须在对话里点名**（"我改了 `control-qt/CMakeLists.txt` 第 60 行"）
2. 落在外面 → 照 `AGENTS.md` §3：commit message 写清**改动清单 / 行为差异 / 验证证据（命令+数字）/ 未验证项**
3. 指针仍只写进**指挥台**（本文件不存指针）
4. **本文件由谁动谁补**：WorkBuddy 补 §2、DeepSeek Harness 补 §5；补完更新顶部时间

---

## 8. 待人裁决（**agent 不自行决定，链在这里**）

1. **分工表要不要改**：`AGENTS.md` §1 现在读起来只有一条线，实际是两条。补一行 WorkBuddy 边界？
2. **同一批未提交怎么提**：`control-qt/src/main.cpp` 里有他人在途的 `exam_mode` / 通知 Island。
   要不要我按文件挑出来分两批 commit（版本比较 vs 考试模式）？
3. **发不发版**：两端现在是 `0.6.23-rc.1`，`ota.json` 还是 agent 0.6.22 / viewer 0.6.14。
   先发一个 `0.6.23-alpha.N` 内测，还是直接发 rc？
4. **灵动岛三套实现（§4.6）**：要不要收敛、收敛到哪一端？（只报，不动手）
5. **`AGENTS.md` 要不要加一行指向本文件**：否则对面 agent 不知道有这份同步面。

---

## 9. 边界声明

- 本文件**只登记、不处置**：§5 里观察到的对方在途工作，我一行没改、一次没提交。
- §6 的三条"约定"是**建议**，不是已生效的契约 —— 等你/用户在 §8 点头才成立。
- 本文件与 `AGENTS.md` 冲突时，**以 `AGENTS.md` 为准**（它是上位契约）。
- 所有"我没验的"都写在 §2.4 与 §8，**没验的不当成已验**。
