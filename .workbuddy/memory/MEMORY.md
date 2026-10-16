# Stelarith-xingjikong-qt · 项目笔记（速查）
> 详细过程见 `.workbuddy/memory/YYYY-MM-DD.md`（日记，append-only）与根 `docs/*.md`（归档）。
> 本文件只留**仍会影响下一步动作**的事实。整理于 2026-10-16 深夜（19.5K → 压缩）。

## 0 铁律
- 总纲=skill `stelarith-project-overview`；裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。
- 重要修复同轮闭环：属实 bug 直接修；重启/发布/出包同轮做；收尾逐条对账。
- **只改三工程** `{cloud-ws,control-qt,viewer-qt}`；`D:\Stelarith\` 下同名三份是 **bare 远端**（remote 名 `outer`，**只 push 不手改**；push 前 `--dry-run` 证伪 remote 指向源仓自身的空转）。
- **顺带发现 ≠ 点名重点**：只报清单、等点单；不许在烂地基上叠补丁。
- 取证避让：本机不跑被控端/不动桌面；**云端重启让修复生效不算避让**。「复制」只 cp 不 mv；定时任务只推大目标。
- **红线可越**（2026-10-16 用户授权）：停生产/换生产 exe 在验证需要时可以做，但必须**先备份（`.bak-<名>`）+ 汇报**。

## 1 部署 / 发版
- 443/wss：`control.245959623.xyz/ws/agent`(被控)、`/ws/viewer`(管理)；cloudflared 回源本机 8788。令牌 `CLOUD_WS_TOKEN`(设备)/`CLOUD_VIEWER_TOKEN`(管理)/`CLOUD_VIEWER_SECRET`(设备票)。本地验证一律 `127.0.0.1:8788`。
- 版本真源＝云端 `ota.json`（UTF-8 无 BOM；**带 BOM ⇒ latest:null**）。`bump-version.mjs` 改 4 副本，**出包前先 bump**。发布 `publish-release.mjs publish --product agent|viewer --version X --file <包>`；验证 `curl 127.0.0.1:8788/api/public/ota?product=X[&current=旧]`。
- ⚠️ `ota.json` 不入库（线上文件即真源）；`ota.json.url` 同时是装机卡链接 ⇒ **只挂全量件**。
- **线上（2026-10-16 深夜）：agent/viewer 均 0.6.36；本机生产 agent 已手动换成含 QML 面板的 0.6.37 build（未走 OTA，备份 `.bak-pre-u5a3`）**。⚠️ kAppVersion 已是 0.6.37。
- **换被控端 exe 必须连 `qml/` 整目录一起换**（QML 运行期从 exe 旁读盘；安装目录 qml/ 是装机时快照，混版 ⇒ `UiRegistry is not a type` 之类，fail-loud 可在 agent-qt.log 看到）。

## 2 编译 / 自检
- Qt **6.12.0** `D:/QTqwq/6.12.0/msvc2022_64`（qml.exe/qmllint.exe/cmake 同级）；被控端入口 `control-qt/scripts/build-agent.sh`（Git Bash 直连 cmake/ninja 自喂 MSVC/SDK+RC/MT）；`_build_viewer.py` 默认 -j4。
- ⚠️ MSVC C1060 ⇒ 串行 `-j1`。改 CMakeLists 源列后 ninja 不一定重跑 configure ⇒ 手动 `cmake -S . -B build`（**必须带脚本同款 -D**：build/CMakeCache 的 `CMAKE_PREFIX_PATH` 曾被污染成 `D:/Qt/6.8.1`，裸 reninja 即炸，而 build.ninja 里还是旧 6.12 值所以平时能编）。
- ⚠️ `file(GLOB)` 不自动重查 ⇒ 改 resources/ 文件名后必须重跑 configure。
- 离屏自检：`STE_ALLOW_MULTI=1 ... -platform offscreen`；offscreen 无 CJK 字体 ⇒ 探针要 `QFontDatabase::addApplicationFont(msyh.ttc)`；Windows Qt 日志走 OutputDebugString ⇒ 判定看**退出码**（10=过/20=败/30=兜底超时），看文字加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`。
- **QML 页面探针必须针对产物树跑**（`qml/island/`、共享 ui 只在 build/qml 下；源树没有）⇒ 跑前 cp 同步产物。
- **AUTOMOC 三连坑（2026-10-16 全实测，header-only QObject 必读）**：
  ① 被源文件 include 的头会被**单独编译成一个 TU** ⇒ 头里不能引用 main.cpp 的任何符号（C2065）⇒ 用「快照+信号」模式：头只持纯内存成员，真实读写放 main.cpp 的 wire/fill（fill 用 friend 直接改成员，**不能 static**——friend 声明是 external linkage）；
  ② 头里不能 `#include "xxx.moc"`（C1083；AUTOMOC 走独立 moc_<name>.cpp 路线）；
  ③ 头必须**显式列入 add_executable**（否则 moc 产物不生成 ⇒ LNK2001 metaObject）。
  另：include 自家头**绝不能放进匿名 namespace**（Qt 头被拽进域内 ⇒ QVariant/QtPrivate C2872 fatal C1003）；Qt 6.12 `QQuickWidget::ResizeMode` 只有 `SizeViewToRootObject/SizeRootObjectToView`（无 ToQML）。

## 3 仓库 / UI / NSIS
- github `WuMengAA/xingjikong-qt` 四分支；curl 到 github 恒 000，push 用 `git -c credential.helper=`。
- 根仓库即 meta（三子工程 gitlink，无 .gitmodules）：子仓提交后 `git add <子目录>` 推进指针；根仓库无 remote。
- QML：字号 12/13/14/16(+20)；**锚只锚 parent**；不用 Qt Button/TextField ⇒ Btn/InputField；令牌实例 id 用 `tok` 不用 `md3`（子组件属性同名 ⇒ `md3: md3` 自引用绑定环）。
- 符号字形（▶♪、emoji）教室机 tofu ⇒ 图标用 `SvgIcon` 或单字汉字。
- NSIS：`.nsi` UTF-8 带 BOM；非 Qt 运行时显式 File。提交路径限定（**永不 `-A`**）。
- **「真 viewer 壳」探针范式**：复用全部 obj 只换 main.cpp.obj + `@rsp` 链接；必备补 `logf`、自己装 messageHandler、`build/_interact_run/` 备齐 Qt dll、抄足 DEFINES+INCLUDES+**FLAGS**。`build.ninja` 的 exe 行含 `|` 依赖段，抓 obj 先截断。
- **假上下文探针**：属性/函数**必须照真实签名写**（bulkList 教训：声明成数组，页里当函数调 ⇒ TypeError 假象污染判断）。

## 4 已修 / 绑定 / 库
- #87 语音时长限；#90 关机重发；#94 暗色通知；#95 终端关不掉；#88 考试模式。
- `ViewerBackend::cloudUrl()` 是 **WebSocket 地址**，管理面 `/api/*` 走 `httpBaseUrl()`；viewerbackend.cpp 另有 5 处同款派生**未收敛（待单独一笔）**。
- 云端 `/recordings/*` 静态托管用 `RECORDINGS_ROOT`（const TDZ ⇒ 定义上移）。
- qml-patch 已并入共享岛 + version 计岛指纹；`CLOUD_ASSETS_DIR` 可覆盖。
- 库：WebRTC(libdatachannel) + FFmpeg(LGPL 动态)；Qt 动态链接；OpenSSL3 取自 PG17。CIMS 废弃。
- ⚠️ **待点单**：① U4 的 `permOk("admin")` 恒真越权（teacher 可锁屏，quickGuard 只保了快捷行）；② **IslandRoot 引用 `image://svgicon/check-circle` 而资源叫 `circle-check.svg`（存量图标名错配，岛历史列表图标加载失败，日志 948 行起）**；③ viewerbackend 5 处 URL 派生未收敛；④ 站点 `/api/device/activate` 不回 classCode。

## 5 状态（2026-10-16 深夜）
- **U 系列（「插件化思维，统一界面结构」）**：U1–U4 完成（共享 ui 基座 Md3+md3/12 件、契约 UiContract.js+UiRegistry+UiPanelHost、14 面板声明化 Main 3858→3237、44 动作单一真源 ActionDefs.js）；**D 档完成**（双绘常量统一+`test-island-metrics.mjs` 11 断言门禁；IslandRoot 2207→2148 抽出 IslandIdleFace/IslandActionRow 2/12 块 + `diff-island-tree.mjs` 基线比对器）。
- **U5（两端同 shell，用户裁决 A）· A-1/A-2/A-3 第一批全部完成**：
  - **A-1** 共享 `ui/UiPageHost.qml`(182，页无 open/close、全预建、ctx 转发 panelHost) + SamplePage + 页探针 28 断言。
  - **A-2** viewer 三页抽两页：`pages/SettingsPage.qml`(575，Main −542) + `pages/ControlPage.qml`(814，Main −781 ⇒ **Main.qml 现 1914 行**)。概览页未抽（362 行/root. 8 处）。**StackLayout 仍直接管三子项，UiPageHost 未接管**。
  - **A-3 第一批**（control `2b30a7a`）：被控端 QML 控制面板——`src/agent_ui.h`(AgentUi+AgentShell) + `qml/shell/`(AgentShell 壳+AgentPages.js 声明+AgentSettingsPage 设置页=openInfoDialog 真实迁移) + 托盘「控制面板…」+ AUTOOPEN 验证开关。**真机已上线**（生产 exe 已换 + qml 全量同步，备份在；心跳/注册/帧流正常，面板加载 0 QML ERROR）。**剩余 4 页**（自检/插件市场/记录/播放时段）QDialog 并存待逐批迁；被控端页面规划=5 页。
- **island**：A 档（管理端跑共享 IslandRoot，IslandHost 几何真源未统一）+ C 档（qml-patch 并岛+MD5 门禁）完成；扩展组件 1–7 + 审批面板随 0.6.36 上线。
- ⏸ 隔离验证 245 过/0 败 + HTTP 审批 17/17。挂起的都是真机类（考试模式内核闸门、#98 岛观感、terminal cd、file_push）。
- 出包前须再 bump（0.6.37 已在本机，云端 0.6.36）。

## 6 同步 / UI 模板
- 指挥台回写真源 `docs/星集控-指挥台-唯一真源-2026-10-02.md` §一；收工即回写；`.workbuddy/memory/` 是私有记忆 ≠ 同步。
- **Material 3 = 全项目唯一视觉模板**，令牌真源 `Stelarith-shared-qml/ui/Md3.qml`；组件库 `md3/` 12 件（Md3Btn/Card/SwitchRow/NavBar/…）。
- HTML 展示页放 `docs/<中文名>.html`；资源目录纯 ASCII；探针截图源 `D:/Stelarith/_probe/shots/` 只 cp 不 mv；台账不另立 md。
- 探针脚本：viewer `.gitignore:34` `scripts/_*.py` 不入库、`_*.qml/_*.cpp` 入库（control 同款：`_*.py/_*.mjs` 不入库）。Bash cwd 不跨命令保持，每次显式 cd。
- 固化成测试的成例：`test-ui-contract.mjs`(32断) / `test-action-manifest.mjs`(23断+fixture) / `test-island-metrics.mjs`(11断) / `diff-island-tree.mjs`（剥 QML 匿名类名序号、时钟与动画相位分别忽略、--rename 显式声明预期改名；**不许放大 --tol 糊**）。

## 7 踩坑实证（最贵）
- ⚠️⚠️ **删组件判据必须是「类型名」不是「路径」**（U3 抓到 A 档活体回归：`SvgIcon {` 靠目录导入解析）。
- ⚠️ **qmllint 给假安全感**（不查目录导入类型缺失、0 Error 也能加载失败）——唯一可靠出口是**运行时探针**；抽完 QML 块的纪律：**awk 括号平衡 → 加载探针 → 最后才 qmllint**。
- ⚠️ **QML 隐式作用域是迁移地雷**：裸标识符（th/isPhone）搬文件后链断且无声；抽前系统扫根对象成员，不只看 `root.` 前缀。`property var X: Y` 的 RHS 替换防自引用（`theme: theme` 环）。
- ⚠️ **跨父级锚被静默丢弃**；`anchors.centerIn: parent` 依赖 parent 几何；锚链对不可见 item 依然起效（隐藏须 height:0 清零）。
- ⚠️ 往共享目录加新扩展名文件必须查两端收集方式（桌面 copy_directory 照单收、Android 显式 GLOB）。
- ⚠️ 同一动作多入口的**门控口径必须逐字保留**（三种门控并存是 U4 发现的安全现状）；排序键按面分开（order/quickOrder/palOrder）；合并注册表 ⇒ id 全局唯一。
- ⚠️ 探针演示态编号 ≠ QML 状态机编号须映射；`setProperty` 同步但锚链重算等事件循环（+100ms）；动画/注入要写的属性不能锚；`Repeater` delegate 的 objectName 不在 QObject 树（走 childItems() 递归）；QML 祖先链 enabled 传导（禁用控件的子孙收不到事件）。
- ⚠️ QML 匿名类名（`_QML_21`）同代码不同跑次也会变 ⇒ 基线比对先剥序号；探针参数「看着该生效」先验它真生效（--actions 实测没注入动作）。
- ⚠️ 测量方法自身的错会污染判断：划范围看缩进层级+闭合标记（粗量把页外算进依赖）；`uniq -c -f1` 在"行号:匹配"上不生效。
- ⚠️ **门禁正则剥注释再匹配**（`/kMinContentW/` 会命中说明该缺口的注释 ⇒ 假红）。
