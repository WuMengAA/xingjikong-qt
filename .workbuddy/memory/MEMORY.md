# Stelarith-xingjikong-qt · 项目速查（2026-10-31 压缩版）
> 细节看 `.workbuddy/memory/YYYY-MM-DD.md`（**交接优先读当日**）；批次全账见根目录 `加减归档-*.md`。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** {cloud,control-qt,viewer-qt}；`D:\Stelarith\` 同名三份是 bare 远端只 push；meta 根仓无 remote。
- **顺带发现 ≠ 点名重点**：只报清单等点单；复制只 cp 不 mv；取证避让：不跑被控端/不动桌面。
- 部署到生产安装目录、工作区只编译。**提交路径限定（永不 `-A`）**，提交前看 `git diff --cached --stat`；探针不入库。
- **生产动作一律受管 Python**（禁 cmd/PowerShell 落地）。多线并行时同一文件（尤其 `CMakeLists.txt`）两边都在改 ⇒ 只暂存自己那条 hunk（`git apply --cached` 片段补丁），绝不 `git add` 整文件。

## 1 部署 / 发版
- 生产 `C:\Program Files\Stelarith\`（guard 每 10min；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产 = 换 exe + qml 整目录**：停任务→杀进程(先 ctypes 取真实 exe 路径确认为生产实例)→改名备份→拷件→sha256→Start。
- ⚠️ 换 qml 前验「import 模块目录非空」+「Qt6*.dll 可达」；旧完整树打底 + 新 QML 覆盖。**生产会长期落后**（10-31：生产 exe 停 10-28、components 9 vs 20 项）⇒ 部署前 dry-run 比对两侧 qml 项数。
- ⚠️ `_deploy_agent.py` 的 `QML_FILES` 是白名单（新增面忘登记 ⇒「新 exe + 旧 QML」静默缺件）；别为看日志重跑 `--go`（会再杀进程+多产备份）；`scripts/_*.py` 在 gitignore ⇒ 换机即丢。
- 云端=工作区 `Stelarith-cloud/`（Cloud/Watchdog 两任务，ota.json 每请求现读 UTF-8 无 BOM）。版本真源=ota.json；stable 先 bump；0.6.45 缺 Controls 勿用。**改代码必须重启** `scripts/_restart_cloud.py`。
- **viewer 发版**：手改两副本（`VIEWER_VERSION` + `kViewerVersion`，`bump-version.mjs` 会连 control 降级⇒单品不能用）→ `_build_viewer.py` → `makensis` → `publish-release.mjs publish --product viewer`（stable / rc 各一次）。443/wss 回源 8788；GitHub curl 恒 000 ⇒ `--ssl-no-revoke`。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`；**正解=项目脚本** `_build_agent.py`/`_build_viewer.py`（subprocess+ninja+自拼 INCLUDE/LIB）。MSVC 在 VS **18** BuildTools；cmd 被禁 ⇒ vcvarsall 走不通。
- ⚠️ `qml.exe` 是 GUI 子系统 ⇒ console.log 不进 stderr（"exit 0 + 0 字节"假阴性）⇒ 加 `QT_QPA_PLATFORM_PLUGIN_PATH` + `QT_ASSUME_STDERR_HAS_CONSOLE=1`。
- 出包：NSIS（`C:/Program Files (x86)/NSIS`，本机无 ISCC）；APK `make-apk.ps1`；主题探针须链 `QtShadcn.lib`。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta（gitlink，无 remote）；**shared-qml 是 meta 子目录，随 meta 提交**。
- **UI 真源 `shared-qml/ui/`**：`theme/Theme.qml` + `App*.qml`（Button/Card/CardTitle/Chip/Dialog/EmptyState/InfoRow/Input/ListItem/SwitchRow/SegmentedButton/NavBar/Snackbar）。`theme/Md3.qml` 是上一代令牌，仅存量页在用，P4 退役。10-29 viewer / 10-30 被控端 md3 与 ash 均已归零 ⇒ 两端只剩 `Theme`+`App*`。
- `Theme.neutralAccent`（10-30）：true 时 accentStrong 回落 textPrimary ⇒ 一处开关全站中性反白（被控端"不要紫色"）。**OOBE 是独立 QML 文档，须在 `OobeWizard.qml` 再设一遍**。
- ⚠️ 两端全量 QtShadcn（`third_party/qtshadcn`）；**`QT_QUICK_CONTROLS_STYLE=Basic` 必做**（main.cpp 在 app 构造前 qputenv），否则覆写被宿主风格吃掉=静默"样式全不对"。QtShadcn 是 static QML 模块、插件目标未编译 ⇒ `Theme.qml` 自带调色板不 import 它。
- `Md3Card→AppCard` 内层是 ColumnLayout ⇒ 子项改 Layout 附加属性（`height`→`Layout.preferredHeight`）。新件引 island 用 `../island`，跨子目录引兄弟组件须 `import "../"`。
- 外壳：viewer `qml/shell/MainWindow.qml`、被控端 `qml/shell/AgentShell.qml`；被控端页表 `qml/shell/AgentPages.js`（按用途过滤，加页只改这张表）。AGENTS.md §9 UI 红线 / §10 架构 / §10.9 QML 纯净封杀令。
- 门禁十件 `shared-qml/scripts/`：file-size（台账 `fixtures/oversize-legacy.json`，**只手工改 lines+plan，禁 `--update-whitelist`**）/ ui-terms（禁 教室机·机器·电脑·节点·黑屏·投屏·学生机 ⇒ 用「设备」「X端」）/ ui-contract / action-manifest / manifest-scope / module-registry / agent-qml-deps / island 三件 / qml-structural。
- ⚠️ file-size 有多条**他线存量红项**（viewer main/mobile/MonitorPage、cloud index/registry、island、Md3、`execute_action.cpp`、`agent_panel.cpp` 645>600）⇒ 只看"自己那条有没有变胖"。

## 4 现役能力（判断是否实现 ⇒ grep blob，不信提交信息）
- **身份两维** `(归属码, 用途)`；role∈{classroom,broadcast}；云端权威 `knownDevices.role`，端侧 `devrole.txt`。
- 系统总图 `docs/星集控-系统总图-2026-10-25.md`；写入红线：**按班写入只在班级看板、按设备写入只在设备集控**。
- 云端班级域 `class-events.js`（归属只认 knownDevices）/ `class-room.js` / `class-data.js`（借其连接避 SQLITE_BUSY）/ `class-admin.js` / `class-timetable.js`。
- **课表**：72 ClassPlan（12 群×6 天），排序=ClassPlans 里 AssociatedGroup 首次出现顺序；`TimeType 0课/1课间/3行动`。恢复 `scripts/restore-classisland-timetable.py --all`；显示序 `qml/shell/ClassSort.js`。
- **班级接入码**：ClassAdminPane 生成 6 位票 → OOBE 输码 → `/api/device/activate`（CIMS 的 `XJK-XXXX-XXX` 已删）。
- 被控端：`media/ThumbnailReporter`（15s/台、黑帧跳、广播端不上报）、`core/BlackboardCard`、`BroadcastScheduleSheet`+`BroadcastClubPage`；记录/自检数据面 `p3log/p3_reader`（文本）+ `p3log/p3_record_view`（结构化行，10-31 新增）。
- viewer 班级域 `src/class/`（零 WS）；`pages/class/` 三栏+三段；发布走 `POST /api/class/publish`。人员三档：管理员 / 班主任·老师 / 广播室。

## 5 盘点纪律（10-31 立）
- ⚠️ 文档/旧记录的"未完成"可能是**过时误判**（10-31 连抓两处：设备管控"未统一"实盘本就统一；P0-2 噪音存证已完成）。
- ⚠️ "UI 未接（API 已就位）"要验 API 真存在（考勤 API 记载 vs 云端 grep 0 命中）。
- 动手前先 grep 核验；产出物 `docs/星集控-功能框架总览-2026-10-31.html`；系统总图只追加「实盘复核」不改原文。

## 6 踩坑实证
- **QML 类型被静默抢占**：显式 `import "../components"` 优先于同目录隐式导入 ⇒ 同名本地包装件永不生效；与自身同名=自引用。症状=报 `Cannot assign to non-existent property "onXxx"`（行号不在真凶文件）⇒ 换名。
- **`x`/`y` 恒相对父坐标**：兄弟定位写 `sibling.x + offset`；`sibling.width − 常量` 漏偏移（"凭空竖线"藏 5 天）。**底色看父是谁**：页面(background)上的浮起件用 surface，面板(surface)上的用 surfaceVariant。
- **同色即消失**：`secondary` 按钮在 `muted`(surfaceVariant) 卡上完全同色 ⇒ 看着是一行字（10-31 实证，AppEmptyState 默认改 `solid`）。
- **逐屏截图**：`_agent_ui_probe`（20 张）→ `build/_objs/_shots/`；**offscreen 必须 `QT_QPA_FONTDIR=C:\Windows\Fonts`**；截图等动画落定（抽屉 360ms）。⚠️ 该脚本原先 os.remove 旧报告会被安全删除守卫拦死（runs 0 秒、报告还是上一版）⇒ 已改为不删、靠覆写。
- **QML 静态检查全不可信**：两个 `Component.onCompleted`=整页不加载；两个同名函数=整页崩。唯一可信出口是真 exe/探针运行时。
- **删 wrapper 漏补根级 `}`** ⇒ 整页静默空白且 page-level 错误不被 `g_qmlErrors` 捕获（探针 PASS 也看不出）⇒ 只能 qml.exe(offscreen) 直载。
- **`.pragma library` 须在前 128 字节内**（中文注释几行就超 ⇒ 从未生效、JS 重复求值）⇒ 放首行。
- **`copy_directory` 只复制不删旧件** ⇒ 移动/删共享 QML 后清残留（否则 `no pragma Singleton found`）；configure 期 QML 扫描滞后一周期 ⇒ 比 sha256。
- **布局**：班级三栏最小 1220 逻辑宽（默认窗 1180 必溢出）；纵向面板进 Flickable；页面根 `clip:true`。「按钮按不动」=按钮在可视区外。
- **API 签名想当然=假阴性**：`Array.findIndex` 无 fromIndex；Qt6 `MouseArea.clicked(null)`；普通 `Row` 写 `Layout.fillWidth` 崩（要 RowLayout）；跨 TU 引 main 符号先查匿名域。
- **LNK1104 = exe 被占用**：先按 PID+exe 路径确认（并行线常在 `build/` 起实例 ⇒ 不杀）；`kill $!` 对 GUI 无效，用 `taskkill //PID`。
- **commit 提交整个 index**（两次血案）⇒ 必看 `--cached --stat`；跨线 `git commit -- <paths>`。`git stash/pop` 在 autocrlf 下静默改行尾（status 干净 ≠ 字节未变）。
- 图标名对照 `resources/icons/*.svg`；写完先跑 `test-ui-terms.mjs`。qml-patch 只服务 Android（桌面 QML 修复只能走 OTA）。
- 被控端日志 `%LOCALAPPDATA%\xingjikong\agent-qt.log`（UTF-8），`channel.txt`/`agent.env`/`classcode.txt`/`devrole.txt` 同目录。抓图只用 `_shotpw.py`（BitBlt 在 Win11/DWM 全黑，判有效看黑占比<10%）。

## 7 落点与顶层指导- **被控端优先、岛为主入口**（10-27）：老师面对讲台机；判据=**被控端本机不依赖管理端在场就能自己用**。
- **QML 纯图形**：只画图/动画/捕获点击；禁 WebSocket/JSON.parse/复杂逻辑，脏活归 C++（**统计/汇总也要 C++ 算好再给 QML**）。
- **UI 规范（10-29）**：去边框禁 border.*；圆角 `Theme.radius.lg`；间距 8 的倍数；阴影极柔；颜色只用 `Theme.color.*`（禁 Hex）；果冻动效；**禁裸 Button/TextField**；只用 Layout 禁绝对 x/y；单文件 ≤300 行。
- **中文排版（10-31 立，设计稿实证）**：字重只用 **400 / 700**（雅黑无中间档，写 500/600 会被合成加粗、笔画粘连）；正文行高 **1.8**（汉字无升降部，同数值比西文挤）；最小字号 **12px**（10.5–11 汉字糊）；行宽 **≤40em**；**中文不加 letter-spacing**；弱文字对比度 **≥5:1**（原 #6E6E78=3.54:1 不达 AA）；全角标点 + 中英之间留一空格。
- 远程桌面换血：DXGI + FFmpeg H264 + WS 透传（`vnc_client.cpp` 是 libvncclient 封装，只连回环）。Local API `127.0.0.1:8080`（**10-31 有人在建 `src/localapi/`，未提交**）。多租户以"班级"为分支携带 `class_id`。
- **main 拆分**：9026 → 4768 行，撤销 1500 硬线只留「只降不升」（`check-main-size.mjs`）。新业务进 `src/<domain>/`。跨 TU：匿名域经 `g_xxxDeps` 注入、全局 `(*g_xDeps->member)`（**必加括号**）、函数 `g_xDeps->fn(...)`。
  - ⚠️ 命名空间作用域 `const` 默认内部链接 ⇒ 跨 TU 须显式 `extern const`；文件作用域 `static` 同理。**纯搬迁不动对齐**（连注释空格都别重排）。

## 8 viewer 打包与外壳现状（11-01 立，并行线实证）
- **打包三坑**：① `file(GLOB)` 必须加 `CONFIGURE_DEPENDS` —— 否则补资源后重建 exe 尺寸纹丝不动、图标根本没进包；② NSIS `SetOverwrite` 默认 **off** ⇒ 升级装 rc=0 但 exe 还是旧的；`Stop-Process` 杀不掉别的 Session 的进程 ⇒ exe 被占 ⇒ 对策 **Rename 让路**；③ 改 `.nsi` 会**丢 UTF-8 BOM** ⇒ NSIS 报 "Bad text encoding"，必须用脚本保 BOM。
- **「viewer 双击打不开」根因在构建层**：`qt_add_qml_module` 的 `QtShadcnplugin` 没人依赖 ⇒ 从未构建，且模块目录从未进包。修法＝`add_dependencies` + 整目录同步 + 目标缺失则 CMake 报错。
- **外壳真相**：10-29 只迁了 9 个**页面**，**外壳从未迁移**；`qml/layout/DesktopConsole.qml` 一直是假数据设计稿。`qml/shell/PreviewConsole.qml`（439 行）＝其真数据版，由 `backend.previewShell` 预览闸切（**默认关，不是切上线**）。**缺口**：预览时其余四栏目空白、**无系统设置内容分支**、抽屉/批量/状态条/竖屏 BottomNav/岛/Toast/ReauthDialog 全未接 ⇒ 切生产前必须补齐。
- **命名纪律**：`backend`＝main.cpp 注册的**全局 context property**（子件直接可用）；`backendRef`＝转发名，**子件若自行声明同名会自绑定 → Binding loop → 该子树全 undefined**。`MainWindow` 曾因裸 `backendRef` 为 null 导致权限门控/班级双写/设备统计走空分支。

