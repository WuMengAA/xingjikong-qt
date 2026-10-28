# Stelarith-xingjikong-qt · 项目速查
> 细节见 `.workbuddy/memory/YYYY-MM-DD.md`；批次记录见根目录 `加减归档-*.md`。

## 0 铁律
- 裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。属实 bug 直接修并同轮闭环。
- **只改三工程** {cloud,control-qt,viewer-qt}；`D:\Stelarith\` 同名三份是 bare 远端（outer）只 push；meta 无 remote。
- **顺带发现 ≠ 点名重点**：只报清单等点单；复制只 cp 不 mv；取证避让：不跑被控端/不动桌面。
- 部署到生产安装目录；工作区只编译（隔离预览例外）。提交路径限定（永不 -A）；探针不入库。

## 1 部署 / 发版
- 生产真源 `C:\Program Files\Stelarith\`（guard 每 10min；互斥体 `Global\StelarithAgentQt_Singleton`）⇒ **换生产=换 exe+qml 整目录**。SOP：停任务→杀进程→改名备份→拷件→sha256→Start；装机按字节核对不看版本号。
- ⚠️⚠️ **换 qml 前必验「import 模块目录非空」+「Qt6*.dll 可达」**（10-20 血案：Controls 空目录+缺 DLL）；用「旧完整树打底+新 app QML 覆盖」。
- ⚠️⚠️ **`_deploy_agent.py` 的 `QML_FILES` 是白名单**：新增一个面或整棵新目录（如 `annotate/`）忘了登记 ⇒「新 exe + 旧 QML」，脚本报完成、sha 全过、进程照起，**生产实际少文件**（10-27 实证：`IslandAnnotateFace is not a type`，岛整块退回手绘兜底）。已加 `scan_missing_qml()` 兜底（构建树 vs 生产树差集→自动补拷+校验）；但 `scripts/_*.py` 在 gitignore，**该修复不入库、换机会丢**。
- 云端生产=工作区份 `Stelarith-cloud/`（Cloud/Watchdog 两任务；ota.json 每请求现读，UTF-8 无 BOM）。版本真源=ota.json；stable 先 bump 再发；**0.6.45 缺 Controls 运行库勿用**。**10-28 现役（rc 渠道）：agent 0.6.50-rc.5 / viewer 0.6.50-rc.5**（viewer rc.5 含关窗 0xC0000005 修复；agent rc.5 是 10-27 包，**不含** 10-28 设置页图标/导航修复，下次发 agent 包才带上）；`ota.json`/`ota-rc.json`/`assets/pkg/` 均 gitignore（只有 ota-gray.json 入库）。
- **云端改代码必须重启**（Node 不热重载）：`python Stelarith-cloud/scripts/_restart_cloud.py`（杀 8788 owner→CREATE_NO_WINDOW 直拉 node→轮询；schtasks 被安全策略拦，别用）。重启副作用：`ensureQmlPatch()` 按 viewer 工作树重打包 qml-patch 并只留当前 zip ⇒ 若重启时正有人在 viewer 跑 git 命令，版本标签会回落成 mtime 标签（内容相同，见 10-25 记录）。
- **viewer 发版**：手改两副本（CMakeLists VIEWER_VERSION + src/main.cpp kViewerVersion；`bump-version.mjs set` 会连 control 一起降级，**单品升版不能用它**）→ `python scripts/_build_viewer.py` → `makensis scripts/make-installer.nsi` → `node scripts/publish-release.mjs publish --product viewer --version <v> --file dist/<exe> --base-url https://control.245959623.xyz`（stable 与 `--channel rc` 各发一次）。
- 443/wss：`control.245959623.xyz/ws/agent|viewer`；回源 8788。GitHub curl 恒 000 ⇒ `--ssl-no-revoke`。

## 2 编译 / 自检
- Qt 6.12.0 `D:/QTqwq/6.12.0/msvc2022_64`。**正解 = 跑项目脚本** `python Stelarith-control-qt/scripts/_build_agent.py` / `Stelarith-viewer-qt/scripts/_build_viewer.py`（内部 subprocess+ninja+自拼 INCLUDE/LIB，SDK 10.0.26100.0，shared 必须带上否则 C1083）。MSVC 在 VS **18** BuildTools（不是 2022，MSVC 14.51）；`cmd.exe` 在两工具都被禁 ⇒ vcvarsall 路线走不通。
- 离屏自检 `STE_ALLOW_MULTI=1 -platform offscreen`（viewer 需 `QT_QPA_PLATFORM_PLUGIN_PATH=<Qt>/plugins/platforms`）；探针退出码 10 过 / 20 败 / 30 兜底。出包：agent ISCC / viewer makensis / APK make-apk.ps1。

## 3 仓库 / UI / 门禁
- github `WuMengAA/xingjikong-qt`；根仓即 meta（gitlink，无 remote）。⚠️ shared-qml 是 meta 子目录非独立仓库，随 meta 提交。
- QML 纪律：字号 12/13/14/16；锚只锚 parent；禁原生控件；NSIS .nsi 带 BOM。
- **UI 单一真源 = `shared-qml/ui/Md3.qml`+12 件组件**；骨架唯一外壳=viewer `qml/shell/MainWindow.qml`、被控端 `qml/shell/AgentShell.qml`。AGENTS.md §9=UI 红线、§10=工程架构红线；**§10.9=QML 纯净 UI 封杀令**（QML 禁 `XMLHttpRequest`/`WebSocket`/`fetch`/`JSON.parse`，脏活全归 C++）。
- 门禁九件在 `shared-qml/scripts/`：file-size（`fixtures/oversize-legacy.json` 47 项台账，**回写只手工改 lines+plan，禁用 `--update-whitelist`**）/ ui-terms（扫字符串字面量，**禁用词含 教室机·机器·电脑·节点·黑屏·投屏·学生机 ⇒ 一律「设备」/「X端」**）/ ui-contract / action-manifest / manifest-scope / module-registry（既有 7-3 失败）/ agent-qml-deps / island 三件。
- 被控端页 = `qml/shell/AgentPages.js` 声明表（`manifestFor(role)` 按用途过滤，order 过滤后重排）；加页只改这张表。

## 4 现役能力（别再问"是否已实现"，查提交与归档）
- **设备身份两维**：`(归属码 code, 用途 role)`。role ∈ {classroom 教室端, broadcast 广播端}，白名单；**OOBE 第一步问用途、第二步问码**，选完即生效（`roleChosen` 不等绑定成功）。云端权威：`knownDevices.role`，register 只听首次自报，`registered` 回执下发覆盖；端侧落 `devrole.txt`。用途决定外壳（广播端首屏=广播台、无记录页）。
- 系统总体设计见 `docs/星集控-系统总图-2026-10-25.md`（四张面 / 身份主线 / 两条写入红线：按班写入只在班级看板、按设备写入只在设备集控 / 数据落点 / 挂账）。
- 云端班级域：`class-events.js`（订阅按班/推送按班/环形 200 条，**班级归属只认 knownDevices，载荷 classCode 一律忽略**）、`class-room.js`（消息+成员，sqlite）、`class-data.js`（学生名单学号幂等 + 班级课表 ≤512KB，**借 class-room 连接避免 SQLITE_BUSY**）、`class-admin.js`（建/改/删/成员/发布/学生/课表；readJsonBody 上限已从 10KB 提到 1MB）、`class-timetable.js`（ClassIsland 档案归一化 → `schema=classisland/v1`）。
- **课表（2026-10-25 晚，12 班已恢复）**：完整 ClassIsland 档案 = **72 个 ClassPlan = 12 群 × 6 天 = 1~12 班**，12 个群**全叫「新课表群」**。排序唯一规则＝**ClassPlans 里 AssociatedGroup 的首次出现顺序**（`ClassPlanGroups` 声明序在改名后会整体错一位，实测 0/12）；群名只有在全部能解析成唯一「N班课表群」时才采信（现役 2 群档案 1班/8班）。`TimeType 0=上课 / 1=课间 / 3=行动`。云端现有 1班…12班（`cls-*`）+ 遗留「测试班」`cls-fbe3c89dc4`（挂生产设备 N7-20091211，未动）。恢复脚本 `scripts/restore-classisland-timetable.py --all`（幂等，自动挑群最多的档案）。班级**显示**序走 `qml/shell/ClassSort.js`（ClassNav + TopBar 共用，只排序不动 code）。
- 发布中心 P1：viewer ClassPublishDialog → `POST /api/class/publish` → `adminPublish` → 按班推 `blackboard-card` + Timeline `publish`；限流用 `opts.distinctContent`（内容指纹）避免"不同内容被 1s 限流吞掉"，缩略图防刷不受影响。
- **班级接入码（设备入班唯一入口，2026-10-25 收归我方）**：管理面板 `ClassAdminPane` 生成 6 位票（云端 `/api/class/bindcode`，viewer 令牌 + 按班权限，24h+绑班+一次性）→ 设备 OOBE 输码 → 云端 `/api/device/activate` 兑换绑定。**CIMS 时期站点 `XJK-XXXX-XXX` 路径已整体删除**：cloud `exchangeSiteActivationCode()` + XJK 分支移除、control `codeFormatOk`/`applyClassBinding` 不再识别、OAuth client id `xjk_device_public` 保留不动。详情见 `加减归档-班级接入码收归我方-2026-10-25.md`。
- 被控端：`media/ThumbnailReporter`（15s/台，黑帧跳，q50→35→25；**广播端不上报**）、`core/BlackboardCard`（黑板卡→复用 notifyFromParams，不另写渲染器）、`BroadcastScheduleSheet`（广播站排期）+ `pages/BroadcastClubPage`（广播社独立身份）。
- viewer 班级域：`src/class/` ClassService/DeviceModel/EventModel/ThumbProvider（零 WS 依赖，出海走 sendRequested）；`pages/class/` 三栏 + 学生/成员/展板三段 + ClassRightRail（右栏可滚）。设置页已拆分类（598→132），个人中心独立成页（头像落点，不设 `system:settings` 门禁）。
- 人员三档：管理员 / 班主任·老师 / 广播室（**保持独立入口，不并进 viewer**）。缺口：viewer 班级列表仍用全量 `/api/classes`，`adminMyClasses()` 已能按 membership 过滤但未接线。

## 5 踩坑实证（还会再踩）
- **布局红线**：班级工作台三栏硬性最小 932 + nav240 + 边距48 = **1220 逻辑宽**，默认窗口 1180 ⇒ 必然溢出。多栏页先算「最小宽 vs 内容区宽」，不满足走断点（<860 切 ClassPortraitStack）；纵向面板必须进 Flickable；页面根 `clip:true`。**「按钮按不动」= 按钮在可视区外**，不是信号绑定问题。
- **QML 静态检查全不可信**：同一 Item 两个 `Component.onCompleted` = 致命（`Property value set multiple times`，整页不加载）；两个同名函数 = `Duplicate method name` 整页崩、进程退出（qmllint/门禁 0 Error 全失明）。唯一可信出口＝真 exe/探针运行时（viewer `_build_class_admin_probe.py` 三套、OOBE `_oobe_probe.py` 真例+假站点）。
- **图标名必须对照 `resources/icons/*.svg` 实体**（`volume-x`/`settings-2`/`book-open` 都不存在）；一次性核对：qml 树 `name:"…"|icon:"…"` 与文件名求差集。
- **术语门禁会拦文案**：`教室机/机器/电脑` 禁用（→「设备」/「X端」）；写完先跑 `test-ui-terms.mjs` 再提交。
- **LNK1104 = exe 被运行中的进程占用**：先按 **PID + exe 路径**确认是谁的实例（并行线常在 `build/` 起实例做现场验证 ⇒ **不杀**，报告等其收尾）。冒烟进程 `kill $!` 对 Windows GUI 无效，必须 `taskkill //PID`。
- **qml-patch 只服务 Android**：`main.cpp` 的 `qml-live/` 分支在 `#if defined(Q_OS_ANDROID)` 内；桌面 viewer 读安装目录 `qml/Stelarith`，**不消费 qml-patch** ⇒ 桌面 QML 修复只能走 OTA 安装包。
- **commit 提交的是整个 index**（两次血案）：①`git add A B` 不清掉别处已 staged 的内容；②2025-10-25 并行线 `bffa5a4` 用它把别人的 main.cpp 改动一起提交。**提交前必看 `git diff --cached --stat`**；跨线只提交指定路径用 `git commit -- <paths>`；改历史禁止，按 `d5202ea` 先例在提交信息/台账双处登记"提交信息名不副实"。
- **判断"某能力是否已实现"必须 grep blob**，不信提交信息（`git show HEAD:src/main.cpp | grep -c 关键字`）。
- **`git stash/pop` 在 `core.autocrlf=true` 下静默改行尾**：工作树 LF→CRLF，`git status` 因双向归一化**显示干净**；会让写死 `\n` 的门禁正则静默失配（已修 `test-action-manifest`→`\r?`）。**`git status` 干净 ≠ 工作树字节未变**；门禁输出异常时看全文，别 grep 总结词。
- **`test-file-size.mjs --update-whitelist` 破坏性**：重写整本台账、冲掉别人的拆分计划与 note（89 增 90 删）。正解＝手工改 lines + plan 末尾追加归属。`git checkout -- <file>` 撤销会连带丢他人在途未提交改动。
- **调试测量两坑**：多实例（自己的+并行线的）会让截图对不上号；PrintWindow 在 DPI 缩放下给缩放位图 ⇒ 宽度以程序自报（`console.log`→stderr）为准，像素扫描只作旁证。
- **探针构建**：link 目标目录必须先 makedirs（否则 LNK1104 像被占用）；cl/link 输出 GBK vs 探针 exe UTF-8 要分场合解码；moc 复用按头文件是否 Q_OBJECT（QQuickImageProvider 没有）；**CMake/ninja 链接是 `cmd→vs_link_exe→link.exe @rsp` 三层**，rsp 构建后即删 ⇒ 自拼 link 要解析 `build/build.ninja` 的 build 语句与变量区。
- **API 签名想当然 = 假阴性之源**：`Array.findIndex` 无 fromIndex 参数（传第 4 参全失效）；`MouseArea.clicked()` 在 Qt6 需 `clicked(null)`；QML 同名函数/属性避 `on`+大写开头；普通 `Row` 写 `Layout.fillWidth` 会崩（需 RowLayout）。
- **幂等护栏要并查全部等价状态**：`cur = subscribed ? code : pending` 三选二 ⇒ 已订 A 订 B 时漏判 pending，连点发两条；正解 `(subscribed && code==c) || (!pending.isEmpty() && pending==c)`。
- **跨 TU 引用 main.cpp 符号前先查匿名域**（类型也跨不了，`ExecOut` 已提到 `core/ExecOut.h`）。
- **被控端 agent 日志是 UTF-8**：`%LOCALAPPDATA%\xingjikong\agent-qt.log`（含 `.log.1`）；`channel.txt`/`agent.env`/`classcode.txt`/`devrole.txt` 同目录。viewer-run.log 追加 ⇒ 轮询带 offset。
- **监察三戒**：`GetWindowThreadProcessId` 在 **win32process**；回调里 except 吞异常会连演假阴性；`EnumWindows` 不过滤可见性（要加 `IsWindowVisible`）；本环境看不到新建控制台窗 ⇒ 桌面可见性只能现场验。
- 抓图只用 `_shotpw.py`（DPI 感知+PrintWindow）；BitBlt 屏幕 DC 在 Win11/DWM 全黑；判有效看 PIL 黑占比<10%；找主窗按类名过滤 MessageWindow/IME 再取最大。

## 6 能力落点：**被控端优先、岛为主入口**（2026-10-27 用户点名）
- 用户原话：「记住这些功能大多是加入到被控端，而且大部分和灵动岛有关」。
- 含义：课堂里老师面对的是**被控端那台讲台机**，不是管理端窗口。凡是"老师当场要用"的能力
  （批注/画图、PPT 翻页、截图、回桌面、关当前程序、音量、通知、课表、广播…）
  **主入口一律放被控端**，且**优先长在灵动岛上**（岛的面签/展开页/动作行），伸手可点。
- 管理端（viewer）面板是**陪跑**：远程替老师操作、看状态、批量下发才用。
  别把能力只做在 viewer 侧就报"做完"——那等于老师上课时够不着。
- 「做完没有」的判据：**被控端本机不依赖管理端在场就能自己用**。
