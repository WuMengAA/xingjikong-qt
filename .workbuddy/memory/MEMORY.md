# Stelarith-xingjikong-qt · 项目笔记（速查）
> 详细过程见 `.workbuddy/memory/YYYY-MM-DD.md`（日记，append-only）与根 `docs/*.md`（归档）。
> 本文件只留**仍会影响下一步动作**的事实。整理于 2026-10-16（原 31.4K 字符 → 压缩）。

## 0 铁律
- 总纲=skill `stelarith-project-overview`；裁决序：**生产事故 > 用户点单 > 已就绪未接线 > 战略**。
- 重要修复同轮闭环：属实 bug 直接修；重启/发布/出包同轮做；收尾逐条对账。
- **只改三工程** `{cloud-ws,control-qt,viewer-qt}`；`D:\Stelarith\` 下同名三份是 **bare 远端**（remote 名 `outer`，**只 push 不手改**）。
- **顺带发现 ≠ 点名重点**：只报清单、等点单；不许在烂地基上叠补丁、不许自顾自推进别的工程。
- 取证避让：本机不跑被控端/不动桌面；**云端重启让修复生效不算避让**。「复制」只 cp 不 mv；定时任务只推大目标。

## 1 部署 / 发版
- 443/wss：`control.245959623.xyz/ws/agent`(被控)、`/ws/viewer`(管理)；cloudflared 回源本机 8788（外机 wss 不带端口）。令牌 `CLOUD_WS_TOKEN`(设备) / `CLOUD_VIEWER_TOKEN`(管理)、`CLOUD_VIEWER_SECRET`(设备票)。本地验证一律 `127.0.0.1:8788`。
- 版本真源＝云端 `ota.json`（UTF-8 无 BOM；**带 BOM ⇒ `latest:null`**）。`bump-version.mjs` 改 4 副本（两端 CMakeLists + 两端 main.cpp），**出包前先 bump**。
- 发布：`cloud-ws/scripts/publish-release.mjs publish --product agent|viewer --version X --file <包>`（倒退默认拒）。验证＝`curl 127.0.0.1:8788/api/public/ota?product=X[&current=旧版]`（云端现读生效、不重启）。
- ⚠️ **`ota.json` 被 cloud `.gitignore` 挡住不入库**（线上文件即真源）；⚠️ **`ota.json.url` 同时是装机卡链接 ⇒ 只挂全量件，热修包绝不挂**。
- 打包：`make-hotfix.mjs`（管理=zip 含 exe、被控=NSIS exe）；NSIS 源必须 `build/`。
- **线上现状（2026-10-16 18:22 实测）：agent 与 viewer 均 0.6.36**；包在 `cloud-ws/assets/pkg/`。
- ⚠️ bare 远端 `D:/Stelarith/Stelarith-{control-qt,viewer-qt,cloud-ws}`（`cloud-ws` 2026-10-16 才补建）。**别把 remote 指到源仓自身**（push 恒 `Everything up-to-date`，空转）⇒ 先 `git push --dry-run <path>` 零副作用证伪。

## 2 编译 / 自检
- Qt **6.12.0** `D:/QTqwq/6.12.0/msvc2022_64`（`qml.exe`/`qmllint.exe` 同目录）；被控端入口 `control-qt/scripts/build-agent.sh`（Git Bash 直连 cmake/ninja，自喂 MSVC/SDK+RC/MT）；`build-qt-agent.bat` 已废。
- ⚠️ **MSVC C1060（编译器堆爆）⇒ 串行 `-j1`**（本机提交内存常年见底）。`_build_viewer.py` 默认 `-j4`，加 `-j1` 覆盖。
- ⚠️ **改 `CMakeLists` 源文件列后 ninja 不一定重跑 configure** ⇒ 症状「只有 Linking + LNK2019」⇒ 手动 `cmake -S . -B build`。
- ⚠️ **`file(GLOB)` 不自动重查** ⇒ 改 `resources/` 文件名后必须重跑 configure，否则打进包的是旧路径、**运行期静默取不到资源**。
- 离屏自检：`STE_ALLOW_MULTI=1 ... -platform offscreen`；`qoffscreen.dll` 每编被 windeployqt 删需重补。
- ⚠️ **offscreen 平台无 CJK 字体** ⇒ 探针要 `QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc")` + 设默认族，否则中文全 tofu。
- ⚠️ **Windows 上 Qt 日志走 `OutputDebugString`** ⇒ 控制台看不见。判定一律看**退出码**（探针用 10=过 / 20=败），要看文字加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`。"没输出" ≠ "没跑"。

## 3 仓库 / UI / NSIS / OTA
- github `WuMengAA/xingjikong-qt` 四分支(agent/viewer/cloud/workspace)，main 别强推；curl 到 github 恒 000，push 用 `git -c credential.helper=`。
- **根仓库本身即 meta**（三子工程 `mode 160000` gitlink，无 `.gitmodules`）：子仓提交后 `git add <子目录>` 推进指针；根仓库**无 remote，不 push**。
- QML：字号 12/13/14/16(+20)；**锚只锚 Item → 一律锚 `parent`**；非限定名只查「自身 → 组件根」两级；不用 Qt `Button`/`TextField` → `Btn`/`InputField`。
- ⚠️ **QML 跨父级锚会被静默丢弃**（同父级锚正常）：行要么放同一容器做兄弟互锚，要么锚自己 `parent`，要么用 `*H` 预算公式定位。
- 符号字形（▶♪）教室机会 tofu ⇒ 用 `SvgIcon` 矢量图标（唯一真源在共享岛目录）。
- 进程终止一律**黑名单**（只杀点名），判能否杀问内核 `NtQuery(29/61/25)`；名字双侧剥 `.exe` 转小写比。
- NSIS：`.nsi` UTF-8 **带 BOM**；非 Qt 运行时(libssl/libcrypto/D3Dcompiler/opengl32sw)显式 `File`；删 `InstallDirRegKey` 改 `.onInit`。OTA 版本比较唯一真源＝云端 `ota.js cmpVersion`。提交路径限定（**永不 `-A`**，改动面里常有他人产出与构建产物）。
- **「真 viewer 壳」探针范式**（不重编 viewer 就能跑真类）：复用 viewer 已编好的**全部 obj**，只把 `main.cpp.obj` 换成探针 obj，用**响应文件** `@rsp` 链出探针 exe。四个必备：① 探针须补 `logf`（定义在 `main.cpp`，被顶替 ⇒ 否则 LNK2001）；② 自己 `qInstallMessageHandler` 写 stdout；③ `build/_interact_run/` 备齐 `Qt6*.dll` + platforms/tls；④ 编探针抄足 `DEFINES`+`INCLUDES`+**`FLAGS`**（漏 FLAGS ⇒ C1189 缺 `/Zc:__cplusplus`、C4819 缺 `/utf-8`）。⚠️ `build.ninja` 的 `build viewer-qt.exe:` 行**含 `|`/`||` 依赖段**，正则抓 obj 必须先在 `|` 处截断。参考 `scripts/_interact_rt_probe.cpp`。

## 4 已修 / 绑定 / 库
- #87 语音时长限→Q_PROPERTY；#90 关机重发→云端 no_resend+agent 延后 2.5s+去重；#94 暗色通知→InputField；#95 终端关不掉→m_termWait；#88 考试模式/首启引导。
- 班级绑定唯一入口 `control main.cpp:applyClassBinding()`；站点 `/api/device/activate` 不回 code、classId 不合云端 6-16 ⇒ 修法=站点响应加 classCode（**待授权**）。
- **云端 `/recordings/*` 静态托管改用 `RECORDINGS_ROOT`**（2026-10-16）：原硬编码工程目录 ⇒ 设 `CLOUD_RECORDINGS_DIR` 后「能存能列、点开 404」。⚠️ `const` TDZ ⇒ 定义必须**上移到静态托管之前**（不能只改一行）。连带修 `tests/regression-recordings.cjs`（补令牌 + 清理路径跟随 env）。
- ⚠️ **`ViewerBackend::cloudUrl()` 返回 WebSocket 地址**（`wss://…/ws/viewer`），管理面 `/api/*` 必须走 **`httpBaseUrl()`**（wss→https / ws→http + 截掉 `/ws/…`）。`InteractBridge` 曾直接拼 `cloudUrl()` ⇒ QNAM 不发 ⇒ **审批面板真机恒空**（2026-10-16 真桥探针抓到）。`viewerbackend.cpp` 另有 **5 处同款派生未收敛**（fetchEvents/requestClasses/fetchRecordings/…）**待单独一笔**。
- **`qml-patch` 打包源已并入共享岛 + version 计入岛指纹**（2026-10-16）：原来热更包缺 `island/`（抽共享目录后真源不在 viewer 源树），且 version 只取 viewer HEAD ⇒ 改岛下发不出去。`CLOUD_ASSETS_DIR` 可覆盖，测试务必隔离。
- 库：WebRTC(libdatachannel, MPL-2.0) + FFmpeg(LGPL `--disable-gpl` 动态链接，禁 libx264/265)；Qt 动态链接；OpenSSL3 取自 PG17（待改自带）。CIMS 已废弃非目标。

## 5 状态（2026-10-16 21:00）
- **U 系列（用户方向「插件化思维，统一界面结构」，裁决范围 U1–U4 + D 档并入）**：**U1–U4 全完成**，viewer 的「页 / 面板 / 动作」三个扩展面已声明化。
  - **U1** 共享 UI 基座 `Stelarith-shared-qml/ui/`（Md3 令牌 + md3/ 12 组件），两端 POST_BUILD 拷回 `qml/components/`（相对导入逐字对齐 ⇒ 消费方零改动）；删死代码 `viewer/qml/components/Theme.qml`。viewer `01e4102` / control `514edd0`。
  - **U2** 契约 `ui/UiContract.js`（纯函数真源）+ `ui/UiRegistry.qml` + `ui/UiPanelHost.qml`（取代 `openOnly()` 28 行）+ `ui/samples/`。权限词表**照 `PluginHost::kKnownPermissions`**（⚠️ 真源**带冒号**：`events:noise`/`events:schedule`/`cmd:exec`）。viewer `4153c54`。
  - **U3** `viewer/qml/UiPlugins.js`（14 条面板声明）+ 6 个内联 Popup 抽成 `qml/panels/*.qml`；`openOnly` 28 行→1 行转发（**同名同签名 ⇒ 20+ 调用点零改动**）；Main.qml 3858→3237 行。viewer `4e2341d`。
  - **U4** `viewer/qml/ActionDefs.js`（44 条动作/命令声明，**纯数据**）+ `runActionById()` 唯一执行入口；右栏 5 组 / 快捷行 / 批量条 / 命令面板 38 条**全部从声明派生**。删掉 `quickAction`(7 分支)/`runAction`(d.f 链)/`capByLabel`(6 条手抄)/`capNeedOf`/命令面板 38 条字面量。viewer `05c3ade`（+589/−235）。
  - 归档 `docs/统一界面结构-U系列-加减归档-2026-10-16.md`（§一~§十一：U1/U2/U3/U4 各一节全账）。
  - **D 档（2026-10-16 晚 · 用户点单「U5 与 D 档」）**：① 跨父级锚**早已修完**（2026-10-14 `mediaRow` 移进 `expandedCol`，仅需记录无需改码）；② **双绘常量已统一**：`IslandTheme.shapeIdle 36→44`、`shapeExpanded 168→320`（旧值是管理端 `DynamicIsland` 残留，其中 expanded **零消费=死令牌**），新增跨仓门禁 `shared-qml/scripts/test-island-metrics.mjs`（11 断言，负向先跑 8/3）；③ **拆分完成 2/12**：抽 `island/IslandIdleFace.qml`(73) + `island/IslandActionRow.qml`(47)，`IslandRoot` **2207→2148 行**，三态探针**零差异**；新增基线比对器 `shared-qml/scripts/diff-island-tree.mjs`。归档 `docs/灵动岛-D档-加减归档-2026-10-16.md`（§3.5 有剩余 10 块切缝表 + 依赖清单）。
  - **U5（两端同 shell）· 用户裁决「A」**：被控端现状＝托盘 + **7 个 QtWidgets `QDialog`**（设置/自检/插件市场/噪音记录/自习记录/播放时段/配置）+ 一颗岛，`control-qt/qml/` 里只有两个**不进 exe** 的探针 ⇒ **全项目唯一没走 QML/MD3 的界面**。**A-1 完成**：新增共享 `ui/UiPageHost.qml`(182)（整屏页宿主，与 `UiPanelHost` 同构；页**无 open/close**、显隐归 `currentIndex`、默认全预建＝零行为差异、ctx **零新增字段**只是把 `open/close/closePanels` **转发给 panelHost**）+ `ui/samples/SamplePage.qml`(98) + 探针 `viewer/scripts/_ui_page_probe.qml`(140，**28 断言**)。★ 关键：契约的 `page` 面 **U2 就定义了却一直零使用**（`UiRegistry.navItems()` 早就有）⇒ A 的地基＝**把"页"也声明化**。归档 `docs/两端同shell-U5-加减归档-2026-10-16.md`。
  - **U5 剩余（A-2/A-3 未开始，且都需重链接 exe）**：A-2 = viewer 三页声明化（~1880 行 + 页面对 `root` 的大量属性引用）；A-3 = 被控端 C++ 建 QML 窗口 + 接宿主 + 7 个 QDialog 逐页迁。**被控端页面划分（7 个 QDialog → ? 页）是产品决策，等用户点**。本批**完全没碰 control 的 C++**。
  - ⚠️ 全程**未重链接 exe**（本机生产 viewer 在跑且被占）⇒ POST_BUILD 未真跑、产物手动 `cp` 同步；**未真机**；Android 未构建验证。一致性门禁 `shared-qml/scripts/check-island-parity.cmake` 参数化 `-DLABEL=` / `-DSUBSET=ON`（混放目录用子集、岛用严格）。
- **island 系统性升级 A+C+D**：**C 档完成**（`cloud 5c25962` / `viewer 27dd592` / `control da8fae2`，含构建期 MD5 门禁）；**A 档完成**（`viewer 25b3b42`，+113/−553）：管理端主界面改跑**被控端同一份 `IslandRoot`**，`DynamicIsland`(287)+`IslandClock`+`MarqueeText`+`SvgIcon`+`WhaleMascot` 全部退役；新增 `qml/components/IslandHost.qml`（QML 侧复刻 C++ `geometryForContent` 尺寸预算，**几何真源尚未统一**）。⚠️ 外观变化（36高/400宽/alpha230 → 56高/640宽/alpha140）**640 宽可能需布局适配，未做**；**D 档未开始**。归档 `docs/灵动岛系统性升级-AC档-加减归档-2026-10-16.md`。
- **灵动岛扩展组件 1–7 + 管理端审批面板**：已全部完成、入库、**随 0.6.36 上线**（实证＝云端 `assets/qml-patch/*.zip` 内含 `InteractDialog.qml`）。归档 `docs/灵动岛扩展-组件7与管理端审批面板-加减归档-2026-10-15.md`。P0–P3（设置扩充/自检向导/插件宿主/播放时段等）亦已上线（0.6.33 起）。
- ⏸ **隔离验证**：云端数据面全量复跑（2026-10-16 · 14 套 **245 过 / 0 败**，按用户边界排除关机/重启类 2 套）＋新增 **HTTP 审批路径** `tests/regression-interact-http.cjs`（17/17）。仍挂起的都是**真机类**：考试模式内核闸门实杀、#98 灵动岛观感、terminal cd、file_push。
- ⚠️ **待点单**：① 右栏/命令面板的**角色门控形同虚设**（`mayDo("admin")` 因权限表无 "admin" 字样恒真 ⇒ teacher 身份也能锁屏；U4 用 `quickGuard` 只保住了快捷行那道，**未擅自收敛**，见归档 §11.5）；② 真机验证清单（安装器实跑、exe 启动）；③ 出包发布（viewer 0.6.34 已被 webrtc 修复占用，本批叠加其上，**出包前须再 bump**）。

## 6 同步 / UI 模板
- 指挥台回写真源 `docs/星集控-指挥台-唯一真源-2026-10-02.md` §一；收工即回写（2026-10-11 虚报过，禁口头充数）；`.workbuddy/memory/` 是私有记忆 ≠ 同步。
- **Material 3 = 全项目唯一视觉模板**，令牌真源 `Stelarith-shared-qml/ui/Md3.qml`（U1 起两端共用）；组件库 `md3/` 12 个；legacy 别名桥已覆盖 `Theme.qml`（U1 已删）。
- **展示页位置约定**：HTML 展示页放 `docs/<中文名>.html`；**资源目录必须纯 ASCII**；探针截图源 `D:/Stelarith/_probe/shots/`，**只 cp 不 mv**。台账类汇总**不另立 md**（避免第 N 份「待办清单」分叉）。
- **探针驱动脚本**：viewer `.gitignore:34` 约定 `scripts/_*.py` **不入库**，`scripts/_*.qml` / `_*.cpp` **入库**。Bash 工具 cwd **不跨命令保持**，每次必须显式 `cd`。
- **固化成测试的成例**（照做）：`shared-qml/scripts/test-ui-contract.mjs`（契约 32 断 + 跨仓读 `pluginhost.cpp` 的漂移门禁）、`test-action-manifest.mjs`（23 断 + fixture 冻结基线）。fixture 在 `shared-qml/scripts/fixtures/`。

## 7 踩坑实证（删组件 / 迁移类，最贵）
- ⚠️⚠️ **删组件前判据必须是「类型名」而不是「路径」**（2026-10-16 U3 探针抓到 A 档活体回归）：只 grep 路径（`components/SvgIcon`）会**漏掉类型使用**（`SvgIcon {` 靠 `import "components"` 解析）⇒ `SvgIcon` 被全套面板 + Main.qml 在用，删完图标全废。
- ⚠️ **`qmllint` 不查「目录导入里的类型缺失」**：实测 `Main.qml` 输出 2491 行**全是 Warning、0 Error** ⇒ 给的是假安全感。**唯一可靠出口是运行时探针**（真实例化每个 entry / 每条声明）。
- ⚠️ **QML 隐式作用域是迁移的隐形地雷**：内联对象的**裸标识符**（`th`、`isPhone`）靠「子对象 → 组件根」隐式解析；搬成独立文件后那条链**无声断掉**（不报错）。抽取前必须**系统扫描根对象成员**，不能只看带 `root.` 前缀的引用（U3 初版漏了 87 处裸 `th`）。
- ⚠️ **`property var X: Y` 里 RHS 的裸标识符替换要防自引用**：`theme: th` 里的 `th` 换成 `theme` 会得到 `theme: theme` **绑定环**（取值位应用 `ctx.window.th`）。
- ⚠️ **`anchors.centerIn: parent` 依赖 parent 几何**：把内联对象搬到宿主里，宿主必须 `anchors.fill: parent`，否则全部挤到左上角。
- ⚠️ **往共享目录加「新扩展名」文件时必须查两端各自的收集方式**：桌面 `copy_directory` 照单全收、Android 是显式 GLOB（漏了就漏了）⇒ U2 加的 `.js` 只砸手机端。
- ⚠️ **同一动作的多个入口容易有"三种门控口径"**：U4 发现快捷行的 lock 比右栏多一道 `permOk("lock")`（因为 `mayDo("admin")` 恒真）——迁移时**必须逐字保留**，否则会变成安全倒退；想收敛要先报用户。
- ⚠️ **排序键要按"面"分开**：右栏要「通知」压顶、命令面板要「锁屏」排头 ⇒ 一个 `order` 满足不了，需 `order`/`quickOrder`/`palOrder`。
- ⚠️ **声明清单合并进同一注册表 ⇒ id 必须全局唯一**：动作 id 与面板 id 同名（`camera`/`file`/`software`）会被契约 `validate` 抓出（这是它该干的活）。
- ⚠️ **探针日志机制**：`_island_qml_probe` 启动时以 WriteOnly 截断 `_probe/shots/_probe_result.txt` ⇒ 每轮只留本块；纯 QML 改动不触发 POST_BUILD，探针前须手动 `cp` 到 `build/qml`（症状：dump 全 `<invalid>`）。
- ⚠️ **QML 匿名类名不是稳定标识**（2026-10-16 D 档）：`metaObject()->className()` 带 `_QML_21` / `_QMLTYPE_11` 序号，**同一份代码不同跑次也会变** ⇒ 凡以 class 名做基线比对的工具必须**先剥序号**；抽出的组件名用**显式 rename 映射**回匿名名（那是拆分**唯一允许**的差异）。
- ⚠️ **基线比对的非确定项要按成因分开处理**：时间源（时钟 `HH:mm`）内置忽略；**动画相位**（鲸鱼呼吸 `Image.y` 三连跑 −1.01 / −1.02 / −1.10）用显式 `--ignore-line` 排除。**都不许靠放大 `--tol` 糊过去**（会同时放过真实的亚像素偏移）。
- ⚠️ **"看着该生效"的探针参数，先验它真生效**：`_island_qml_probe.py --expand --actions` 实测 `root._actions.count = 0`（该参数**没把动作注入 QML**，既有探针缺口，非本轮引入）⇒ 差点把"零差异"误当"动作按钮可见态已验证"。
- ⚠️ **冻基线的正确姿势**：拆分后才发现某态没冻基线时，用 `git show HEAD:<path>` 取原版临时放回、跑完**立即 `cp` 恢复**，并用 `diff` 校验恢复完整 —— 比"回滚改动"安全，比"不验证"负责。
- ⚠️ **门禁正则要去注释再匹配**：`/kMinContentW/` 命中的是自己写的**注释**（那注释正在说明这个缺口）⇒ 假红。凡"检测代码特征"的门禁，先剥注释行。
- 其他（灵动岛组件 1-6 期间实证，仍有效）：① QML `implicitWidth` 从不直接生效，岛宽由 C++ `geometryForContent` 定；② **锚链对不可见 item 依然起效**（隐藏须同时 `height:0` 清零）；③ **探针演示态编号 ≠ QML 状态机编号必须映射**；④ `setProperty` 同步但锚链重算要等事件循环 ⇒ 注入与点击之间 +100ms；⑤ **动画/注入要写的属性不能锚**；⑥ **`Repeater` delegate 内的 `objectName` 不在 QObject 树上** ⇒ 探针走 `childItems()` 递归；⑦ **云端下发统一走指令信封**（`type='instruction'` + `payload.action`，不是扁平 `type:'notify'`）；⑧ **QML 祖先链 `enabled` 传导 ⇒ 禁用控件的子孙收不到事件**，穿透守卫只能放 `gestureArea` 单点。
