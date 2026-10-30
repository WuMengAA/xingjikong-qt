# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`（**交接优先读当日日志**）；批次全账见根目录 `加减归档-*.md`。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** {cloud,control-qt,viewer-qt}；`D:\Stelarith\` 同名三份是 bare 远端只 push；meta 根仓无 remote。
- **顺带发现 ≠ 点名重点**：只报清单等点单；复制只 cp 不 mv；取证避让：不跑被控端/不动桌面。
- 部署到生产安装目录；工作区只编译。**提交路径限定（永不 `-A`）**，提交前必看 `git diff --cached --stat`；探针不入库。
- **生产动作一律用受管 Python**（禁 cmd.exe / PowerShell 落地）：`C:\Users\Administrator\.workbuddy\binaries\python\versions\3.14.3\python.exe`。

## 1 部署 / 发版
- 生产真源 `C:\Program Files\Stelarith\`（guard 每 10min；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产=换 exe+qml 整目录**。SOP：停任务→杀进程→改名备份→拷件→sha256→Start。
- ⚠️ 换 qml 前必验「import 模块目录非空」+「Qt6*.dll 可达」（10-20 血案）；用「旧完整树打底 + 新 QML 覆盖」。
- ⚠️ **生产会长期落后于工作区**（2026-10-31 实证：生产 exe 停在 10-28，`qml/components` 仅 9 项 vs 工作区 20 项 ⇒ 10-29/30 的 QML 大改从未进生产）。部署前先 dry-run 预检并比对两侧 qml 项数，别默认"生产已是最新"。
- ⚠️ **部署脚本别为了看日志重跑 `--go`**（幂等但会再杀一次进程 + 多产一份备份）。日志单独读，或把输出落文件再 tail。
- 杀进程前先 ctypes 取**真实 exe 路径**确认为生产实例（防误杀工作区 `build/` 下的开发实例，记忆 §5 有同款 LNK1104 教训）。
- ⚠️ `_deploy_agent.py` 的 `QML_FILES` 是白名单：新增面忘登记 ⇒「新 exe + 旧 QML」静默缺文件（10-27 实证）。已加 `scan_missing_qml()` 兜底；但 `scripts/_*.py` 在 gitignore ⇒ 换机即丢。
- 云端生产=工作区份 `Stelarith-cloud/`（Cloud/Watchdog 两任务；ota.json 每请求现读，UTF-8 无 BOM）。版本真源=ota.json；stable 先 bump 再发；0.6.45 缺 Controls 勿用。`ota*.json`/`assets/pkg/` 均 gitignore。
- **云端改代码必须重启**（Node 不热重载）：`python Stelarith-cloud/scripts/_restart_cloud.py`（schtasks 被拦别用）。
- **viewer 发版**：手改两副本（CMakeLists `VIEWER_VERSION` + `main.cpp kViewerVersion`；`bump-version.mjs set` 会连 control 一起降级，**单品升版不能用**）→ `_build_viewer.py` → `makensis scripts/make-installer.nsi` → `publish-release.mjs publish --product viewer`（stable 与 `--channel rc` 各一次）。
- 443/wss：`control.245959623.xyz` 回源 8788。GitHub curl 恒 000 ⇒ `--ssl-no-revoke`。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`。**正解=跑项目脚本** `_build_agent.py` / `_build_viewer.py`（subprocess+ninja+自拼 INCLUDE/LIB，SDK 10.0.26100.0，shared 必须带否则 C1083）。MSVC 在 VS **18** BuildTools（MSVC 14.51）；cmd.exe 被禁 ⇒ vcvarsall 走不通。
- ⚠️ `qml.exe` 是 GUI 子系统，QML `console.log` 走 OutputDebugString 不进 stderr ⇒ "exit 0 + 0 字节"假阴性。让它出话：`QT_QPA_PLATFORM_PLUGIN_PATH=<Qt>/plugins/platforms` + `QT_ASSUME_STDERR_HAS_CONSOLE=1`；或仓库自带控制台壳 `viewer/scripts/_qml_probe_runner.cpp`。
- 主题探针：`viewer/scripts/_theme_probe.{cpp,qml,py}`（必须链接 `QtShadcn.lib`）。出包：agent/viewer 用 NSIS（`makensis` 在 `C:/Program Files (x86)/NSIS`）——本机无 ISCC；APK `make-apk.ps1`。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta（gitlink，无 remote）。**shared-qml 是 meta 子目录非独立仓库，随 meta 提交**。
- **UI 单一真源 = `shared-qml/ui/theme/Theme.qml` + `ui/App*.qml`（10 件：Button/Card/Chip/Dialog/Input/ListItem/SwitchRow/SegmentedButton/NavBar/Snackbar）**。`ui/theme/Md3.qml` 是上一代令牌，存量页面仍在用，P4 退役。
- ✅ **2026-10-29 viewer 端 Md3 组件层归零**（47→0，提交 f3cd100 等 5 件）⇒ 不再加载 `md3/`。✅ **2026-10-30 被控端「全部换新」**（control `b47e8b2` + meta `8ddc05b`）：`ash/` 9 件删除、md3 归零、三套令牌归零 ⇒ 被控端 QML 只剩 `Theme`+`App*`。剩两档：① viewer 令牌层 `Md3.*`→`Theme.*`；② 去边框后余量核验。
- **`Theme.neutralAccent`（10-30）**：true 时 `accentStrong` 回落 `textPrimary`、前景回落 `background` ⇒ 一处开关把全部选中态/主按钮/开关轨道变中性反白。被控端「不要紫色」靠它；**OOBE 是独立 QML 文档，必须在 `OobeWizard.qml` 再设一遍**（AgentShell 那时没加载）。管理端不设零影响。
- `Md3Card→AppCard` 有结构性差异（内部 ColumnLayout ⇒ 子项 anchors 改 Layout 附加属性）。新件访问 island 用 `../island`（`ui/md3/` 旧件才写 `../../island`）。
- ⚠️ **2026-10-29 用户裁决：两端全量 QtShadcn，原「被控端零 QQC」铁律废止**。vendor 在 `third_party/qtshadcn`（MIT v0.1.2）。**`QT_QUICK_CONTROLS_STYLE=Basic` 必做**（两端 main.cpp 在 app 构造前 qputenv），否则覆写被宿主风格吃掉 ⇒ 静默"样式全不对"。**QtShadcn 是 static QML 模块且插件目标未编译** ⇒ 真 exe 正常；裸 qml.exe/未链接宿主报 `plugin not found`；故 `Theme.qml` 自带调色板、不 import QtShadcn。`ThemeManager` 只开放 `primary` 一个覆写口。
- 骨架唯一外壳：viewer `qml/shell/MainWindow.qml`、被控端 `qml/shell/AgentShell.qml`。AGENTS.md §9=UI 红线、§10=工程架构红线、**§10.9=QML 纯净 UI 封杀令**。
- 门禁**十件**在 `shared-qml/scripts/`：file-size（台账 `fixtures/oversize-legacy.json`，**只手工改 lines+plan，禁用 `--update-whitelist`**）/ ui-terms（禁用词 教室机·机器·电脑·节点·黑屏·投屏·学生机 ⇒ 一律「设备」/「X端」）/ ui-contract / action-manifest / manifest-scope / module-registry / agent-qml-deps / island 三件 / **qml-structural（10-29 新增）**。
- 被控端页 = `qml/shell/AgentPages.js` 声明表（按用途过滤）；加页只改这张表。

## 4 现役能力（判断是否实现 ⇒ grep blob，不信提交信息）
- **设备身份两维**：`(归属码 code, 用途 role)`；role ∈ {classroom, broadcast}；OOBE 先问用途再问码。云端权威 `knownDevices.role`，端侧落 `devrole.txt`。
- 系统总图 `docs/星集控-系统总图-2026-10-25.md`。两条写入红线：**按班写入只在班级看板、按设备写入只在设备集控**。
- 云端班级域：`class-events.js`（**班级归属只认 knownDevices，载荷 classCode 忽略**）/ `class-room.js` / `class-data.js`（借 class-room 连接避 SQLITE_BUSY）/ `class-admin.js` / `class-timetable.js`。
- **课表**：完整 ClassIsland 档案 = 72 ClassPlan = 12 群 × 6 天。排序唯一规则＝ClassPlans 里 AssociatedGroup 首次出现顺序。`TimeType 0=上课/1=课间/3=行动`。恢复 `scripts/restore-classisland-timetable.py --all`；显示序走 `qml/shell/ClassSort.js`。
- **班级接入码**（入班唯一入口）：ClassAdminPane 生成 6 位票 → OOBE 输码 → `/api/device/activate`。CIMS 时期 `XJK-XXXX-XXX` 路径已删。
- 被控端：`media/ThumbnailReporter`（15s/台，黑帧跳，广播端不上报）、`core/BlackboardCard`、`BroadcastScheduleSheet`+`BroadcastClubPage`。
- viewer 班级域：`src/class/`（零 WS 依赖）；`pages/class/` 三栏+三段。发布中心走 `POST /api/class/publish`（限流用 `distinctContent` 指纹）。
- 人员三档：管理员 / 班主任·老师 / 广播室（独立入口，不并进 viewer）。

## 4.5 盘点纪律（2026-10-31 立）
- ⚠️ **文档/旧记录的"未完成"可能是过时误判**：10-31 实盘复核连抓两处 —— ①系统总图挂账②「两页设备管控统一」实盘**本就统一**（两页同一 `runActionById` + 同一 `permOk` + 云端 per-device 鉴权），照文档去重做=无效劳动；②P0-2 噪音存证实盘已完成（旧记录记"未完成"）。**动手前先 grep 核验，别信提交信息/文档**。
- ⚠️ **"UI 未接（API 已就位）"这类记载要验 API 是否真存在**：系统总图写"考勤 API 已就位"，实盘云端 grep 考勤/attendance **0 命中** ⇒ 不是接 UI，是要先建云端数据与 API。
- 盘点产出物：`docs/星集控-功能框架总览-2026-10-31.html`（四张面 × 能力 × 完成度）；系统总图文档追加「实盘复核」章节（保留原记录不篡改，只追加）。

## 5 踩坑实证（还会再踩）
- **QML 类型被"静默抢占"**：显式 `import "../components"` 优先级高于同目录隐式导入 ⇒ 同名本地包装件永不被用；包装件与自身文件同名则是自引用。表现=调用方报 `Cannot assign to non-existent property "onXxx"`（行号不在真凶文件）。10-30 实证 `qml/oobe/AppDialog.qml` 让 OOBE 加载失败 ⇒ 包装件换名（已改 `OobeErrorDialog.qml`）+ 内层走别名。
- **`x`/`y` 恒相对父坐标**：兄弟定位写 `sibling.x + offset`；`sibling.width - 常量` 会漏 x 偏移（10-30 实证滚动条成"凭空竖线"，藏 5 天）。**底色映射看"父是谁"**：页面(`background`)上的浮起件用 `surface`，面板(`surface`)上的件用 `surfaceVariant`；错用 `background` 让卡片/输入框彻底消失。
- **逐屏截图核验（10-30 建）**：`_agent_ui_probe` 出 20 张 → `build/_objs/_shots/`；`_oobe_wizard_probe` 加 `STE_OOBE_SHOT=1` 出六步图。**offscreen 无字体（全 tofu），必须 `QT_QPA_FONTDIR=C:\Windows\Fonts`；截图必须等动画落定**（抽屉 360ms）。
- **布局红线**：班级工作台三栏最小 1220 逻辑宽，默认窗口 1180 必溢出。多栏页先算「最小宽 vs 内容区宽」；纵向面板进 Flickable；页面根 `clip:true`。**「按钮按不动」= 按钮在可视区外**。
- **QML 静态检查全不可信**：两个 `Component.onCompleted` = 致命（整页不加载）；两个同名函数 = 整页崩。qmllint/门禁均 0 Error ⇒ 唯一可信出口是真 exe/探针运行时。
- **`.pragma library` 必须在前 128 字节内**（Qt 用 LIMIT_INPUT 128 判定）：中文注释几行就超 ⇒ pragma 从未被读到，JS 被重复求值。放首行。
- **`copy_directory` 只复制不删旧件** ⇒ 移动/删除共享 QML 后必须清产物残留（旧 `components/Md3.qml` 与 `theme/qmldir` singleton 撞车报 `no pragma Singleton found`）。
- **configure 期 QML 扫描滞后一个构建周期**（扫旧副本）⇒ 见报错先比对源与副本 sha256。
- 判同名函数按 **block id**（`Connections { function onX(){} }` 跨对象同名合法）；剥字符串不只剥注释。
- 图标名对照 `resources/icons/*.svg` 实体；写完先跑 `test-ui-terms.mjs`。
- **LNK1104 = exe 被运行中进程占用**：先按 PID+exe 路径确认（并行线常在 `build/` 起实例 ⇒ **不杀**）。`kill $!` 对 Windows GUI 无效，必须 `taskkill //PID`。
- **qml-patch 只服务 Android**：桌面 viewer 读安装目录 `qml/Stelarith` 不消费 qml-patch ⇒ 桌面 QML 修复只能走 OTA 包。
- **commit 提交的是整个 index**（两次血案）⇒ 提交前必看 `git diff --cached --stat`；跨线用 `git commit -- <paths>`。
- **`git stash/pop` 在 autocrlf=true 下静默改行尾**，status 因双向归一化显示干净 ⇒ 干净 ≠ 字节未变。
- **API 签名想当然 = 假阴性**：`Array.findIndex` 无 fromIndex；`MouseArea.clicked()` 在 Qt6 需 `clicked(null)`；普通 `Row` 写 `Layout.fillWidth` 崩（需 RowLayout）。跨 TU 引用 main.cpp 符号先查匿名域（`ExecOut` 已提 `core/ExecOut.h`）。
- 被控端日志 `%LOCALAPPDATA%\xingjikong\agent-qt.log`（UTF-8）；`channel.txt`/`agent.env`/`classcode.txt`/`devrole.txt` 同目录。viewer-run.log 追加 ⇒ 轮询带 offset。抓图只用 `_shotpw.py`（DPI+PrintWindow）；BitBlt 在 Win11/DWM 全黑；判有效看黑占比<10%。
- **删 wrapper 漏补根级闭合括号 → 整页静默空白**（10-30 实证 `BroadcastScheduleSheet.qml`）：整文件少一个 `}` ⇒ 解析失败、不报错、探针 PASS 也看不出（page-level 错误不被 `g_qmlErrors` 捕获）。诊断只能 `qml.exe`(offscreen) 直接加载（报 `Expected token '}'` + `Type X unavailable`）⇒ 删结构 wrapper 后必比对括号配平。
- **跨子目录引用兄弟组件必须 `import "../"`**（10-30 实证）：`qml/shell/pages/` 引 `qml/shell/` 兄弟组件须显式 import；同目录无需。

## 6 能力落点：**被控端优先、岛为主入口**（10-27 用户点名）
- 老师面对的是**被控端讲台机**，不是管理端窗口。凡"老师当场要用"的能力**主入口一律放被控端**，优先长在灵动岛上；管理端面板是**陪跑**。「做完没有」判据=**被控端本机不依赖管理端在场就能自己用**。

## 7 顶层指导（10-28 纲领 + 10-29 QtShadcn 规范）
- **QML 纯图形**：只画图/动画/捕获点击；禁 `WebSocket`/`JSON.parse`/复杂逻辑，脏活全归 C++。
- **远程桌面换血**：弃 WebRTC/纯 VNC，改 **DXGI 抓屏 + FFmpeg H264 硬编 + WebSocket 透传**（`vnc_client.cpp` 为 540 行真实 libvncclient 封装，"安全"=只连回环）。
- **被控端 Local API**：本地 `127.0.0.1:8080` HTTP/WS，让 ClassIsland/插件/局域网手机免外网联动。
- **多租户隔离**：以"班级"为分支，查询/指令携带 `class_id`。
- 端侧职责：viewer=PC 管理端；agent=静默进程+顶部灵动岛（角标/跑马灯/弹窗/全屏）+独立设置窗。
- **UI 规范（10-29 颁布，取代原 MD3 条目）**：① 去边框，禁用 border.*，分区靠背景色差；② 圆角统一 `Theme.radius.lg`(12–16)，padding/spacing 严格 8 的倍数；③ 阴影极柔（4% 黑/blur 20/yOffset 4），浮层半透明+MultiEffect 毛玻璃；④ 颜色一律 `Theme.color.*`，禁硬编码 Hex（令牌文件本身允许）；⑤ 果冻动效 hover 1.02/150ms、press 0.95+OutBack/300ms、展开 OutCubic；⑥ **禁裸用 `Button`/`TextField`**，必须 `AppButton`/`AppInput`；只用 Layout 系列禁绝对 x/y；⑦ **单文件 ≤300 行**，超了拆 Loader/子组件。
- **工程底线**：日志异步+分级+日轮转+崩溃抓栈；通知防轰炸；耗时任务扔子线程，UI 卡死 5s 看门狗强杀。
- ⚠️ **已知缺口**：远程桌面 H264 全链路、Local API 鉴权网关、多租户 `class_id` 全链路携带均**待建**。
- **main 拆分（2026-10-30/31，第一刀→第十五刀完成）**：main.cpp 历史峰值 9026 → **4768** 行（−4258，−47.2%）。**用户裁决「锁死1500没意义」**→ 撤销 1500 硬线，只保留「只降不升」防膨胀护栏（`check-main-size.mjs` 台账 `main-size-baseline.json`）。新增业务一律进 `src/<domain>/`，不堆 main.cpp。**跨 TU 迁移模式（方案 B 依赖注入）**：匿名域符号经 `g_xxxDeps` 上下文注入（main() 同一 TU 取地址），全局量 `g_X`→`(*g_actionDeps->memberX)`（**必加括号**）、函数 `fn`→`g_actionDeps->fn(...)`；文件作用域**外部链接**全局按既有 `extern` 模式复用；匿名域**模型类型**（SchedTask/SoftPolicy/WinCache 等）迁头文件两端共用。已迁模块：`src/action/execute_action.{h,cpp}`、`src/ui/agent_panel.{h,cpp}`、`src/wire/frame_builder.{h,cpp}`、`src/core/proc_inspect.{h,cpp}`、`src/policy/policy_engine.{h,cpp}`、`src/ota/ota_update.{h,cpp}` 等。
  - ⚠️ **跨 TU const 链接性**：C++ 命名空间作用域 `const` **默认内部链接** ⇒ 常量要跨 TU 共享，定义处也必须显式写 `extern const int kX = ...;`（只写 `const` 或 `static const` 都是内部链接，别处引用报 C2065）。同理文件作用域 `static` 全局变量跨不了 TU（#15 的 `g_otaPromptedVer` 就是这样去掉 static 的）。
  - **纯搬迁不动对齐**：连"注释前空格数"都别顺手重排 —— 保真比对脚本会把它判成 DIFF（#15 实证）。