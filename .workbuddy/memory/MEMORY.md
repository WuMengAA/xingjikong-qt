# Stelarith-xingjikong-qt · 项目笔记（速查）
> 过程细节见 `.workbuddy/memory/YYYY-MM-DD.md`（append-only）与根 `docs/*.md`（归档）。
> 只留**仍会影响下一步动作**的事实。三次压缩 2026-10-17（19.6K → ~10K）。

## 0 铁律
- 总纲=skill `stelarith-project-overview`；裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。
- 属实 bug 直接修并同轮闭环；收尾逐条对账。
- **只改三工程** `{cloud-ws,control-qt,viewer-qt}`；`D:\Stelarith\` 下同名三份是 **bare 远端**（只 push 不手改）。
- **顺带发现 ≠ 点名重点**：只报清单、等点单；不许在烂地基上叠补丁。
- 取证避让：不跑被控端/不动桌面；「复制」只 cp 不 mv。红线可越（已授权）但**先备份 + 汇报**。

## 1 部署 / 发版（最容易错）
- ⚠️ **生产真源＝`C:\Program Files\Stelarith\`**，不是 build 产物（计划任务 `StelarithAgentGuard` 每 10 分钟按进程名拉起）。
  ⇒ **换生产 = 换安装目录 exe + `qml/` 整目录**（QML 运行期从 exe 旁读盘；混版 ⇒ `UiRegistry is not a type`）。
- 线上现状（2026-10-17）：安装目录 **0.6.44**；`ota.json` agent=0.6.44(stable)、viewer 0.6.37；`ota-gray.json` 卡在他线 0.6.43。
- ⚠️ **C 盘长期告急**（剩 10–850MB）：**打安装包 / 整树 cp 会失败**；安装目录只留 `_appqml-bak-pre-*` 小备份。
- 443/wss：`control.245959623.xyz/ws/agent`(被控)、`/ws/viewer`(管理)；cloudflared 回源 8788；本地验证一律 `127.0.0.1:8788`。
- 版本真源＝云端 `ota.json`（**UTF-8 无 BOM，带 BOM ⇒ latest:null**）。`bump-version.mjs` 改 4 副本，**出包前先 bump**；
  发布 `publish-release.mjs`；`ota.json.url` 同时是装机卡链接 ⇒ **只挂全量件**。
- ⚠️ **stable 发布必须"先 bump 再发"**（用户裁决）：bump → 重编 → `build-agent-installer.sh`（**Inno Setup 6**，ISCC 在
  `%LOCALAPPDATA%\Programs\Inno Setup 6\`）→ publish。⚠️ bump 会同时改 viewer 2 处 ⇒ viewer 仓要单独提交版本号。
- **「OTA 不生效」= 三层叠加**（已全修）：①端侧只在注册回执看一次版本 ⇒ `otaSelfCheck()` 周期复查；②stable 长期没发布；
  ③`mandatory:false` 无人值守永不升 ⇒ 「非考试 + 桌面空闲≥10min」自动装。查：`curl 127.0.0.1:8788/api/public/ota?product=agent&channel=stable&current=X`。
- ⚠️ **同一工作树有并行工作线**：他线 `a1e915c`(H264 addSSRC) / `03bafbc` 已提交，`src/ldc_streamer.*`、`main.cpp` 曾同时在途。
  **提交前先 `git log`**；同文件有他人改动时**别指望"留着不提交"**（我的 #171 修复就这样被他们 commit 带走）。
  装机后按字节核对"装的包是否含我的改动"，不看版本号。

## 2 编译 / 自检
- Qt **6.12.0** `D:/QTqwq/6.12.0/msvc2022_64`；被控端 `control-qt/scripts/_build_agent.py`（viewer 用 `_build_viewer.py`，-j4）。
- ⚠️ MSVC C1060 ⇒ `-j1`。改 CMakeLists 源列后手动 configure（**带脚本同款 -D**，`CMAKE_PREFIX_PATH` 曾被污染成 6.8.1）；
  `file(GLOB)` 不自动重查 ⇒ 改 resources 文件名后必须重跑。
- 离屏自检：`STE_ALLOW_MULTI=1 ... -platform offscreen`；offscreen 无 CJK ⇒ 加 `msyh.ttc`；日志走 OutputDebugString ⇒
  判定看**退出码**（10 过/20 败/30 兜底），看文字加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`。
- ⚠️ **探针 QML 暂存来自 `build/qml`** ⇒ 改共享岛后先 `cp shared-qml/island/*.qml build/qml/island/`。
- ⚠️ **探针链的是 agent 的 `island.cpp.obj`** ⇒ 改 enum/头文件后**先重编 agent**（否则新枚举落 else：
  实测 Banner(3) 变 Fullscreen 752×752 而 form 仍 capsule）。判据：几何=全屏尺寸却串是 capsule ⇒ 先疑 obj 混版。
- **AUTOMOC 四连坑**：①被 include 的头会单独编译成 TU ⇒ 头里不能引用 main.cpp 符号；②头里不能 `#include "x.moc"`；
  ③含 Q_OBJECT 的头**必须显式列入 add_executable**（否则 LNK2001）；④moc 对 .cpp 输入生成的 TU 不含原文件 ⇒ 夹具拆头。
  include 自家头**绝不能进匿名 namespace**（C2872/C1003）。`LNK1104` = 隔离实例占用 build 产物（先看 ExecutablePath 再杀）。

## 3 仓库 / UI / 探针
- github `WuMengAA/xingjikong-qt` 四分支；curl 到 github 恒 000，push 用 `git -c credential.helper=`。
- 根仓即 meta（三子工程 gitlink，无 .gitmodules）：子仓提交后 `git add <子目录>`；根仓无 remote。
- QML 纪律：字号 12/13/14/16(+20)；**锚只锚 parent**；不用 Qt Button/TextField；令牌实例 id 用 `tok` 不用 `md3`；
  `Flickable` 必须显式 `contentWidth: width`；符号字形（▶♪）教室机 tofu ⇒ `SvgIcon` 或单字汉字。
- NSIS `.nsi` UTF-8 带 BOM；提交路径限定（**永不 `-A`**）。
- 探针 `scripts/_*.py`/`_*.mjs` 不入库（白名单 `_build_agent.py`、`_voicehub_fake_server.mjs`）；`_*.qml/_*.cpp` 入库。
- 门禁（`shared-qml/scripts/`）：`test-ui-contract`(32) `test-action-manifest`(23) `test-island-metrics`(11)
  `test-manifest-scope`(15) `test-module-registry`(10) `test-agent-qml-deps`(12) `test-island-forms`(21) `diff-island-tree`。

## 4 已交付（细节查 git log / docs 归档）
- U1–U5 / D 档：共享 ui 基座（Md3+12 件）、契约 `UiContract`+`UiRegistry`+`UiPanelHost`、14 面板声明化、44 动作单一真源、
  双绘常量+漂移门禁、`ui/UiPageHost.qml`、被控端控制面板 5 页（`2b30a7a`/`a8b843e`）。
- 岛基座：管理端跑共享 IslandRoot + 被控端 qml-patch 并岛（MD5 门禁）；扩展组件 1–7 + 审批面板随 0.6.36。
- **内置模块层**（`b851e67`）：`src/modules/`(modulehost/usb/voicehub) 编进 agent，与进程外 PluginHost 并列；
  `executeAction` 内置优先；`ModuleAgentApi` 10 个 std::function 在 main.cpp 注入。
- **U盘模块**（`b851e67`）：WM_DEVICECHANGE + Centered 询问；只开根目录、不自动运行；考试静默。
- **VoiceHub**（`92be4c6`）：SSE `{base}/api/music/websocket` 主 + 30s 轮询 `GET /api/music/broadcast` 兜底（clamp 5s）+
  本地 tick + staleSec 自愈（10s）+ 开播提醒。控制三件（点歌/歌词/广播下发）**留桩默认关**（x-api-key 未定）。
- **多界面**（`f12f79b`）：`face` 0概/1媒/2史/3态 + `faceNow` + `facesOn` + `facePanelOn`；右上竖排面签 22×16；概览面零改动。
- **形态五档**（`3a82ec5`）：Capsule/Centered/Fullscreen/Banner/Mini；Banner 通栏（可用宽−48 ≤1280，高 72/60，单行 elide）；
  Mini 右下角标（112 方牌 / 96 正圆，点一下升格）；`showWithActions` 有按钮强制 Centered；`paintCompactForm()` 是手绘退路。
  开关 `STE_QT_ISLAND_DEMO_FORM=banner|mini|mini-nodesc|centered|fullscreen`。
- **G2 圆角 + 大卡片**（`87afff8`）：`IslandSurface.qml`（Canvas 超椭圆 n=5）六档圆角；C++ `steG2Path()` 同口径；
  Centered/Fullscreen 几何改用 **availableGeometry**；补 `IslandCardFace`/`IslandFullscreenFace`（此前这两档套胶囊版式 = "丑"）。
- **通知记录与逻辑**（`c2ba193`）：`HistoryItem` 加 `id/dateGroup/category/level/read`；`historyCategoryOf()` 空串=不进历史；
  四分类 告警>指令>系统>消息；未读/全部已读/删除；筛选**收口在 `historyOrder()`**（history 与 showHistoryItem 共用防错位）；
  筛选行默认收起、收起即清筛子；老数据无 `read` ⇒ 已读。
- **通知时间**（`dd34d80` + 根仓 `983e30f`）：岛上本体按形态显示 `noticeTime`；`m_shownTs` 在 showIsland 落点
  （ts=发生时刻，不是归档时刻）；`historyTimeOf`/`historyTimeFullOf`/`noticeTimeOf` 三个口径；
  timeStr/dateGroup **bridge 组装时现算**，不用 JSON 死值。归档 `docs/归档-通知写上时间-2026-10-17.md`。
- **WebRTC #171**（已随 `a1e915c` 入库）：`renegotiate` 先 `Rollback` 再显式 `Type::Offer`；见 §6。

## 5 待点单（只报清单，等用户点头）
1. `permOk("admin")` 恒真越权（teacher 可锁屏；quickGuard 只保快捷行）。
2. IslandRoot 引用 `image://svgicon/check-circle` 但资源叫 `circle-check.svg`（历史图标加载失败，日志刷）。
3. viewerbackend 5 处 URL 派生未收敛（`cloudUrl()` 是 WS，管理面走 `httpBaseUrl()`）。
4. 站点 `/api/device/activate` 不回 classCode。
5. `md3/Md3Btn.qml:11` 自定义 `enabled` 盖掉 `Item.enabled` ⇒ 祖先 enabled 传导失效。
6. 管理端下发 `kind=banner|mini` 的入口（viewer 未改，解析已在 main.cpp 就位）。
7. **stable 0.6.44 不含 G2 / 通知记录 / 通知时间三批** ⇒ 是否 bump ≥0.6.45 重打包（等他线收口一并 bump，别抢版本号）。

## 6 踩坑实证（最贵，按"还会再踩"排序）
- ⚠️⚠️ **时间戳在「事件发生」时落，不在「消费」时算**：历史 ts 原取归档时刻（归档在"被顶掉/收回空闲"）⇒ 10:00 弹的
  记成 10:30，23:59 弹的分到第二天组。**派生时间文本（分组/相对时间）绝不能持久化**：昨天算的「今天」今天看还是「今天」。
  ⇒ 落盘只存原始 ts，显示时现算。取证手法：探针 `--hold N` 让第一条停 N 秒再被顶掉，ts 比归档写入早 ~N 秒即成立。
- ⚠️⚠️ **libdatachannel `setLocalDescription` 的 state 陷阱**（#171）：`Unspec` 按 signaling state **猜类型** —— Stable 时猜
  Offer 所以首次能工作，掩盖了"非 Stable 被静默忽略"。首次 offer 未收 answer ⇒ state 停在 `HaveLocalOffer(1)`，此后任何
  `setLocalDescription(Offer)` 走 `peerconnection.cpp:132` default 分支：`LOG_WARNING + return`（不抛不回调）。
  ⇒ 重新协商**先 `Rollback` 再显式 `Type::Offer`**。判据打在 `signalingState()` 上，别只看"有没有 offer"。
- ⚠️⚠️ **探针跨线程 connect 不带 context ⇒ 直连在库线程跑**：回调里注册 `QTimer::singleShot` 撞
  `QBasicTimer::start: event dispatcher already destroyed`（表现为"函数根本没被调用"）。
  必须 `connect(&s,&S::sig,&s,lambda,Qt::QueuedConnection)`。裸 `PeerConnection` 只 addTrack 不配 mediaHandler/不建 dataChannel 崩 0xC0000409。
- ⚠️⚠️ **截图/几何取证必须声明 DPI 感知**：未 `SetProcessDpiAwareness(2)` 时 `GetWindowRect`/`PrintWindow` 返回**虚拟化坐标**
  ⇒ 右侧一列 + 底部一条永远在截图外，被误判成"没画"。判据：彩条标尺（80 逻辑→120px ⇒ 缩放 1.5）。
- ⚠️⚠️ **qmllint 给假安全感**（不查目录导入类型缺失，0 Error 也能加载失败）——唯一可靠出口是**运行时探针**。
- ⚠️ **被控端部署树没有 `QtQuick.Controls` 也没有 `QtQuick.Shapes`**（有 qml 没 C++ 后端）⇒ 只能用 QtQuick 基础类型；
  自定义路径（G2）只能 `Canvas`。`Rectangle.radius` 是圆弧不是 G2，收益只在 20~28px 卡片档。
- ⚠️ **Canvas 手写超椭圆角**：曲线起点必须等于前一段 `lineTo` 落点（sin/cos 写反 ⇒ 路径自交 ⇒ 非零填充挖空，露白三角）。
- ⚠️ **带 `elide` 的 Text，`implicitWidth` 不是自然宽**（跟 width 走）⇒ 在 width 绑定里读它必成环（真机每秒刷屏）。
  用 `TextMetrics.advanceWidth`。门禁 `test-island-forms` 会拦这一条（我 2026-10-17 被它拦下过一次）。
- ⚠️ **QML 隐式作用域是迁移地雷**：裸标识符搬文件后链断且无声；`property var X: Y` 的 RHS 替换防自引用（`theme: theme` 环）；
  删组件判据必须是**类型名**不是路径。`z` 只在同一父级内比较 ⇒ 盖不住时关下层 `visible`，别加 z。
- ⚠️ **跨父级锚被静默丢弃**；锚链对不可见 item 依然起效（隐藏须 `height: 0`）；普通 `Item` 的 height 不跟 implicitHeight。
- ⚠️ **`setProperty` 后 QML 绑定异步重算**：同帧读派生属性得 `<invalid>`（探针假红）⇒ 注入留 ≥500ms。
- ⚠️ **契约默认值是"对一半人正确"的陷阱**：`UiContract.js` 的 `scope` 默认 `["viewer"]` ⇒ 被控端漏写就全被过滤（零日志）。
- ⚠️ **`qBound` 下限会钳掉探针的"压缩时间参数"** ⇒ 探针等待窗口按**下限值**设计；设计探针前先读 clamp。
- ⚠️ **假服务必须复刻真实载荷形状**：SSE `broadcast_state.data` 挂 nextUp/listeners、轮询端点平级 ⇒ 两种都认；
  线上 SSE **无 `event:` 行**（类型只在 JSON `type`），行尾 `\n`/`\r\n` 必须归一。
- ⚠️ **门禁正则会误杀合法写法 / 命中注释** ⇒ 匹配前先剥注释；断言要锁到具体行，别只查"某处存在"。
- ⚠️ **Windows 原生事件**：`PostThreadMessageW` 投的 WM_DEVICECHANGE **到不了** QAbstractNativeEventFilter；
  SendMessage 到 Qt 顶层窗口能到。`GetLastInputInfo.dwTime` 是 32 位 tick ⇒ 只能与 `GetTickCount()` 做 32 位差。
- ⚠️ **cl 链接步绝不带 /TP**；MSYS/Git Bash 会把 `/EHsc` 当路径 ⇒ cl 参数一律走 Python subprocess（list 形式）。
- ⚠️ **`availableGeometry()` 的 x/y 偏移会让"反推可用区"算错**（本机任务栏在左，ag=(48,0,1659,960)）⇒ 几何核对把 ag 打进日志。
- ⚠️ **嵌套 lambda 捕获 `main` 的局部 `app` ⇒ C2326** ⇒ 定时器接收者用 `qApp`。
- ⚠️ 探针自身也会骗人：QML 匿名类名每次变（基线先剥序号）；`Repeater` delegate 的 objectName 不在 QObject 树；
  参数"看着该生效"先验它真生效；假上下文属性**必须照真实签名**写。
- ⚠️ **写死的"到点才动作"延迟让探针没法取证** ⇒ 新逻辑延迟做成环境变量可调（如 `STE_QT_OTA_AUTO_DELAY_SEC`）。
  隔离实例日志是 **UTF-8**（别 iconv GBK），用 Python `read().decode('utf-8','replace')` 再筛关键词。
