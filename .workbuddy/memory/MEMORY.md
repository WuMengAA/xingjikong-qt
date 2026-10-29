# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`；批次全账见根目录 `加减归档-*.md`。
> **交接优先读** `.workbuddy/memory/2026-10-29-日记忆与待办.md`（含当日决策/已完成/待办/规范摘要）。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** {cloud,control-qt,viewer-qt}；`D:\Stelarith\` 同名三份是 bare 远端只 push；meta 无 remote。
- **顺带发现 ≠ 点名重点**：只报清单等点单；复制只 cp 不 mv；取证避让：不跑被控端/不动桌面。
- 部署到生产安装目录；工作区只编译。**提交路径限定（永不 `-A`）**，提交前必看 `git diff --cached --stat`；探针不入库。
- **生产动作一律用受管 Python**（禁 cmd.exe / PowerShell 落地）：`C:\Users\Administrator\.workbuddy\binaries\python\versions\3.14.3\python.exe`。

## 1 部署 / 发版
- 生产真源 `C:\Program Files\Stelarith\`（guard 每 10min；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产=换 exe+qml 整目录**。SOP：停任务→杀进程→改名备份→拷件→sha256→Start。
- ⚠️ **换 qml 前必验「import 模块目录非空」+「Qt6*.dll 可达」**（10-20 血案）；用「旧完整树打底 + 新 QML 覆盖」。
- ⚠️ **`_deploy_agent.py` 的 `QML_FILES` 是白名单**：新增面/新目录忘登记 ⇒「新 exe + 旧 QML」，脚本报完成、sha 全过、进程照起，**生产实际少文件**（10-27 实证 `IslandAnnotateFace is not a type`）。已加 `scan_missing_qml()` 兜底，但 `scripts/_*.py` 在 gitignore ⇒ **换机即丢**。
- 云端生产=工作区份 `Stelarith-cloud/`（Cloud/Watchdog 两任务；ota.json 每请求现读，UTF-8 无 BOM）。版本真源=ota.json；stable 先 bump 再发；0.6.45 缺 Controls 勿用。**10-28 现役 rc：agent/viewer 均 0.6.50-rc.5**；`ota.json`/`ota-rc.json`/`assets/pkg/` 均 gitignore。
- **云端改代码必须重启**（Node 不热重载）：`python Stelarith-cloud/scripts/_restart_cloud.py`（schtasks 被拦别用）。
- **viewer 发版**：手改两副本（CMakeLists `VIEWER_VERSION` + `main.cpp kViewerVersion`；`bump-version.mjs set` 会连 control 一起降级，**单品升版不能用**）→ `_build_viewer.py` → `makensis scripts/make-installer.nsi` → `publish-release.mjs publish --product viewer`（stable 与 `--channel rc` 各一次）。
- 443/wss：`control.245959623.xyz/ws/agent|viewer` 回源 8788。GitHub curl 恒 000 ⇒ `--ssl-no-revoke`。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`。**正解=跑项目脚本** `_build_agent.py` / `_build_viewer.py`（subprocess+ninja+自拼 INCLUDE/LIB，SDK 10.0.26100.0，shared 必须带否则 C1083）。MSVC 在 VS **18** BuildTools（MSVC 14.51）；`cmd.exe` 被禁 ⇒ vcvarsall 走不通。
- ⚠️ **`qml.exe` 是 GUI 子系统程序，QML `console.log` 走 OutputDebugString 不进 stderr** ⇒ "exit 0 + 0 字节输出"的假阴性。让它出话的两个环境变量：`QT_QPA_PLATFORM_PLUGIN_PATH=<Qt>/plugins/platforms` + **`QT_ASSUME_STDERR_HAS_CONSOLE=1`**。或直接用仓库自带控制台壳 `viewer/scripts/_qml_probe_runner.cpp`。
- 主题运行时探针：`viewer/scripts/_theme_probe.{cpp,qml,py}`（**必须链接 `QtShadcn.lib`**，见 §3）。
- 出包：agent 用 NSIS（`installer.nsi`，`makensis` 在 `C:/Program Files (x86)/NSIS`）——**本机无 ISCC**，Inno 路线不可用；viewer `make-installer.nsi`；APK `make-apk.ps1`。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta（gitlink，无 remote）。**shared-qml 是 meta 子目录非独立仓库，随 meta 提交**。
- **UI 单一真源 = `shared-qml/ui/theme/Theme.qml` + `ui/App*.qml`（2026-10-29 起 10 件）**。`ui/theme/Md3.qml` 是上一代令牌，存量页面仍在用，P4 退役。
  - App 十件：Button / Card / Chip / Dialog / Input / ListItem / **SwitchRow / SegmentedButton / NavBar / Snackbar**（后 4 件 2026-10-29 新增，meta 提交 1e8afb8）。
- ✅ **2026-10-29：viewer 端 Md3 组件层归零**（47 → 0；viewer 提交 f3cd100 / c23e402 / 0be111f / 699660b / d45a8a2）⇒ viewer 运行时**不再加载 `md3/` 组件库**。
- ✅ **2026-10-30：被控端「全部换新」完成**（control `b47e8b2` + meta `8ddc05b`）——`ash/` 9 件整目录删除、md3 组件层归零、`tok.*`/`tk.*`/`st.*` 三套令牌归零 ⇒ 被控端 QML 只剩 `Theme` + `App*`。剩两档：① viewer 的令牌层 `Md3.*` → `Theme.*`；② 两端去边框后的余量核验。
- **`Theme.neutralAccent`（2026-10-30）**：设 true 时 `accentStrong` 回落 `textPrimary`、`accentStrongForeground` 回落 `background` ⇒ **一处开关把全部选中态/主按钮/开关轨道变中性反白**。被控端「不要紫色」就靠它；**OOBE 是独立 QML 文档，必须在 `OobeWizard.qml` 里再设一遍**（AgentShell 那时没加载）。管理端不设即零影响。
  - `Md3Card → AppCard` 有结构性差异：AppCard 内部是 ColumnLayout ⇒ 子项 anchors 必须改 Layout 附加属性（见今日日志「转换规则」）。
  - 共享库仍缺的分段/开关等已补齐；新件访问 island 用 `../island`（`ui/md3/` 下旧件才写 `../../island`）。
  - ⚠️ **2026-10-29 用户裁决：两端全量采用 QtShadcn，原「被控端零 QtQuick.Controls」铁律废止**。vendor 在 `third_party/qtshadcn`（MIT v0.1.2，49 组件）。
  - **`QT_QUICK_CONTROLS_STYLE=Basic` 必做**（两端 main.cpp 在 app 构造前 qputenv）：QtShadcn 靠覆写 QQC 的 background/contentItem 实现，宿主风格非 Basic 会吃掉覆写 ⇒ "渲染了但样式全不对"的静默故障。
  - **QtShadcn 是 static QML 模块且插件目标未编译**（`optional plugin QtShadcnplugin`、无 .dll）⇒ 真 exe 正常；**裸 qml.exe / 未链接它的宿主会报 `plugin not found`**。故 `Theme.qml` **自带调色板、不 import QtShadcn**。
  - `ThemeManager` **只开放 `primary` 一个覆写口**（其余只读）⇒ 不改三方 `rebuildTokens()`。
- 骨架唯一外壳：viewer `qml/shell/MainWindow.qml`、被控端 `qml/shell/AgentShell.qml`。AGENTS.md §9=UI 红线、§10=工程架构红线、**§10.9=QML 纯净 UI 封杀令**。
- 门禁**十件**在 `shared-qml/scripts/`：file-size（台账 `fixtures/oversize-legacy.json`，**只手工改 lines+plan，禁用 `--update-whitelist`**）/ ui-terms（禁用词 教室机·机器·电脑·节点·黑屏·投屏·学生机 ⇒ 一律「设备」/「X端」）/ ui-contract / action-manifest / manifest-scope / module-registry / agent-qml-deps / island 三件 / **qml-structural（2026-10-29 新增）**。
- 被控端页 = `qml/shell/AgentPages.js` 声明表（按用途过滤）；加页只改这张表。

## 4 现役能力（别再问"是否已实现"，grep blob）
- **设备身份两维**：`(归属码 code, 用途 role)`；role ∈ {classroom, broadcast}；OOBE 先问用途再问码。云端权威 `knownDevices.role`，端侧落 `devrole.txt`。
- 系统总图 `docs/星集控-系统总图-2026-10-25.md`。两条写入红线：**按班写入只在班级看板、按设备写入只在设备集控**。
- 云端班级域：`class-events.js`（**班级归属只认 knownDevices，载荷 classCode 忽略**）/ `class-room.js` / `class-data.js`（**借 class-room 连接避 SQLITE_BUSY**）/ `class-admin.js` / `class-timetable.js`。
- **课表**：完整 ClassIsland 档案 = 72 ClassPlan = 12 群 × 6 天。排序唯一规则＝**ClassPlans 里 AssociatedGroup 首次出现顺序**。`TimeType 0=上课/1=课间/3=行动`。恢复 `scripts/restore-classisland-timetable.py --all`；显示序走 `qml/shell/ClassSort.js`。
- **班级接入码**（设备入班唯一入口）：ClassAdminPane 生成 6 位票 → OOBE 输码 → `/api/device/activate`。**CIMS 时期 `XJK-XXXX-XXX` 路径已删除**。
- 被控端：`media/ThumbnailReporter`（15s/台，黑帧跳，**广播端不上报**）、`core/BlackboardCard`、`BroadcastScheduleSheet` + `BroadcastClubPage`。
- viewer 班级域：`src/class/`（零 WS 依赖）；`pages/class/` 三栏 + 三段。发布中心走 `POST /api/class/publish`（限流用 `distinctContent` 指纹）。
- 人员三档：管理员 / 班主任·老师 / 广播室（**独立入口，不并进 viewer**）。

## 5 踩坑实证（还会再踩）
- **QML 类型被"静默抢占"**：`import "../components"` 的**显式导入优先级高于同目录隐式导入** ⇒ 同名本地包装件永远不会被用到；若包装件又**与自身文件同名**则是自引用。两种都表现为**调用方**报 `Cannot assign to non-existent property "onXxx"`（错误行号不在真凶文件里）。2026-10-30 实证：`qml/oobe/AppDialog.qml` 让整个 OOBE 自 10-29 起加载失败。⇒ 包装件必须换名（已改 `OobeErrorDialog.qml`）+ 内层走别名 `Shared.AppDialog`。
- **`x`/`y` 恒相对父坐标**：同级兄弟定位要写 `sibling.x + offset`，写 `sibling.width - 常量` 会漏掉兄弟自身的 x 偏移（2026-10-30 实证：设置页滚动条因此落在内容区正中，成一条"凭空竖线"，藏了 5 天）。**同理：底色映射看"父是谁"—— 页面(`background`)上的浮起件用 `surface`，面板(`surface`)上的件用 `surfaceVariant`；错用 `background` 会让卡片/输入框在页面上彻底消失。**
- **逐屏截图核验能力（2026-10-30 建）**：`_agent_ui_probe` 会出 20 张（6 页 + 设置 7 分类 + 广播浮层 + 广播机 6 页）→ `build/_objs/_shots/`；`_oobe_wizard_probe` 加 `STE_OOBE_SHOT=1` 出六步图。**offscreen 平台没有字体（全 tofu），必须 `QT_QPA_FONTDIR=C:\Windows\Fonts`；且截图必须等动画落定**（抽屉 360ms，不等会截到"半截抽屉盖住页面"被误判成布局溢出）。
- **布局红线**：班级工作台三栏最小 1220 逻辑宽，默认窗口 1180 ⇒ 必然溢出。多栏页先算「最小宽 vs 内容区宽」；纵向面板必须进 Flickable；页面根 `clip:true`。**「按钮按不动」= 按钮在可视区外**。
- **QML 静态检查全不可信**：同一 Item 两个 `Component.onCompleted` = 致命（整页不加载）；两个同名函数 = 整页崩、进程退出。qmllint/门禁均 0 Error。⇒ 已做成门禁第 10 件；唯一可信出口仍是真 exe/探针运行时。
- **`.pragma library` 必须在前 128 字节内**：Qt 用 `file(STRINGS ... LIMIT_INPUT 128)` 判定，中文注释几行就超 ⇒ pragma 从未被读到，该 JS 被每个 QML 文档重复求值。**放首行，注释在其后**。
- **`copy_directory` 只复制、不删旧件** ⇒ 移动/删除共享 QML 后**必须清产物残留**（否则旧 `components/Md3.qml` 会与 `theme/qmldir` 的 singleton 撞车，报 `qmldir defines type as singleton, but no pragma Singleton found`）。
- **configure 期 QML 扫描结果滞后一个构建周期**（扫的是上一次的旧副本）⇒ 见报错先比对源与副本 sha256，别误判没修好。
- **判同名函数必须按 block id**：`Connections { function onX(){} }` 跨对象同名合法，按文件名判重必误报；且要剥**字符串**不只剥注释。
- **图标名必须对照 `resources/icons/*.svg` 实体**；术语门禁会拦文案，写完先跑 `test-ui-terms.mjs`。
- **LNK1104 = exe 被运行中进程占用**：先按 **PID + exe 路径**确认（并行线常在 `build/` 起实例 ⇒ **不杀**）。冒烟 `kill $!` 对 Windows GUI 无效，必须 `taskkill //PID`。
- **qml-patch 只服务 Android**：桌面 viewer 读安装目录 `qml/Stelarith`，**不消费 qml-patch** ⇒ 桌面 QML 修复只能走 OTA 包。
- **commit 提交的是整个 index**（两次血案）⇒ 提交前必看 `git diff --cached --stat`；跨线用 `git commit -- <paths>`。**判断"某能力是否已实现"必须 grep blob，不信提交信息**。
- **`git stash/pop` 在 `core.autocrlf=true` 下静默改行尾**，`git status` 因双向归一化显示干净 ⇒ **干净 ≠ 字节未变**。
- **API 签名想当然 = 假阴性之源**：`Array.findIndex` 无 fromIndex；`MouseArea.clicked()` 在 Qt6 需 `clicked(null)`；普通 `Row` 写 `Layout.fillWidth` 会崩（需 RowLayout）。
- **跨 TU 引用 main.cpp 符号前先查匿名域**（`ExecOut` 已提到 `core/ExecOut.h`）。
- **被控端日志 UTF-8**：`%LOCALAPPDATA%\xingjikong\agent-qt.log`；`channel.txt`/`agent.env`/`classcode.txt`/`devrole.txt` 同目录。viewer-run.log 追加 ⇒ 轮询带 offset。
- 抓图只用 `_shotpw.py`（DPI 感知+PrintWindow）；BitBlt 在 Win11/DWM 全黑；判有效看 PIL 黑占比<10%。

## 6 能力落点：**被控端优先、岛为主入口**（2026-10-27 用户点名）
- 老师面对的是**被控端那台讲台机**，不是管理端窗口。凡"老师当场要用"的能力（批注/PPT 翻页/截图/回桌面/音量/通知/课表/广播…）**主入口一律放被控端**，且**优先长在灵动岛上**。
- 管理端（viewer）面板是**陪跑**。别把能力只做在 viewer 侧就报"做完"。
- 「做完没有」的判据：**被控端本机不依赖管理端在场就能自己用**。

## 7 顶层指导（2026-10-28 纲领 + 2026-10-29 QtShadcn 规范）
- **QML 纯图形**：只画图/动画/捕获点击；禁 `WebSocket`/`JSON.parse`/复杂逻辑，脏活全归 C++。
- **远程桌面换血**：弃 WebRTC/纯 VNC，改 **DXGI 抓屏 + FFmpeg H264 硬编 + WebSocket 透传**。（`vnc_client.cpp` 实为 540 行真实 libvncclient 封装，"安全"指只连回环不碰公网，不是空桩）
- **被控端 Local API**：本地 `127.0.0.1:8080` HTTP/WS，让 ClassIsland/插件/局域网手机免外网联动。
- **多租户隔离**：以"班级"为分支，查询/指令携带 `class_id`。
- **端侧职责**：viewer=PC 管理端；agent=静默进程+顶部灵动岛（**四态：角标/跑马灯/弹窗/全屏**）+独立设置窗。
- **UI 规范（2026-10-29 用户颁布，取代原 MD3 条目）**：
  1. **去边框**：绝对禁 `border.width`/`border.color`，分区只靠背景色差；
  2. 圆角统一 `Theme.radius.lg`(12–16)；padding/spacing 严格 8 的倍数；
  3. 阴影极柔（4% 黑/blur 20/yOffset 4），禁浓重黑边；浮层用半透明 + MultiEffect 毛玻璃；
  4. 颜色一律 `Theme.color.*`，**页面/组件禁硬编码 Hex**（令牌文件本身是调色板，允许）；
  5. 果冻动效：hover 1.02/150ms、press 0.95 + `Easing.OutBack`/300ms、展开用 `Easing.OutCubic`；
  6. **禁裸用 `Button`/`TextField`**，必须 `AppButton`/`AppInput`；只用 Layout 系列，禁绝对 x/y；
  7. **单文件 ≤300 行**，超了拆 Loader/子组件。
- **工程底线**：日志异步+分级+日轮转+崩溃抓栈；通知防轰炸（同时只一条）；耗时任务扔子线程，UI 卡死 5s 看门狗强杀。
- ⚠️ **已知缺口**：远程桌面 H264 全链路、Local API 鉴权网关、多租户 `class_id` 全链路携带均**待建**；被控端侧（AgentShell/岛/oobe）UI 迁移**未开始**。
